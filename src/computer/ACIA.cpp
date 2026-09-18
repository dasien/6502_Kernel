/**
 * @file ACIA.cpp
 * @brief Emulated 6551 ACIA implementation.
 */

#include "computer/ACIA.h"

namespace Computer
{
    bool ACIA::isAciaAddress(uint16_t address)
    {
        return address >= kRegData && address <= kRegControl;
    }

    uint8_t ACIA::read(uint16_t address)
    {
        switch (address)
        {
        case kRegData:
            if (rx_.empty())
                return 0;
            {
                const uint8_t b = rx_.front();
                rx_.pop_front();
                return b;
            }
        case kRegStatus:
        {
            uint8_t status = kStatusTxEmpty; // TX is always ready in the model
            if (!rx_.empty())
                status |= kStatusRxFull;
            return status;
        }
        case kRegCommand:
            return command_;
        case kRegControl:
            return control_;
        default:
            return 0;
        }
    }

    void ACIA::write(uint16_t address, uint8_t value)
    {
        switch (address)
        {
        case kRegData:
            tx_.push_back(value);
            break;
        case kRegStatus:
            // A real 6551 treats a write to the status register as a programmed
            // reset (it clears the receiver). For the polled spike, drop any
            // pending received byte so a reset re-syncs the input.
            rx_.clear();
            break;
        case kRegCommand:
            command_ = value;
            break;
        case kRegControl:
            control_ = value;
            break;
        default:
            break;
        }
    }

    void ACIA::hostSend(uint8_t byte)
    {
        rx_.push_back(byte);
    }

    uint8_t ACIA::hostRecv()
    {
        if (tx_.empty())
            return 0;
        const uint8_t b = tx_.front();
        tx_.pop_front();
        return b;
    }
} // namespace Computer
