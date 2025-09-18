#include "TimingCircuit.h"
#include <chrono>
#include <thread>

namespace Computer
{
    TimingCircuit::TimingCircuit() : clock_frequency_(1000000), actual_cycle_time_(0)
    {
        cycle_time_ns_ = 1000000000 / clock_frequency_;
    }

    void TimingCircuit::waitForCycle()
    {
        const auto start = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::nanoseconds(cycle_time_ns_));
        const auto end = std::chrono::high_resolution_clock::now();

        actual_cycle_time_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }

    double TimingCircuit::getActualFrequency() const
    {
        if (actual_cycle_time_ == 0) return 0.0;
        return 1000000000.0 / static_cast<double>(actual_cycle_time_);
    }

    uint32_t TimingCircuit::getTargetFrequency() const
    {
        return clock_frequency_;
    }
} // namespace Computer
