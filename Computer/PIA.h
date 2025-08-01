#pragma once

#include <cstdint>
#include <array>

namespace Computer {

class PIA {
public:
    static constexpr uint16_t kPiaMemoryStart = 0xDC00;
    static constexpr uint16_t kPiaMemoryEnd = 0xDC0F;
    static constexpr uint8_t kKeyboardBufferSize = 32;
    
    // PIA Register offsets
    static constexpr uint8_t kPortAData = 0x00;      // $DC00 - Keyboard data
    static constexpr uint8_t kPortADdr = 0x01;       // $DC01 - Data direction register
    static constexpr uint8_t kPortAControl = 0x02;   // $DC02 - Control register
    static constexpr uint8_t kPortBData = 0x03;      // $DC03 - Port B data (future use)
    static constexpr uint8_t kPortBDdr = 0x04;       // $DC04 - Port B DDR
    static constexpr uint8_t kPortBControl = 0x05;   // $DC05 - Port B control
    
    // Control register flags
    static constexpr uint8_t kDataAvailable = 0x01;  // Bit 0: Data ready to read
    static constexpr uint8_t kBufferFull = 0x02;     // Bit 1: Buffer is full
    static constexpr uint8_t kInterruptFlag = 0x04;  // Bit 2: Interrupt flag
    static constexpr uint8_t kInterruptEnable = 0x08; // Bit 3: Interrupt enable
    
    PIA();
    
    // Memory interface
    bool isPiaAddress(uint16_t address) const;
    void writePia(uint16_t address, uint8_t value);
    uint8_t readPia(uint16_t address);
    
    // Keyboard interface
    void addKeypress(uint8_t ascii_code);
    bool hasKeypress() const;
    uint8_t getKeypress();
    void clearKeyboardBuffer();
    
    // Status interface
    bool isBufferFull() const;
    bool isDataAvailable() const;
    uint8_t getBufferCount() const;
    
private:
    // Keyboard circular buffer
    std::array<uint8_t, kKeyboardBufferSize> keyboard_buffer_;
    uint8_t buffer_head_;
    uint8_t buffer_tail_;
    uint8_t buffer_count_;
    
    // PIA registers
    uint8_t port_a_data_;
    uint8_t port_a_ddr_;
    uint8_t port_a_control_;
    uint8_t port_b_data_;
    uint8_t port_b_ddr_;
    uint8_t port_b_control_;
    
    // Helper functions
    uint8_t addressToOffset(uint16_t address) const;
    void updateControlFlags();
    void incrementBufferHead();
    void incrementBufferTail();
};

} // namespace Computer