#include "Memory.h"
#include "VIC.h"
#include "PIA.h"
#include "BlockDevice.h"
#include "Acia.h"
#include "Sid.h"
#include "Rtc.h"
#include "PowerSwitch.h"

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

        // Check if this is an ACIA (serial) register read ($FE29-$FE2C)
        if (acia_ && Acia::isAciaAddress(address))
        {
            return acia_->read(address);
        }

        // Check if this is a VIC video-register read ($FE2D-$FE37, plus the
        // soft-font port at $FE62-$FE64). Neither the screen nor the font is in
        // the 64K map -- both live behind these register ports.
        if (video_chip_ && VIC::isVideoRegAddress(address))
        {
            return video_chip_->read(address);
        }

        // Check if this is a SID sound-chip register read ($FE38-$FE54).
        if (sid_ && Sid::isSidAddress(address))
        {
            return sid_->read(address);
        }

        // Check if this is an RTC register read ($FE55-$FE60).
        if (power_ && PowerSwitch::isPowerAddress(address))
        {
            return power_->read(address);
        }

        if (rtc_ && Rtc::isRtcAddress(address))
        {
            return rtc_->read(address);
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

        // Check if this is an ACIA (serial) register write ($FE29-$FE2C)
        if (acia_ && Acia::isAciaAddress(address))
        {
            acia_->write(address, value);
            return;
        }

        // Check if this is a VIC video-register write ($FE2D-$FE37, plus the
        // soft-font port at $FE62-$FE64). The screen
        // itself is not in the 64K map -- it lives behind this register port.
        if (video_chip_ && VIC::isVideoRegAddress(address))
        {
            video_chip_->write(address, value);
            return;
        }

        // Check if this is a SID sound-chip register write ($FE38-$FE54).
        if (sid_ && Sid::isSidAddress(address))
        {
            sid_->write(address, value);
            return;
        }

        // Check if this is an RTC register write ($FE55-$FE60; RTC_LATCH).
        if (power_ && PowerSwitch::isPowerAddress(address))
        {
            power_->write(address, value);
            return;
        }

        if (rtc_ && Rtc::isRtcAddress(address))
        {
            rtc_->write(address, value);
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

        // Kernel ROM is read-only once installed. The I/O page ($FE00-$FE60) sits
        // inside this range but is matched above, so it still reaches its devices.
        if (rom_write_protect_ && address >= kKernelRomStart)
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

    // NOTE: writeWord and loadProgram write ram_ directly, bypassing I/O decoding,
    // bank routing and ROM protection. That is deliberate -- they are the loader /
    // test back door (ROM images are installed through loadProgram, and the CPU
    // tests seed the $FFFE vector with writeWord). Guest code never reaches them;
    // it goes through write(). Both used to index past ram_: `address + 1` promotes
    // to int, so writeWord(0xFFFF) wrote ram_[0x10000], and loadProgram had no
    // bound at all -- an oversized image ran off the end of the vector.
    void Memory::writeWord(uint16_t address, uint16_t value)
    {
        ram_[address] = value & 0xFF;
        ram_[static_cast<uint16_t>(address + 1)] = (value >> 8) & 0xFF; // wraps at $FFFF
    }

    void Memory::loadProgram(const std::vector<uint8_t> &program, uint16_t start_address)
    {
        const size_t n = std::min(program.size(), ram_.size() - start_address);
        for (size_t i = 0; i < n; ++i)
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

    void Memory::setAcia(Acia *acia)
    {
        acia_ = acia;
    }

    void Memory::setSid(Sid *sid)
    {
        sid_ = sid;
    }

    void Memory::setRtc(Rtc *rtc)
    {
        rtc_ = rtc;
    }

    void Memory::setPowerSwitch(PowerSwitch *power)
    {
        power_ = power;
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
