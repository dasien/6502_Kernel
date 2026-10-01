// Unit tests for the software SID sound chip (Computer::SID).
//
// Exercises the memory-mapped register port and the phase-1 synthesis engine
// (oscillators + ADSR envelopes + master volume). No Qt / audio device needed:
// the tests drive registers and pull PCM directly via generateSamples().

#include "computer/SID.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using Computer::SID;

namespace
{
    // Voice-1 register addresses.
    constexpr uint16_t kFreqLo = SID::kRegBase + SID::kOffFreqLo;
    constexpr uint16_t kFreqHi = SID::kRegBase + SID::kOffFreqHi;
    constexpr uint16_t kControl = SID::kRegBase + SID::kOffControl;
    constexpr uint16_t kAtkDec = SID::kRegBase + SID::kOffAttackDecay;
    constexpr uint16_t kSusRel = SID::kRegBase + SID::kOffSustainRelease;
    constexpr uint16_t kModeVol = SID::kRegBase + SID::kRegModeVol;
    constexpr uint16_t kFcLo = SID::kRegBase + SID::kRegFcLo;
    constexpr uint16_t kFcHi = SID::kRegBase + SID::kRegFcHi;
    constexpr uint16_t kResFilt = SID::kRegBase + SID::kRegResFilt;
    constexpr uint16_t kOsc3 = SID::kRegBase + SID::kRegOsc3;
    constexpr uint16_t kEnv3 = SID::kRegBase + SID::kRegEnv3;

    // Voice-3 register addresses (voice base = 2 * kVoiceRegs).
    constexpr uint16_t kV3FreqHi = SID::kRegBase + 2 * SID::kVoiceRegs + SID::kOffFreqHi;
    constexpr uint16_t kV3Control = SID::kRegBase + 2 * SID::kVoiceRegs + SID::kOffControl;
    constexpr uint16_t kV3AtkDec = SID::kRegBase + 2 * SID::kVoiceRegs + SID::kOffAttackDecay;
    constexpr uint16_t kV3SusRel = SID::kRegBase + 2 * SID::kVoiceRegs + SID::kOffSustainRelease;

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
    EXPECT_FALSE(SID::isSidAddress(SID::kRegBase - 1));
    EXPECT_TRUE(SID::isSidAddress(SID::kRegBase));
    EXPECT_TRUE(SID::isSidAddress(SID::kRegLast));
    EXPECT_FALSE(SID::isSidAddress(SID::kRegLast + 1));
    EXPECT_EQ(SID::kRegBase, 0xFE38u);
    EXPECT_EQ(SID::kRegLast, 0xFE54u);
}

TEST(SidTest, RegisterRoundTrip)
{
    SID sid;
    sid.write(kFreqLo, 0xAB);
    sid.write(kFreqHi, 0xCD);
    EXPECT_EQ(sid.read(kFreqLo), 0xAB);
    EXPECT_EQ(sid.read(kFreqHi), 0xCD);

    // Read-only registers (paddles + OSC3/ENV3 read-back) return 0 in phase 1.
    EXPECT_EQ(sid.read(SID::kRegBase + SID::kRegPotX), 0x00);
    EXPECT_EQ(sid.read(SID::kRegBase + SID::kRegOsc3), 0x00);
    EXPECT_EQ(sid.read(SID::kRegBase + SID::kRegEnv3), 0x00);

    // Addresses outside the port are inert.
    sid.write(SID::kRegLast + 1, 0x55);
    EXPECT_EQ(sid.read(SID::kRegLast + 1), 0x00);
}

TEST(SidTest, SilentWithoutGate)
{
    SID sid;
    // Full volume + a waveform + a pitch, but the gate is never opened, so the
    // envelope stays idle and the output must be pure silence.
    sid.write(kModeVol, 0x0F);
    sid.write(kFreqHi, 0x1C); // ~ mid pitch
    sid.write(kControl, SID::kCtrlSawtooth);

    std::vector<int16_t> buf(4410);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));
    for (int16_t s : buf)
        EXPECT_EQ(s, 0);
}

