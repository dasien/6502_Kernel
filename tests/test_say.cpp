/**
 * @file test_say.cpp
 * @brief SAY (programs/say): S.A.M. ported to MFC, driven on the emulated machine.
 *
 * Two things can go wrong in a port like this, and each has a test.
 *
 * The rules. RECITER is a few thousand bytes of English-to-phoneme rules reached
 * through pointer tables, and the port moved all of it -- new addresses, new zero
 * page. A table entry left pointing at the C64's address would not crash; it would
 * quietly mispronounce a class of words. So RECITER is run on four hundred words
 * from the JavaScript port's fixtures and its phonemes compared exactly.
 *
 * The clock. SAM's loops ARE its sample clock, written for a 1 MHz 6502. Run at
 * MFC's 4 MHz it would speak four times too fast and two octaves up, and still
 * "work". So an utterance is rendered through the real SID on machine time and
 * its length and pitch compared with the JavaScript port's rendering of the same
 * phonemes, which runs SAM's timing at a fixed 22,050 Hz.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
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
#include "computer/VIC.h"
#include "computer/SID.h"

namespace {

constexpr int kBootInstructions = 400000;
constexpr uint16_t kArgBuf = 0x0382;        // DOS_ARGBUF: the launch command tail
constexpr uint8_t kSamEol = 0x9B;
constexpr uint16_t kChimeOn = 0x02E0;      // the kernel's boot chime switch

class SayTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // No disk: SAY never opens a file, and the drive's default is the real one.
        c.getBlockDevice()->setImagePath(
            (std::filesystem::temp_directory_path() / "mfc_say_nodisk.img").string());
        c.power_on();
        c.runInstructions(kBootInstructions);   // the kernel and DOS set up (unpaced)
        cpu = c.getCpu();
        mem = c.getMemory();

        std::ifstream f("../kernel/say.bin", std::ios::binary);
        ASSERT_TRUE(f.good()) << "say.bin not found - build the say_bin target";
        const std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());
        for (size_t i = 0; i < blob.size(); ++i)
            mem->write(static_cast<uint16_t>(0x0800 + i), blob[i]);
    }

    static const std::map<std::string, uint16_t> &labels()
    {
        static std::map<std::string, uint16_t> m = [] {
            std::map<std::string, uint16_t> out;
            std::ifstream f("../kernel/say.lbl");
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
        const auto it = labels().find(name);
        EXPECT_NE(it, labels().end()) << "no label " << name << " -- is say.lbl current?";
        return it == labels().end() ? 0 : it->second;
    }

    /* RECITER on one line of English. Runs SAM's machine-language entry and stops
       where RECITER hands its phonemes to SAM (say_main), reading them out of the
       buffer before anything is spoken. Empty if RECITER refused the text. */
    std::string recite(const std::string &text)
    {
        const uint16_t input = sym("L9a15_input");
        for (size_t i = 0; i < text.size(); ++i)
            mem->write(static_cast<uint16_t>(input + i), static_cast<uint8_t>(text[i]));
        mem->write(static_cast<uint16_t>(input + text.size()), kSamEol);

        cpu->reg.SP = 0xFF;
        cpu->pushByte(0xFF);                    // a return address nothing will reach
        cpu->pushByte(0xFE);
        const uint8_t sp0 = 0xFD;
        cpu->reg.PC = sym("L9a09_Reciter_ML_entry");
        const uint16_t say_main = sym("S9b61_say_main");
        for (int guard = 0; guard < 5'000'000; guard++) {
            c.runInstructions(1);
            if (cpu->reg.PC == say_main) {
                std::string out;
                for (int i = 0; i < 256; i++) {
                    const uint8_t b = mem->read(static_cast<uint16_t>(input + i));
                    if (b == kSamEol) break;
                    out.push_back(static_cast<char>(b));
                }
                return out;
            }
            if (cpu->reg.SP == sp0 + 2) return "";   // returned: RECITER refused it
        }
        ADD_FAILURE() << "RECITER did not finish on \"" << text << "\"";
        return "";
    }

    Computer::Computer6502 c;
    Computer::CPU6502 *cpu = nullptr;
    Computer::Memory *mem = nullptr;
};

