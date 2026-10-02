/**
 * @file test_dos_startup.cpp
 * @brief The boot config: SYSTEM/STARTUP.CFG.
 *
 * A text file of ordinary DOS commands, run by _DOS_COLD before the sign-on.
 * There is no config parser -- _DOS_DISPATCH already reads MON_CMDBUF, so the
 * runner fills that buffer from a file instead of the keyboard and every verb
 * the shell has works.
 *
 * These tests need the disk in place BEFORE the machine boots, so each builds
 * its own Computer6502 rather than sharing a booted one -- the same reason
 * testShutdown() in the integration suite owns its machine.
 *
 * What is worth pinning here is the failure behaviour, not the happy path: a
 * config file is the one thing on the disk that can stop the machine reaching a
 * prompt, so every test below checks the prompt still arrives.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "computer/BlockDevice.h"
#include "computer/Computer6502.h"
#include "computer/Memory.h"
#include "computer/PIA.h"
#include "computer/VIC.h"
#include "support/fat16_image.h"

namespace {

using mfcdos_test::Fat16File;
using mfcdos_test::Fat16ImageBuilder;

// Boot takes the RESET path, a 12KB window wipe and the sign-on box before the
// shell is listening; the config runs inside that.
constexpr int kBootInstructions = 400000;

class DosStartupTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        if (!image_path_.empty()) std::remove(image_path_.c_str());
    }

    /* Put a disk in the drive with the given SYSTEM/STARTUP.CFG, then power on.
       Pass an empty string for "no config file at all", which must be the silent
       case -- most disks will not have one. */
    void bootWith(const std::string &cfg, bool press_escape = false,
                  std::vector<Fat16File> files = {})
    {
        if (!image_path_.empty()) std::remove(image_path_.c_str());   // a second boot
        files.push_back({"README.TXT", {'h', 'i', '\r', '\n'}, ""});
        if (!cfg.empty())
            files.push_back({"STARTUP.CFG",
                             std::vector<uint8_t>(cfg.begin(), cfg.end()),
                             "SYSTEM"});

        image_path_ = (std::filesystem::temp_directory_path() /
                       ("mfcdos_startup_" + std::to_string(++counter_) + ".img"))
                          .string();
        const std::vector<uint8_t> img = Fat16ImageBuilder::build(files);
        std::ofstream f(image_path_, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char *>(img.data()),
                static_cast<std::streamsize>(img.size()));
        f.close();

        box_.getBlockDevice()->setImagePath(image_path_);
        box_.power_on();
        // ESC has to be in the keystroke buffer when _DOS_STARTUP looks, which is
        // early in the cold path -- so it goes in before the machine runs, the
        // same as pressing it while the kernel is still clearing memory.
        if (press_escape) box_.getPia()->addKeypress(0x1B);
        box_.runInstructions(kBootInstructions);
    }

    std::string screen()
    {
        std::string out;
        for (int y = 0; y < 25; y++) {
            for (int x = 0; x < 80; x++)
                out.push_back(static_cast<char>(
                    box_.getVideoChip()->getCharacterAt(static_cast<uint16_t>(x),
                                                        static_cast<uint16_t>(y))));
            out.push_back('\n');
        }
        return out;
    }

    Computer::Computer6502 box_;
    std::string image_path_;
    static int counter_;
};

int DosStartupTest::counter_ = 0;

/* No config file is the ordinary case and must be completely invisible. */
TEST_F(DosStartupTest, ADiskWithNoConfigBootsExactlyAsBefore)
{
    bootWith("");
    const std::string s = screen();
    EXPECT_NE(s.find("OPERATIONAL"), std::string::npos) << s;
}

/* The kernel reports what is installed before the DOS signs on: its own segments
   from the linker, and every ROM that carries an identity block (romid.inc). The
   ROM lines are read off the ROMs, so this also proves each block is in place.

   Paced, on emulated time (runCycles ticks the timer): the chime plays out first,
   ~1.5 s, then a line every half second, then a half second's hold and a clear --
   so the header is up at ~1.5 s, the DOS line at ~5 s, and the screen is clear at
   ~5.5 s.
   Sampled every quarter second, and judged at instants clear of those edges. */
