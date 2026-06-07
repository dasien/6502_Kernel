#include "Computer6502.h"
#include "MapFileParser.h"
#include <vector>
#include <fstream>
#include <iostream>
#include <cstdlib>

#ifdef QT_GUI
#include <QMessageBox>
#include <QApplication>
#endif

namespace Computer
{
    Computer6502::Computer6502() : memory(&video_chip, &pia), cpu(memory), reset_circuit(cpu)
    {
        // Connect PIA to memory for file operations
        pia.setMemoryInterface(&memory);

        // Let the PIA acknowledge (deassert) the CPU IRQ line when the timer
        // interrupt is serviced.
        pia.setCpu(&cpu);
    }

    void Computer6502::showFatalError(const std::string& message)
    {
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

    void Computer6502::power_on()
    {
        // The kernel ROM file.
        std::vector<uint8_t> kernel_rom;

        // Set path for ROM file.
        const std::string rom_path = "../kernel/kernel.rom";

        // Try to open it.
        std::ifstream rom_file(rom_path, std::ios::binary | std::ios::ate);

        // Check to see if we found the file.
        if (!rom_file.is_open())
        {
            // Show error.
            showFatalError("Could not open kernel.rom file at expected location: " + rom_path);
        }

        // Get file size.
        std::streamsize size = rom_file.tellg();

        // Go back to beginning of file.
        rom_file.seekg(0, std::ios::beg);

        // Allocate vector space.
        kernel_rom.resize(size);

        // Read the file.
        if (!rom_file.read(reinterpret_cast<char *>(kernel_rom.data()), size))
        {
            // If read failed, inform user.
            showFatalError("Failed to read kernel.rom file.");
        }

        // Close file stream.
        rom_file.close();

        // Parser for map file.
        MapFileParser parser;

        // Set path for map file.
        const std::string map_path = "../kernel/kernel.map";

        // Try to open it
        std::ifstream map_file(map_path);

        // Check to see if it was found.
        if (!map_file.is_open())
        {
            // Show error.
            showFatalError("Could not find kernel.map file at expected location: " + map_path);
        }

        // Close the file.
        map_file.close();

        // Parse the map file segments.
        auto segments = parser.parseMapFile(map_path);

        // Check to see if any were created.
        if (segments.empty())
        {
            showFatalError("Could not parse kernel.map file.");
        }

        // Create segments.
        SegmentInfo *codeSegment = parser.findSegment(segments, "CODE");
        SegmentInfo *jumpsSegment = parser.findSegment(segments, "JUMPS");
        SegmentInfo *vecsSegment = parser.findSegment(segments, "VECS");

        // Check to see if any are missing.
        if (!codeSegment || !jumpsSegment || !vecsSegment)
        {
            // Show error.
            std::string missingSegments = "Missing required segments in kernel.map file:\n\n";
            if (!codeSegment) missingSegments += "• CODE segment (main kernel code)\n";
            if (!jumpsSegment) missingSegments += "• JUMPS segment (kernel API functions)\n";
            if (!vecsSegment) missingSegments += "• VECS segment (interrupt vectors)\n";

            showFatalError(missingSegments);
        }

        // Load segments using correct ROM file offsets
        // The ROM file is laid out with segments at their actual memory addresses
        // ROM is 8KB (0x2000) starting at $E000, so ROM offset = memory_address - 0xE000
        size_t codeOffset = codeSegment->start - 0xE000;
        size_t jumpsOffset = jumpsSegment->start - 0xE000;
        size_t vecsOffset = vecsSegment->start - 0xE000;

        // Load CODE segment.
        memory.loadProgram(
            std::vector<uint8_t>(kernel_rom.begin() + codeOffset,
                                 kernel_rom.begin() + codeOffset + codeSegment->size),
            codeSegment->start
        );

        // Load JUMPS segment.
        memory.loadProgram(
            std::vector<uint8_t>(kernel_rom.begin() + jumpsOffset,
                                 kernel_rom.begin() + jumpsOffset + jumpsSegment->size),
            jumpsSegment->start
        );

        // Load VECS segment.
        memory.loadProgram(
            std::vector<uint8_t>(kernel_rom.begin() + vecsOffset,
                                 kernel_rom.begin() + vecsOffset + vecsSegment->size),
            vecsSegment->start
        );

        // Load BASIC ROM at $B000-$DFFF
        const std::string basic_rom_path = "../kernel/basic.rom";
        std::ifstream basic_rom_file(basic_rom_path, std::ios::binary | std::ios::ate);

        if (basic_rom_file.is_open())
        {
            // Get BASIC ROM size
            std::streamsize basic_size = basic_rom_file.tellg();
            basic_rom_file.seekg(0, std::ios::beg);

            // Read BASIC ROM
            std::vector<uint8_t> basic_rom(basic_size);
            if (basic_rom_file.read(reinterpret_cast<char *>(basic_rom.data()), basic_size))
            {
                // Load BASIC ROM into memory at $B000
                memory.loadProgram(basic_rom, 0xB000);
                std::cout << "BASIC ROM loaded successfully at $B000 (" << basic_size << " bytes)\n";
            }
            basic_rom_file.close();
        }
        else
        {
            std::cout << "Warning: BASIC ROM not found at " << basic_rom_path << " - B: command will show error\n";
        }

        // Power-on reset
        reset_circuit.powerOnReset();
    }

    void Computer6502::run(const int max_cycles)
    {
        // Starting execution
        for (int i = 0; i < max_cycles; ++i)
        {
            if (!cpu.executeSingleInstruction())
            {
                // Execution stopped due to unknown instruction
                break;
            }

            // Process any pending file operations
            pia.processFileOperations();
        }
    }

    void Computer6502::reset()
    {
        reset_circuit.triggerReset();
    }
}