TEST(SidTest, GateProducesSound)
{
    SID sid;
    sid.write(kModeVol, 0x0F);           // master volume = 15
    sid.write(kFreqLo, 0xD6);            // 0x1CD6 = 7382 -> ~440 Hz
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x00);            // fastest attack, fastest decay
    sid.write(kSusRel, 0xF0);            // sustain full, fast release
    sid.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate);

    std::vector<int16_t> buf(SID::kSampleRate); // 1 second
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));

    // After the (few-ms) attack, a sustained sawtooth should have real energy.
    EXPECT_GT(rms(buf), 0.1);
}

TEST(SidTest, OscillatorPitchIsApproximatelyCorrect)
{
    SID sid;
    sid.write(kModeVol, 0x0F);
    sid.write(kFreqLo, 0xD6); // 7382 -> ~440.0 Hz at the nominal 1 MHz SID clock
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x00);
    sid.write(kSusRel, 0xF0);
    sid.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate);

    std::vector<int16_t> buf(SID::kSampleRate);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));

    // A sawtooth crosses zero (rising) once per cycle; ~440 cycles in one second.
    const int cycles = risingCrossings(buf);
    EXPECT_NEAR(cycles, 440, 15);
}

TEST(SidTest, ReleaseFadesToSilence)
{
    SID sid;
    sid.write(kModeVol, 0x0F);
    sid.write(kFreqLo, 0xD6);
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x00);
    sid.write(kSusRel, 0xF0); // sustain full, fastest release
    sid.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate);

    // Let the tone establish, then drop the gate.
    std::vector<int16_t> on(4410);
    sid.generateSamples(on.data(), static_cast<int>(on.size()));
    ASSERT_GT(rms(on), 0.1);

    sid.write(kControl, SID::kCtrlSawtooth); // gate off -> release

    std::vector<int16_t> off(SID::kSampleRate / 2); // 0.5 s of release
    sid.generateSamples(off.data(), static_cast<int>(off.size()));

    // The very end of the release must be effectively silent.
    std::vector<int16_t> tail(off.end() - 200, off.end());
    for (int16_t s : tail)
        EXPECT_LT(std::abs(static_cast<int>(s)), 8);
}

TEST(SidTest, LowPassFilterAttenuatesHighTone)
{
    // Baseline: a bright 440 Hz sawtooth, unfiltered.
    SID dry;
    dry.write(kModeVol, 0x0F);
    dry.write(kFreqLo, 0xD6);
    dry.write(kFreqHi, 0x1C);
    dry.write(kAtkDec, 0x00);
    dry.write(kSusRel, 0xF0);
    dry.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate);
    std::vector<int16_t> dry_buf(SID::kSampleRate);
    dry.generateSamples(dry_buf.data(), static_cast<int>(dry_buf.size()));

    // Same tone routed through the filter with the cutoff at the bottom (~30 Hz)
    // and low-pass mode selected: the 440 Hz content must be strongly attenuated.
    SID wet;
    wet.write(kModeVol, 0x0F | SID::kModeLowPass);
    wet.write(kFreqLo, 0xD6);
    wet.write(kFreqHi, 0x1C);
    wet.write(kAtkDec, 0x00);
    wet.write(kSusRel, 0xF0);
    wet.write(kFcLo, 0x00);
    wet.write(kFcHi, 0x00);
    wet.write(kResFilt, SID::kFiltVoice1); // route voice 1 through the filter
    wet.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate);
    std::vector<int16_t> wet_buf(SID::kSampleRate);
    wet.generateSamples(wet_buf.data(), static_cast<int>(wet_buf.size()));

    EXPECT_GT(rms(dry_buf), 0.1);
    EXPECT_LT(rms(wet_buf), rms(dry_buf) * 0.5);
}

