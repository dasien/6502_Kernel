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
     * BITMAP SPRITES. A glyph is one colour and shares its code with the text, so a
     * sprite can instead take its picture from PATTERN RAM: 256 image slots of 16x16
     * pixels at 4 bits a pixel, 32 KB of video RAM inside the chip and, like the font,
     * reached only through an index/data port ($FECE-$FED0). This is the TMS9918's
     * sprite pattern table in shape. Setting kSprBitmap in a sprite's Y high byte makes
     * its glyph register name a slot instead of a character code; size then composes
     * consecutive SLOTS rather than codes, and magnify still doubles.
     *
     * Pixel index 0 is transparent and 1-15 name palette slots, so a palette effect
     * reaches bitmap sprites as it reaches everything else. The cost is that palette
     * slot 0 cannot be drawn by one; art that wants it maps it to another slot.
     *
     * Pattern RAM is zero at power-on and survives kCmdClear, which turns bitmap mode
     * off with the rest of the sprite state but leaves the images alone -- a program
     * uploads its art once and a screen clear should not cost it the upload.
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

        // Soft-palette port, in the free space above the sprites. Same idiom as the
        // font: an index and an auto-incrementing data port, so loading colours is a
        // seek followed by a run of writes.
        //
        // The palette is chip state, exactly like the font, and for the same reason:
        // whoever owns the screen owns it. The machine boots with the CGA sixteen, a
        // theme is whatever the DOS loads over them, and a program with an opinion
        // about its colours loads its own -- the Scott Adams games have no opinion
        // and inherit whatever is there, which is the behaviour you want from them.
        //
        // The index counts BYTES, not slots, so a theme that only wants to restate
        // the background and the normal text writes two short runs instead of the
        // whole table.
        static constexpr uint16_t kRegPaletteIdx = 0xFECB;  ///< palette byte index (W)
        static constexpr uint16_t kRegPaletteData = 0xFECC; ///< palette data, auto-inc (R/W)

        static constexpr uint8_t kPaletteSlots = 16;        ///< as many as the attribute can name
        static constexpr uint8_t kPaletteBytes = kPaletteSlots * 3;  ///< R, G, B per slot

        static constexpr uint16_t kRegFirst = kRegAddrLo;
        static constexpr uint16_t kRegLast = kRegScrollBot;
        static constexpr uint16_t kRegFontFirst = kRegFontLo;
        static constexpr uint16_t kRegFontLast = kRegFontData;
        static constexpr uint16_t kRegPaletteFirst = kRegPaletteIdx;
        static constexpr uint16_t kRegPaletteLast = kRegPaletteData;

        /// Sprite registers, six per sprite, immediately after the font port.
        /// Positions are in NOMINAL pixels on an 8x16 cell grid (so 0..639 x 0..399);
        /// the renderer scales them by the window zoom.
        /// 17 x 6 bytes ends at $FECA. The palette takes $FECB-$FECC, the frame
        /// counter $FECD and the sprite pattern port $FECE-$FED0, leaving 47 free.
        /// 25 sprites fitted
        /// too, but left only 5 -- a poor price for a shot count that only occurs at one
        /// weapon's peak, and this page is the only address space new devices have.
        static constexpr uint8_t kSpriteCount = 17;
        static constexpr uint16_t kRegSpriteFirst = 0xFE65;
        static constexpr uint8_t kSpriteStride = 6;
        static constexpr uint16_t kRegSpriteLast =
            kRegSpriteFirst + static_cast<uint16_t>(kSpriteCount) * kSpriteStride - 1;

        /// FRAME COUNTER ($FECD). Reading returns it; writing any value PRESENTS.
        ///
        /// Read: increments once per displayed frame.
        ///
        /// A counter and not a flag, and deliberately NOT cleared by reading. The
        /// VIC-II's collision registers clear on read, which means exactly one
        /// consumer: a second reader -- a debugger, a monitor, another subsystem --
        /// silently destroys the value for the first. A counter has no such problem,
        /// any number of readers see the same thing, and a delta greater than one
        /// tells a program it missed frames rather than hiding it.
        ///
        /// Eight bits wraps every 4.3 seconds at 60 Hz. That is ample for "has the
        /// frame changed since I looked", which is the only question it answers;
        /// durations are what the kernel's 16-bit jiffy counter is for.
        ///
        /// Write: "this frame is finished, show it". Without it the host can only
        /// show the plane at a frame boundary, and a program that paints after
        /// waking at one is always a frame behind -- measured at 15 ms of VENTURE's
        /// 23. A program that presents is shown at once, and for any frame it
        /// presented the boundary repaint is skipped, because that repaint would
        /// catch its next frame half drawn. A frame that passes with no present puts
        /// the boundary repaint back, so a program that never presents -- or stops,
        /// by exiting -- is shown exactly as before. The same address as the counter
        /// because the two are one conversation: ask which frame it is, say you are
        /// done with it.
        static constexpr uint16_t kRegFrame = 0xFECD;

        /// Sprite pattern port: a byte index into pattern RAM, low then high, and an
        /// auto-incrementing data port that reads back. Slot n starts at n * 128.
        static constexpr uint16_t kRegSprPatLo = 0xFECE;   ///< pattern byte index low (W/R)
        static constexpr uint16_t kRegSprPatHi = 0xFECF;   ///< pattern byte index high (W/R)
        static constexpr uint16_t kRegSprPatData = 0xFED0; ///< pattern data, auto-inc (R/W)
        static constexpr uint16_t kRegSprPatFirst = kRegSprPatLo;
        static constexpr uint16_t kRegSprPatLast = kRegSprPatData;

        /// Pattern RAM geometry. Two pixels a byte, the LEFT pixel in the high nibble,
        /// 8 bytes a row, rows top to bottom.
        static constexpr uint16_t kSprPatSlots = 256;
        static constexpr uint8_t kSprPatDim = 16;                           ///< pixels a side
        static constexpr uint16_t kSprPatRowBytes = kSprPatDim / 2;         ///< 8
        static constexpr uint16_t kSprPatBytes = kSprPatRowBytes * kSprPatDim; ///< 128
        static constexpr uint32_t kSprPatRamSize =
            static_cast<uint32_t>(kSprPatBytes) * kSprPatSlots;             ///< 32 KB
        // Offsets within a sprite's block.
        static constexpr uint8_t kSprXLo = 0;
        static constexpr uint8_t kSprXHi = 1;   ///< bits 1-0 pos; bits 4-2 WIDTH-1
        static constexpr uint8_t kSprYLo = 2;
        static constexpr uint8_t kSprYHi = 3;   ///< bits 1-0 pos; bits 4-2 HEIGHT-1;
                                                ///< bit 6 = BITMAP; bit 7 = ENABLE
        static constexpr uint8_t kSprGlyph = 4;
        static constexpr uint8_t kSprAttr = 5;  ///< fg/bright as in a cell attribute
        static constexpr uint8_t kSprEnable = 0x80;
        /// The glyph register names a pattern-RAM slot, not a character code. See the
        /// class comment; the attribute byte is ignored and reserved in this mode.
        static constexpr uint8_t kSprBitmap = 0x40;     ///< bit 6 of the Y high byte

        /// Size in CELLS, held in the spare bits of the two position high bytes: a
        /// position needs 10 bits of a 16-bit pair, so bits 4-2 of each were being
        /// masked off and discarded. Stored as size-1, which is what makes this
        /// backwards compatible -- code written before sizes existed leaves those bits
        /// zero and still gets a 1x1 sprite.
        static constexpr uint8_t kSprSizeShift = 2;
        static constexpr uint8_t kSprSizeMask = 0x1C;   ///< bits 4-2
        static constexpr uint8_t kSprSizeMax = 8;       ///< 3 bits, stored as size-1

        /// Magnify: draw each pixel of the pattern at double size on that axis.
        /// The OTHER way to make a sprite bigger, and not interchangeable with size.
        /// Size composes ADJACENT GLYPH CODES -- a 2x2 draws g, g+1, g+2, g+3 -- so it
        /// buys detail but needs artwork drawn across four patterns. Magnify stretches
        /// ONE pattern, so it buys no detail but matches what a double-size row does
        /// to a character. A program replacing a double-row cell with a sprite wants
        /// magnify; one drawing a large object from scratch wants size.
        static constexpr uint8_t kSprMagX = 0x20;       ///< bit 5 of the X high byte
        static constexpr uint8_t kSprMagY = 0x20;       ///< bit 5 of the Y high byte

        /// Glyphs per font, bytes per glyph, and how many complete fonts are held.
        /// 16 sets is enough for 2 px phase steps of a 32 px double-height cell; the
        /// whole thing is 64 KB of video RAM inside the chip and zero of the CPU's
        /// address space.
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
        static constexpr uint8_t kCmdPaletteReset = 0x0D; ///< restore the built-in CGA sixteen
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

        /// End the current frame: advance the frame counter. Called by the machine
        /// at the same 60 Hz boundary that raises the jiffy interrupt, so the two
        /// cannot drift apart. The host repaints on this boundary, which is what
        /// makes "wait for the counter to change, then paint" actually tear-free --
        /// a program that starts painting at a boundary has the whole frame interval
        /// to finish before anything reads the plane again.
        void endFrame();

        /// The frame counter's current value, as the guest sees it at $FECD.
        [[nodiscard]] uint8_t frame() const { return frame_; }

        /// Whether the program presented since the host last asked, then clear it.
        bool takePresent()
        {
            const bool p = present_pending_;
            present_pending_ = false;
            return p;
        }

        /// Whether the frame that last ended was presented. While it is, the host
        /// leaves the boundary alone and paints only on a present.
        [[nodiscard]] bool presentDriven() const { return presented_last_frame_; }
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
            /// Size in cells. A sprite wider or taller than one cell draws CONSECUTIVE
            /// glyph codes from the base, row-major: a 2x2 at code g is g,g+1 across
            /// the top and g+2,g+3 across the bottom. Codes wrap at 255.
            ///
            /// Composing from adjacent codes rather than magnifying one glyph is the
            /// difference between more size and more detail -- a 2x2 is 16x32 pixels of
            /// real artwork. It also keeps a big object as ONE sprite: four separate
            /// sprites would cost four position updates a frame, four of the seventeen
            /// slots, and could shear apart if an update landed mid-frame.
            uint8_t w = 1;
            uint8_t h = 1;
            bool magx = false;   ///< each pattern pixel drawn 2x wide
            bool magy = false;   ///< ...and/or 2x tall
            /// Picture from pattern RAM rather than the font. w and h then count
            /// 16x16 slots, so the pattern is w*16 by h*16 pixels.
            bool bitmap = false;
        };
        [[nodiscard]] const Sprite &sprite(uint8_t index) const;

        /// One pixel of a BITMAP sprite's pattern, as a palette index; 0 is
        /// transparent. x and y are pattern pixels, before magnify, in 0..w*16-1 and
        /// 0..h*16-1. Slots compose row-major from the base, as glyph codes do, and
        /// wrap at 255. The decoding lives in the chip rather than the renderer so the
        /// format has one owner and can be tested without a window.
        [[nodiscard]] uint8_t spritePixel(const Sprite &sp, uint16_t x, uint16_t y) const;

        /// One palette slot as 8-bit R, G, B. The renderer asks per cell rather than
        /// keeping a copy, so a palette write takes effect on the next repaint with
        /// nothing to invalidate.
        void paletteColor(uint8_t slot, uint8_t &r, uint8_t &g, uint8_t &b) const;

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
        uint8_t frame_ = 0;                        ///< $FECD, wraps every 256 frames
        bool present_pending_ = false;             ///< a present the host has not seen
        bool presented_this_frame_ = false;        ///< a present since the last boundary
        bool presented_last_frame_ = false;        ///< the frame just ended had one

        // Palette and soft font. Not in the 6502's address space -- see the class
        // comment.
        std::array<uint8_t, kPaletteBytes> palette_{};  ///< R,G,B per slot
        mutable uint8_t palette_index_ = 0;      ///< byte index for the data port

        std::vector<uint8_t> font_ram_;          ///< kFontSets x kFontSize
        mutable uint32_t font_index_ = 0;        ///< byte index for the data port
        uint8_t font_set_ = 0;                   ///< which set the renderer reads
        bool font_ram_active_ = false;           ///< false = render from the ROM

        std::vector<uint8_t> sprpat_ram_;        ///< kSprPatSlots x kSprPatBytes
        mutable uint16_t sprpat_index_ = 0;      ///< byte index for the data port
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
        void seedPalette();                      ///< the built-in CGA sixteen
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
