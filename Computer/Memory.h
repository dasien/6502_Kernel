#ifndef MEMORY_H
#define MEMORY_H

#include <vector>
#include <cstdint>

namespace Computer {
    class VIC;
    class PIA;

    class Memory
{
public:
    explicit Memory(VIC* video_chip = nullptr, PIA* pia = nullptr);

    uint8_t read(uint16_t address) const;

    void write(uint16_t address, uint8_t value);

    uint16_t readWord(uint16_t address) const;

    void writeWord(uint16_t address, uint16_t value);

    void loadProgram(const std::vector<uint8_t> &program, uint16_t start_address);

    // Video chip integration
    void setVideoChip(VIC* video_chip);
    
    // PIA integration
    void setPia(PIA* pia);

private:
    std::vector<uint8_t> ram_;
    VIC* video_chip_;
    PIA* pia_;
};

} // namespace Computer

#endif // MEMORY_H