TEST_F(DosStartupTest, TheBootReportIsPacedAndNamesTheSegmentsAndTheRoms)
{
    bootWith("");                       // the disk; the boot itself is re-run below
    box_.power_on();
    constexpr uint64_t kQuarter = 1'000'000;            // 4 MHz
    std::string seen, at1, at2, at5_25, at8;
    for (int q = 1; q <= 32; q++) {                     // 8 s
        box_.runCycles(kQuarter);
        const std::string s = screen();
        seen += s;
        if (q == 4)  at1 = s;
        if (q == 8)  at2 = s;
        if (q == 21) at5_25 = s;
        if (q == 32) at8 = s;
    }

    EXPECT_EQ(at1.find("MFC 6502 KERNEL"), std::string::npos)
        << "the report did not wait for the chime\n" << at1;
    EXPECT_NE(at2.find("MFC 6502 KERNEL"), std::string::npos) << at2;
    EXPECT_EQ(at2.find("DOS ROM installed"), std::string::npos)
        << "the lines were not paced\n" << at2;
    EXPECT_NE(at5_25.find("DOS ROM installed"), std::string::npos) << at5_25;

    EXPECT_NE(seen.find("CODE segment loaded at  $F000-$F"), std::string::npos);
    EXPECT_NE(seen.find("JUMPS segment loaded at $FF00-$FF47 (72 bytes)"), std::string::npos);
    EXPECT_NE(seen.find("VECS segment loaded at  $FFFA-$FFFF (6 bytes)"), std::string::npos);
    EXPECT_NE(seen.find("BASIC ROM installed as module bank 1 (16384 bytes)"), std::string::npos);
    EXPECT_NE(seen.find("FORTH ROM installed as module bank 3 (16384 bytes)"), std::string::npos);
    EXPECT_NE(seen.find("MONITOR ROM installed as module bank 4 (16384 bytes)"), std::string::npos);
    EXPECT_NE(seen.find("DOS ROM installed at $8800-$AFFF (10240 bytes)"), std::string::npos);
    EXPECT_EQ(seen.find("bank 2"), std::string::npos) << "an empty bank was reported";

    // Then a clear screen, and the sign-on as it always was.
    EXPECT_EQ(at8.find("ROM installed"), std::string::npos) << "the report was not cleared\n" << at8;
    EXPECT_NE(at8.find("OPERATIONAL"), std::string::npos) << at8;
}

/* Without a timer -- the CPU run on its own, as most harnesses here do -- the
   pauses are skipped rather than waited on forever: the same boot reaches the same
   cleared screen and sign-on. */
TEST_F(DosStartupTest, WithoutATimerTheReportIsNotPacedAndStillCleared)
{
    bootWith("");
    const std::string s = screen();
    EXPECT_EQ(s.find("ROM installed"), std::string::npos) << s;
    EXPECT_NE(s.find("OPERATIONAL"), std::string::npos) << s;
}

/* The chime is started at reset and the DOS stops it before running a program:
   the kernel's IRQ must never write the SID under one. Without a timer the boot
   leaves it armed, which is exactly the state a program launch would find. */
TEST_F(DosStartupTest, TheBootChimeIsArmedAndALaunchStopsIt)
{
    bootWith("");
    EXPECT_NE(box_.getMemory()->read(0x02E0), 0) << "reset did not start the chime";

    // A program that returns at once ($0800: RTS), run from the config.
    bootWith("NOP\r\n", false, {{"NOP.PRG", {0x00, 0x08, 0x60}, ""}});
    EXPECT_EQ(box_.getMemory()->read(0x02E0), 0)
        << "a program was run with the chime still playing";
}

/* RESTART starts the machine again from the reset vector: the kernel's RESET runs,
   so the chime is started afresh and the DOS signs on again. The chime is stopped
   by hand first, so a re-armed chime can only have come from the restart. */
TEST_F(DosStartupTest, RestartRunsTheBootAgain)
{
    bootWith("");
    box_.getMemory()->write(0x02E0, 0);             // CHIME_ON: as if it had played out
    for (char c : std::string("RESTART\r")) box_.getPia()->addKeypress(c);
    box_.runInstructions(kBootInstructions);
    EXPECT_NE(box_.getMemory()->read(0x02E0), 0) << "the restart did not run RESET";
    EXPECT_NE(screen().find("OPERATIONAL"), std::string::npos) << screen();
}

/* The DOS prompt puts the clock back to full speed, so a program that slows the
   machine (CPU_SPEED, $FED4) and exits -- or is stopped -- cannot leave the shell at
   1 MHz. Simulated by setting it slow at the prompt and running one command. */
TEST_F(DosStartupTest, ThePromptPutsTheClockBackToFullSpeed)
{
    bootWith("");
    box_.getMemory()->write(0xFED4, 0x01);
    ASSERT_EQ(box_.clockHz(), 1000000u);
    for (char c : std::string("VERSION\r")) box_.getPia()->addKeypress(c);
    box_.runInstructions(100000);
    EXPECT_EQ(box_.getMemory()->read(0xFED4), 0x00) << "the prompt left the machine slow";
    EXPECT_EQ(box_.clockHz(), 4000000u);
}

