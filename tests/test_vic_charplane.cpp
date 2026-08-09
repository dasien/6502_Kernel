/**
 * @file test_vic_charplane.cpp
 * @brief The VIC character plane stores full 8-bit CP437 code points, and
 *        reverse-video lives in the attribute plane (not char bit 7).
 *
 * Drives the VIC register port directly (no 6502) and checks that a high-bit
 * glyph is stored whole and that reverse comes only from the attribute latch.
 */

#include <gtest/gtest.h>

#include "computer/VIC.h"

using Computer::VIC;

namespace
{
    void putAt(VIC &v, uint16_t cell, uint8_t glyph)
    {
        v.write(VIC::kRegAddrLo, static_cast<uint8_t>(cell & 0xFF));
        v.write(VIC::kRegAddrHi, static_cast<uint8_t>(cell >> 8));
        v.write(VIC::kRegChar, glyph);
    }
}

// A full 8-bit code point (e.g. 0xDB '█') is stored verbatim, not masked to 7 bits.
TEST(VicCharPlane, StoresFullEightBitGlyph)
{
    VIC v;
    putAt(v, 0, 0xDB);
    EXPECT_EQ(v.getCharacterAt(0, 0), 0xDB); // not 0x5B
}

// Reverse comes from the attribute latch; a high-bit glyph does NOT imply it.
TEST(VicCharPlane, ReverseLivesInTheAttributeNotTheChar)
{
    VIC v;

    v.write(VIC::kRegAttr, 0x82); // reverse (0x80) | green (2)
    putAt(v, 0, 'A');
    EXPECT_EQ(v.getCharacterAt(0, 0), 'A');
    EXPECT_EQ(v.getColorAt(0, 0), 0x82); // attribute carries reverse; char is clean

    v.write(VIC::kRegAttr, 0x02); // plain default
    putAt(v, 1, 0xDB);            // high bit set, but no reverse implied
    EXPECT_EQ(v.getCharacterAt(1, 0), 0xDB);
    EXPECT_EQ(v.getColorAt(1, 0), 0x02);
}

// A scroll region (VREG_SCROLL_BOT) confines scrolling to rows 0..bottom, so an
// app can pin footer rows below it. Here rows 0-1 scroll; row 2 stays put.
TEST(VicCharPlane, ScrollRegionPinsRowsBelow)
{
    VIC v;
    putAt(v, 0 * VIC::kScreenWidth, 'A');   // row 0
    putAt(v, 1 * VIC::kScreenWidth, 'B');   // row 1
    putAt(v, 2 * VIC::kScreenWidth, 'P');   // row 2 (to be pinned)

    v.write(VIC::kRegScrollBot, 1);         // scroll region = rows 0..1
    v.write(VIC::kRegCmd, VIC::kCmdScrollUp);

    EXPECT_EQ(v.getCharacterAt(0, 0), 'B'); // row 1 moved up into row 0
    EXPECT_EQ(v.getCharacterAt(0, 1), ' '); // row 1 blanked
    EXPECT_EQ(v.getCharacterAt(0, 2), 'P'); // row 2 pinned (below the region)
}

/* A region has a top as well as a bottom, so rows above it are pinned too.
 * ANSI's DECSTBM asks for exactly that pair, and a terminal that could only pin
 * footers would scroll an app's header away. The top rides the command engine
 * rather than a register of its own only because the port block ends at $FE37. */
TEST(VicCharPlane, ScrollRegionPinsRowsAboveAndBelow)
{
    VIC v;
    putAt(v, 0 * VIC::kScreenWidth, 'H');   // row 0: header, above the region
    putAt(v, 1 * VIC::kScreenWidth, 'A');   // row 1  |
    putAt(v, 2 * VIC::kScreenWidth, 'B');   // row 2  | the region
    putAt(v, 3 * VIC::kScreenWidth, 'C');   // row 3  |
    putAt(v, 4 * VIC::kScreenWidth, 'F');   // row 4: footer, below the region

    v.write(VIC::kRegCmdParam, 1);          // region = rows 1..3
    v.write(VIC::kRegCmd, VIC::kCmdScrollTop);
    v.write(VIC::kRegScrollBot, 3);
    v.write(VIC::kRegCmdParam, ' ');        // param is shared: put the fill char back
    v.write(VIC::kRegCmd, VIC::kCmdScrollUp);

    EXPECT_EQ(v.getCharacterAt(0, 0), 'H') << "the row above the region must not move";
    EXPECT_EQ(v.getCharacterAt(0, 1), 'B');
    EXPECT_EQ(v.getCharacterAt(0, 2), 'C');
    EXPECT_EQ(v.getCharacterAt(0, 3), ' ') << "the region's bottom row is blanked";
    EXPECT_EQ(v.getCharacterAt(0, 4), 'F') << "the row below the region must not move";
}

