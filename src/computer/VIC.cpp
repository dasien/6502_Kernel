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
            const uint8_t value = screen_buffer_[cell()];
            advanceIndex();
            return value;
        }
        case kRegColor:
        {
            const uint8_t value = color_buffer_[cell()];
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
            // Partial update: set the low byte only, and do NOT wrap yet. The
            // still-stale high byte can push this intermediate value past the
            // screen size; wrapping it here would corrupt the address before the
            // high byte arrives. The wrap happens once the high byte is written.
            cell_index_ = (cell_index_ & 0xFF00) | value;
            break;
        case kRegAddrHi:
            cell_index_ = (cell_index_ & 0x00FF) | (static_cast<uint16_t>(value) << 8);
            if (cell_index_ >= kScreenSize) cell_index_ %= kScreenSize;
            break;
        case kRegChar:
        {
            // Full 8-bit CP437 code point. Reverse/bright/color all live in the
            // attribute plane (VREG_ATTR latch), so the whole byte is the glyph.
            screen_buffer_[cell()] = value;
            color_buffer_[cell()] = attr_latch_;
            advanceIndex();
            dirty_flag_ = true;
            break;
        }
        case kRegColor:
            color_buffer_[cell()] = value;
            advanceIndex();
            dirty_flag_ = true;
            break;
        case kRegAttr:
            attr_latch_ = value;
            break;
        case kRegCmdParam:
            cmd_param_ = value;
            break;
        case kRegScrollBot:
            scroll_bot_ = (value < kScreenHeight) ? value : (kScreenHeight - 1);
            break;
        case kRegCmd:
            switch (value)
            {
            case kCmdClear: cmdClear(); break;
            case kCmdScrollUp: cmdScrollUp(); break;
            case kCmdScrollDown: cmdScrollDown(); break;
            case kCmdFillRow: cmdFillRow(); break;
            case kCmdRowSize: cmdRowSize(); break;
            case kCmdRowsNormal: row_double_ = 0; dirty_flag_ = true; break;
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
        scroll_bot_ = kScreenHeight - 1;   // a clear also resets the scroll region
        row_double_ = 0;                   // ...and puts every row back to 8x16, so a
        dirty_flag_ = true;                // program cannot strand the shell at 16x32
    }

    // Flag one row double or normal. The row comes in the low bits of the command
    // parameter and bit 7 says which way, so setting up a playfield is one write pair
    // per row and needs no register of its own.
    void VIC::cmdRowSize()
    {
        const uint8_t row = cmd_param_ & kRowSizeMask;
        if (row >= kScreenHeight) return;
        const uint32_t bit = 1u << row;
        if (cmd_param_ & kRowSizeDouble) row_double_ |= bit;
        else                             row_double_ &= ~bit;
        dirty_flag_ = true;
    }

    bool VIC::isRowDouble(const uint16_t row) const
    {
        // The bottom row has no row beneath it to grow into, so it stays 8x16 whatever
        // the flag says -- the alternative is a half-drawn glyph hanging off the screen.
        if (row + 1 >= kScreenHeight) return false;
        return (row_double_ >> row) & 1u;
    }

    // Scroll/blank only the region rows 0..scroll_bot_ (default: whole screen), so
    // an app can pin footer rows (e.g. an input/status line) below the region.
    void VIC::cmdScrollUp()
    {
        const uint16_t regionEnd = (static_cast<uint16_t>(scroll_bot_) + 1) * kScreenWidth;
        for (uint16_t i = 0; i + kScreenWidth < regionEnd; ++i)
        {
            screen_buffer_[i] = screen_buffer_[i + kScreenWidth];
            color_buffer_[i] = color_buffer_[i + kScreenWidth];
        }
        for (uint16_t i = regionEnd - kScreenWidth; i < regionEnd; ++i)
        {
            screen_buffer_[i] = cmd_param_;
            color_buffer_[i] = attr_latch_;
        }
        shiftRowFlags(true);
        dirty_flag_ = true;
    }

    void VIC::cmdScrollDown()
    {
        const uint16_t regionEnd = (static_cast<uint16_t>(scroll_bot_) + 1) * kScreenWidth;
        for (uint16_t i = regionEnd; i-- > kScreenWidth;)
        {
            screen_buffer_[i] = screen_buffer_[i - kScreenWidth];
            color_buffer_[i] = color_buffer_[i - kScreenWidth];
        }
        for (uint16_t i = 0; i < kScreenWidth; ++i)
        {
            screen_buffer_[i] = cmd_param_;
            color_buffer_[i] = attr_latch_;
        }
        shiftRowFlags(false);
        dirty_flag_ = true;
    }

    // A row's size travels with its contents. Scrolling text past a double row and
    // leaving the flag behind would resize whatever line happened to land there.
    void VIC::shiftRowFlags(const bool up)
    {
        const uint32_t region = (scroll_bot_ + 1 >= 32)
                                    ? 0xFFFFFFFFu
                                    : ((1u << (scroll_bot_ + 1)) - 1u);
        const uint32_t inside = row_double_ & region;
        const uint32_t moved = up ? (inside >> 1) : ((inside << 1) & region);
        row_double_ = (row_double_ & ~region) | moved;
    }

    void VIC::cmdFillRow()
    {
        const uint16_t row_start = (cell() / kScreenWidth) * kScreenWidth;
        for (uint16_t x = 0; x < kScreenWidth; ++x)
        {
            screen_buffer_[row_start + x] = cmd_param_;
            color_buffer_[row_start + x] = attr_latch_;
        }
        dirty_flag_ = true;
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
