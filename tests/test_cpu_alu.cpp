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

    // Stage and execute a three-byte instruction (opcode + two operand bytes).
    void execThreeByte(const uint8_t opcode, const uint8_t b1, const uint8_t b2) {
        mem.write(kProgAddr, opcode);
        mem.write(kProgAddr + 1, b1);
        mem.write(kProgAddr + 2, b2);
        cpu.reg.PC = kProgAddr;
        ASSERT_TRUE(cpu.executeSingleInstruction());
    }

    bool d() const { return cpu.getFlag(CPU6502::kDecimal); }
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

// ---------------------------------------------------------------------------
// 65C02: zero-page indirect (zp) addressing  — opcodes $12/$32/$52/$72/$92/
// $B2/$D2/$F2. A 1-byte zero-page operand names a pointer at zp/zp+1.
// (These were previously mis-decoded as 2-byte absolute-indirect.)
// ---------------------------------------------------------------------------

constexpr uint8_t kLdaZpInd = 0xB2;
constexpr uint8_t kStaZpInd = 0x92;
constexpr uint8_t kAdcZpInd = 0x72;

TEST_F(CpuAluTest, LdaZeroPageIndirect) {
    // Pointer at zp $40 -> $0900 (plain RAM); target holds $7C.
    mem.write(0x40, 0x00);
    mem.write(0x41, 0x09);
    mem.write(0x0900, 0x7C);
    execImm(kLdaZpInd, 0x40);          // LDA ($40)
    EXPECT_EQ(cpu.reg.A, 0x7C);
    EXPECT_FALSE(z());
    EXPECT_FALSE(n());
    EXPECT_EQ(cpu.reg.PC, static_cast<uint16_t>(kProgAddr + 2));  // 2-byte instruction
}

TEST_F(CpuAluTest, StaZeroPageIndirect) {
    mem.write(0x40, 0x00);
    mem.write(0x41, 0x09);
    cpu.reg.A = 0xAB;
    execImm(kStaZpInd, 0x40);          // STA ($40)
    EXPECT_EQ(mem.read(0x0900), 0xAB);
}

TEST_F(CpuAluTest, AdcZeroPageIndirect) {
    mem.write(0x40, 0x00);
    mem.write(0x41, 0x09);
    mem.write(0x0900, 0x22);
    cpu.reg.A = 0x10;
    setMode(/*decimal=*/false, /*carry=*/false);
    execImm(kAdcZpInd, 0x40);          // ADC ($40)
    EXPECT_EQ(cpu.reg.A, 0x32);
}

// The pointer's high byte must wrap within zero page: ($FF) reads from $FF/$00.
TEST_F(CpuAluTest, ZeroPageIndirectPointerWraps) {
    mem.write(0xFF, 0x34);             // low byte of pointer
    mem.write(0x00, 0x09);             // high byte wraps to $00 -> $0934
    mem.write(0x0934, 0x5E);
    execImm(kLdaZpInd, 0xFF);          // LDA ($FF)
    EXPECT_EQ(cpu.reg.A, 0x5E);
}

// ---------------------------------------------------------------------------
// 65C02 (Rockwell/WDC) single-bit memory ops: RMB/SMB and BBR/BBS
// ---------------------------------------------------------------------------

TEST_F(CpuAluTest, RmbClearsBit) {
    mem.write(0x40, 0xFF);
    execImm(0x37, 0x40);               // RMB3 $40
    EXPECT_EQ(mem.read(0x40), 0xF7);   // bit 3 cleared
}

TEST_F(CpuAluTest, SmbSetsBit) {
    mem.write(0x40, 0x00);
    execImm(0xD7, 0x40);               // SMB5 $40  ($87 + 5*$10)
    EXPECT_EQ(mem.read(0x40), 0x20);   // bit 5 set
}

TEST_F(CpuAluTest, BbrBranchesWhenBitReset) {
    mem.write(0x40, 0x00);             // bit 0 is reset
    execThreeByte(0x0F, 0x40, 0x10);   // BBR0 $40,+$10
    EXPECT_EQ(cpu.reg.PC, static_cast<uint16_t>(kProgAddr + 3 + 0x10));
}