// Scrolling the region the other way (what a reverse index does) pins the same rows.
TEST(VicCharPlane, ScrollDownStaysInsideTheRegion)
{
    VIC v;
    putAt(v, 0 * VIC::kScreenWidth, 'H');
    putAt(v, 1 * VIC::kScreenWidth, 'A');
    putAt(v, 2 * VIC::kScreenWidth, 'B');
    putAt(v, 3 * VIC::kScreenWidth, 'C');
    putAt(v, 4 * VIC::kScreenWidth, 'F');

    v.write(VIC::kRegCmdParam, 1);
    v.write(VIC::kRegCmd, VIC::kCmdScrollTop);
    v.write(VIC::kRegScrollBot, 3);
    v.write(VIC::kRegCmdParam, ' ');        // param is shared: put the fill char back
    v.write(VIC::kRegCmd, VIC::kCmdScrollDown);

    EXPECT_EQ(v.getCharacterAt(0, 0), 'H');
    EXPECT_EQ(v.getCharacterAt(0, 1), ' ') << "the region's top row is blanked";
    EXPECT_EQ(v.getCharacterAt(0, 2), 'A');
    EXPECT_EQ(v.getCharacterAt(0, 3), 'B');
    EXPECT_EQ(v.getCharacterAt(0, 4), 'F') << "'C' must not spill past the region";
}

// A write to VREG_ADDR_LO deliberately does not wrap (the stale high byte would
// corrupt the address before the high byte arrives), so cell_index_ can hold up to
// $07FF while the planes are only kScreenSize (2000) entries. Indexing the arrays
// with it directly wrote past the end of the object -- at index 2047 a glyph write
// landed 47 bytes into the colour plane, and a colour write ran past the plane into
// the VIC's own control fields. Every access is now bounded.
TEST(VicCharPlane, OutOfRangeCellIndexDoesNotCorruptState)
{
    VIC v;
    putAt(v, 5, 'A');                      // a known cell to check afterwards
    v.write(VIC::kRegAttr, 0x02);

    // Drive the index out of range the way 6502 code can: high byte then low byte.
    v.write(VIC::kRegAddrHi, 0x07);
    v.write(VIC::kRegAddrLo, 0xFF);        // cell_index_ = 2047 > kScreenSize - 1
    v.write(VIC::kRegChar, 'Z');           // must land in-bounds, not past the plane
    v.write(VIC::kRegColor, 0x41);         // (kRegChar auto-advanced to 2048 -> 48)

    // The write must resolve to the wrapped cell (2047 % 2000 = 47). Unfixed, the
    // glyph went to screen_buffer_[2047] -- 47 bytes PAST the plane, landing in the
    // colour plane instead -- so cell 47's character stayed blank. Asserting the
    // wrapped cell is what discriminates; checking some other cell does not, because
    // the stray byte lands at a specific offset determined by the overrun distance.
    const uint16_t wrapped = 2047 % VIC::kScreenSize;   // 47
    EXPECT_EQ(v.getCharacterAt(wrapped, 0), 'Z')
        << "the glyph must land in the character plane at the wrapped cell";
    EXPECT_EQ(v.getColorAt(5, 0), 0x02) << "an unrelated colour cell must be intact";
    EXPECT_EQ(v.getCharacterAt(5, 0), 'A') << "an unrelated cell must be untouched";

    // The chip is still usable: its control state was not overwritten.
    v.write(VIC::kRegAddrHi, 0x00);
    v.write(VIC::kRegAddrLo, 0x0A);
    v.write(VIC::kRegChar, 'Q');
    EXPECT_EQ(v.getCharacterAt(10, 0), 'Q');
}

// Clearing the screen resets the scroll region to the whole screen.
TEST(VicCharPlane, ClearResetsScrollRegion)
{
    VIC v;
    v.write(VIC::kRegCmdParam, 2);          // shrink the region from both ends
    v.write(VIC::kRegCmd, VIC::kCmdScrollTop);
    v.write(VIC::kRegScrollBot, 4);
    v.write(VIC::kRegCmd, VIC::kCmdClear);   // clear should reset it to full-screen

    putAt(v, 0, 'X');
    putAt(v, (VIC::kScreenHeight - 1) * VIC::kScreenWidth, 'Z'); // last row
    v.write(VIC::kRegCmd, VIC::kCmdScrollUp);
    EXPECT_EQ(v.getCharacterAt(0, VIC::kScreenHeight - 2), 'Z'); // full-screen scroll moved it up
}
