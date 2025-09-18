#include "PIA.h"
#include "Memory.h"
#include <cstdio>
#include <string>
#ifdef QT_GUI
#include <QFileDialog>
#include <QCoreApplication>
#endif
#include <fstream>
#include <vector>

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
    , file_command_(kFileIdle)
    , file_status_(kFileIdle)
    , file_address_(0x0000)
    , file_end_address_(0x0000)
    , memory_(nullptr)
{
    clearKeyboardBuffer();
    filename_.fill(0);
}

bool PIA::isPiaAddress(const uint16_t address) const
{
    return address >= kPiaMemoryStart && address <= kPiaMemoryEnd;
}

void PIA::writePia(const uint16_t address, const uint8_t value)
{
    if (!isPiaAddress(address))
    {
        return;
    }

    switch (const uint8_t offset = addressToOffset(address))
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
        case kFileCommand:
            printf("PIA: Received file command: 0x%02X\n", value);
            file_command_ = value;
            if (value == kFileLoadCommand || value == kFileSaveCommand) {
                printf("PIA: Setting file status to IN_PROGRESS\n");
                file_status_ = kFileInProgress;
            }
            break;
        case kFileAddrLo:
            file_address_ = (file_address_ & 0xFF00) | value;
            break;
        case kFileAddrHi:
            file_address_ = (file_address_ & 0x00FF) | (value << 8);
            break;
        case kFileEndAddrLo:
            file_end_address_ = (file_end_address_ & 0xFF00) | value;
            break;
        case kFileEndAddrHi:
            file_end_address_ = (file_end_address_ & 0x00FF) | (value << 8);
            break;
        default:
            // Handle filename buffer writes ($DC14-$DC1F)
            if (offset >= kFilenameStart && offset < kFilenameStart + 12) {
                filename_[offset - kFilenameStart] = static_cast<char>(value);
            }
            break;
    }
}

uint8_t PIA::readPia(const uint16_t address)
{
    if (!isPiaAddress(address))
    {
        return 0x00;
    }

    switch (addressToOffset(address))
    {
        case kPortAData:
            // Reading keyboard data - return next character from buffer
            if (hasKeypress())
            {
                const uint8_t key = getKeypress();
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
            
        case kFileStatus:
            return file_status_;
            
        default:
            return 0x00;
    }
}

void PIA::addKeypress(const uint8_t ascii_code)
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

    const uint8_t key = keyboard_buffer_[buffer_tail_];
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

uint8_t PIA::addressToOffset(const uint16_t address) const
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

// File I/O implementation
void PIA::setMemoryInterface(Memory* memory)
{
    memory_ = memory;
}

bool PIA::hasFileOperation() const
{
    return (file_command_ == kFileLoadCommand || file_command_ == kFileSaveCommand) && file_status_ == kFileInProgress;
}

void PIA::processFileOperations()
{
    if (!hasFileOperation() || !memory_) {
        return;
    }
    
    if (file_command_ == kFileLoadCommand) {
        printf("PIA: File load request - Address: $%04X\n", file_address_);

        std::string filename;

#ifdef QT_GUI
        // Open file dialog to let user select file
        QString qfilename = QFileDialog::getOpenFileName(
            nullptr,
            "Load Binary File",
            QString(),
            "Binary Files (*.bin *.rom *.prg);;All Files (*.*)"
        );

        if (qfilename.isEmpty()) {
            printf("PIA: File load cancelled by user\n");
            file_status_ = kFileError;
            return;
        }

        filename = qfilename.toStdString();
#else
        // Console-only mode - use a default filename or disable file operations
        printf("PIA: File operations not supported in console mode\n");
        file_status_ = kFileError;
        return;
#endif

        printf("PIA: User selected file: '%s'\n", filename.c_str());

        // Load file using C++ streams for better error handling
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            printf("PIA: File load error - Could not open file: %s\n", filename.c_str());
            file_status_ = kFileError;
            return;
        }
        
        // Get file size
        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (file_size <= 0 || file_size > 65536) {
            printf("PIA: File load error - Invalid file size: %ld bytes\n", static_cast<long>(file_size));
            file_status_ = kFileError;
            return;
        }
        
        // Read file into buffer
        std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
            printf("PIA: File load error - Failed to read file data\n");
            file_status_ = kFileError;
            return;
        }
        
        // Load file data into emulated memory
        uint16_t current_address = file_address_;
        size_t bytes_loaded = 0;
        
        for (uint8_t byte : buffer) {
            if (current_address > 0xFFFF) break;
            memory_->write(current_address++, byte);
            bytes_loaded++;
        }
        
        printf("PIA: File loaded successfully - %zu bytes loaded at $%04X\n", 
               bytes_loaded, file_address_);
        
        // Clear the file operation
        file_command_ = kFileIdle;
        file_status_ = kFileSuccess;
    }
    else if (file_command_ == kFileSaveCommand) {
        printf("PIA: File save request - Range: $%04X-$%04X\n", file_address_, file_end_address_);
        
        // Validate address range
        if (file_end_address_ < file_address_) {
            printf("PIA: File save error - Invalid address range (end < start)\n");
            file_status_ = kFileError;
            return;
        }
        
        // Calculate number of bytes to save
        size_t bytes_to_save = file_end_address_ - file_address_ + 1;
        if (bytes_to_save > 65536) {
            printf("PIA: File save error - Range too large: %zu bytes\n", bytes_to_save);
            file_status_ = kFileError;
            return;
        }
        
        std::string filename;

#ifdef QT_GUI
        // Open file dialog to let user select save location
        QString qfilename = QFileDialog::getSaveFileName(
            nullptr,
            "Save Binary File",
            QString(),
            "Binary Files (*.bin);;All Files (*.*)"
        );

        if (qfilename.isEmpty()) {
            printf("PIA: File save cancelled by user\n");
            file_status_ = kFileError;
            return;
        }

        filename = qfilename.toStdString();
#else
        // Console-only mode - disable file operations
        printf("PIA: File operations not supported in console mode\n");
        file_status_ = kFileError;
        return;
#endif

        printf("PIA: User selected save file: '%s'\n", filename.c_str());

        // Read memory range and save to file
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            printf("PIA: File save error - Could not create file: %s\n", filename.c_str());
            file_status_ = kFileError;
            return;
        }
        
        // Read memory and write to file
        std::vector<uint8_t> buffer;
        buffer.reserve(bytes_to_save);
        
        for (uint16_t addr = file_address_; addr <= file_end_address_; ++addr) {
            buffer.push_back(memory_->read(addr));
        }
        
        if (!file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()))) {
            printf("PIA: File save error - Failed to write file data\n");
            file_status_ = kFileError;
            return;
        }
        
        printf("PIA: File saved successfully - %zu bytes saved from $%04X-$%04X\n", 
               buffer.size(), file_address_, file_end_address_);
        
        // Clear the file operation
        file_command_ = kFileIdle;
        file_status_ = kFileSuccess;
    }
}

} // namespace Computer