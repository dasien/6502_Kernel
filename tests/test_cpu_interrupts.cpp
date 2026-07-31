/**
 * @file test_cpu_interrupts.cpp
 * @brief Unit tests for the CPU6502 hardware interrupt path and the 65C02
 *        processor-control instructions (WAI/STP).
 *
 * The IRQ/NMI dispatch added in v2.2 drives the ~60Hz PIA interval timer
 * (BASIC's ON IRQ) and the NMI stop key (break to monitor), but was only ever
 * exercised incidentally through those two features -- nothing asserted the
 * mechanics directly. Interrupts fail quietly: a masked IRQ that fires anyway,
 * or an NMI that retriggers, corrupts state far from the cause. These cover:
 *
 *   - IRQ is taken only while the I flag is clear, and is level-sensitive
 *     (it re-enters for as long as the line stays asserted).
 *   - NMI is non-maskable and edge-triggered (one entry per request), and wins
 *     when both lines are pending.
 *   - The status byte pushed by a hardware interrupt has B clear, where BRK's
 *     has B set. This is the only way a handler can tell the two apart.
 *   - Entry sets I and clears D (65C02), so a handler never inherits BCD mode.
 *   - WAI halts until a line is signalled -- including a *masked* IRQ, which
 *     resumes execution without vectoring -- and STP halts until reset.
 */

#include <gtest/gtest.h>

#include "computer/CPU6502.h"
#include "computer/Memory.h"

using Computer::CPU6502;
using Computer::Memory;

namespace {

constexpr uint8_t kNop = 0xEA;
constexpr uint8_t kBrk = 0x00;
constexpr uint8_t kRti = 0x40;
constexpr uint8_t kCli = 0x58;
constexpr uint8_t kSei = 0x78;
constexpr uint8_t kWai = 0xCB;
constexpr uint8_t kStp = 0xDB;
constexpr uint8_t kLdaImm = 0xA9;

// Where test programs live: plain RAM, clear of screen memory and the I/O page.
constexpr uint16_t kProgAddr = 0x0200;
// Interrupt handlers.
constexpr uint16_t kIrqHandler = 0x0300;
constexpr uint16_t kNmiHandler = 0x0400;

class CpuInterruptTest : public ::testing::Test {
protected:
    Memory mem{nullptr, nullptr};
    CPU6502 cpu{mem};

    void SetUp() override {
        mem.writeWord(0xFFFE, kIrqHandler);   // IRQ/BRK vector
        mem.writeWord(0xFFFA, kNmiHandler);   // NMI vector
        mem.writeWord(0xFFFC, kProgAddr);     // RESET vector
        mem.write(kIrqHandler, kRti);
        mem.write(kNmiHandler, kRti);
        cpu.reg.PC = kProgAddr;
        cpu.reg.SP = 0xFF;
    }

    // Fill the program area with NOPs so execution never wanders into $00 bytes
    // (BRK), which would look like a spurious interrupt.
    void fillNops() {
        for (uint16_t a = kProgAddr; a < kProgAddr + 0x40; ++a) mem.write(a, kNop);
    }

    void step() { ASSERT_TRUE(cpu.executeSingleInstruction()); }

    // The status byte an interrupt pushed, read straight off the stack. Entry
    // pushes PC (2 bytes) then P, so P is the last byte written.
    uint8_t pushedStatus() const { return mem.read(0x0100 + cpu.reg.SP + 1); }

    // The return address an interrupt pushed (below the status byte).
    uint16_t pushedPc() const {
        return static_cast<uint16_t>(mem.read(0x0100 + cpu.reg.SP + 2)) |
               static_cast<uint16_t>(mem.read(0x0100 + cpu.reg.SP + 3) << 8);
    }

