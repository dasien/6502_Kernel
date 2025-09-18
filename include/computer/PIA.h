/**
 * @file PIA.h
 * @brief Peripheral Interface Adapter (PIA) with Keyboard and File I/O
 * @author 6502 Kernel Project
 */

#pragma once

#include <cstdint>
#include <array>

namespace Computer
{
    /**
     * @class PIA
     * @brief Peripheral Interface Adapter for keyboard input and file operations
     *
     * This class emulates a PIA (Peripheral Interface Adapter) chip, providing
     * keyboard input buffering and file I/O operations for the 6502 system.
     * It extends traditional PIA functionality with modern file operations
     * needed for the monitor program.
     *
     * Features:
     * - Keyboard input buffering with 32-character circular buffer
     * - Memory-mapped I/O interface at $DC00-$DC21
     * - File load/save operations for the monitor L: and S: commands
     * - Status flags for data availability and buffer management
     * - Integration with system memory for file operations
     *
     * Memory Layout:
     * - $DC00-$DC05: Traditional PIA registers (ports A/B with control)
     * - $DC10-$DC13: File operation command and addressing
     * - $DC14-$DC1F: Filename buffer (12 characters)
     * - $DC20-$DC21: End address for save operations
     *
     * @see Memory, Computer6502
     */
    class PIA
    {
    public:
        static constexpr uint16_t kPiaMemoryStart = 0xDC00;
        static constexpr uint16_t kPiaMemoryEnd = 0xDC21; // Extended for file I/O and save range
        static constexpr uint8_t kKeyboardBufferSize = 32;

        // PIA Register offsets
        static constexpr uint8_t kPortAData = 0x00; // $DC00 - Keyboard data
        static constexpr uint8_t kPortADdr = 0x01; // $DC01 - Data direction register
        static constexpr uint8_t kPortAControl = 0x02; // $DC02 - Control register
        static constexpr uint8_t kPortBData = 0x03; // $DC03 - Port B data (future use)
        static constexpr uint8_t kPortBDdr = 0x04; // $DC04 - Port B DDR
        static constexpr uint8_t kPortBControl = 0x05; // $DC05 - Port B control

        // File I/O interface (extended PIA)
        static constexpr uint8_t kFileCommand = 0x10; // $DC10 - File operation command
        static constexpr uint8_t kFileStatus = 0x11; // $DC11 - File operation status
        static constexpr uint8_t kFileAddrLo = 0x12; // $DC12 - Target address low byte
        static constexpr uint8_t kFileAddrHi = 0x13; // $DC13 - Target address high byte
        static constexpr uint8_t kFilenameStart = 0x14; // $DC14-$DC1F - Filename buffer (12 bytes)
        static constexpr uint8_t kFileEndAddrLo = 0x20; // $DC20 - End address low byte (for save range)
        static constexpr uint8_t kFileEndAddrHi = 0x21; // $DC21 - End address high byte (for save range)

        // File command codes
        static constexpr uint8_t kFileLoadCommand = 0x01;
        static constexpr uint8_t kFileSaveCommand = 0x02;

        // File status codes
        static constexpr uint8_t kFileIdle = 0x00;
        static constexpr uint8_t kFileInProgress = 0x01;
        static constexpr uint8_t kFileSuccess = 0x02;
        static constexpr uint8_t kFileError = 0xFF;

        // Control register flags
        static constexpr uint8_t kDataAvailable = 0x01; // Bit 0: Data ready to read
        static constexpr uint8_t kBufferFull = 0x02; // Bit 1: Buffer is full
        static constexpr uint8_t kInterruptFlag = 0x04; // Bit 2: Interrupt flag
        static constexpr uint8_t kInterruptEnable = 0x08; ///< Bit 3: Interrupt enable

        /**
         * @brief Construct a new PIA instance
         *
         * Initializes the PIA registers, keyboard buffer, and file I/O state.
         * Sets up the circular buffer for keyboard input management.
         */
        PIA();

        /**
         * @brief Check if a memory address corresponds to PIA registers
         * @param address Memory address to check
         * @return bool true if address is within PIA range ($DC00-$DC21)
         */
        [[nodiscard]] bool isPiaAddress(uint16_t address) const;

        /**
         * @brief Write a value to PIA register space
         * @param address Memory address within PIA range
         * @param value 8-bit value to write to the register
         * @note Handles both traditional PIA registers and extended file I/O interface
         */
        void writePia(uint16_t address, uint8_t value);

        /**
         * @brief Read a value from PIA register space
         * @param address Memory address within PIA range
         * @return uint8_t Value from the specified PIA register
         * @note Reading keyboard data register automatically removes keypress from buffer
         */
        uint8_t readPia(uint16_t address);

        /**
         * @brief Add a keypress to the input buffer
         * @param ascii_code ASCII code of the key pressed (0-127)
         * @note If buffer is full, the keypress is ignored
         */
        void addKeypress(uint8_t ascii_code);

        /**
         * @brief Check if keyboard data is available in the buffer
         * @return bool true if at least one keypress is waiting in buffer
         */
        [[nodiscard]] bool hasKeypress() const;

        /**
         * @brief Get the next keypress from the buffer
         * @return uint8_t ASCII code of the next keypress (0 if buffer empty)
         * @note Removes the keypress from the buffer
         */
        uint8_t getKeypress();

        /**
         * @brief Clear all keypresses from the keyboard buffer
         * @note Resets buffer to empty state and updates control flags
         */
        void clearKeyboardBuffer();

        /**
         * @brief Check if the keyboard buffer is full
         * @return bool true if buffer cannot accept more keypresses
         */
        [[nodiscard]] bool isBufferFull() const;

        /**
         * @brief Check if keyboard data is available (same as hasKeypress)
         * @return bool true if at least one keypress is waiting
         */
        [[nodiscard]] bool isDataAvailable() const;

        /**
         * @brief Get the current number of keypresses in the buffer
         * @return uint8_t Number of keypresses waiting (0-32)
         */
        [[nodiscard]] uint8_t getBufferCount() const;

        /**
         * @brief Set the memory interface for file operations
         * @param memory Pointer to system memory interface
         * @note Required for L: (load) and S: (save) monitor commands
         */
        void setMemoryInterface(class Memory *memory);

        /**
         * @brief Check if a file operation is pending
         * @return bool true if load or save operation is queued
         */
        [[nodiscard]] bool hasFileOperation() const;

        /**
         * @brief Process pending file operations
         * @note Should be called regularly during CPU execution cycles
         */
        void processFileOperations();

    private:
        // Keyboard circular buffer
        std::array<uint8_t, kKeyboardBufferSize> keyboard_buffer_{};
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
        std::array<char, 12> filename_{};
        class Memory *memory_;

        // Helper functions
        [[nodiscard]] uint8_t addressToOffset(uint16_t address) const;
        void updateControlFlags();
        void incrementBufferHead();
        void incrementBufferTail();
    };
} // namespace Computer
