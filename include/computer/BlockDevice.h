/**
 * @file BlockDevice.h
 * @brief Memory-mapped block-device interface backed by a host disk image.
 * @author 6502 Kernel Project
 */

#ifndef BLOCKDEVICE_H
#define BLOCKDEVICE_H

#include <array>
#include <cstdint>
#include <string>

namespace Computer
{
    /**
     * @class BlockDevice
     * @brief A simple 512-byte-sector block device backed by a host `disk.img`.
     *
     * This is the storage foundation for MFC-DOS (the resident FAT16 filesystem
     * lives a layer above this; see docs/dos_design.md). Rather than emulating an
     * SD/SPI controller, the 6502 talks to a single host image file through four
     * memory-mapped registers in the I/O page, just after MODULE_BANK ($FE23):
     *
     * | Addr          | Name       | Purpose                                       |
     * |---------------|------------|-----------------------------------------------|
     * | $FE24-$FE25   | BLK_LBA    | 16-bit sector number (little-endian)          |
     * | $FE26         | BLK_CMD    | write 1 = read sector, 2 = write sector       |
     * | $FE27         | BLK_STATUS | 0 = ready, non-zero = error / no disk         |
     * | $FE28         | BLK_DATA   | 512-byte sector data port (auto-incrementing) |
     *
     * Read a sector:  set BLK_LBA, write BLK_CMD=1, then read BLK_DATA x512.
     * Write a sector: set BLK_LBA, write BLK_DATA x512, then write BLK_CMD=2.
     *
     * The data port exposes an internal 512-byte sector buffer through a single
     * address; each access auto-advances an index that wraps at 512. Setting
     * BLK_LBA (either byte) resets the index to 0, giving a clean start for the
     * next transfer; a READ command also refills the buffer and resets the index.
     *
     * The host image is opened lazily and created if absent (a fresh image reads
     * back as zeros and grows on write), so a missing disk.img is not an error.
     * Only a genuine open/I/O failure raises the error status.
     *
     * @see Memory, Computer6502
     */
    class BlockDevice
    {
    public:
        /// Register addresses in the always-mapped I/O page.
        static constexpr uint16_t kRegLbaLo = 0xFE24;
        static constexpr uint16_t kRegLbaHi = 0xFE25;
        static constexpr uint16_t kRegCmd = 0xFE26;
        static constexpr uint16_t kRegStatus = 0xFE27;
        static constexpr uint16_t kRegData = 0xFE28;

        /// Fixed sector size for the block interface.
        static constexpr size_t kSectorSize = 512;

        /// Command codes written to BLK_CMD.
        static constexpr uint8_t kCmdReadSector = 0x01;  ///< sector -> buffer
        static constexpr uint8_t kCmdWriteSector = 0x02; ///< buffer -> sector

        /// Status codes read from BLK_STATUS.
        static constexpr uint8_t kStatusReady = 0x00; ///< idle / last op OK
        static constexpr uint8_t kStatusError = 0xFF; ///< open or I/O failure

        /**
         * @brief Construct a block device bound to a host image path.
         * @param image_path Path to the backing disk image (opened lazily).
         */
        explicit BlockDevice(std::string image_path = "../disk.img");

        /**
         * @brief Point the device at a different host image.
         * @param image_path New backing image path; takes effect on next access.
         */
        void setImagePath(const std::string &image_path);

        /**
         * @brief Whether an address falls within the block-device registers.
         * @param address 16-bit address to test ($FE24-$FE28).
         */
        [[nodiscard]] static bool isBlockAddress(uint16_t address);

        /**
         * @brief Read a block-device register.
         * @param address Register address within $FE24-$FE28.
         * @return Register value (BLK_DATA advances the sector-buffer index).
         * @note Non-const: reading BLK_DATA mutates the auto-increment index.
         */
        uint8_t read(uint16_t address);

        /**
         * @brief Write a block-device register.
         * @param address Register address within $FE24-$FE28.
         * @param value Byte to write (BLK_CMD triggers a sector transfer).
         */
        void write(uint16_t address, uint8_t value);

    private:
        /// Read the sector at lba_ from the image into buffer_; reset index_.
        void readSector();
        /// Write buffer_ to the sector at lba_ in the image (growing it as needed).
        void writeSector();

        std::string image_path_;                  ///< host backing image
        std::array<uint8_t, kSectorSize> buffer_{}; ///< current sector buffer
        uint16_t lba_ = 0;                         ///< selected sector number
        size_t index_ = 0;                         ///< data-port index (0..511)
        uint8_t status_ = kStatusReady;            ///< last-operation status
    };
} // namespace Computer

#endif // BLOCKDEVICE_H