TEST(SidTest, HighCutoffLowPassStaysStable)
{
    // A low-pass with the cutoff near the top must PASS a 440 Hz tone -- not blow
    // up into NaN/silence the way a naive Chamberlin SVF does above ~fs/6.
    SID sid;
    sid.write(kModeVol, 0x0F | SID::kModeLowPass);
    sid.write(kFreqLo, 0xD6);
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x00);
    sid.write(kSusRel, 0xF0);
    sid.write(kResFilt, 0x80 | SID::kFiltVoice1); // resonance 8 + route voice 1
    sid.write(kFcLo, 0x07);
    sid.write(kFcHi, 0xFF); // cutoff near maximum
    sid.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate);

    std::vector<int16_t> buf(SID::kSampleRate);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));
    EXPECT_GT(rms(buf), 0.1);
}

TEST(SidTest, FilterSweepRemainsAudible)
{
    // Sweep the cutoff across the whole range (as examples/sid_filter_sweep.asm
    // does). The filter must never latch into a corrupted (silent) state.
    SID sid;
    sid.write(kModeVol, 0x0F | SID::kModeLowPass);
    sid.write(kFreqLo, 0xD6);
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x00);
    sid.write(kSusRel, 0xF0);
    sid.write(kResFilt, 0x80 | SID::kFiltVoice1);
    sid.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate);

    std::vector<int16_t> blk(441);
    for (int hi = 0; hi <= 255; ++hi)
    {
        sid.write(kFcHi, static_cast<uint8_t>(hi));
        sid.generateSamples(blk.data(), static_cast<int>(blk.size()));
    }
    // After the full sweep, a high-cutoff block is still audible (no NaN latch).
    sid.write(kFcHi, 0xFF);
    std::vector<int16_t> tail(SID::kSampleRate / 4);
    sid.generateSamples(tail.data(), static_cast<int>(tail.size()));
    EXPECT_GT(rms(tail), 0.1);
}

TEST(SidTest, Voice3ReadBack)
{
    SID sid;
    sid.write(kModeVol, 0x0F);
    sid.write(kV3FreqHi, 0x1C);
    sid.write(kV3AtkDec, 0x00);
    sid.write(kV3SusRel, 0xF0); // sustain full
    sid.write(kV3Control, SID::kCtrlSawtooth | SID::kCtrlGate);

    std::vector<int16_t> buf(SID::kSampleRate / 2);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));

    // ENV3 should have climbed to (near) full sustain; OSC3 reflects the wave.
    EXPECT_GT(sid.read(kEnv3), 200);
    // OSC3 is a live oscillator sample -- just require the read path is wired
    // (non-throwing, within range). Its exact value depends on the phase.
    EXPECT_LE(sid.read(kOsc3), 255);
}

TEST(SidTest, Voice3DisconnectSilencesVoice3)
{
    SID sid;
    // Only voice 3 is gated, but the voice-3-off bit disconnects it from the mix.
    sid.write(kModeVol, 0x0F | SID::kModeVoice3Off);
    sid.write(kV3FreqHi, 0x1C);
    sid.write(kV3AtkDec, 0x00);
    sid.write(kV3SusRel, 0xF0);
    sid.write(kV3Control, SID::kCtrlSawtooth | SID::kCtrlGate);

    std::vector<int16_t> buf(4410);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));
    for (int16_t s : buf)
        EXPECT_EQ(s, 0);
}

TEST(SidTest, ResetSilencesAndClears)
{
    SID sid;
    sid.write(kModeVol, 0x0F);
    sid.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate);
    sid.reset();
    EXPECT_EQ(sid.read(kModeVol), 0x00);
    EXPECT_EQ(sid.read(kControl), 0x00);

    std::vector<int16_t> buf(4410);
    sid.generateSamples(buf.data(), static_cast<int>(buf.size()));
    for (int16_t s : buf)
        EXPECT_EQ(s, 0);
}