std::string trim(const std::string &s)
{
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(' ') - a + 1);
}

/* RECITER's rules, moved: four hundred words, exactly the phonemes the reference
   gives. A wrong pointer in the moved rule tables shows up as a class of words. */
TEST_F(SayTest, ReciterMakesTheReferencePhonemes)
{
    std::ifstream f(std::string(MFC_TEST_DATA) + "/sam_reciter.tsv");
    ASSERT_TRUE(f.good()) << "tests/data/sam_reciter.tsv not found";
    std::string line;
    int cases = 0, wrong = 0;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        ASSERT_NE(tab, std::string::npos) << line;
        const std::string in = line.substr(0, tab), want = line.substr(tab + 1);
        const std::string got = trim(recite(in));
        cases++;
        if (got != want && ++wrong <= 10)
            ADD_FAILURE() << "\"" << in << "\": got \"" << got << "\", want \"" << want << "\"";
    }
    EXPECT_GT(cases, 300);
    EXPECT_EQ(wrong, 0) << wrong << " of " << cases << " words came out differently";
}

/* The original, as the reference for the two RECITER cases the fixtures disagree
   on. vendor/c64-sam/lkg/sam.c64 is the unmodified C64 build of the source the
   port came from; it runs on MFC's CPU if nothing is mapped over $0801-$CFFF -- a
   machine that has not been powered on has no ROMs, so all 64K is RAM -- and the
   C64 registers it writes ($01, $D011, $D4xx) are plain memory here. */
