/**
 * @file VIC.h
 * @brief Video Interface Chip emulator (80x25 text + per-cell color)
 * @author 6502 Kernel Project
 */

#ifndef VIC_H
#define VIC_H

#include <cstdint>
#include <array>
#include <vector>

namespace Computer
{
    /**
     * @class VIC
     * @brief Video chip emulator for an 80x25 color text display.
     *
     * The chip owns two parallel cell planes that are NOT mapped into the CPU's
     * 64K address space: a character plane (one ASCII byte per cell) and a color
     * plane (one attribute byte per cell). The 6502 reaches them through a small
     * I/O-page register port ($FE2D-$FE36), exactly like the BlockDevice idiom:
     * set a cell index via VREG_ADDR_LO/HI, then read/write the auto-incrementing
     * VREG_CHAR / VREG_COLOR data ports. A command register (VREG_CMD) drives
     * chip-side block operations (clear / scroll / fill-row) so the CPU never has
     * to copy ~4000 bytes per scroll.
     *
     * Attribute byte format: bit7 = reverse-video, bit6 = bright, bits5-3 = bg
     * color (0-7), bits2-0 = fg color (0-7). Default = $02 (green on black).
     *
     * DOUBLE-SIZE ROWS. Any row can be flagged double: its glyphs render at twice
     * the size, 16x32 instead of 8x16, so the row spans two rows of screen and holds
     * 40 characters instead of 80. The row underneath it is covered and its contents
     * are not drawn. Same idea as the VT100's double-height lines, and for the same
     * reason -- it is a property of the line, not a mode of the chip, so a program can
     * have a chunky playfield above a normal-sized status line.
     *
     * The character plane does not change shape: a double row's 40 cells are just the
     * first 40 cells of that row, addressed exactly as always. Nothing about the port
     * protocol differs; only the size the pixels come out at.
     *
     * kCmdClear puts every row back to normal. That is deliberate -- it means no
     * program can leave the machine in a state where the shell renders at double size,
     * however badly it exits, and a program that wants double rows simply sets them up
     * after it clears (which it was doing anyway).
     *
     * SOFT FONT. The glyph shapes themselves are RAM, not a fixed ROM. Font storage
     * lives inside the chip like the cell planes do -- it is NOT in the 6502's address
     * space -- and is reached through its own index/data port at $FE62-$FE64. This is
     * the TMS9918 / VDC 8563 model; the C64 and Atari instead let the video chip read
     * main RAM, which costs address space and steals CPU cycles.
     *
     * It holds kFontSets complete 256-glyph fonts, and kCmdFontSet picks which one the
     * renderer reads. That is the important part: a program does not rewrite glyphs
     * per frame, it uploads the variants once and then switches sets with a single
     * write -- the equivalent of pointing the C64's $D018 at a different charset, and
     * what makes pixel-smooth character scrolling affordable. "Set" and not "bank",
     * because MODULE_BANK ($FE23) already means something entirely different.
     *
     * reset() seeds every set from the CP437 ROM and selects the ROM font, so a program
     * can switch to RAM, redefine a handful of glyphs and leave the other 248 alone --
     * and no program can strand the shell with an unreadable font.
     *
     * FINE VERTICAL SCROLL. kCmdFineY slides the whole scroll region down by a pixel
     * count, so a program can move the world in steps finer than a character cell and
     * only issue a real row-scroll when the offset wraps. This is the C64 VIC-II's
     * YSCROLL; a character-cell chip without it can only scroll in whole-cell jumps,
     * which reads as a strobe rather than motion.
     *
     * The region's TOP row is a hidden staging row. Sliding down opens a gap at the
     * top of the region and what belongs there is the row that does not exist yet, so
     * the renderer clips the region to start one row down: at offset 0 the top row is
     * entirely above the clip and invisible, and it slides into view as the offset
     * grows. When the offset reaches a cell height the program issues a real scroll,
     * resets the offset and writes a fresh hidden top row. It costs one row.
     *
     * KNOWN LIMITATION: everything in the region shifts, including objects the game
     * drew there. Things that ride the world (terrain, enemies riding the scroll) come
     * out right for free; a SCREEN-FIXED object -- a player craft, a shot travelling
     * up -- will sawtooth by one cell per scroll, because it is in the plane being
     * slid. That is what sprites are for, and we do not have them yet.
     *
     * SPRITES. A small set of glyphs positioned in PIXELS rather than cells, drawn
     * over the character planes and unaffected by the scroll region -- neither by the
     * row scroll nor by the fine offset. That last part is the whole reason they exist:
     * anything a program draws into the cell plane rides the fine offset, so a
     * screen-fixed object like a player's craft sawtooths by a cell on every scroll.
     * A sprite does not, because it is not in the plane.
     *
     * Their shapes come from the same font storage the cells use, so a program that has
     * already uploaded a glyph gets it for free, and a sprite can be re-shaped by
     * pointing it at a different code. Colour comes from an attribute byte; the glyph's
     * background bits are TRANSPARENT, which is the other thing a cell cannot do.
     *
     * kCmdClear disables all of them, for the same reason it resets row sizes and the
     * font: no program can leave something stranded on the shell's screen.
     *
     * @see Memory, BlockDevice, Computer6502
     */
    class VIC
    {
    public:
        static constexpr uint16_t kScreenWidth = 80;
        static constexpr uint16_t kScreenHeight = 25;
        static constexpr uint16_t kScreenSize = kScreenWidth * kScreenHeight; // 2000

