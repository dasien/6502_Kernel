#include "PIA.h"
#include <cstdio>

namespace Computer {

PIA::PIA() 
    : buffer_head_(0)
    , buffer_tail_(0)
    , buffer_count_(0)
    , port_a_data_(0x00)
    , port_a_ddr_(0x00)
    , port_a_control_(0x00)
    , port_b_data_(0x00)
    , port_b_ddr_(0x00)
    , port_b_control_(0x00)
{
    clearKeyboardBuffer();
}

bool PIA::isPiaAddress(uint16_t address) const
{
    return address >= kPiaMemoryStart && address <= kPiaMemoryEnd;
}

void PIA::writePia(uint16_t address, uint8_t value)
{
    if (!isPiaAddress(address))
    {
        return;
    }
    
    uint8_t offset = addressToOffset(address);
    
    switch (offset)
    {
        case kPortAData:
            port_a_data_ = value;
            break;
        case kPortADdr:
            port_a_ddr_ = value;
            break;
        case kPortAControl:
            port_a_control_ = value;
            updateControlFlags();
            break;
        case kPortBData:
            port_b_data_ = value;
            break;
        case kPortBDdr:
            port_b_ddr_ = value;
            break;
        case kPortBControl:
            port_b_control_ = value;
            break;
        default:
            // Reserved registers - ignore writes
            break;
    }
}

uint8_t PIA::readPia(uint16_t address)
{
    if (!isPiaAddress(address))
    {
        return 0x00;
    }
    
    uint8_t offset = addressToOffset(address);
    
    switch (offset)
    {
        case kPortAData:
            // Reading keyboard data - return next character from buffer
            if (hasKeypress())
            {
                uint8_t key = getKeypress();
                printf("PIA: 6502 reading data register: '%c' (0x%02X), remaining count=%d\n", 
                       (key >= 32 && key <= 126) ? key : '?', key, buffer_count_);
                updateControlFlags();
                return key;
            }
            printf("PIA: 6502 reading data register: no data available\n");
            return 0x00;
            
        case kPortADdr:
            return port_a_ddr_;
            
        case kPortAControl:
            // Return current status flags
            updateControlFlags();
            return port_a_control_;
            
        case kPortBData:
            return port_b_data_;
            
        case kPortBDdr:
            return port_b_ddr_;
            
        case kPortBControl:
            return port_b_control_;
            
        default:
            return 0x00;
    }
}

void PIA::addKeypress(uint8_t ascii_code)
{
    if (isBufferFull())
    {
        // Buffer is full - ignore keypress or could overwrite oldest
        // PIA: Keyboard buffer full - ignoring keypress
        return;
    }
    
    keyboard_buffer_[buffer_head_] = ascii_code;
    incrementBufferHead();
    buffer_count_++;
    
    updateControlFlags();
}

bool PIA::hasKeypress() const
{
    return buffer_count_ > 0;
}

uint8_t PIA::getKeypress()
{
    if (!hasKeypress())
    {
        return 0x00;
    }
    
    uint8_t key = keyboard_buffer_[buffer_tail_];
    incrementBufferTail();
    buffer_count_--;
    
    updateControlFlags();
    
    return key;
}

void PIA::clearKeyboardBuffer()
{
    keyboard_buffer_.fill(0x00);
    buffer_head_ = 0;
    buffer_tail_ = 0;
    buffer_count_ = 0;
    updateControlFlags();
}

bool PIA::isBufferFull() const
{
    return buffer_count_ >= kKeyboardBufferSize;
}

bool PIA::isDataAvailable() const
{
    return hasKeypress();
}

uint8_t PIA::getBufferCount() const
{
    return buffer_count_;
}

uint8_t PIA::addressToOffset(uint16_t address) const
{
    return address - kPiaMemoryStart;
}

void PIA::updateControlFlags()
{
    // Clear existing data flags
    port_a_control_ &= ~(kDataAvailable | kBufferFull);
    
    // Set data available flag if we have keypresses
    if (hasKeypress())
    {
        port_a_control_ |= kDataAvailable;
    }
    
    // Set buffer full flag if buffer is full
    if (isBufferFull())
    {
        port_a_control_ |= kBufferFull;
    }
    
    // Set interrupt flag if data is available and interrupts are enabled
    if (hasKeypress() && (port_a_control_ & kInterruptEnable))
    {
        port_a_control_ |= kInterruptFlag;
    }
    else
    {
        port_a_control_ &= ~kInterruptFlag;
    }
}

void PIA::incrementBufferHead()
{
    buffer_head_ = (buffer_head_ + 1) % kKeyboardBufferSize;
}

void PIA::incrementBufferTail()
{
    buffer_tail_ = (buffer_tail_ + 1) % kKeyboardBufferSize;
}

} // namespace Computer