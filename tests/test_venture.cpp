/**
 * @file test_venture.cpp
 * @brief Drives VENTURE on the emulator, and proof-reads its dungeon layouts.
 *
 * A real-time game cannot be verified by compiling it. These tests run the actual
 * 6502 blob, hold keys down through the PIA's live control port ($FE0F) the way a
 * player would, and read the screen back out of the VIC to see what happened.
 *
 * That is the only way to check the things that matter here: that eight-way
 * movement comes off the control port rather than the keystroke FIFO, that a room
 * is blind from the hall until you walk into it, that an arrow is limited to one
 * in flight, that a killed monster leaves a body which is still lethal, that a
 * Hallmonster kills on contact and cannot be shot, and that the original's scoring
 * rule holds -- monsters pay nothing until the treasure is in hand.
 *
 * The layout tests are a different kind: they read the room pictures out of
 * venture.c and flood-fill them. A one-character typo that walls a treasure off is
 * an unwinnable room, and nothing about it looks wrong until somebody plays it.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <map>
#include <vector>

#include "computer/Computer6502.h"
#include "computer/CPU6502.h"
#include "computer/Memory.h"
#include "computer/PIA.h"
#include "computer/Sid.h"
#include "computer/Cp437Font.h"
#include "computer/VIC.h"

using Computer::Computer6502;

namespace {

/* Must match venture.h.
 *
 * The playfield lives on the VIC's double-size rows, so a logical tile is NOT a
 * screen cell: logical row N is physical row kPlayRow0 + 2N, and a double row holds
 * 40 characters rather than 80. Columns are one-to-one, offset by the bezel. Every
 * test below works in logical tiles; roomGlyph()/mapGlyph() do the conversion, and
 * screenRow() is the one thing that still reads physical rows -- the HUD, the banners
 * and the roster all sit on ordinary rows outside the band. */
constexpr int kScrW = 80;
constexpr int kPlayCols = 40, kPlayRow0 = 1;
constexpr int kRoomW = 30, kRoomH = 11;
constexpr int kRoomX = (kPlayCols - kRoomW) / 2, kRoomY = 0;
constexpr int kMapW = kRoomW, kMapH = kRoomH;
constexpr int kMapX = kRoomX, kMapY = kRoomY;
constexpr int kMapStartX = 14, kMapStartY = 5;
constexpr int kMinSpawnGap = 7;
/* TICK_RATE from venture.h: jiffies per simulation tick. Every helper that wants to
 * advance the game by whole ticks has to use it -- hard-coding it was silently
 * shortening every scripted leg the moment the game's pacing was retuned. */
constexpr int kTickRate = 6;
constexpr int kMonEvery = 3;   // MON_EVERY: monsters step every Nth tick
constexpr int kLevels = 3;
constexpr int kThemes = 6, kRoomsPerLevel = 4;

constexpr uint8_t kGlyphWinky = 0x01, kGlyphWinky2 = 0x02;
constexpr uint8_t kGlyphWall = 0xDB, kGlyphCorpse = 0xB0;

/* Winky's 16 scanlines as venture.c uploads them. Duplicated here on purpose: a test
   that read the shape out of the game's own table could only prove the upload path
   round-trips, not that the intended picture arrived. */
constexpr uint8_t kWinkyArt[16] = {
    0x00, 0x3C, 0x7E, 0xFF, 0xDB, 0xDB, 0xFF, 0xFF,
    0xFF, 0xBD, 0xC3, 0xFF, 0x7E, 0x3C, 0x00, 0x00
};
constexpr uint8_t kGlyphSerpent = 0x15, kGlyphApples = 0x05;
constexpr uint8_t kGlyphRing = 0x09;   // the spider room's treasure
constexpr uint8_t kGlyphDoor = 0xFE;      // an open room entrance in the hall
/* A room in the hall is drawn with the solid-block glyph -- the same one as a wall,
 * as in the arcade. Its outline is always there; what changes when it is looted is
 * that the INTERIOR fills in with the same glyph and the entrances seal. So counting
 * "room-coloured solid blocks" measures outline before, outline+interior+entrances
 * after, and the attribute plane is the only way to tell a room from a wall. */
constexpr uint8_t kGlyphSealed = 0xDB;
constexpr uint8_t kGlyphDoorway = 0xF0;   // a doorway in a room's border
constexpr uint8_t kGlyphHallmon = 0xE8;
constexpr uint8_t kGlyphFaceL = 0x11, kGlyphFaceD = 0x1F;

// The level-1 palette (venture.c's lvl_* tables): walls dim magenta, room blocks
// bright magenta, Hallmonsters bright green.
constexpr uint8_t kL1Wall = 0x05, kL1Room = 0x45, kL1Hall = 0x42;
constexpr int kMaxHallPosts = 6, kHallBase = 3, kMaxMon = 6;

constexpr uint8_t kKsUp = 0x01, kKsDown = 0x02, kKsLeft = 0x04,
                  kKsRight = 0x08, kKsFire = 0x10;

// Voice 1 of the SID, which is what K_SOUND_TONE gates, and the kernel's mute
// flag that gates K_SOUND_TONE in turn (kernel_vars.inc).
constexpr uint16_t kSidV1FreqLo = 0xFE38, kSidV1Control = 0xFE3C;
constexpr uint8_t kSidGate = 0x01;
constexpr uint16_t kSoundEnable = 0x0029;

/* ------------------------------------------------------------------------
 * Layout proof-reading: pull the pictures back out of venture.c.
 *
 * Reading the source rather than duplicating the layouts here is the point --
 * a copy in the test would pass forever while the shipped room was broken.
 * ---------------------------------------------------------------------- */

std::string sourceText()
{
    std::ifstream f(std::string(VENTURE_SRC_DIR) + "/venture.c");
    if (!f.good()) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

// Collect the quoted row strings of the array declared at `decl`, up to the
// closing "};" -- which is the whole table for room_art, all six rooms in order.
std::vector<std::string> rowsOf(const std::string &src, const std::string &decl)
{
    std::vector<std::string> rows;
    const size_t at = src.find(decl);
    if (at == std::string::npos) return rows;
    std::istringstream in(src.substr(at + decl.size()));
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("};") != std::string::npos) break;
        const size_t a = line.find('"');
        if (a == std::string::npos) continue;
        const size_t b = line.rfind('"');
        if (b > a) rows.push_back(line.substr(a + 1, b - a - 1));
    }
    return rows;
}

// Everything walkable reachable from `start`. In the hall a room is solid whichever
// character it is drawn with, and its entrances are not in the data at all, so this
// cannot just test for '#'.
bool walkable(char c)
{
    return c == '.' || c == 'h' || c == '+' || c == 'm' || c == '*';
}

// std::abs on ints
using std::abs;

std::set<std::pair<int, int>> flood(const std::vector<std::string> &g, int w, int h,
                                    std::pair<int, int> start)
{
    std::set<std::pair<int, int>> seen{start};
    std::deque<std::pair<int, int>> q{start};
    const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
    while (!q.empty()) {
        const auto [x, y] = q.front();
        q.pop_front();
        for (int i = 0; i < 4; i++) {
            const int nx = x + dx[i], ny = y + dy[i];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            if (!walkable(g[ny][nx]) || seen.count({nx, ny})) continue;
            seen.insert({nx, ny});
            q.push_back({nx, ny});
        }
    }
    return seen;
}

// --- layout tests ---------------------------------------------------------

// The cell just inside a border doorway -- where Winky is put when he comes in.
std::pair<int, int> insideOf(int x, int y, int w, int h)
{
    if (y == 0)     return {x, 1};
    if (y == h - 1) return {x, h - 2};
    if (x == 0)     return {1, y};
    return {w - 2, y};
}

