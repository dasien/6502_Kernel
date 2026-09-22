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
    void bootWith(const std::string &cfg, bool press_escape = false)
    {
        std::vector<Fat16File> files;
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
 * Asserted as a ceiling rather than a figure: it is a shape, not a benchmark. */
TEST_F(DosStartupTest, ACommentOnlyConfigBarelyCostsAnything)
{
    const std::string cfg = [] {
        std::string s;
        for (int i = 0; i < 15; i++) s += "# comment line\r\n";
        return s;
    }();

    bootWith(cfg);
    EXPECT_NE(screen().find("OPERATIONAL"), std::string::npos)
        << "a comment-only config did not reach the sign-on";

    // The shipped default is comments only, so this is the cost every disk pays.
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
    EXPECT_LT(n, 180000L)
        << "fifteen comment lines cost " << n << " instructions to boot; the "
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
