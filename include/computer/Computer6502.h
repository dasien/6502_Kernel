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
#include "BlockDevice.h"
#include "Acia.h"
#include "Sid.h"
#include "Rtc.h"
#include "PowerSwitch.h"
#include "PowerSwitch.h"

namespace Computer
{
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
    class Computer6502
    {
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

        /// The machine's clock. Everything timed derives from this, so it is a stated
        /// number rather than a side effect of how often a host timer happens to fire.
        ///
        /// 4MHz. The classic home 6502 ran at about 1MHz -- Apple II at 1.023, PET and
        /// VIC-20 and C64 all near 1.0 -- but that was never the only figure: the Atari
        /// 800 and the NES clocked theirs at 1.79 and the BBC Micro at 2. The MFC is a
        /// WDC 65C02, a part sold at 1, 2, 4, 8 and 14MHz, and it has an 80x25 soft-font
        /// display with sprites and a FAT16 disc; 4MHz suits that machine better than
        /// the 1977 figure does.
        ///
        /// It is also, to within measurement, the speed the machine has always run at.
        /// The GUI used to execute 1000 INSTRUCTIONS per 1ms timer tick; at the 3.47
        /// cycles an instruction this workload averages, that is 3.47MHz. Naming it
        /// 4MHz changes nothing about how anything behaves -- it only makes the number
        /// something we chose instead of something we inherited from a loop bound.
        static constexpr uint32_t kDefaultClockHz = 4'000'000;

        /// Interval-timer ("jiffy") rate. Derived from the clock, so emulated time is
        /// internally consistent whether or not the host can keep up.
        static constexpr uint32_t kJiffyHz = 60;

        void setClockHz(uint32_t hz) { clock_hz_ = hz ? hz : kDefaultClockHz; }
        [[nodiscard]] uint32_t clockHz() const { return clock_hz_; }

        /**
         * @brief Execute a number of CPU INSTRUCTIONS.
         *
         * The raw primitive: no timing of any kind, and nothing is inferred about how
         * long they take. Use runCycles() for anything that should run at the machine's
         * speed.
         *
         * @param count How many instructions to execute
         * @note Stops early on an unknown instruction
         */
        void runInstructions(int count = 100);

        /**
         * @brief Run the machine for a number of CPU CYCLES, generating jiffy IRQs.
         *
         * The cycle-accurate entry point, and the one the GUI drives. Pulses the PIA's
         * interval timer every clockHz/kJiffyHz cycles, so the 60Hz tick is a property
         * of the emulated machine rather than of a host timer running alongside it --
         * which is what let the two disagree. They cannot now.
         *
         * @param cycles How many CPU cycles of work to perform
         * @note Stops early on an unknown instruction
         */
        void runCycles(uint64_t cycles);

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
        VIC *getVideoChip()
        {
            return &video_chip;
        }

        /**
         * @brief Get pointer to the block device
         * @return BlockDevice* Pointer to the $FE24-$FE28 block device
         * @note Used primarily for testing (e.g. pointing it at a temp image)
         */
        BlockDevice *getBlockDevice()
        {
            return &block_device;
        }

        /**
         * @brief Get pointer to the serial ACIA
         * @return Acia* Pointer to the $FE29-$FE2C serial port
         * @note Used to drive the "other end of the wire" (host RX/TX FIFOs)
         */
        Acia *getAcia()
        {
            return &acia;
        }

        /**
         * @brief Get pointer to the SID sound chip
         * @return Sid* Pointer to the $FE38-$FE54 sound chip
         * @note Used by the GUI audio bridge (QAudioSink) and by tests
         */
        Sid *getSid()
        {
            return &sid;
        }

        /**
         * @brief Get pointer to the real-time clock
         * @return Rtc* Pointer to the $FE55-$FE60 RTC
         * @note Used by tests to pin a known time via setTimeProvider
         */
        /**
         * @brief Has the machine been switched off at the power register?
         *
         * The host polls this and closes the window. Distinct from the CPU being
         * stopped: STP halts the processor, this cuts the power.
         */
        [[nodiscard]] bool isPoweredOff() const { return power.isOff(); }

        /**
         * @brief Get the soft power switch
         * @return PowerSwitch* Pointer to the $FE61 power register
         */
        PowerSwitch *getPowerSwitch()
        {
            return &power;
        }

        Rtc *getRtc()
        {
            return &rtc;
        }

        /**
         * @brief Get pointer to the peripheral interface adapter (PIA)
         * @return PIA* Pointer to the PIA for keyboard and file operations
         * @note Used primarily for testing and input simulation
         */
        PIA *getPia()
        {
            return &pia;
        }

        /**
         * @brief Get pointer to the CPU
         * @return CPU6502* Pointer to the 6502 CPU for direct access
         * @note Used primarily for debugging and testing
         */
        CPU6502 *getCpu()
        {
            return &cpu;
        }

        /**
         * @brief Get pointer to system memory
         * @return Memory* Pointer to the 64KB memory for direct read/write
         * @note Used primarily for testing (verifying command results in RAM)
         */
        Memory *getMemory()
        {
            return &memory;
        }

    private:
        /**
         * @brief Display fatal error message and exit program
         * @param message Error message to display to user
         * @note This function does not return - it terminates the program
         */
        void showFatalError(const std::string &message);

        VIC video_chip; ///< VIC-II video chip for screen output
        PIA pia; ///< Peripheral Interface Adapter for I/O
        uint32_t clock_hz_ = kDefaultClockHz;  ///< the machine's stated speed
        uint64_t next_jiffy_ = 0;              ///< cycle count the next jiffy IRQ is due
        BlockDevice block_device; ///< Block device backing the FAT16 disk image
        Acia acia; ///< Serial ACIA ($FE29-$FE2C) for XMODEM/serial transfers
        Sid sid; ///< SID sound chip ($FE38-$FE54)
        Rtc rtc; ///< real-time clock ($FE55-$FE60)
        PowerSwitch power; ///< soft power switch ($FE61)
        Memory memory; ///< 64KB system memory with memory-mapped I/O
        CPU6502 cpu; ///< MOS 65C02 microprocessor
        ResetCircuit reset_circuit; ///< Reset circuit for system initialization
        TimingCircuit timing_circuit; ///< System timing and synchronization
    };
} // namespace Computer

#endif // COMPUTER6502_H
