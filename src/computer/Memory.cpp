#include "Memory.h"
#include "VIC.h"
#include "PIA.h"
#include "BlockDevice.h"

#include <algorithm>

namespace Computer
{
    Memory::Memory(VIC *video_chip, PIA *pia)
        : ram_(0x10000, 0x00), video_chip_(video_chip), pia_(pia),
          bank_rom_(kBankCount)
    {
    }

    uint8_t Memory::read(uint16_t address) const
    {
        // MODULE_BANK select register reads back the current bank
        if (address == kModuleBankRegister)
        {
            return current_bank_;
        }

        // Check if this is a PIA register read
        if (pia_ && pia_->isPiaAddress(address))
        {
            return pia_->readPia(address);
        }

        // Check if this is a block-device register read ($FE24-$FE28)
        if (block_device_ && BlockDevice::isBlockAddress(address))
        {
            return block_device_->read(address);
        }

        // Check if this is a video memory read
        if (video_chip_ && video_chip_->isScreenAddress(address))
        {
            return video_chip_->readScreen(address);
        }

        // DOS ROM: always-mapped read-only region. Falls through to RAM when no
        // image is installed (the pre-DOS default).
        if (!dos_rom_.empty() && address >= kDosRomStart && address <= kDosRomEnd)
        {
            return dos_rom_[address - kDosRomStart];
        }

        // Module window: a non-zero bank maps a read-only ROM module here.
        // Bank 0 falls through to RAM (the default/boot state).
        if (current_bank_ != 0 && address >= kModuleWindowStart && address <= kModuleWindowEnd)
        {
            const std::vector<uint8_t> &image = bank_rom_[current_bank_];
            // An empty (uninstalled) bank reads as open bus -> 0x00.
            return image.empty() ? 0x00 : image[address - kModuleWindowStart];
        }

        return ram_[address];
    }

    void Memory::write(const uint16_t address, const uint8_t value)
    {
        // MODULE_BANK select register: map a bank into the module window
        if (address == kModuleBankRegister)
        {
            current_bank_ = value;
            return;
        }

        // Check if this is a PIA register write
        if (pia_ && pia_->isPiaAddress(address))
        {
            pia_->writePia(address, value);
            return;
        }

        // Check if this is a block-device register write ($FE24-$FE28)
        if (block_device_ && BlockDevice::isBlockAddress(address))
        {
            block_device_->write(address, value);
            return;
        }

        // Check if this is a video memory write
        if (video_chip_ && video_chip_->isScreenAddress(address))
        {
            video_chip_->writeScreen(address, value);
            return;
        }

        // DOS ROM is read-only: ignore writes when an image is installed.
        if (!dos_rom_.empty() && address >= kDosRomStart && address <= kDosRomEnd)
        {
            return;
        }

        // Module window backed by ROM (non-zero bank): writes are ignored.
        // Bank 0 falls through to RAM.
        if (current_bank_ != 0 && address >= kModuleWindowStart && address <= kModuleWindowEnd)
        {
            return;
        }

        ram_[address] = value;
    }

    uint16_t Memory::readWord(const uint16_t address) const
    {
        uint8_t low = read(address);
        uint8_t high = read(address + 1);
        return low | (high << 8);
    }

    void Memory::writeWord(uint16_t address, uint16_t value)
    {
        ram_[address] = value & 0xFF;
        ram_[address + 1] = (value >> 8) & 0xFF;
    }

    void Memory::loadProgram(const std::vector<uint8_t> &program, uint16_t start_address)
    {
        for (size_t i = 0; i < program.size(); ++i)
        {
            ram_[start_address + i] = program[i];
        }
    }

    void Memory::setVideoChip(VIC *video_chip)
    {
        video_chip_ = video_chip;
    }

    void Memory::setPia(PIA *pia)
    {
        pia_ = pia;
    }

    void Memory::setBlockDevice(BlockDevice *block_device)
    {
        block_device_ = block_device;
    }

    void Memory::loadBank(uint8_t bank, const std::vector<uint8_t> &image)
    {
        // Bank 0 is RAM, not a ROM bank - nothing to install.
        if (bank == 0)
        {
            return;
        }

        std::vector<uint8_t> &dst = bank_rom_[bank];
        dst.assign(kModuleWindowSize, 0x00);
        const size_t n = std::min(image.size(), kModuleWindowSize);
        std::copy_n(image.begin(), n, dst.begin());
    }

    void Memory::loadDosRom(const std::vector<uint8_t> &image)
    {
        if (image.empty())
        {
            dos_rom_.clear(); // leaves the region as RAM
            return;
        }
        dos_rom_.assign(kDosRomSize, 0x00);
        const size_t n = std::min(image.size(), kDosRomSize);
        std::copy_n(image.begin(), n, dos_rom_.begin());
    }

    bool Memory::isDosRomLoaded() const
    {
        return !dos_rom_.empty();
    }

    void Memory::selectBank(uint8_t bank)
    {
        current_bank_ = bank;
    }

    uint8_t Memory::currentBank() const
    {
        return current_bank_;
    }

    bool Memory::isBankLoaded(uint8_t bank) const
    {
        return bank != 0 && !bank_rom_[bank].empty();
    }
} // namespace Computer
