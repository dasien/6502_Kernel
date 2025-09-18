/**
 * @file Computer6502.h
 * @brief Main 6502 Computer System Emulator Class
 * @author 6502 Kernel Project
 */

#ifndef COMPUTER6502_H
#define COMPUTER6502_H

#include "Memory.h"
#include "CPU6502.h"
#include "ResetCircuit.h"
#include "TimingCircuit.h"
#include "VIC.h"
#include "PIA.h"

namespace Computer {

/**
 * @class Computer6502
 * @brief Complete 6502-based computer system emulator
 *
 * This class represents a complete 6502-based computer system with all necessary
 * components including CPU, memory, video chip (VIC), peripheral interface (PIA),
 * and timing circuits. It loads and executes the 6502 kernel ROM and provides
 * an interactive monitor environment.
 *
 * The system emulates a Commodore 64-like architecture with:
 * - 64KB of addressable memory
 * - VIC-II video chip for screen output
 * - PIA for keyboard input and file operations
 * - Complete 6502 CPU instruction set
 * - Reset and timing circuits for proper initialization
 *
 * @see CPU6502, Memory, VIC, PIA
 */
class Computer6502 {
public:
    /**
     * @brief Construct a new Computer6502 system
     *
     * Initializes all system components and connects them together.
     * The system is ready to be powered on after construction.
     */
    Computer6502();

    /**
     * @brief Power on the computer system
     *
     * Loads the kernel ROM and MAP files, initializes all hardware components,
     * and performs a power-on reset. The system will be ready to execute
     * the 6502 monitor program after this call.
     *
     * @throws std::runtime_error if kernel.rom or kernel.map cannot be loaded
     * @note ROM and MAP files must exist at ../kernel.rom and ../kernel.map
     *       relative to the executable location
     */
    void power_on();

    /**
     * @brief Execute CPU instructions for a specified number of cycles
     *
     * Runs the 6502 CPU for the specified number of instruction cycles.
     * Each cycle represents one CPU instruction execution. File operations
     * and other system tasks are processed between instruction executions.
     *
     * @param max_cycles Maximum number of CPU instruction cycles to execute
     *                   Default is 100 cycles
     * @note Execution may stop early if an unknown instruction is encountered
     */
    void run(int max_cycles = 100);

    /**
     * @brief Reset the computer system
     *
     * Triggers a system reset, reinitializing the CPU and all components
     * to their default state. The program counter will be loaded from
     * the reset vector at $FFFC-$FFFD.
     */
    void reset();

    /**
     * @brief Get pointer to the video chip (VIC)
     * @return VIC* Pointer to the VIC video chip for screen operations
     * @note Used primarily for testing and screen buffer access
     */
    VIC* getVideoChip() { return &video_chip; }

    /**
     * @brief Get pointer to the peripheral interface adapter (PIA)
     * @return PIA* Pointer to the PIA for keyboard and file operations
     * @note Used primarily for testing and input simulation
     */
    PIA* getPia() { return &pia; }

    /**
     * @brief Get pointer to the CPU
     * @return CPU6502* Pointer to the 6502 CPU for direct access
     * @note Used primarily for debugging and testing
     */
    CPU6502* getCpu() { return &cpu; }

private:
    /**
     * @brief Display fatal error message and exit program
     * @param message Error message to display to user
     * @note This function does not return - it terminates the program
     */
    void showFatalError(const std::string& message);

    VIC video_chip;           ///< VIC-II video chip for screen output
    PIA pia;                  ///< Peripheral Interface Adapter for I/O
    Memory memory;            ///< 64KB system memory with memory-mapped I/O
    CPU6502 cpu;              ///< MOS 65C02 microprocessor
    ResetCircuit reset_circuit; ///< Reset circuit for system initialization
    TimingCircuit timing_circuit; ///< System timing and synchronization
};

} // namespace Computer

#endif // COMPUTER6502_H