std::string reciteOriginal(const std::string &text)
{
    static Computer::Computer6502 bare;            // no power_on: no ROMs mapped
    Computer::Memory *m = bare.getMemory();
    Computer::CPU6502 *p = bare.getCpu();
    static bool loaded = false;
    if (!loaded) {
        std::ifstream f(std::string(MFC_VENDOR) + "/c64-sam/lkg/sam.c64", std::ios::binary);
        const std::vector<uint8_t> prg((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());
        const uint16_t at = static_cast<uint16_t>(prg[0] | (prg[1] << 8));
        for (size_t i = 2; i < prg.size(); ++i) m->write(static_cast<uint16_t>(at + i - 2), prg[i]);
        loaded = true;
    }
    constexpr uint16_t input = 0x9A15, entry = 0x9A09, say_main = 0x9B61;  // sam.c64.lbl
    for (size_t i = 0; i < text.size(); ++i) m->write(static_cast<uint16_t>(input + i), static_cast<uint8_t>(text[i]));
    m->write(static_cast<uint16_t>(input + text.size()), kSamEol);
    p->reg.SP = 0xFF;
    p->pushByte(0xFF);
    p->pushByte(0xFE);
    p->reg.PC = entry;
    for (int guard = 0; guard < 5'000'000; guard++) {
        bare.runInstructions(1);
        if (p->reg.PC == say_main) {
            std::string out;
            for (int i = 0; i < 256; i++) {
                const uint8_t b = m->read(static_cast<uint16_t>(input + i));
                if (b == kSamEol) break;
                out.push_back(static_cast<char>(b));
            }
            return out;
        }
        if (p->reg.SP == 0xFF) return "";
    }
    return "(did not finish)";
}

/* The port says what the original says. The fixtures came from a C port of SAM,
   so they cannot see a difference between SAM and that port; the original build
   can. Every fixture word, and sentences with the punctuation that changes how
   RECITER reads them -- including the '?' rule c64-sam fixed, which the fixtures
   leave out. This is the test that caught the character map: assembled as plain
   ASCII, RECITER's rule-set headers lost the bit 7 its matcher looks for. */
TEST_F(SayTest, ThePortSaysWhatTheOriginalSays)
{
    std::vector<std::string> words = {
        "HELLO, HOW ARE YOU? ", "I AM THE SOFTWARE AUTOMATIC MOUTH. ",
        "1982, 1999 AND 2026! ", "&C, $5 & 10% ", "DON'T PANIC. ",
    };
    std::ifstream f(std::string(MFC_TEST_DATA) + "/sam_reciter.tsv");
    std::string line;
    while (std::getline(f, line))
        if (!line.empty() && line[0] != '#') words.push_back(line.substr(0, line.find('\t')));

    int wrong = 0;
    for (const std::string &w : words) {
        const std::string want = trim(reciteOriginal(w)), got = trim(recite(w));
        if (got != want && ++wrong <= 10)
            ADD_FAILURE() << "\"" << w << "\": original \"" << want << "\", port \"" << got << "\"";
    }
    EXPECT_EQ(wrong, 0) << wrong << " of " << words.size() << " differ from the original";
}

/* The voice, on machine time: SAY launched with a command tail, as the DOS would,
   run through the real SID, with every sample kept. */
class SaySoundTest : public SayTest {
protected:
    /* Run `seconds` of machine time from $0800 with `tail` as the command line, and
       return the SID's output. Ten milliseconds a slice, whatever the clock -- at
       1 MHz a slice is fewer cycles, which is the point. */
    std::vector<int16_t> speak(const std::string &tail, double seconds)
    {
        for (size_t i = 0; i < tail.size(); ++i)
            mem->write(static_cast<uint16_t>(kArgBuf + i), static_cast<uint8_t>(tail[i]));
        mem->write(static_cast<uint16_t>(kArgBuf + tail.size()), 0);
        // The DOS stops the boot chime when it launches a program; jumping to $0800
        // directly does not, and the unpaced boot leaves it armed -- it would play
        // the moment the timer runs, over the end of the speech.
        mem->write(kChimeOn, 0);
        cpu->reg.SP = 0xFF;
        cpu->reg.PC = 0x0800;

        Computer::SID *sid = c.getSid();
        int16_t chunk[4096];
        while (sid->drainSamples(chunk, 4096) > 0) {}      // the boot's leftovers
        std::vector<int16_t> out;
        for (int t = 0; t < static_cast<int>(seconds * 100); t++) {
            c.runCycles(c.clockHz() / 100);
            int n;
            while ((n = sid->drainSamples(chunk, 4096)) > 0) out.insert(out.end(), chunk, chunk + n);
        }
        return out;
    }

    /* How long it speaks: from the first to the last 10 ms window that moves. SAM
       holds the volume at a level between samples, so "silent" is not zero -- it
       is a window with no change in it. */
    static double sounding(const std::vector<int16_t> &s, size_t &first, size_t &last)
    {
        const size_t w = Computer::SID::kSampleRate / 100;
        first = last = 0;
        bool any = false;
        for (size_t i = 0; i + w <= s.size(); i += w) {
            int lo = s[i], hi = s[i];
            for (size_t j = i; j < i + w; j++) { lo = std::min<int>(lo, s[j]); hi = std::max<int>(hi, s[j]); }
            if (hi - lo > 2000) { if (!any) first = i; last = i + w; any = true; }
        }
        return any ? static_cast<double>(last - first) / Computer::SID::kSampleRate : 0.0;
    }

    /* The pitch in the middle of the sound: the lag of the strongest
       autocorrelation between 60 and 400 Hz -- the same estimator the reference
       numbers were measured with. */
    static double pitch(const std::vector<int16_t> &s, size_t first, size_t last)
    {
        const double rate = Computer::SID::kSampleRate;
        const size_t mid = (first + last) / 2, w = static_cast<size_t>(rate / 10);
        const size_t from = mid - w, to = mid + w;
        double mean = 0;
        for (size_t i = from; i < to; i++) mean += s[i];
        mean /= static_cast<double>(to - from);
        double best = 0; size_t best_lag = 1;
        for (size_t lag = static_cast<size_t>(rate / 400); lag < static_cast<size_t>(rate / 60); lag++) {
            double sum = 0;
            for (size_t i = from; i + lag < to; i++) sum += (s[i] - mean) * (s[i + lag] - mean);
            if (sum > best) { best = sum; best_lag = lag; }
        }
        return rate / static_cast<double>(best_lag);
    }
};

/* The JavaScript port, at its fixed 22,050 Hz, renders "AA4AA4AA4AA4" (pitch 64,
   speed 72) as 0.635 s of sound at about 179 Hz by this estimator. At 4 MHz ours
   would last about 0.16 s; at 1 MHz it should match. The windows are wide because
   the reference is a different implementation, not the same bytes -- the test is
   for a factor of four, not a percent. */
TEST_F(SaySoundTest, ItSpeaksAtTheSpeedAndPitchSAMWasWrittenFor)
{
    const std::vector<int16_t> s = speak("]AA4AA4AA4AA4", 2.0);
    size_t first = 0, last = 0;
    const double len = sounding(s, first, last);
    ASSERT_GT(len, 0.0) << "SAY made no sound";
    const double f0 = pitch(s, first, last);
    std::printf("sounding %.3f s (reference 0.635), pitch %.1f Hz (reference 179)\n", len, f0);
    EXPECT_NEAR(len, 0.635, 0.635 * 0.25) << "the length is off -- is the clock at 1 MHz?";
    EXPECT_NEAR(f0, 179.0, 179.0 * 0.25) << "the pitch is off -- is the clock at 1 MHz?";
}

/* After speaking, the machine is as SAY found it: full speed, interrupts on. SAM
   switches to 1 MHz and turns interrupts off for each utterance. Checked where SAY
   hands back to the DOS (QUITDOS) -- after that the DOS prompt resets the clock
   itself, and would hide SAY forgetting to. Stepped by instruction: no timer is
   needed, and SAY never waits on one. */
TEST_F(SaySoundTest, ItPutsTheClockAndInterruptsBack)
{
    const std::string tail = "HELLO";
    for (size_t i = 0; i < tail.size(); ++i)
        mem->write(static_cast<uint16_t>(kArgBuf + i), static_cast<uint8_t>(tail[i]));
    mem->write(static_cast<uint16_t>(kArgBuf + tail.size()), 0);
    cpu->reg.SP = 0xFF;
    cpu->reg.PC = 0x0800;
    cpu->setFlag(Computer::CPU6502::kInterrupt, false);

    const uint16_t quit = sym("_QUITDOS");
    bool slowed = false;
    int guard = 0;
    for (; guard < 20'000'000 && cpu->reg.PC != quit; guard++) {
        c.runInstructions(1);
        if (mem->read(0xFED4)) slowed = true;
    }
    ASSERT_EQ(cpu->reg.PC, quit) << "SAY did not finish";
    EXPECT_TRUE(slowed) << "SAY never switched to 1 MHz";
    EXPECT_EQ(mem->read(0xFED4), 0x00) << "SAY left the machine at 1 MHz";
    EXPECT_EQ(c.clockHz(), 4000000u);
    EXPECT_FALSE(cpu->getFlag(Computer::CPU6502::kInterrupt)) << "SAY left interrupts off";
}

/* The > prompt, driven the way a person drives it: SAY started with an empty
   command tail, keys typed through the PIA, the screen read back from the VIC's
   character plane. Reusable -- the "/" settings tests build on it: start(), type(),
   run(), screen(), lastLine() and atPrompt() are the whole interface. */
class SayPromptTest : public SaySoundTest {
protected:
    static constexpr uint16_t kSoundEnable = 0x0029;

    void SetUp() override
    {
        SayTest::SetUp();
        pia = c.getPia();
        vic = c.getVideoChip();
        samples.clear();
    }

    /* Launch as the DOS would with no command tail. `sound` is SOUND_ENABLE. */
    void start(bool sound = true)
    {
        mem->write(kArgBuf, 0);
        mem->write(kChimeOn, 0);
        mem->write(kSoundEnable, sound ? 1 : 0);
        cpu->reg.SP = 0xFF;
        cpu->reg.PC = 0x0800;
        cpu->setFlag(Computer::CPU6502::kInterrupt, false);
        int16_t chunk[4096];
        while (c.getSid()->drainSamples(chunk, 4096) > 0) {}
    }

    /* `seconds` of machine time, ten milliseconds a slice; the SID's output is
       appended to `samples`. */
    void run(double seconds)
    {
        int16_t chunk[4096];
        for (int t = 0; t < static_cast<int>(seconds * 100); t++) {
            c.runCycles(c.clockHz() / 100);
            int n;
            while ((n = c.getSid()->drainSamples(chunk, 4096)) > 0)
                samples.insert(samples.end(), chunk, chunk + n);
        }
    }

    /* Type text (a '\n' is Return), then give the machine time to act on it. */
    void type(const std::string &text, double seconds = 0.2)
    {
        for (char ch : text) pia->addKeypress(ch == '\n' ? '\r' : ch);
        run(seconds);
    }

    /* Step by instruction until SAY reaches QUITDOS; true if it did. */
    bool leaves()
    {
        const uint16_t quit = sym("_QUITDOS");
        for (int i = 0; i < 20'000'000 && cpu->reg.PC != quit; i++) c.runInstructions(1);
        return cpu->reg.PC == quit;
    }

    std::string row(int y)
    {
        std::string out;
        for (int x = 0; x < 80; x++)
            out.push_back(static_cast<char>(vic->getCharacterAt(static_cast<uint16_t>(x),
                                                               static_cast<uint16_t>(y))));
        return out;
    }

    static std::string rtrim(std::string s)
    {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }

    std::string screen()
    {
        std::string out;
        for (int y = 0; y < 25; y++) out += rtrim(row(y)) + "\n";
        return out;
    }

    /* The last row with anything on it ("" if the screen is blank). */
    std::string lastLine()
    {
        for (int y = 24; y >= 0; y--)
            if (!rtrim(row(y)).empty()) return rtrim(row(y));
        return "";
    }

    /* A fresh, empty prompt is the last thing on the screen. */
    bool atPrompt() { return lastLine() == ">"; }

    int occurrences(const std::string &what)
    {
        int n = 0;
        const std::string s = screen();
        for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) n++;
        return n;
    }

    bool madeSound()
    {
        size_t first, last;
        return sounding(samples, first, last) > 0.05;
    }

    Computer::PIA *pia = nullptr;
    Computer::VIC *vic = nullptr;
    std::vector<int16_t> samples;
};

