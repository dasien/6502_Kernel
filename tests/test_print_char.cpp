/**
 * @file test_print_char.cpp
 * @brief Regression tests for the kernel PRINT_CHAR ABI contract.
 *
 * PRINT_CHAR (reached via the $FF00 jump-table entry K_PRINT_CHAR = JMP
 * PRINT_CHAR) is the system's single character-output primitive. The 80-column
 * rewrite (which moved the screen behind the VIC register port) regressed two
 * contracts that the integration tests' substring scraping did not catch:
 *
 *  1. PRINT_CHAR must return with A unchanged (= the character printed). EhBASIC's
 *     LAB_PRNA does `JSR V_OUTP / CMP #$0D` to detect the CR it just printed and
 *     reset its column counter; if A is clobbered that reset never fires and the
 *     terminal-position tracking drifts. (X and Y must survive too: PRINT_MESSAGE
 *     loops with Y, and the shell line editor indexes its buffer with X.)
 *
 *  2. Control characters (< $20) other than CR/LF/BS must NOT render a glyph.
 *     EhBASIC emits BEL ($07) on every keypress once its input buffer is full;
 *     rendering it would paint garbage cells.
 *
 * These tests call PRINT_CHAR directly on the real kernel ROM and assert both.
 */

#include <gtest/gtest.h>
#include <string>
#include <iomanip>
#include "computer/Computer6502.h"

using Computer::Computer6502;
using Computer::VIC;
using Computer::CPU6502;

namespace
{
    constexpr uint16_t kPrintChar = 0xFF00;   // K_PRINT_CHAR (JMP PRINT_CHAR)
    constexpr uint16_t kClearScreen = 0xFF0C; // K_CLEAR_SCREEN (JMP CLEAR_SCREEN)
    constexpr uint16_t kSetAttr = 0xFF2D;     // K_SET_ATTR (JMP SET_ATTR)
    constexpr uint16_t kReadLine = 0xFF15;    // K_READ_LINE (JMP READ_COMMAND_LINE)
    constexpr uint16_t kMonCmdLen = 0x026A;   // MON_CMDLEN
    constexpr uint16_t kPrintMessage = 0xFF03; // K_PRINT_MESSAGE (JMP PRINT_MESSAGE)
    constexpr uint16_t kMonMsgPtrLo = 0x0016;  // MON_MSG_PTR
    constexpr uint16_t kMonMsgPtrHi = 0x0017;
    constexpr uint16_t kCmdLineCount = 0x0021; // CMD_LINE_COUNT (pager line counter)
    constexpr uint16_t kReturnMarker = 0xABCD;

    class PrintCharTest : public ::testing::Test
    {
    protected:
        Computer6502 computer;

        void SetUp() override
        {
            computer.power_on();
            computer.run(200000); // reach the boot prompt
        }

        // Call a kernel subroutine via its $FF00 jump-table entry with the given
        // A/X/Y, single-stepping until it returns (RTS to kReturnMarker). Leaves
        // the final register state in the CPU for inspection.
        void callKernel(uint16_t entry, uint8_t a, uint8_t x = 0x5A, uint8_t y = 0x3C,
                        int steps = 5000)
        {
            auto *cpu = computer.getCpu();
            const uint8_t sp0 = cpu->reg.SP;

            // Push (marker-1) so the routine's RTS returns to kReturnMarker.
            cpu->pushByte(static_cast<uint8_t>((kReturnMarker - 1) >> 8));
            cpu->pushByte(static_cast<uint8_t>((kReturnMarker - 1) & 0xFF));

            cpu->reg.A = a;
            cpu->reg.X = x;
            cpu->reg.Y = y;
            cpu->reg.PC = entry;

            for (int i = 0; i < steps; ++i)
            {
                if (cpu->reg.PC == kReturnMarker && cpu->reg.SP == sp0)
                {
                    return; // returned cleanly
                }
                if (!cpu->executeSingleInstruction())
                {
                    FAIL() << "unknown opcode while running kernel routine";
                }
            }
            FAIL() << "kernel routine did not return within the step budget"
                   << " (stuck at PC=$" << std::hex << cpu->reg.PC << ")";
        }

