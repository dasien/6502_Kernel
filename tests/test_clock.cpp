#include <gtest/gtest.h>
#include "computer/Computer6502.h"
#include "computer/CPU6502.h"
#include "computer/Memory.h"
#include "computer/VIC.h"
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

/* The frame boundary is the same event as the jiffy, and the host is told about it.
 *
 * Before this the machine had two 60 Hz clocks that were never phase-locked: the
 * jiffy came off emulated cycles while the display repainted on a wall-clock QTimer
 * of the host's own. They drifted, and under stall the jiffies bunched while the
 * display showed a single frame -- so no instant existed that a program could know
 * was safe to paint in. Ticking the frame with the jiffy is what makes VREG_FRAME
 * mean something. */
TEST(Clock, ASecondOfMachineTimeIsSixtyFrames)
{
    Computer::Computer6502 box; box.power_on();
    Computer::VIC *vic = box.getVideoChip();

    const uint8_t before = vic->read(Computer::VIC::kRegFrame);
    box.runCycles(box.clockHz());                 // exactly one second
    const int frames = (vic->read(Computer::VIC::kRegFrame) - before) & 0xFF;

    EXPECT_GE(frames, 58);
    EXPECT_LE(frames, 62);
}

/* The host asks "has anything happened since I last painted", and gets one answer
 * however many boundaries went by. Painting four times into one display refresh
 * buys nothing, and after a stall the catch-up cap makes several boundaries in one
 * slice ordinary rather than exceptional. */
TEST(Clock, FramesElapsedCoalescesAndClears)
{
    Computer::Computer6502 box; box.power_on();

    box.takeFramesElapsed();                      // discard whatever boot produced
    box.runCycles(box.clockHz() / 10);            // ~6 frames in one slice
    const unsigned n = box.takeFramesElapsed();
    EXPECT_GE(n, 5u);
    EXPECT_LE(n, 7u);

    EXPECT_EQ(box.takeFramesElapsed(), 0u) << "reading it must clear it";
}

/* The host's repaint rule, which lives in the machine so it can be tested without
 * a window. Repaint when the program presents; at a boundary only if the frame
 * before it was not presented; and fall back to boundaries as soon as a frame goes
 * by without a present, so a program that stops presenting cannot freeze the
 * screen. */
TEST(Clock, RepaintFollowsPresentAndFallsBackToTheBoundary)
{
    Computer::Computer6502 box; box.power_on();
    Computer::VIC *vic = box.getVideoChip();
    const uint64_t frame = box.clockHz() / 60;

    box.runCycles(box.clockHz() / 4);   // boot; the kernel never presents
    box.takeRepaintDue();
    box.runCycles(frame + frame / 2);   // at least one boundary, no present
    EXPECT_TRUE(box.takeRepaintDue()) << "a program that never presents is shown at boundaries";

    vic->write(Computer::VIC::kRegFrame, 0);
    EXPECT_TRUE(box.takeRepaintDue()) << "a present is shown at once";
    EXPECT_FALSE(box.takeRepaintDue());

    // Present, then cross exactly the boundary that closes that frame: the frame
    // was presented, so the boundary must not repaint over the next one.
    vic->write(Computer::VIC::kRegFrame, 0);
    box.takeRepaintDue();
    box.runCycles(frame);
    EXPECT_FALSE(box.takeRepaintDue()) << "a presented frame's boundary must not repaint";

    // A whole frame with no present hands control back to the boundary.
    box.runCycles(frame);
    EXPECT_TRUE(box.takeRepaintDue()) << "stopping presenting must not freeze the screen";
}
}