TEST_F(SayPromptTest, ItSignsOnAndAsksForALine)
{
    start();
    run(0.3);
    const std::string s = screen();
    const size_t on = s.find("SAY -- S.A.M., the Software Automatic Mouth");
    ASSERT_NE(on, std::string::npos) << s;
    EXPECT_NE(s.find("\n>", on), std::string::npos) << "no prompt after the sign-on:\n" << s;
    EXPECT_TRUE(atPrompt()) << s;
    EXPECT_FALSE(madeSound());
}

TEST_F(SayPromptTest, ATypedEnglishLineIsSpokenAndAFreshPromptFollows)
{
    start();
    run(0.3);
    type("HELLO\n", 2.0);
    EXPECT_TRUE(madeSound()) << "SAM made no sound\n" << screen();
    EXPECT_EQ(occurrences("> HELLO"), 1) << screen();
    EXPECT_TRUE(atPrompt()) << screen();
    EXPECT_EQ(screen().find("SAM could not"), std::string::npos) << screen();
}

TEST_F(SayPromptTest, LowerCaseIsSpokenAsCapitalsAre)
{
    // What reaches SAM is compared, caught at the call into it: the same bytes
    // whatever the case typed, so it is spoken the same.
    const uint16_t say_text = sym("_sam_say_text"), input = sym("_sam_input");
    auto whatSAMGets = [&](const std::string &typed) {
        SetUp();                                    // a fresh machine for each line
        start();
        run(0.3);
        type(typed, 0.0);
        for (int i = 0; i < 5'000'000 && cpu->reg.PC != say_text; i++) c.runInstructions(1);
        EXPECT_EQ(cpu->reg.PC, say_text) << "the line never reached SAM";
        std::string got;
        for (int i = 0; i < 256; i++) {
            const uint8_t b = mem->read(static_cast<uint16_t>(input + i));
            if (b == kSamEol) break;
            got.push_back(static_cast<char>(b));
        }
        return got;
    };
    EXPECT_EQ(whatSAMGets("HELLO\n"), "HELLO");
    EXPECT_EQ(whatSAMGets("hello\n"), "HELLO");
    EXPECT_EQ(whatSAMGets("HeLLo\n"), "HELLO");

    // And it makes sound, with no complaint.
    SetUp();
    start();
    run(0.3);
    type("hello\n", 2.0);
    EXPECT_TRUE(madeSound()) << screen();
    EXPECT_TRUE(atPrompt()) << screen();
    EXPECT_EQ(screen().find("SAM could not"), std::string::npos) << screen();
}

