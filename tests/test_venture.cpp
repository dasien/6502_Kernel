/**
 * @file test_venture.cpp
 * @brief Drives VENTURE (steps 1-5: one playable room) on the emulator.
 *
 * A real-time game cannot be verified by compiling it. These tests run the actual
 * 6502 blob, hold keys down through the PIA's live control port ($FE0F) the way a
 * player would, and read the screen back out of the VIC to see what happened.
 *
 * That is the only way to check the things that matter here: that eight-way
 * movement comes off the control port rather than the keystroke FIFO, that an
 * arrow is limited to one in flight, that a killed monster leaves a body which is
 * still lethal, and that the original's scoring rule holds -- monsters pay
 * nothing until the treasure is in hand.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "computer/Computer6502.h"
#include "computer/CPU6502.h"
#include "computer/Memory.h"
#include "computer/PIA.h"
#include "computer/VIC.h"

using Computer::Computer6502;

namespace {

// Must match venture.h.
constexpr int kScrW = 80;
constexpr int kRoomW = 44, kRoomH = 15;
constexpr int kRoomX = (kScrW - kRoomW) / 2, kRoomY = 5;

constexpr uint8_t kGlyphWinky = 0x01, kGlyphWinky2 = 0x02;
constexpr uint8_t kGlyphWall = 0xDB, kGlyphCorpse = 0xB0;
constexpr uint8_t kGlyphSerpent = 0x15, kGlyphApples = 0x05;

constexpr uint8_t kKsUp = 0x01, kKsDown = 0x02, kKsLeft = 0x04,
                  kKsRight = 0x08, kKsFire = 0x10;

class VentureTest : public ::testing::Test
{
protected:
    Computer6502 c;
    Computer::CPU6502 *cpu = nullptr;
    Computer::Memory *mem = nullptr;
    Computer::PIA *pia = nullptr;

    /* Cycles per 60 Hz tick on a nominal 1 MHz machine. The CPU's cycle counts are
     * datasheet-exact, so pumping the timer off the counter gives a true 60 Hz
     * rather than an instruction-count guess. */
    static constexpr uint64_t kCyclesPerJiffy = 1000000 / 60;

    void SetUp() override
    {
        c.power_on();
        cpu = c.getCpu();
        mem = c.getMemory();
        pia = c.getPia();

        std::ifstream f("../kernel/venture.bin", std::ios::binary);
        ASSERT_TRUE(f.good()) << "venture.bin not found - build the venture_bin target";
        std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
        ASSERT_GE(blob.size(), 0x100u);
        for (size_t i = 0; i < blob.size(); ++i)
            mem->write(static_cast<uint16_t>(0x0800 + i), blob[i]);

        cpu->reg.SP = 0xFF;
        cpu->pushByte(0xFF);
        cpu->pushByte(0xFF);
        cpu->reg.PC = 0x0800;

        // Two things the DOS would have provided that jumping straight to $0800
        // does not:
        //
        // 1. Interrupts enabled. RESET leaves I set; the kernel CLIs before
        //    handing off to the shell. Without this the timer IRQ never lands.
        // 2. The 60 Hz interval timer itself. It is pulsed by the GUI
        //    (MainWindow), NOT by Computer6502::run(), so a headless harness has
        //    to drive it or jiffies() never advances -- and a jiffy-paced game
        //    then sits on its title screen forever. See run() below.
        cpu->setFlag(Computer::CPU6502::kInterrupt, false);

        // Budgets are in jiffies. Painting 660 room cells through the register
        // port takes a while: the room is not fully up until ~120 jiffies, and
        // asserting before that reads a half-drawn screen.
        run(30);            // title screen
        pressKey('\r');
        run(100);           // clear + room + HUD + first ticks
    }

    /* Advance `ticks` 60 Hz jiffies, pulsing the interval timer at each boundary
     * so the game's fixed-tick accumulator actually fires. Measured in CPU cycles,
     * so it is real time rather than a number of instructions. */
    void run(int jiffy_count)
    {
        for (int i = 0; i < jiffy_count; i++) {
            const uint64_t until = cpu->getCycles() + kCyclesPerJiffy;
            while (cpu->getCycles() < until) c.run(1);
            pia->pulseTimerIrq();
        }
    }

    void pressKey(char ch) { pia->addKeypress(ch); }

    /* Press an arrow the way the host actually does: set the control-port bit AND
     * push the ANSI sequence into the keystroke FIFO. DisplayWidget does both, so a
     * test that only sets the bit cannot see bugs caused by the escape bytes -- and
     * the first version of this game quit instantly on any arrow because it treated
     * the leading ESC as quit. */
    void pressArrow(uint8_t bit, char final_byte, int ticks)
    {
        pia->addKeypress(0x1B);
        pia->addKeypress('[');
        pia->addKeypress(final_byte);
        pia->setKeyState(bit);
        run(ticks * 4 + 2);
        pia->setKeyState(0);
        run(2);
    }

    // Hold control bits for `ticks` game ticks. TICK_RATE is 4 jiffies per tick.
    void hold(uint8_t mask, int ticks)
    {
        pia->setKeyState(mask);
        run(ticks * 4 + 2);
        pia->setKeyState(0);
        run(2);
    }

    uint8_t glyphAt(int col, int row)
    {
        return c.getVideoChip()->getCharacterAt(col, row);
    }

    uint8_t roomGlyph(int rx, int ry) { return glyphAt(kRoomX + rx, kRoomY + ry); }

    // Winky is drawn with two alternating frames, so either counts.
    bool isWinky(uint8_t g) { return g == kGlyphWinky || g == kGlyphWinky2; }

    // Scan the room for Winky; returns false if he is not on screen.
    bool findWinky(int *rx, int *ry)
    {
        for (int y = 0; y < kRoomH; y++)
            for (int x = 0; x < kRoomW; x++)
                if (isWinky(roomGlyph(x, y))) { *rx = x; *ry = y; return true; }
        return false;
    }

    int countGlyph(uint8_t g)
    {
        int n = 0;
        for (int y = 0; y < kRoomH; y++)
            for (int x = 0; x < kRoomW; x++)
                if (roomGlyph(x, y) == g) n++;
        return n;
    }

    /* Kill a serpent the way a player would. Worked out by tracing the room, and
     * the route matters:
     *
     * From the spawn corner nothing lines up. Serpents close the larger axis
     * first, so they only ever align at distance 1 -- too late to aim at, and
     * stepping toward them walks Winky into them. But climbing puts Winky on row
     * 10, which is where the third serpent spawns, and that gives a long clear
     * horizontal line to shoot along.
     *
     * So: climb, then turn to face the line and fire standing still. Facing
     * persists after the key is released, which is what makes firing without
     * moving possible. */
    bool killOneSerpent(int budget)
    {
        pia->setKeyState(kKsUp);       // climb to row 10, against the wall above
        run(14);
        pia->setKeyState(0);

        for (int i = 0; i < budget; i++) {
            int wx, wy;
            if (!findWinky(&wx, &wy)) break;      // died, or the room ended
            pia->setKeyState(kKsLeft);
            run(4);                               // one tick: turn (and advance)
            pia->setKeyState(kKsFire);
            run(6);                               // fire while standing still
            pia->setKeyState(0);
            if (countGlyph(kGlyphCorpse) >= 1) return true;
        }
        pia->setKeyState(0);
        return false;
    }

    std::string screenRow(int row)
    {
        std::string s;
        for (int col = 0; col < kScrW; col++) {
            uint8_t ch = glyphAt(col, row);
            s.push_back((ch >= 32 && ch < 127) ? static_cast<char>(ch) : ' ');
        }
        return s;
    }
};

