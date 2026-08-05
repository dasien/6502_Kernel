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
#include <vector>

#include "computer/Computer6502.h"
#include "computer/CPU6502.h"
#include "computer/Memory.h"
#include "computer/PIA.h"
#include "computer/Sid.h"
#include "computer/VIC.h"

using Computer::Computer6502;

namespace {

// Must match venture.h.
constexpr int kScrW = 80;
constexpr int kRoomW = 44, kRoomH = 15;
constexpr int kRoomX = (kScrW - kRoomW) / 2, kRoomY = 5;
constexpr int kMapW = 56, kMapH = 15;
constexpr int kMapX = (kScrW - kMapW) / 2, kMapY = 5;
constexpr int kThemes = 6, kRoomsPerLevel = 4;

constexpr uint8_t kGlyphWinky = 0x01, kGlyphWinky2 = 0x02;
constexpr uint8_t kGlyphWall = 0xDB, kGlyphCorpse = 0xB0;
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
constexpr int kMaxHallPosts = 6, kHallBase = 1;

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

TEST(VentureLayout, TheHallIsWellFormedAndNoRoomCanStrandAnother)
{
    const std::string src = sourceText();
    ASSERT_FALSE(src.empty());

    const auto g = rowsOf(src, "map_layout[MAP_H] = {");
    ASSERT_EQ(g.size(), static_cast<size_t>(kMapH));
    for (int y = 0; y < kMapH; y++)
        ASSERT_EQ(g[y].size(), static_cast<size_t>(kMapW)) << "hall row " << y;

    for (int x = 0; x < kMapW; x++) {
        EXPECT_EQ(g[0][x], '#') << "the hall leaks at the top, column " << x;
        EXPECT_EQ(g[kMapH - 1][x], '#') << "the hall leaks at the bottom, column " << x;
    }
    for (int y = 0; y < kMapH; y++) {
        EXPECT_EQ(g[y][0], '#');
        EXPECT_EQ(g[y][kMapW - 1], '#');
    }

    // MAP_START_X / MAP_START_Y from venture.h.
    ASSERT_EQ(g[5][28], '.') << "Winky would start inside a wall";
    const auto reach = flood(g, kMapW, kMapH, {28, 5});

    int posts = 0;
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++)
            if (g[y][x] == 'h') {
                posts++;
                EXPECT_TRUE(reach.count({x, y}))
                    << "a Hallmonster is sealed off from the hall it patrols";
            }
    EXPECT_EQ(posts, kMaxHallPosts) << "one post per possible Hallmonster";

    /* Entrances are NOT in the layout. They are cut at runtime into the middle of
     * whichever block edges match the room a slot is holding, so all four edge-middles
     * of every block have to be usable -- and sealing any one room must not strand
     * another's, whichever sides those turn out to be. */
    const char outline[] = "ABCD", inside[] = "abcd";
    const std::pair<int, int> blk[] = {{3, 2}, {42, 2}, {3, 8}, {42, 8}};
    const int kBlkW = 11, kBlkH = 5;

