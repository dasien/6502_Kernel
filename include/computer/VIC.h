/**
 * @file VIC.h
 * @brief VIC-II Video Interface Chip Emulator
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
     * @brief VIC-II Video Interface Chip emulator for text mode display
     *
     * This class emulates the VIC-II video chip found in the Commodore 64,
     * specifically focusing on text mode operation. It provides a 40x25 character
     * display with memory-mapped screen buffer access.
     *
     * Features:
     * - 40x25 character text mode display (1000 characters total)
     * - Memory-mapped I/O at $0400-$07E7 (screen memory)
     * - Screen buffer management and cursor tracking
     * - Text operations like scrolling and screen clearing
     * - Direct character access and manipulation
     *
     * The VIC chip interfaces with the 6502 memory system to provide video
     * output for the monitor program and system display.
     *
     * @see Memory, Computer6502
     */
    class VIC
    {
    public:
        static constexpr uint16_t kScreenWidth = 40;
        static constexpr uint16_t kScreenHeight = 25;
        static constexpr uint16_t kScreenSize = kScreenWidth * kScreenHeight;
        static constexpr uint16_t kScreenMemoryStart = 0x0400;
        static constexpr uint16_t kScreenMemoryEnd = 0x07E7;

        VIC();

        // Memory-mapped I/O interface
        [[nodiscard]] bool isScreenAddress(uint16_t address) const;
        void writeScreen(uint16_t address, uint8_t value);
        [[nodiscard]] uint8_t readScreen(uint16_t address) const;

        // Display buffer access
        [[nodiscard]] const std::array<uint8_t, kScreenSize> &getScreenBuffer() const;
        [[nodiscard]] uint8_t getCharacterAt(uint16_t x, uint16_t y) const;
        void setCharacterAt(uint16_t x, uint16_t y, uint8_t character);

        // Screen operations
        void clearScreen(uint8_t fill_char = 0x20); // Default to space character
        void scrollUp();
        void setCursorPosition(uint16_t x, uint16_t y);
        void getCursorPosition(uint16_t &x, uint16_t &y) const;

        // Status and control
        [[nodiscard]] bool isDirty() const;
        void clearDirty();

    private:
        std::array<uint8_t, kScreenSize> screen_buffer_{};
        uint16_t cursor_x_;
        uint16_t cursor_y_;
        bool dirty_flag_;

        // Helper functions
        [[nodiscard]] uint16_t addressToOffset(uint16_t address) const;
        [[nodiscard]] uint16_t coordinatesToOffset(uint16_t x, uint16_t y) const;
        void offsetToCoordinates(uint16_t offset, uint16_t &x, uint16_t &y) const;
    };
} // namespace Computer

#endif // VIC_H
