/**
 * @file test_vic_softfont.cpp
 * @brief The VIC's glyph shapes are RAM, not a fixed ROM: an index/data port at
 *        $FE62-$FE64, several complete font sets, and a command to pick the live one.
 *
 * Drives the register port directly (no 6502). The point of the feature is that a
 * program uploads phase variants ONCE and then switches sets with a single write --
 * the equivalent of repointing the C64's $D018 -- so the tests care most about
 * set selection and about the safety defaults that stop a program stranding the
 * shell with an unreadable font.
 */

#include <gtest/gtest.h>

#include "computer/VIC.h"
#include "computer/Cp437Font.h"

using Computer::VIC;

namespace
{
    void fontSeek(VIC &v, uint32_t index)
    {
        v.write(VIC::kRegFontLo, static_cast<uint8_t>(index & 0xFF));
        v.write(VIC::kRegFontHi, static_cast<uint8_t>((index >> 8) & 0xFF));
    }

    void command(VIC &v, uint8_t cmd, uint8_t param = 0)
    {
        v.write(VIC::kRegCmdParam, param);
        v.write(VIC::kRegCmd, cmd);
    }

    /// Write one glyph's 16 scanlines at `index`, all the same byte, so a test can
    /// tell one variant from another at a glance.
    void fillGlyph(VIC &v, uint32_t index, uint8_t byte)
    {
        fontSeek(v, index);
        for (int i = 0; i < VIC::kGlyphBytes; ++i)
        {
            v.write(VIC::kRegFontData, byte);
        }
    }
}

// --- addressing -----------------------------------------------------------

TEST(VicSoftFont, PortAddressesAreRoutedToTheVic)
{
    // Pinned as literals: the routing derives from these, so a future map change
    // that moves them must fail here rather than silently land on another device.
    EXPECT_EQ(VIC::kRegFontLo, 0xFE62);
    EXPECT_EQ(VIC::kRegFontHi, 0xFE63);
    EXPECT_EQ(VIC::kRegFontData, 0xFE64);

    EXPECT_TRUE(VIC::isVideoRegAddress(0xFE62));
    EXPECT_TRUE(VIC::isVideoRegAddress(0xFE63));
    EXPECT_TRUE(VIC::isVideoRegAddress(0xFE64));
}

TEST(VicSoftFont, NeighboursAreNotClaimed)
{
    // $FE61 below is the PowerSwitch. Above, $FE65 is no longer free -- the sprite
    // block starts there -- so the boundary that matters now is the end of that block.
    EXPECT_FALSE(VIC::isVideoRegAddress(0xFE61));
    EXPECT_TRUE(VIC::isVideoRegAddress(0xFE65)) << "sprite 0 lives here now";
    EXPECT_FALSE(VIC::isVideoRegAddress(VIC::kRegSpriteLast + 1))
        << "first free byte in the I/O page";
    // The original block is still intact and the SID after it is still not ours.
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegAddrLo));
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegScrollBot));
    EXPECT_FALSE(VIC::isVideoRegAddress(0xFE38));
}

// --- defaults -------------------------------------------------------------

TEST(VicSoftFont, DefaultsToTheRomFont)
{
    VIC v;
    const uint8_t *rows = v.glyphRows('A');
    for (int i = 0; i < VIC::kGlyphBytes; ++i)
    {
        EXPECT_EQ(rows[i], Computer::kCp437Font['A' * 16 + i]) << "row " << i;
    }
}

TEST(VicSoftFont, EverySetIsSeededWithCp437)
{
    // So a program can switch to RAM, redefine a handful of glyphs, and still have
    // readable text in the other 248.
    VIC v;
    command(v, VIC::kCmdFontRam);
    for (uint8_t set = 0; set < VIC::kFontSets; ++set)
    {
        command(v, VIC::kCmdFontSet, set);
        EXPECT_EQ(v.glyphRows('Z')[0], Computer::kCp437Font['Z' * 16]) << "set " << int(set);
    }
}

// --- the data port --------------------------------------------------------

TEST(VicSoftFont, DataPortRoundTripsAndAutoIncrements)
{
    VIC v;
    fontSeek(v, 0);
    for (int i = 0; i < 4; ++i)
    {
        v.write(VIC::kRegFontData, static_cast<uint8_t>(0xA0 + i));
    }

    fontSeek(v, 0);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(v.read(VIC::kRegFontData), 0xA0 + i) << "byte " << i;
    }
}

TEST(VicSoftFont, IndexSpansTheWholeFontRamNotJustTheCellPlane)
{
    // The cell index wraps at 2000; this one must reach every byte of every set,
    // which is what makes the sets addressable at all.
    VIC v;
    const uint32_t last = VIC::kFontSize * VIC::kFontSets - 1;
    ASSERT_GT(last, 2000u);

    fontSeek(v, last);
    v.write(VIC::kRegFontData, 0x5A);
    fontSeek(v, last);
    EXPECT_EQ(v.read(VIC::kRegFontData), 0x5A);
}

