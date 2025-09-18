/**
 * @file Memory.h
 * @brief 64KB System Memory with Memory-Mapped I/O
 * @author 6502 Kernel Project
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <vector>
#include <cstdint>

namespace Computer
{
    class VIC;
    class PIA;

    /**
     * @class Memory
     * @brief 64KB system memory with memory-mapped I/O support
     *
     * This class implements the complete memory system for the 6502 computer,
     * providing 64KB of addressable memory space with memory-mapped I/O
     * integration for video chip (VIC) and peripheral interface (PIA).
     *
     * Memory Layout:
     * - $0000-$03FF: System RAM (1KB) - Zero page, stack, and system variables
     * - $0400-$07FF: Screen memory (1KB) - Character display data
     * - $0800-$CFFF: User RAM (51KB) - Available for programs
     * - $D000-$DFFF: I/O area (4KB) - Hardware registers (VIC, SID, CIA)
     * - $E000-$FFFF: ROM area (8KB) - Kernel and BASIC ROM
     *
     * @see VIC, PIA, CPU6502
     */
    class Memory
    {
    public:
        /**
         * @brief Construct a new Memory system
         * @param video_chip Pointer to VIC chip for memory-mapped video I/O
         * @param pia Pointer to PIA for memory-mapped peripheral I/O
         */
        explicit Memory(VIC *video_chip = nullptr, PIA *pia = nullptr);

        /**
         * @brief Read a byte from memory
         * @param address 16-bit memory address to read from
         * @return uint8_t Value at the specified memory address
         * @note Automatically handles memory-mapped I/O for VIC and PIA regions
         */
        [[nodiscard]] uint8_t read(uint16_t address) const;

        /**
         * @brief Write a byte to memory
         * @param address 16-bit memory address to write to
         * @param value 8-bit value to write
         * @note Automatically handles memory-mapped I/O for VIC and PIA regions
         */
        void write(uint16_t address, uint8_t value);

        /**
         * @brief Read a 16-bit word from memory (little-endian)
         * @param address Starting address to read from
         * @return uint16_t 16-bit value read in little-endian format
         * @note Reads low byte first, then high byte (6502 convention)
         */
        [[nodiscard]] uint16_t readWord(uint16_t address) const;

        /**
         * @brief Write a 16-bit word to memory (little-endian)
         * @param address Starting address to write to
         * @param value 16-bit value to write in little-endian format
         * @note Writes low byte first, then high byte (6502 convention)
         */
        void writeWord(uint16_t address, uint16_t value);

        /**
         * @brief Load a program or ROM data into memory
         * @param program Vector containing the program data to load
         * @param start_address Memory address where the program should be loaded
         * @note Used to load kernel ROM segments and user programs
         */
        void loadProgram(const std::vector<uint8_t> &program, uint16_t start_address);

        /**
         * @brief Set or update the video chip for memory-mapped I/O
         * @param video_chip Pointer to VIC chip instance
         */
        void setVideoChip(VIC *video_chip);

        /**
         * @brief Set or update the PIA for memory-mapped I/O
         * @param pia Pointer to PIA instance
         */
        void setPia(PIA *pia);

    private:
        std::vector<uint8_t> ram_;    ///< 64KB system RAM storage
        VIC *video_chip_;             ///< Pointer to VIC for memory-mapped video I/O
        PIA *pia_;                    ///< Pointer to PIA for memory-mapped peripheral I/O
    };
} // namespace Computer

#endif // MEMORY_H
