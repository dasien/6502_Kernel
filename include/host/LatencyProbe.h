/**
 * @file LatencyProbe.h
 * @brief Measures how long a key press or release takes to reach the screen.
 *
 * Off unless MFC_LATENCY is set in the environment, so it costs one branch per
 * hook otherwise. Set it to a file path to log there, or to 1 for stderr. A file
 * is the reliable choice: each line is flushed as it is written, so nothing is
 * lost if the process is killed, where a pipe to grep loses whatever grep had
 * buffered. When on, every change to the held-key state is followed along the
 * whole path and reported as one line:
 *
 *   key      the host's key event arrives
 *   read     the program first reads $FE0F and sees the new state
 *   move     sprite 0 -- the player in VENTURE and KPANIC -- changes position
 *   paint    the first paintEvent after that begins
 *   painted  that paint ends, and the frame is handed to the compositor
 *
 * The gaps between them separate the three owners of the delay. key->read is the
 * program's polling cadence, read->move is its game logic, move->painted is the
 * host. What happens after `painted` -- the compositor and the panel -- cannot be
 * seen from inside the application and is not included.
 *
 * A release is measured to the LAST movement rather than the first, because what
 * a player feels on letting go is how long the sprite keeps moving. It is reported
 * once the sprite has been still for kSettleFrames frames.
 *
 * Everything runs on the Qt main thread, so there is no locking.
 */

