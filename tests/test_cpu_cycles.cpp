/**
 * @file test_cpu_cycles.cpp
 * @brief Cycle-count accuracy for every defined 65C02 opcode.
 *
 * The bus helpers (readByte/readWord/pushByte/pullByte) used to charge cycles of
 * their own while each handler ALSO added the instruction's full published count,
 * so everything ran over: the opcode fetch alone put every instruction one cycle
 * past its datasheet value, and JSR -- billed for a fetch, an operand word and two
 * stack pushes on top of its own 6 -- cost 11 cycles instead of 6. Nothing read
 * the counter at the time, so the drift was invisible; this locks it down before
 * anything starts depending on it.
 *
 * kExpected below is reference data taken from the W65C02S datasheet, NOT
 * generated from the emulator -- deriving it from CPU6502 would just assert that
 * the code agrees with itself. Base counts only; the conditional penalties
 * (page crossing, taken branches) are covered separately at the end.
 */

#include <gtest/gtest.h>

#include "computer/CPU6502.h"
#include "computer/Memory.h"

using Computer::CPU6502;
using Computer::Memory;

namespace {

struct OpcodeCycles {
    uint8_t opcode;
    const char *mnemonic;
    const char *mode;
    int cycles;      ///< base cost, no page-crossing or branch-taken penalty
};

// Every opcode the emulator defines, with its datasheet cycle count. The four
// branches that are TAKEN under the test's flag setup (all flags clear) carry the
// +1 taken penalty here; the other four fall through at their base cost.
constexpr OpcodeCycles kExpected[] = {
    {0x00, "BRK", "IMP", 7},
    {0x01, "ORA", "IZX", 6},
    {0x04, "TSB", "ZP", 5},
    {0x05, "ORA", "ZP", 3},
    {0x06, "ASL", "ZP", 5},
    {0x07, "RMB0", "ZP", 5},
    {0x08, "PHP", "IMP", 3},
    {0x09, "ORA", "IMM", 2},
    {0x0A, "ASL", "ACC", 2},
    {0x0C, "TSB", "ABS", 6},
    {0x0D, "ORA", "ABS", 4},
    {0x0E, "ASL", "ABS", 6},
    {0x0F, "BBR0", "ZPR", 5},
    {0x10, "BPL", "REL", 3},  // taken
    {0x11, "ORA", "IZY", 5},
    {0x12, "ORA", "ZPI", 5},
    {0x14, "TRB", "ZP", 5},
    {0x15, "ORA", "ZPX", 4},
    {0x16, "ASL", "ZPX", 6},
    {0x17, "RMB1", "ZP", 5},
    {0x18, "CLC", "IMP", 2},
    {0x19, "ORA", "ABY", 4},
    {0x1A, "INC", "ACC", 2},
    {0x1C, "TRB", "ABS", 6},
    {0x1D, "ORA", "ABX", 4},
    {0x1E, "ASL", "ABX", 7},
    {0x1F, "BBR1", "ZPR", 5},
    {0x20, "JSR", "ABS", 6},
    {0x21, "AND", "IZX", 6},
    {0x24, "BIT", "ZP", 3},
    {0x25, "AND", "ZP", 3},
    {0x26, "ROL", "ZP", 5},
    {0x27, "RMB2", "ZP", 5},
    {0x28, "PLP", "IMP", 4},
    {0x29, "AND", "IMM", 2},
    {0x2A, "ROL", "ACC", 2},
    {0x2C, "BIT", "ABS", 4},
    {0x2D, "AND", "ABS", 4},
    {0x2E, "ROL", "ABS", 6},
    {0x2F, "BBR2", "ZPR", 5},
    {0x30, "BMI", "REL", 2},
    {0x31, "AND", "IZY", 5},
    {0x32, "AND", "ZPI", 5},
    {0x34, "BIT", "ZPX", 4},
    {0x35, "AND", "ZPX", 4},
    {0x36, "ROL", "ZPX", 6},
    {0x37, "RMB3", "ZP", 5},
    {0x38, "SEC", "IMP", 2},
    {0x39, "AND", "ABY", 4},
    {0x3A, "DEC", "ACC", 2},
    {0x3C, "BIT", "ABX", 4},
    {0x3D, "AND", "ABX", 4},
    {0x3E, "ROL", "ABX", 7},
    {0x3F, "BBR3", "ZPR", 5},
    {0x40, "RTI", "IMP", 6},
    {0x41, "EOR", "IZX", 6},
    {0x45, "EOR", "ZP", 3},
    {0x46, "LSR", "ZP", 5},
    {0x47, "RMB4", "ZP", 5},
    {0x48, "PHA", "IMP", 3},
    {0x49, "EOR", "IMM", 2},
    {0x4A, "LSR", "ACC", 2},
    {0x4C, "JMP", "ABS", 3},
    {0x4D, "EOR", "ABS", 4},
    {0x4E, "LSR", "ABS", 6},
    {0x4F, "BBR4", "ZPR", 5},
    {0x50, "BVC", "REL", 3},  // taken
    {0x51, "EOR", "IZY", 5},
    {0x52, "EOR", "ZPI", 5},
    {0x55, "EOR", "ZPX", 4},
    {0x56, "LSR", "ZPX", 6},
    {0x57, "RMB5", "ZP", 5},
    {0x58, "CLI", "IMP", 2},
    {0x59, "EOR", "ABY", 4},
    {0x5A, "PHY", "IMP", 3},
    {0x5D, "EOR", "ABX", 4},
    {0x5E, "LSR", "ABX", 7},
    {0x5F, "BBR5", "ZPR", 5},
    {0x60, "RTS", "IMP", 6},
    {0x61, "ADC", "IZX", 6},
    {0x64, "STZ", "ZP", 3},
    {0x65, "ADC", "ZP", 3},
    {0x66, "ROR", "ZP", 5},
    {0x67, "RMB6", "ZP", 5},
    {0x68, "PLA", "IMP", 4},
    {0x69, "ADC", "IMM", 2},
    {0x6A, "ROR", "ACC", 2},
    {0x6C, "JMP", "IND", 6},
    {0x6D, "ADC", "ABS", 4},
    {0x6E, "ROR", "ABS", 6},
    {0x6F, "BBR6", "ZPR", 5},
    {0x70, "BVS", "REL", 2},
    {0x71, "ADC", "IZY", 5},
    {0x72, "ADC", "ZPI", 5},
    {0x74, "STZ", "ZPX", 4},
    {0x75, "ADC", "ZPX", 4},
    {0x76, "ROR", "ZPX", 6},
    {0x77, "RMB7", "ZP", 5},
    {0x78, "SEI", "IMP", 2},
    {0x79, "ADC", "ABY", 4},
    {0x7A, "PLY", "IMP", 4},
    {0x7D, "ADC", "ABX", 4},
    {0x7E, "ROR", "ABX", 7},
    {0x7F, "BBR7", "ZPR", 5},
    {0x80, "BRA", "REL", 2},
    {0x81, "STA", "IZX", 6},
    {0x84, "STY", "ZP", 3},
    {0x85, "STA", "ZP", 3},
    {0x86, "STX", "ZP", 3},
    {0x87, "SMB0", "ZP", 5},
    {0x88, "DEY", "IMP", 2},
    {0x89, "BIT", "IMM", 2},
    {0x8A, "TXA", "IMP", 2},
    {0x8C, "STY", "ABS", 4},
    {0x8D, "STA", "ABS", 4},
    {0x8E, "STX", "ABS", 4},
    {0x8F, "BBS0", "ZPR", 5},
    {0x90, "BCC", "REL", 3},  // taken
    {0x91, "STA", "IZY", 6},
    {0x92, "STA", "ZPI", 5},
    {0x94, "STY", "ZPX", 4},
    {0x95, "STA", "ZPX", 4},
    {0x96, "STX", "ZPY", 4},
    {0x97, "SMB1", "ZP", 5},
    {0x98, "TYA", "IMP", 2},
    {0x99, "STA", "ABY", 5},
    {0x9A, "TXS", "IMP", 2},
    {0x9C, "STZ", "ABS", 4},
    {0x9D, "STA", "ABX", 5},
    {0x9E, "STZ", "ABX", 5},
    {0x9F, "BBS1", "ZPR", 5},
    {0xA0, "LDY", "IMM", 2},
    {0xA1, "LDA", "IZX", 6},
    {0xA2, "LDX", "IMM", 2},
    {0xA4, "LDY", "ZP", 3},
    {0xA5, "LDA", "ZP", 3},
    {0xA6, "LDX", "ZP", 3},
    {0xA7, "SMB2", "ZP", 5},
    {0xA8, "TAY", "IMP", 2},
    {0xA9, "LDA", "IMM", 2},
    {0xAA, "TAX", "IMP", 2},
    {0xAC, "LDY", "ABS", 4},
    {0xAD, "LDA", "ABS", 4},
    {0xAE, "LDX", "ABS", 4},
    {0xAF, "BBS2", "ZPR", 5},
    {0xB0, "BCS", "REL", 2},
    {0xB1, "LDA", "IZY", 5},
    {0xB2, "LDA", "ZPI", 5},
    {0xB4, "LDY", "ZPX", 4},
    {0xB5, "LDA", "ZPX", 4},
    {0xB6, "LDX", "ZPY", 4},
    {0xB7, "SMB3", "ZP", 5},
    {0xB8, "CLV", "IMP", 2},
    {0xB9, "LDA", "ABY", 4},
    {0xBA, "TSX", "IMP", 2},
    {0xBC, "LDY", "ABX", 4},
    {0xBD, "LDA", "ABX", 4},
    {0xBE, "LDX", "ABY", 4},
    {0xBF, "BBS3", "ZPR", 5},
    {0xC0, "CPY", "IMM", 2},
    {0xC1, "CMP", "IZX", 6},
    {0xC4, "CPY", "ZP", 3},
    {0xC5, "CMP", "ZP", 3},
    {0xC6, "DEC", "ZP", 5},
    {0xC7, "SMB4", "ZP", 5},
    {0xC8, "INY", "IMP", 2},
    {0xC9, "CMP", "IMM", 2},
    {0xCA, "DEX", "IMP", 2},
    {0xCB, "WAI", "IMP", 3},
    {0xCC, "CPY", "ABS", 4},
    {0xCD, "CMP", "ABS", 4},
    {0xCE, "DEC", "ABS", 6},
    {0xCF, "BBS4", "ZPR", 5},
    {0xD0, "BNE", "REL", 3},  // taken
    {0xD1, "CMP", "IZY", 5},
    {0xD2, "CMP", "ZPI", 5},
    {0xD5, "CMP", "ZPX", 4},
    {0xD6, "DEC", "ZPX", 6},
    {0xD7, "SMB5", "ZP", 5},
    {0xD8, "CLD", "IMP", 2},
    {0xD9, "CMP", "ABY", 4},
    {0xDA, "PHX", "IMP", 3},
    {0xDB, "STP", "IMP", 3},
    {0xDD, "CMP", "ABX", 4},
    {0xDE, "DEC", "ABX", 7},
    {0xDF, "BBS5", "ZPR", 5},
    {0xE0, "CPX", "IMM", 2},
    {0xE1, "SBC", "IZX", 6},
    {0xE4, "CPX", "ZP", 3},
    {0xE5, "SBC", "ZP", 3},
    {0xE6, "INC", "ZP", 5},
    {0xE7, "SMB6", "ZP", 5},
    {0xE8, "INX", "IMP", 2},
    {0xE9, "SBC", "IMM", 2},
    {0xEA, "NOP", "IMP", 2},
    {0xEC, "CPX", "ABS", 4},
    {0xED, "SBC", "ABS", 4},
    {0xEE, "INC", "ABS", 6},
    {0xEF, "BBS6", "ZPR", 5},
    {0xF0, "BEQ", "REL", 2},
    {0xF1, "SBC", "IZY", 5},
    {0xF2, "SBC", "ZPI", 5},
    {0xF5, "SBC", "ZPX", 4},
    {0xF6, "INC", "ZPX", 6},
    {0xF7, "SMB7", "ZP", 5},
    {0xF8, "SED", "IMP", 2},
    {0xF9, "SBC", "ABY", 4},
    {0xFA, "PLX", "IMP", 4},
    {0xFD, "SBC", "ABX", 4},
    {0xFE, "INC", "ABX", 7},
    {0xFF, "BBS7", "ZPR", 5},
};

class CpuCycleTest : public ::testing::Test {
protected:
    // Fresh machine per instruction: staged at $0200 with a zero-page operand of
    // $10 and an absolute operand of $0310, all flags clear.
    static int measure(const uint8_t opcode) {
        Memory mem{nullptr, nullptr};
        CPU6502 cpu{mem};
        mem.writeWord(0xFFFE, 0x0300);
        mem.writeWord(0xFFFA, 0x0400);
        mem.writeWord(0xFFFC, 0x0200);
        cpu.reg.SP = 0xFF;
        cpu.reg.PC = 0x0200;
        cpu.setFlag(CPU6502::kZero, false);
        cpu.setFlag(CPU6502::kNegative, false);
        cpu.setFlag(CPU6502::kCarry, false);
        cpu.setFlag(CPU6502::kOverflow, false);
        mem.write(0x0200, opcode);
        mem.write(0x0201, 0x10);
        mem.write(0x0202, 0x03);
        const uint64_t before = cpu.getCycles();
        cpu.executeSingleInstruction();
        return static_cast<int>(cpu.getCycles() - before);
    }
};

TEST_F(CpuCycleTest, EveryDefinedOpcodeMatchesItsDatasheetCycleCount) {
    for (const auto &e : kExpected) {
        EXPECT_EQ(measure(e.opcode), e.cycles)
            << "$" << std::hex << static_cast<int>(e.opcode) << std::dec
            << " " << e.mnemonic << " " << e.mode;
    }
}

// The instructions that were furthest off, called out so a regression names itself
// rather than surfacing as one of 211 anonymous failures.
TEST_F(CpuCycleTest, StackAndControlFlowInstructionsAreExact) {
    EXPECT_EQ(measure(0x20), 6) << "JSR abs";
    EXPECT_EQ(measure(0x60), 6) << "RTS";
    EXPECT_EQ(measure(0x40), 6) << "RTI";
    EXPECT_EQ(measure(0x00), 7) << "BRK";
    EXPECT_EQ(measure(0x48), 3) << "PHA";
    EXPECT_EQ(measure(0x08), 3) << "PHP";
    EXPECT_EQ(measure(0x68), 4) << "PLA";
    EXPECT_EQ(measure(0x28), 4) << "PLP";
    EXPECT_EQ(measure(0xDA), 3) << "PHX";
    EXPECT_EQ(measure(0xFA), 4) << "PLX";
    EXPECT_EQ(measure(0xEA), 2) << "NOP";
}

// Hardware interrupt entry is 7 cycles, same as BRK.
TEST_F(CpuCycleTest, InterruptEntryCostsSevenCycles) {
    Memory mem{nullptr, nullptr};
    CPU6502 cpu{mem};
    mem.writeWord(0xFFFE, 0x0300);
    mem.writeWord(0xFFFA, 0x0400);
    cpu.reg.SP = 0xFF;
    cpu.reg.PC = 0x0200;
    mem.write(0x0200, 0xEA);

    cpu.setFlag(CPU6502::kInterrupt, false);
    cpu.setIrqLine(true);
    uint64_t before = cpu.getCycles();
    cpu.executeSingleInstruction();
    EXPECT_EQ(static_cast<int>(cpu.getCycles() - before), 7) << "IRQ entry";

    cpu.setIrqLine(false);
    cpu.requestNmi();
    before = cpu.getCycles();
    cpu.executeSingleInstruction();
    EXPECT_EQ(static_cast<int>(cpu.getCycles() - before), 7) << "NMI entry";
}

// Indexed reads pay one extra cycle only when the index carries into a new page.
TEST_F(CpuCycleTest, IndexedReadsPayThePageCrossingPenalty) {
    auto ldaAbsX = [](const uint16_t base, const uint8_t x) {
        Memory mem{nullptr, nullptr};
        CPU6502 cpu{mem};
        cpu.reg.PC = 0x0200;
        cpu.reg.X = x;
        mem.write(0x0200, 0xBD);                       // LDA abs,X
        mem.write(0x0201, base & 0xFF);
        mem.write(0x0202, base >> 8);
        const uint64_t before = cpu.getCycles();
        cpu.executeSingleInstruction();
        return static_cast<int>(cpu.getCycles() - before);
    };
    EXPECT_EQ(ldaAbsX(0x0310, 0x02), 4) << "same page";
    EXPECT_EQ(ldaAbsX(0x02F0, 0x20), 5) << "crosses into the next page";
}

// A branch costs 2 not taken, 3 taken, 4 taken across a page boundary -- where
// the page compared is the one holding the address after the operand byte.
TEST_F(CpuCycleTest, BranchesCostTwoThreeOrFour) {
    auto bne = [](const uint16_t at, const uint8_t offset, const bool zero) {
        Memory mem{nullptr, nullptr};
        CPU6502 cpu{mem};
        cpu.reg.PC = at;
        cpu.setFlag(CPU6502::kZero, zero);
        mem.write(at, 0xD0);
        mem.write(at + 1, offset);
        const uint64_t before = cpu.getCycles();
        cpu.executeSingleInstruction();
        return static_cast<int>(cpu.getCycles() - before);
    };
    EXPECT_EQ(bne(0x0200, 0x10, true), 2) << "not taken";
    EXPECT_EQ(bne(0x0200, 0x10, false), 3) << "taken, same page";
    EXPECT_EQ(bne(0x02F0, 0x20, false), 4) << "taken, crosses a page";
}

} // namespace