    bool i() const { return cpu.getFlag(CPU6502::kInterrupt); }
    bool d() const { return cpu.getFlag(CPU6502::kDecimal); }
};

// ---------------------------------------------------------------- IRQ masking

TEST_F(CpuInterruptTest, IrqIsTakenWhenInterruptFlagClear) {
    fillNops();
    cpu.setFlag(CPU6502::kInterrupt, false);
    cpu.setIrqLine(true);

    step();
    EXPECT_EQ(cpu.reg.PC, kIrqHandler) << "IRQ should vector through $FFFE";
}

TEST_F(CpuInterruptTest, IrqIsIgnoredWhileInterruptFlagSet) {
    fillNops();
    cpu.setFlag(CPU6502::kInterrupt, true);
    cpu.setIrqLine(true);

    step();
    EXPECT_EQ(cpu.reg.PC, kProgAddr + 1)
        << "a masked IRQ must not vector; the NOP should have run instead";
}

TEST_F(CpuInterruptTest, MaskedIrqIsTakenOnceCliRuns) {
    fillNops();
    mem.write(kProgAddr, kCli);
    cpu.setFlag(CPU6502::kInterrupt, true);
    cpu.setIrqLine(true);   // asserted the whole time, but masked

    step();                 // CLI
    EXPECT_EQ(cpu.reg.PC, kProgAddr + 1);
    EXPECT_FALSE(i());

    step();                 // now unmasked, the pending line is serviced
    EXPECT_EQ(cpu.reg.PC, kIrqHandler);
}

// ------------------------------------------------------- IRQ level-sensitivity

TEST_F(CpuInterruptTest, IrqReentersWhileTheLineStaysAsserted) {
    fillNops();
    cpu.setFlag(CPU6502::kInterrupt, false);
    cpu.setIrqLine(true);

    step();                                   // enter the handler
    ASSERT_EQ(cpu.reg.PC, kIrqHandler);
    step();                                   // RTI back to the program
    ASSERT_EQ(cpu.reg.PC, kProgAddr);

    // The source was never acked, so the line is still low: the IRQ must fire
    // again rather than being consumed by the first entry.
    step();
    EXPECT_EQ(cpu.reg.PC, kIrqHandler)
        << "IRQ is level-sensitive, not a one-shot latch";
}

TEST_F(CpuInterruptTest, IrqStopsOnceTheLineIsDeasserted) {
    fillNops();
    cpu.setFlag(CPU6502::kInterrupt, false);
    cpu.setIrqLine(true);

    step();                                   // enter
    ASSERT_EQ(cpu.reg.PC, kIrqHandler);
    cpu.setIrqLine(false);                    // handler acks the source
    step();                                   // RTI
    ASSERT_EQ(cpu.reg.PC, kProgAddr);

    step();
    EXPECT_EQ(cpu.reg.PC, kProgAddr + 1) << "acked IRQ must not fire again";
}

// ------------------------------------------------------------------------ NMI

TEST_F(CpuInterruptTest, NmiIsTakenEvenWhenInterruptsAreMasked) {
    fillNops();
    cpu.setFlag(CPU6502::kInterrupt, true);   // masks IRQ, must not mask NMI
    cpu.requestNmi();

    step();
    EXPECT_EQ(cpu.reg.PC, kNmiHandler) << "NMI should vector through $FFFA";
}

TEST_F(CpuInterruptTest, NmiIsEdgeTriggeredAndFiresOncePerRequest) {
    fillNops();
    cpu.requestNmi();

    step();                                   // enter
    ASSERT_EQ(cpu.reg.PC, kNmiHandler);
    step();                                   // RTI
    ASSERT_EQ(cpu.reg.PC, kProgAddr);

    step();
    EXPECT_EQ(cpu.reg.PC, kProgAddr + 1)
        << "one requestNmi() must produce exactly one entry";
}

TEST_F(CpuInterruptTest, NmiTakesPriorityOverAPendingIrq) {
    fillNops();
    cpu.setFlag(CPU6502::kInterrupt, false);
    cpu.setIrqLine(true);
    cpu.requestNmi();

    step();
    EXPECT_EQ(cpu.reg.PC, kNmiHandler) << "NMI outranks IRQ when both are pending";

    step();                                   // RTI out of the NMI handler
    ASSERT_EQ(cpu.reg.PC, kProgAddr);
    step();
    EXPECT_EQ(cpu.reg.PC, kIrqHandler) << "the IRQ is still pending afterwards";
}

// -------------------------------------------------- pushed state on entry

// A handler distinguishes a hardware interrupt from BRK by bit 4 of the status
// byte on the stack: hardware pushes it clear, BRK pushes it set. Getting this
// backwards makes a BRK-based debugger fire on timer ticks.
TEST_F(CpuInterruptTest, HardwareInterruptPushesStatusWithBreakClear) {
    fillNops();
    cpu.setFlag(CPU6502::kInterrupt, false);
    cpu.setIrqLine(true);

    step();
    EXPECT_EQ(pushedStatus() & CPU6502::kBreak, 0)
        << "hardware IRQ must push B clear";
    EXPECT_NE(pushedStatus() & CPU6502::kUnused, 0)
        << "bit 5 is always set in a pushed status byte";
}

TEST_F(CpuInterruptTest, BrkPushesStatusWithBreakSet) {
    fillNops();
    mem.write(kProgAddr, kBrk);

    step();
    ASSERT_EQ(cpu.reg.PC, kIrqHandler) << "BRK shares the IRQ vector";
    EXPECT_NE(pushedStatus() & CPU6502::kBreak, 0) << "BRK must push B set";
}

TEST_F(CpuInterruptTest, InterruptPushesTheAddressOfTheInterruptedInstruction) {
    fillNops();
    cpu.reg.PC = kProgAddr + 4;
    cpu.setFlag(CPU6502::kInterrupt, false);
    cpu.setIrqLine(true);

    step();
    EXPECT_EQ(pushedPc(), kProgAddr + 4)
        << "the interrupted instruction must be re-run on RTI, not skipped";
}

TEST_F(CpuInterruptTest, EntrySetsInterruptDisableAndClearsDecimal) {
    fillNops();
    cpu.setFlag(CPU6502::kInterrupt, false);
    cpu.setFlag(CPU6502::kDecimal, true);
    cpu.setIrqLine(true);

    step();
    EXPECT_TRUE(i()) << "entry must mask further IRQs";
    EXPECT_FALSE(d()) << "the 65C02 clears D on interrupt entry";
    EXPECT_NE(pushedStatus() & CPU6502::kDecimal, 0)
        << "the pushed status must still record D so RTI restores it";
}

TEST_F(CpuInterruptTest, RtiRestoresStatusAndResumesTheInterruptedInstruction) {
    fillNops();
    mem.write(kProgAddr + 4, kLdaImm);
    mem.write(kProgAddr + 5, 0x7F);
    cpu.reg.PC = kProgAddr + 4;
    cpu.setFlag(CPU6502::kInterrupt, false);
    cpu.setFlag(CPU6502::kDecimal, true);
    cpu.setIrqLine(true);

    step();                                   // enter
    ASSERT_EQ(cpu.reg.PC, kIrqHandler);
    cpu.setIrqLine(false);
    step();                                   // RTI

    EXPECT_EQ(cpu.reg.PC, kProgAddr + 4) << "resumes at the interrupted LDA";
    EXPECT_TRUE(d()) << "RTI restores D";
    EXPECT_FALSE(i()) << "RTI restores the pre-entry I flag";

    step();                                   // the LDA finally runs
    EXPECT_EQ(cpu.reg.A, 0x7F);
}

// -------------------------------------------------------------- WAI and STP

TEST_F(CpuInterruptTest, WaiHaltsUntilAnIrqArrives) {
    fillNops();
    mem.write(kProgAddr, kWai);
    cpu.setFlag(CPU6502::kInterrupt, false);

    step();                                   // WAI
    EXPECT_TRUE(cpu.isWaiting());

    const uint16_t parked = cpu.reg.PC;
    for (int n = 0; n < 20; ++n) step();       // no interrupt: nothing advances
    EXPECT_EQ(cpu.reg.PC, parked) << "WAI must not fall through on its own";

    cpu.setIrqLine(true);
    step();
    EXPECT_FALSE(cpu.isWaiting());
    EXPECT_EQ(cpu.reg.PC, kIrqHandler) << "an enabled IRQ wakes WAI and vectors";
}

// The subtle half of WAI: the line waking the processor need not be enabled.
// A masked IRQ resumes execution *without* vectoring, which is how WAI is used
// to sync to an event with no dispatch overhead.
TEST_F(CpuInterruptTest, MaskedIrqWakesWaiWithoutVectoring) {
    fillNops();
    mem.write(kProgAddr, kWai);
    cpu.setFlag(CPU6502::kInterrupt, true);   // masked

    step();                                   // WAI
    ASSERT_TRUE(cpu.isWaiting());
    const uint16_t resume = cpu.reg.PC;

    cpu.setIrqLine(true);
    step();
    EXPECT_FALSE(cpu.isWaiting());
    EXPECT_EQ(cpu.reg.PC, resume + 1)
        << "a masked IRQ resumes at the instruction after WAI, it does not vector";
}

TEST_F(CpuInterruptTest, WaiWakesOnNmi) {
    fillNops();
    mem.write(kProgAddr, kWai);
    cpu.setFlag(CPU6502::kInterrupt, true);   // NMI ignores this

    step();                                   // WAI
    ASSERT_TRUE(cpu.isWaiting());

    cpu.requestNmi();
    step();
    EXPECT_FALSE(cpu.isWaiting());
    EXPECT_EQ(cpu.reg.PC, kNmiHandler);
}

TEST_F(CpuInterruptTest, StpHaltsUntilResetAndIgnoresInterrupts) {
    fillNops();
    mem.write(kProgAddr, kStp);
    cpu.setFlag(CPU6502::kInterrupt, false);

    step();                                   // STP
    EXPECT_TRUE(cpu.isStopped());
    const uint16_t parked = cpu.reg.PC;

    // Neither line revives a stopped processor: the clock itself is halted.
    cpu.setIrqLine(true);
    cpu.requestNmi();
    for (int n = 0; n < 20; ++n) step();
    EXPECT_TRUE(cpu.isStopped());
    EXPECT_EQ(cpu.reg.PC, parked) << "only reset restarts a stopped CPU";

    cpu.reset();
    EXPECT_FALSE(cpu.isStopped());
    EXPECT_EQ(cpu.reg.PC, kProgAddr) << "reset vectors through $FFFC";

    mem.write(kProgAddr, kNop);
    step();
    EXPECT_EQ(cpu.reg.PC, kProgAddr + 1) << "the CPU runs again after reset";
}

TEST_F(CpuInterruptTest, HaltedCpuStillReportsSuccessSoRunLoopsTerminate) {
    fillNops();
    mem.write(kProgAddr, kStp);
    step();
    ASSERT_TRUE(cpu.isStopped());

    // Computer6502::run() breaks its loop on a false return (unknown opcode).
    // A halted CPU is idle, not faulted, so it must keep returning true.
    EXPECT_TRUE(cpu.executeSingleInstruction());
}

} // namespace
