#pragma once

#include <cstdint>
#include <array>

namespace Computer {

class PIA {
public:
    static constexpr uint16_t kPiaMemoryStart = 0xDC00;
    static constexpr uint16_t kPiaMemoryEnd = 0xDC21;  // Extended for file I/O and save range
    static constexpr uint8_t kKeyboardBufferSize = 32;
    
    // PIA Register offsets
    static constexpr uint8_t kPortAData = 0x00;      // $DC00 - Keyboard data
    static constexpr uint8_t kPortADdr = 0x01;       // $DC01 - Data direction register
    static constexpr uint8_t kPortAControl = 0x02;   // $DC02 - Control register
    static constexpr uint8_t kPortBData = 0x03;      // $DC03 - Port B data (future use)
    static constexpr uint8_t kPortBDdr = 0x04;       // $DC04 - Port B DDR
    static constexpr uint8_t kPortBControl = 0x05;   // $DC05 - Port B control
    
    // File I/O interface (extended PIA)
    static constexpr uint8_t kFileCommand = 0x10;    // $DC10 - File operation command
    static constexpr uint8_t kFileStatus = 0x11;     // $DC11 - File operation status
    static constexpr uint8_t kFileAddrLo = 0x12;     // $DC12 - Target address low byte
    static constexpr uint8_t kFileAddrHi = 0x13;     // $DC13 - Target address high byte
    static constexpr uint8_t kFilenameStart = 0x14;  // $DC14-$DC1F - Filename buffer (12 bytes)
    static constexpr uint8_t kFileEndAddrLo = 0x20;  // $DC20 - End address low byte (for save range)
    static constexpr uint8_t kFileEndAddrHi = 0x21;  // $DC21 - End address high byte (for save range)
    
    // File command codes
    static constexpr uint8_t kFileLoadCommand = 0x01;
    static constexpr uint8_t kFileSaveCommand = 0x02;
    
    // File status codes
    static constexpr uint8_t kFileIdle = 0x00;
    static constexpr uint8_t kFileInProgress = 0x01;
    static constexpr uint8_t kFileSuccess = 0x02;
    static constexpr uint8_t kFileError = 0xFF;
    
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
    
    // File I/O interface
    void setMemoryInterface(class Memory* memory);
    bool hasFileOperation() const;
    void processFileOperations();
    
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
    
    // File I/O state
    uint8_t file_command_;
    uint8_t file_status_;
    uint16_t file_address_;
    uint16_t file_end_address_;
    std::array<char, 12> filename_;
    class Memory* memory_;
    
    // Helper functions
    uint8_t addressToOffset(uint16_t address) const;
    void updateControlFlags();
    void incrementBufferHead();
    void incrementBufferTail();
};

} // namespace Computer