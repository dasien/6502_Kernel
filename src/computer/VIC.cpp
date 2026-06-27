#include "VIC.h"

namespace Computer
{
    VIC::VIC() : cursor_x_(0), cursor_y_(0), dirty_flag_(false)
    {
        clearScreen();
    }

    // ---------------------------------------------------------------------
    // Register port ($FE2D-$FE36) -- the real interface to the planes.
    // ---------------------------------------------------------------------

    bool VIC::isVideoRegAddress(const uint16_t address)
    {
        return address >= kRegFirst && address <= kRegLast;
    }

    void VIC::advanceIndex() const
    {
        ++cell_index_;
        if (cell_index_ >= kScreenSize)
        {
            cell_index_ = 0;
        }
    }

    uint8_t VIC::read(const uint16_t address) const
    {
        switch (address)
        {
        case kRegChar:
        {
            const uint8_t value = screen_buffer_[cell_index_];
            advanceIndex();
            return value;
        }
        case kRegColor:
        {
            const uint8_t value = color_buffer_[cell_index_];
            advanceIndex();
            return value;
        }
        case kRegStatus:
            return 0x00; // synchronous command engine: always ready
        case kRegAddrLo:
            return static_cast<uint8_t>(cell_index_ & 0xFF);
        case kRegAddrHi:
            return static_cast<uint8_t>((cell_index_ >> 8) & 0xFF);
        default:
            return 0x00; // write-only registers read as 0
        }
    }

    void VIC::write(const uint16_t address, const uint8_t value)
    {
        switch (address)
        {
        case kRegAddrLo:
            cell_index_ = (cell_index_ & 0xFF00) | value;
            if (cell_index_ >= kScreenSize) cell_index_ %= kScreenSize;
            break;
        case kRegAddrHi:
            cell_index_ = (cell_index_ & 0x00FF) | (static_cast<uint16_t>(value) << 8);
            if (cell_index_ >= kScreenSize) cell_index_ %= kScreenSize;
            break;
        case kRegChar:
        {
            // Store a clean 7-bit glyph; legacy char-bit7 becomes the reverse bit
            // in the color plane, otherwise the cell takes the current latch.
            screen_buffer_[cell_index_] = value & 0x7F;
            uint8_t attr = attr_latch_;
            if (value & 0x80) attr |= kAttrReverse;
            color_buffer_[cell_index_] = attr;
            advanceIndex();
            dirty_flag_ = true;
            break;
        }
        case kRegColor:
            color_buffer_[cell_index_] = value;
            advanceIndex();
            dirty_flag_ = true;
            break;
        case kRegAttr:
            attr_latch_ = value;
            break;
        case kRegCmdParam:
            cmd_param_ = value;
            break;
        case kRegCmd:
            switch (value)
            {
            case kCmdClear: cmdClear(); break;
            case kCmdScrollUp: cmdScrollUp(); break;
            case kCmdScrollDown: cmdScrollDown(); break;
            case kCmdFillRow: cmdFillRow(); break;
            default: break;
            }
            break;
        case kRegCursorLo:
            cursor_index_ = (cursor_index_ & 0xFF00) | value;
            dirty_flag_ = true;
            break;
        case kRegCursorHi:
            cursor_hidden_ = (value & kCursorHiddenBit) != 0;
            cursor_index_ = (cursor_index_ & 0x00FF) |
                            (static_cast<uint16_t>(value & ~kCursorHiddenBit) << 8);
            dirty_flag_ = true;
            break;
        default:
            break;
        }
    }

    void VIC::cmdClear()
    {
        screen_buffer_.fill(cmd_param_);
        color_buffer_.fill(attr_latch_);
        dirty_flag_ = true;
    }

    void VIC::cmdScrollUp()
    {
        for (uint16_t i = 0; i < kScreenSize - kScreenWidth; ++i)
        {
            screen_buffer_[i] = screen_buffer_[i + kScreenWidth];
            color_buffer_[i] = color_buffer_[i + kScreenWidth];
        }
        for (uint16_t i = kScreenSize - kScreenWidth; i < kScreenSize; ++i)
        {
            screen_buffer_[i] = cmd_param_;
            color_buffer_[i] = attr_latch_;
        }
        dirty_flag_ = true;
    }