// --- step 1: the loop runs, the room paints, Winky is on screen -------------

TEST_F(VentureTest, RoomPaintsAndWinkyIsPresent)
{
    int wx = -1, wy = -1;
    EXPECT_TRUE(findWinky(&wx, &wy)) << "Winky was never drawn";
    EXPECT_GT(countGlyph(kGlyphWall), 100) << "the room's walls did not paint";
    EXPECT_EQ(countGlyph(kGlyphApples), 1) << "the treasure should be on the floor";
    EXPECT_GE(countGlyph(kGlyphSerpent), 1) << "serpents should have spawned";
}

TEST_F(VentureTest, HudShowsScoreLivesLevel)
{
    const std::string hud = screenRow(1);
    EXPECT_NE(hud.find("SCORE"), std::string::npos);
    EXPECT_NE(hud.find("LIVES"), std::string::npos);
    EXPECT_NE(hud.find("LEVEL"), std::string::npos);
}

// --- step 1: movement comes off the control port ----------------------------

TEST_F(VentureTest, MovesOnHeldKeyFromTheControlPort)
{
    int x0, y0, x1, y1;
    ASSERT_TRUE(findWinky(&x0, &y0));

    hold(kKsLeft, 4);
    ASSERT_TRUE(findWinky(&x1, &y1)) << "Winky vanished while moving";
    EXPECT_LT(x1, x0) << "holding LEFT did not move Winky left";
    EXPECT_EQ(y1, y0) << "LEFT should not change the row";
}

