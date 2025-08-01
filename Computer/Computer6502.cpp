#include "Computer6502.h"
#include <vector>
#include <fstream>

namespace Computer {

Computer6502::Computer6502() : video_chip(), pia(), memory(&video_chip, &pia), cpu(memory), reset_circuit(cpu), timing_circuit() {}

void Computer6502::power_on() {
    // 6502 Computer System Starting
    
    // Set up reset vector to point to kernel ROM at $F000
    memory.writeWord(0xFFFC, 0xF000);  // Reset vector points to $F000
    
    // Load kernel ROM from file
    std::vector<uint8_t> kernel_rom;
    
    // Try to load kernel.rom from the current directory
    std::ifstream rom_file("kernel.rom", std::ios::binary | std::ios::ate);
    if (!rom_file.is_open()) {
        // Error: Could not open kernel.rom file
        return;
    }
    
    // Get file size and read the entire ROM
    std::streamsize size = rom_file.tellg();
    rom_file.seekg(0, std::ios::beg);
    
    kernel_rom.resize(size);
    if (!rom_file.read(reinterpret_cast<char*>(kernel_rom.data()), size))
    {
        // Error: Failed to read kernel.rom file
        return;
    }
    
    rom_file.close();
    
    // Loaded kernel ROM
    
    // Load kernel ROM at $F000
    memory.loadProgram(kernel_rom, 0xF000);
    
    // Power-on reset
    reset_circuit.powerOnReset();
    
    // Initial CPU state setup complete
}

void Computer6502::run(int max_cycles) {
    // Starting execution
    
    for (int i = 0; i < max_cycles; ++i) {
        if (!cpu.executeSingleInstruction()) {
            // Execution stopped due to unknown instruction
            break;
        }
        
        // Status updates handled by UI
    }
    
    // Execution completed
}

void Computer6502::reset() {
    reset_circuit.triggerReset();
}

} // namespace Computer