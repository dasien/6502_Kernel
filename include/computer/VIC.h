/**
 * @file VIC.h
 * @brief Video Interface Chip emulator (80x25 text + per-cell color)
 * @author 6502 Kernel Project
 */

#ifndef VIC_H
#define VIC_H

#include <cstdint>
#include <array>

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
     * Transitional compatibility: the legacy memory-mapped window at $0400-$07E7
     * (40 columns) still writes through into the top-left 40 columns of the new
     * 80-column character plane, so an unmodified kernel keeps rendering while the
     * port is brought online. This window is removed once the kernel drives the
     * port directly.
     *
     * @see Memory, BlockDevice, Computer6502
     */
    class VIC
    {
    public:
        static constexpr uint16_t kScreenWidth = 80;
        static constexpr uint16_t kScreenHeight = 25;
        static constexpr uint16_t kScreenSize = kScreenWidth * kScreenHeight; // 2000

        // Legacy 40-column memory-mapped window (transitional compat shim).
        static constexpr uint16_t kCompatWidth = 40;
        static constexpr uint16_t kScreenMemoryStart = 0x0400;
        static constexpr uint16_t kScreenMemoryEnd = 0x07E7; // $0400 + 40*25 - 1

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

        static constexpr uint16_t kRegFirst = kRegAddrLo;
        static constexpr uint16_t kRegLast = kRegCmdParam;

        // Command codes written to VREG_CMD.
        static constexpr uint8_t kCmdClear = 0x01;      ///< fill whole screen
        static constexpr uint8_t kCmdScrollUp = 0x02;   ///< scroll up one row, blank bottom
        static constexpr uint8_t kCmdScrollDown = 0x03; ///< scroll down one row, blank top
        static constexpr uint8_t kCmdFillRow = 0x04;    ///< fill the row of the current cell

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

        // --- Legacy 40-col memory-mapped window (transitional) ---
        [[nodiscard]] bool isScreenAddress(uint16_t address) const;
        void writeScreen(uint16_t address, uint8_t value);
        [[nodiscard]] uint8_t readScreen(uint16_t address) const;

        // --- Display buffer access (for the host renderer) ---
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
        void advanceIndex() const;

        // Helper functions
        [[nodiscard]] uint16_t coordinatesToOffset(uint16_t x, uint16_t y) const;
        void offsetToCoordinates(uint16_t offset, uint16_t &x, uint16_t &y) const;
    };
} // namespace Computer

#endif // VIC_H