        uint8_t cellAt(int x, int y) { return computer.getVideoChip()->getCharacterAt(x, y); }
        uint8_t colorAt(int x, int y) { return computer.getVideoChip()->getColorAt(x, y); }
    };

    // A printable character is written and A/X/Y are preserved across the call.
    TEST_F(PrintCharTest, PrintablePreservesRegistersAndDraws)
    {
        callKernel(kClearScreen, 0x20);   // home the cursor, blank the screen
        callKernel(kPrintChar, 'A');      // print 'A' at (0,0)

        auto *cpu = computer.getCpu();
        EXPECT_EQ(cpu->reg.A, 'A') << "PRINT_CHAR must return A = the character";
        EXPECT_EQ(cpu->reg.X, 0x5A) << "PRINT_CHAR must preserve X";
        EXPECT_EQ(cpu->reg.Y, 0x3C) << "PRINT_CHAR must preserve Y";
        EXPECT_EQ(cellAt(0, 0), 'A') << "the glyph should land in the top-left cell";
    }

    // Carriage return returns A = $0D (EhBASIC's CMP #$0D depends on this).
    TEST_F(PrintCharTest, CarriageReturnReturnsCR)
    {
        callKernel(kClearScreen, 0x20);
        callKernel(kPrintChar, 0x0D);
        EXPECT_EQ(computer.getCpu()->reg.A, 0x0D)
            << "PRINT_CHAR must return A = $0D after a carriage return";
    }

    // A control character (BEL) produces no glyph and does not advance the cursor.
    TEST_F(PrintCharTest, BellProducesNoGlyph)
    {
        callKernel(kClearScreen, 0x20);
        const uint8_t before = cellAt(0, 0); // space after clear
        callKernel(kPrintChar, 0x07);         // BEL

        auto *cpu = computer.getCpu();
        EXPECT_EQ(cpu->reg.A, 0x07) << "PRINT_CHAR must return A unchanged for BEL";
        EXPECT_EQ(cellAt(0, 0), before) << "BEL must not paint a glyph";
        EXPECT_EQ(computer.getMemory()->read(0x0276), 0x00)
            << "BEL must not advance the cursor (CURSOR_X stays 0)";
    }

    // K_SET_ATTR ($FF2D) sets the attribute latch; the next printed glyph carries
    // it into the color plane. (Phase C: the ANSI terminal's color hook.)
    TEST_F(PrintCharTest, SetAttrColorsTheNextGlyph)
    {
        callKernel(kClearScreen, 0x20);
        const uint8_t attr = 0x15; // fg=5 (magenta), bg=2 (green), no bright/reverse
        callKernel(kSetAttr, attr);
        callKernel(kPrintChar, 'Z');

        EXPECT_EQ(cellAt(0, 0), 'Z');
        EXPECT_EQ(colorAt(0, 0), attr) << "the printed cell should take the latched attribute";
        // A cell that was not written keeps the clear-time default ($02).
        EXPECT_EQ(colorAt(1, 0), VIC::kDefaultAttr);
    }

    // K_READ_LINE ($FF15) is documented as returning the line length in A with the
    // zero flag set for an empty line. Both of its exits used to end in a tail jump
    // to PRINT_NEWLINE, and PRINT_CHAR deliberately preserves A -- so callers always
    // got $0D with Z clear. A caller writing "JSR $FF15 / BEQ empty" could never
    // detect an empty line, and one using A as the length read 13 bytes of a 0-byte
    // buffer. Every in-tree caller happens to reload MON_CMDLEN, which is why this
    // went unnoticed, but $FF15 is a published entry point for disk programs.
    TEST_F(PrintCharTest, ReadLineReturnsTheLengthInA)
    {
        auto *pia = computer.getPia();
        auto *cpu = computer.getCpu();

        for (char c : std::string("HI\r")) pia->addKeypress(static_cast<uint8_t>(c));
        callKernel(kReadLine, 0x00);
        EXPECT_EQ(cpu->reg.A, 2) << "A must be the line length, not the printed CR";
        EXPECT_FALSE(cpu->getFlag(CPU6502::kZero)) << "a non-empty line must clear Z";
        EXPECT_EQ(computer.getMemory()->read(kMonCmdLen), 2);
    }