    for (int i = 0; i < kRoomsPerLevel; i++) {
        std::vector<std::pair<int, int>> body, interior;
        for (int y = 0; y < kMapH; y++)
            for (int x = 0; x < kMapW; x++) {
                if (g[y][x] == outline[i]) body.push_back({x, y});
                if (g[y][x] == inside[i]) interior.push_back({x, y});
            }
        EXPECT_EQ(body.size(), 28u) << "room " << i << "'s outline is not an 11x5 ring";
        EXPECT_EQ(interior.size(), 27u) << "room " << i << "'s interior is not 9x3";

        const auto [x0, y0] = blk[i];
        const std::pair<int, int> mid[] = {
            {x0 + kBlkW / 2, y0}, {x0 + kBlkW / 2, y0 + kBlkH - 1},
            {x0, y0 + kBlkH / 2}, {x0 + kBlkW - 1, y0 + kBlkH / 2},
        };
        for (const auto &m : mid) {
            EXPECT_EQ(g[m.second][m.first], outline[i])
                << "room " << i << " edge-middle " << m.first << "," << m.second
                << " is not part of its outline";
            bool open_beside = false;
            for (const auto &d : {std::pair{1, 0}, std::pair{-1, 0},
                                  std::pair{0, 1}, std::pair{0, -1}})
                if (g[m.second + d.second][m.first + d.first] == '.') open_beside = true;
            EXPECT_TRUE(open_beside) << "an entrance cut at " << m.first << ","
                                     << m.second << " would be unreachable";
        }

        // The interior is what fills in when the room is looted, so it must be sealed
        // inside its own outline -- a leak would put unwalkable black cells out in the
        // open hall, which reads as floor.
        for (const auto &cl : interior)
            for (const auto &d : {std::pair{1, 0}, std::pair{-1, 0},
                                  std::pair{0, 1}, std::pair{0, -1}}) {
                const char n = g[cl.second + d.second][cl.first + d.first];
                EXPECT_TRUE(n == outline[i] || n == inside[i])
                    << "room " << i << " interior leaks into '" << n << "'";
            }

        std::vector<std::string> sealed = g;
        for (const auto &cl : body)     sealed[cl.second][cl.first] = '#';
        for (const auto &cl : interior) sealed[cl.second][cl.first] = '#';
        const auto after = flood(sealed, kMapW, kMapH, {28, 5});
        for (int j = 0; j < kRoomsPerLevel; j++) {
            if (j == i) continue;
            const auto [jx, jy] = blk[j];
            const std::pair<int, int> jmid[] = {
                {jx + kBlkW / 2, jy}, {jx + kBlkW / 2, jy + kBlkH - 1},
                {jx, jy + kBlkH / 2}, {jx + kBlkW - 1, jy + kBlkH / 2},
            };
            for (const auto &m : jmid) {
                bool reachable = false;
                for (const auto &d : {std::pair{1, 0}, std::pair{-1, 0},
                                      std::pair{0, 1}, std::pair{0, -1}})
                    if (after.count({m.first + d.first, m.second + d.second}))
                        reachable = true;
                EXPECT_TRUE(reachable) << "sealing room " << i << " strands room " << j
                                       << "'s " << m.first << "," << m.second
                                       << " entrance";
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
    static constexpr uint64_t kCyclesPerJiffy = 1000000 / 60;

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

    /* Hold `mask` until Winky has actually moved `cells` cells, or stops moving.
     *
     * Counting ticks cannot be trusted to the cell. The game's fixed-tick accumulator
     * and this harness's jiffy budget drift by a tick depending on phase, so a leg
     * asked for in ticks lands one cell short often enough to matter -- and a route
     * that stops one cell short of its treasure walks the whole way back without it,
     * which is exactly the bug this replaced. Reading the screen is exact. */
    void walk(uint8_t mask, int cells, bool on_map)
    {
        int sx, sy;
        if (!(on_map ? findWinkyOnMap(&sx, &sy) : findWinky(&sx, &sy))) return;
        pia->setKeyState(mask);
        for (int i = 0; i < cells * 3 + 8; i++) {
            run(4);
            int x, y;
            if (!(on_map ? findWinkyOnMap(&x, &y) : findWinky(&x, &y))) break;
            if (abs(x - sx) + abs(y - sy) >= cells) break;
        }
        pia->setKeyState(0);
        run(2);
    }

    void walkInRoom(uint8_t mask, int cells) { walk(mask, cells, false); }
    void walkOnMap(uint8_t mask, int cells)  { walk(mask, cells, true); }

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

    uint8_t attrAt(int col, int row)
    {
        return c.getVideoChip()->getColorAt(col, row);
    }

    // A sealed room and a plain hall wall share the solid-block glyph -- as they do
    // in the arcade -- so telling them apart needs the attribute plane.
    int countOnMapWithAttr(uint8_t g, uint8_t attr)
    {
        int n = 0;
        for (int y = 0; y < kMapH; y++)
            for (int x = 0; x < kMapW; x++)
                if (glyphAt(kMapX + x, kMapY + y) == g &&
                    attrAt(kMapX + x, kMapY + y) == attr) n++;
        return n;
    }

    uint8_t roomGlyph(int rx, int ry) { return glyphAt(kRoomX + rx, kRoomY + ry); }
    uint8_t mapGlyph(int rx, int ry) { return glyphAt(kMapX + rx, kMapY + ry); }

    // Winky is drawn with two alternating frames, so either counts.
    bool isWinky(uint8_t g) { return g == kGlyphWinky || g == kGlyphWinky2; }

    /* Find Winky, retrying over a few frames.
     *
     * The retry is not slop -- it is required. A step() erases every cell that can
     * move and only then redraws them, and the harness stops the CPU at whatever
     * instruction boundary a cycle budget lands on. Sample in that window and Winky
     * is genuinely not on the screen: erased, not yet redrawn. A single-frame scan
     * therefore fails at random, and reads as "the game died" when nothing at all
     * is wrong. Extra jiffies are harmless because winky_move() does nothing with no
     * direction bits held. */
    bool findWinkyIn(int w, int h, int ox, int oy, int *rx, int *ry)
    {
        for (int attempt = 0; attempt < 4; attempt++) {
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    if (isWinky(glyphAt(ox + x, oy + y))) {
                        *rx = x; *ry = y; return true;
                    }
            run(2);
        }
        return false;
    }

    bool findWinky(int *rx, int *ry)
    {
        return findWinkyIn(kRoomW, kRoomH, kRoomX, kRoomY, rx, ry);
    }

    bool findWinkyOnMap(int *rx, int *ry)
    {
        return findWinkyIn(kMapW, kMapH, kMapX, kMapY, rx, ry);
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
            run(4);
            int x, y;
            if (!findWinkyOnMap(&x, &y)) break;
            if (x == px && y == py) { if (++still >= 2) break; }
            else { still = 0; px = x; py = y; }
        }
        pia->setKeyState(0);
        run(2);
    }

    bool hudUp() { return screenRow(1).find("LEVEL") != std::string::npos; }
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
     * The entrances are cut into the middle of whichever block edges match the room's
     * doorway sides, so a north entrance is approached from the row above it and a
     * south one from the row below. At level 1 slot 0 holds the serpent room, whose
     * doorways are north and south, so its entrances are at (8,2) and (8,6).
     *
     * `north` picks which. Coming in the north entrance puts Winky at the room's north
     * doorway; the south one puts him at its south doorway. That is the point.
     *
     * `ready` is the treasure glyph, which paints near the end of the board: polling
     * for it is how we know the room is up, and waiting no longer than that matters
     * because every extra jiffy is one Winky spends in the doorway while the room
     * converges on him. */
    bool enterSlotZero(bool north, uint8_t ready = kGlyphApples)
    {
        /* Corner first, then count. Going straight for the entrance only works from
         * the start cell, and this gets called again after a death -- when Winky is
         * back beside whichever entrance he last used. Two wall-clamped legs put him
         * somewhere known from anywhere. */
        if (north) {
            hold(kKsUp, 20);             // clamps on row 1
            hold(kKsLeft, 60);           // clamps at (1,1)
            walkOnMap(kKsRight, 7);      // to (8,1), above the entrance
            hold(kKsDown, 2);            // down onto it at (8,2)
        } else {
            hold(kKsDown, 20);           // clamps on row 13
            hold(kKsLeft, 60);           // clamps at (1,13)
            walkOnMap(kKsUp, 6);         // to (1,7), the open row below the block
            walkOnMap(kKsRight, 7);      // to (8,7)
            hold(kKsUp, 2);              // up onto the entrance at (8,6)
        }
        int x, y;
        for (int i = 0; i < 60; i++) {
            run(5);
            if (countGlyph(ready) == 1 && findWinky(&x, &y)) return true;
        }
        if (caught()) { pressKey('\r'); run(240); }
        return false;
    }

    bool enterRoomZero() { return enterSlotZero(true); }

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
        /* In by slot 2's SOUTH entrance, at (8,12), which is approached from row 13.
         * Row 13 is the one open lane in the hall with no Hallmonster post on or beside
         * it, and at a level's start only the first post is awake -- so this is the
         * quietest way across. */
        hold(kKsDown, 20);          // clamps on row 13
        hold(kKsLeft, 60);          // clamps at (1,13) -- works from anywhere, which
        walkOnMap(kKsRight, 7);     // matters because this is retried after a death
        hold(kKsUp, 2);             // up onto the entrance at (8,12)

        int x, y;
        bool inside = false;
        for (int i = 0; i < 60 && !inside; i++) {
            run(5);
            inside = countGlyph(kGlyphRing) == 1 && findWinky(&x, &y);
        }
        // Acknowledge a death here too, or the game sits on CAUGHT waiting for a key
        // and every retry runs against a stopped machine.
        if (!inside) { if (caught()) { pressKey('\r'); run(240); } return false; }

        /* A south entrance puts Winky at the room's south doorway, (33,14), so he
         * starts at (33,13). Column 33 and rows 1 and 13 are all clear of the web's
         * pillars, and column 21 carries the ring. Out by the doorway he came in. */
        walkInRoom(kKsLeft, 12);    // (21,13)
        walkInRoom(kKsUp, 6);       // (21,7), the ring
        walkInRoom(kKsDown, 6);     // back to (21,13)
        walkInRoom(kKsRight, 12);   // (33,13), under the doorway
        hold(kKsDown, 3);           // and out
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
            for (int y = 0; y < kRoomH; y++)
                for (int x = 0; x < kRoomW; x++)
                    if (roomGlyph(x, y) == kGlyphSerpent) {
                        const int d = abs(x - wx) + abs(y - wy);
                        if (d < best) { best = d; bx = x; by = y; }
                    }
            // Nothing drawn: step() erases before it redraws, so a sample can land
            // in a frame with no entities in it at all.
            if (bx < 0) { run(4); continue; }

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
            run(4);
            pia->setKeyState(0);
            run(shoot ? 8 : 2);      // let the arrow finish its flight
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
            run(4);
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

TEST_F(VentureTest, TheGameOpensOnTheDungeonHall)
{
    int wx = -1, wy = -1;
    EXPECT_TRUE(findWinkyOnMap(&wx, &wy)) << "Winky was never drawn in the hall";
    EXPECT_GT(countGlyphOnMap(kGlyphWall), 120) << "the hall's walls did not paint";

    // Four rooms, two entrances apiece, all open, and four hollow outlines: an 11x5
    // box is 26 perimeter cells once its two entrances are notched out of it.
    EXPECT_EQ(countGlyphOnMap(kGlyphDoor), kRoomsPerLevel * 2)
        << "every room should have both entrances open at a level's start";
    EXPECT_EQ(countOnMapWithAttr(kGlyphSealed, kL1Room), kRoomsPerLevel * 26)
        << "the four room outlines did not paint, or are not hollow";
}

TEST_F(VentureTest, TheLevelOnePaletteIsMagenta)
{
    // The arcade recolours the whole dungeon each level -- magenta, then cyan, then
    // yellow. Reaching level 2 means looting four rooms, so this pins level 1 and
    // the tables in venture.c carry the rest.
    int wall = 0;
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++)
            if (mapGlyph(x, y) == kGlyphWall && attrAt(kMapX + x, kMapY + y) == kL1Wall)
                wall++;
    EXPECT_GT(wall, 120) << "the hall's walls are not the level's colour";
    EXPECT_EQ(countOnMapWithAttr(kGlyphSealed, kL1Room), kRoomsPerLevel * 26)
        << "the room outlines are not the level's colour";

    // Walls dim, rooms bright. They share the solid-block glyph, so that difference
    // is the only thing separating the dungeon's structure from its rooms.
    EXPECT_NE(kL1Wall, kL1Room);
}

TEST_F(VentureTest, HudShowsScoreLivesLevel)
{
    const std::string hud = screenRow(1);
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
    EXPECT_EQ(countGlyphOnMap(kGlyphSerpent), 0) << "a monster is visible from the hall";
    EXPECT_EQ(countGlyphOnMap(kGlyphCorpse), 0);
}

TEST_F(VentureTest, WalkingOntoADoorEntersTheRoomBehindIt)
{
    ASSERT_TRUE(enterRoomZero()) << "never reached the first room";
    EXPECT_GE(countGlyph(kGlyphSerpent), 1) << "the room's monsters did not spawn";
    EXPECT_GT(countGlyph(kGlyphWall), 100) << "the room's walls did not paint";
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
    ASSERT_TRUE(enterSlotZero(true)) << "never got into the room by the north entrance";
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
    ASSERT_TRUE(enterSlotZero(false)) << "never got into the room by the south entrance";
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
    // both entrances gone and the hollow box filled in. That is the 9x3 interior plus
    // the two entrance cells -- 29 more room-coloured blocks than before.
    EXPECT_EQ(countGlyphOnMap(kGlyphDoor), doors_before - 2)
        << "a looted room's entrances should be sealed";
    EXPECT_EQ(countOnMapWithAttr(kGlyphSealed, kL1Room), solid_before + 29)
        << "the looted room did not fill in solid";

    /* And the rule runs the other way too. We went in and came out by the spider
     * room's SOUTH doorway, so we should be standing at the south of its block --
     * (8,13), the only open cell beside the entrance at (8,12). Coming out somewhere
     * else is the same bug seen from the other end. */
    int wx, wy;
    ASSERT_TRUE(findWinkyOnMap(&wx, &wy));
    EXPECT_EQ(wx, 8) << "left by the south doorway and came out at column " << wx;
    EXPECT_EQ(wy, 13) << "left by the south doorway and came out at row " << wy;
}

TEST_F(VentureTest, ASealedRoomCannotBeWalkedBackInto)
{
    ASSERT_TRUE(lootARoomWithRetries()) << "never got the treasure out of the spider room";

    /* Winky comes back at (8,13), directly below the entrance he used -- which is now
     * wall. Push straight up into it: he must stay on row 13 rather than re-enter a
     * room with nothing left in it. Kept short on purpose; two Hallmonsters are awake
     * by now and a long demonstration is just a long walk toward them. */
    int wx, wy;
    ASSERT_TRUE(findWinkyOnMap(&wx, &wy));
    ASSERT_EQ(wy, 13) << "not where a south exit should have put us";
    hold(kKsUp, 6);
    ASSERT_TRUE(findWinkyOnMap(&wx, &wy)) << "left the hall through a sealed room";
    EXPECT_EQ(countGlyph(kGlyphRing), 0) << "walked back into a looted room";
    EXPECT_EQ(wy, 13) << "pushed up through room 2's sealed entrance";
}

TEST_F(VentureTest, HallmonstersGrowInNumberAsRoomsAreLooted)
{
    // The arcade's hall starts nearly empty and is crawling by the fourth room,
    // which is what stops the last room of a level being the easiest.
    ASSERT_EQ(countGlyphOnMap(kGlyphHallmon), kHallBase)
        << "the hall should start with just the one";

    ASSERT_TRUE(lootARoomWithRetries()) << "never got the treasure out of the spider room";
    EXPECT_EQ(countGlyphOnMap(kGlyphHallmon), kHallBase + 1)
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
    EXPECT_EQ(roomGlyph(wx, wy + 1), kGlyphFaceD)
        << "no pip below Winky, who came in through the top doorway facing down";

    hold(kKsLeft, 2);
    ASSERT_TRUE(findWinky(&wx, &wy)) << "died before facing could be re-tested";
    EXPECT_EQ(roomGlyph(wx - 1, wy), kGlyphFaceL) << "the pip did not follow facing";
}

TEST_F(VentureTest, RoomsHaveTwoDoorwaysAndBothAreDrawn)
{
    ASSERT_TRUE(enterRoomZero());
    EXPECT_EQ(countGlyph(kGlyphDoorway), 2)
        << "a room should show the doorway it was entered by and the one to run for";
}

// --- step 7: Hallmonsters --------------------------------------------------

TEST_F(VentureTest, HallmonstersPatrolTheMap)
{
    EXPECT_EQ(countGlyphOnMap(kGlyphHallmon), kHallBase)
        << "the hall should be patrolled from the moment the game opens";

    // And they move: a clock that never advances is not a clock.
    std::vector<std::pair<int, int>> before, after;
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++)
            if (mapGlyph(x, y) == kGlyphHallmon) before.push_back({x, y});
    run(60);
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++)
            if (mapGlyph(x, y) == kGlyphHallmon) after.push_back({x, y});
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
    hold(kKsDown | kKsLeft, 40);
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
    const std::string hud = screenRow(1);
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
    ASSERT_TRUE(enterRoomZero());

    // The original's rule: a monster pays nothing until the treasure is in hand.
    // Kill one without having taken the apples and the score must still be zero,
    // which is what inverts the safe instinct to clear the room and then loot.
    ASSERT_TRUE(killOneSerpent(60)) << "never managed to kill a serpent";

    const std::string row = screenRow(1);
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
        for (int y = 0; y < kRoomH; y++)
            for (int x = 0; x < kRoomW; x++)
                if (roomGlyph(x, y) == kGlyphSerpent) here.insert({x, y});
        if (!here.empty()) samples.push_back(here);
        hold(i & 1 ? kKsRight : kKsLeft, 6);
        int wx, wy;
        if (!findWinky(&wx, &wy)) break;
    }
    ASSERT_GE(samples.size(), 4u) << "the room ended before we could watch it";

    for (const auto &cell : samples.front()) {
        bool everywhere = true;
        for (const auto &sample : samples)
            if (!sample.count(cell)) { everywhere = false; break; }
        EXPECT_FALSE(everywhere)
            << "a serpent held cell " << cell.first << "," << cell.second
            << " through every sample -- it is stalled, not hunting";
    }
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
