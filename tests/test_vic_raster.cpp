/**
 * @file test_vic_raster.cpp
 * @brief The VIC's raster: which line the beam is on, and splits made mid-frame.
 *
 * The raster is machine time -- the CPU's cycle count since the frame began, 500
 * lines a frame, 400 of them drawn. What makes it useful is the split: a palette,
 * fine-scroll or font change made on a visible line applies from that line down,
 * so the chip records it against its line and hands the finished frame's bands to
 * the renderer. The renderer's clipping is verified by eye; what is pinned here is
 * the chip's side -- the line arithmetic, the latch, and exactly which changes make
 * a band and which do not.
 */

#include <gtest/gtest.h>

#include "computer/VIC.h"

using Computer::VIC;

namespace
{
    // 100 cycles a line: a frame of 500 lines is 50,000 cycles, and line n starts
    // at cycle 100 * n, which keeps the arithmetic in the tests obvious.
    constexpr uint64_t kCyclesPerFrame = 50'000;
    constexpr uint64_t kCyclesPerLine = kCyclesPerFrame / VIC::kLinesPerFrame;

    struct Clocked
    {
        VIC v;
        uint64_t now = 0;
        Clocked()
        {
            v.setClock([this] { return now; }, kCyclesPerFrame);
            v.endFrame(0);
        }
        void at(uint16_t line) { now = line * kCyclesPerLine; }
        uint16_t line()
        {
            const uint8_t lo = v.read(VIC::kRegRasterLo);
            const uint8_t hi = v.read(VIC::kRegRasterHi);
            return static_cast<uint16_t>(lo | ((hi & 1) << 8));
        }
        // A whole slot: red as given, green and blue zero. A split takes a colour on
        // its third byte, so a test that wants a split writes all three.
        void paletteRed(uint8_t slot, uint8_t red)
        {
            v.write(VIC::kRegPaletteIdx, static_cast<uint8_t>(slot * 3));
            v.write(VIC::kRegPaletteData, red);
            v.write(VIC::kRegPaletteData, 0);
            v.write(VIC::kRegPaletteData, 0);
        }
        void command(uint8_t cmd, uint8_t param)
        {
            v.write(VIC::kRegCmdParam, param);
            v.write(VIC::kRegCmd, cmd);
        }
    };
}

// --- addressing -------------------------------------------------------------

TEST(VicRaster, TheChipClaimsItsRasterRegisters)
{
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegRasterLo));
    EXPECT_TRUE(VIC::isVideoRegAddress(VIC::kRegRasterHi));
    EXPECT_EQ(VIC::kRegSprPatLast + 1, VIC::kRegRasterFirst) << "packed after the pattern port";
    EXPECT_FALSE(VIC::isVideoRegAddress(VIC::kRegRasterLast + 1)) << "first free byte";
}

// --- the line -----------------------------------------------------------------

TEST(VicRaster, TheLineCountsFromTheFrameBoundary)
{
    Clocked c;
    c.at(0);   EXPECT_EQ(c.line(), 0);
    c.at(57);  EXPECT_EQ(c.line(), 57);
    c.at(300); EXPECT_EQ(c.line(), 300) << "past 255 needs the high bit";
    c.now = 300 * kCyclesPerLine + kCyclesPerLine - 1;
    EXPECT_EQ(c.line(), 300) << "a line lasts until the next one starts";

    c.v.endFrame(c.now);
    EXPECT_EQ(c.line(), 0) << "a frame boundary restarts the raster";
}

TEST(VicRaster, TheHighByteFlagsBlanking)
{
    Clocked c;
    c.at(399);
    c.v.read(VIC::kRegRasterLo);
    EXPECT_EQ(c.v.read(VIC::kRegRasterHi) & VIC::kRasterBlanking, 0) << "399 is drawn";
    c.at(400);
    c.v.read(VIC::kRegRasterLo);
    EXPECT_NE(c.v.read(VIC::kRegRasterHi) & VIC::kRasterBlanking, 0) << "400 is blanking";
}

// A low-then-high read must be one line, even if the beam moves between them.
TEST(VicRaster, ReadingTheLowByteLatchesTheHigh)
{
    Clocked c;
    c.at(255);
    EXPECT_EQ(c.v.read(VIC::kRegRasterLo), 255);
    c.at(256);                                          // the beam crosses 255/256
    EXPECT_EQ(c.v.read(VIC::kRegRasterHi) & 1, 0)
        << "the high byte came from the new line, making 255 read as 511";
}

