/**
 * @file test_vic_palette.cpp
 * @brief The VIC's soft palette: sixteen loadable colour slots.
 *
 * Every attribute byte names slots rather than colours -- three bits of
 * foreground widened to four by the bright bit, three bits of background -- so
 * what the machine calls "red" is whatever slot 1 currently holds. The palette
 * is chip state for the same reason the font is: whoever owns the screen owns
 * it. The machine boots with the built-in sixteen, a theme is what the DOS loads
 * over them, and a program with an opinion about its colours loads its own.
 *
 * A program with no opinion inherits whatever is there, which is the wanted
 * behaviour rather than a shortcoming -- the Scott Adams adventures are pure
 * text and should follow the machine.
 *
 * The port mirrors the soft font's: an index and an auto-incrementing data
 * register, indexed in BYTES so a caller can restate two slots without sending
 * the whole table.
 */

#include <gtest/gtest.h>

#include "computer/VIC.h"

using Computer::VIC;

namespace
{
    struct Rgb { uint8_t r, g, b; };

    Rgb slot(const VIC &v, uint8_t index)
    {
        Rgb c{};
        v.paletteColor(index, c.r, c.g, c.b);
        return c;
    }

    void seek(VIC &v, uint8_t byte_index)
    {
        v.write(VIC::kRegPaletteIdx, byte_index);
    }

    /// Write one slot as three bytes through the auto-incrementing data port.
    void writeSlot(VIC &v, uint8_t index, uint8_t r, uint8_t g, uint8_t b)
    {
        seek(v, static_cast<uint8_t>(index * 3));
        v.write(VIC::kRegPaletteData, r);
        v.write(VIC::kRegPaletteData, g);
        v.write(VIC::kRegPaletteData, b);
    }

    bool same(const Rgb &c, uint8_t r, uint8_t g, uint8_t b)
    {
        return c.r == r && c.g == g && c.b == b;
    }
}

/* The machine boots with colours, so a program that never touches the palette
   sees exactly what it always has. */
TEST(VicPalette, BootsWithTheBuiltInSixteen)
{
    VIC v;
    EXPECT_TRUE(same(slot(v, 0), 0, 0, 0)) << "slot 0 is the background, and it is black";
    EXPECT_TRUE(same(slot(v, 7), 170, 170, 170)) << "slot 7 is plain white";
    EXPECT_TRUE(same(slot(v, 15), 255, 255, 255)) << "slot 15 is bright white";
}

/* Slot 2 is pure green, NOT CGA's (0,170,0), and this pins a deliberate choice.
 *
 * The renderer used to special-case the default attribute and paint it in
 * Qt::green while palette index 2 held the darker CGA green, so the machine had
 * two greens and which one you got depended on whether your background happened
 * to be black. Everything anyone has actually looked at -- the boot prompt, the
 * DOS, every game drawing with the default attribute -- came out of the first
 * path. Folding them the other way would quietly darken all of it. */
TEST(VicPalette, GreenIsTheOneTheMachineAlwaysShowed)
{
    VIC v;
    EXPECT_TRUE(same(slot(v, 2), 0, 255, 0))
        << "the default text colour changed shade";
}

/* The data port advances, so a run of writes fills consecutive bytes -- the
   same contract the font port has. */
TEST(VicPalette, TheDataPortAdvancesAsItIsWritten)
{
    VIC v;
    writeSlot(v, 1, 10, 20, 30);
    EXPECT_TRUE(same(slot(v, 1), 10, 20, 30));

    // ...and it kept going: the next three bytes land in slot 2 with no reseek.
    v.write(VIC::kRegPaletteData, 40);
    v.write(VIC::kRegPaletteData, 50);
    v.write(VIC::kRegPaletteData, 60);
    EXPECT_TRUE(same(slot(v, 2), 40, 50, 60));
}

/* Indexed in bytes, so restating one slot is a seek and three writes rather
   than resending the table. This is what makes a two-colour theme cheap. */
TEST(VicPalette, OneSlotCanBeRestatedWithoutTouchingTheRest)
{
    VIC v;
    const Rgb before = slot(v, 7);
    writeSlot(v, 0, 0x2a, 0x1e, 0x1d);     // a warm dark background

    EXPECT_TRUE(same(slot(v, 0), 0x2a, 0x1e, 0x1d));
    EXPECT_TRUE(same(slot(v, 7), before.r, before.g, before.b))
        << "writing one slot disturbed another";
}

/* Readable, so a program can save the palette it is about to replace. */
TEST(VicPalette, ThePaletteReadsBack)
{
    VIC v;
    writeSlot(v, 3, 1, 2, 3);
    seek(v, 3 * 3);
    EXPECT_EQ(v.read(VIC::kRegPaletteData), 1);
    EXPECT_EQ(v.read(VIC::kRegPaletteData), 2);
    EXPECT_EQ(v.read(VIC::kRegPaletteData), 3);
}

/* The reset command puts the built-in colours back. This is the DOS's way of
   reclaiming the screen after a program that loaded its own, so a game cannot
   leave the shell wearing its dungeon colours. */
TEST(VicPalette, ResetRestoresTheBuiltInColours)
{
    VIC v;
    for (uint8_t i = 0; i < VIC::kPaletteSlots; i++) writeSlot(v, i, 9, 9, 9);
    ASSERT_TRUE(same(slot(v, 4), 9, 9, 9)) << "the overwrite did not take";

    v.write(VIC::kRegCmd, VIC::kCmdPaletteReset);

    EXPECT_TRUE(same(slot(v, 0), 0, 0, 0));
    EXPECT_TRUE(same(slot(v, 2), 0, 255, 0));
    EXPECT_TRUE(same(slot(v, 15), 255, 255, 255));
}

/* The index wraps rather than running off the table, so a caller that streams
   more than forty-eight bytes writes over its own start instead of the sprites. */
TEST(VicPalette, TheIndexWrapsAtTheEndOfTheTable)
{
    VIC v;
    seek(v, VIC::kPaletteBytes - 1);
    v.write(VIC::kRegPaletteData, 0x11);   // last byte of slot 15
    v.write(VIC::kRegPaletteData, 0x22);   // wrapped: first byte of slot 0

    uint8_t r = 0, g = 0, b = 0;
    v.paletteColor(15, r, g, b);
    EXPECT_EQ(b, 0x11);
    v.paletteColor(0, r, g, b);
    EXPECT_EQ(r, 0x22);
}

/* The palette lives in the I/O page above the sprites, and the chip has to
   claim those two addresses or Memory will not route them here. */
TEST(VicPalette, TheChipClaimsItsPaletteRegisters)
{
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegPaletteIdx));
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegPaletteData));
    // The byte after the palette is the frame counter and after that the sprite
    // pattern port, which the VIC answers too; past those the claim has to stop.
    EXPECT_EQ(VIC::kRegPaletteData + 1, VIC::kRegFrame);
    EXPECT_FALSE(VIC::isVideoRegAddress(VIC::kRegSprPatLast + 1))
        << "the claim is wider than the palette, the frame counter and the pattern port";
}
