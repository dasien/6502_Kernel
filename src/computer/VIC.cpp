#include "VIC.h"
#include "Cp437Font.h"

#include <algorithm>

namespace Computer
{
    VIC::VIC() : cursor_x_(0), cursor_y_(0), dirty_flag_(false)
    {
        font_ram_.resize(kFontRamSize);
        seedFontRam();
        clearScreen();
    }

    // Every set starts as a copy of the CP437 ROM, so a program that redefines a
    // handful of glyphs keeps readable text everywhere else -- and cannot leave the
    // shell with an unreadable font however badly it exits.
    void VIC::seedFontRam()
    {
        for (uint8_t set = 0; set < kFontSets; ++set)
        {
            std::copy_n(kCp437Font, kFontSize,
                        font_ram_.begin() + static_cast<size_t>(set) * kFontSize);
        }
    }

    const uint8_t *VIC::glyphRows(const uint8_t glyph) const
    {
        if (!font_ram_active_)
        {
            return &kCp437Font[static_cast<size_t>(glyph) * kGlyphBytes];
        }
        return &font_ram_[static_cast<size_t>(font_set_) * kFontSize +
                          static_cast<size_t>(glyph) * kGlyphBytes];
    }

    // ---------------------------------------------------------------------
    // Register port ($FE2D-$FE36) -- the real interface to the planes.
    // ---------------------------------------------------------------------

    // Three ranges: the original port block, then the soft-font port and the sprite
    // registers, which had to go above the RTC because $FE38 (the SID) sits
    // immediately after the first block.
    bool VIC::isVideoRegAddress(const uint16_t address)
    {
        return (address >= kRegFirst && address <= kRegLast) ||
               (address >= kRegFontFirst && address <= kRegFontLast) ||
               (address >= kRegSpriteFirst && address <= kRegSpriteLast);
    }

