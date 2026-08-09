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

        /* Jumping straight to $0800 skips the kernel boot, and RESET leaves the I
         * flag set -- so the interval-timer IRQ step() raises would never be taken
         * and the 60 Hz jiffy counter would stay at zero. TERM's title card waits on
         * it, so without this the terminal never reaches its poll loop at all. */
        cpu->setFlag(Computer::CPU6502::kInterrupt, false);

        // Let the cc65 startup run and the terminal reach its poll loop
        // (it prints a local banner, then idles reading the ACIA/keyboard).
        step(2'000'000);
    }

    /* One instruction, plus the interval timer if enough cycles have gone by --
     * on the machine the GUI pulses it, and a harness driving the CPU directly has to
     * do it itself or nothing that waits on elapsed time ever finishes. */
    static constexpr uint64_t kCyclesPerJiffy = 1000000 / 60;
    uint64_t next_jiffy_ = kCyclesPerJiffy;

    void step(long n)
    {
        for (long i = 0; i < n; ++i) {
            if (!cpu->executeSingleInstruction()) break;
            if (cpu->getCycles() >= next_jiffy_) {
                next_jiffy_ = cpu->getCycles() + kCyclesPerJiffy;
                c.getPia()->pulseTimerIrq();
            }
        }
    }

    // Feed an ANSI byte stream as "the BBS", then let the terminal render it.
    void feed(const std::string &s)
    {
        for (char ch : s) acia->hostSend(static_cast<uint8_t>(ch));
        step(1'000'000);
    }

    uint8_t charAt(int x, int y) { return c.getVideoChip()->getCharacterAt(x, y); }

    std::string rowText(int y)
    {
        std::string t;
        for (int x = 0; x < 80; ++x) {
            const uint8_t ch = charAt(x, y);
            t.push_back((ch >= 32 && ch < 127) ? static_cast<char>(ch) : ' ');
        }
        return t;
    }
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

/* DECSTBM: a full-screen app pins a header and a status line and scrolls only the
 * middle. Without it the terminal scrolled all 25 rows on every line feed, so any
 * app that set a region had its header walk off the top -- which is why vi and mc
 * over telnet came out wrong. The region is the chip's, so this also exercises the
 * scroll-top the VIC grew for it. */
TEST_F(TermAnsiTest, ScrollingRegionPinsHeaderAndFooter)
{
    feed("\x1b[2J\x1b[H");
    feed("HEAD\r\n");                              // row 0: the header
    feed("\x1b[25;1HFOOT");                        // row 24: the status line
    feed("\x1b[2;24r");                            // region = rows 1..23 (1-based 2..24)

    /* DECSTBM homes the cursor to the top of the region. Fill the region exactly
       (23 lines, no trailing newline, so nothing has scrolled yet), then one line
       feed to scroll it once and one more line to land in the row that opened. */
    for (int i = 1; i <= 23; ++i) feed("L" + std::to_string(i) + (i < 23 ? "\r\n" : ""));
    feed("\r\n");
    feed("L24");

    EXPECT_EQ(rowText(0).substr(0, 4), "HEAD") << "the header must not scroll away";
    EXPECT_EQ(rowText(24).substr(0, 4), "FOOT") << "the status line must not scroll";
    EXPECT_EQ(rowText(1).substr(0, 3), "L2 ") << "the region scrolled by one line";
    EXPECT_EQ(rowText(22).substr(0, 4), "L23 ");
    EXPECT_EQ(rowText(23).substr(0, 4), "L24 ");
}

// ESC M (reverse index) at the top of the region scrolls it the other way. An app
// panning backwards is unusable without it.
TEST_F(TermAnsiTest, ReverseIndexScrollsTheRegionDown)
{
    feed("\x1b[2J\x1b[H""HEAD");
    feed("\x1b[2;24r");                            // region = rows 1..23
    feed("AAA\r\nBBB");                            // rows 1 and 2
    feed("\x1b[2;1H");                             // back to the top of the region
    feed("\x1bM");                                 // reverse index -> region scrolls down

    EXPECT_EQ(rowText(0).substr(0, 4), "HEAD") << "the header is above the region";
    EXPECT_EQ(rowText(1).substr(0, 3), "   ") << "a blank line came in at the top";
    EXPECT_EQ(rowText(2).substr(0, 3), "AAA");
    EXPECT_EQ(rowText(3).substr(0, 3), "BBB");
}

// ESC[r with no parameters puts the region back to the whole screen.
TEST_F(TermAnsiTest, ResettingTheRegionRestoresFullScreenScrolling)
{
    feed("\x1b[2J\x1b[H""HEAD");
    feed("\x1b[2;24r");                            // shrink...
    feed("\x1b[r");                                // ...and reset
    feed("\x1b[25;1H");                            // last row; a line feed now scrolls all
    feed("\r\n");

    EXPECT_EQ(rowText(0).substr(0, 4), "    ") << "row 0 scrolled off, region is full again";
}

/* A chip-side clear resets the region, so the terminal has to reprogram it or a
 * mid-session ESC[2J silently un-pins the app's header. */