/* HELP lists RESTART and SHUTDOWN. SHUTDOWN's help line was written when the verb
   was, and never put in the table, so HELP did not mention it. HELP pages, so the
   screens are collected across the --MORE-- break. */
TEST_F(DosStartupTest, HelpListsRestartAndShutdown)
{
    bootWith("");
    std::string seen;
    for (char c : std::string("HELP\r")) box_.getPia()->addKeypress(c);
    for (int i = 0; i < 300; i++) { box_.runInstructions(1000); seen += screen(); }
    box_.getPia()->addKeypress(' ');                // past the page break
    for (int i = 0; i < 300; i++) { box_.runInstructions(1000); seen += screen(); }
    EXPECT_NE(seen.find("RESTART"), std::string::npos);
    EXPECT_NE(seen.find("SHUTDOWN"), std::string::npos);
}

/* The point of the whole feature: a command in the file takes effect. OPEN is
   the one with visible state -- the prompt prints the drawer name before ']'. */
TEST_F(DosStartupTest, ACommandInTheConfigRuns)
{
    bootWith("OPEN SYSTEM\r\n");
    const std::string s = screen();
    EXPECT_NE(s.find("SYSTEM]"), std::string::npos)
        << "the config did not open the drawer\n" << s;
    EXPECT_NE(s.find("OPERATIONAL"), std::string::npos)
        << "the sign-on did not survive the config";
}

/* Comments and blank lines are skipped, and a file made only of them is silent.
   This is what makes a shipped, self-documenting default config possible. */
TEST_F(DosStartupTest, CommentsAndBlankLinesAreIgnored)
{
    bootWith("# a comment\r\n\r\n# another\r\n");
    const std::string s = screen();
    EXPECT_EQ(s.find("ERROR"), std::string::npos) << s;
    EXPECT_NE(s.find("OPERATIONAL"), std::string::npos);
}

/* The sign-on has to come out BELOW whatever the config printed. _DOS_SPLASH
   does not clear the screen, so running the config first leaves the box where it
   always is -- immediately above the prompt -- instead of scrolled away. */
TEST_F(DosStartupTest, TheSignOnLandsBelowTheConfigOutput)
{
    bootWith("OPEN SYSTEM\r\n");
    const std::string s = screen();
    const size_t oper = s.find("OPERATIONAL");
    const size_t prompt = s.rfind("SYSTEM]");
    ASSERT_NE(oper, std::string::npos) << s;
    ASSERT_NE(prompt, std::string::npos) << s;
    EXPECT_LT(oper, prompt) << "the sign-on is not above the prompt\n" << s;
}

/* A line the shell cannot make sense of must not stop the boot. This is the
   failure a config file introduces that nothing else on the disk can. */
TEST_F(DosStartupTest, ABadLineDoesNotStopTheBoot)
{
    bootWith("NOSUCHVERB\r\nOPEN SYSTEM\r\n");
    EXPECT_NE(screen().find("SYSTEM]"), std::string::npos)
        << "a bad line stopped the rest of the config, or the boot\n" << screen();
}

/* CATALOG prints more than a page. With the pager left on, the boot would stop
   at --MORE-- waiting for a key before a prompt had ever appeared. */
TEST_F(DosStartupTest, ALongCommandDoesNotStallTheBootOnTheMorePrompt)
{
    bootWith("CATALOG\r\n");
    const std::string s = screen();
    EXPECT_EQ(s.find("MORE"), std::string::npos)
        << "the boot stopped at the pager\n" << s;
    EXPECT_NE(s.find("OPERATIONAL"), std::string::npos);
}

/* ESC skips the file. Without this, one bad line means rebuilding the disk from
   the host to get the machine back. */
TEST_F(DosStartupTest, EscapeAtBootSkipsTheConfigEntirely)
{
    bootWith("OPEN SYSTEM\r\n", /*press_escape=*/true);
    const std::string s = screen();
    EXPECT_EQ(s.find("SYSTEM]"), std::string::npos)
        << "ESC did not skip the config\n" << s;
    EXPECT_NE(s.find("OPERATIONAL"), std::string::npos);
}

