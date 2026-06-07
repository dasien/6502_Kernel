/**
 * @file test_cpu_alu.cpp
 * @brief Unit tests for the CPU6502 ALU: ADC/SBC (binary and BCD) and the
 *        compare instructions (CMP/CPX/CPY), exercised through real opcode
 *        execution.
 *
 * These cover the flag/arithmetic fixes:
 *   - CMP/CPX/CPY set N from bit 7 of (reg - operand), not from reg.
 *   - ADC/SBC overflow (V) flag uses the correct signed-overflow rule.
 *   - ADC/SBC honor decimal mode (BCD) with correct per-nibble adjust.
 */

#include <gtest/gtest.h>

#include "computer/CPU6502.h"
#include "computer/Memory.h"

using Computer::CPU6502;
using Computer::Memory;

namespace {

// Immediate-mode opcodes used by the tests.
constexpr uint8_t kAdcImm = 0x69;
constexpr uint8_t kSbcImm = 0xE9;
constexpr uint8_t kCmpImm = 0xC9;
constexpr uint8_t kCpxImm = 0xE0;
constexpr uint8_t kCpyImm = 0xC0;

// Implied-mode opcodes used by the BRK/RTI test.
constexpr uint8_t kBrk = 0x00;
constexpr uint8_t kRti = 0x40;

// Address used to stage the two-byte instruction. Well clear of screen RAM
// ($0400-$07E7) and the PIA ($DC00+), so Memory routes it to plain RAM.
constexpr uint16_t kProgAddr = 0x0200;

class CpuAluTest : public ::testing::Test {
protected:
    // No VIC/PIA needed: test programs live in plain RAM.
    Memory mem{nullptr, nullptr};
    CPU6502 cpu{mem};

    // Stage and execute one immediate-mode instruction (opcode + operand).
    void execImm(const uint8_t opcode, const uint8_t operand) {
        mem.write(kProgAddr, opcode);
        mem.write(kProgAddr + 1, operand);
        cpu.reg.PC = kProgAddr;
        ASSERT_TRUE(cpu.executeSingleInstruction());
    }

    bool c() const { return cpu.getFlag(CPU6502::kCarry); }
    bool z() const { return cpu.getFlag(CPU6502::kZero); }
    bool v() const { return cpu.getFlag(CPU6502::kOverflow); }
    bool n() const { return cpu.getFlag(CPU6502::kNegative); }

    void setMode(const bool decimal, const bool carry) {
        cpu.setFlag(CPU6502::kDecimal, decimal);
        cpu.setFlag(CPU6502::kCarry, carry);
    }