    void VIC::cmdScrollDown()
    {
        for (uint16_t i = kScreenSize; i-- > kScreenWidth;)
        {
            screen_buffer_[i] = screen_buffer_[i - kScreenWidth];
            color_buffer_[i] = color_buffer_[i - kScreenWidth];
        }
        for (uint16_t i = 0; i < kScreenWidth; ++i)
        {
            screen_buffer_[i] = cmd_param_;
            color_buffer_[i] = attr_latch_;
        }
        dirty_flag_ = true;
    }

    void VIC::cmdFillRow()
    {
        const uint16_t row_start = (cell_index_ / kScreenWidth) * kScreenWidth;
        for (uint16_t x = 0; x < kScreenWidth; ++x)
        {
            screen_buffer_[row_start + x] = cmd_param_;
            color_buffer_[row_start + x] = attr_latch_;
        }
        dirty_flag_ = true;
    }

    // ---------------------------------------------------------------------
    // Legacy 40-column memory-mapped window (transitional compat shim).
    // Maps a $0400-based 40-col offset into the top-left 40 columns of the
    // 80-col character plane so an unmodified kernel keeps rendering.
    // ---------------------------------------------------------------------

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
        const uint16_t old_offset = address - kScreenMemoryStart; // 0..999 (40-col)
        const uint16_t x = old_offset % kCompatWidth;
        const uint16_t y = old_offset / kCompatWidth;
        if (y < kScreenHeight)
        {
            screen_buffer_[y * kScreenWidth + x] = value;
            dirty_flag_ = true;
        }
    }

    uint8_t VIC::readScreen(const uint16_t address) const
    {
        if (!isScreenAddress(address))
        {
            return 0x00;
        }
        const uint16_t old_offset = address - kScreenMemoryStart;
        const uint16_t x = old_offset % kCompatWidth;
        const uint16_t y = old_offset / kCompatWidth;
        if (y < kScreenHeight)
        {
            return screen_buffer_[y * kScreenWidth + x];
        }
        return 0x00;
    }

    // ---------------------------------------------------------------------
    // Display buffer access (host renderer).
    // ---------------------------------------------------------------------

    const std::array<uint8_t, VIC::kScreenSize> &VIC::getScreenBuffer() const
    {
        return screen_buffer_;
    }

    const std::array<uint8_t, VIC::kScreenSize> &VIC::getColorBuffer() const
    {
        return color_buffer_;
    }

    uint8_t VIC::getCharacterAt(const uint16_t x, const uint16_t y) const
    {
        if (x >= kScreenWidth || y >= kScreenHeight)
        {
            return 0x00;
        }
        return screen_buffer_[coordinatesToOffset(x, y)];
    }

    uint8_t VIC::getColorAt(const uint16_t x, const uint16_t y) const
    {
        if (x >= kScreenWidth || y >= kScreenHeight)
        {
            return kDefaultAttr;
        }
        return color_buffer_[coordinatesToOffset(x, y)];
    }

    void VIC::setCharacterAt(const uint16_t x, const uint16_t y, const uint8_t character)
    {
        if (x >= kScreenWidth || y >= kScreenHeight)
        {
            return;
        }
        screen_buffer_[coordinatesToOffset(x, y)] = character;
        dirty_flag_ = true;
    }

    void VIC::getCursorCell(uint16_t &index, bool &hidden) const
    {
        index = cursor_index_;
        hidden = cursor_hidden_;
    }

    // ---------------------------------------------------------------------
    // Screen operations.
    // ---------------------------------------------------------------------

    void VIC::clearScreen(const uint8_t fill_char)
    {
        screen_buffer_.fill(fill_char);
        color_buffer_.fill(kDefaultAttr);
        cursor_x_ = 0;
        cursor_y_ = 0;
        cell_index_ = 0;
        dirty_flag_ = true;
    }

    void VIC::scrollUp()
    {
        cmdScrollUp();
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
