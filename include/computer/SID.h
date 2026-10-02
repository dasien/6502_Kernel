/**
 * @file SID.h
 * @brief Software sound chip modeled on the MOS 6581/8580 SID.
 * @author 6502 Kernel Project
 */

#ifndef SID_H
#define SID_H

#include <cstdint>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace Computer
{
    /**
     * @class SID
     * @brief A register-faithful software SID (6581/8580) sound chip.
     *
     * The real SID exposes 29 registers ($D400-$D41C). This model relocates that
     * exact register layout to the free I/O block at $FE38-$FE54 (just past the VIC
     * port) so SID knowledge and music transfer over directly. Three voices, each
     * with a 16-bit frequency, 12-bit pulse width, a control register (gate + four
     * waveforms), and an ADSR envelope; plus a master volume (the multimode filter
     * and OSC3/ENV3 read-back arrive in phase 2).
     *
     * The chip is headless (no Qt) and runs on MACHINE TIME, caught up LAZILY, as VICE
     * runs reSID. It is never stepped per instruction. Instead, a register write
     * first renders every sample up to the current cycle and only then takes the new
     * value, so the write lands at the sample it happened on; and the machine calls
     * clock() at the end of each run of instructions to render the rest. Either way
     * the chip synthesizes exactly the samples the elapsed time is worth -- 44,100 a
     * second -- from the registers as they stood during it.
     * The samples go into a buffer; the GUI's QAudioSink bridge drains it on the audio
     * thread (drainSamples) and never synthesizes anything itself.
     *
     * It used to be the other way round: the audio thread asked for a block whenever
     * the sound system wanted one, and the chip rendered the whole block from ONE
     * snapshot of its registers. The block size is the operating system's, tens of
     * milliseconds, so register changes were only noticed that often -- a smooth
     * pitch sweep came out as a run of short notes, and a sound that started and
     * stopped between two snapshots was never heard at all. On machine time the chip
     * reacts to a write when it happens, as a real SID does.
     *
     * Everything here runs on the emulation thread except drainSamples(), and the
     * sample buffer is the only thing the two threads share (ring_mtx_). If the buffer
     * runs dry -- the machine paused, or the emulation behind -- the audio thread holds
     * the last sample rather than clicking; if it overfills -- no audio device at all,
     * as in the tests -- the oldest samples are dropped, so neither memory nor latency
     * can grow.
     *
     * This synthesizer is written from scratch from public SID documentation. It is
     * musically faithful (register-compatible, familiar pitches) but not cycle-exact
     * -- envelopes use a float exponential approximation rather than the segmented
     * hardware counter, the filter is a Chamberlin state-variable filter, and
     * combined waveforms use a bitwise-AND approximation. Ring/sync modulation are
     * not modeled. No reSID (or other GPL) code is used.
     *
     * @see Memory, Computer6502, ACIA
     */
    class SID
    {
    public:
        // --- Register map: 29 registers at $FE38-$FE54, in real-SID order. ---
        static constexpr uint16_t kRegBase = 0xFE38;
        static constexpr int kNumRegs = 29;
        static constexpr uint16_t kRegLast = kRegBase + kNumRegs - 1; // $FE54

        // Per-voice register offsets (voice n base = n*7).
        static constexpr int kVoiceRegs = 7;
        static constexpr int kOffFreqLo = 0;
        static constexpr int kOffFreqHi = 1;
        static constexpr int kOffPwLo = 2;
        static constexpr int kOffPwHi = 3;   ///< low nibble = PW bits 8-11
        static constexpr int kOffControl = 4;
        static constexpr int kOffAttackDecay = 5;  ///< hi nibble attack, lo nibble decay
        static constexpr int kOffSustainRelease = 6; ///< hi nibble sustain, lo nibble release

        // Global registers (offsets from kRegBase).
        static constexpr int kRegFcLo = 21;    ///< filter cutoff low (bits 0-2)
        static constexpr int kRegFcHi = 22;    ///< filter cutoff high (8 bits)
        static constexpr int kRegResFilt = 23; ///< resonance (hi nibble) + routing (lo)
        static constexpr int kRegModeVol = 24; ///< filter mode (hi) + master volume (lo)
        static constexpr int kRegPotX = 25;    ///< paddle X (read-only, unused)
        static constexpr int kRegPotY = 26;    ///< paddle Y (read-only, unused)
        static constexpr int kRegOsc3 = 27;    ///< voice-3 oscillator read-back (RO)
        static constexpr int kRegEnv3 = 28;    ///< voice-3 envelope read-back (RO)

        // Control-register bits.
        static constexpr uint8_t kCtrlGate = 0x01;
        static constexpr uint8_t kCtrlSync = 0x02;
        static constexpr uint8_t kCtrlRing = 0x04;
        static constexpr uint8_t kCtrlTest = 0x08;
        static constexpr uint8_t kCtrlTriangle = 0x10;
        static constexpr uint8_t kCtrlSawtooth = 0x20;
        static constexpr uint8_t kCtrlPulse = 0x40;
        static constexpr uint8_t kCtrlNoise = 0x80;

        // RES_FILT ($FE4F+... i.e. kRegResFilt): low nibble routes voices 1-3
        // (bit0=v1, bit1=v2, bit2=v3, bit3=external) through the filter; high
        // nibble is resonance.
        static constexpr uint8_t kFiltVoice1 = 0x01;
        static constexpr uint8_t kFiltVoice2 = 0x02;
        static constexpr uint8_t kFiltVoice3 = 0x04;

        // MODE_VOL: low nibble = master volume; high nibble selects filter mode
        // and voice-3 disconnect.
        static constexpr uint8_t kModeLowPass = 0x10;
        static constexpr uint8_t kModeBandPass = 0x20;
        static constexpr uint8_t kModeHighPass = 0x40;
        static constexpr uint8_t kModeVoice3Off = 0x80;

        // Synthesis constants.
        static constexpr int kSampleRate = 44100;
        static constexpr double kSidClock = 1000000.0; ///< nominal SID master clock (Hz)
        static constexpr int kNumVoices = 3;

        SID();

        // --- Memory-mapped register port ($FE38-$FE54) ---
        [[nodiscard]] static bool isSidAddress(uint16_t address);
        [[nodiscard]] uint8_t read(uint16_t address) const;
        void write(uint16_t address, uint8_t value);

        // --- Machine time (emulation thread) ---
        /// The CPU clock, so a cycle count converts to samples. Without it clock()
        /// does nothing.
        void setClockHz(uint64_t hz)
        {
            // The part of a sample already accumulated is kept as a fraction of one, so
            // a change of speed (CPU_SPEED) neither drops nor invents a sample.
            if (clock_hz_ && hz) sample_acc_ = sample_acc_ * hz / clock_hz_;
            clock_hz_ = hz;
        }
        /// Where "now" comes from: the CPU's cycle count. A register write catches the
        /// chip up to it first. Without a source, writes simply apply (the tests that
        /// render directly with generateSamples work that way).
        void setClock(std::function<uint64_t()> now) { now_ = std::move(now); }
        /// Catch up to the CPU's cycle count @p now: synthesize the samples the time
        /// since the last call is worth, into the buffer.
        void clock(uint64_t now);
        /// The CPU's cycle count has started again from 0 (a reset). The next clock()
        /// takes its time as the new origin instead of subtracting the old one from
        /// it, which wraps and asks for ~2^64 cycles' worth of samples.
        void restartClock() { clocked_ = false; sample_acc_ = 0; }

        /**
         * @brief Synthesize @p frames mono 16-bit samples into @p out, now, from the
         *        current registers.
         *
         * The core that clock() runs; tests also call it directly, to render a known
         * stretch of sound without a machine around the chip.
         */
        void generateSamples(int16_t *out, int frames);

        // --- Audio backend interface (the audio thread) ---
        /// Take up to @p frames buffered samples, exactly as rendered; returns how many
        /// there were. The tests read the buffer through this.
        int drainSamples(int16_t *out, int frames);

        /// Fill @p frames samples for the sound system -- always all of them: what the
        /// buffer holds, then the last sample held if it is short.
        void playback(int16_t *out, int frames);

        /// Samples waiting in the buffer.
        [[nodiscard]] int buffered() const;
        /// What playback() has had to do: samples padded because the buffer was empty,
        /// and samples thrown away because it overflowed. For MFC_AUDIO_LOG.
        [[nodiscard]] uint64_t underrunSamples() const { return underruns_.load(); }
        [[nodiscard]] uint64_t droppedSamples() const { return dropped_.load(); }
        /// Samples the sound system has taken, in all. The machine watches it to tell a
        /// live audio device from a stuck one.
        [[nodiscard]] uint64_t drainedSamples() const { return drained_.load(); }
        /// The largest request the sound system has made, in samples. For MFC_AUDIO_LOG:
        /// it was 8192 (186 ms) while SidAudio's device was a buffered QIODevice, which
        /// topped every small read up to its own 16 KB chunk.
        [[nodiscard]] int largestRequest() const { return largest_request_.load(); }

        /// The buffer's bounds, in samples. Past kRingMax the oldest are dropped, back
        /// to kRingKeep, which bounds the delay between a write and hearing it.
        static constexpr int kRingMax = 6144;    ///< ~140 ms
        static constexpr int kRingKeep = 2048;   ///< ~46 ms

        /// @brief Reset all registers and synthesis state (silence).
        void reset();

    private:
        // Envelope state machine (one per voice).
        enum class EnvPhase { Idle, Attack, Decay, Release };

        struct Voice
        {
            double phase = 0.0;   ///< oscillator phase in [0,1)
            uint32_t lfsr = 0x7FFFFF; ///< 23-bit noise shift register
            double noise_phase = 0.0; ///< accumulator for noise clocking
            double noise_level = 0.0; ///< current sample-and-hold noise output
            EnvPhase env_phase = EnvPhase::Idle;
            double env = 0.0;     ///< envelope level [0,1]
            bool last_gate = false;
            /// The gate was written clear since the envelope last looked. The envelope
            /// runs once a sample, about every 90 cycles, so a gate cleared and set
            /// again a few instructions later -- how every player restarts a note --
            /// would otherwise never be seen closed, and the note would not restart.
            /// A real SID is clocked with the CPU and does see it.
            bool gate_dropped = false;
        };

        // Register array. Indexed 0..kNumRegs-1. Emulation thread only.
        std::array<uint8_t, kNumRegs> regs_{};

        // Machine time.
        std::function<uint64_t()> now_;
        uint64_t clock_hz_ = 0;
        uint64_t last_cycle_ = 0;
        bool clocked_ = false;                 ///< last_cycle_ has been set
        uint64_t sample_acc_ = 0;              ///< cycles*kSampleRate not yet a sample

        // Samples rendered, waiting for the audio thread: a ring, guarded by ring_mtx_.
        // pending_ collects a few on the emulation thread first, so the lock is taken
        // every 64 samples rather than every one.
        std::vector<int16_t> ring_ = std::vector<int16_t>(kRingMax);
        int ring_head_ = 0, ring_count_ = 0;
        mutable std::mutex ring_mtx_;
        std::array<int16_t, 64> pending_{};
        int pending_count_ = 0;
        void flushPending();
        int16_t last_out_ = 0;                 ///< playback()'s last sample, for padding
        std::atomic<uint64_t> underruns_{0}, dropped_{0}, drained_{0};
        std::atomic<int> largest_request_{0};

        // Synthesis state.
        std::array<Voice, kNumVoices> voices_{};
        double filt_ic1_ = 0.0;   ///< TPT state-variable filter integrator 1 state
        double filt_ic2_ = 0.0;   ///< TPT state-variable filter integrator 2 state

        // Voice-3 read-back for the OSC3/ENV3 read-only registers.
        std::atomic<uint8_t> osc3_val_{0};
        std::atomic<uint8_t> env3_val_{0};

        // Per-voice synthesis helpers (operate on a register snapshot).
        static double oscillatorOutput(Voice &v, const uint8_t *vr);
        static void advanceEnvelope(Voice &v, const uint8_t *vr);
    };
} // namespace Computer

#endif // SID_H
