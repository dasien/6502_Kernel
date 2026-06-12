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

        // Route the block-device registers ($FE24-$FE28) through memory. The
        // image is created on first write, so a missing disk.img is harmless.
        memory.setBlockDevice(&block_device);
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

        // Module bank table: pre-load each module ROM into a switchable bank in
        // the $B000-$DFFF window (MODULE_BANK selects one; the kernel B: menu maps
        // them on demand). Banks must match the kernel's MODULE_DIR: 1 = BASIC,
        // 2 = dev tools. Add new modules here and in MODULE_DIR together.
        auto installBank = [this](uint8_t bank, const std::string &path, const char *name)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                std::cout << "Warning: " << name << " ROM not found at " << path
                          << " - bank " << static_cast<int>(bank) << " will be empty\n";
                return;
            }
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> image(size);
            if (file.read(reinterpret_cast<char *>(image.data()), size))
            {
                memory.loadBank(bank, image);
                std::cout << name << " ROM installed as module bank "
                          << static_cast<int>(bank) << " (" << size << " bytes)\n";
            }
        };

        installBank(1, "../kernel/basic.rom", "BASIC");
        installBank(2, "../kernel/devtools.rom", "DEV TOOLS");

        // Always-mapped DOS ROM at $9000-$AFFF (resident FAT16 filesystem / DOS
        // shell). If absent, the region stays RAM and the machine boots as before.
        {
            std::ifstream dos_file("../kernel/dos.rom", std::ios::binary | std::ios::ate);
            if (dos_file.is_open())
            {
                std::streamsize dos_size = dos_file.tellg();
                dos_file.seekg(0, std::ios::beg);
                std::vector<uint8_t> dos_image(dos_size);
                if (dos_file.read(reinterpret_cast<char *>(dos_image.data()), dos_size))
                {
                    memory.loadDosRom(dos_image);
                    std::cout << "DOS ROM installed at $9000-$AFFF (" << dos_size
                              << " bytes)\n";
                }
            }
            else
            {
                std::cout << "Warning: DOS ROM not found at ../kernel/dos.rom - "
                             "$9000-$AFFF will be RAM\n";
            }
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
