/**
 * @file test_kpanic.cpp
 * @brief Headless tests for KERNEL PANIC (programs/kpanic).
 *
 * The game had no harness while steps 1-6 were built, so its tuning was done by
 * replicating the logic in throwaway host C and measuring. That caught real
 * things, but it could only ever test a copy of the game. This drives the real
 * blob: `kpanic_bin` links a flat $0800 image with `-Ln`, the fixture writes it
 * into memory and sets PC there, and state is read back by name out of
 * kpanic.lbl rather than inferred from the screen.
 *
 * The label file carries non-static symbols only, so anything a test needs to
 * see is declared without `static` in kpanic.c -- linkage only, same storage and
 * the same generated code (the blob is byte-for-byte the same size).
 *
 * What belongs here and what does not: cycle counts, register contents, score
 * arithmetic and grid coordinates are facts a harness settles. Whether an
 * explosion reads as an explosion is not, and no amount of instrumentation makes
 * it one -- that gets built and looked at.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "computer/CPU6502.h"
#include "computer/Computer6502.h"
#include "computer/Memory.h"
#include "computer/PIA.h"
#include "computer/RTC.h"
#include "computer/VIC.h"
#include "support/fat16_image.h"

namespace {

// The kernel's SOUND_ENABLE flag. Boot sets it; a headless harness has to, or
// every tone takes the muted path.
constexpr uint16_t kSoundEnable = 0x0029;

// 4 MHz / 60 Hz. The game is paced by a jiffy accumulator, so the harness has to
// advance real cycles and pulse the timer -- counting instructions would run the
// simulation at whatever speed the instruction mix happened to give.
constexpr uint64_t kCyclesPerJiffy = 4'000'000ull / 60ull;

using mfcdos_test::Fat16File;
using mfcdos_test::Fat16ImageBuilder;
using mfcdos_test::Fat16ImageReader;

class KpanicTest : public ::testing::Test {
protected:
    /* What is on the disk when the game starts. The game keeps its score table
       there, so every test gets a disk of its own: the drive's default is the
       real ../disk.img, and a test that died with a score would have written
       its table onto the disk the machine boots from. */
    virtual std::vector<Fat16File> diskFiles() { return {}; }

    void TearDown() override
    {
        if (!image_path_.empty()) std::remove(image_path_.c_str());
    }

    /* The disk as it is now, for reading back what the game wrote. */
    std::vector<uint8_t> diskFile(const std::string &name)
    {
        std::ifstream f(image_path_, std::ios::binary);
        std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
        std::vector<uint8_t> out;
        if (!Fat16ImageReader(img).read(name, out)) out.clear();
        return out;
    }

    void SetUp() override
    {
        image_path_ = (std::filesystem::temp_directory_path() /
                       ("mfc_kpanic_" + std::to_string(::getpid()) + "_" +
                        std::to_string(++counter_) + ".img")).string();
        const std::vector<uint8_t> img = Fat16ImageBuilder::build(diskFiles());
        std::ofstream out(image_path_, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(img.data()),
                  static_cast<std::streamsize>(img.size()));
        out.close();
        c.getBlockDevice()->setImagePath(image_path_);

        c.power_on();
        cpu = c.getCpu();
        mem = c.getMemory();
        pia = c.getPia();
        vic = c.getVideoChip();

        // Pin the clock. KPANIC seeds its xorshift from rng_seed(), which folds
        // the RTC, so on the real clock every run lays out different terrain --
        // and an assertion like "the ship is not dead after startRun" then
        // depends on whether this run happened to drop an obstacle in the way.
        // A fixed instant makes the whole run reproducible.
        c.getRtc()->setTimeProvider([] { return static_cast<std::time_t>(1'000'000'000); });
        c.getRtc()->latch();

        std::ifstream f("../kernel/kpanic.bin", std::ios::binary);
        ASSERT_TRUE(f.good()) << "kpanic.bin not found - build the kpanic_bin target";
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
        // does not: interrupts enabled (RESET leaves I set, and without the timer
        // IRQ a jiffy-paced game sits on its title screen forever), and the sound
        // flag the kernel sets during boot.
        cpu->setFlag(Computer::CPU6502::kInterrupt, false);
        mem->write(kSoundEnable, 0x01);
    }

    /* Advance `jiffies` 60 Hz ticks, pulsing the interval timer at each boundary.
       The GUI does this from MainWindow; Computer6502::run() does not, so a
       headless test must. */
    void run(int jiffies)
    {
        for (int i = 0; i < jiffies; i++) {
            const uint64_t until = cpu->getCycles() + kCyclesPerJiffy;
            while (cpu->getCycles() < until) c.runInstructions(1);
            tick();
        }
    }

    void pressKey(char ch) { pia->addKeypress(ch); }

    /* Title screen -> a running conduit. 'S' starts; the run then needs a few
       jiffies to generate its first screen of terrain. */
    void startRun()
    {
        run(20);                    // title screen drawn
        pressKey('S');
        // Two seconds, not a token few frames: the playfield pre-fills a screen of
        // terrain before the HUD's first paint, so a shorter wait lands on a screen
        // that is genuinely mid-startup and reads as a missing HUD.
        run(120);
    }

    static const std::map<std::string, uint16_t> &labels()
    {
        static std::map<std::string, uint16_t> m = [] {
            std::map<std::string, uint16_t> out;
            std::ifstream f("../kernel/kpanic.lbl");
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
                                      << " -- is kpanic.lbl current?";
        return it == labels().end() ? 0 : it->second;
    }

    uint8_t peek(const char *name, int index = 0)
    {
        return mem->read(static_cast<uint16_t>(sym(name) + index));
    }

    void poke(const char *name, uint8_t v, int index = 0)
    {
        mem->write(static_cast<uint16_t>(sym(name) + index), v);
    }

    /* A two-byte little-endian value, which is how cc65 lays out `unsigned int`. */
    unsigned peek16(const char *name)
    {
        return static_cast<unsigned>(peek(name)) |
               (static_cast<unsigned>(peek(name, 1)) << 8);
    }

    void poke16(const char *name, unsigned v)
    {
        poke(name, static_cast<uint8_t>(v & 0xFF));
        poke(name, static_cast<uint8_t>(v >> 8), 1);
    }

    /* Call a one-argument C function in the blob and return when it does.
     *
     * cc65 passes the last parameter of a non-variadic function in A/X, low byte
     * in A. Returning is detected by the stack pointer coming back to where it
     * started rather than by watching for an address, so it survives whatever the
     * callee pushes. This exists because some properties -- score arithmetic at
     * its ceiling, say -- cannot be provoked by playing: passive flight scores
     * nothing, so a test that waited for a scoring event would pass against a
     * broken build as readily as a fixed one. */
    void call1(const char *fn, unsigned arg)
    {
        const uint8_t sp0 = cpu->reg.SP;
        cpu->pushByte(0xFF);            // sentinel return address
        cpu->pushByte(0xFF);
        cpu->reg.A = static_cast<uint8_t>(arg & 0xFF);
        cpu->reg.X = static_cast<uint8_t>(arg >> 8);
        cpu->reg.PC = sym(fn);
        for (int guard = 0; guard < 200000; guard++) {
            c.runInstructions(1);
            if (cpu->reg.SP == sp0) return;
        }
        ADD_FAILURE() << fn << " did not return within 200k instructions";
    }

    /* One row of the VIC's character plane as text, for reading the HUD and the
       end screen the way a player does. */
    std::string screenRow(int row)
    {
        std::string out;
        for (int x = 0; x < 80; x++)
            out.push_back(static_cast<char>(
                c.getVideoChip()->getCharacterAt(static_cast<uint16_t>(x),
                                            static_cast<uint16_t>(row))));
        return out;
    }

    std::string screenText()
    {
        std::string out;
        for (int y = 0; y < 25; y++) { out += screenRow(y); out.push_back('\n'); }
        return out;
    }

    Computer::Computer6502 c;
    Computer::CPU6502 *cpu = nullptr;
    Computer::Memory *mem = nullptr;
    /* One 60 Hz boundary. The PIA's timer IRQ and the VIC's end-of-frame are a
       single event in the machine -- Computer6502::runCycles ticks them together
       -- so a harness faking one must fake the other, or a program blocked in
       K_WAIT_FRAME never wakes. One helper so no site can forget half of it. */
    void tick() { pia->pulseTimerIrq(); vic->endFrame(); }

    Computer::PIA *pia = nullptr;
    Computer::VIC *vic = nullptr;
    std::string image_path_;
    static int counter_;
};

int KpanicTest::counter_ = 0;

/* The blob boots to its own title screen rather than the DOS. This is the
   harness proving itself: if this fails, nothing below means anything. */
TEST_F(KpanicTest, TheGameOpensOnItsTitleScreen)
{
    run(20);
    const std::string s = screenText();
    EXPECT_NE(s.find("K E R N E L"), std::string::npos) << s;
    EXPECT_NE(s.find("P A N I C"), std::string::npos);
    EXPECT_NE(s.find("S to start"), std::string::npos);
}

/* Starting a run leaves the title screen and puts the HUD up with a full tank.
   ENERGY is the game's whole economy, so a run that starts anywhere but full is
   a broken run. */
TEST_F(KpanicTest, StartingARunFillsTheTankAndPaintsTheHud)
{
    startRun();
    EXPECT_EQ(screenText().find("S to start"), std::string::npos)
        << "still on the title screen";
    const std::string hud = screenRow(24);
    EXPECT_NE(hud.find("PWR"), std::string::npos) << hud;
    EXPECT_NE(hud.find("SCORE"), std::string::npos);
    EXPECT_NE(hud.find("DIST"), std::string::npos);
    EXPECT_EQ(peek("_dead"), 0u);
    EXPECT_GT(peek16("_energy"), 0u);
}

/* The conduit scrolls on its own. DIST is the score's twin -- "how far can you
   go" is the game -- so it advancing without input is the core loop running. */
TEST_F(KpanicTest, TheConduitScrollsWithoutInput)
{
    startRun();
    const unsigned before = peek16("_rows");
    run(60);
    EXPECT_GT(peek16("_rows"), before) << "the world did not move in a second";
}

/* Energy drains while flying. It is the timer the whole game is played against:
   without the drain there is no reason to take a node and no way to die. */
TEST_F(KpanicTest, FlyingCostsEnergy)
{
    startRun();
    const unsigned before = peek16("_energy");
    run(120);
    EXPECT_LT(peek16("_energy"), before) << "two seconds of flight cost nothing";
}

/* Running the tank dry ends the run and says so. */
TEST_F(KpanicTest, AnEmptyTankIsAKernelPanic)
{
    startRun();
    poke16("_energy", 1);           // one unit from the end
    run(120);
    EXPECT_EQ(peek("_dead"), 1u) << "an empty tank did not end the run";

    run(60);                        // death blast, then the end screen
    const std::string s = screenText();

    // The headline names the cause, not the game. Asserted on because it is the
    // only reason this screen can appear, so a "KERNEL PANIC" banner here just
    // repeated the title screen.
    for (int r = 8; r <= 16; r++) { std::string q = screenRow(r);
        while (!q.empty() && q.back() == ' ') q.pop_back();
        printf("row %2d: |%s|\n", r, q.c_str()); }
    EXPECT_NE(s.find("ENERGY DEPLETED"), std::string::npos) << s;
    EXPECT_EQ(s.find("KERNEL PANIC"), std::string::npos)
        << "the end screen is repeating the application title";
    EXPECT_NE(s.find("SCORE"), std::string::npos);
    EXPECT_NE(s.find("SECTOR"), std::string::npos);
    EXPECT_NE(s.find("DIST"), std::string::npos);
}

/* The energy economy sets how long a run lasts, which is the balance dial the
   whole game hangs off. Flying passively is the worst case a player can manage,
   so this is a floor: if a hands-off run survives past a minute and a half, the
   drain has stopped meaning anything, and if it dies inside ten seconds there is
   no game to play. Pinned as a window rather than a number because the drain is
   deliberately tuned and a single value would just be a change-detector. */
TEST_F(KpanicTest, AHandsOffRunDiesInAPlayableWindow)
{
    startRun();
    int seconds = 0;
    while (seconds < 120 && peek("_dead") == 0) { run(60); seconds++; }

    EXPECT_EQ(peek("_dead"), 1u) << "a hands-off run never ran out of energy";
    EXPECT_GE(seconds, 10) << "energy drains so fast there is no run to fly";
    EXPECT_LE(seconds, 90) << "energy lasts long enough that the drain is not a clock";
}

/* The score saturates rather than wrapping.
 *
 * The HUD field is five digits, so it can display 99,999, while the variable is
 * 16-bit and stops at 65,535. A wrapping add would put a small number on screen
 * after a large one -- a score nobody had, presented as fact. The wider
 * score was measured at 650 and 1,054 bytes and rejected as unreachable headroom;
 * this is the part of it that was a real defect. */
TEST_F(KpanicTest, TheScoreSaturatesInsteadOfWrapping)
{
    startRun();

    // Ordinary addition still works.
    poke16("_score", 1000);
    call1("_add_score", 150);
    EXPECT_EQ(peek16("_score"), 1150u) << "a normal add did not land";

    // Ten short of the ceiling, plus a firewall: it must pin, not wrap to 139.
    poke16("_score", 0xFFFF - 10);
    call1("_add_score", 150);
    EXPECT_EQ(peek16("_score"), 0xFFFFu)
        << "the score wrapped past its ceiling and reported a smaller number";

    // Already pinned stays pinned.
    call1("_add_score", 40);
    EXPECT_EQ(peek16("_score"), 0xFFFFu);
}

/* The craft's impact flash must actually show, and no sprite may set the
 * reverse-video bit.
 *
 * The flash was once `A_WARN | 0x80` on a glyph sprite. A sprite has no cell
 * behind it to swap with -- DisplayWidget draws sprite pixels in the FOREGROUND
 * colour only -- so bit 7 made resolveCellColors hand back the background, which
 * is black: hitting a wall read as the craft blinking out rather than being hit.
 *
 * Every sprite is a bitmap sprite now, which ignores the attribute, and the flash
 * is its own picture (SL_CRAFT_HIT). The reverse-bit check stays, because a
 * sprite switched back to a glyph would bring the trap straight back, and the
 * flash check now asks for that picture. */
TEST_F(KpanicTest, NoSpriteEverSetsTheReverseBit)
{
    startRun();

    // Hold the craft in its impact flash so draw_craft() takes that branch, then
    // let a frame run so the attribute actually reaches the chip.
    poke("_flash", 3);
    run(2);

    bool saw_flash = false;
    for (uint8_t i = 0; i < 17; i++) {
        const auto &s = c.getVideoChip()->sprite(i);
        if (!s.enabled) continue;
        EXPECT_EQ(s.attr & 0x80, 0)
            << "sprite " << static_cast<int>(i) << " has the reverse bit set, so it "
            << "draws in its background colour -- which is black";
        if (i == 0 && s.bitmap && s.glyph == 1) saw_flash = true;    // SL_CRAFT_HIT
    }
    EXPECT_TRUE(saw_flash) << "the craft was not showing its impact flash, so the "
                              "attribute under test was never exercised";
}

/* A spread volley draws as ONE sprite, not one per shot.
 *
 * One sprite per shot is what once let the spread gun consume 15 of the chip's 17
 * sprites, leaving nothing for the enemies. A volley's shots share a row, a speed
 * and a burn-out row, so each volley is now one picture of its surviving shots,
 * three columns (two pattern slots) wide.
 *
 * Held fire is the heaviest the gun gets, so the shot sprites (1-4) are checked
 * while it runs: every one that is on must be a bitmap sprite, two slots wide,
 * showing a volley picture -- slots 15..42, the bright set and the spent one. */
TEST_F(KpanicTest, ASpreadVolleyIsOneSprite)
{
    startRun();
    poke("_weapon", 1);             // W_SPREAD
    poke("_wammo", 99);
    poke16("_energy", 1000);        // so the run outlives the test

    pia->setKeyState(0x10);         // KS_FIRE, held
    int volleys_seen = 0;
    for (int f = 0; f < 90; f++) {
        run(1);
        for (uint8_t i = 1; i <= 4; i++) {
            const auto &sp = c.getVideoChip()->sprite(i);
            if (!sp.enabled) continue;
            ASSERT_TRUE(sp.bitmap) << "shot sprite " << int(i) << " is a glyph";
            EXPECT_EQ(sp.w, 2) << "a spread volley should be two slots wide";
            EXPECT_GE(sp.glyph, 15); EXPECT_LE(sp.glyph, 42);
            volleys_seen++;
        }
    }
    pia->setKeyState(0);
    EXPECT_GT(volleys_seen, 0) << "holding fire never put a volley on screen";
}

/* Enemies are bitmap sprites in their own range (5-12), showing one of the three
 * enemy pictures or its hit flash. Flown hands-off until one appears; the first
 * spawn comes within a few seconds. */
TEST_F(KpanicTest, EnemiesAreBitmapSprites)
{
    startRun();
    poke16("_energy", 1000);
    bool seen = false;
    for (int f = 0; f < 60 * 10 && !seen; f++) {
        run(1);
        for (uint8_t i = 5; i <= 12; i++) {
            const auto &sp = c.getVideoChip()->sprite(i);
            if (!sp.enabled) continue;
            EXPECT_TRUE(sp.bitmap);
            EXPECT_GE(sp.glyph, 2); EXPECT_LE(sp.glyph, 7);    // SL_DAEMON..SL_FOE_HIT+2
            EXPECT_EQ(sp.w, 1); EXPECT_EQ(sp.h, 1);
            seen = true;
        }
    }
    EXPECT_TRUE(seen) << "no enemy appeared in ten seconds of flight";
}

/* Q quits even straight after a broken arrow sequence.
 *
 * Held arrows fill the keystroke buffer with ESC [ x, and a full buffer drops the tail
 * of one, leaving a lone ESC. The decoder used to swallow whatever byte came next as
 * the rest of that sequence -- so the Q after it vanished, and quitting took two
 * presses. A lone ESC, then Q, must quit. */
TEST_F(KpanicTest, QQuitsAfterABrokenArrowSequence)
{
    startRun();
    ASSERT_EQ(peek("_dead"), 0u);
    ASSERT_NE(screenRow(24).find("PWR"), std::string::npos) << "not in a run";
    pressKey(0x1B);                 // what is left of an arrow whose tail was dropped
    pressKey('q');
    run(30);
    // A quit mid-run leaves the game: the HUD goes and every sprite is switched off.
    EXPECT_EQ(screenRow(24).find("PWR"), std::string::npos)
        << "the Q after a lone ESC was swallowed: still in the run";
    EXPECT_FALSE(c.getVideoChip()->sprite(0).enabled) << "the craft is still up";
}

/* Shots climb at the same rate whatever the throttle.
 *
 * Everything used to move on world steps, and the throttle sets how often a step comes,
 * so at speed the player's own shots flew faster up the screen and throttling back
 * slowed them. The throttle moves the WORLD; the gun is the player's. Shots now move on
 * their own clock, every frame. Measured as the shot sprite's climb in pixels over a
 * fixed number of frames, at the starting speed and then with the throttle opened up. */
TEST_F(KpanicTest, ShotsClimbAtTheSameRateAtAnyThrottle)
{
    auto climb = [&]() {
        run(20);                                    // the previous shot is long gone
        pia->setKeyState(0x10);                     // fire...
        run(1);
        pia->setKeyState(0);                        // ...one shot
        int y0 = -1, y1 = -1;
        for (int f = 0; f < 12; f++) {
            run(1);
            const auto &sp = c.getVideoChip()->sprite(1);
            if (!sp.enabled) continue;
            if (y0 < 0) y0 = sp.y;
            y1 = sp.y;
        }
        return y0 - y1;                             // pixels climbed
    };

    // Rows of world covered in a second: proof the throttle really moved.
    auto world = [&]() {
        const unsigned r0 = peek16("_rows");
        run(60);
        return peek16("_rows") - r0;
    };

    startRun();
    poke16("_energy", 1000);
    const unsigned world_slow = world();
    const int slow = climb();
    for (int i = 0; i < 4; i++) {                   // throttle up: ESC [ A, four times
        pressKey(0x1B); pressKey('['); pressKey('A');
        run(2);
    }
    poke16("_energy", 1000);
    const unsigned world_fast = world();
    const int fast = climb();

    ASSERT_GT(world_fast, world_slow * 2) << "the throttle did not open up -- test is void";
    ASSERT_GT(slow, 0) << "no shot was seen climbing";
    EXPECT_NEAR(fast, slow, 4) << "the throttle changed how fast the shots fly";
}

/* KPANIC asserts its own background rather than wearing the machine's theme.
 *
 * Attributes name palette slots, so a theme loaded by the DOS reaches into any
 * program that has not said otherwise -- and A_BOARD is the default pair, so
 * the inside of the conduit would be whatever colour the shell was wearing.
 * Stating it is one seek and three writes at startup, with nothing to undo: the
 * shell reloads the theme when it takes the screen back.
 *
 * Simulated here by loading a non-black background BEFORE the game runs, which
 * is exactly what arriving from a themed prompt looks like. */
TEST_F(KpanicTest, ItAssertsABlackBackgroundOverAnyTheme)
{
    // A theme is already loaded when the game starts.
    mem->write(Computer::VIC::kRegPaletteIdx, 0);
    mem->write(Computer::VIC::kRegPaletteData, 0x2c);
    mem->write(Computer::VIC::kRegPaletteData, 0x1c);
    mem->write(Computer::VIC::kRegPaletteData, 0x15);

    uint8_t r = 0, g = 0, b = 0;
    c.getVideoChip()->paletteColor(0, r, g, b);
    ASSERT_EQ(r, 0x2c) << "the stand-in theme did not load";

    run(20);            // the game's startup is all this needs

    c.getVideoChip()->paletteColor(0, r, g, b);
    EXPECT_EQ(r, 0x00); EXPECT_EQ(g, 0x00); EXPECT_EQ(b, 0x00)
        << "KPANIC inherited the theme's background instead of stating its own";
}


/* ---- the score table ---------------------------------------------------- */

/* A run that scores takes a name on the end screen and is written to
   KPANIC.SCO, in the format the header documents. */
TEST_F(KpanicTest, AScoringRunIsNamedAndWrittenToTheDisk)
{
    startRun();
    poke16("_score", 500);
    poke16("_energy", 1);
    run(180);                       // dead, the blast, the end screen
    ASSERT_EQ(peek("_dead"), 1u);
    std::string s = screenText();
    ASSERT_NE(s.find("type your name"), std::string::npos) << s;

    for (const char *p = "ADA"; *p; p++) { pressKey(*p); run(2); }
    pressKey(0x1B); pressKey('['); pressKey('D');   // an arrow types nothing
    run(4);
    pressKey('\r');
    run(30);

    s = screenText();
    EXPECT_NE(s.find("ADA "), std::string::npos) << s;
    EXPECT_EQ(s.find("ADAk"), std::string::npos) << "an arrow was typed as a letter";
    EXPECT_EQ(s.find("not saved"), std::string::npos) << s;
    EXPECT_EQ(peek("_hs_n"), 1u);

    const std::vector<uint8_t> f = diskFile("KPANIC.SCO");
    ASSERT_EQ(f.size(), 3u + 10u + 2u + 2u + 1u) << "no table, or the wrong size";
    EXPECT_EQ(f[0], 'K');
    EXPECT_EQ(f[1], 1u);
    EXPECT_EQ(f[2], 1u);
    EXPECT_EQ(std::string(f.begin() + 3, f.begin() + 13), "ADA       ");
    EXPECT_EQ(f[13] | (f[14] << 8), 500);
    EXPECT_EQ(unsigned(f[15] | (f[16] << 8)), peek16("_rows"));
}

/* A run that scored nothing does not place, and the disk is left alone. */
TEST_F(KpanicTest, AScorelessRunLeavesTheDiskAlone)
{
    startRun();
    poke16("_energy", 1);
    run(180);
    ASSERT_EQ(peek("_dead"), 1u);
    const std::string s = screenText();
    EXPECT_EQ(s.find("type your name"), std::string::npos) << s;
    EXPECT_NE(s.find("none yet"), std::string::npos) << s;
    EXPECT_TRUE(diskFile("KPANIC.SCO").empty()) << "a scoreless run wrote the table";
}

/* A table on the disk is read at start-up, shown on the title screen, and a new
   run is ranked into it rather than appended. */
class KpanicTableTest : public KpanicTest {
protected:
    static void entry(std::vector<uint8_t> &f, const char *name, unsigned score,
                      unsigned dist, uint8_t sector)
    {
        std::string n(name);
        n.resize(10, ' ');
        f.insert(f.end(), n.begin(), n.end());
        f.push_back(score & 0xFF); f.push_back(score >> 8);
        f.push_back(dist & 0xFF);  f.push_back(dist >> 8);
        f.push_back(sector);
    }
    std::vector<Fat16File> diskFiles() override
    {
        std::vector<uint8_t> f = {'K', 1, 2};
        entry(f, "GRACE", 900, 400, 2);
        entry(f, "LINUS", 300, 200, 1);
        return {{"KPANIC.SCO", f, ""}};
    }
};

TEST_F(KpanicTableTest, TheTableIsLoadedShownAndRankedInto)
{
    run(20);
    EXPECT_EQ(peek("_hs_n"), 2u);
    std::string s = screenText();
    EXPECT_NE(s.find("BEST  GRACE"), std::string::npos) << s;

    pressKey('S');
    run(120);
    poke16("_score", 500);          // between the two
    poke16("_energy", 1);
    run(180);
    ASSERT_EQ(peek("_dead"), 1u);
    pressKey('B'); run(2);
    pressKey('\r'); run(30);

    s = screenText();
    const size_t g = s.find("GRACE"), b = s.find("B "), l = s.find("LINUS");
    ASSERT_NE(g, std::string::npos); ASSERT_NE(b, std::string::npos); ASSERT_NE(l, std::string::npos);
    EXPECT_LT(g, b); EXPECT_LT(b, l) << s;
    EXPECT_NE(s.find("STACK"), std::string::npos) << "GRACE's sector was not read";

    const std::vector<uint8_t> f = diskFile("KPANIC.SCO");
    ASSERT_EQ(f.size(), 3u + 3u * 15u);
    EXPECT_EQ(f[2], 3u);
    EXPECT_EQ(std::string(f.begin() + 18, f.begin() + 28), "B         ");
}

/* Someone else's file under the name -- or a future format -- is not misread:
   the game starts with an empty table. */
class KpanicForeignTableTest : public KpanicTest {
protected:
    std::vector<Fat16File> diskFiles() override
    {
        return {{"KPANIC.SCO", {'F', 1, 8, 'x', 'y'}, ""}};
    }
};

TEST_F(KpanicForeignTableTest, AFileOfAnotherFormatGivesAnEmptyTable)
{
    run(20);
    EXPECT_EQ(peek("_hs_n"), 0u);
    EXPECT_EQ(screenText().find("BEST"), std::string::npos);
}

} // namespace
