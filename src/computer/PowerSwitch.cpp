/**
 * @file PowerSwitch.cpp
 * @brief Soft power control for the MFC 6502.
 * @author 6502 Kernel Project
 */

#include "computer/PowerSwitch.h"

namespace Computer
{
    void PowerSwitch::write(const uint16_t address, const uint8_t value)
    {
        if (address != kRegPower) return;

        // Two writes, in order. Anything else is not the sequence and re-evaluates as
        // a fresh arm, so $5A $5A $A5 still works and a stray byte in the middle
        // simply loses the attempt rather than taking the machine down.
        if (armed_ && value == kFire)
        {
            off_ = true;
            armed_ = false;
            return;
        }
        armed_ = (value == kArm);
    }

    uint8_t PowerSwitch::read(const uint16_t) const
    {
        return 0x00;   // write-only
    }
}
