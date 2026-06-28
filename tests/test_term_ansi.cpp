/**
 * @file test_term_ansi.cpp
 * @brief Headless test of the 6502 serial terminal (programs/term) ANSI engine.
 *
 * Loads the real TERM blob into RAM at $0800 and runs it. The C++ side plays
 * "the BBS": it feeds an ANSI byte stream into the ACIA RX FIFO and then asserts
 * on the VIC screen buffer (getCharacterAt/getColorAt) that the terminal placed
 * the right glyphs in the right cells with the right colors. A second test
 * checks that a keypress is forwarded out the ACIA TX.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "computer/Computer6502.h"
#include "computer/Acia.h"
#include "computer/PIA.h"
#include "computer/VIC.h"

namespace
{
    constexpr uint8_t ATTR_DEFAULT = 0x02; // green on black
}

class TermAnsiTest : public ::testing::Test
{
protected:
    Computer::Computer6502 c;
    Computer::Acia *acia = nullptr;
    Computer::CPU6502 *cpu = nullptr;
    Computer::Memory *mem = nullptr;

    void SetUp() override
    {
        c.power_on();
        acia = c.getAcia();
        cpu = c.getCpu();
        mem = c.getMemory();

        std::ifstream f("../kernel/term.bin", std::ios::binary);
        ASSERT_TRUE(f.good()) << "term.bin not found - build the term_bin target";
        std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
        ASSERT_GE(blob.size(), 0x100u);
        for (size_t i = 0; i < blob.size(); ++i)
            mem->write(static_cast<uint16_t>(0x0800 + i), blob[i]);

        // Enter the program; its final RTS (never reached) would hit the sentinel.
        cpu->reg.SP = 0xFF;
        cpu->pushByte(0xFF);
        cpu->pushByte(0xFF);
        cpu->reg.PC = 0x0800;

        // Let the cc65 startup run and the terminal reach its poll loop
        // (it prints a local banner, then idles reading the ACIA/keyboard).
        step(2'000'000);
    }

    void step(long n)
    {
        for (long i = 0; i < n; ++i)
            if (!cpu->executeSingleInstruction()) break;
    }

    // Feed an ANSI byte stream as "the BBS", then let the terminal render it.
    void feed(const std::string &s)
    {
        for (char ch : s) acia->hostSend(static_cast<uint8_t>(ch));
        step(1'000'000);
    }

    uint8_t charAt(int x, int y) { return c.getVideoChip()->getCharacterAt(x, y); }
    uint8_t colorAt(int x, int y) { return c.getVideoChip()->getColorAt(x, y); }
};

// CUP positioning + glyphs + SGR color land in the right cells.
TEST_F(TermAnsiTest, PositionsAndColorsText)
{
    // clear; cursor to row2,col3 (1-based); "AB"; bright red; "C".
    feed("\x1b[2J\x1b[2;3HAB\x1b[1;31mC");

    EXPECT_EQ(charAt(2, 1), 'A');
    EXPECT_EQ(charAt(3, 1), 'B');
    EXPECT_EQ(charAt(4, 1), 'C');
    EXPECT_EQ(colorAt(2, 1), ATTR_DEFAULT);        // before SGR
    EXPECT_EQ(colorAt(4, 1), 0x41);                // bright(0x40) | red(1)
}

// SGR reverse sets the attribute reverse bit; SGR 0 resets to default.
TEST_F(TermAnsiTest, ReverseAndResetAttributes)
{
    feed("\x1b[2J\x1b[H\x1b[7mR\x1b[0mD");
    EXPECT_EQ(charAt(0, 0), 'R');
    EXPECT_EQ(colorAt(0, 0), 0x82);                // reverse(0x80) | default green(2)
    EXPECT_EQ(charAt(1, 0), 'D');
    EXPECT_EQ(colorAt(1, 0), ATTR_DEFAULT);
}

// A malformed/unknown CSI must be consumed without desyncing the parser.
TEST_F(TermAnsiTest, MalformedSequenceDoesNotDesync)
{
    feed("\x1b[2J\x1b[H\x1b[99ZX");                // ESC[99Z is unknown -> ignored
    EXPECT_EQ(charAt(0, 0), 'X');                  // the 'X' still renders at home
}

// Erase-line (ESC[2K) blanks the cursor's row.
TEST_F(TermAnsiTest, EraseLineClearsRow)
{
    feed("\x1b[2J\x1b[H""HELLO\x1b[2K");
    EXPECT_EQ(charAt(0, 0), ' ');
    EXPECT_EQ(charAt(4, 0), ' ');
}

// A keypress is forwarded out the serial line (keyboard -> ACIA TX).
TEST_F(TermAnsiTest, KeyboardForwardsToSerial)
{
    c.getPia()->addKeypress('k');
    step(500'000);

    bool sawK = false;
    while (acia->hostHasTx())
        if (acia->hostRecv() == 'k') sawK = true;
    EXPECT_TRUE(sawK) << "the terminal did not forward the key to the ACIA TX";
}