TEST(VentureLayout, EveryRoomIsWellFormedAndWinnable)
{
    const std::string src = sourceText();
    ASSERT_FALSE(src.empty()) << "could not read venture.c from " << VENTURE_SRC_DIR;

    const auto all = rowsOf(src, "room_art[THEMES][ROOM_H] = {");
    ASSERT_EQ(all.size(), static_cast<size_t>(kThemes * kRoomH))
        << "expected " << kThemes << " rooms of " << kRoomH << " rows";

    for (int r = 0; r < kThemes; r++) {
        SCOPED_TRACE("room " + std::to_string(r));
        const std::vector<std::string> g(all.begin() + r * kRoomH,
                                         all.begin() + (r + 1) * kRoomH);

        std::vector<std::pair<int, int>> doors, posts;
        std::pair<int, int> treasure{-1, -1};
        int treasures = 0;

        for (int y = 0; y < kRoomH; y++) {
            ASSERT_EQ(g[y].size(), static_cast<size_t>(kRoomW))
                << "row " << y << " is " << g[y].size() << " cells wide";
            for (int x = 0; x < kRoomW; x++) {
                const char c = g[y][x];
                const bool border = (x == 0 || x == kRoomW - 1 ||
                                     y == 0 || y == kRoomH - 1);
                if (c == '+') {
                    doors.push_back({x, y});
                    EXPECT_TRUE(border) << "doorway at " << x << "," << y
                                        << " is not in the border";
                } else if (border) {
                    EXPECT_EQ(c, '#') << "hole in the border at " << x << "," << y;
                }
                if (c == '*') { treasures++; treasure = {x, y}; }
                if (c == 'm') posts.push_back({x, y});
            }
        }

        // Two doorways, as the arcade's rooms have: one you came in by and one to
        // run for. A room with a single door is a cul-de-sac you have to fight back
        // out of, which is not how Venture plays.
        ASSERT_EQ(doors.size(), 2u) << "a room needs exactly two doorways";
        /* exits_of() reads a doorway's SIDE from its position and skips the corners,
         * so a corner doorway would be invisible to it -- and the hall would cut its
         * matching entrance somewhere else entirely. */
        for (const auto &d : doors)
            EXPECT_FALSE((d.first == 0 || d.first == kRoomW - 1) &&
                         (d.second == 0 || d.second == kRoomH - 1))
                << "doorway " << d.first << "," << d.second
                << " is in a corner -- which side is it on?";
        ASSERT_EQ(treasures, 1) << "a room needs exactly one treasure";
        EXPECT_GE(posts.size(), 2u) << "a room with nothing guarding it is not a room";
        EXPECT_LE(posts.size(), 6u) << "more monster posts than MAX_MON";

        /* Nothing may be lying in wait at a doorway. This is not a tuning knob: a
         * monster posted on or beside the cell a door puts you on kills you before you
         * have taken a step, which is a coin flip rather than a difficulty. Both
         * doorways count, because either can be the one you come in by -- and this
         * caught two real ones, a skeleton sitting exactly on the north arrival cell
         * and a serpent two tiles from it. */
        for (const auto &d : doors) {
            const auto in = insideOf(d.first, d.second, kRoomW, kRoomH);
            for (const auto &p : posts) {
                const int gap = std::abs(p.first - in.first) +
                                std::abs(p.second - in.second);
                EXPECT_GE(gap, kMinSpawnGap)
                    << "a monster starts " << gap << " tiles from the arrival cell at "
                    << in.first << "," << in.second;
            }
        }

        // Winnable from EITHER doorway, since either can be the one you enter by:
        // the treasure reachable, every monster able to reach you, and the other
        // doorway reachable so the escape route actually exists.
        for (const auto &d : doors) {
            const auto in = insideOf(d.first, d.second, kRoomW, kRoomH);
            ASSERT_NE(g[in.second][in.first], '#')
                << "doorway " << d.first << "," << d.second << " opens into a wall";
            const auto reach = flood(g, kRoomW, kRoomH, in);
            EXPECT_TRUE(reach.count(treasure))
                << "the treasure is walled off from doorway " << d.first << ","
                << d.second << " -- the room cannot be won from there";
            for (const auto &p : posts)
                EXPECT_TRUE(reach.count(p)) << "a monster is sealed in at " << p.first
                                            << "," << p.second;
            for (const auto &d2 : doors)
                EXPECT_TRUE(reach.count(insideOf(d2.first, d2.second, kRoomW, kRoomH)))
                    << "the two doorways are not connected to each other";
        }
    }
}

/* Which sides a theme's two doorways are on, read off its room art exactly as
 * exits_of() does in the game -- scanning the border and skipping the corners. */
std::string doorSidesOf(const std::vector<std::string> &room)
{
    std::string sides;
    for (int x = 1; x < kRoomW - 1; x++) if (room[0][x] == '+') sides += 'N';
    for (int x = 1; x < kRoomW - 1; x++) if (room[kRoomH - 1][x] == '+') sides += 'S';
    for (int y = 1; y < kRoomH - 1; y++) if (room[y][0] == '+') sides += 'W';
    for (int y = 1; y < kRoomH - 1; y++) if (room[y][kRoomW - 1] == '+') sides += 'E';
    return sides;
}

// Outline cells of a block that have open hall on the given side -- the candidates
// cut_notches() chooses an entrance from.
std::vector<std::pair<int, int>> exposedFaces(const std::vector<std::string> &g,
                                              char block, char side)
{
    const int dx = side == 'W' ? -1 : side == 'E' ? 1 : 0;
    const int dy = side == 'N' ? -1 : side == 'S' ? 1 : 0;
    std::vector<std::pair<int, int>> out;
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++) {
            if (g[y][x] != block) continue;
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
            if (g[ny][nx] == '.') out.push_back({x, y});
        }
    return out;
}

TEST(VentureLayout, EveryHallIsWellFormedAndNoRoomCanStrandAnother)
{
    const std::string src = sourceText();
    ASSERT_FALSE(src.empty());

    const auto rooms = rowsOf(src, "room_art[THEMES][ROOM_H] = {");
    ASSERT_EQ(rooms.size(), static_cast<size_t>(kThemes * kRoomH));

    /* A slot's entrances are cut on the sides of whichever room it is holding, and
     * slot i of level L holds theme ((L-1)*4 + i) % 6. So which faces each hall has to
     * expose is decided by the room art, and the two have to be checked together --
     * a hall that is fine for level 1 can leave a level-3 room with no way in. */
    std::string sides[kThemes];
    for (int t = 0; t < kThemes; t++)
        sides[t] = doorSidesOf(std::vector<std::string>(rooms.begin() + t * kRoomH,
                                                        rooms.begin() + (t + 1) * kRoomH));

    const auto all = rowsOf(src, "map_layout[LEVELS][MAP_H] = {");
    ASSERT_EQ(all.size(), static_cast<size_t>(kLevels * kMapH))
        << "expected " << kLevels << " halls of " << kMapH << " rows";

    const char outline[] = "ABCD", inside[] = "abcd";

    for (int lvl = 0; lvl < kLevels; lvl++) {
        SCOPED_TRACE("hall " + std::to_string(lvl + 1));
        const std::vector<std::string> g(all.begin() + lvl * kMapH,
                                         all.begin() + (lvl + 1) * kMapH);

        for (int y = 0; y < kMapH; y++)
            ASSERT_EQ(g[y].size(), static_cast<size_t>(kMapW)) << "row " << y;
        for (int x = 0; x < kMapW; x++) {
            EXPECT_EQ(g[0][x], '#') << "leaks at the top, column " << x;
            EXPECT_EQ(g[kMapH - 1][x], '#') << "leaks at the bottom, column " << x;
        }
        for (int y = 0; y < kMapH; y++) {
            EXPECT_EQ(g[y][0], '#');
            EXPECT_EQ(g[y][kMapW - 1], '#');
        }
        ASSERT_EQ(g[kMapStartY][kMapStartX], '.') << "Winky would start inside a wall";

        const auto reach = flood(g, kMapW, kMapH, {kMapStartX, kMapStartY});
        int posts = 0;
        for (int y = 0; y < kMapH; y++)
            for (int x = 0; x < kMapW; x++)
                if (g[y][x] == 'h') {
                    posts++;
                    EXPECT_TRUE(reach.count({x, y})) << "a Hallmonster is sealed off";
                }
        EXPECT_EQ(posts, 6) << "one post per possible Hallmonster";

        for (int i = 0; i < kRoomsPerLevel; i++) {
            const std::string want = sides[((lvl * kRoomsPerLevel) + i) % kThemes];
            ASSERT_EQ(want.size(), 2u);

            std::vector<std::pair<int, int>> ring, interior;
            for (int y = 0; y < kMapH; y++)
                for (int x = 0; x < kMapW; x++) {
                    if (g[y][x] == outline[i]) ring.push_back({x, y});
                    if (g[y][x] == inside[i]) interior.push_back({x, y});
                }
            ASSERT_FALSE(ring.empty()) << "room " << i << " has no outline";
            EXPECT_FALSE(interior.empty()) << "room " << i << " has nothing to fill in";

            // The interior must be sealed inside its own ring, or looting the room
            // would paint unwalkable black cells out in the open hall.
            for (const auto &c : interior)
                for (const auto &d : {std::pair{1, 0}, std::pair{-1, 0},
                                      std::pair{0, 1}, std::pair{0, -1}}) {
                    const char n = g[c.second + d.second][c.first + d.first];
                    EXPECT_TRUE(n == outline[i] || n == inside[i])
                        << "room " << i << " interior leaks into '" << n << "'";
                }

            for (const char side : want) {
                const auto faces = exposedFaces(g, outline[i], side);
                EXPECT_FALSE(faces.empty())
                    << "room " << i << " has no " << side
                    << " face to cut an entrance into, so it cannot be entered";
                bool reachable = false;
                for (const auto &f : faces)
                    for (const auto &d : {std::pair{1, 0}, std::pair{-1, 0},
                                          std::pair{0, 1}, std::pair{0, -1}})
                        if (reach.count({f.first + d.first, f.second + d.second}))
                            reachable = true;
                EXPECT_TRUE(reachable) << "room " << i << "'s " << side
                                       << " entrance would be unreachable";
            }

            /* Looting a room seals it solid, which changes the shape of the hall.
             * That must never wall off another room: it would leave a level
             * impossible to finish through no fault of the player. */
            std::vector<std::string> sealed = g;
            for (const auto &c : ring)     sealed[c.second][c.first] = '#';
            for (const auto &c : interior) sealed[c.second][c.first] = '#';
            const auto after = flood(sealed, kMapW, kMapH, {kMapStartX, kMapStartY});
            for (int j = 0; j < kRoomsPerLevel; j++) {
                if (j == i) continue;
                for (const char side : sides[((lvl * kRoomsPerLevel) + j) % kThemes]) {
                    bool ok = false;
                    for (const auto &f : exposedFaces(g, outline[j], side))
                        for (const auto &d : {std::pair{1, 0}, std::pair{-1, 0},
                                              std::pair{0, 1}, std::pair{0, -1}})
                            if (after.count({f.first + d.first, f.second + d.second}))
                                ok = true;
                    EXPECT_TRUE(ok) << "sealing room " << i << " strands room " << j
                                    << "'s " << side << " entrance";
                }
            }
        }
    }
}