TEST_F(SayPromptTest, APhonemeLineIsSpokenWithNoError)
{
    start();
    run(0.3);
    type("]/HEH4LOW\n", 2.0);
    EXPECT_TRUE(madeSound()) << screen();
    EXPECT_EQ(screen().find("SAM does not know"), std::string::npos) << screen();
    EXPECT_EQ(screen().find("SAM could not"), std::string::npos) << screen();
    EXPECT_TRUE(atPrompt()) << screen();
}

TEST_F(SayPromptTest, PhonemesSAMCannotReadAreEchoedWithACaretWhereItStopped)
{
    start();
    run(0.3);
    type("]XQ\n", 2.0);
    const std::string s = screen();
    const std::string head = "SAM does not know this:\n";
    const size_t at = s.find(head);
    ASSERT_NE(at, std::string::npos) << s;
    // The phonemes as typed (without the ]), then the caret line under them.
    const size_t echo = at + head.size();
    const size_t eol = s.find('\n', echo);
    ASSERT_NE(eol, std::string::npos);
    EXPECT_EQ(s.substr(echo, eol - echo), " XQ") << s;
    const size_t caretRow = eol + 1;
    const size_t caret = s.find('^', caretRow);
    ASSERT_NE(caret, std::string::npos) << s;
    ASSERT_LT(caret, s.find('\n', caretRow)) << "the ^ is not on the line under the phonemes";
    // Both lines are indented one column. "X" is a phoneme, "XQ" is not: SAM
    // stops at the Q, index 1, so the caret sits under it.
    EXPECT_EQ(caret - caretRow, 1u + 1u) << s;
    EXPECT_TRUE(atPrompt()) << s;
}

