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

namespace {

// The kernel's SOUND_ENABLE flag. Boot sets it; a headless harness has to, or
// every tone takes the muted path.
constexpr uint16_t kSoundEnable = 0x0029;

// 4 MHz / 60 Hz. The game is paced by a jiffy accumulator, so the harness has to
// advance real cycles and pulse the timer -- counting instructions would run the
// simulation at whatever speed the instruction mix happened to give.
constexpr uint64_t kCyclesPerJiffy = 4'000'000ull / 60ull;

class KpanicTest : public ::testing::Test {
protected:
    void SetUp() override
    {
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
};

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

/* The craft's impact flash must not set the reverse-video bit.
 *
 * A sprite has no cell behind it to swap with -- DisplayWidget draws sprite
 * pixels in the FOREGROUND colour only -- so bit 7 makes resolveCellColors hand
 * back the background instead, and every attribute this game uses has a black
 * background. The flash was `A_WARN | 0x80`, which drew the ship in black pixels:
 * hitting a wall read as the craft blinking out rather than being hit.
 *
 * Reported from play, not found here; this exists so it cannot come back. The
 * assertion is deliberately about the whole sprite set, because the bit is wrong
 * on ANY sprite for the same reason. */
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
        if (s.glyph == 15) saw_flash = true;      // G_BLAST, the impact glyph
    }
    EXPECT_TRUE(saw_flash) << "the craft was not showing its impact flash, so the "
                              "attribute under test was never exercised";
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

} // namespace
