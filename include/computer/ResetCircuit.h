/**
 * @file ResetCircuit.h
 * @brief System Reset Circuit for 6502 Computer
 * @author 6502 Kernel Project
 */

#ifndef RESETCIRCUIT_H
#define RESETCIRCUIT_H

#include "CPU6502.h"

namespace Computer
{
    /**
     * @class ResetCircuit
     * @brief System reset circuit for proper 6502 initialization
     *
     * This class manages reset operations for the 6502 computer system,
     * providing both power-on reset and manual reset functionality.
     * It ensures the CPU is properly initialized according to 6502
     * specifications.
     *
     * The reset circuit handles:
     * - Power-on reset sequence for system startup
     * - Manual reset triggered by user or system events
     * - Proper CPU state initialization
     * - Reset vector loading from memory locations $FFFC-$FFFD
     *
     * @see CPU6502, Computer6502
     */
    class ResetCircuit
    {
    public:
        /**
         * @brief Construct a new ResetCircuit
         * @param cpu Reference to the CPU to control
         */
        explicit ResetCircuit(CPU6502 &cpu);

        /**
         * @brief Trigger a manual system reset
         *
         * Performs a warm reset of the system, similar to pressing
         * a reset button. The CPU is reset to initial state and
         * program execution begins at the reset vector address.
         */
        void triggerReset();

        /**
         * @brief Perform power-on reset sequence
         *
         * Executes the complete power-on reset sequence for system
         * startup. This is typically called once when the computer
         * is first powered on or after loading the kernel ROM.
         */
        void powerOnReset();

    private:
        CPU6502 &cpu_ref_; ///< Reference to the CPU being controlled
    };
} // namespace Computer

#endif // RESETCIRCUIT_H