TEST_F(SayPromptTest, MutedSystemSoundIsSaidSoAndThePromptReturns)
{
    start(false);
    run(0.3);
    type("HELLO\n", 0.5);
    EXPECT_NE(screen().find("System sound is muted."), std::string::npos) << screen();
    EXPECT_FALSE(madeSound());
    EXPECT_TRUE(atPrompt()) << screen();
}

TEST_F(SayPromptTest, ReturnOnAnEmptyLineLeaves)
{
    start();
    run(0.3);                                   // sign-on, waiting at the prompt
    pia->addKeypress('\r');
    EXPECT_TRUE(leaves()) << screen();
}

TEST_F(SayPromptTest, EscapeOnAnEmptyLineLeaves)
{
    start();
    run(0.3);
    pia->addKeypress(0x1B);
    EXPECT_TRUE(leaves()) << screen();
}

TEST_F(SayPromptTest, EscapeMidLineClearsItAndStaysAtThePrompt)
{
    start();
    run(0.3);
    type("HELLO", 0.2);
    EXPECT_EQ(occurrences("> HELLO"), 1) << screen();
    type("\x1b", 0.2);
    EXPECT_TRUE(atPrompt()) << "the line was not cleared:\n" << screen();
    EXPECT_EQ(screen().find("HELLO"), std::string::npos) << screen();
    EXPECT_FALSE(madeSound());
    EXPECT_NE(cpu->reg.PC, sym("_QUITDOS"));
    // Still alive and reading: the next line is taken.
    type("]/HEH4LOW\n", 2.0);
    EXPECT_TRUE(madeSound()) << screen();
}