    const VIC::Sprite &VIC::sprite(const uint8_t index) const
    {
        return sprites_[index < kSpriteCount ? index : 0];
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
        case kRegFontData:
        {
            const uint8_t value = font_ram_[font_index_ % kFontRamSize];
            font_index_ = (font_index_ + 1) % kFontRamSize;
            return value;
        }
        case kRegFontLo:
            return static_cast<uint8_t>(font_index_ & 0xFF);
        case kRegFontHi:
            return static_cast<uint8_t>((font_index_ >> 8) & 0xFF);
        default:
            if (address >= kRegSpriteFirst && address <= kRegSpriteLast)
            {
                const uint8_t i = static_cast<uint8_t>((address - kRegSpriteFirst) / kSpriteStride);
                const Sprite &sp = sprites_[i];
                switch ((address - kRegSpriteFirst) % kSpriteStride)
                {
                case kSprXLo:  return static_cast<uint8_t>(sp.x & 0xFF);
                case kSprXHi:  return static_cast<uint8_t>(((sp.x >> 8) & 0x03) |
                                   ((sp.w - 1) << kSprSizeShift) |
                                   (sp.magx ? kSprMagX : 0));
                case kSprYLo:  return static_cast<uint8_t>(sp.y & 0xFF);
                case kSprYHi:  return static_cast<uint8_t>(((sp.y >> 8) & 0x03) |
                                                           ((sp.h - 1) << kSprSizeShift) |
                                                           (sp.magy ? kSprMagY : 0) |
                                                           (sp.enabled ? kSprEnable : 0));
                case kSprGlyph: return sp.glyph;
                default:        return sp.attr;
                }
            }
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
        // Font index: unlike the cell index this wraps on each byte, because it has
        // no equivalent of the low/high ordering hazard -- the data port is the only
        // thing that consumes it.
        case kRegFontLo:
            font_index_ = (font_index_ & 0xFF00u) | value;
            break;
        case kRegFontHi:
            font_index_ = ((font_index_ & 0x00FFu) |
                           (static_cast<uint32_t>(value) << 8)) % kFontRamSize;
            break;
        case kRegFontData:
            font_ram_[font_index_ % kFontRamSize] = value;
            font_index_ = (font_index_ + 1) % kFontRamSize;
            dirty_flag_ = true;
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
            case kCmdScrollTop:
                scroll_top_ = (cmd_param_ < kScreenHeight) ? cmd_param_ : (kScreenHeight - 1);
                break;
            case kCmdFineY:
                // Clamped rather than ignored: unlike a font set, an out-of-range
                // offset has an obvious right answer (the furthest the region can
                // slide), and a game computing it from a tick counter is the normal
                // way to get one.
                fine_y_ = (cmd_param_ < 32) ? cmd_param_ : 31;
                fine_active_ = true;
                dirty_flag_ = true;
                break;
            case kCmdFontRom: font_ram_active_ = false; dirty_flag_ = true; break;
            case kCmdFontRam: font_ram_active_ = true;  dirty_flag_ = true; break;
            case kCmdFontReset: seedFontRam(); dirty_flag_ = true; break;
            case kCmdFontSet:
                // Out-of-range is ignored rather than clamped: a wild value is a bug,
                // and silently rendering someone else's set hides it worse than
                // leaving the display alone does.
                if (cmd_param_ < kFontSets)
                {
                    font_set_ = cmd_param_;
                    dirty_flag_ = true;
                }
                break;
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
            // The sprite block is a run of six-byte records rather than named
            // registers, so it is decoded here rather than as switch cases.
            if (address >= kRegSpriteFirst && address <= kRegSpriteLast)
            {
                const uint8_t i = static_cast<uint8_t>((address - kRegSpriteFirst) / kSpriteStride);
                Sprite &sp = sprites_[i];
                switch ((address - kRegSpriteFirst) % kSpriteStride)
                {
                case kSprXLo: sp.x = static_cast<uint16_t>((sp.x & 0x0300) | value); break;
                case kSprXHi:
                    sp.x = static_cast<uint16_t>((sp.x & 0x00FF) |
                              (static_cast<uint16_t>(value & 0x03) << 8));
                    sp.w = static_cast<uint8_t>(((value & kSprSizeMask) >> kSprSizeShift) + 1);
                    sp.magx = (value & kSprMagX) != 0;
                    break;
                case kSprYLo: sp.y = static_cast<uint16_t>((sp.y & 0x0300) | value); break;
                case kSprYHi:
                    sp.y = static_cast<uint16_t>((sp.y & 0x00FF) |
                              (static_cast<uint16_t>(value & 0x03) << 8));
                    sp.h = static_cast<uint8_t>(((value & kSprSizeMask) >> kSprSizeShift) + 1);
                    sp.magy = (value & kSprMagY) != 0;
                    sp.enabled = (value & kSprEnable) != 0;
                    break;
                case kSprGlyph: sp.glyph = value; break;
                default:        sp.attr = value; break;
                }
                dirty_flag_ = true;
            }
            break;
        }
    }

    void VIC::cmdClear()
    {
        screen_buffer_.fill(cmd_param_);
        color_buffer_.fill(attr_latch_);
        scroll_top_ = 0;                   // a clear also resets the scroll region
        scroll_bot_ = kScreenHeight - 1;
        row_double_ = 0;                   // ...and puts every row back to 8x16, so a
                                           // program cannot strand the shell at 16x32
        font_ram_active_ = false;          // ...and back to the ROM font, set 0, for
        font_set_ = 0;                     // exactly the same reason
        fine_y_ = 0;                       // ...and fine scrolling off, so the shell
        fine_active_ = false;              // never loses a row to a staging row
        for (Sprite &sp : sprites_)        // ...and every sprite off and back to one
        {                                  // cell, so nothing is left stranded over
            sp.enabled = false;            // the shell's screen at any size
            sp.w = 1;
            sp.h = 1;
            sp.magx = false;
            sp.magy = false;
        }
        dirty_flag_ = true;
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

    // Scroll/blank only rows scroll_top_..scroll_bot_ (default: the whole screen),
    // so an app can pin header rows above the region and footer rows (e.g. an
    // input/status line) below it. That pair is what ANSI's DECSTBM asks for.
    void VIC::cmdScrollUp()
    {
        if (scroll_top_ > scroll_bot_) return;      // empty region: nothing to shift
        const uint16_t regionStart = static_cast<uint16_t>(scroll_top_) * kScreenWidth;
        const uint16_t regionEnd = (static_cast<uint16_t>(scroll_bot_) + 1) * kScreenWidth;
        for (uint16_t i = regionStart; i + kScreenWidth < regionEnd; ++i)
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
        if (scroll_top_ > scroll_bot_) return;
        const uint16_t regionStart = static_cast<uint16_t>(scroll_top_) * kScreenWidth;
        const uint16_t regionEnd = (static_cast<uint16_t>(scroll_bot_) + 1) * kScreenWidth;
        for (uint16_t i = regionEnd; i-- > regionStart + kScreenWidth;)
        {
            screen_buffer_[i] = screen_buffer_[i - kScreenWidth];
            color_buffer_[i] = color_buffer_[i - kScreenWidth];
        }
        for (uint16_t i = regionStart; i < regionStart + kScreenWidth; ++i)
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
        if (scroll_top_ > scroll_bot_) return;
        const uint32_t below = (scroll_bot_ + 1 >= 32) ? 0xFFFFFFFFu
                                                       : ((1u << (scroll_bot_ + 1)) - 1u);
        const uint32_t region = below & ~((1u << scroll_top_) - 1u);
        const uint32_t inside = row_double_ & region;
        const uint32_t moved = up ? ((inside >> 1) & region) : ((inside << 1) & region);
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
