/**
 * @file test_vic_finescroll.cpp
 * @brief kCmdFineY slides the scroll region down by a pixel count, so a program can
 *        move the world in steps finer than a character cell.
 *
 * Drives the register port directly (no 6502). The renderer geometry -- the clip and
 * the hidden staging row -- is not unit-testable here and is verified by eye; what
 * IS pinned is the chip-side contract the renderer reads: the offset, whether fine
 * scrolling is on at all, and that it is off for every program that never asks for
 * it. That last one is the important one: turning it on costs a row of the region,
 * so it must never happen by accident.
 */

#include <gtest/gtest.h>

#include "computer/VIC.h"
#include "computer/Cp437Font.h"

using Computer::VIC;

namespace
{
    void command(VIC &v, uint8_t cmd, uint8_t param = 0)
    {
        v.write(VIC::kRegCmdParam, param);
        v.write(VIC::kRegCmd, cmd);
    }
}

TEST(VicFineScroll, OffByDefault)
{
    VIC v;
    EXPECT_FALSE(v.fineActive()) << "a program that never asks must be unaffected";
    EXPECT_EQ(v.fineY(), 0);
}

TEST(VicFineScroll, CommandSetsTheOffsetAndTurnsItOn)
{
    VIC v;
    command(v, VIC::kCmdFineY, 5);
    EXPECT_TRUE(v.fineActive());
    EXPECT_EQ(v.fineY(), 5);

    command(v, VIC::kCmdFineY, 11);
    EXPECT_EQ(v.fineY(), 11);
}

TEST(VicFineScroll, ZeroOffsetStillCountsAsOn)
{
    // The wrap point of every scroll cycle is offset 0, and the clip has to stay put
    // across it -- otherwise the staging row pops into view for one frame each cell.
    VIC v;
    command(v, VIC::kCmdFineY, 15);
    command(v, VIC::kCmdFineY, 0);
    EXPECT_TRUE(v.fineActive()) << "offset 0 is a position, not a request to stop";
    EXPECT_EQ(v.fineY(), 0);
}

TEST(VicFineScroll, OutOfRangeOffsetIsClamped)
{
    // Clamped rather than ignored, unlike a font set: the furthest the region can
    // slide is an obvious right answer, and a game computing this from a tick counter
    // is the normal way to produce one.
    VIC v;
    command(v, VIC::kCmdFineY, 200);
    EXPECT_EQ(v.fineY(), 31);
}

TEST(VicFineScroll, ClearTurnsItOff)
{
    // Same contract as double-size rows and the scroll region: however badly a
    // program exits, the shell gets a whole screen back. Fine scroll costs a row to
    // the staging row, so a stuck offset would eat a line of the shell forever.
    VIC v;
    command(v, VIC::kCmdFineY, 9);
    ASSERT_TRUE(v.fineActive());

    command(v, VIC::kCmdClear, ' ');
    EXPECT_FALSE(v.fineActive());
    EXPECT_EQ(v.fineY(), 0);
}

TEST(VicFineScroll, RegionAccessorReportsWhatTheScrollCommandsSet)
{
    // The renderer derives the clip from these, so they have to agree with the same
    // registers the scroll operations use.
    VIC v;
    uint8_t top = 99, bot = 99;
    v.getScrollRegion(top, bot);
    EXPECT_EQ(top, 0);
    EXPECT_EQ(bot, VIC::kScreenHeight - 1) << "default region is the whole screen";

    command(v, VIC::kCmdScrollTop, 3);
    v.write(VIC::kRegScrollBot, 20);
    v.getScrollRegion(top, bot);
    EXPECT_EQ(top, 3);
    EXPECT_EQ(bot, 20);
}

TEST(VicFineScroll, DoesNotDisturbTheCellPlaneOrTheFont)
{
    // Fine scroll is presentation only: it must not move the cell index, and it must
    // not touch the font selection it shares a command engine with.
    VIC v;
    v.write(VIC::kRegAddrLo, 0x20);
    v.write(VIC::kRegAddrHi, 0x00);
    v.write(VIC::kRegChar, 'K');

    command(v, VIC::kCmdFineY, 7);

    v.write(VIC::kRegAddrLo, 0x20);
    v.write(VIC::kRegAddrHi, 0x00);
    EXPECT_EQ(v.read(VIC::kRegChar), 'K');
    EXPECT_EQ(v.glyphRows('K')[0], Computer::kCp437Font['K' * 16])
        << "fine scroll must not disturb the font selection it shares an engine with";
}