/* ---- the / settings ------------------------------------------------------- */

class SaySettingsTest : public SayPromptTest {
protected:
    /* A fresh machine, SAY at its prompt. */
    void fresh()
    {
        SetUp();
        start();
        run(0.3);
    }

    uint8_t var(const char *name) { return mem->read(sym(name)); }

    /* SPEED, PITCH, THROAT, MOUTH as SAY keeps them. */
    std::string vars()
    {
        return std::to_string(var("_sam_speed")) + " " + std::to_string(var("_sam_pitch")) + " " +
               std::to_string(var("_sam_throat")) + " " + std::to_string(var("_sam_mouth"));
    }

    static std::string shown(int speed, int pitch, int throat, int mouth)
    {
        return "SPEED " + std::to_string(speed) + "  PITCH " + std::to_string(pitch) +
               "  THROAT " + std::to_string(throat) + "  MOUTH " + std::to_string(mouth);
    }

    bool has(const std::string &what) { return screen().find(what) != std::string::npos; }

    /* How long "AA4AA4AA4AA4" lasts, typed at the prompt after `setup`. */
    double spokenLength(const std::string &setup)
    {
        fresh();
        if (!setup.empty()) type(setup, 0.3);
        samples.clear();
        type("]AA4AA4AA4AA4\n", 5.0);
        size_t first = 0, last = 0;
        return sounding(samples, first, last);
    }
};

TEST_F(SaySettingsTest, ShowReportsSAMsOwnSettingsAtAFreshPrompt)
{
    fresh();
    type("/SHOW\n");
    EXPECT_TRUE(has(shown(72, 64, 128, 128))) << screen();
    EXPECT_TRUE(atPrompt()) << screen();
}

TEST_F(SaySettingsTest, EachSettingTakesANumberAndPrintsItBack)
{
    fresh();
    type("/SPEED 100\n");
    EXPECT_TRUE(has(shown(100, 64, 128, 128))) << screen();
    EXPECT_EQ(vars(), "100 64 128 128");
    type("/PITCH 33\n");
    EXPECT_TRUE(has(shown(100, 33, 128, 128))) << screen();
    EXPECT_EQ(vars(), "100 33 128 128");
    type("/THROAT 200\n");
    EXPECT_TRUE(has(shown(100, 33, 200, 128))) << screen();
    EXPECT_EQ(vars(), "100 33 200 128");
    type("/MOUTH 255\n");
    EXPECT_TRUE(has(shown(100, 33, 200, 255))) << screen();
    EXPECT_EQ(vars(), "100 33 200 255");
    type("/SPEED 1\n");
    EXPECT_TRUE(has(shown(1, 33, 200, 255))) << screen();
    EXPECT_EQ(vars(), "1 33 200 255");
    EXPECT_TRUE(atPrompt()) << screen();
}

TEST_F(SaySettingsTest, ANumberOutOfRangeOrMissingIsRefusedAndNothingChanges)
{
    const char *bad[] = {"/SPEED 0\n", "/SPEED 256\n", "/PITCH\n", "/MOUTH X\n"};
    for (const char *line : bad) {
        fresh();
        type(line);
        EXPECT_EQ(occurrences("A number from 1 to 255."), 1) << line << screen();
        EXPECT_EQ(vars(), "72 64 128 128") << line;
        EXPECT_TRUE(atPrompt()) << line << screen();
        type("/SHOW\n");
        EXPECT_TRUE(has(shown(72, 64, 128, 128))) << line << screen();
    }
}

