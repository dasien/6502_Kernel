// Unit tests for the software SID sound chip (Computer::Sid).
//
// Exercises the memory-mapped register port and the phase-1 synthesis engine
// (oscillators + ADSR envelopes + master volume). No Qt / audio device needed:
// the tests drive registers and pull PCM directly via generateSamples().

#include "computer/Sid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using Computer::Sid;

namespace
{
    // Voice-1 register addresses.
    constexpr uint16_t kFreqLo = Sid::kRegBase + Sid::kOffFreqLo;
    constexpr uint16_t kFreqHi = Sid::kRegBase + Sid::kOffFreqHi;
    constexpr uint16_t kControl = Sid::kRegBase + Sid::kOffControl;
    constexpr uint16_t kAtkDec = Sid::kRegBase + Sid::kOffAttackDecay;
    constexpr uint16_t kSusRel = Sid::kRegBase + Sid::kOffSustainRelease;
    constexpr uint16_t kModeVol = Sid::kRegBase + Sid::kRegModeVol;

    // RMS of a block of samples, normalized to [0,1].
    double rms(const std::vector<int16_t> &buf)
    {
        double acc = 0.0;
        for (int16_t s : buf)
            acc += static_cast<double>(s) * s;
        return std::sqrt(acc / buf.size()) / 32767.0;
    }

    // Count rising zero-crossings (negative -> non-negative) in a block.
    int risingCrossings(const std::vector<int16_t> &buf)
    {
        int count = 0;
        for (size_t i = 1; i < buf.size(); ++i)
            if (buf[i - 1] < 0 && buf[i] >= 0)
                ++count;
        return count;
    }
} // namespace

TEST(SidTest, AddressRange)
{
    EXPECT_FALSE(Sid::isSidAddress(Sid::kRegBase - 1));
    EXPECT_TRUE(Sid::isSidAddress(Sid::kRegBase));
    EXPECT_TRUE(Sid::isSidAddress(Sid::kRegLast));
    EXPECT_FALSE(Sid::isSidAddress(Sid::kRegLast + 1));
    EXPECT_EQ(Sid::kRegBase, 0xFE38u);
    EXPECT_EQ(Sid::kRegLast, 0xFE54u);
}

TEST(SidTest, RegisterRoundTrip)
{
    Sid sid;
    sid.write(kFreqLo, 0xAB);
    sid.write(kFreqHi, 0xCD);
    EXPECT_EQ(sid.read(kFreqLo), 0xAB);
    EXPECT_EQ(sid.read(kFreqHi), 0xCD);

    // Read-only registers (paddles + OSC3/ENV3 read-back) return 0 in phase 1.
    EXPECT_EQ(sid.read(Sid::kRegBase + Sid::kRegPotX), 0x00);
    EXPECT_EQ(sid.read(Sid::kRegBase + Sid::kRegOsc3), 0x00);
    EXPECT_EQ(sid.read(Sid::kRegBase + Sid::kRegEnv3), 0x00);

    // Addresses outside the port are inert.
    sid.write(Sid::kRegLast + 1, 0x55);
    EXPECT_EQ(sid.read(Sid::kRegLast + 1), 0x00);
}

TEST(SidTest, SilentWithoutGate)
{
    Sid sid;
    // Full volume + a waveform + a pitch, but the gate is never opened, so the
    // envelope stays idle and the output must be pure silence.
    sid.write(kModeVol, 0x0F);
    sid.write(kFreqHi, 0x1C); // ~ mid pitch
    sid.write(kControl, Sid::kCtrlSawtooth);

    std::vector<int16_t> buf(4410);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));
    for (int16_t s : buf)
        EXPECT_EQ(s, 0);
}

TEST(SidTest, GateProducesSound)
{
    Sid sid;
    sid.write(kModeVol, 0x0F);           // master volume = 15
    sid.write(kFreqLo, 0xD6);            // 0x1CD6 = 7382 -> ~440 Hz
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x00);            // fastest attack, fastest decay
    sid.write(kSusRel, 0xF0);            // sustain full, fast release
    sid.write(kControl, Sid::kCtrlSawtooth | Sid::kCtrlGate);

    std::vector<int16_t> buf(Sid::kSampleRate); // 1 second
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));

    // After the (few-ms) attack, a sustained sawtooth should have real energy.
    EXPECT_GT(rms(buf), 0.1);
}

TEST(SidTest, OscillatorPitchIsApproximatelyCorrect)
{
    Sid sid;
    sid.write(kModeVol, 0x0F);
    sid.write(kFreqLo, 0xD6); // 7382 -> ~440.0 Hz at the nominal 1 MHz SID clock
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x00);
    sid.write(kSusRel, 0xF0);
    sid.write(kControl, Sid::kCtrlSawtooth | Sid::kCtrlGate);

    std::vector<int16_t> buf(Sid::kSampleRate);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));

    // A sawtooth crosses zero (rising) once per cycle; ~440 cycles in one second.
    const int cycles = risingCrossings(buf);
    EXPECT_NEAR(cycles, 440, 15);
}

TEST(SidTest, ReleaseFadesToSilence)
{
    Sid sid;
    sid.write(kModeVol, 0x0F);
    sid.write(kFreqLo, 0xD6);
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x00);
    sid.write(kSusRel, 0xF0); // sustain full, fastest release
    sid.write(kControl, Sid::kCtrlSawtooth | Sid::kCtrlGate);

    // Let the tone establish, then drop the gate.
    std::vector<int16_t> on(4410);
    sid.generateSamples(on.data(), static_cast<int>(on.size()));
    ASSERT_GT(rms(on), 0.1);

    sid.write(kControl, Sid::kCtrlSawtooth); // gate off -> release

    std::vector<int16_t> off(Sid::kSampleRate / 2); // 0.5 s of release
    sid.generateSamples(off.data(), static_cast<int>(off.size()));

    // The very end of the release must be effectively silent.
    std::vector<int16_t> tail(off.end() - 200, off.end());
    for (int16_t s : tail)
        EXPECT_LT(std::abs(static_cast<int>(s)), 8);
}

TEST(SidTest, ResetSilencesAndClears)
{
    Sid sid;
    sid.write(kModeVol, 0x0F);
    sid.write(kControl, Sid::kCtrlSawtooth | Sid::kCtrlGate);
    sid.reset();
    EXPECT_EQ(sid.read(kModeVol), 0x00);
    EXPECT_EQ(sid.read(kControl), 0x00);

    std::vector<int16_t> buf(4410);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));
    for (int16_t s : buf)
        EXPECT_EQ(s, 0);
}