        // VDC-style register port in the always-mapped I/O page (after the ACIA
        // at $FE2C). See class docs for the access protocol.
        static constexpr uint16_t kRegAddrLo = 0xFE2D;   ///< cell index low (W)
        static constexpr uint16_t kRegAddrHi = 0xFE2E;   ///< cell index high (W)
        static constexpr uint16_t kRegChar = 0xFE2F;     ///< char data port, auto-inc (R/W)
        static constexpr uint16_t kRegColor = 0xFE30;    ///< color data port, auto-inc (R/W)
        static constexpr uint16_t kRegAttr = 0xFE31;     ///< current attribute latch (W)
        static constexpr uint16_t kRegCmd = 0xFE32;      ///< command engine (W)
        static constexpr uint16_t kRegStatus = 0xFE33;   ///< 0 = ready (R)
        static constexpr uint16_t kRegCursorLo = 0xFE34; ///< cursor cell low (W)
        static constexpr uint16_t kRegCursorHi = 0xFE35; ///< cursor cell high; bit7 set = hidden (W)
        static constexpr uint16_t kRegCmdParam = 0xFE36; ///< command parameter / fill char (W)
        // Soft-font port. Separate from the block above because the VIC's own range
        // ends at $FE37 and $FE38 is the SID; these sit in the free space above the
        // RTC ($FE61 is the PowerSwitch). isVideoRegAddress() accepts both ranges.
        static constexpr uint16_t kRegFontLo = 0xFE62;   ///< font byte index low (W)
        static constexpr uint16_t kRegFontHi = 0xFE63;   ///< font byte index high (W)
        static constexpr uint16_t kRegFontData = 0xFE64; ///< font data, auto-inc (R/W)

        static constexpr uint16_t kRegScrollBot = 0xFE37; ///< scroll-region bottom row (W);
                                                          ///< scroll affects rows 0..this. Reset
                                                          ///< to the last row on clear.

        static constexpr uint16_t kRegFirst = kRegAddrLo;
        static constexpr uint16_t kRegLast = kRegScrollBot;
        static constexpr uint16_t kRegFontFirst = kRegFontLo;
        static constexpr uint16_t kRegFontLast = kRegFontData;

        /// Sprite registers, six per sprite, immediately after the font port.
        /// Positions are in NOMINAL pixels on an 8x16 cell grid (so 0..639 x 0..399);
        /// the renderer scales them by the window zoom.
        /// 17 x 6 bytes ends at $FECA, leaving 53 bytes of the I/O page free. 25 fitted
        /// too, but left only 5 -- a poor price for a shot count that only occurs at one
        /// weapon's peak, and this page is the only address space new devices have.
        static constexpr uint8_t kSpriteCount = 17;
        static constexpr uint16_t kRegSpriteFirst = 0xFE65;
        static constexpr uint8_t kSpriteStride = 6;
        static constexpr uint16_t kRegSpriteLast =
            kRegSpriteFirst + static_cast<uint16_t>(kSpriteCount) * kSpriteStride - 1;
        // Offsets within a sprite's block.
        static constexpr uint8_t kSprXLo = 0;
        static constexpr uint8_t kSprXHi = 1;   ///< bits 1-0
        static constexpr uint8_t kSprYLo = 2;
        static constexpr uint8_t kSprYHi = 3;   ///< bits 1-0; bit 7 = ENABLE
        static constexpr uint8_t kSprGlyph = 4;
        static constexpr uint8_t kSprAttr = 5;  ///< fg/bright as in a cell attribute
        static constexpr uint8_t kSprEnable = 0x80;