/* A config of comments must stay nearly free.
 *
 * The reader skips blanks and comments inside ONE open; only a line that will be
 * dispatched needs the file re-opened, because only a dispatch can steal the
 * cursor (the FS holds one open file). Filtering in the caller instead made the
 * cost grow with the number of lines rather than commands -- fifteen comment
 * lines cost 122,000 extra instructions to boot, and 11,000 after the fix.
 * Asserted as a ceiling rather than a figure: it is a shape, not a benchmark --
 * and measured against the same boot with no config, so what the rest of the
 * boot costs (the kernel's boot report, say) does not move the line. */
TEST_F(DosStartupTest, ACommentOnlyConfigBarelyCostsAnything)
{
    const std::string cfg = [] {
        std::string s;
        for (int i = 0; i < 15; i++) s += "# comment line\r\n";
        return s;
    }();

    // Instructions from power-on to the ']' prompt, on the disk bootWith made.
    auto bootCost = [this] {
        long n = 0;
        Computer::Computer6502 b;
        b.getBlockDevice()->setImagePath(image_path_);
        b.power_on();
        for (; n < 2000000; n += 1000) {
            b.runInstructions(1000);
            bool at_prompt = false;
            for (int y = 0; y < 25 && !at_prompt; y++)
                if (b.getVideoChip()->getCharacterAt(0, y) == ']') at_prompt = true;
            if (at_prompt) break;
        }
        return n;
    };

    bootWith("");
    const long bare = bootCost();

    bootWith(cfg);
    EXPECT_NE(screen().find("OPERATIONAL"), std::string::npos)
        << "a comment-only config did not reach the sign-on";

    // The shipped default is comments only, so this is the cost every disk pays.
    const long extra = bootCost() - bare;
    EXPECT_LT(extra, 40000L)
        << "fifteen comment lines cost " << extra << " instructions to boot; the "
        << "reader is re-opening the file per line again";
}

/* A theme in the boot config, which is the whole persistence story: the machine
   has no settings file of its own and does not need one, because STARTUP.CFG is
   already a list of commands it runs at boot. */
TEST_F(DosStartupTest, AThemeInTheConfigIsLoadedAtBoot)
{
    bootWith("THEME AMBER\r\n");

    uint8_t r = 0, g = 0, b = 0;
    box_.getVideoChip()->paletteColor(2, r, g, b);   // normal text
    EXPECT_EQ(r, 0xff); EXPECT_EQ(g, 0xcc); EXPECT_EQ(b, 0x2f)
        << "the config did not load the theme";

    box_.getVideoChip()->paletteColor(0, r, g, b);   // background
    EXPECT_EQ(r, 0x2c); EXPECT_EQ(g, 0x1c); EXPECT_EQ(b, 0x15);

    // ...and the machine still got to a prompt wearing it.
    EXPECT_NE(screen().find("OPERATIONAL"), std::string::npos);
}

/* No theme named means the machine's own colours, so an untouched disk boots
   looking exactly as it always has. */
TEST_F(DosStartupTest, WithNoThemeTheMachineKeepsItsOwnColours)
{
    bootWith("");
    uint8_t r = 0, g = 0, b = 0;
    box_.getVideoChip()->paletteColor(2, r, g, b);
    EXPECT_EQ(r, 0x00); EXPECT_EQ(g, 0xff); EXPECT_EQ(b, 0x00) << "green moved";
    box_.getVideoChip()->paletteColor(0, r, g, b);
    EXPECT_EQ(r, 0x00); EXPECT_EQ(g, 0x00); EXPECT_EQ(b, 0x00) << "black moved";
}

/* The theme survives a disk write.
 *
 * DOS_THEME was first placed at $03BC, which is DOS_W_ERR -- the write-error
 * flag -- so every SAVE stamped the theme index back to zero and the machine
 * silently reverted to GREEN. It showed up as "FRONTIER resets the theme when
 * you quit", because that game writes a high-score file on the way out; any
 * write does it. Page 3 is nearly full and the two were allocated weeks apart,
 * which is exactly the collision a test can hold shut and a survey cannot. */
TEST_F(DosStartupTest, AThemeSurvivesADiskWrite)
{
    bootWith("THEME AMBER\r\n");

    uint8_t r = 0, g = 0, b = 0;
    box_.getVideoChip()->paletteColor(2, r, g, b);
    ASSERT_EQ(r, 0xff) << "the config did not set the theme in the first place";

    for (char c : std::string("SAVE T.BIN,0900-090F\r"))
        box_.getPia()->addKeypress(c);
    box_.runInstructions(1500000);

    box_.getVideoChip()->paletteColor(2, r, g, b);
    EXPECT_EQ(r, 0xff); EXPECT_EQ(g, 0xcc); EXPECT_EQ(b, 0x2f)
        << "a disk write reverted the theme -- DOS_THEME is sharing an address";
}

} // namespace
