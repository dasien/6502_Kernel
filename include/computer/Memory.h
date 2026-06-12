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
    class BlockDevice;

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
     * - $0800-$8FFF: User RAM (~34KB) - Available for programs / module working RAM
     * - $9000-$AFFF: DOS ROM (8KB) - always-mapped FAT16 filesystem / DOS shell
     * - $B000-$DFFF: Module window (12KB) - bank 0 = RAM, banks 1..255 = ROM modules
     * - $E000-$FFFF: ROM area (8KB) - Kernel ROM (I/O page at $FE00, bank reg $FE23,
     *                block-device registers $FE24-$FE28)
     *
     * @see VIC, PIA, CPU6502
     */
    class Memory
    {
    public:
        /// DOS ROM ($9000-$AFFF, 8KB). Always-mapped, read-only ROM holding the
        /// resident FAT16 filesystem (and later the DOS shell). Unlike the module
        /// window it is never banked. When no image is installed the region falls
        /// through to RAM, preserving the pre-DOS memory map.
        static constexpr uint16_t kDosRomStart = 0x9000;
        static constexpr uint16_t kDosRomEnd = 0xAFFF;
        static constexpr size_t kDosRomSize = 0x2000; // 8 KB

        /// Bankable module window ($B000-$DFFF, 12KB). Backed by RAM when the
        /// selected bank is 0, or by a read-only module ROM for banks 1..255.
        static constexpr uint16_t kModuleWindowStart = 0xB000;
        static constexpr uint16_t kModuleWindowEnd = 0xDFFF;
        static constexpr size_t kModuleWindowSize = 0x3000; // 12 KB

        /// MODULE_BANK select register. Write n to map bank n into the window;
        /// read returns the current bank. Lives in the always-mapped I/O page.
        static constexpr uint16_t kModuleBankRegister = 0xFE23;

        /// Number of selectable banks (one byte of bank index: 0..255).
        static constexpr int kBankCount = 256;

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

        /**
         * @brief Set or update the block device for memory-mapped I/O
         * @param block_device Pointer to BlockDevice instance ($FE24-$FE28)
         */
        void setBlockDevice(BlockDevice *block_device);

        /**
         * @brief Install the always-mapped DOS ROM image ($9000-$AFFF)
         * @param image DOS ROM image; truncated/zero-padded to 8KB
         * @note Once installed the region is read-only (writes ignored). Passing
         *       an empty image leaves the region as RAM (the pre-DOS default).
         */
        void loadDosRom(const std::vector<uint8_t> &image);

        /**
         * @brief Whether a DOS ROM image has been installed
         */
        [[nodiscard]] bool isDosRomLoaded() const;

        /**
         * @brief Install a module ROM image into a bank (host bank table)
         * @param bank Bank index 1..255 (bank 0 is RAM and cannot be loaded)
         * @param image Module ROM image; truncated/zero-padded to 12KB
         * @note Pre-loaded once at startup; bank switching is just a pointer change.
         */
        void loadBank(uint8_t bank, const std::vector<uint8_t> &image);

        /**
         * @brief Map a bank into the module window (same effect as writing MODULE_BANK)
         * @param bank 0 = RAM, 1..255 = ROM module
         */
        void selectBank(uint8_t bank);

        /**
         * @brief Get the currently mapped bank
         * @return uint8_t Current bank (0 = RAM)
         */
        [[nodiscard]] uint8_t currentBank() const;

        /**
         * @brief Whether a ROM image has been installed for a bank
         * @param bank Bank index (bank 0 is RAM, always returns false)
         */
        [[nodiscard]] bool isBankLoaded(uint8_t bank) const;

    private:
        std::vector<uint8_t> ram_;    ///< 64KB system RAM storage
        VIC *video_chip_;             ///< Pointer to VIC for memory-mapped video I/O
        PIA *pia_;                    ///< Pointer to PIA for memory-mapped peripheral I/O
        BlockDevice *block_device_ = nullptr; ///< Block device ($FE24-$FE28), or null

        /// Module ROM images, indexed by bank (1..255). Each entry is either
        /// empty (no module installed) or exactly kModuleWindowSize bytes.
        std::vector<std::vector<uint8_t>> bank_rom_;
        uint8_t current_bank_ = 0;    ///< Bank mapped into $B000-$DFFF (0 = RAM)

        /// Always-mapped DOS ROM image ($9000-$AFFF). Empty = not installed
        /// (region behaves as RAM); otherwise exactly kDosRomSize bytes.
        std::vector<uint8_t> dos_rom_;
    };
} // namespace Computer

#endif // MEMORY_H
