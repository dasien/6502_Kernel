#include "ResetCircuit.h"
#include <thread>
#include <chrono>

namespace Computer {

ResetCircuit::ResetCircuit(CPU6502 &cpu) : cpu_ref_(cpu)
{
}

void ResetCircuit::triggerReset()
{
    // Reset circuit triggered
    cpu_ref_.reset();
}

void ResetCircuit::powerOnReset()
{
    // Power-on reset sequence initiated
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    triggerReset();
    // System ready
}

} // namespace Computer
