#include "Computer6502.h"
#include "MapFileParser.h"
#include <vector>
#include <fstream>
#include <cstdlib>

#ifdef QT_GUI
#include <QMessageBox>
#include <QApplication>
#endif

namespace Computer {

// Helper function to show fatal error and exit
void showFatalError(const std::string& message) {
#ifdef QT_GUI
    QMessageBox::critical(nullptr, "6502 Computer System - Fatal Error", 
                         QString::fromStdString(message));
#else
    fprintf(stderr, "FATAL ERROR: %s\n", message.c_str());
#endif
    fprintf(stderr, "FATAL ERROR: %s\n", message.c_str());
    fflush(stderr);
    std::exit(1);
}

Computer6502::Computer6502() : video_chip(), pia(), memory(&video_chip, &pia), cpu(memory), reset_circuit(cpu), timing_circuit() {
    // Connect PIA to memory for file operations
    pia.setMemoryInterface(&memory);
}

void Computer6502::power_on() {
    // 6502 Computer System Starting
    
    // Reset vector will be loaded from VECS segment
    
    // Load kernel ROM from file
    std::vector<uint8_t> kernel_rom;
    
    // Try to load kernel.rom from the current directory
    std::ifstream rom_file("kernel.rom", std::ios::binary | std::ios::ate);
    if (!rom_file.is_open()) {
        showFatalError("Could not open kernel.rom file.\n\n"
                      "Make sure the kernel.rom file exists in the build directory.\n"
                      "This file should be automatically generated during the build process.");
    }
    
    // Get file size and read the entire ROM
    std::streamsize size = rom_file.tellg();
    rom_file.seekg(0, std::ios::beg);
    
    kernel_rom.resize(size);
    if (!rom_file.read(reinterpret_cast<char*>(kernel_rom.data()), size))
    {
        showFatalError("Failed to read kernel.rom file.\n\n"
                      "The file may be corrupted or there may be insufficient memory.");
    }
    
    rom_file.close();
    
    // Loaded kernel ROM
    
    // Parse map file to get segment layout
    
    MapFileParser parser;
    auto segments = parser.parseMapFile("kernel.map");
    
    if (segments.empty()) {
        showFatalError("Could not parse kernel.map file.\n\n"
                      "Make sure the kernel.map file exists in the build directory.\n"
                      "This file should be automatically generated during the build process.");
    }
    
    // Find each segment
    SegmentInfo* codeSegment = parser.findSegment(segments, "CODE");
    SegmentInfo* jumpsSegment = parser.findSegment(segments, "JUMPS");
    SegmentInfo* vecsSegment = parser.findSegment(segments, "VECS");
    
    if (!codeSegment || !jumpsSegment || !vecsSegment) {
        std::string missingSegments = "Missing required segments in kernel.map file:\n\n";
        if (!codeSegment) missingSegments += "• CODE segment (main kernel code)\n";
        if (!jumpsSegment) missingSegments += "• JUMPS segment (kernel API functions)\n";
        if (!vecsSegment) missingSegments += "• VECS segment (interrupt vectors)\n";
        missingSegments += "\nThe kernel ROM may be corrupted or built incorrectly.";
        showFatalError(missingSegments);
    }
    
    // Load segments using correct file offsets
    // The segments are stored sequentially in ROM file: CODE, then JUMPS, then VECS
    size_t codeOffset = 0;
    size_t jumpsOffset = codeSegment->size;
    size_t vecsOffset = codeSegment->size + jumpsSegment->size;
    
    // Load CODE segment
    memory.loadProgram(
        std::vector<uint8_t>(kernel_rom.begin() + codeOffset, 
                           kernel_rom.begin() + codeOffset + codeSegment->size),
        codeSegment->start
    );
    
    // Load JUMPS segment  
    memory.loadProgram(
        std::vector<uint8_t>(kernel_rom.begin() + jumpsOffset,
                           kernel_rom.begin() + jumpsOffset + jumpsSegment->size),
        jumpsSegment->start
    );
    
    // Load VECS segment
    memory.loadProgram(
        std::vector<uint8_t>(kernel_rom.begin() + vecsOffset,
                           kernel_rom.begin() + vecsOffset + vecsSegment->size),
        vecsSegment->start
    );

    // Power-on reset
    reset_circuit.powerOnReset();
}

void Computer6502::run(int max_cycles) {
    // Starting execution
    
    for (int i = 0; i < max_cycles; ++i) {
        if (!cpu.executeSingleInstruction()) {
            // Execution stopped due to unknown instruction
            break;
        }
        
        // Process any pending file operations
        pia.processFileOperations();
        
        // Status updates handled by UI
    }
    
    // Execution completed
}

void Computer6502::reset() {
    reset_circuit.triggerReset();
}

} // namespace Computer