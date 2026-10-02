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

} // namespace
