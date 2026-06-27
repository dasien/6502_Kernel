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
#include "computer/Computer6502.h"

using Computer::Computer6502;
using Computer::VIC;

namespace
{
    constexpr uint16_t kPrintChar = 0xFF00;   // K_PRINT_CHAR (JMP PRINT_CHAR)
    constexpr uint16_t kClearScreen = 0xFF0C; // K_CLEAR_SCREEN (JMP CLEAR_SCREEN)
    constexpr uint16_t kSetAttr = 0xFF2D;     // K_SET_ATTR (JMP SET_ATTR)
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
        void callKernel(uint16_t entry, uint8_t a, uint8_t x = 0x5A, uint8_t y = 0x3C)
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

            for (int i = 0; i < 5000; ++i)
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
            FAIL() << "kernel routine did not return within the step budget";
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
}
