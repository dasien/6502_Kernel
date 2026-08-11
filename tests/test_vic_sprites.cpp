/**
 * @file test_vic_sprites.cpp
 * @brief Sprites: glyphs positioned in pixels, drawn over the cell planes, and
 *        deliberately outside the scroll region.
 *
 * Not moving with the region is the entire reason they exist -- anything in the cell
 * plane rides the fine-scroll offset, so a screen-fixed object like a player's craft
 * sawtooths by a whole cell on every scroll. The renderer geometry is verified by eye;
 * what is pinned here is the register contract and the safety default that stops a
 * program leaving something stranded over the shell.
 */

#include <gtest/gtest.h>

#include "computer/VIC.h"
#include "computer/Cp437Font.h"

using Computer::VIC;

namespace
{
    uint16_t reg(uint8_t sprite, uint8_t field)
    {
        return static_cast<uint16_t>(VIC::kRegSpriteFirst + sprite * VIC::kSpriteStride + field);
    }

    void place(VIC &v, uint8_t s, uint16_t x, uint16_t y, bool on)
    {
        v.write(reg(s, VIC::kSprXLo), static_cast<uint8_t>(x & 0xFF));
        v.write(reg(s, VIC::kSprXHi), static_cast<uint8_t>((x >> 8) & 0x03));
        v.write(reg(s, VIC::kSprYLo), static_cast<uint8_t>(y & 0xFF));
        v.write(reg(s, VIC::kSprYHi), static_cast<uint8_t>(((y >> 8) & 0x03) |
                                                           (on ? VIC::kSprEnable : 0)));
    }

    void command(VIC &v, uint8_t cmd, uint8_t param = 0)
    {
        v.write(VIC::kRegCmdParam, param);
        v.write(VIC::kRegCmd, cmd);
    }
}

// --- addressing -----------------------------------------------------------

TEST(VicSprites, BlockIsRoutedToTheVicAndDoesNotOverrunThePage)
{
    EXPECT_EQ(VIC::kRegSpriteFirst, 0xFE65) << "first free byte after the soft-font port";
    EXPECT_EQ(VIC::kRegSpriteLast,
              0xFE65 + VIC::kSpriteCount * VIC::kSpriteStride - 1);
    EXPECT_LE(VIC::kRegSpriteLast, 0xFEFF) << "must stay inside the I/O page";

    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegSpriteFirst));
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegSpriteLast));
    // The font port below it and the first byte above it are not ours.
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegFontData));
    EXPECT_FALSE(VIC::isVideoRegAddress(VIC::kRegSpriteLast + 1));
}

// --- defaults -------------------------------------------------------------

TEST(VicSprites, AllDisabledByDefault)
{
    VIC v;
    for (uint8_t i = 0; i < VIC::kSpriteCount; ++i)
    {
        EXPECT_FALSE(v.sprite(i).enabled) << "sprite " << int(i);
    }
}

// --- position -------------------------------------------------------------

TEST(VicSprites, PositionRoundTripsIncludingTheHighBits)
{
    VIC v;
    // 632, 352 is the far-right column and the craft's row on an 8x16 grid -- both
    // need their high bits, which is what a one-byte-per-axis design would have lost.
    place(v, 0, 632, 352, true);
    EXPECT_EQ(v.sprite(0).x, 632);
    EXPECT_EQ(v.sprite(0).y, 352);
    EXPECT_TRUE(v.sprite(0).enabled);

    EXPECT_EQ(v.read(reg(0, VIC::kSprXLo)), 632 & 0xFF);
    EXPECT_EQ(v.read(reg(0, VIC::kSprXHi)), (632 >> 8) & 0x03);
    EXPECT_EQ(v.read(reg(0, VIC::kSprYLo)), 352 & 0xFF);
}

