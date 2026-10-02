/**
 * @file TimingCircuit.cpp
 * @brief The clock's speed latch.
 */

#include "TimingCircuit.h"

namespace Computer
{
    void TimingCircuit::write(const uint16_t address, const uint8_t value)
    {
        if (!isSpeedAddress(address))
            return;
        const bool slow = (value & kSpeedSlow) != 0;
        if (slow == slow_)
            return;                 // no change: nothing to re-derive
        slow_ = slow;
        apply();
    }

    uint8_t TimingCircuit::read(const uint16_t address) const
    {
        if (!isSpeedAddress(address))
            return 0x00;
        return slow_ ? kSpeedSlow : kSpeedFull;
    }

    void TimingCircuit::reset()
    {
        if (!slow_)
            return;
        slow_ = false;
        apply();
    }

    void TimingCircuit::apply()
    {
        if (on_change_)
            on_change_(slow_ ? kSlowHz : full_hz_);
    }
} // namespace Computer