TEST(VicSoftFont, UploadedGlyphIsWhatTheRendererGets)
{
    VIC v;
    fillGlyph(v, 'Q' * VIC::kGlyphBytes, 0xFF);

    // Still the ROM until the program says otherwise.
    EXPECT_EQ(v.glyphRows('Q')[0], Computer::kCp437Font['Q' * 16]);

    command(v, VIC::kCmdFontRam);
    for (int i = 0; i < VIC::kGlyphBytes; ++i)
    {
        EXPECT_EQ(v.glyphRows('Q')[i], 0xFF) << "row " << i;
    }
}

// --- set selection: the feature this exists for ---------------------------

TEST(VicSoftFont, SwitchingSetsSwitchesTheGlyph)
{
    VIC v;
    // Same glyph code, a different shape in each set -- exactly how phase variants
    // for smooth scrolling are laid out.
    for (uint8_t set = 0; set < 4; ++set)
    {
        fillGlyph(v, static_cast<uint32_t>(set) * VIC::kFontSize + 'T' * VIC::kGlyphBytes,
                  static_cast<uint8_t>(0x10 + set));
    }

    command(v, VIC::kCmdFontRam);
    for (uint8_t set = 0; set < 4; ++set)
    {
        command(v, VIC::kCmdFontSet, set);
        EXPECT_EQ(v.glyphRows('T')[0], 0x10 + set) << "set " << int(set);
    }
}

TEST(VicSoftFont, OutOfRangeSetIsIgnored)
{
    VIC v;
    fillGlyph(v, VIC::kFontSize + 'T' * VIC::kGlyphBytes, 0x33);
    command(v, VIC::kCmdFontRam);
    command(v, VIC::kCmdFontSet, 1);
    ASSERT_EQ(v.glyphRows('T')[0], 0x33);

    command(v, VIC::kCmdFontSet, VIC::kFontSets); // one past the end
    EXPECT_EQ(v.glyphRows('T')[0], 0x33) << "a wild set index must not move the display";
}

// --- safety: no program can strand the shell ------------------------------

TEST(VicSoftFont, FontResetRestoresCp437InEverySet)
{
    VIC v;
    for (uint8_t set = 0; set < VIC::kFontSets; ++set)
    {
        fillGlyph(v, static_cast<uint32_t>(set) * VIC::kFontSize + 'M' * VIC::kGlyphBytes, 0x7E);
    }

    command(v, VIC::kCmdFontReset);
    command(v, VIC::kCmdFontRam);
    for (uint8_t set = 0; set < VIC::kFontSets; ++set)
    {
        command(v, VIC::kCmdFontSet, set);
        EXPECT_EQ(v.glyphRows('M')[0], Computer::kCp437Font['M' * 16]) << "set " << int(set);
    }
}

TEST(VicSoftFont, ClearReturnsToTheRomFontAndSetZero)
{
    // Same reasoning as kCmdClear resetting double-size rows: however badly a program
    // exits, the shell must come back readable. A program that wants a soft font sets
    // it up after clearing, which it does anyway.
    VIC v;
    fillGlyph(v, VIC::kFontSize + 'W' * VIC::kGlyphBytes, 0x0F);
    command(v, VIC::kCmdFontRam);
    command(v, VIC::kCmdFontSet, 1);
    ASSERT_EQ(v.glyphRows('W')[0], 0x0F);

    command(v, VIC::kCmdClear, ' ');
    EXPECT_EQ(v.glyphRows('W')[0], Computer::kCp437Font['W' * 16])
        << "a clear must put the ROM font back";

    // ...and it selected set 0, not merely the ROM.
    command(v, VIC::kCmdFontRam);
    EXPECT_EQ(v.glyphRows('W')[0], Computer::kCp437Font['W' * 16]);
}

TEST(VicSoftFont, FontUploadDoesNotDisturbTheCellPlane)
{
    // The two ports share nothing: a font write must not move the cell index, which
    // is the failure mode a "font access mode" flag on VREG_CHAR would have had.
    VIC v;
    v.write(VIC::kRegAddrLo, 0x10);
    v.write(VIC::kRegAddrHi, 0x00);
    v.write(VIC::kRegChar, 'X');

    fillGlyph(v, 0, 0x55);

    v.write(VIC::kRegAddrLo, 0x10);
    v.write(VIC::kRegAddrHi, 0x00);
    EXPECT_EQ(v.read(VIC::kRegChar), 'X');
}