TEST(VicSprites, EnableLivesInTheYHighByteAndSurvivesRepositioning)
{
    // The game moves the craft every frame and enables it once, so a position write
    // must not silently switch it off.
    VIC v;
    place(v, 0, 100, 200, true);
    v.write(reg(0, VIC::kSprXLo), 8);
    v.write(reg(0, VIC::kSprYLo), 16);
    EXPECT_TRUE(v.sprite(0).enabled);
    EXPECT_EQ(v.sprite(0).x, 8);
    EXPECT_EQ(v.sprite(0).y, 16);
}

TEST(VicSprites, SpritesAreIndependent)
{
    VIC v;
    place(v, 0, 8, 16, true);
    place(v, 3, 64, 128, true);
    place(v, 7, 632, 384, false);

    EXPECT_EQ(v.sprite(0).x, 8);
    EXPECT_EQ(v.sprite(3).x, 64);
    EXPECT_EQ(v.sprite(7).x, 632);
    EXPECT_TRUE(v.sprite(3).enabled);
    EXPECT_FALSE(v.sprite(7).enabled);
    EXPECT_FALSE(v.sprite(1).enabled) << "an untouched sprite stays off";
}

// --- appearance -----------------------------------------------------------

TEST(VicSprites, GlyphAndAttributeRoundTrip)
{
    VIC v;
    v.write(reg(2, VIC::kSprGlyph), 30);        // CP437 solid up triangle
    v.write(reg(2, VIC::kSprAttr), 0x43);       // bright yellow
    EXPECT_EQ(v.sprite(2).glyph, 30);
    EXPECT_EQ(v.sprite(2).attr, 0x43);
    EXPECT_EQ(v.read(reg(2, VIC::kSprGlyph)), 30);
    EXPECT_EQ(v.read(reg(2, VIC::kSprAttr)), 0x43);
}

TEST(VicSprites, ShapeComesFromTheLiveFontSet)
{
    // Sprites share the cells' font storage, so a program that redefines a glyph gets
    // it on its sprite too -- and phase-shifted sets work for sprites as well.
    VIC v;
    v.write(VIC::kRegFontLo, static_cast<uint8_t>(('A' * 16) & 0xFF));
    v.write(VIC::kRegFontHi, static_cast<uint8_t>((('A' * 16) >> 8) & 0xFF));
    for (int i = 0; i < 16; ++i) v.write(VIC::kRegFontData, 0xC3);

    EXPECT_EQ(v.glyphRows('A')[0], Computer::kCp437Font['A' * 16]) << "ROM until asked";
    command(v, VIC::kCmdFontRam);
    EXPECT_EQ(v.glyphRows('A')[0], 0xC3);
}

// --- safety ---------------------------------------------------------------

TEST(VicSprites, ClearDisablesEverySprite)
{
    // Same contract as row sizes, the font and fine scroll: however badly a program
    // exits, nothing of its is left hanging over the shell's screen.
    VIC v;
    for (uint8_t i = 0; i < VIC::kSpriteCount; ++i) place(v, i, 8 * i, 16, true);
    for (uint8_t i = 0; i < VIC::kSpriteCount; ++i) ASSERT_TRUE(v.sprite(i).enabled);

    command(v, VIC::kCmdClear, ' ');
    for (uint8_t i = 0; i < VIC::kSpriteCount; ++i)
    {
        EXPECT_FALSE(v.sprite(i).enabled) << "sprite " << int(i);
    }
}

TEST(VicSprites, DoNotDisturbTheCellPlane)
{
    VIC v;
    v.write(VIC::kRegAddrLo, 0x40);
    v.write(VIC::kRegAddrHi, 0x00);
    v.write(VIC::kRegChar, 'S');

    place(v, 0, 24, 48, true);
    v.write(reg(0, VIC::kSprGlyph), 'Z');

    v.write(VIC::kRegAddrLo, 0x40);
    v.write(VIC::kRegAddrHi, 0x00);
    EXPECT_EQ(v.read(VIC::kRegChar), 'S') << "a sprite is a separate layer entirely";
}
