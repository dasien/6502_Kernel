/**
 * @file test_edit_refresh.cpp
 * @brief What EDIT redraws per keystroke, and what that costs, on the real blob.
 *
 * EDIT is a full-screen editor on a 1MHz machine, so redraw cost is a feature,
 * not an implementation detail: at 80 cells a row and ~9,700 cycles a row, a
 * 24-row repaint is ~0.28s and the editor visibly cannot keep up with a held
 * arrow key. Every vertical cursor move used to force exactly that, because
 * repainting was the only way to erase the reverse-video block cursor.
 *
 * Rather than assert on which rows get painted (an internal detail that a later
 * rewrite is entitled to change), the budgets measure what the user actually
 * feels: the cycles the editor burns between accepting one keystroke and
 * accepting the next. Against the pre-fix blob, on this same document, the three
 * budgeted figures were 463,612 cycles for a cursor move, 510,405 for a window
 * scroll and 62,830 for a typed character -- so all three fail on it.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "computer/CPU6502.h"
#include "computer/Computer6502.h"
#include "computer/Memory.h"
#include "computer/PIA.h"
#include "computer/VIC.h"

using Computer::Computer6502;
using Computer::CPU6502;

namespace
{
    class EditCostTest : public ::testing::Test
    {
    protected:
        Computer6502 computer;
        CPU6502 *cpu = nullptr;
        Computer::PIA *pia = nullptr;

        // 60Hz interval timer, driven off the cycle counter. On the machine the GUI
        // pulses it; a harness stepping the CPU directly has to do it or the jiffy
        // counter never moves and EDIT's title card waits for ever.
        static constexpr uint64_t kCyclesPerJiffy = 1000000 / 60;
        uint64_t next_jiffy_ = kCyclesPerJiffy;

        void SetUp() override
        {
            computer.power_on();
            cpu = computer.getCpu();
            pia = computer.getPia();

            std::ifstream f("../kernel/edit.bin", std::ios::binary);
            ASSERT_TRUE(f.good()) << "edit.bin not found - build the edit_bin target";
            const std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                                            std::istreambuf_iterator<char>());
            for (size_t i = 0; i < blob.size(); ++i)
                computer.getMemory()->write(static_cast<uint16_t>(0x0800 + i), blob[i]);

            cpu->reg.SP = 0xFF;
            cpu->pushByte(0xFF);
            cpu->pushByte(0xFF);
            cpu->reg.PC = 0x0800;
            // Entering at $0800 skips the kernel boot, which would have CLI'd for us.
            cpu->setFlag(CPU6502::kInterrupt, false);
        }

        bool tick()
        {
            const bool ok = cpu->executeSingleInstruction();
            if (cpu->getCycles() >= next_jiffy_) {
                next_jiffy_ = cpu->getCycles() + kCyclesPerJiffy;
                pia->pulseTimerIrq();
            }
            return ok;
        }

        void run(long n) { for (long i = 0; i < n; ++i) if (!tick()) break; }

        /* Queue a key sequence and run until EDIT has taken the last byte of it.
         * The gap between two consecutive drains is the work EDIT does for the
         * first sequence: it finishes handling that key, comes back round the
         * loop, and only then reads the next one -- which is already waiting. */
        uint64_t feedAndDrain(const std::string &seq)
        {
            for (unsigned char c : seq) pia->addKeypress(c);
            for (long i = 0; i < 20'000'000; ++i) {
                if (!tick()) break;
                if (!pia->hasKeypress()) break;
            }
            return cpu->getCycles();
        }

        // Average cycles per keystroke over `reps` presses of `seq`.
        uint64_t costOf(const std::string &seq, int reps)
        {
            uint64_t prev = cpu->getCycles(), total = 0;
            for (int i = 0; i < reps; ++i) {
                const uint64_t now = feedAndDrain(seq);
                total += now - prev;
                prev = now;
            }
            return total / static_cast<uint64_t>(reps);
        }

        // Read screen row y as text, trailing blanks trimmed.
        std::string screenRow(int y)
        {
            auto *vic = computer.getVideoChip();
            std::string r;
            for (int x = 0; x < 80; ++x)
                r += static_cast<char>(vic->getCharacterAt(static_cast<uint16_t>(x),
                                                           static_cast<uint16_t>(y)));
            while (!r.empty() && r.back() == ' ') r.pop_back();
            return r;
        }

        /* Type a string a key at a time, waiting for EDIT to take each one. The
           PIA's keyboard buffer is small and silently drops keys once it is full,
           so queueing a whole document up front loses most of it. */
        void type(const std::string &text)
        {
            for (char c : text) feedAndDrain(std::string(1, c));
        }

        // 30 distinguishable lines L01..L30, then a trailing empty line, so the
        // document is longer than the 24-row window and every row is identifiable.
        void openWithNumberedText()
        {
            run(4'000'000);
            for (int l = 1; l <= 30; ++l) {
                type(std::string("L") + static_cast<char>('0' + l / 10)
                                      + static_cast<char>('0' + l % 10) + "\r");
            }
            run(2'000'000);
        }

        // Past the title card, then 30 short lines so there is a document to move in.
        void openWithText()
        {
            run(4'000'000);
            for (int l = 0; l < 30; ++l) type("hello\r");
            run(2'000'000);
        }
    };

    /* The block cursor must exist in exactly one place.
     *
     * It is drawn by setting the reverse bit on a cell's attribute, and writing a
     * character stamps the attribute latch over it -- so repainting the row it sat
     * on erased it for free. That is the only reason a cursor move used to repaint
     * all 24 rows. Now the old cell is cleared explicitly, and getting that wrong
     * leaves a highlighted cell behind at every position the cursor has visited. */
    TEST_F(EditCostTest, CursorMoveLeavesNoStaleHighlight)
    {
        openWithText();
        for (int i = 0; i < 4; ++i) feedAndDrain("\x1b[A");    // up through the text
        for (int i = 0; i < 3; ++i) feedAndDrain("\x1b[C");    // and along a line
        run(2'000'000);

        auto *vic = computer.getVideoChip();
        std::vector<std::pair<int, int>> lit;
        for (int y = 0; y < 24; ++y)                    // text area only; row 24 is
            for (int x = 0; x < 80; ++x)                // the reverse-video status line
                if (vic->getColorAt(static_cast<uint16_t>(x), static_cast<uint16_t>(y)) & 0x80)
                    lit.emplace_back(x, y);

        ASSERT_EQ(lit.size(), 1u) << "expected one block cursor, found " << lit.size();
    }

    /* Arrowing off the top or bottom scrolls the window with a chip command
     * instead of repainting it. Getting the direction or the exposed row wrong
     * shows the wrong text, so check the window contents, not just the cost.
     *
     * After typing, the view sits at the end of the document. Walk to the top and
     * back down again; each step past the edge is one chip scroll. */
    TEST_F(EditCostTest, ScrollingTheWindowShowsTheRightLines)
    {
        openWithNumberedText();

        for (int i = 0; i < 30; ++i) feedAndDrain("\x1b[A");   // up to the first line
        run(2'000'000);
        EXPECT_EQ(screenRow(0), "L01");
        EXPECT_EQ(screenRow(23), "L24");

        for (int i = 0; i < 30; ++i) feedAndDrain("\x1b[B");   // and back to the last
        run(2'000'000);
        EXPECT_EQ(screenRow(0), "L08");
        EXPECT_EQ(screenRow(22), "L30");
        EXPECT_EQ(screenRow(23), "");

        // The status row is pinned below the scroll region, so it never shifted.
        EXPECT_EQ(screenRow(24).substr(0, 8), "MFC EDIT");
    }

    // One-line window scrolls go through the chip, so they cost about the same as
    // a cursor move rather than the 24-row repaint they used to (510,405 cycles).
    TEST_F(EditCostTest, ScrollingTheWindowDoesNotRepaintIt)
    {
        openWithNumberedText();
        for (int i = 0; i < 30; ++i) feedAndDrain("\x1b[A");   // park at the top edge
        const uint64_t scrolled = costOf("\x1b[A", 1);        // this one has to scroll
        fprintf(stderr, "[ cost     ] scroll up: %llu cycles\n", (unsigned long long)scrolled);
        EXPECT_LT(scrolled, 100'000u) << "window scroll costs " << scrolled << " cycles";
    }

    // Moving the cursor inside the visible window must not repaint the window.
    // The old cursor is erased by clearing its one cell, so this costs about a
    // row -- not the 24 rows (463,612 cycles) it did before.
    TEST_F(EditCostTest, CursorMoveDoesNotRepaintTheScreen)
    {
        openWithText();
        const uint64_t up = costOf("\x1b[A", 10);
        fprintf(stderr, "[ cost     ] cursor up: %llu cycles\n", (unsigned long long)up);
        EXPECT_LT(up, 100'000u) << "cursor up costs " << up
                                << " cycles - a full-screen repaint is back";
    }

    /* Typing repaints the cursor's row, and writes only the used part of the
     * status row rather than all 80 cells. Was 62,830 cycles -- a thin margin,
     * because writing ~25 status cells instead of 80 is a real but small saving;
     * the budget is here to catch a return to the full-row write, not to leave
     * room for one. */
    TEST_F(EditCostTest, TypingRepaintsOneRowAndOnlyTheUsedStatusCells)
    {
        openWithText();
        const uint64_t typed = costOf("x", 10);
        fprintf(stderr, "[ cost     ] typed char: %llu cycles\n", (unsigned long long)typed);
        EXPECT_LT(typed, 58'000u) << "typing a character costs " << typed << " cycles";
    }
}
