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
 *
 * That division applies to sprite SIZE too. The chip owns the width and height in
 * cells, and those are pinned below. Which glyph lands in which cell -- consecutive
 * codes, row-major from the base, wrapping at 255 -- is the renderer's arithmetic and
 * is not covered here. The same goes for magnify: the bits are pinned below, the
 * doubled destination rectangle is not.
 *
 * BITMAP sprites are the exception. Their pixels are decoded by the chip
 * (VIC::spritePixel), slot composition included, so the pattern format is pinned
 * here and the renderer only looks the results up in the palette.
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

    // Size rides the spare bits of the two position high bytes, so setting it means
    // rewriting those bytes with the position bits intact.
    void resize(VIC &v, uint8_t s, uint8_t w, uint8_t h)
    {
        const VIC::Sprite &sp = v.sprite(s);
        v.write(reg(s, VIC::kSprXHi),
                static_cast<uint8_t>(((sp.x >> 8) & 0x03) | ((w - 1) << VIC::kSprSizeShift)));
        v.write(reg(s, VIC::kSprYHi),
                static_cast<uint8_t>(((sp.y >> 8) & 0x03) | ((h - 1) << VIC::kSprSizeShift) |
                                     (sp.enabled ? VIC::kSprEnable : 0)));
    }

    void command(VIC &v, uint8_t cmd, uint8_t param = 0)
    {
        v.write(VIC::kRegCmdParam, param);
        v.write(VIC::kRegCmd, cmd);
    }

    void patternSeek(VIC &v, uint16_t index)
    {
        v.write(VIC::kRegSprPatLo, static_cast<uint8_t>(index & 0xFF));
        v.write(VIC::kRegSprPatHi, static_cast<uint8_t>(index >> 8));
    }

    // Set one pixel of one slot to a palette index, through the port, leaving its
    // neighbour in the same byte alone.
    void patternPixel(VIC &v, uint8_t slot, uint8_t x, uint8_t y, uint8_t index)
    {
        const uint16_t at = static_cast<uint16_t>(slot * VIC::kSprPatBytes +
                                                  y * VIC::kSprPatRowBytes + x / 2);
        patternSeek(v, at);
        const uint8_t pair = v.read(VIC::kRegSprPatData);
        const uint8_t out = (x & 1) ? static_cast<uint8_t>((pair & 0xF0) | index)
                                    : static_cast<uint8_t>((pair & 0x0F) | (index << 4));
        patternSeek(v, at);
        v.write(VIC::kRegSprPatData, out);
    }

    void bitmapOn(VIC &v, uint8_t s)
    {
        v.write(reg(s, VIC::kSprYHi),
                static_cast<uint8_t>(v.read(reg(s, VIC::kSprYHi)) | VIC::kSprBitmap));
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
    // The font port below it is ours, and so are the palette port, the frame
    // counter and the pattern port above it; the first byte the chip does NOT claim
    // is past all three.
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegFontData));
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegSpriteLast + 1));
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegPaletteLast + 1));
    EXPECT_FALSE(VIC::isVideoRegAddress(VIC::kRegSprPatLast + 1));
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

// --- size -----------------------------------------------------------------

/* A sprite is one cell unless it says otherwise, and the size travels in bits that
 * used to be masked off and thrown away. That is what makes this backwards
 * compatible, and it is worth pinning: the nine tests around this one all place
 * sprites with the pre-size encoding and must keep getting 1x1. */
TEST(VicSprites, SizeDefaultsToOneCellAndRoundTrips)
{
    VIC v;
    EXPECT_EQ(v.sprite(3).w, 1);
    EXPECT_EQ(v.sprite(3).h, 1);

    place(v, 3, 640 - 16, 400 - 32, true);
    resize(v, 3, 2, 2);
    EXPECT_EQ(v.sprite(3).w, 2);
    EXPECT_EQ(v.sprite(3).h, 2);

    // Reading the high bytes back gives position and size together.
    EXPECT_EQ(v.read(reg(3, VIC::kSprXHi)),
              static_cast<uint8_t>((((640 - 16) >> 8) & 0x03) | (1 << VIC::kSprSizeShift)));
    EXPECT_EQ(v.read(reg(3, VIC::kSprYHi)),
              static_cast<uint8_t>((((400 - 32) >> 8) & 0x03) | (1 << VIC::kSprSizeShift) |
                                   VIC::kSprEnable));
}