        /// Glyphs per font, bytes per glyph, and how many complete fonts are held.
        /// 16 sets is enough for 2 px phase steps of a 32 px double-height cell; the
        /// whole thing is 64 KB of host memory and zero guest address space.
        static constexpr uint16_t kGlyphCount = 256;
        static constexpr uint16_t kGlyphBytes = 16;
        static constexpr uint16_t kFontSize = kGlyphCount * kGlyphBytes; // 4096
        static constexpr uint8_t kFontSets = 16;
        static constexpr uint32_t kFontRamSize = static_cast<uint32_t>(kFontSize) * kFontSets;

        // Command codes written to VREG_CMD.
        static constexpr uint8_t kCmdClear = 0x01;      ///< fill whole screen
        static constexpr uint8_t kCmdScrollUp = 0x02;   ///< scroll up one row, blank bottom
        static constexpr uint8_t kCmdScrollDown = 0x03; ///< scroll down one row, blank top
        static constexpr uint8_t kCmdFillRow = 0x04;    ///< fill the row of the current cell
        static constexpr uint8_t kCmdRowSize = 0x05;    ///< param: bit7 = double, bits4-0 = row
        static constexpr uint8_t kCmdRowsNormal = 0x06; ///< every row back to 8x16
        static constexpr uint8_t kCmdFineY = 0x0C;      ///< param: 0..cell height-1.
                                                        ///< Slides the scroll region
                                                        ///< down that many PIXELS, and
                                                        ///< turns fine scrolling on;
                                                        ///< kCmdClear turns it off.
        static constexpr uint8_t kCmdFontRom = 0x08;    ///< render from the CP437 ROM
        static constexpr uint8_t kCmdFontRam = 0x09;    ///< render from font RAM
        static constexpr uint8_t kCmdFontReset = 0x0A;  ///< reload CP437 into every set
        static constexpr uint8_t kCmdFontSet = 0x0B;    ///< param: live set index. The
                                                        ///< $D018 equivalent -- this is
                                                        ///< the one written per frame.
        static constexpr uint8_t kCmdScrollTop = 0x07;  ///< param: scroll-region top row.
                                                        ///< A command rather than a register
                                                        ///< only because the port block ends
                                                        ///< at $FE37. Reset to 0 on clear.
                                                        ///< Like kCmdRowSize it consumes the
                                                        ///< shared parameter, so reset the fill
                                                        ///< char before the next clear/scroll.

        /// Set in VREG_CMD_PARAM alongside kCmdRowSize to make that row double.
        static constexpr uint8_t kRowSizeDouble = 0x80;
        static constexpr uint8_t kRowSizeMask = 0x1F;

        // Attribute byte layout.
        static constexpr uint8_t kAttrFgMask = 0x07;
        static constexpr uint8_t kAttrBgShift = 3;
        static constexpr uint8_t kAttrBgMask = 0x38;
        static constexpr uint8_t kAttrBright = 0x40;
        static constexpr uint8_t kAttrReverse = 0x80;
        static constexpr uint8_t kDefaultAttr = 0x02; ///< green (fg=2) on black (bg=0)

        // Hidden-cursor flag in the high byte of VREG_CURSOR_HI.
        static constexpr uint8_t kCursorHiddenBit = 0x80;

        VIC();

        // --- Register port (the real interface) ---
        [[nodiscard]] static bool isVideoRegAddress(uint16_t address);
        // Reading a data port advances the cell index, so the internal index is
        // mutable and read() stays const (preserving Memory::read's const contract).
        [[nodiscard]] uint8_t read(uint16_t address) const;
        void write(uint16_t address, uint8_t value);

        // --- Display buffer access (for the host renderer) ---
        /// Is this row drawn at 16x32? A double row covers the row below it, and the
        /// last row can never be one -- there is nothing under it to cover.
        [[nodiscard]] bool isRowDouble(uint16_t row) const;

