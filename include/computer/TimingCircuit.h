#ifndef TIMINGCIRCUIT_H
#define TIMINGCIRCUIT_H

#include <cstdint>

namespace Computer {

class TimingCircuit
{
public:
    TimingCircuit();

    void waitForCycle();

    double getActualFrequency() const;

    uint32_t getTargetFrequency() const;

private:
    uint32_t clock_frequency_;
    uint64_t cycle_time_ns_;
    uint64_t actual_cycle_time_;
};

} // namespace Computer

#endif // TIMINGCIRCUIT_H