    // Stage and execute one implied (single-byte) instruction.
    void execImplied(const uint8_t opcode) {
        mem.write(cpu.reg.PC, opcode);
        ASSERT_TRUE(cpu.executeSingleInstruction());
    }
};

// ---------------------------------------------------------------------------
// ADC, binary mode
// ---------------------------------------------------------------------------

TEST_F(CpuAluTest, AdcBinarySimple) {
    cpu.reg.A = 0x10;
    setMode(/*decimal=*/false, /*carry=*/false);
    execImm(kAdcImm, 0x20);
    EXPECT_EQ(cpu.reg.A, 0x30);
    EXPECT_FALSE(c());
    EXPECT_FALSE(v());
    EXPECT_FALSE(n());
    EXPECT_FALSE(z());
}

TEST_F(CpuAluTest, AdcBinaryCarryIn) {
    cpu.reg.A = 0x10;
    setMode(false, /*carry=*/true);
    execImm(kAdcImm, 0x20);
    EXPECT_EQ(cpu.reg.A, 0x31);
    EXPECT_FALSE(c());
}

TEST_F(CpuAluTest, AdcBinaryCarryOut) {
    cpu.reg.A = 0xF0;
    setMode(false, false);
    execImm(kAdcImm, 0x20);  // 0x110
    EXPECT_EQ(cpu.reg.A, 0x10);
    EXPECT_TRUE(c());
    EXPECT_FALSE(z());
}

TEST_F(CpuAluTest, AdcBinaryZero) {
    cpu.reg.A = 0x00;
    setMode(false, false);
    execImm(kAdcImm, 0x00);
    EXPECT_EQ(cpu.reg.A, 0x00);
    EXPECT_TRUE(z());
    EXPECT_FALSE(c());
}

TEST_F(CpuAluTest, AdcOverflowPosPlusPosGivesNegative) {
    cpu.reg.A = 0x50;  // +80
    setMode(false, false);
    execImm(kAdcImm, 0x50);  // +80 -> 0xA0 (-96)
    EXPECT_EQ(cpu.reg.A, 0xA0);
    EXPECT_TRUE(v());
    EXPECT_TRUE(n());
    EXPECT_FALSE(c());
}

// This is the case the old incomplete overflow formula missed.
TEST_F(CpuAluTest, AdcOverflowNegPlusNegGivesPositive) {
    cpu.reg.A = 0x90;  // -112
    setMode(false, false);
    execImm(kAdcImm, 0x90);  // -112 + -112 = -224 -> 0x20 with carry
    EXPECT_EQ(cpu.reg.A, 0x20);
    EXPECT_TRUE(v());
    EXPECT_TRUE(c());
    EXPECT_FALSE(n());
}

TEST_F(CpuAluTest, AdcNoOverflowPosPlusNeg) {
    cpu.reg.A = 0x50;
    setMode(false, false);
    execImm(kAdcImm, 0xF0);  // +80 + -16 = +64
    EXPECT_EQ(cpu.reg.A, 0x40);
    EXPECT_FALSE(v());
    EXPECT_TRUE(c());
}

// ---------------------------------------------------------------------------
// SBC, binary mode
// ---------------------------------------------------------------------------

TEST_F(CpuAluTest, SbcBinarySimple) {
    cpu.reg.A = 0x50;
    setMode(false, /*carry=*/true);  // carry set => no borrow in
    execImm(kSbcImm, 0x20);
    EXPECT_EQ(cpu.reg.A, 0x30);
    EXPECT_TRUE(c());  // no borrow out
    EXPECT_FALSE(v());
    EXPECT_FALSE(n());
}

TEST_F(CpuAluTest, SbcBinaryBorrowOut) {
    cpu.reg.A = 0x20;
    setMode(false, true);
    execImm(kSbcImm, 0x50);  // 0x20 - 0x50 = -0x30 -> 0xD0
    EXPECT_EQ(cpu.reg.A, 0xD0);
    EXPECT_FALSE(c());  // borrow
    EXPECT_TRUE(n());
}

TEST_F(CpuAluTest, SbcBinaryBorrowIn) {
    cpu.reg.A = 0x50;
    setMode(false, /*carry=*/false);  // carry clear => borrow in
    execImm(kSbcImm, 0x20);  // 0x50 - 0x20 - 1 = 0x2F
    EXPECT_EQ(cpu.reg.A, 0x2F);
    EXPECT_TRUE(c());
}

TEST_F(CpuAluTest, SbcOverflow) {
    // +80 - (-80) = +160 -> overflow, result 0xA0 (looks negative)
    cpu.reg.A = 0x50;
    setMode(false, true);
    execImm(kSbcImm, 0xB0);
    EXPECT_EQ(cpu.reg.A, 0xA0);
    EXPECT_TRUE(v());
    EXPECT_FALSE(c());  // borrow
}

// ---------------------------------------------------------------------------
// CMP / CPX / CPY  (N must come from the difference, not the register)
// ---------------------------------------------------------------------------

TEST_F(CpuAluTest, CmpEqual) {
    cpu.reg.A = 0x42;
    execImm(kCmpImm, 0x42);
    EXPECT_TRUE(c());
    EXPECT_TRUE(z());
    EXPECT_FALSE(n());
}

TEST_F(CpuAluTest, CmpGreater) {
    cpu.reg.A = 0x84;
    execImm(kCmpImm, 0x83);  // 0x84 - 0x83 = 0x01
    EXPECT_TRUE(c());
    EXPECT_FALSE(z());
    EXPECT_FALSE(n());
}

TEST_F(CpuAluTest, CmpLessSetsNegativeFromDifference) {
    // 0x83 - 0x84 = 0xFF -> N set. The old bug took N from A (0x83) -> N clear.
    cpu.reg.A = 0x83;
    execImm(kCmpImm, 0x84);
    EXPECT_FALSE(c());  // borrow
    EXPECT_FALSE(z());
    EXPECT_TRUE(n());   // bit 7 of 0xFF
}

// Regression test for the FOR/NEXT bug: EhBASIC's FP add does `CMP #$F9 / BMI`
// to choose a mantissa-shift path. With A=$FE the difference is $05 (N clear),
// so BMI must NOT be taken. The old bug set N from bit 7 of A ($FE) and broke
// "FOR I = 1 TO 10" (it ran only 4 times).
TEST_F(CpuAluTest, CmpForLoopShiftDecisionRegression) {
    cpu.reg.A = 0xFE;
    execImm(kCmpImm, 0xF9);
    EXPECT_TRUE(c());   // 0xFE >= 0xF9
    EXPECT_FALSE(n());  // bit 7 of (0xFE - 0xF9 = 0x05)
}

TEST_F(CpuAluTest, CpxLessSetsNegative) {
    cpu.reg.X = 0x10;
    execImm(kCpxImm, 0x20);  // 0x10 - 0x20 = 0xF0
    EXPECT_FALSE(c());
    EXPECT_TRUE(n());
}

TEST_F(CpuAluTest, CpyGreaterClearsNegative) {
    cpu.reg.Y = 0x90;
    execImm(kCpyImm, 0x10);  // 0x90 - 0x10 = 0x80 -> N set
    EXPECT_TRUE(c());
    EXPECT_TRUE(n());
}

// ---------------------------------------------------------------------------
// ADC, decimal (BCD) mode
// ---------------------------------------------------------------------------

TEST_F(CpuAluTest, AdcDecimalSimple) {
    cpu.reg.A = 0x25;
    setMode(/*decimal=*/true, /*carry=*/false);
    execImm(kAdcImm, 0x48);
    EXPECT_EQ(cpu.reg.A, 0x73);
    EXPECT_FALSE(c());
}

TEST_F(CpuAluTest, AdcDecimalCarryIn) {
    cpu.reg.A = 0x12;
    setMode(true, /*carry=*/true);
    execImm(kAdcImm, 0x34);  // 0x12 + 0x34 + 1 = 0x47
    EXPECT_EQ(cpu.reg.A, 0x47);
    EXPECT_FALSE(c());
}

TEST_F(CpuAluTest, AdcDecimalCarryOut) {
    cpu.reg.A = 0x99;
    setMode(true, false);
    execImm(kAdcImm, 0x01);  // 99 + 1 = 100 -> 00 carry
    EXPECT_EQ(cpu.reg.A, 0x00);
    EXPECT_TRUE(c());
    EXPECT_TRUE(z());
}

TEST_F(CpuAluTest, AdcDecimalLowNibbleAdjust) {
    cpu.reg.A = 0x08;
    setMode(true, false);
    execImm(kAdcImm, 0x08);  // 8 + 8 = 16 -> 0x16
    EXPECT_EQ(cpu.reg.A, 0x16);
    EXPECT_FALSE(c());
}

// ---------------------------------------------------------------------------
// SBC, decimal (BCD) mode
// ---------------------------------------------------------------------------

TEST_F(CpuAluTest, SbcDecimalSimple) {
    cpu.reg.A = 0x50;
    setMode(/*decimal=*/true, /*carry=*/true);
    execImm(kSbcImm, 0x25);
    EXPECT_EQ(cpu.reg.A, 0x25);
    EXPECT_TRUE(c());  // no borrow
}

TEST_F(CpuAluTest, SbcDecimalBorrowOut) {
    cpu.reg.A = 0x00;
    setMode(true, /*carry=*/true);
    execImm(kSbcImm, 0x01);  // 0 - 1 = 99 with borrow
    EXPECT_EQ(cpu.reg.A, 0x99);
    EXPECT_FALSE(c());
}

TEST_F(CpuAluTest, SbcDecimalBorrowIn) {
    cpu.reg.A = 0x50;
    setMode(true, /*carry=*/false);  // borrow in
    execImm(kSbcImm, 0x25);  // 0x50 - 0x25 - 1 = 0x24
    EXPECT_EQ(cpu.reg.A, 0x24);
    EXPECT_TRUE(c());
}

// ---------------------------------------------------------------------------
// BRK / RTI return address
// ---------------------------------------------------------------------------

// Regression test for the BRK off-by-one. BRK pushes the address of the BRK
// opcode + 2 (it skips a one-byte signature/pad byte that follows the opcode),
// so a matching RTI resumes at BRK+2. The old handler pushed BRK+3 because it
// added 2 to a PC that the opcode fetch had already advanced past the opcode.
// Klaus2m5's functional test trapped on this in its BRK return-address check.
TEST_F(CpuAluTest, BrkPushesReturnAddressBrkPlusTwo) {
    // Point the IRQ/BRK vector ($FFFE/$FFFF) at an RTI handler.
    constexpr uint16_t kHandler = 0x0300;
    mem.writeWord(0xFFFE, kHandler);
    mem.write(kHandler, kRti);

    cpu.reg.SP = 0xFF;
    cpu.reg.PC = kProgAddr;          // BRK lives at kProgAddr
    mem.write(kProgAddr, kBrk);
    ASSERT_TRUE(cpu.executeSingleInstruction());

    // BRK should have vectored through $FFFE.
    EXPECT_EQ(cpu.reg.PC, kHandler);

    // Stack now holds (top-down): PCH @ $01FF, PCL @ $01FE, P @ $01FD.
    const uint16_t pushed =
        (static_cast<uint16_t>(mem.read(0x01FF)) << 8) | mem.read(0x01FE);
    EXPECT_EQ(pushed, static_cast<uint16_t>(kProgAddr + 2));

    // The pushed status must have the Break flag set.
    EXPECT_NE(mem.read(0x01FD) & CPU6502::kBreak, 0);

    // RTI must resume exactly at BRK+2.
    ASSERT_TRUE(cpu.executeSingleInstruction());
    EXPECT_EQ(cpu.reg.PC, static_cast<uint16_t>(kProgAddr + 2));
}

}  // namespace