// Position, size and enable share two bytes; none may clobber another.
TEST(VicSprites, SizeAndPositionAndEnableAreIndependent)
{
    VIC v;
    place(v, 0, 600, 380, true);           // both high bits in use
    resize(v, 0, 4, 3);
    EXPECT_EQ(v.sprite(0).x, 600);
    EXPECT_EQ(v.sprite(0).y, 380);
    EXPECT_TRUE(v.sprite(0).enabled) << "resizing must not switch the sprite off";
    EXPECT_EQ(v.sprite(0).w, 4);
    EXPECT_EQ(v.sprite(0).h, 3);

    place(v, 0, 8, 16, true);              // repositioning with the old encoding...
    EXPECT_EQ(v.sprite(0).w, 1) << "...writes zero size bits, which means 1x1";
    EXPECT_EQ(v.sprite(0).h, 1);
}

// Three bits, stored as size-1, so eight cells is the ceiling in each direction.
TEST(VicSprites, MaximumSizeIsEightCells)
{
    VIC v;
    resize(v, 1, VIC::kSprSizeMax, VIC::kSprSizeMax);
    EXPECT_EQ(v.sprite(1).w, VIC::kSprSizeMax);
    EXPECT_EQ(v.sprite(1).h, VIC::kSprSizeMax);
}

/* Magnify is the OTHER way to be bigger, and confusing it with size is a real
 * mistake with a visible symptom. Size composes adjacent glyph codes, so a 2x2 at
 * code $01 draws $01 $02 $03 $04 -- four different characters tiled into one body,
 * which is exactly what VENTURE's Winky came out as. Magnify stretches the one
 * pattern, which is what a double-size row does to a character. They are independent
 * and a sprite can have both. */
TEST(VicSprites, MagnifyIsIndependentOfSize)
{
    VIC v;
    EXPECT_FALSE(v.sprite(0).magx);
    EXPECT_FALSE(v.sprite(0).magy);

    place(v, 0, 600, 380, true);
    v.write(reg(0, VIC::kSprXHi), static_cast<uint8_t>(((600 >> 8) & 0x03) | VIC::kSprMagX));
    v.write(reg(0, VIC::kSprYHi), static_cast<uint8_t>(((380 >> 8) & 0x03) | VIC::kSprMagY |
                                                       VIC::kSprEnable));
    EXPECT_TRUE(v.sprite(0).magx);
    EXPECT_TRUE(v.sprite(0).magy);
    EXPECT_EQ(v.sprite(0).w, 1) << "magnifying must not imply a size";
    EXPECT_EQ(v.sprite(0).h, 1);
    EXPECT_EQ(v.sprite(0).x, 600) << "nor disturb the position it shares a byte with";
    EXPECT_EQ(v.sprite(0).y, 380);
    EXPECT_TRUE(v.sprite(0).enabled);

    // ...and setting a size must not imply magnification.
    resize(v, 0, 2, 2);
    EXPECT_FALSE(v.sprite(0).magx) << "resizing dropped the magnify bit";
    EXPECT_FALSE(v.sprite(0).magy);
    EXPECT_EQ(v.sprite(0).w, 2);
}

// One axis at a time, since a program may want a wide sprite that is not tall.
TEST(VicSprites, MagnifyAxesAreSeparate)
{
    VIC v;
    v.write(reg(2, VIC::kSprXHi), VIC::kSprMagX);
    EXPECT_TRUE(v.sprite(2).magx);
    EXPECT_FALSE(v.sprite(2).magy);
}

// --- safety ---------------------------------------------------------------

