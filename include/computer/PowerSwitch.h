/**
 * @file PowerSwitch.h
 * @brief Soft power control for the MFC 6502.
 * @author 6502 Kernel Project
 */

#ifndef POWERSWITCH_H
#define POWERSWITCH_H

#include <cstdint>

namespace Computer
{
    /**
     * @class PowerSwitch
     * @brief The machine's soft power switch: one register that cuts the power.
     *
     * A companion to ResetCircuit. That one models the machine coming up; this one
     * models it going down, and the two are deliberately separate devices because
     * they are separate events.
     *
     * They are separate from the CPU halting, too. The 65C02's STP stops the
     * processor and its clock -- it was added for battery work, where the part sleeps
     * until a RESET wakes it -- so STP means "the processor is off" while the system
     * around it still has power. Powering the machine down is a system-level act, and
     * it belongs to a system-level device rather than to an opcode.
     *
     * No micro of this vintage could actually cut its own mains power; the switch was
     * on the case and you flipped it. What CP/M and early DOS machines with hard disks
     * did have was a PARK or SHIPDISK utility that made switching off SAFE -- heads
     * moved off the data area, then a message telling you to go ahead. MFC's SHUTDOWN
     * is that utility with the last step automated, which is the one liberty taken.
     *
     * ARMING. Power-off takes two writes, $5A then $A5, and any other value cancels a
     * half-entered sequence. A single magic byte would mean one wild pointer could
     * take the machine down, and soft-power and watchdog registers on real parts
     * routinely wanted an unlock sequence for exactly that reason.
     *
     * The register is write-only; reads return zero.
     *
     * @see ResetCircuit, Memory, Computer6502
     */
    class PowerSwitch
    {
    public:
        static constexpr uint16_t kRegPower = 0xFE61;  ///< the switch itself (W)
        static constexpr uint8_t kArm = 0x5A;          ///< first of the two writes
        static constexpr uint8_t kFire = 0xA5;         ///< ...and the second

        [[nodiscard]] static bool isPowerAddress(const uint16_t address)
        {
            return address == kRegPower;
        }

        void write(uint16_t address, uint8_t value);
        [[nodiscard]] uint8_t read(uint16_t address) const;

        /// Has the machine been switched off? The host polls this and closes.
        [[nodiscard]] bool isOff() const { return off_; }

        /// A reset abandons a half-entered sequence but cannot restore power -- there
        /// is nothing to reset once the power is gone.
        void reset() { armed_ = false; }

        /// Plugging it back in.
        void powerOn() { armed_ = false; off_ = false; }

    private:
        bool armed_ = false;
        bool off_ = false;
    };
}

#endif // POWERSWITCH_H