    // K_PRINT_MESSAGE ($FF03) must survive the pager it triggers. PRINT_MESSAGE keeps
    // its string cursor in MON_MSG_PTR as well as Y; when an embedded $0D fills the
    // page, PRINT_CHAR -> PAGE_ADVANCE -> HANDLE_PAGE_BREAK prints "--MORE--" through
    // PRINT_MSG_AY, which re-points MON_MSG_PTR. PAGE_ADVANCE saved X and Y but not
    // the pointer, so the outer message resumed reading from the prompt string and
    // printed its tail (and then whatever ROM followed) instead of its own remaining
    // lines. Build the situation directly: a multi-line message in RAM, printed with
    // the line counter already near a page boundary.
    TEST_F(PrintCharTest, PrintMessageSurvivesAPageBreakMidString)
    {
        auto *mem = computer.getMemory();
        auto *pia = computer.getPia();

        // A five-segment message; each $0D counts a line, so a break lands inside it.
        const uint16_t msg = 0x0900;
        const std::string text = "SEG1\rSEG2\rSEG3\rSEG4\rLAST\r";
        for (size_t i = 0; i < text.size(); ++i)
            mem->write(static_cast<uint16_t>(msg + i), static_cast<uint8_t>(text[i]));
        mem->write(static_cast<uint16_t>(msg + text.size()), 0x00);

        callKernel(kClearScreen, 0x20);

        // Queue the --MORE-- acknowledgements FIRST: a break during the setup below
        // would otherwise block on GET_KEYSTROKE. Then pin the line counter, since
        // the boot banner leaves it at an unknown value.
        for (int i = 0; i < 12; ++i) pia->addKeypress(' ');
        mem->write(kCmdLineCount, 0);

        // Walk up to just under a full page (LINES_PER_PAGE = 24) so the break lands
        // inside the message rather than before it.
        for (int i = 0; i < 21; ++i) callKernel(kPrintChar, 0x0D);

        mem->write(kMonMsgPtrLo, msg & 0xFF);
        mem->write(kMonMsgPtrHi, msg >> 8);
        callKernel(kPrintMessage, 0x00, 0x5A, 0x3C, /*steps=*/400000);

        // Every segment must have been printed. Without the fix the message is
        // abandoned at the break, so the later ones never appear.
        std::string screen;
        for (int y = 0; y < VIC::kScreenHeight; ++y)
            for (int x = 0; x < VIC::kScreenWidth; ++x)
                screen.push_back(static_cast<char>(cellAt(x, y)));
        for (const char *seg : {"SEG1", "SEG2", "SEG3", "SEG4", "LAST"})
            EXPECT_NE(screen.find(seg), std::string::npos)
                << seg << " is missing: the message did not survive the page break";
    }

    TEST_F(PrintCharTest, ReadLineSignalsAnEmptyLineWithZ)
    {
        auto *pia = computer.getPia();
        auto *cpu = computer.getCpu();

        pia->addKeypress('\r');           // Enter on an empty line
        callKernel(kReadLine, 0xFF);      // seed A with junk so a stale value shows
        EXPECT_EQ(cpu->reg.A, 0) << "an empty line must return length 0";
        EXPECT_TRUE(cpu->getFlag(CPU6502::kZero))
            << "the documented empty-line signal is the zero flag";
    }
}