TEST(VicRaster, AFrameThatRunsLongHoldsOnItsLastLine)
{
    Clocked c;
    c.now = kCyclesPerFrame + 5000;                     // the boundary is overdue
    EXPECT_EQ(c.line(), VIC::kLinesPerFrame - 1) << "it wrapped into a frame not begun";
}

TEST(VicRaster, WithoutAClockTheRasterReadsLineZero)
{
    VIC v;
    EXPECT_EQ(v.read(VIC::kRegRasterLo), 0);
    EXPECT_EQ(v.read(VIC::kRegRasterHi), 0);
}

// --- splits -------------------------------------------------------------------

TEST(VicRaster, AFrameWithNoChangeIsOneBand)
{
    Clocked c;
    c.at(250);
    c.v.endFrame(c.now);
    EXPECT_EQ(c.v.frameBands().size(), 1u) << "no split, so the renderer draws live state";
}

TEST(VicRaster, APaletteChangeOnAVisibleLineStartsABandThere)
{
    Clocked c;
    c.at(200);
    c.paletteRed(0, 0xC0);
    c.v.endFrame(kCyclesPerFrame);

    const auto &bands = c.v.frameBands();
    ASSERT_EQ(bands.size(), 2u);
    EXPECT_EQ(bands[0].line, 0);
    EXPECT_EQ(bands[1].line, 200);
    EXPECT_EQ(bands[0].state.palette[0], 0x00) << "above the split keeps the old colour";
    EXPECT_EQ(bands[1].state.palette[0], 0xC0) << "from line 200 down is the new one";
}

// A change in the blanking lines has no frame left to split: it is simply where
// the next frame starts.
TEST(VicRaster, AChangeDuringBlankingIsTheNextFramesStart)
{
    Clocked c;
    c.at(450);
    c.paletteRed(0, 0xC0);
    c.v.endFrame(kCyclesPerFrame);
    EXPECT_EQ(c.v.frameBands().size(), 1u) << "blanking split the frame just ended";

    c.v.endFrame(2 * kCyclesPerFrame);
    ASSERT_EQ(c.v.frameBands().size(), 1u);
    EXPECT_EQ(c.v.frameBands()[0].state.palette[0], 0xC0) << "the next frame did not start with it";
}

// Setting a colour is three writes; made on one line they are one split, not three.
TEST(VicRaster, ChangesOnOneLineMakeOneBand)
{
    Clocked c;
    c.at(120);
    c.v.write(VIC::kRegPaletteIdx, 0);
    c.v.write(VIC::kRegPaletteData, 0x10);
    c.v.write(VIC::kRegPaletteData, 0x20);
    c.v.write(VIC::kRegPaletteData, 0x30);
    c.v.endFrame(kCyclesPerFrame);
    ASSERT_EQ(c.v.frameBands().size(), 2u);
    EXPECT_EQ(c.v.frameBands()[1].state.palette[2], 0x30) << "the band holds the last write";
}

// The first-line case: a change at line 0 is the whole frame, not a band of it.
TEST(VicRaster, AChangeOnLineZeroIsTheWholeFrame)
{
    Clocked c;
    c.at(0);
    c.paletteRed(0, 0x40);
    c.v.endFrame(kCyclesPerFrame);
    ASSERT_EQ(c.v.frameBands().size(), 1u);
    EXPECT_EQ(c.v.frameBands()[0].state.palette[0], 0x40);
}

TEST(VicRaster, FineScrollAndTheFontSetSplitToo)
{
    Clocked c;
    c.at(100);
    c.command(VIC::kCmdFineY, 6);
    c.at(300);
    c.command(VIC::kCmdFontSet, 3);
    c.v.endFrame(kCyclesPerFrame);

    const auto &bands = c.v.frameBands();
    ASSERT_EQ(bands.size(), 3u);
    EXPECT_FALSE(bands[0].state.fine_active);
    EXPECT_TRUE(bands[1].state.fine_active);
    EXPECT_EQ(bands[1].state.fine_y, 6);
    EXPECT_EQ(bands[1].state.font_set, 0);
    EXPECT_EQ(bands[2].state.font_set, 3);
    EXPECT_EQ(bands[2].state.fine_y, 6) << "a later band carries the earlier change";
}