        /// One sprite's state, for the renderer. Positions are nominal pixels.
        struct Sprite
        {
            bool enabled = false;
            uint16_t x = 0;
            uint16_t y = 0;
            uint8_t glyph = 0;
            uint8_t attr = kDefaultAttr;
        };
        [[nodiscard]] const Sprite &sprite(uint8_t index) const;

        /// Pixel offset the scroll region is currently slid down by, and whether fine
        /// scrolling is on at all. Off means the renderer draws exactly as before --
        /// a program that never issues kCmdFineY is completely unaffected.
        [[nodiscard]] uint8_t fineY() const { return fine_y_; }
        [[nodiscard]] bool fineActive() const { return fine_active_; }
        void getScrollRegion(uint8_t &top, uint8_t &bot) const { top = scroll_top_; bot = scroll_bot_; }

        /// The 16 scanline bytes for a glyph, from the ROM or the live font set.
        /// The renderer calls this per cell instead of indexing kCp437Font directly.
        [[nodiscard]] const uint8_t *glyphRows(uint8_t glyph) const;

        [[nodiscard]] const std::array<uint8_t, kScreenSize> &getScreenBuffer() const;
        [[nodiscard]] const std::array<uint8_t, kScreenSize> &getColorBuffer() const;
        [[nodiscard]] uint8_t getCharacterAt(uint16_t x, uint16_t y) const;
        [[nodiscard]] uint8_t getColorAt(uint16_t x, uint16_t y) const;
        void setCharacterAt(uint16_t x, uint16_t y, uint8_t character);
        // Hardware cursor cell index (0..kScreenSize-1) and whether it is hidden.
        void getCursorCell(uint16_t &index, bool &hidden) const;

        // --- Screen operations ---
        void clearScreen(uint8_t fill_char = 0x20); // Default to space character
        void scrollUp();
        void setCursorPosition(uint16_t x, uint16_t y);
        void getCursorPosition(uint16_t &x, uint16_t &y) const;

        // Status and control
        [[nodiscard]] bool isDirty() const;
        void clearDirty();

    private:
        std::array<uint8_t, kScreenSize> screen_buffer_{};
        std::array<uint8_t, kScreenSize> color_buffer_{};

        // Register-port state.
        mutable uint16_t cell_index_ = 0; ///< shared char/color data-port index
        uint8_t attr_latch_ = kDefaultAttr;
        uint8_t cmd_param_ = 0x20; ///< fill char for commands (default space)
        uint8_t scroll_top_ = 0;                 ///< scroll-region top row (default: full screen)
        uint8_t scroll_bot_ = kScreenHeight - 1; ///< scroll-region bottom row (default: full screen)
        uint32_t row_double_ = 0;                ///< one bit per row; set = 16x32

        // Fine vertical scroll. See the class comment for the staging-row contract.
        uint8_t fine_y_ = 0;
        bool fine_active_ = false;

        std::array<Sprite, kSpriteCount> sprites_{};

        // Soft font. Not in the 6502's address space -- see the class comment.
        std::vector<uint8_t> font_ram_;          ///< kFontSets x kFontSize
        mutable uint32_t font_index_ = 0;        ///< byte index for the data port
        uint8_t font_set_ = 0;                   ///< which set the renderer reads
        bool font_ram_active_ = false;           ///< false = render from the ROM
        uint16_t cursor_index_ = 0;
        bool cursor_hidden_ = false;

        uint16_t cursor_x_;
        uint16_t cursor_y_;
        bool dirty_flag_;

        // Command-engine helpers.
        void cmdClear();
        void cmdScrollUp();
        void cmdScrollDown();
        void cmdFillRow();
        void cmdRowSize();
        void seedFontRam();
        void shiftRowFlags(bool up);
        void advanceIndex() const;

        /// The cell index, clamped into range, for use when indexing the planes.
        /// A write to VREG_ADDR_LO deliberately does NOT wrap (the stale high byte
        /// would corrupt the address before the high byte arrives), so cell_index_
        /// can legitimately hold up to $07FF while kScreenSize is 2000 -- indexing
        /// the std::arrays with it directly ran off the end of the object.
        [[nodiscard]] uint16_t cell() const { return cell_index_ % kScreenSize; }

        // Helper functions
        [[nodiscard]] uint16_t coordinatesToOffset(uint16_t x, uint16_t y) const;
        void offsetToCoordinates(uint16_t offset, uint16_t &x, uint16_t &y) const;
    };
} // namespace Computer

#endif // VIC_H