#ifndef HOST_LATENCY_PROBE_H
#define HOST_LATENCY_PROBE_H

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace Host
{
    class LatencyProbe
    {
    public:
        static LatencyProbe &get()
        {
            static LatencyProbe probe;
            return probe;
        }

        [[nodiscard]] bool on() const { return on_; }

        /// The host changed the held-key mask. A mask with more bits than before
        /// is a press, fewer is a release.
        void keyChanged(uint8_t mask)
        {
            if (!on_) return;
            const bool press = popcount(mask) > popcount(prev_mask_);
            prev_mask_ = mask;
            press_ = press;
            expect_ = mask;
            t_key_ = now();
            t_read_ = t_move_ = t_paint_ = t_painted_ = 0;
            paint_pending_ = painted_pending_ = false;
            quiet_frames_ = wait_frames_ = 0;
            stage_ = Stage::WaitRead;
        }

        /// The program read $FE0F.
        void keyStateRead(uint8_t value)
        {
            if (!on_ || stage_ != Stage::WaitRead || value != expect_) return;
            t_read_ = now();
            stage_ = press_ ? Stage::WaitMove : Stage::Settling;
        }

        /// Sprite 0's position changed.
        void playerMoved()
        {
            if (!on_) return;
            if (stage_ == Stage::WaitMove)
            {
                t_move_ = now();
                stage_ = Stage::WaitPaint;
            }
            else if (stage_ == Stage::Settling)
            {
                t_move_ = now();            // the latest movement so far
                paint_pending_ = true;      // the paint that shows it is still owed
                quiet_frames_ = 0;
            }
        }

        void paintBegin()
        {
            if (!on_) return;
            if (stage_ == Stage::WaitPaint)
            {
                t_paint_ = now();
                stage_ = Stage::InPaint;
            }
            else if (stage_ == Stage::Settling && paint_pending_)
            {
                t_paint_ = now();
                paint_pending_ = false;
                painted_pending_ = true;
            }
        }

        void paintEnd()
        {
            if (!on_) return;
            if (stage_ == Stage::InPaint)
            {
                t_painted_ = now();
                report();
            }
            else if (stage_ == Stage::Settling && painted_pending_)
            {
                t_painted_ = now();
                painted_pending_ = false;
            }
        }

        /// One emulated frame went by. Ends a release once the sprite is still,
        /// and gives up on a press that never moves anything (a wall, a menu).
        void frame()
        {
            if (!on_) return;
            if (stage_ == Stage::Settling)
            {
                if (++quiet_frames_ >= kSettleFrames && !paint_pending_ && !painted_pending_)
                    report();
            }
            else if (stage_ == Stage::WaitMove || stage_ == Stage::WaitRead)
            {
                if (++wait_frames_ >= kGiveUpFrames)
                {
                    log("[latency] %s  no response within %d frames\n",
                        press_ ? "press  " : "release", kGiveUpFrames);
                    stage_ = Stage::Idle;
                }
            }
        }

        /// One execution-timer callback: how long since the last one, and how
        /// many cycles it ran. The timer is nominally 1 ms, but what matters is how
        /// often it really fires, because the machine only advances -- and the
        /// screen only changes -- when it does. Summarised every five seconds so a
        /// killed process still leaves its figures behind.
        void slice(double interval_ms, uint64_t cycles)
        {
            if (!on_) return;
            const double t = now();
            if (slice_window_start_ == 0) slice_window_start_ = t;
            int b = interval_ms < 2 ? 0 : interval_ms < 5 ? 1 : interval_ms < 10 ? 2
                  : interval_ms < 20 ? 3 : interval_ms < 40 ? 4 : 5;
            bucket_[b]++;
            slice_n_++;
            slice_ms_ += interval_ms;
            slice_cycles_ += static_cast<double>(cycles);
            if (t - slice_window_start_ >= 5000.0)
            {
                log("[latency] timer, last %.1f s: %d callbacks, interval ms  <2:%d  2-5:%d  "
                    "5-10:%d  10-20:%d  20-40:%d  >40:%d  mean %.1f ms, %.0f cycles each\n",
                    (t - slice_window_start_) / 1000.0, slice_n_, bucket_[0], bucket_[1],
                    bucket_[2], bucket_[3], bucket_[4], bucket_[5], slice_ms_ / slice_n_,
                    slice_cycles_ / slice_n_);
                slice_window_start_ = t;
                slice_n_ = 0;
                slice_ms_ = slice_cycles_ = 0;
                for (int &c : bucket_) c = 0;
            }
        }

    private:
        enum class Stage { Idle, WaitRead, WaitMove, WaitPaint, InPaint, Settling };

        static constexpr int kSettleFrames = 20;    // a third of a second of stillness
        static constexpr int kGiveUpFrames = 120;   // two seconds

        LatencyProbe()
        {
            const char *dest = std::getenv("MFC_LATENCY");
            if (!dest) return;
            on_ = true;
            out_ = stderr;
            if (dest[0] != '\0' && !(dest[0] == '1' && dest[1] == '\0'))
            {
                if (std::FILE *f = std::fopen(dest, "a")) out_ = f;
            }
            log("[latency] probe on; watching sprite 0. Figures stop at the paint -- "
                "the compositor and panel are extra.\n");
        }

        // Every line goes through here so every line is flushed. The attribute
        // keeps the compiler checking each format against its arguments.
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        void log(const char *fmt, ...)
        {
            va_list ap;
            va_start(ap, fmt);
            std::vfprintf(out_, fmt, ap);
            va_end(ap);
            std::fflush(out_);
        }

        static double now()
        {
            using namespace std::chrono;
            return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
        }

        static int popcount(uint8_t v)
        {
            int n = 0;
            for (; v; v &= static_cast<uint8_t>(v - 1)) n++;
            return n;
        }

        void report()
        {
            const double read = t_read_ - t_key_;
            if (t_move_ == 0)
            {
                // A release the program acted on at once: nothing moved afterwards.
                log("[latency] release  wait-for-read %6.1f ms | stopped at once, nothing "
                    "moved after the release\n", read);
            }
            else
            {
                const double game = t_move_ - t_read_;
                const double host = t_paint_ - t_move_;
                const double paint = t_painted_ - t_paint_;
                const double total = t_painted_ - t_key_;
                const int k = press_ ? 0 : 1;
                n_[k]++;
                sum_[k] += total;
                log("[latency] %s  wait-for-read %6.1f | %s %6.1f | host-out %6.1f "
                    "| paint %4.1f | key->painted %6.1f ms  (mean %6.1f over %d)\n",
                    press_ ? "press  " : "release", read,
                    press_ ? "game      " : "kept-going", game, host, paint, total,
                    sum_[k] / n_[k], n_[k]);
            }
            stage_ = Stage::Idle;
        }

        bool on_ = false;
        std::FILE *out_ = nullptr;
        Stage stage_ = Stage::Idle;
        bool press_ = false;
        uint8_t prev_mask_ = 0;
        uint8_t expect_ = 0;
        double t_key_ = 0, t_read_ = 0, t_move_ = 0, t_paint_ = 0, t_painted_ = 0;
        bool paint_pending_ = false, painted_pending_ = false;
        int quiet_frames_ = 0, wait_frames_ = 0;
        int n_[2] = {0, 0};             // [0] presses, [1] releases
        double slice_window_start_ = 0;
        int slice_n_ = 0;
        int bucket_[6] = {0, 0, 0, 0, 0, 0};
        double slice_ms_ = 0, slice_cycles_ = 0;
        double sum_[2] = {0, 0};
    };
}

#endif
