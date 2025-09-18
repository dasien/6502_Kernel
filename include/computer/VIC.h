#ifndef VIC_H
#define VIC_H

#include <cstdint>
#include <array>

namespace Computer {

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
    bool isScreenAddress(uint16_t address) const;
    void writeScreen(uint16_t address, uint8_t value);
    uint8_t readScreen(uint16_t address) const;

    // Display buffer access
    const std::array<uint8_t, kScreenSize>& getScreenBuffer() const;
    uint8_t getCharacterAt(uint16_t x, uint16_t y) const;
    void setCharacterAt(uint16_t x, uint16_t y, uint8_t character);

    // Screen operations
    void clearScreen(uint8_t fill_char = 0x20); // Default to space character
    void scrollUp();
    void setCursorPosition(uint16_t x, uint16_t y);
    void getCursorPosition(uint16_t& x, uint16_t& y) const;

    // Status and control
    bool isDirty() const;
    void clearDirty();

private:
    std::array<uint8_t, kScreenSize> screen_buffer_;
    uint16_t cursor_x_;
    uint16_t cursor_y_;
    bool dirty_flag_;

    // Helper functions
    uint16_t addressToOffset(uint16_t address) const;
    uint16_t coordinatesToOffset(uint16_t x, uint16_t y) const;
    void offsetToCoordinates(uint16_t offset, uint16_t& x, uint16_t& y) const;
};

} // namespace Computer

#endif // VIC_H