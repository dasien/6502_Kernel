#ifndef RESETCIRCUIT_H
#define RESETCIRCUIT_H

#include "CPU6502.h"

namespace Computer {

class ResetCircuit
{
public:
    explicit ResetCircuit(CPU6502 &cpu);

    void triggerReset();

    void powerOnReset();

private:
    CPU6502 &cpu_ref_;
};

} // namespace Computer

#endif // RESETCIRCUIT_H
