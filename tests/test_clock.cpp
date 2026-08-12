#include <gtest/gtest.h>
#include "computer/Computer6502.h"
#include "computer/CPU6502.h"
#include "computer/Memory.h"
namespace {
// runCycles must deliver the clock it advertises, and a jiffy every clockHz/60 of it.
TEST(Clock, RunCyclesHonoursTheStatedClock)
{
    Computer::Computer6502 box; box.power_on();
    const uint64_t hz = box.clockHz();
    EXPECT_EQ(hz, 4000000u);

    const uint64_t before = box.getCpu()->getCycles();
    box.runCycles(hz / 10);                       // a tenth of a second of machine time
    const uint64_t did = box.getCpu()->getCycles() - before;
    fprintf(stderr, "asked for %llu cycles, ran %llu\n",
            (unsigned long long)(hz / 10), (unsigned long long)did);
    EXPECT_GE(did, hz / 10);                      // at least the budget
    EXPECT_LT(did, hz / 10 + 8);                  // and no more than one instruction over
}

/* The jiffy comes out of runCycles(), not from a timer running beside it.
 *
 * That separation is what let the machine's two clocks disagree: the GUI ran a fixed
 * count of INSTRUCTIONS per host millisecond while a second QTimer pulsed the jiffy
 * at 60Hz, so CPU speed was whatever that loop bound worked out to -- about 3.5MHz --
 * and the two drifted under load. Tying the tick to cycles makes a second of machine
 * time contain sixty jiffies by construction, whatever the host is doing.
 *
 * Read through the kernel's own counter (JIFFY_LO/HI at $31/$32, what K_GET_JIFFIES
 * returns) so this checks the IRQ actually lands and is serviced, not merely that a
 * function was called. */
TEST(Clock, ASecondOfMachineTimeIsSixtyJiffies)
{
    Computer::Computer6502 box; box.power_on();
    box.runCycles(box.clockHz() / 4);             // boot far enough to be servicing IRQs

    Computer::Memory *mem = box.getMemory();
    const int before = mem->read(0x31) | (mem->read(0x32) << 8);
    box.runCycles(box.clockHz());                 // exactly one second
    const int after = mem->read(0x31) | (mem->read(0x32) << 8);

    const int jiffies = (after - before) & 0xFFFF;
    fprintf(stderr, "one second of machine time advanced the kernel jiffy by %d\n",
            jiffies);
    EXPECT_GE(jiffies, 58);
    EXPECT_LE(jiffies, 62);
}
}