TEST_F(CpuAluTest, BbrNotTakenWhenBitSet) {
    mem.write(0x40, 0x01);             // bit 0 is set
    execThreeByte(0x0F, 0x40, 0x10);   // BBR0 $40,+$10 (not taken)
    EXPECT_EQ(cpu.reg.PC, static_cast<uint16_t>(kProgAddr + 3));
}

TEST_F(CpuAluTest, BbsBranchesWhenBitSet) {
    mem.write(0x40, 0x80);             // bit 7 is set
    execThreeByte(0xFF, 0x40, 0x20);   // BBS7 $40,+$20  ($8F + 7*$10)
    EXPECT_EQ(cpu.reg.PC, static_cast<uint16_t>(kProgAddr + 3 + 0x20));
}

// ---------------------------------------------------------------------------
// 65C02 undefined opcodes act as deterministic multi-byte NOPs
// ---------------------------------------------------------------------------

TEST_F(CpuAluTest, NopOneByte) {
    mem.write(kProgAddr, 0x03);        // $03: 1-byte NOP
    cpu.reg.PC = kProgAddr;
    ASSERT_TRUE(cpu.executeSingleInstruction());
    EXPECT_EQ(cpu.reg.PC, static_cast<uint16_t>(kProgAddr + 1));
}

TEST_F(CpuAluTest, NopTwoByte) {
    execImm(0x02, 0xEE);               // $02: 2-byte NOP
    EXPECT_EQ(cpu.reg.PC, static_cast<uint16_t>(kProgAddr + 2));
}

TEST_F(CpuAluTest, NopThreeByte) {
    execThreeByte(0x5C, 0xEE, 0xEE);   // $5C: 3-byte NOP
    EXPECT_EQ(cpu.reg.PC, static_cast<uint16_t>(kProgAddr + 3));
}

// ---------------------------------------------------------------------------
// 65C02 BRK clears the decimal flag (the NMOS 6502 does not)
// ---------------------------------------------------------------------------

TEST_F(CpuAluTest, BrkClearsDecimalFlag) {
    mem.writeWord(0xFFFE, 0x0300);
    mem.write(0x0300, 0x40);           // RTI (irrelevant; we check D after BRK)
    cpu.reg.SP = 0xFF;
    cpu.setFlag(CPU6502::kDecimal, true);
    cpu.reg.PC = kProgAddr;
    mem.write(kProgAddr, 0x00);        // BRK
    ASSERT_TRUE(cpu.executeSingleInstruction());
    EXPECT_FALSE(d());                 // D cleared on entry to the handler
}

// ---------------------------------------------------------------------------
// 65C02 decimal mode sets N/V/Z validly, and matches the hardware even for
// invalid BCD inputs (validated against the Klaus2m5/amb5l decimal test).
// ---------------------------------------------------------------------------

// Valid BCD: 50 + 50 = 100 -> $00 with carry; Z set, V set, N clear on 65C02.
TEST_F(CpuAluTest, AdcDecimalFlagsValid) {
    cpu.reg.A = 0x50;
    setMode(/*decimal=*/true, /*carry=*/false);
    execImm(kAdcImm, 0x50);
    EXPECT_EQ(cpu.reg.A, 0x00);
    EXPECT_TRUE(c());
    EXPECT_TRUE(z());
    EXPECT_FALSE(n());
}

// Invalid BCD: $00 - $0B with carry set. A real W65C02S yields $8F here (the
// 6502.org per-nibble formula would give $9F). C comes from the binary borrow.
TEST_F(CpuAluTest, SbcDecimalInvalidBcdMatchesHardware) {
    cpu.reg.A = 0x00;
    setMode(/*decimal=*/true, /*carry=*/true);
    execImm(kSbcImm, 0x0B);
    EXPECT_EQ(cpu.reg.A, 0x8F);
    EXPECT_FALSE(c());                 // binary borrow
}

}  // namespace
