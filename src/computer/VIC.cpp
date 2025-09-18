#include "VIC.h"
#include <iostream>
#include <cstdio>

namespace Computer
{
    VIC::VIC() : cursor_x_(0), cursor_y_(0), dirty_flag_(false)
    {
        clearScreen();
    }

    bool VIC::isScreenAddress(const uint16_t address) const
    {
        return address >= kScreenMemoryStart && address <= kScreenMemoryEnd;
    }

    void VIC::writeScreen(const uint16_t address, const uint8_t value)
    {
        if (!isScreenAddress(address))
        {
            return;
        }

        const uint16_t offset = addressToOffset(address);
        if (offset < kScreenSize)
        {
            // Only log non-space characters to reduce noise
            if (value != 0x20)
            {
                printf("VIC: Writing '%c' (0x%02X) to screen offset %d (addr $%04X)\n",
                       (value >= 32 && value <= 126) ? value : '?', value, offset, address);
            }
            screen_buffer_[offset] = value;
            dirty_flag_ = true;
        }
    }

    uint8_t VIC::readScreen(const uint16_t address) const
    {
        if (!isScreenAddress(address))
        {
            return 0x00;
        }

        const uint16_t offset = addressToOffset(address);

        if (offset < kScreenSize)
        {
            return screen_buffer_[offset];
        }

        return 0x00;
    }

    const std::array<uint8_t, VIC::kScreenSize> &VIC::getScreenBuffer() const
    {
        return screen_buffer_;
    }

    uint8_t VIC::getCharacterAt(const uint16_t x, const uint16_t y) const
    {
        if (x >= kScreenWidth || y >= kScreenHeight)
        {
            return 0x00;
        }

        const uint16_t offset = coordinatesToOffset(x, y);
        return screen_buffer_[offset];
    }

    void VIC::setCharacterAt(const uint16_t x, const uint16_t y, const uint8_t character)
    {
        if (x >= kScreenWidth || y >= kScreenHeight)
        {
            return;
        }

        const uint16_t offset = coordinatesToOffset(x, y);
        screen_buffer_[offset] = character;
        dirty_flag_ = true;
    }

    void VIC::clearScreen(const uint8_t fill_char)
    {
        screen_buffer_.fill(fill_char);
        cursor_x_ = 0;
        cursor_y_ = 0;
        dirty_flag_ = true;
    }

    void VIC::scrollUp()
    {
        // Move all lines up by one
        for (uint16_t y = 0; y < kScreenHeight - 1; ++y)
        {
            for (uint16_t x = 0; x < kScreenWidth; ++x)
            {
                const uint16_t current_offset = coordinatesToOffset(x, y);
                const uint16_t next_line_offset = coordinatesToOffset(x, y + 1);
                screen_buffer_[current_offset] = screen_buffer_[next_line_offset];
            }
        }

        // Clear the bottom line
        for (uint16_t x = 0; x < kScreenWidth; ++x)
        {
            const uint16_t bottom_offset = coordinatesToOffset(x, kScreenHeight - 1);
            screen_buffer_[bottom_offset] = 0x20; // Space character
        }

        dirty_flag_ = true;
    }

    void VIC::setCursorPosition(const uint16_t x, const uint16_t y)
    {
        if (x < kScreenWidth && y < kScreenHeight)
        {
            cursor_x_ = x;
            cursor_y_ = y;
        }
    }

    void VIC::getCursorPosition(uint16_t &x, uint16_t &y) const
    {
        x = cursor_x_;
        y = cursor_y_;
    }

    bool VIC::isDirty() const
    {
        return dirty_flag_;
    }

    void VIC::clearDirty()
    {
        dirty_flag_ = false;
    }

    uint16_t VIC::addressToOffset(const uint16_t address) const
    {
        return address - kScreenMemoryStart;
    }

    uint16_t VIC::coordinatesToOffset(const uint16_t x, const uint16_t y) const
    {
        return y * kScreenWidth + x;
    }

    void VIC::offsetToCoordinates(const uint16_t offset, uint16_t &x, uint16_t &y) const
    {
        y = offset / kScreenWidth;
        x = offset % kScreenWidth;
    }
} // namespace Computer