// A note is restarted by clearing the gate and setting it again, a few instructions
// apart -- far closer together than one audio sample. The chip must still see the gate
// close, or the note does not restart: KPANIC's shots fired while the last one's
// envelope had died played nothing at all.
TEST(SidTest, AGateClearedAndSetBetweenSamplesRestartsTheNote)
{
    SID sid;
    sid.write(kModeVol, 0x0F);
    sid.write(kFreqLo, 0xD6);
    sid.write(kFreqHi, 0x1C);
    sid.write(kAtkDec, 0x03);           // instant attack, short decay...
    sid.write(kSusRel, 0x00);           // ...to silence: a pluck
    sid.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate);

    std::vector<int16_t> first(SID::kSampleRate / 2);   // the pluck, and long after
    sid.generateSamples(first.data(), static_cast<int>(first.size()));
    std::vector<int16_t> quiet(first.end() - 200, first.end());
    ASSERT_LT(rms(quiet), 0.01) << "the pluck should have died away";

    sid.write(kControl, SID::kCtrlSawtooth);                  // gate off...
    sid.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate); // ...and on, no sample between

    std::vector<int16_t> again(2205);
    sid.generateSamples(again.data(), static_cast<int>(again.size()));
    EXPECT_GT(rms(again), 0.05) << "the note did not restart";
}

// --- machine time -------------------------------------------------------------
//
// The chip renders its samples as the machine's cycles pass (clock), and the audio
// thread only drains them. These pin what that is for: a register write lands at the
// sample it happened on. The chip used to render each audio block from one snapshot
// of its registers, so changes were heard only as often as the sound system asked --
// tens of milliseconds -- which turned sweeps into steps and lost short sounds.

namespace
{
    constexpr uint64_t kHz = 1'000'000;             // a 1 MHz CPU: 1 cycle = 1 us

    // Run the chip for `ms` of machine time from cycle `t`, draining as the audio
    // thread would, and return what came out.
    std::vector<int16_t> runFor(SID &sid, uint64_t &t, int ms)
    {
        std::vector<int16_t> out, chunk(512);
        for (int i = 0; i < ms; ++i) {
            t += kHz / 1000;
            sid.clock(t);
            int n;
            while ((n = sid.drainSamples(chunk.data(), static_cast<int>(chunk.size()))) > 0)
                out.insert(out.end(), chunk.begin(), chunk.begin() + n);
        }
        return out;
    }

    void tone(SID &sid, uint8_t freq_hi)
    {
        sid.write(kModeVol, 0x0F);
        sid.write(kFreqLo, 0x00);
        sid.write(kFreqHi, freq_hi);
        sid.write(kAtkDec, 0x00);
        sid.write(kSusRel, 0xF0);
        sid.write(kControl, SID::kCtrlSawtooth | SID::kCtrlGate);
    }
}

TEST(SidMachineTime, ASecondOfCyclesIsASecondOfAudio)
{
    SID sid;
    sid.setClockHz(kHz);
    uint64_t t = 0;
    sid.clock(t);
    tone(sid, 0x1C);
    const auto out = runFor(sid, t, 1000);
    // pending_ holds up to 63 not yet handed over; nothing is lost.
    EXPECT_NEAR(static_cast<double>(out.size() + sid.buffered()), SID::kSampleRate, 1.0);
}

// A reset starts the CPU's cycle count again from 0. The SID must take that as a new
// origin: subtracting its old catch-up point from the new, smaller count wrapped, and
// the first catch-up after any reset tried to render billions of samples -- the
// machine froze. A millisecond after the reset is a millisecond of sound.
TEST(SidMachineTime, AResetStartsTheClockAgainRatherThanWrapping)
{
    SID sid;
    sid.setClockHz(kHz);
    uint64_t t = 0;
    sid.clock(t);
    runFor(sid, t, 100);                            // the machine has been running
    const int before = sid.buffered();

    sid.restartClock();                             // reset: the count is 0 again
    sid.clock(0);
    sid.clock(kHz / 1000);                          // one millisecond
    EXPECT_NEAR(sid.buffered() - before, SID::kSampleRate / 1000, 2);
}