// Every visible line can be a band of its own. It was capped at 64, and colour bars
// with a wobble below them overflowed it: everything past the cap merged, and half
// the screen stopped splitting.
TEST(VicRaster, EveryVisibleLineCanBeABand)
{
    Clocked c;
    for (uint16_t line = 1; line < VIC::kVisibleLines; ++line) {
        c.at(line);
        c.paletteRed(0, static_cast<uint8_t>(line));
    }
    c.v.endFrame(kCyclesPerFrame);
    const auto &bands = c.v.frameBands();
    ASSERT_EQ(bands.size(), VIC::kVisibleLines);
    EXPECT_EQ(bands.back().line, VIC::kVisibleLines - 1);
    EXPECT_EQ(bands.back().state.palette[0], static_cast<uint8_t>(VIC::kVisibleLines - 1));
}

// A colour is three writes, and they routinely straddle a line. The split takes the
// colour whole, on its blue byte -- never a stripe of new red over old green and blue.
TEST(VicRaster, AColourSplitsWholeOnItsBlueByte)
{
    Clocked c;
    c.at(120);
    c.v.write(VIC::kRegPaletteIdx, 0);
    c.v.write(VIC::kRegPaletteData, 0x10);      // red, on line 120
    c.at(121);
    c.v.write(VIC::kRegPaletteData, 0x20);      // green, on 121
    c.at(122);
    c.v.write(VIC::kRegPaletteData, 0x30);      // blue, on 122: the colour is complete
    c.v.endFrame(kCyclesPerFrame);

    const auto &bands = c.v.frameBands();
    ASSERT_EQ(bands.size(), 2u) << "a colour straddling lines made more than one split";
    EXPECT_EQ(bands[1].line, 122);
    EXPECT_EQ(bands[1].state.palette[0], 0x10);
    EXPECT_EQ(bands[1].state.palette[1], 0x20);
    EXPECT_EQ(bands[1].state.palette[2], 0x30);
}

// A presented frame is drawn from the live settings however split it was: it is shown
// at the present, with the cells as they are then, and the recorded bands belong to
// that moment's past. KPANIC shook because of this -- it moves its fine offset partway
// into every frame and presents.
TEST(VicRaster, APresentedFrameIsNotDrawnInBands)
{
    Clocked c;
    c.at(120);
    c.command(VIC::kCmdFineY, 6);
    c.v.endFrame(kCyclesPerFrame);
    ASSERT_EQ(c.v.frameBands().size(), 2u);
    EXPECT_TRUE(c.v.drawInBands()) << "an unpresented split frame should draw in bands";

    c.now = kCyclesPerFrame + 120 * kCyclesPerLine;
    c.command(VIC::kCmdFineY, 8);
    c.v.write(VIC::kRegFrame, 0);                   // present
    c.v.endFrame(2 * kCyclesPerFrame);
    ASSERT_EQ(c.v.frameBands().size(), 2u);
    EXPECT_FALSE(c.v.drawInBands()) << "a presented frame was drawn from stale bands";

    // A presenting program that misses ONE present -- a frame whose work ran long --
    // is still a presenting program. Drawing that frame in bands gave KPANIC a judder.
    c.now = 2 * kCyclesPerFrame + 120 * kCyclesPerLine;
    c.command(VIC::kCmdFineY, 10);                  // no present this time
    c.v.endFrame(3 * kCyclesPerFrame);
    EXPECT_FALSE(c.v.drawInBands()) << "one missed present made it a split program";

    c.now = 3 * kCyclesPerFrame + 120 * kCyclesPerLine;
    c.command(VIC::kCmdFineY, 12);                  // and none again: a split program
    c.v.endFrame(4 * kCyclesPerFrame);
    EXPECT_TRUE(c.v.drawInBands()) << "two frames without a present should draw bands";
}

// The renderer draws each band by asking the chip for that band's settings.
TEST(VicRaster, SelectingABandRedirectsTheRenderersAccessors)
{
    Clocked c;
    c.at(200);
    c.paletteRed(1, 0xAB);
    c.command(VIC::kCmdFineY, 9);
    c.v.endFrame(kCyclesPerFrame);
    c.paletteRed(1, 0x11);                          // live state moves on again

    uint8_t r, g, b;
    c.v.selectBand(0);
    c.v.paletteColor(1, r, g, b);
    EXPECT_NE(r, 0xAB) << "band 0 is above the split";
    EXPECT_FALSE(c.v.fineActive());

    c.v.selectBand(1);
    c.v.paletteColor(1, r, g, b);
    EXPECT_EQ(r, 0xAB);
    EXPECT_TRUE(c.v.fineActive());
    EXPECT_EQ(c.v.fineY(), 9);

    c.v.selectBand(-1);
    c.v.paletteColor(1, r, g, b);
    EXPECT_EQ(r, 0x11) << "-1 is the live state";
}