/* ------------------------------------------------------------------------
 * Playing the game.
 * ---------------------------------------------------------------------- */

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
    /* One jiffy of the MACHINE's time, taken from its own clock rather than assumed.
     *
     * This was hardcoded to a 1MHz machine while the GUI ran 1000 instructions per
     * millisecond -- about 3.5MHz, now stated as 4 -- so every timing-sensitive test
     * measured something a quarter the speed of the thing being shipped. That cost a
     * misdiagnosed performance "bug", a wrongly withdrawn clock figure, and an arrow
     * that appeared to stutter when it did not. Derived, so it cannot drift again. */
    static constexpr uint64_t kCyclesPerJiffy =
        Computer::Computer6502::kDefaultClockHz / Computer::Computer6502::kJiffyHz;

    /* How far into the game's opening screens to boot. Budgets are in jiffies:
     * painting 840 hall cells through the register port takes the better part of
     * 150 of them, and asserting before that reads a half-drawn screen. */
    enum BootStop { kRoster, kHall };
    BootStop stop_at = kHall;

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

        // 3. SOUND_ENABLE ($29). The kernel sets it during boot; without it every
        //    K_SOUND_TONE takes the muted path and the game is silent for reasons
        //    that have nothing to do with the game.
        mem->write(kSoundEnable, 0x01);

        bootTo(stop_at);
    }

    void bootTo(BootStop stop)
    {
        run(30);                     // V E N T U R E
        pressKey('\r');
        run(20);                     // the treasure roster / GET READY
        if (stop == kRoster) return;
        pressKey('\r');
        run(170);                    // clear + hall + HUD + first ticks
    }

    /* Advance `jiffy_count` 60 Hz jiffies, pulsing the interval timer at each
     * boundary so the game's fixed-tick accumulator actually fires. Measured in CPU
     * cycles, so it is real time rather than a number of instructions. */
    void run(int jiffy_count)
    {
        for (int i = 0; i < jiffy_count; i++) {
            const uint64_t until = cpu->getCycles() + kCyclesPerJiffy;
            while (cpu->getCycles() < until) c.runInstructions(1);
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
        run(ticks * kTickRate + 2);
        pia->setKeyState(0);
        run(2);
    }

    /* Hold `mask` until Winky has actually moved `cells` cells, or stops moving.
     *
     * Counting ticks cannot be trusted to the cell. The game's fixed-tick accumulator
     * and this harness's jiffy budget drift by a tick depending on phase, so a leg
     * asked for in ticks lands one cell short often enough to matter -- and a route
     * that stops one cell short of its treasure walks the whole way back without it,
     * which is exactly the bug this replaced. Reading the screen is exact. */
    /* Hold `mask` until Winky has moved `cells` cells, or until a wall stops him.
     *
     * Both halves matter. Counting ticks cannot be trusted to the cell -- the game's
     * accumulator and this harness's jiffy budget drift by a tick depending on phase,
     * so a leg asked for in ticks lands one short and walks past its target. And a leg
     * that clamps early must return early: standing on the spot with the key still
     * down is how Winky used to get eaten in a corner, since a Hallmonster is closing
     * the whole time. */
    void walk(uint8_t mask, int cells, bool on_map)
    {
        int sx, sy;
        if (!(on_map ? findWinkyOnMap(&sx, &sy) : findWinky(&sx, &sy))) return;
        int px = sx, py = sy, stalled = 0;
        pia->setKeyState(mask);
        for (int i = 0; i < cells * 3 + 8; i++) {
            run(kTickRate);
            int x, y;
            if (!(on_map ? findWinkyOnMap(&x, &y) : findWinky(&x, &y))) break;
            if (abs(x - sx) + abs(y - sy) >= cells) break;
            if (x == px && y == py) { if (++stalled >= 3) break; }
            else { stalled = 0; px = x; py = y; }
        }
        pia->setKeyState(0);
        run(2);
    }

    void walkInRoom(uint8_t mask, int cells) { walk(mask, cells, false); }

    /* Walk to an absolute hall cell rather than a number of cells.
     *
     * The routes used to say "west nine", which is only the same thing when you start
     * where you expect to. A death puts Winky back somewhere else, so every retry
     * after the first walked nine cells from the wrong place and stopped two short of
     * the door -- which read as "the route is impassable" when it was just relative.
     * Now that the harness can read his coordinates, aim at the cell. */
    void walkOnMapTo(int tx, int ty)
    {
        for (int axis = 0; axis < 2; axis++) {
            int x, y;
            if (!findWinkyOnMap(&x, &y)) return;
            const int want = axis ? ty : tx;
            const int have = axis ? y : x;
            if (have == want) continue;
            const uint8_t mask = axis ? (have > want ? kKsUp : kKsDown)
                                      : (have > want ? kKsLeft : kKsRight);
            pia->setKeyState(mask);
            for (int i = 0; i < 80; i++) {
                run(kTickRate);
                if (!findWinkyOnMap(&x, &y)) break;
                if ((axis ? y : x) == want) break;
            }
            pia->setKeyState(0);
            run(2);
        }
    }
    void walkOnMap(uint8_t mask, int cells)  { walk(mask, cells, true); }

    // Hold control bits for `ticks` game ticks. TICK_RATE is 4 jiffies per tick.
    void hold(uint8_t mask, int ticks)
    {
        pia->setKeyState(mask);
        run(ticks * kTickRate + 2);
        pia->setKeyState(0);
        run(2);
    }

    uint8_t glyphAt(int col, int row)
    {
        return c.getVideoChip()->getCharacterAt(col, row);
    }

    uint8_t attrAt(int col, int row)
    {
        return c.getVideoChip()->getColorAt(col, row);
    }

    // The 16 scanlines the renderer would actually draw for a code, from whichever
    // font the chip is currently reading.
    std::vector<uint8_t> liveGlyph(uint8_t code)
    {
        const uint8_t *r = c.getVideoChip()->glyphRows(code);
        return std::vector<uint8_t>(r, r + 16);
    }

    static std::vector<uint8_t> romGlyph(uint8_t code)
    {
        return std::vector<uint8_t>(&Computer::kCp437Font[code * 16],
                                    &Computer::kCp437Font[code * 16] + 16);
    }

    // A sealed room and a plain hall wall share the solid-block glyph -- as they do
    // in the arcade -- so telling them apart needs the attribute plane.
    int countOnMapWithAttr(uint8_t g, uint8_t attr)
    {
        int n = 0;
        for (int y = 0; y < kMapH; y++)
            for (int x = 0; x < kMapW; x++)
                if (glyphAt(kMapX + x, physRow(kMapY + y)) == g &&
                    attrAt(kMapX + x, physRow(kMapY + y)) == attr) n++;
        return n;
    }

    // Logical tile -> physical cell. The row between two logical rows is the bottom
    // half of the glyphs above it and is never addressed.
    int physRow(int ry) const { return kPlayRow0 + ry * 2; }
    uint8_t roomGlyph(int rx, int ry) { return glyphAt(kRoomX + rx, physRow(kRoomY + ry)); }
    uint8_t mapGlyph(int rx, int ry) { return glyphAt(kMapX + rx, physRow(kMapY + ry)); }

    // Winky is drawn with two alternating frames, so either counts.
    bool isWinky(uint8_t g) { return g == kGlyphWinky || g == kGlyphWinky2; }

    /* --- the game's own state, read by address ---------------------------
     *
     * cl65 emits a label file; dropping `static` from the mover variables puts them
     * in it, at no cost to the binary. These read them straight out of RAM.
     *
     * This replaces scanning the screen for glyphs, which stopped being decidable
     * once the movers began sliding between cells: a sprite in flight is drawn
     * between two of them, and position does not carry direction -- three sixteenths
     * right and thirteen sixteenths left land on the same pixel. Asking the game
     * where something is was always the better question; interpolation only made the
     * old answer impossible. Tests about what is DRAWN still read the screen. */
    static const std::map<std::string, uint16_t> &labels()
    {
        static std::map<std::string, uint16_t> m = [] {
            std::map<std::string, uint16_t> out;
            std::ifstream f("../kernel/venture.lbl");
            std::string kind, addr, name;
            while (f >> kind >> addr >> name) {
                if (kind != "al" || name.size() < 2) continue;
                out[name.substr(1)] = static_cast<uint16_t>(std::stoul(addr, nullptr, 16));
            }
            return out;
        }();
        return m;
    }

    uint16_t sym(const char *name)
    {
        auto it = labels().find(name);
        EXPECT_NE(it, labels().end()) << "no label " << name
                                      << " -- is venture.lbl current?";
        return it == labels().end() ? 0 : it->second;
    }

    uint8_t peek(const char *name, int index = 0)
    {
        return c.getMemory()->read(static_cast<uint16_t>(sym(name) + index));
    }

    struct Ent { int x, y; };

    /* Winky's coordinates are board-relative, so they mean a room cell in a room and
       a hall cell in the hall. Returning false when he is not in the region asked
       about keeps the old "died, or the room ended" signal the walking helpers stop
       on -- now exact, rather than "his glyph was not on the screen this frame". */
    bool findWinkyIn(bool room, int *rx, int *ry)
    {
        if (inRoom() != room) return false;
        *rx = peek("_wx"); *ry = peek("_wy");
        return true;
    }
    bool findWinky(int *rx, int *ry)      { return findWinkyIn(true,  rx, ry); }
    bool findWinkyOnMap(int *rx, int *ry) { return findWinkyIn(false, rx, ry); }

    std::vector<Ent> liveMonsters()
    {
        std::vector<Ent> v;
        for (int i = 0; i < kMaxMon; i++)
            if (peek("_m_live", i)) v.push_back({peek("_m_x", i), peek("_m_y", i)});
        return v;
    }

    std::vector<Ent> liveHallmonsters()
    {
        std::vector<Ent> v;
        for (int i = 0; i < kMaxHallPosts; i++)
            if (peek("_h_live", i)) v.push_back({peek("_h_x", i), peek("_h_y", i)});
        return v;
    }

    bool arrowInFlight() { return peek("_a_live") != 0; }
    bool inRoom()        { return peek("_mode") != 0; }
    bool pipAt(int *px, int *py)
    {
        if (!peek("_f_live")) return false;
        *px = peek("_f_x"); *py = peek("_f_y");
        return true;
    }

    int countGlyph(uint8_t g)
    {
        int n = 0;
        for (int y = 0; y < kRoomH; y++)
            for (int x = 0; x < kRoomW; x++)
                if (roomGlyph(x, y) == g) n++;
        return n;
    }

    int countGlyphOnMap(uint8_t g)
    {
        int n = 0;
        for (int y = 0; y < kMapH; y++)
            for (int x = 0; x < kMapW; x++)
                if (mapGlyph(x, y) == g) n++;
        return n;
    }

    /* Hold a direction until Winky stops moving, so a route does not depend on
     * counting ticks. Walls clamp him, which makes each leg exact -- and leaving
     * the keys down no longer than needed matters, because Hallmonsters are closing
     * the whole time. */
    void slideOnMap(uint8_t mask, int limit = 60)
    {
        int px = -1, py = -1, still = 0;
        pia->setKeyState(mask);
        for (int i = 0; i < limit; i++) {
            run(kTickRate);
            int x, y;
            if (!findWinkyOnMap(&x, &y)) break;
            if (x == px && y == py) { if (++still >= 2) break; }
            else { still = 0; px = x; py = y; }
        }
        pia->setKeyState(0);
        run(2);
    }

    bool hudUp() { return screenRow(0).find("LEVEL") != std::string::npos; }
    bool caught() { return screenRow(11).find("CAUGHT") != std::string::npos; }

    /* Wait until the hall is back up and fully drawn, and detect it by the TRANSITION
     * rather than by the end state.
     *
     * Every screen in this game ends with the HUD -- draw_hud() runs after the whole
     * board -- so "the HUD is up" means "that repaint finished". But it is also still up
     * from the room we just left, and the map region on screen OVERLAPS the room region,
     * so counting walls or blocks cannot tell the two apart either. Waiting for the HUD
     * to go (the clear) and then come back is unambiguous. */
    bool waitForHall()
    {
        bool cleared = false;
        for (int i = 0; i < 140; i++) {
            run(5);
            if (caught()) { pressKey('\r'); run(240); return false; }
            if (!cleared) { cleared = !hudUp(); continue; }
            if (hudUp()) {
                run(20);      // Winky and the Hallmonsters are drawn by step(),
                return true;  // not by the board pass
            }
        }
        return false;
    }

    /* Walk from the middle of the hall into a room, by one of its two entrances.
     *
     * Level 1's slot 0 holds the serpent room, whose doorways are north and south, so
     * its entrances are cut into the top and bottom faces of its block -- (5,2) and
     * (5,4), approached from row 1 and row 5 respectively.
     *
     * `north` picks which. Coming in the north entrance puts Winky at the room's north
     * doorway, the south one at its south doorway. That is the point of the test above.
     *
     * Corner first, then count: this gets called again after a death, when Winky is
     * back beside whichever entrance he last used, so two wall-clamped legs put him
     * somewhere known from anywhere.
     *
     * `ready` is the treasure glyph, which paints near the end of the board -- polling
     * for it is how we know the room is up, and waiting no longer than that matters
     * because every extra jiffy is one Winky spends in the doorway. */
    bool enterSlotZero(bool north, uint8_t ready = kGlyphApples)
    {
        if (north) {
            walkOnMapTo(14, 1);          // north to row 1, which is open all the way
            walkOnMapTo(5, 1);           // west to above the entrance
            /* One tick, one cell. This used to ask for two, because at the harness's
               old quarter-speed clock a hold of two ticks produced about one cell of
               movement -- the fixtures had been calibrated against the wrong clock
               without anyone deciding to. */
            hold(kKsDown, 1);            // down onto it at (5,2)
        } else {
            /* Slot 2's south entrance at (7,8), reached along row 9.
             *
             * NOT room 0's, which is what this used to do. Its south entrance is at
             * the far end of the row-5 corridor, and with three Hallmonsters awake
             * level one posts one at (1,3) -- the west mouth of that corridor. Winky
             * reaches the cell below the door and is caught standing on it. Row 9 is
             * the quiet lane: the nearest woken post is seventeen cells east. */
            walkOnMapTo(14, 9);
            walkOnMapTo(7, 9);
            hold(kKsUp, 1);              // up onto the entrance at (7,8)
        }
        /* Entered is not the same as drawn. enter_room() flips the mode and then
           paints the whole board inside the same step, so a check that fires the
           moment the mode changes samples a half-painted room -- the bottom rows are
           simply not there yet, and a doorway count comes up short. Reading the
           game's state made this visible: the old screen-scan could not report
           "entered" until Winky's glyph had been drawn, which happens last, so it
           waited for the paint by accident. */
        for (int i = 0; i < 60; i++) {
            run(5);
            if (!inRoom()) continue;
            run(20);                    // let the board finish painting
            return countGlyph(ready) == 1;
        }
        if (caught()) { pressKey('\r'); run(240); }
        return false;
    }

    // Three lives, so three goes at getting in. The way to slot 0 runs along the top
    // of the hall, which is also where the first Hallmonster is patrolling.
    bool enterSlot(bool north, uint8_t ready = kGlyphApples)
    {
        for (int attempt = 0; attempt < 3; attempt++)
            if (enterSlotZero(north, ready)) return true;
        return false;
    }

    bool enterRoomZero() { return enterSlot(true); }

    /* Loot a room and get out by a far doorway -- the whole loop the game is built
     * round, and what every test of the hall's after-state needs.
     *
     * Slot 2 rather than slot 0. Room 0 is the hook: its treasure sits behind an L of
     * wall, so looting it means crossing the room twice, about 160 ticks with three
     * serpents hunting -- and the moment monsters learnt to walk round their own dead
     * instead of stalling next to them, that route stopped surviving. Slot 2 is the
     * spider room, whose pillars leave row 1 and column 21 clear: in at the north
     * doorway, east, down over the ring, and back out the way we came. Under 40 ticks.
     *
     * It can still fail -- three spiders are hunting -- so the caller retries.
     * Returns false with the CAUGHT screen already acknowledged. */
    bool lootSpiderRoom()
    {
        /* Slot 2 holds the spider room at level 1. Its block is the long one on the
         * bottom left, so the south entrance is at (7,8), reached from row 9 -- the
         * quietest lane in the hall. */
        walkOnMap(kKsDown, 20);     // clamps on row 9, the quietest lane in the hall
        walkOnMap(kKsLeft, 7);      // west to (7,9), below the entrance
        hold(kKsUp, 1);             // up onto it at (7,8)

        int x, y;
        bool inside = false;
        for (int i = 0; i < 60 && !inside; i++) {
            run(5);
            inside = countGlyph(kGlyphRing) == 1 && findWinky(&x, &y);
        }
        // Acknowledge a death here too, or the game sits on CAUGHT waiting for a key
        // and every retry runs against a stopped machine.
        if (!inside) { if (caught()) { pressKey('\r'); run(240); } return false; }

        /* In at the south doorway (20,10), so Winky starts at (20,9); out by the north
         * one at (10,0). Two different doors on purpose -- it exercises the side
         * matching in both directions.
         *
         * Up column 20 and along row 1 rather than straight west along row 9: a spider
         * posts at (14,8), which is exactly seven tiles from the arrival cell and so
         * legal, but walking the bottom lane goes straight at it. Columns 20 and 14 and
         * rows 1 and 4 are all clear of the web's pillars. */
        walkInRoom(kKsUp, 8);       // (20,1)
        walkInRoom(kKsLeft, 6);     // (14,1)
        walkInRoom(kKsDown, 3);     // (14,4), the ring
        walkInRoom(kKsUp, 3);       // back to (14,1)
        walkInRoom(kKsLeft, 4);     // (10,1), under the north doorway
        hold(kKsUp, 2);             // and out
        return waitForHall();
    }

    // Three lives, so three attempts.
    bool lootARoomWithRetries()
    {
        for (int attempt = 0; attempt < 3; attempt++)
            if (lootSpiderRoom()) return true;
        return false;
    }

    /* Kill a serpent the way a player would, because there is no scripted route
     * that survives: the serpents converge from three posts and the geometry that
     * lines one up depends on where they have got to.
     *
     * Each pass finds the nearest serpent. Not lined up: close the SHORTER axis,
     * which moves across the gap rather than into it. Lined up: fire down the line.
     * Too close to shoot safely: back off. Returns the number of bodies on the
     * floor, stopping early once `want` of them are down. */
    int huntSerpents(int budget, int want)
    {
        for (int i = 0; i < budget; i++) {
            if (countGlyph(kGlyphCorpse) >= want) return want;
            int wx, wy;
            if (!findWinky(&wx, &wy)) break;           // died, or the room ended

            int bx = -1, by = -1, best = 9999;
            for (const Ent &m : liveMonsters()) {
                const int d = abs(m.x - wx) + abs(m.y - wy);
                if (d < best) { best = d; bx = m.x; by = m.y; }
            }
            if (bx < 0) { run(kTickRate); continue; }   // all dead

            const int dx = bx - wx, dy = by - wy;
            const int dist = abs(dx) + abs(dy);
            uint8_t dir;
            bool shoot = false;

            if (dy == 0 && abs(dx) >= 2)      { dir = dx > 0 ? kKsRight : kKsLeft; shoot = true; }
            else if (dx == 0 && abs(dy) >= 2) { dir = dy > 0 ? kKsDown : kKsUp;    shoot = true; }
            else if (dist <= 2)               { dir = dx > 0 ? kKsLeft : kKsRight; }
            else if (abs(dx) <= abs(dy))      { dir = dx > 0 ? kKsRight : kKsLeft; }
            else                              { dir = dy > 0 ? kKsDown : kKsUp; }

            /* Aim and fire on the SAME tick. keystate() is sampled once per tick and
             * winky_move() sets facing before fire() reads it, so the arrow leaves on
             * the tick the direction goes down, and travels two cells while Winky
             * travels one. Turning first and firing second -- the obvious way -- walks
             * him into whatever he is aiming at whenever it is close, which is how
             * this test kept dying. */
            pia->setKeyState(shoot ? (uint8_t)(dir | kKsFire) : dir);
            run(kTickRate);
            pia->setKeyState(0);
            run(shoot ? kTickRate * 2 : 2);   // let the arrow finish its flight
        }
        return countGlyph(kGlyphCorpse);
    }

    bool killOneSerpent(int budget) { return huntSerpents(budget, 1) >= 1; }

    /* Get caught by a Hallmonster. Winky starts in the middle corridor, which they
     * can only reach the long way round, so step out to the west wall first and
     * then stand still: they close on you wherever you are, at a quarter of your
     * speed, and there is nothing you can do about it. That is the contract.
     *
     * Death banners CAUGHT and waits for a key, so that is the signal, and in the
     * hall it is unambiguous -- nothing else out there can kill you. */
    bool waitToBeCaughtInTheHall()
    {
        slideOnMap(kKsLeft);
        for (int i = 0; i < 220; i++) {
            run(kTickRate);
            if (screenRow(11).find("CAUGHT") != std::string::npos) return true;
        }
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

// --- step 6: the hall ------------------------------------------------------

/* The dungeon draws itself with its own glyphs, not the ones the character ROM
 * happened to have.
 *
 * The monsters used to be whatever CP437 code looked closest -- a section sign for a
 * serpent, a theta for a cyclops -- because a fixed font is as far as you can go.
 * These shapes are drawn instead of found. The CODES did not change, which is why
 * every other test in this file needed no edit. */
TEST_F(VentureTest, TheDungeonUsesItsOwnGlyphsAndNotTheCharacterRom)
{
    EXPECT_NE(liveGlyph(kGlyphWinky), romGlyph(kGlyphWinky))
        << "Winky is still the ROM's smiley";
    EXPECT_EQ(liveGlyph(kGlyphWinky), std::vector<uint8_t>(kWinkyArt, kWinkyArt + 16))
        << "Winky is not the shape venture.c uploads";

    // Seeded from the ROM, so anything the game does not redefine is untouched --
    // the HUD text renders exactly as it did.
    EXPECT_EQ(liveGlyph('S'), romGlyph('S')) << "an unredefined glyph must be the ROM's";
    EXPECT_EQ(liveGlyph(kGlyphWall), romGlyph(kGlyphWall))
        << "the wall block is deliberately left as the ROM's solid block";
}

/* NOT TESTED: that quitting restores the ROM font. venture.c does restore it, but a
 * test cannot tell -- DOS's warm start clears the screen on re-entry and a clear
 * resets the font anyway, so the assertion passes with the restore removed. Verified
 * by deleting the call and watching the test still pass, which is why there is no
 * test here rather than a green one that checks nothing. */

/* Hallmonsters must never stack. They all patrol the same hall and drift toward the
 * same player, so without a rule against it they pile into one square and draw as a
 * single figure -- the hall then looks emptier than it is, which matters when its
 * filling up is the level's clock. Room monsters already refused to share a cell;
 * Hallmonsters were never given the same rule.
 *
 * Checked in PIXELS rather than cells: overlapping is what you actually see, and a
 * sprite mid-slide is between cells, so inferring a cell from its position would
 * report overlaps that are not there. */
TEST_F(VentureTest, HallmonstersNeverOverlap)
{
    /* Three seconds, not ten. At the machine's real clock the Hallmonsters cross the
       hall at their intended speed, so a Winky who stands still for ten of them is
       dead well before the window closes -- and then there is nothing left to sample.
       They start at least fourteen cells off and cover about two and a half a second,
       so three seconds watches them move without getting him killed. */
    int worst = 0, sampled = 0;
    for (int t = 0; t < 90; t++) {
        run(2);
        std::vector<std::pair<int, int>> at;
        const Computer::VIC *vic = c.getVideoChip();
        for (uint8_t i = 0; i < Computer::VIC::kSpriteCount; ++i) {
            const Computer::VIC::Sprite &sp = vic->sprite(i);
            if (sp.enabled && sp.glyph == kGlyphHallmon) at.push_back({sp.x, sp.y});
        }
        if (at.size() < 2) continue;
        sampled++;
        for (size_t a = 0; a < at.size(); ++a)
            for (size_t b = a + 1; b < at.size(); ++b) {
                // a magnified sprite is 16 nominal px wide and 32 tall
                if (abs(at[a].first - at[b].first) < 16 &&
                    abs(at[a].second - at[b].second) < 32) worst++;
            }
    }
    ASSERT_GT(sampled, 40) << "never saw two Hallmonsters at once";
    EXPECT_EQ(worst, 0) << worst << " frames had two Hallmonsters drawn on top of each other";
}

/* Room monsters guard a patch of floor; they do not hunt you across the room.
 *
 * They used to walk straight at you from wherever they were, so backing away pulled
 * the whole set after you in a line and the way to clear a room was to reverse down a
 * corridor shooting. The arcade's work a patch, dart at you when you come inside it,
 * and drift back when you leave -- the room becomes a set of dangerous places rather
 * than a pack that follows you, and their bodies end up on the squares worth holding.
 *
 * Asserted on the SUM of the distances, which is what tells the two apart: hunters
 * all converge on you and the sum collapses towards nothing, guards stay spread out
 * wherever they are. */
TEST_F(VentureTest, MonstersGuardTheirGroundUntilYouComeClose)
{
    ASSERT_TRUE(enterRoomZero());
    int wx, wy;
    ASSERT_TRUE(findWinky(&wx, &wy));

    int worst = 9999;                      // the tightest they ever drew in
    for (int t = 0; t < 8; t++) {          // well short of the Hallmonster's cue
        run(30);
        ASSERT_TRUE(findWinky(&wx, &wy)) << "died standing still, well clear of them";
        int sum = 0;
        for (const Ent &m : liveMonsters()) sum += abs(m.x - wx) + abs(m.y - wy);
        if (sum < worst) worst = sum;
    }
    EXPECT_GT(worst, 15) << "they closed on a stationary player from across the room";

    // ...but walk into one's patch and it comes for you.
    int nearest = 99;
    for (int t = 0; t < 8; t++) {
        walkInRoom(kKsRight, 1);
        if (!findWinky(&wx, &wy)) return;  // caught: it certainly came
        for (const Ent &m : liveMonsters())
            nearest = std::min(nearest, abs(m.x - wx) + abs(m.y - wy));
    }
    EXPECT_LE(nearest, 3) << "walked into a monster's patch and it ignored me";
}

/* What a frame of sprite drawing costs, inclusive of everything it calls.
 *
 * A budget, not a benchmark: the point is that adding to the draw path shows up here
 * rather than as stutter no one can attribute. Measured 9,919 cycles before the write
 * path was flattened and 7,726 after; at 60 frames a second and a 4MHz clock, 7,726 is
 * about 11% of the machine. The ceiling leaves room to breathe without leaving room to
 * put the multiply and the six-argument call back. */
TEST_F(VentureTest, AFrameOfDrawingStaysWithinItsBudget)
{
    const uint16_t entry = sym("_draw_movers");
    ASSERT_TRUE(enterRoomZero());

    uint64_t total = 0, calls = 0, nextj = cpu->getCycles() + kCyclesPerJiffy;
    for (int i = 0; i < 2000000 && calls < 200; ++i) {
        const bool at_entry = (cpu->reg.PC == entry);
        const uint8_t sp0 = cpu->reg.SP;
        const uint64_t c0 = cpu->getCycles();
        if (!cpu->executeSingleInstruction()) break;
        if (cpu->getCycles() >= nextj) { nextj += kCyclesPerJiffy; pia->pulseTimerIrq(); }
        if (!at_entry) continue;
        for (int g = 0; g < 200000; ++g) {          // run to the matching RTS
            if (cpu->reg.SP > sp0) break;
            if (!cpu->executeSingleInstruction()) break;
            if (cpu->getCycles() >= nextj) { nextj += kCyclesPerJiffy; pia->pulseTimerIrq(); }
        }
        total += cpu->getCycles() - c0;
        calls++;
    }
    ASSERT_GT(calls, 100u) << "never saw enough frames to measure";
    const uint64_t per = total / calls;
    fprintf(stderr, "[ cost     ] draw_movers: %llu cycles/frame\n",
            (unsigned long long)per);
    EXPECT_LT(per, 9000u) << "a frame of drawing costs " << per << " cycles";
}

/* What a simulation tick costs, inclusive of everything it calls.
 *
 * The companion to the frame budget, and the two together are the machine's real
 * load: ten ticks and sixty frames a second is 10*24,042 + 60*7,726 = about 704,000
 * cycles, a shade over 0.7MHz of productive work. At the 4MHz clock that is 18% of
 * the machine; the rest is absorbed by the main loop's busy-wait, which is what a
 * game loop is for.
 *
 * Worth knowing which half is which: a tick costs three times a frame, so the AI is
 * not free -- it only looked free in a profile, where its cost is spread across
 * chase(), chase_blocked() and monsters_advance() and no single symbol stands out. */
TEST_F(VentureTest, ATickStaysWithinItsBudget)
{
    const uint16_t entry = sym("_step");
    ASSERT_TRUE(enterRoomZero());

    uint64_t total = 0, calls = 0, nextj = cpu->getCycles() + kCyclesPerJiffy;
    for (int i = 0; i < 16000000 && calls < 100; ++i) {
        const bool at = (cpu->reg.PC == entry);
        const uint8_t sp0 = cpu->reg.SP;
        const uint64_t c0 = cpu->getCycles();
        if (!cpu->executeSingleInstruction()) break;
        if (cpu->getCycles() >= nextj) { nextj += kCyclesPerJiffy; pia->pulseTimerIrq(); }
        if (!at) continue;
        for (int g = 0; g < 400000; ++g) {          // run to the matching RTS
            if (cpu->reg.SP > sp0) break;
            if (!cpu->executeSingleInstruction()) break;
            if (cpu->getCycles() >= nextj) { nextj += kCyclesPerJiffy; pia->pulseTimerIrq(); }
        }
        total += cpu->getCycles() - c0;
        calls++;
    }
    ASSERT_GT(calls, 50u) << "never saw enough ticks to measure";
    const uint64_t per = total / calls;
    fprintf(stderr, "[ cost     ] step: %llu cycles/tick\n", (unsigned long long)per);
    EXPECT_LT(per, 30000u) << "a tick costs " << per << " cycles";
}

/* A shot flies; it does not strobe across the room.
 *
 * An arrow covers ARROW_STEP cells a tick -- it is meant to outrun what it is shot at
 * -- and spr_launch() treated any step longer than one cell as a teleport and landed
 * it. So a shot jumped 32 pixels ten times a second instead of moving, which is what
 * "the shot motion is choppy" turned out to mean.
 *
 * Sampled a frame at a time. When the harness ran a quarter of the machine's clock a
 * tick stretched while the glide still finished in its allotted frames, so the arrow
 * appeared to hold for several frames -- an artifact of the instrument, not of the
 * game, and one I nearly took for the bug. The harness runs the real clock now. */
TEST_F(VentureTest, AShotFliesRatherThanJumps)
{
    ASSERT_TRUE(enterRoomZero());
    hold(kKsRight, 1);                     // face east: room 0's row 1 is clear
    pia->setKeyState(kKsFire);

    const uint64_t real_jiffy = kCyclesPerJiffy;
    int between = 0, seen = 0;
    for (int i = 0; i < 60; i++) {
        const uint64_t until = cpu->getCycles() + real_jiffy;
        while (cpu->getCycles() < until) c.runInstructions(1);
        pia->pulseTimerIrq();
        const Computer::VIC::Sprite &a = c.getVideoChip()->sprite(1);
        if (!a.enabled) continue;
        seen++;
        if (a.x % 16) between++;           // a cell is 16 nominal px: not on a boundary
    }
    pia->setKeyState(0);

    ASSERT_GT(seen, 20) << "never saw an arrow in flight";
    // Snapping puts it only ever on cell boundaries; gliding spends most frames between.
    EXPECT_GT(between * 2, seen)
        << "only " << between << " of " << seen
        << " frames had the arrow between cells -- it is jumping, not flying";
}

/* Winky moves every frame while a direction is held.
 *
 * SLIDE_DEN used to be 2, which crossed a cell in three frames of a six-frame tick and
 * then stood still for the other three. Continuous or not, a 50% duty cycle reads as
 * choppy. It was meant to cut input lag and was aimed at the wrong thing: the delay
 * before a press is felt is the keystate being sampled once a tick, and arriving early
 * does nothing about that. */
TEST_F(VentureTest, WinkyMovesEveryFrameWhileWalking)
{
    ASSERT_TRUE(enterRoomZero());
    pia->setKeyState(kKsRight);

    int held = 0, frames = 0;
    unsigned prev = 0xFFFF;
    for (int i = 0; i < 24; i++) {
        const uint64_t until = cpu->getCycles() + kCyclesPerJiffy;
        while (cpu->getCycles() < until) c.runInstructions(1);
        pia->pulseTimerIrq();
        const unsigned x = c.getVideoChip()->sprite(0).x;
        if (prev != 0xFFFF) { frames++; if (x == prev) held++; }
        prev = x;
    }
    pia->setKeyState(0);

    ASSERT_GT(frames, 20);
    EXPECT_LE(held, 3) << held << " of " << frames
                       << " frames drew Winky in the same place -- he is stuttering";
}

/* The intruder comes in by the doorway FURTHEST from Winky.
 *
 * It used to take the first exit in scan order, which is the doorway he walked in
 * through -- and if he has not moved since, which is exactly what dawdling means, that
 * is the cell next to him. So the clock meant to hurry him along materialised on top of
 * him and killed him on its first step. It is a reason to leave, not an ambush.
 *
 * Found while measuring something else: the intruder's sprite appeared frozen, which
 * turned out to be Winky already dead behind a CAUGHT screen. */
TEST_F(VentureTest, TheIntruderComesInByTheFarDoorway)
{
    ASSERT_TRUE(enterRoomZero());
    int wx, wy;
    ASSERT_TRUE(findWinky(&wx, &wy));

    // Stand still until one arrives (HALL_ROOM_TICKS ticks of dawdling).
    for (int i = 0; i < 2000 && !peek("_h_live"); i++) run(1);
    ASSERT_TRUE(peek("_h_live")) << "no intruder ever arrived";

    const int hx = peek("_h_x"), hy = peek("_h_y");
    const int d = abs(hx - wx) + abs(hy - wy);
    fprintf(stderr, "[ intruder ] came in at (%d,%d), %d cells from Winky at (%d,%d)\n",
            hx, hy, d, wx, wy);
    EXPECT_GT(d, 5) << "it materialised on top of a player who had not moved";
}

TEST_F(VentureTest, TheGameOpensOnTheDungeonHall)
{
    int wx = -1, wy = -1;
    EXPECT_TRUE(findWinkyOnMap(&wx, &wy)) << "Winky was never drawn in the hall";
    EXPECT_GT(countGlyphOnMap(kGlyphWall), 100) << "the hall's walls did not paint";

    // Four rooms, two entrances apiece, all open, and four hollow outlines: an 11x5
    // box is 26 perimeter cells once its two entrances are notched out of it.
    EXPECT_EQ(countGlyphOnMap(kGlyphDoor), kRoomsPerLevel * 2)
        << "every room should have both entrances open at a level's start";
    EXPECT_EQ(countOnMapWithAttr(kGlyphSealed, kL1Room), 72)
        << "the four room outlines did not paint, or are not hollow "
           "(rings of 16+24+24+16, less the eight cells cut into entrances)";
}

TEST_F(VentureTest, TheLevelOnePaletteIsMagenta)
{
    // The arcade recolours the whole dungeon each level -- magenta, then cyan, then
    // yellow. Reaching level 2 means looting four rooms, so this pins level 1 and
    // the tables in venture.c carry the rest.
    int wall = 0;
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++)
            if (mapGlyph(x, y) == kGlyphWall &&
                attrAt(kMapX + x, physRow(kMapY + y)) == kL1Wall)
                wall++;
    EXPECT_GT(wall, 60) << "the hall's walls are not the level's colour";
    EXPECT_EQ(countOnMapWithAttr(kGlyphSealed, kL1Room), 72)
        << "the room outlines are not the level's colour";

    // Walls dim, rooms bright. They share the solid-block glyph, so that difference
    // is the only thing separating the dungeon's structure from its rooms.
    EXPECT_NE(kL1Wall, kL1Room);
}

TEST_F(VentureTest, HudShowsScoreLivesLevel)
{
    const std::string hud = screenRow(0);
    EXPECT_NE(hud.find("SCORE"), std::string::npos);
    EXPECT_NE(hud.find("LIVES"), std::string::npos);
    EXPECT_NE(hud.find("LEVEL"), std::string::npos);
}

TEST_F(VentureTest, RoomsAreBlindFromTheHall)
{
    // The map shows a door, never what is behind it. If any of a room's contents
    // leaked onto the hall screen the whole point of committing blind would go
    // with it.
    EXPECT_EQ(countGlyphOnMap(kGlyphApples), 0) << "a treasure is visible from the hall";
    EXPECT_EQ((int)liveMonsters().size(), 0) << "a monster is live outside a room";
    EXPECT_EQ(countGlyphOnMap(kGlyphCorpse), 0);
}

TEST_F(VentureTest, WalkingOntoADoorEntersTheRoomBehindIt)
{
    ASSERT_TRUE(enterRoomZero()) << "never reached the first room";
    EXPECT_GE((int)liveMonsters().size(), 1) << "the room's monsters did not spawn";
    EXPECT_GT(countGlyph(kGlyphWall), 80) << "the room's walls did not paint";
    EXPECT_EQ(countGlyphOnMap(kGlyphDoor), 0) << "the hall is still on screen";
}

/* --- the level-start roster ------------------------------------------------ */

class VentureRosterTest : public VentureTest
{
protected:
    void SetUp() override { stop_at = kRoster; VentureTest::SetUp(); }
};

TEST_F(VentureRosterTest, ListsEveryTreasureBeforeTheLevel)
{
    // The arcade's level-start screen, and the only long-run progress the game
    // shows: a slot per treasure in the game, each a '?' until you have taken it.
    EXPECT_NE(screenRow(9).find("TREASURES"), std::string::npos)
        << "no treasure roster before the level";
    EXPECT_NE(screenRow(13).find("PLAYER 1 GET READY"), std::string::npos);

    int unknown = 0;
    for (int x = 0; x < kScrW; x++) if (glyphAt(x, 9) == '?') unknown++;
    EXPECT_EQ(unknown, kThemes)
        << "one slot per treasure, all unknown before anything is collected";

    // ...and it is a screen of its own, not drawn over the hall.
    EXPECT_EQ(countGlyphOnMap(kGlyphWall), 0) << "the hall is still on screen";
}

/* --- the doors line up --------------------------------------------------- */

TEST_F(VentureTest, TheEntranceYouUseDecidesWhichDoorwayYouArriveAt)
{
    // Slot 0 holds the serpent room at level 1, whose doorways are north and south, so
    // its hall entrances are cut into the north and south edges of its block. Come in
    // the north one and you should be at the room's north doorway -- not at whichever
    // doorway happened to be first in the layout, which is what it used to be.
    ASSERT_TRUE(enterSlot(true)) << "never got into the room by the north entrance";
    int wx, wy;
    ASSERT_TRUE(findWinky(&wx, &wy));
    EXPECT_LE(wy, 2) << "came in the north entrance and arrived at row " << wy;
    EXPECT_EQ(countGlyph(kGlyphDoorway), 2) << "both doorways should be drawn";
}

class VentureSouthTest : public VentureTest {};

TEST_F(VentureSouthTest, TheSouthEntranceArrivesAtTheSouthDoorway)
{
    // The other half of the same rule, and the half that was visibly broken: entering
    // from one side and being put down on a completely different one.
    ASSERT_TRUE(enterSlot(false, kGlyphRing))
        << "never got into the room by the south entrance";
    int wx, wy;
    ASSERT_TRUE(findWinky(&wx, &wy));
    EXPECT_GE(wy, kRoomH - 3) << "came in the south entrance and arrived at row " << wy;
}

/* --- looting a room, and what the hall shows afterwards -------------------- */

TEST_F(VentureTest, LootingARoomSealsItInTheHall)
{
    const int doors_before = countGlyphOnMap(kGlyphDoor);
    const int solid_before = countOnMapWithAttr(kGlyphSealed, kL1Room);
    ASSERT_EQ(doors_before, kRoomsPerLevel * 2);

    ASSERT_TRUE(lootARoomWithRetries()) << "never got the treasure out of the spider room";

    // The arcade's signal, and the only thing the hall ever tells you about a room:
    // both entrances gone and the hollow box filled in. Slot 2's block is 11x3, so
    // that is a 9x1 interior plus its two entrance cells -- eleven more room-coloured
    // blocks than before.
    EXPECT_EQ(countGlyphOnMap(kGlyphDoor), doors_before - 2)
        << "a looted room's entrances should be sealed";
    EXPECT_EQ(countOnMapWithAttr(kGlyphSealed, kL1Room), solid_before + 11)
        << "the looted room did not fill in solid";

    /* And the rule runs the other way too. We went IN by the spider room's south
     * doorway and OUT by its north one, so we should be standing at the north of its
     * block -- (7,5), the only open cell beside that entrance. Coming out where we went
     * in would be the same bug seen from the other end. */
    int wx, wy;
    ASSERT_TRUE(findWinkyOnMap(&wx, &wy));
    EXPECT_EQ(wx, 7) << "left by the north doorway and came out at column " << wx;
    EXPECT_EQ(wy, 5) << "left by the north doorway and came out at row " << wy;
}

TEST_F(VentureTest, ASealedRoomCannotBeWalkedBackInto)
{
    ASSERT_TRUE(lootARoomWithRetries()) << "never got the treasure out of the spider room";

    /* Winky comes back at (7,5), directly above the entrance he left by -- which is
     * now wall. Push straight down into it: he must stay on row 5 rather than re-enter
     * a room with nothing left in it. Kept short on purpose; two Hallmonsters are awake
     * by now and a long demonstration is just a long walk toward them. */
    int wx, wy;
    ASSERT_TRUE(findWinkyOnMap(&wx, &wy));
    ASSERT_EQ(wy, 5) << "not where a north exit should have put us";
    hold(kKsDown, 6);
    ASSERT_TRUE(findWinkyOnMap(&wx, &wy)) << "left the hall through a sealed room";
    EXPECT_EQ(countGlyph(kGlyphRing), 0) << "walked back into a looted room";
    EXPECT_EQ(wy, 5) << "pushed down through room 2's sealed entrance";
}

TEST_F(VentureTest, HallmonstersGrowInNumberAsRoomsAreLooted)
{
    // The arcade's hall starts nearly empty and is crawling by the fourth room,
    // which is what stops the last room of a level being the easiest.
    ASSERT_EQ((int)liveHallmonsters().size(), kHallBase)
        << "the hall should start with just the one";

    ASSERT_TRUE(lootARoomWithRetries()) << "never got the treasure out of the spider room";
    EXPECT_EQ((int)liveHallmonsters().size(), kHallBase + 1)
        << "looting a room should wake another Hallmonster";
}

/* --- the facing pip -------------------------------------------------------- */

TEST_F(VentureTest, TheFacingPipShowsWhereTheArrowWillGo)
{
    ASSERT_TRUE(enterRoomZero());

    // Winky arrives facing in from the doorway, so the pip is already there. Facing
    // persists after you release a key -- that is what lets you back away from
    // something while still aiming at it, and without the pip it is a guess.
    int wx, wy;
    ASSERT_TRUE(findWinky(&wx, &wy));
    int px, py;
    ASSERT_TRUE(pipAt(&px, &py))
        << "no pip at all, and Winky came in through the top doorway facing down";
    EXPECT_EQ(px, wx);
    EXPECT_EQ(py, wy + 1) << "the pip is not below Winky";

    hold(kKsLeft, 2);
    ASSERT_TRUE(findWinky(&wx, &wy)) << "died before facing could be re-tested";
    ASSERT_TRUE(pipAt(&px, &py)) << "the pip did not follow facing";
    EXPECT_EQ(px, wx - 1);
    EXPECT_EQ(py, wy);
}

TEST_F(VentureTest, RoomsHaveTwoDoorwaysAndBothAreDrawn)
{
    ASSERT_TRUE(enterRoomZero());
    EXPECT_EQ(countGlyph(kGlyphDoorway), 2) << "both doorways should be drawn";
}


// --- step 7: Hallmonsters --------------------------------------------------

TEST_F(VentureTest, HallmonstersPatrolTheMap)
{
    EXPECT_EQ((int)liveHallmonsters().size(), kHallBase)
        << "the hall should be patrolled from the moment the game opens";

    // And they move: a clock that never advances is not a clock.
    std::vector<std::pair<int, int>> before, after;
    for (const Ent &m : liveHallmonsters()) before.push_back({m.x, m.y});
    run(60);
    for (const Ent &m : liveHallmonsters()) after.push_back({m.x, m.y});
    EXPECT_NE(before, after) << "the Hallmonsters never moved";
}

TEST_F(VentureTest, AHallmonsterKillsOnContact)
{
    // They cannot be shot, blocked or outrun forever -- walking into one is death,
    // and that is the whole of their contract. Head for the one posted in the lower
    // hall; it is closing on Winky at the same time, so contact is not in doubt.
    EXPECT_TRUE(waitToBeCaughtInTheHall())
        << "standing in a patrolled hall was survivable indefinitely";
}

/* NOT tested, and why. Both are logged in TODO.md.
 *
 * 1. The Hallmonster that comes into a room you linger in -- including the fact that
 *    it walks through the walls to get to you. It needs Winky alive for
 *    HALL_ROOM_TICKS -- 260 ticks, about seventeen seconds -- inside a room with
 *    three serpents converging, and no scripted route survives that: standing still,
 *    lapping the room's outer circuit and clearing the serpents first were all tried
 *    and each dies well short. A test would have to actually play well, which is a
 *    bigger thing than the assertion is worth.
 *
 *    Unverified: the `dawdle` counter, the spawn point in hall_intrude(), and the
 *    through_walls path in chase(). Everything else the intruder does -- pursuit,
 *    ignoring bodies, killing on contact, stopping arrows -- is the same chase() and
 *    hall_advance() the hall tests above drive directly.
 *
 * 2. That a shot never passes through a monster. The bug was real and specific -- the
 *    arrow advanced past a monster's old cell earlier in the tick, the monster stepped
 *    into the arrow's new one, and nothing looked again -- and the fix is small, but no
 *    test here reliably tells the two builds apart. The screen cannot show it: the
 *    arrow is drawn over the monster it is sitting on, so the only signature is a
 *    serpent briefly missing from a finished frame, and provoking that needs a monster
 *    to step onto a live arrow, on a tick monsters move, while Winky is alive to watch.
 *    Every setup tried -- hunting, holding fire in a doorway, across all three lives --
 *    produced too few shots near enough monsters, and passed on the broken build as
 *    often as the fixed one. A test that passes on the broken build is worse than no
 *    test, so there is none.
 *
 *    Untested with it, for the same reason: monsters no longer stepping onto each
 *    other. Both are in chase()/monsters_advance(), which everything else here drives
 *    hard.
 *
 * 3. The between-levels tally (SCORE THIS LEVEL / BONUS MULTIPLIER / TOTAL BONUS).
 *    It only appears once all four rooms of a level are looted, which is four bespoke
 *    routes through four different layouts -- an order of magnitude more harness than
 *    lootRoomZero(), for one screen. Unverified: the trigger, BONUS_MULT() and the
 *    overflow clamp in scaled(). The roster screen, which uses the same drawing
 *    primitives, IS tested. */

// --- step 1: movement comes off the control port ----------------------------

TEST_F(VentureTest, MovesOnHeldKeyFromTheControlPort)
{
    int x0, y0, x1, y1;
    ASSERT_TRUE(findWinkyOnMap(&x0, &y0));

    hold(kKsLeft, 4);
    ASSERT_TRUE(findWinkyOnMap(&x1, &y1)) << "Winky vanished while moving";
    EXPECT_LT(x1, x0) << "holding LEFT did not move Winky left";
    EXPECT_EQ(y1, y0) << "LEFT should not change the row";
}

TEST_F(VentureTest, MovesDiagonallyWhenTwoBitsAreHeld)
{
    int x0, y0, x1, y1;
    // Straight from the start cell, which has open hall on both axes. Walking
    // somewhere else first only spends ticks walking toward a Hallmonster.
    ASSERT_TRUE(findWinkyOnMap(&x0, &y0));

    // Both bits at once: the whole reason the control port exists. The keystroke
    // FIFO could never express this -- it has no key-up and the host only
    // auto-repeats the most recent key.
    hold(kKsLeft | kKsUp, 3);
    ASSERT_TRUE(findWinkyOnMap(&x1, &y1));
    EXPECT_LT(x1, x0) << "diagonal did not move left";
    EXPECT_LT(y1, y0) << "diagonal did not move up";
}

TEST_F(VentureTest, WallsBlockMovement)
{
    int x1, y1;
    // Drive into the corner well past the wall and confirm he stops rather than
    // walking out of the hall entirely.
    hold(kKsDown | kKsLeft, 14);
    ASSERT_TRUE(findWinkyOnMap(&x1, &y1)) << "Winky left the hall through a wall";
    EXPECT_GT(x1, 0);
    EXPECT_LT(y1, kMapH - 1);
    EXPECT_NE(mapGlyph(x1, y1), kGlyphWall);
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
        ASSERT_TRUE(findWinkyOnMap(&wx, &wy))
            << "the game exited (or died) after an arrow with final byte '"
            << a.final_byte << "'";
    }

    // ...and the HUD is still there, so swallowing the sequence has not eaten
    // the loop along with it.
    const std::string hud = screenRow(0);
    EXPECT_NE(hud.find("SCORE"), std::string::npos) << "still in the game";
}

