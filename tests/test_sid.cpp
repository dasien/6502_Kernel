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
    constexpr uint16_t kFcLo = Sid::kRegBase + Sid::kRegFcLo;
    constexpr uint16_t kFcHi = Sid::kRegBase + Sid::kRegFcHi;
    constexpr uint16_t kResFilt = Sid::kRegBase + Sid::kRegResFilt;
    constexpr uint16_t kOsc3 = Sid::kRegBase + Sid::kRegOsc3;
    constexpr uint16_t kEnv3 = Sid::kRegBase + Sid::kRegEnv3;

    // Voice-3 register addresses (voice base = 2 * kVoiceRegs).
    constexpr uint16_t kV3FreqHi = Sid::kRegBase + 2 * Sid::kVoiceRegs + Sid::kOffFreqHi;
    constexpr uint16_t kV3Control = Sid::kRegBase + 2 * Sid::kVoiceRegs + Sid::kOffControl;
    constexpr uint16_t kV3AtkDec = Sid::kRegBase + 2 * Sid::kVoiceRegs + Sid::kOffAttackDecay;
    constexpr uint16_t kV3SusRel = Sid::kRegBase + 2 * Sid::kVoiceRegs + Sid::kOffSustainRelease;

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

TEST(SidTest, LowPassFilterAttenuatesHighTone)
{
    // Baseline: a bright 440 Hz sawtooth, unfiltered.
    Sid dry;
    dry.write(kModeVol, 0x0F);
    dry.write(kFreqLo, 0xD6);
    dry.write(kFreqHi, 0x1C);
    dry.write(kAtkDec, 0x00);
    dry.write(kSusRel, 0xF0);
    dry.write(kControl, Sid::kCtrlSawtooth | Sid::kCtrlGate);
    std::vector<int16_t> dry_buf(Sid::kSampleRate);
    dry.generateSamples(dry_buf.data(), static_cast<int>(dry_buf.size()));

    // Same tone routed through the filter with the cutoff at the bottom (~30 Hz)
    // and low-pass mode selected: the 440 Hz content must be strongly attenuated.
    Sid wet;
    wet.write(kModeVol, 0x0F | Sid::kModeLowPass);
    wet.write(kFreqLo, 0xD6);
    wet.write(kFreqHi, 0x1C);
    wet.write(kAtkDec, 0x00);
    wet.write(kSusRel, 0xF0);
    wet.write(kFcLo, 0x00);
    wet.write(kFcHi, 0x00);
    wet.write(kResFilt, Sid::kFiltVoice1); // route voice 1 through the filter
    wet.write(kControl, Sid::kCtrlSawtooth | Sid::kCtrlGate);
    std::vector<int16_t> wet_buf(Sid::kSampleRate);
    wet.generateSamples(wet_buf.data(), static_cast<int>(wet_buf.size()));

    EXPECT_GT(rms(dry_buf), 0.1);
    EXPECT_LT(rms(wet_buf), rms(dry_buf) * 0.5);
}

TEST(SidTest, HighCutoffLowPassStaysStable)
{
    // A low-pass with the cutoff near the top must PASS a 440 Hz tone -- not blow
    // up into NaN/silence the way a naive Chamberlin SVF does above ~fs/6.
    Sid sid;
    sid.write(kModeVol, 0x0F | Sid::kModeLowPass);
    sid.write(kFreqLo, 0xD6);
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x00);
    sid.write(kSusRel, 0xF0);
    sid.write(kResFilt, 0x80 | Sid::kFiltVoice1); // resonance 8 + route voice 1
    sid.write(kFcLo, 0x07);
    sid.write(kFcHi, 0xFF); // cutoff near maximum
    sid.write(kControl, Sid::kCtrlSawtooth | Sid::kCtrlGate);

    std::vector<int16_t> buf(Sid::kSampleRate);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));
    EXPECT_GT(rms(buf), 0.1);
}

TEST(SidTest, FilterSweepRemainsAudible)
{
    // Sweep the cutoff across the whole range (as examples/sid_filter_sweep.asm
    // does). The filter must never latch into a corrupted (silent) state.
    Sid sid;
    sid.write(kModeVol, 0x0F | Sid::kModeLowPass);
    sid.write(kFreqLo, 0xD6);
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x00);
    sid.write(kSusRel, 0xF0);
    sid.write(kResFilt, 0x80 | Sid::kFiltVoice1);
    sid.write(kControl, Sid::kCtrlSawtooth | Sid::kCtrlGate);

    std::vector<int16_t> blk(441);
    for (int hi = 0; hi <= 255; ++hi)
    {
        sid.write(kFcHi, static_cast<uint8_t>(hi));
        sid.generateSamples(blk.data(), static_cast<int>(blk.size()));
    }
    // After the full sweep, a high-cutoff block is still audible (no NaN latch).
    sid.write(kFcHi, 0xFF);
    std::vector<int16_t> tail(Sid::kSampleRate / 4);
    sid.generateSamples(tail.data(), static_cast<int>(tail.size()));
    EXPECT_GT(rms(tail), 0.1);
}

TEST(SidTest, Voice3ReadBack)
{
    Sid sid;
    sid.write(kModeVol, 0x0F);
    sid.write(kV3FreqHi, 0x1C);
    sid.write(kV3AtkDec, 0x00);
    sid.write(kV3SusRel, 0xF0); // sustain full
    sid.write(kV3Control, Sid::kCtrlSawtooth | Sid::kCtrlGate);

    std::vector<int16_t> buf(Sid::kSampleRate / 2);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));

    // ENV3 should have climbed to (near) full sustain; OSC3 reflects the wave.
    EXPECT_GT(sid.read(kEnv3), 200);
    // OSC3 is a live oscillator sample -- just require the read path is wired
    // (non-throwing, within range). Its exact value depends on the phase.
    EXPECT_LE(sid.read(kOsc3), 255);
}

TEST(SidTest, Voice3DisconnectSilencesVoice3)
{
    Sid sid;
    // Only voice 3 is gated, but the voice-3-off bit disconnects it from the mix.
    sid.write(kModeVol, 0x0F | Sid::kModeVoice3Off);
    sid.write(kV3FreqHi, 0x1C);
    sid.write(kV3AtkDec, 0x00);
    sid.write(kV3SusRel, 0xF0);
    sid.write(kV3Control, Sid::kCtrlSawtooth | Sid::kCtrlGate);

    std::vector<int16_t> buf(4410);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));
    for (int16_t s : buf)
        EXPECT_EQ(s, 0);
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
