#include "Memory.h"
#include "VIC.h"
#include "PIA.h"

namespace Computer
{
    Memory::Memory(VIC *video_chip, PIA *pia) : ram_(0x10000, 0x00), video_chip_(video_chip), pia_(pia)
    {
    }

    uint8_t Memory::read(uint16_t address) const
    {
        // Check if this is a PIA register read
        if (pia_ && pia_->isPiaAddress(address))
        {
            return pia_->readPia(address);
        }

        // Check if this is a video memory read
        if (video_chip_ && video_chip_->isScreenAddress(address))
        {
            return video_chip_->readScreen(address);
        }

        return ram_[address];
    }

    void Memory::write(const uint16_t address, const uint8_t value)
    {
        // Check if this is a PIA register write
        if (pia_ && pia_->isPiaAddress(address))
        {
            pia_->writePia(address, value);
            return;
        }

        // Check if this is a video memory write
        if (video_chip_ && video_chip_->isScreenAddress(address))
        {
            video_chip_->writeScreen(address, value);
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
} // namespace Computer