TEST(VicSprites, ClearDisablesEverySprite)
{
    // Same contract as row sizes, the font and fine scroll: however badly a program
    // exits, nothing of its is left hanging over the shell's screen.
    VIC v;
    for (uint8_t i = 0; i < VIC::kSpriteCount; ++i) place(v, i, 8 * i, 16, true);
    for (uint8_t i = 0; i < VIC::kSpriteCount; ++i) ASSERT_TRUE(v.sprite(i).enabled);

    resize(v, 0, 4, 4);                 // and a big one, which must also be undone
    v.write(reg(1, VIC::kSprXHi), VIC::kSprMagX);   // and a magnified one

    command(v, VIC::kCmdClear, ' ');
    for (uint8_t i = 0; i < VIC::kSpriteCount; ++i)
    {
        EXPECT_FALSE(v.sprite(i).enabled) << "sprite " << int(i);
    }
    EXPECT_EQ(v.sprite(0).w, 1) << "a clear returns the size to one cell too";
    EXPECT_EQ(v.sprite(0).h, 1);
    EXPECT_FALSE(v.sprite(1).magx) << "...and drops magnification";
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

// --- bitmap sprites -------------------------------------------------------

TEST(VicSpriteBitmap, PatternRamIsTheAdvertisedSize)
{
    EXPECT_EQ(VIC::kSprPatBytes, 128) << "16x16 at 4 bits a pixel";
    EXPECT_EQ(VIC::kSprPatRamSize, 32768u) << "256 slots";
}

TEST(VicSpriteBitmap, PatternRamIsZeroAtPowerOn)
{
    VIC v;
    patternSeek(v, 0);
    for (uint32_t i = 0; i < VIC::kSprPatRamSize; ++i)
    {
        ASSERT_EQ(v.read(VIC::kRegSprPatData), 0) << "byte " << i;
    }
}

TEST(VicSpriteBitmap, ThePortWritesReadsBackAndAutoIncrements)
{
    VIC v;
    patternSeek(v, 0x1234);
    EXPECT_EQ(v.read(VIC::kRegSprPatLo), 0x34);
    EXPECT_EQ(v.read(VIC::kRegSprPatHi), 0x12);
    for (uint8_t b = 1; b <= 3; ++b) v.write(VIC::kRegSprPatData, b);
    EXPECT_EQ(v.read(VIC::kRegSprPatLo), 0x37) << "one step per data write";

    patternSeek(v, 0x1234);
    for (uint8_t b = 1; b <= 3; ++b) EXPECT_EQ(v.read(VIC::kRegSprPatData), b);
    EXPECT_EQ(v.read(VIC::kRegSprPatLo), 0x37) << "and one per data read";
}

// Like the font index it wraps at the end of its RAM rather than running off it.
TEST(VicSpriteBitmap, TheIndexWrapsAtTheEndOfPatternRam)
{
    VIC v;
    patternSeek(v, 0x7FFF);
    v.write(VIC::kRegSprPatData, 0xAA);
    v.write(VIC::kRegSprPatData, 0xBB);
    patternSeek(v, 0);
    EXPECT_EQ(v.read(VIC::kRegSprPatData), 0xBB) << "the second byte lands at 0";

    patternSeek(v, 0x8000 | 0x0005);
    EXPECT_EQ(v.read(VIC::kRegSprPatHi), 0x00) << "an index past 32 KB folds back";
}

TEST(VicSpriteBitmap, BitmapModeRoundTripsWithoutDisturbingTheRest)
{
    VIC v;
    place(v, 3, 0x2AB, 0x1CD, true);
    resize(v, 3, 2, 3);
    EXPECT_FALSE(v.sprite(3).bitmap) << "glyph sprites are the default";

    bitmapOn(v, 3);
    const VIC::Sprite &sp = v.sprite(3);
    EXPECT_TRUE(sp.bitmap);
    EXPECT_TRUE(v.read(reg(3, VIC::kSprYHi)) & VIC::kSprBitmap) << "and it reads back";
    EXPECT_TRUE(sp.enabled);
    EXPECT_EQ(sp.y, 0x1CD);
    EXPECT_EQ(sp.w, 2);
    EXPECT_EQ(sp.h, 3);

    place(v, 3, 0x2AB, 0x1CD, true);    // a plain position write clears the bit
    EXPECT_FALSE(v.sprite(3).bitmap);
}

// Two pixels a byte, the LEFT one in the high nibble, eight bytes a row.
TEST(VicSpriteBitmap, PixelsDecodeLeftPixelInTheHighNibble)
{
    VIC v;
    patternSeek(v, 5 * VIC::kSprPatBytes + 3 * VIC::kSprPatRowBytes + 2);
    v.write(VIC::kRegSprPatData, 0xAB);            // pixels (4,3) and (5,3) of slot 5

    v.write(reg(0, VIC::kSprGlyph), 5);
    bitmapOn(v, 0);
    const VIC::Sprite &sp = v.sprite(0);
    EXPECT_EQ(v.spritePixel(sp, 4, 3), 0xA);
    EXPECT_EQ(v.spritePixel(sp, 5, 3), 0xB);
    EXPECT_EQ(v.spritePixel(sp, 3, 3), 0) << "0 is transparent";
    EXPECT_EQ(v.spritePixel(sp, 4, 4), 0);
    EXPECT_EQ(v.spritePixel(sp, 16, 0), 0) << "outside a 1x1 sprite is transparent";
}

// A larger sprite composes consecutive SLOTS, row-major, as glyph sprites compose
// consecutive codes.
TEST(VicSpriteBitmap, SlotsComposeRowMajor)
{
    VIC v;
    for (uint8_t i = 0; i < 4; ++i) patternPixel(v, static_cast<uint8_t>(10 + i), 0, 0, 1 + i);

    v.write(reg(0, VIC::kSprGlyph), 10);
    resize(v, 0, 2, 2);
    bitmapOn(v, 0);
    const VIC::Sprite &sp = v.sprite(0);
    EXPECT_EQ(v.spritePixel(sp, 0, 0), 1) << "slot 10, top left";
    EXPECT_EQ(v.spritePixel(sp, 16, 0), 2) << "slot 11, top right";
    EXPECT_EQ(v.spritePixel(sp, 0, 16), 3) << "slot 12, bottom left";
    EXPECT_EQ(v.spritePixel(sp, 16, 16), 4) << "slot 13, bottom right";
    EXPECT_EQ(v.spritePixel(sp, 32, 0), 0) << "past the width";
    EXPECT_EQ(v.spritePixel(sp, 0, 32), 0) << "past the height";
}

TEST(VicSpriteBitmap, SlotsWrapAt255)
{
    VIC v;
    patternPixel(v, 0, 0, 0, 7);
    v.write(reg(0, VIC::kSprGlyph), 255);
    resize(v, 0, 2, 1);
    bitmapOn(v, 0);
    EXPECT_EQ(v.spritePixel(v.sprite(0), 16, 0), 7) << "the slot after 255 is 0";
}

// Pattern RAM is unaffected by the font: a glyph upload cannot touch an image.
TEST(VicSpriteBitmap, PatternRamIsSeparateFromTheFont)
{
    VIC v;
    v.write(VIC::kRegFontLo, 0);
    v.write(VIC::kRegFontHi, 0);
    for (int i = 0; i < 256; ++i) v.write(VIC::kRegFontData, 0xFF);
    patternSeek(v, 0);
    for (int i = 0; i < 256; ++i) ASSERT_EQ(v.read(VIC::kRegSprPatData), 0);
}

// A clear puts sprites back to glyphs, like the rest of their state, but the
// images stay: a program uploads its art once.
TEST(VicSpriteBitmap, ClearTurnsBitmapModeOffButKeepsThePatterns)
{
    VIC v;
    patternPixel(v, 9, 2, 2, 0xC);
    bitmapOn(v, 0);
    command(v, VIC::kCmdClear, ' ');
    EXPECT_FALSE(v.sprite(0).bitmap);

    v.write(reg(0, VIC::kSprGlyph), 9);
    bitmapOn(v, 0);
    EXPECT_EQ(v.spritePixel(v.sprite(0), 2, 2), 0xC);
}
