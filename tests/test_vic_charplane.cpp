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
