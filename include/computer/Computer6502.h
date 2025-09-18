#ifndef COMPUTER6502_H
#define COMPUTER6502_H

#include "Memory.h"
#include "CPU6502.h"
#include "ResetCircuit.h"
#include "TimingCircuit.h"
#include "VIC.h"
#include "PIA.h"

namespace Computer {

class Computer6502 {
public:
    Computer6502();
    
    void power_on();
    void run(int max_cycles = 100);
    void reset();
    
    // Getters for external access
    VIC* getVideoChip() { return &video_chip; }
    PIA* getPia() { return &pia; }
    CPU6502* getCpu() { return &cpu; }
    
private:
    VIC video_chip;
    PIA pia;
    Memory memory;
    CPU6502 cpu;
    ResetCircuit reset_circuit;
    TimingCircuit timing_circuit;
};

} // namespace Computer

#endif // COMPUTER6502_H