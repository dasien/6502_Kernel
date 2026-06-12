#include "BlockDevice.h"

#include <fstream>

namespace Computer
{
    BlockDevice::BlockDevice(std::string image_path)
        : image_path_(std::move(image_path))
    {
    }

    void BlockDevice::setImagePath(const std::string &image_path)
    {
        image_path_ = image_path;
    }

    bool BlockDevice::isBlockAddress(const uint16_t address)
    {
        return address >= kRegLbaLo && address <= kRegData;
    }

    uint8_t BlockDevice::read(const uint16_t address)
    {
        switch (address)
        {
            case kRegLbaLo:
                return static_cast<uint8_t>(lba_ & 0x00FF);
            case kRegLbaHi:
                return static_cast<uint8_t>((lba_ >> 8) & 0x00FF);
            case kRegStatus:
                return status_;
            case kRegData:
            {
                // Expose the sector buffer through one address; auto-advance and
                // wrap so a 512-byte loop walks the whole sector.
                const uint8_t value = buffer_[index_];
                index_ = (index_ + 1) % kSectorSize;
                return value;
            }
            case kRegCmd:
            default:
                // BLK_CMD is write-only; reads return a benign 0.
                return 0x00;
        }
    }

    void BlockDevice::write(const uint16_t address, const uint8_t value)
    {
        switch (address)
        {
            case kRegLbaLo:
                lba_ = static_cast<uint16_t>((lba_ & 0xFF00) | value);
                index_ = 0; // new sector selected: fresh buffer pointer
                break;
            case kRegLbaHi:
                lba_ = static_cast<uint16_t>((lba_ & 0x00FF) | (value << 8));
                index_ = 0;
                break;
            case kRegCmd:
                if (value == kCmdReadSector)
                {
                    readSector();
                }
                else if (value == kCmdWriteSector)
                {
                    writeSector();
                }
                break;
            case kRegData:
                buffer_[index_] = value;
                index_ = (index_ + 1) % kSectorSize;
                break;
            case kRegStatus:
            default:
                // BLK_STATUS is read-only; ignore writes.
                break;
        }
    }

    void BlockDevice::readSector()
    {
        index_ = 0;

        // A fresh/short image reads back as zeros: start from a cleared buffer so
        // sectors past the current end-of-file are well-defined.
        buffer_.fill(0x00);

        std::ifstream image(image_path_, std::ios::binary);
        if (!image.is_open())
        {
            // No image yet is not an error - it behaves as an all-zero disk that
            // springs into existence on the first write.
            status_ = kStatusReady;
            return;
        }

        const std::streamoff offset =
            static_cast<std::streamoff>(lba_) * static_cast<std::streamoff>(kSectorSize);
        image.seekg(offset, std::ios::beg);
        if (image.good())
        {
            // read() may hit EOF for a sector beyond the image; the buffer stays
            // zero-filled for any bytes not present, which is the desired result.
            image.read(reinterpret_cast<char *>(buffer_.data()),
                       static_cast<std::streamsize>(kSectorSize));
        }
        status_ = kStatusReady;
    }

    void BlockDevice::writeSector()
    {
        // Open read/write without truncating so other sectors survive; create the
        // image on first use. fstream won't create a missing file in in|out mode,
        // so fall back to a create pass when the open fails.
        std::fstream image(image_path_,
                           std::ios::binary | std::ios::in | std::ios::out);
        if (!image.is_open())
        {
            std::ofstream create(image_path_, std::ios::binary | std::ios::app);
            create.close();
            image.open(image_path_,
                       std::ios::binary | std::ios::in | std::ios::out);
            if (!image.is_open())
            {
                status_ = kStatusError;
                return;
            }
        }

        const std::streamoff offset =
            static_cast<std::streamoff>(lba_) * static_cast<std::streamoff>(kSectorSize);
        image.seekp(offset, std::ios::beg);
        image.write(reinterpret_cast<const char *>(buffer_.data()),
                    static_cast<std::streamsize>(kSectorSize));
        image.flush();
        status_ = image.good() ? kStatusReady : kStatusError;
    }
} // namespace Computer