TEST_F(VentureTest, MovesDiagonallyWhenTwoBitsAreHeld)
{
    int x0, y0, x1, y1;
    ASSERT_TRUE(findWinky(&x0, &y0));

    // Both bits at once: the whole reason the control port exists. The keystroke
    // FIFO could never express this -- it has no key-up and the host only
    // auto-repeats the most recent key.
    hold(kKsLeft | kKsUp, 3);
    ASSERT_TRUE(findWinky(&x1, &y1));
    EXPECT_LT(x1, x0) << "diagonal did not move left";
    EXPECT_LT(y1, y0) << "diagonal did not move up";
}

TEST_F(VentureTest, WallsBlockMovement)
{
    int x0, y0, x1, y1;
    ASSERT_TRUE(findWinky(&x0, &y0));

    // Winky starts near the bottom-right corner; drive into it well past the
    // wall and confirm he stops rather than leaving the room.
    hold(kKsDown | kKsRight, 30);
    ASSERT_TRUE(findWinky(&x1, &y1)) << "Winky left the room through a wall";
    EXPECT_LT(x1, kRoomW - 1);
    EXPECT_LT(y1, kRoomH - 1);
    EXPECT_NE(roomGlyph(x1, y1), kGlyphWall);
}

/* --- regression: arrows must not be read as commands ----------------------- */

TEST_F(VentureTest, ArrowKeysDoNotQuitTheGame)
{
    // The host sends an arrow twice over: the control-port bit, and ESC [ A into
    // the keystroke FIFO. Every arrow therefore begins with an ESC, so ESC cannot
    // mean quit -- and when it did, pressing any direction exited to the DOS
    // immediately. Press each of the four, sequence bytes included, and the game
    // must still be running with Winky on screen.
    const struct { uint8_t bit; char final_byte; } arrows[] = {
        { kKsUp, 'A' }, { kKsDown, 'B' }, { kKsRight, 'C' }, { kKsLeft, 'D' },
    };
    for (const auto &a : arrows) {
        pressArrow(a.bit, a.final_byte, 2);
        int wx, wy;
        ASSERT_TRUE(findWinky(&wx, &wy))
            << "the game exited (or died) after an arrow with final byte '"
            << a.final_byte << "'";
    }

    // ...and Q still works, so swallowing the sequence has not eaten commands.
    const std::string hud = screenRow(1);
    EXPECT_NE(hud.find("SCORE"), std::string::npos) << "still in the game";
}

// --- step 3: one arrow in flight -------------------------------------------

TEST_F(VentureTest, FiringLeavesAtMostOneArrowInFlight)
{
    // Face up (the spawn facing) and hold fire for a long time. Even holding it,
    // the original allows exactly one arrow on screen, so no frame may ever show
    // two. Sample repeatedly while the key is down.
    // Face left first: firing up from the spawn hits a wall within three cells,
    // which would make "no arrow was ever visible" look like a pass.
    pia->setKeyState(kKsLeft);
    run(4);
    pia->setKeyState(kKsFire);
    int worst = 0, sightings = 0;
    for (int i = 0; i < 16; i++) {
        run(2);
        const int arrows = countGlyph(0x18) + countGlyph(0x19) +
                           countGlyph(0x1A) + countGlyph(0x1B);
        if (arrows > worst) worst = arrows;
        if (arrows) sightings++;
    }
    pia->setKeyState(0);
    EXPECT_GE(sightings, 1) << "no arrow was ever drawn, so the limit is untested";
    EXPECT_LE(worst, 1) << "more than one arrow was in flight at once";
}

// --- steps 4-5: kills, bodies, and the scoring rule ------------------------

TEST_F(VentureTest, KillingBeforeTheTreasureScoresNothing)
{
    // The original's rule: a monster pays nothing until the treasure is in hand.
    // Kill one without having taken the apples and the score must still be zero,
    // which is what inverts the safe instinct to clear the room and then loot.
    ASSERT_TRUE(killOneSerpent(30)) << "never managed to kill a serpent";

    const std::string row = screenRow(1);
    const size_t at = row.find("SCORE");
    ASSERT_NE(at, std::string::npos) << "the room ended before we could look";
    const std::string field = row.substr(at + 5, 8);
    EXPECT_EQ(field.find_first_not_of(" 0"), std::string::npos)
        << "score must be zero before the treasure, got '" << field << "'";
}

TEST_F(VentureTest, DeadMonstersLeaveBodiesBehind)
{
    ASSERT_EQ(countGlyph(kGlyphCorpse), 0) << "bodies exist before anything died";

    // Venture's signature cruelty: the body remains, and remains lethal, so every
    // kill shrinks the room you have left to move in.
    EXPECT_TRUE(killOneSerpent(30)) << "a killed serpent left no body on the floor";
}

} // namespace
