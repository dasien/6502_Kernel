/**
 * @file TimingCircuit.h
 * @brief System Timing Circuit for 6502 Computer
 * @author 6502 Kernel Project
 */

#ifndef TIMINGCIRCUIT_H
#define TIMINGCIRCUIT_H

#include <cstdint>

namespace Computer
{
    /**
     * @class TimingCircuit
     * @brief System timing circuit for CPU cycle timing control
     *
     * This class manages timing operations for the 6502 computer system,
     * providing accurate cycle timing for proper CPU emulation. It helps
     * maintain realistic execution speeds and timing accuracy.
     *
     * Features:
     * - Target frequency management (typically 1 MHz for 6502)
     * - Cycle-accurate timing delays
     * - Actual frequency measurement and monitoring
     * - Precise timing control for emulation accuracy
     *
     * The timing circuit ensures that the emulated 6502 runs at realistic
     * speeds, preventing it from running too fast compared to original
     * hardware while maintaining smooth operation.
     *
     * @see CPU6502, Computer6502
     */
    class TimingCircuit
    {
    public:
        /**
         * @brief Construct a new TimingCircuit
         *
         * Initializes the timing circuit with default 6502 frequency
         * and sets up timing parameters for cycle-accurate emulation.
         */
        TimingCircuit();

        /**
         * @brief Wait for one CPU cycle to complete
         *
         * Delays execution to maintain proper timing for the next
         * CPU cycle. This ensures the emulated CPU runs at realistic
         * speeds rather than maximum host CPU speed.
         */
        void waitForCycle();

        /**
         * @brief Get the actual measured frequency of the emulation
         * @return double Actual frequency in Hz (cycles per second)
         * @note Used for performance monitoring and timing verification
         */
        [[nodiscard]] double getActualFrequency() const;

        /**
         * @brief Get the target frequency for the emulation
         * @return uint32_t Target frequency in Hz (typically 1,000,000 for 1 MHz)
         */
        [[nodiscard]] uint32_t getTargetFrequency() const;

    private:
        uint32_t clock_frequency_;    ///< Target clock frequency in Hz
        uint64_t cycle_time_ns_;      ///< Target cycle time in nanoseconds
        uint64_t actual_cycle_time_;  ///< Actual measured cycle time
    };
} // namespace Computer

#endif // TIMINGCIRCUIT_H
