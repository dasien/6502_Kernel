/**
 * @file TimingCircuit.h
 * @brief The clock: the machine's speed, and the switch that slows it.
 * @author 6502 Kernel Project
 */

#ifndef TIMINGCIRCUIT_H
#define TIMINGCIRCUIT_H

#include <cstdint>
#include <functional>

namespace Computer
{
    /**
     * @class TimingCircuit
     * @brief The clock's speed latch, CPU_SPEED at $FED4.
     *
     * The machine runs at 4 MHz. Write 1 here and it runs at 1 MHz until 0 is
     * written back; read it to find out which. This is the slow switch Apple II
     * accelerator cards and the IIgs had, and for the same reason: software that
     * times itself by counting cycles -- S.A.M.'s speech loops are the case in
     * point -- was written for a 1 MHz part and plays four times too fast at four.
     *
     * The latch does not keep time itself. It tells Computer6502, which re-derives
     * everything from the new rate (setClockHz): the jiffy period, the raster's
     * frame, the SID's samples, and the host's real-time budget. So at 1 MHz the CPU
     * really does a quarter of the work per second while the timer still ticks 60
     * times a second and the display and sound stay in real time.
     *
     * A reset clears it, as a reset line clears a latch: a crash or a reset can
     * never leave the machine slow.
     */
    class TimingCircuit
    {
    public:
        static constexpr uint16_t kRegSpeed = 0xFED4;   ///< CPU_SPEED (R/W)
        static constexpr uint8_t kSpeedFull = 0x00;     ///< 4 MHz
        static constexpr uint8_t kSpeedSlow = 0x01;     ///< 1 MHz
        static constexpr uint32_t kSlowHz = 1'000'000;  ///< a classic 6502's clock

        [[nodiscard]] static bool isSpeedAddress(const uint16_t address)
        {
            return address == kRegSpeed;
        }

        /// Where a change of speed goes: Computer6502::setClockHz.
        void setOnChange(std::function<void(uint32_t)> on_change) { on_change_ = std::move(on_change); }
        /// The full-speed clock, the one a write of 0 restores.
        void setFullHz(const uint32_t hz) { full_hz_ = hz; }

        /// Bit 0 selects the speed; the other bits are ignored, and read back as 0.
        void write(uint16_t address, uint8_t value);
        [[nodiscard]] uint8_t read(uint16_t address) const;

        /// Back to full speed (power-on and reset).
        void reset();

        [[nodiscard]] bool isSlow() const { return slow_; }

    private:
        void apply();

        std::function<void(uint32_t)> on_change_;
        uint32_t full_hz_ = 4'000'000;
        bool slow_ = false;
    };
} // namespace Computer

#endif // TIMINGCIRCUIT_H