// --- the raster interrupt -------------------------------------------------------

namespace
{
    void compare(Clocked &c, uint16_t line)
    {
        c.v.write(VIC::kRegRasterLo, static_cast<uint8_t>(line & 0xFF));
        c.v.write(VIC::kRegRasterHi, static_cast<uint8_t>(line >> 8));
    }
    void enable(Clocked &c) { c.v.write(VIC::kRegRasterCtl, VIC::kRasterEnable); }
    bool pending(Clocked &c) { return c.v.read(VIC::kRegRasterCtl) & VIC::kRasterPending; }
}

TEST(VicRasterIrq, ReachingTheCompareLineRaisesIt)
{
    Clocked c;
    compare(c, 300);                    // past 255, so the high bit is in play
    enable(c);
    c.at(299); c.v.pollRaster();
    EXPECT_FALSE(c.v.irqAsserted()) << "a line early";
    c.at(300); c.v.pollRaster();
    EXPECT_TRUE(pending(c));
    EXPECT_TRUE(c.v.irqAsserted());
}

// The machine polls between instructions and an instruction can span a line, so
// the exact line may never be seen; crossing it is what counts.
TEST(VicRasterIrq, CrossingTheLineCountsEvenIfItWasNeverSeen)
{
    Clocked c;
    compare(c, 150);
    enable(c);
    c.at(149); c.v.pollRaster();
    c.at(152); c.v.pollRaster();
    EXPECT_TRUE(c.v.irqAsserted()) << "stepping over line 150 missed it";
}

TEST(VicRasterIrq, AcknowledgingLetsGoOfTheLine)
{
    Clocked c;
    compare(c, 50);
    enable(c);
    c.at(50); c.v.pollRaster();
    ASSERT_TRUE(c.v.irqAsserted());
    c.v.write(VIC::kRegRasterCtl, VIC::kRasterAck | VIC::kRasterEnable);
    EXPECT_FALSE(c.v.irqAsserted());
    c.at(60); c.v.pollRaster();
    EXPECT_FALSE(c.v.irqAsserted()) << "it fired twice in one frame for one compare";
}

// Enabling does not look back: a compare the beam already passed waits a frame.
TEST(VicRasterIrq, ACompareAlreadyPassedWaitsForTheNextFrame)
{
    Clocked c;
    c.at(200);
    compare(c, 100);
    enable(c);
    c.at(210); c.v.pollRaster();
    EXPECT_FALSE(c.v.irqAsserted()) << "fired for a line the beam had already left";

    c.v.endFrame(kCyclesPerFrame);
    c.now = kCyclesPerFrame + 100 * kCyclesPerLine;
    c.v.pollRaster();
    EXPECT_TRUE(c.v.irqAsserted()) << "and did not fire when the next frame reached it";
}

TEST(VicRasterIrq, CompareLineZeroFiresAtTheFrameBoundary)
{
    Clocked c;
    compare(c, 0);
    enable(c);
    c.at(420);
    c.v.endFrame(kCyclesPerFrame);
    EXPECT_TRUE(c.v.irqAsserted());
}

TEST(VicRasterIrq, NothingIsRaisedUnlessEnabled)
{
    Clocked c;
    compare(c, 80);
    c.at(90); c.v.pollRaster();
    EXPECT_FALSE(c.v.irqAsserted());
    EXPECT_EQ(c.v.read(VIC::kRegRasterCtl) & VIC::kRasterEnable, 0) << "on at power-up";
}

// A program that quits must not leave the chip calling a handler in memory it no
// longer owns; the shell clears the screen when it takes over, and a clear stops it.
TEST(VicRasterIrq, AClearTurnsItOff)
{
    Clocked c;
    compare(c, 40);
    enable(c);
    c.at(40); c.v.pollRaster();
    ASSERT_TRUE(c.v.irqAsserted());
    c.command(VIC::kCmdClear, ' ');
    EXPECT_FALSE(c.v.irqAsserted());
    EXPECT_EQ(c.v.read(VIC::kRegRasterCtl), 0);
}