// A pitch change is heard from the moment it was written, not from the next time the
// sound system happened to ask.
TEST(SidMachineTime, APitchChangeIsHeardWhereItWasWritten)
{
    SID sid;
    sid.setClockHz(kHz);
    uint64_t t = 0;
    sid.clock(t);
    tone(sid, 0x10);                                // about 244 Hz
    const auto low = runFor(sid, t, 500);
    sid.write(kFreqHi, 0x20);                       // about 489 Hz
    const auto high = runFor(sid, t, 500);

    const int a = risingCrossings(low), b = risingCrossings(high);
    EXPECT_NEAR(a, 122, 6) << "the first half is not the first pitch";
    EXPECT_NEAR(b, 244, 8) << "the second half is not the second pitch";
}

// A shot: gate on, 30 ms, gate off. Rendered in blocks from snapshots, a sound this
// short could start and end between two of them and never be heard.
TEST(SidMachineTime, AShortSoundIsNotLost)
{
    SID sid;
    sid.setClockHz(kHz);
    uint64_t t = 0;
    sid.clock(t);
    runFor(sid, t, 10);
    tone(sid, 0x1C);
    auto burst = runFor(sid, t, 30);
    sid.write(kControl, SID::kCtrlSawtooth);        // release
    runFor(sid, t, 50);
    EXPECT_GT(rms(burst), 0.1) << "a 30 ms sound was not heard";
}

// With nothing draining it -- no audio device, as here -- the buffer stays bounded.
TEST(SidMachineTime, TheBufferCannotGrowWithoutLimit)
{
    SID sid;
    sid.setClockHz(kHz);
    sid.clock(0);
    tone(sid, 0x1C);
    sid.clock(10 * kHz);                            // ten seconds, never drained
    EXPECT_LE(sid.buffered(), SID::kRingMax + 64);
}

// --- playback -------------------------------------------------------------------
//
// playback() always fills the sound system's request and is a plain copy. An earlier
// version stretched and squeezed the audio to steer the buffer's level, and clipped
// notes. These pin that it copies one-for-one and pads only when it must.

namespace
{
    void fill(SID &sid, uint64_t &t, int n)
    {
        t += static_cast<uint64_t>(n) * kHz / SID::kSampleRate;
        sid.clock(t);
    }
}

TEST(SidPlayback, ARequestIsServedOneForOne)
{
    SID sid; sid.setClockHz(kHz);
    uint64_t t = 0; sid.clock(t); tone(sid, 0x1C);
    fill(sid, t, 3000);
    const int before = sid.buffered();
    std::vector<int16_t> out(1024);
    sid.playback(out.data(), 1024);
    EXPECT_EQ(before - sid.buffered(), 1024) << "playback resampled instead of copying";
    EXPECT_EQ(sid.underrunSamples(), 0u);
}

// Short: what the buffer holds, then the last of it held for the rest.
TEST(SidPlayback, AShortBufferIsPaddedWithItsLastSample)
{
    SID sid; sid.setClockHz(kHz);
    uint64_t t = 0; sid.clock(t); tone(sid, 0x1C);
    fill(sid, t, 1200);
    std::vector<int16_t> first(1024);
    sid.playback(first.data(), 1024);               // leaves ~176 buffered
    const int left = sid.buffered();
    ASSERT_GT(left, 0);
    ASSERT_LT(left, 1024);

    std::vector<int16_t> out(1024);
    sid.playback(out.data(), 1024);
    for (int i = left; i < 1024; ++i)
        ASSERT_EQ(out[i], out[left - 1]) << "padding at " << i << " is not the held sample";
    EXPECT_EQ(sid.underrunSamples(), static_cast<uint64_t>(1024 - left));
    EXPECT_EQ(sid.buffered(), 0);
}

TEST(SidPlayback, AnEmptyBufferHoldsTheLastSample)
{
    SID sid; sid.setClockHz(kHz);
    uint64_t t = 0; sid.clock(t); tone(sid, 0x1C);
    fill(sid, t, 1024);
    std::vector<int16_t> out(1024);
    sid.playback(out.data(), 1024);
    const int16_t last = out.back();
    sid.playback(out.data(), 1024);                 // nothing left
    EXPECT_EQ(out[0], last);
    EXPECT_EQ(out[1023], last);
}