// --- step 3: one arrow in flight -------------------------------------------

TEST_F(VentureTest, FiringLeavesAtMostOneArrowInFlight)
{
    ASSERT_TRUE(enterRoomZero());

    // Winky arrives facing into the room down a column that is clear for seven
    // cells, so hold fire from where he stands. Even holding it, the original allows
    // exactly one arrow on screen, so no frame may ever show two. Sample repeatedly
    // while the key is down.
    pia->setKeyState(kKsFire);
    int worst = 0, sightings = 0;
    for (int i = 0; i < 16; i++) {
        run(2);
        const int arrows = arrowInFlight() ? 1 : 0;
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
    ASSERT_TRUE(enterRoomZero());

    // The original's rule: a monster pays nothing until the treasure is in hand.
    // Kill one without having taken the apples and the score must still be zero,
    // which is what inverts the safe instinct to clear the room and then loot.
    ASSERT_TRUE(killOneSerpent(60)) << "never managed to kill a serpent";

    const std::string row = screenRow(0);
    const size_t at = row.find("SCORE");
    ASSERT_NE(at, std::string::npos) << "the room ended before we could look";
    const std::string field = row.substr(at + 5, 8);
    EXPECT_EQ(field.find_first_not_of(" 0"), std::string::npos)
        << "score must be zero before the treasure, got '" << field << "'";
}

TEST_F(VentureTest, DeadMonstersLeaveBodiesBehind)
{
    ASSERT_TRUE(enterRoomZero());
    ASSERT_EQ(countGlyph(kGlyphCorpse), 0) << "bodies exist before anything died";

    // Venture's signature cruelty: the body remains, and remains lethal, so every
    // kill shrinks the room you have left to move in.
    EXPECT_TRUE(killOneSerpent(60)) << "a killed serpent left no body on the floor";
}

TEST_F(VentureTest, MonstersKeepComingOnceTheirDeadAreInTheWay)
{
    ASSERT_TRUE(enterRoomZero());
    ASSERT_GE(huntSerpents(60, 1), 1) << "could not get a body onto the floor";

    /* A monster will not walk over a corpse. The question is what it does instead:
     * route round it, or stop dead. It used to stop dead -- the step was chosen and
     * then vetoed, which left the monster standing next to its own dead waiting to be
     * shot, and a room full of bodies was a room full of statues.
     *
     * Sample every serpent's cell five times, six ticks apart. MON_EVERY is 3, so
     * that is two moves per sample: a serpent that is still hunting cannot hold one
     * cell across all five, and a stalled one cannot leave it. Winky keeps walking so
     * he is not standing still while they close. */
    std::vector<std::set<std::pair<int, int>>> samples;
    for (int i = 0; i < 5; i++) {
        std::set<std::pair<int, int>> here;
        for (const Ent &m : liveMonsters()) here.insert({m.x, m.y});
        if (!here.empty()) samples.push_back(here);
        hold(i & 1 ? kKsRight : kKsLeft, 6);
        int wx, wy;
        if (!findWinky(&wx, &wy)) break;
    }
    ASSERT_GE(samples.size(), 2u) << "the room ended before we could watch it";

    for (const auto &cell : samples.front()) {
        bool everywhere = true;
        for (const auto &sample : samples)
            if (!sample.count(cell)) { everywhere = false; break; }
        EXPECT_FALSE(everywhere)
            << "a serpent held cell " << cell.first << "," << cell.second
            << " through every sample -- it is stalled, not hunting";
    }
}

/* --- the end of a run ------------------------------------------------------ */

TEST_F(VentureTest, GameOverShowsTheScoreAndOffersAnotherGo)
{
    /* Burn all three lives in the hall, which needs no skill at all -- a Hallmonster
     * is already patrolling and standing still is fatal. Each death banners CAUGHT and
     * waits for a key; the last one should not. */
    bool over = false;
    for (int life = 0; life < 4 && !over; life++) {
        for (int i = 0; i < 400; i++) {
            run(kTickRate);
            if (screenRow(8).find("G A M E   O V E R") != std::string::npos) {
                over = true;
                break;
            }
            if (caught()) { pressKey('\r'); run(200); break; }
        }
    }
    ASSERT_TRUE(over) << "three deaths did not end the run";
    run(20);   // the banner paints before the score and the offer below it

    // What you scored, and the offer. A run that just exits to the DOS tells you
    // nothing about how you did.
    EXPECT_NE(screenRow(11).find("FINAL SCORE"), std::string::npos)
        << "no final score on the game-over screen";
    EXPECT_NE(screenRow(14).find("PLAY AGAIN"), std::string::npos)
        << "no offer of another game";

    // Y starts a fresh run: back to the roster, with the score cleared.
    pressKey('y');
    run(200);
    EXPECT_NE(screenRow(9).find("TREASURES"), std::string::npos)
        << "answering Y should deal a new game, not resume the old one";

    pressKey('\r');
    run(220);
    const std::string hud = screenRow(0);
    ASSERT_NE(hud.find("SCORE"), std::string::npos) << "the new game did not start";
    EXPECT_NE(hud.find("LIVES 3"), std::string::npos) << "lives were not restored";
    const size_t at = hud.find("SCORE");
    EXPECT_EQ(hud.substr(at + 5, 8).find_first_not_of(" 0"), std::string::npos)
        << "the score carried over into the new game";
}

// --- step 10: sound --------------------------------------------------------

TEST_F(VentureTest, SoundCuesGateVoiceOneAndReleaseIt)
{
    // Cues, not a score. Death is the easiest one to provoke, and it is also the
    // one with the longest hold: the tone runs under the CAUGHT screen and is only
    // released when the player acknowledges it. A cue that never released would
    // leave the machine droning through the whole of the next room.
    ASSERT_TRUE(waitToBeCaughtInTheHall()) << "could not provoke a death to listen to";

    EXPECT_TRUE(c.getSid()->read(kSidV1Control) & kSidGate)
        << "dying made no sound at all";
    EXPECT_NE(c.getSid()->read(kSidV1FreqLo), 0) << "voice 1 was gated at zero Hz";

    pressKey('\r');            // acknowledge CAUGHT
    run(200);
    EXPECT_FALSE(c.getSid()->read(kSidV1Control) & kSidGate)
        << "the death cue was never gated off";
}

} // namespace
