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

// Clearing the screen resets the scroll region to the whole screen.
TEST(VicCharPlane, ClearResetsScrollRegion)
{
    VIC v;
    v.write(VIC::kRegScrollBot, 1);         // shrink the region
    v.write(VIC::kRegCmd, VIC::kCmdClear);   // clear should reset it to full-screen

    putAt(v, 0, 'X');
    putAt(v, (VIC::kScreenHeight - 1) * VIC::kScreenWidth, 'Z'); // last row
    v.write(VIC::kRegCmd, VIC::kCmdScrollUp);
    EXPECT_EQ(v.getCharacterAt(0, VIC::kScreenHeight - 2), 'Z'); // full-screen scroll moved it up
}