TEST_F(TermAnsiTest, ClearScreenKeepsTheScrollingRegion)
{
    feed("\x1b[2;24r");                            // region = rows 1..23
    feed("\x1b[2J");                               // clear -- resets the chip's region
    feed("\x1b[1;1H""HEAD");                       // header outside the region
    feed("\x1b[24;1H");                            // bottom row OF THE REGION
    for (int i = 0; i < 3; ++i) feed("\r\n");      // scroll the region a few times

    EXPECT_EQ(rowText(0).substr(0, 4), "HEAD") << "the region was not reprogrammed";
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

// Scrollback: rows that scroll off the top are recalled by PgUp (ESC[5~); a
// normal key leaves review and restores the live screen.
TEST_F(TermAnsiTest, ScrollbackPageUpRecallsScrolledLines)
{
    auto onScreen = [&](const std::string &t) {
        for (int y = 0; y < 25; y++) {
            std::string r;
            for (int x = 0; x < 80; x++) r += (char)charAt(x, y);
            if (r.find(t) != std::string::npos) return true;
        }
        return false;
    };
    auto press = [&](const std::string &keys) {
        for (char ch : keys) c.getPia()->addKeypress(static_cast<uint8_t>(ch));
        step(3'000'000);
    };

    feed("\x1b[2J\x1b[H");
    std::string lines;                    // 40 numbered lines -> the early ones scroll off
    for (int n = 1; n <= 40; n++) {
        char b[3] = { (char)('0' + n / 10), (char)('0' + n % 10), 0 };
        lines += std::string("L") + b + "\r\n";
    }
    feed(lines);
    step(3'000'000);

    ASSERT_TRUE(onScreen("L40"));         // a late line is live on screen
    ASSERT_FALSE(onScreen("L05"));        // an early line has scrolled off

    press("\x1b[5~");                     // PgUp -> review reveals scrolled-off lines
    EXPECT_TRUE(onScreen("L05")) << "PgUp did not recall scrolled-off line L05";

    press("x");                           // any key -> leave review, restore live screen
    EXPECT_TRUE(onScreen("L40")) << "live screen not restored after review";
    EXPECT_FALSE(onScreen("L05"));
}

// A BBS probes terminal capabilities with ANSI queries; the terminal must reply
// so the board serves enhanced ANSI instead of falling back to plain ASCII.
TEST_F(TermAnsiTest, AnswersAnsiCapabilityQueries)
{
    auto drainTx = [&] {
        std::string s;
        while (acia->hostHasTx()) s += static_cast<char>(acia->hostRecv());
        return s;
    };

    // Clear + home, then Device Status Report (ESC[6n) -> cursor at row1,col1.
    feed("\x1b[2J\x1b[H\x1b[6n");
    EXPECT_NE(drainTx().find("\x1b[1;1R"), std::string::npos);

    // Device Attributes (ESC[c) -> identify as a VT100-class ANSI terminal.
    feed("\x1b[c");
    EXPECT_NE(drainTx().find("\x1b[?1;0c"), std::string::npos);
}

/* The title card. Three seconds is a long time to leave unverified -- it is the first
 * thing anyone sees, and it is drawn before the terminal has painted anything else.
 *
 * On its own machine, stepped only far enough to be inside the card's three seconds:
 * the fixture deliberately runs past it to reach the poll loop. */
TEST(TermSplash, TitleCardShowsAtStartup)
{
    Computer::Computer6502 box;
    box.power_on();

    std::ifstream f("../kernel/term.bin", std::ios::binary);
    ASSERT_TRUE(f.good()) << "term.bin not found - build the term_bin target";
    const std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    for (size_t i = 0; i < blob.size(); ++i)
        box.getMemory()->write(static_cast<uint16_t>(0x0800 + i), blob[i]);

    auto *bcpu = box.getCpu();
    bcpu->reg.SP = 0xFF;
    bcpu->pushByte(0xFF);
    bcpu->pushByte(0xFF);
    bcpu->reg.PC = 0x0800;
    bcpu->setFlag(Computer::CPU6502::kInterrupt, false);

    // No timer pulses at all, so the card cannot time out and we can read it at
    // leisure. Enough instructions to be well past cc65's startup.
    for (int i = 0; i < 200000; ++i)
        if (!bcpu->executeSingleInstruction()) break;

    std::string title, subtitle;
    for (int x = 0; x < 80; ++x) {
        const uint8_t a = box.getVideoChip()->getCharacterAt(x, 10);
        const uint8_t b = box.getVideoChip()->getCharacterAt(x, 12);
        title.push_back((a >= 32 && a < 127) ? static_cast<char>(a) : ' ');
        subtitle.push_back((b >= 32 && b < 127) ? static_cast<char>(b) : ' ');
    }
    EXPECT_NE(title.find("M F C   T E R M"), std::string::npos)
        << "no title on row 10, got: [" << title << "]";
    EXPECT_NE(subtitle.find("ANSI TERMINAL WITH XMODEM"), std::string::npos)
        << "no subtitle on row 12, got: [" << subtitle << "]";
}