TEST_F(SaySettingsTest, EachVoiceSetsItsFourNumbers)
{
    struct V { const char *name; int speed, pitch, throat, mouth; };
    const V voices[] = {
        {"SAM", 72, 64, 128, 128},  {"ELF", 72, 64, 110, 160}, {"ROBOT", 92, 60, 190, 190},
        {"STUFFY", 82, 72, 110, 105}, {"LADY", 82, 32, 145, 145}, {"ET", 100, 64, 150, 200},
    };
    fresh();
    for (const V &v : voices) {
        type(std::string("/VOICE ") + v.name + "\n");
        EXPECT_TRUE(has(shown(v.speed, v.pitch, v.throat, v.mouth))) << v.name << "\n" << screen();
        EXPECT_EQ(vars(), std::to_string(v.speed) + " " + std::to_string(v.pitch) + " " +
                              std::to_string(v.throat) + " " + std::to_string(v.mouth)) << v.name;
        EXPECT_TRUE(atPrompt()) << v.name << screen();
    }
}

TEST_F(SaySettingsTest, AnUnknownVoiceListsTheSix)
{
    fresh();
    type("/VOICE NOBODY\n");
    EXPECT_TRUE(has("Voices: SAM ELF ROBOT STUFFY LADY ET")) << screen();
    EXPECT_EQ(vars(), "72 64 128 128");
    EXPECT_TRUE(atPrompt()) << screen();
}

TEST_F(SaySettingsTest, HelpAndQuestionMarkListTheSettings)
{
    for (const char *line : {"/HELP\n", "/?\n"}) {
        fresh();
        type(line);
        for (const char *what : {"/SPEED", "/PITCH", "/THROAT", "/MOUTH", "/VOICE", "/SHOW",
                                 "/HELP", "/QUIT"})
            EXPECT_TRUE(has(what)) << line << what << "\n" << screen();
        // The current text, not the line ama took out.
        EXPECT_FALSE(has("says the last line again")) << screen();
        EXPECT_TRUE(atPrompt()) << line << screen();
    }
}

TEST_F(SaySettingsTest, AnUnknownSettingIsRefusedAndThePromptReturns)
{
    fresh();
    type("/FOO\n");
    EXPECT_EQ(occurrences("Not a setting. /HELP lists them."), 1) << screen();
    EXPECT_TRUE(atPrompt()) << screen();
    EXPECT_NE(cpu->reg.PC, sym("_QUITDOS"));
}

TEST_F(SaySettingsTest, QuitAndQLeave)
{
    for (const char *line : {"/QUIT\n", "/Q\n"}) {
        fresh();
        for (char ch : std::string(line)) pia->addKeypress(ch == '\n' ? '\r' : ch);
        EXPECT_TRUE(leaves()) << line << screen();
    }
}

TEST_F(SaySettingsTest, LowerCaseSettingsApply)
{
    fresh();
    type("/speed 100\n");
    EXPECT_TRUE(has(shown(100, 64, 128, 128))) << screen();
    EXPECT_EQ(vars(), "100 64 128 128");
    type("/voice elf\n");
    EXPECT_TRUE(has(shown(72, 64, 110, 160))) << screen();
    type("/show\n");
    EXPECT_EQ(occurrences(shown(72, 64, 110, 160)), 2) << screen();
}

TEST_F(SaySettingsTest, SettingsLastAcrossSpokenLines)
{
    fresh();
    type("/SPEED 150\n");
    type("HELLO\n", 3.0);
    EXPECT_TRUE(madeSound()) << screen();
    type("/SHOW\n");
    EXPECT_EQ(occurrences(shown(150, 64, 128, 128)), 2) << screen();
    EXPECT_EQ(vars(), "150 64 128 128");
}

TEST_F(SaySettingsTest, SpeedMakesTheVoiceTakeLonger)
{
    const double normal = spokenLength("");
    const double slow = spokenLength("/SPEED 144\n");
    std::printf("speed 72: %.3f s, speed 144: %.3f s\n", normal, slow);
    ASSERT_GT(normal, 0.0) << "no sound at the default speed";
    EXPECT_GT(slow, normal * 1.5) << "/SPEED 144 did not lengthen the speech";
}

} // namespace
