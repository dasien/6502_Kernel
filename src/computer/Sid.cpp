/**
 * @file Sid.cpp
 * @brief Software SID (6581/8580) synthesis implementation.
 *
 * Phase 1: three voices (triangle/sawtooth/pulse/noise), per-voice ADSR envelopes,
 * and master volume. The multimode filter, OSC3/ENV3 read-back, and combined-
 * waveform tables arrive in phase 2; the filter registers ($FE4F-$FE52) are stored
 * but inert for now.
 *
 * Written from public SID documentation -- no reSID/GPL code.
 */

#include "computer/Sid.h"

#include <algorithm>
#include <cmath>

namespace Computer
{
    namespace
    {
        // Attack times (ms for 0 -> full) for the 16 attack-rate nibble values, per
        // the SID datasheet. Decay/Release use the same table scaled x3 (the SID's
        // decay/release from full->0 takes three times as long as the attack).
        constexpr double kAttackMs[16] = {
            2.0, 8.0, 16.0, 24.0, 38.0, 56.0, 68.0, 80.0,
            100.0, 250.0, 500.0, 800.0, 1000.0, 3000.0, 5000.0, 8000.0};

        // Convert a duration (ms) for a full-scale sweep into a per-sample linear
        // step. Guard against a zero-length sweep.
        double stepPerSample(double ms)
        {
            const double samples = (ms / 1000.0) * Sid::kSampleRate;
            return samples > 1.0 ? (1.0 / samples) : 1.0;
        }
    } // namespace

    Sid::Sid()
    {
        reset();
    }

    bool Sid::isSidAddress(uint16_t address)
    {
        return address >= kRegBase && address <= kRegLast;
    }

    uint8_t Sid::read(uint16_t address) const
    {
        if (!isSidAddress(address))
            return 0;
        std::lock_guard<std::mutex> lock(mtx_);
        // Read-only paddle/oscillator/envelope read-back registers are not modeled
        // in phase 1 and read as 0. Everything else reads back the last write.
        const int idx = address - kRegBase;
        if (idx == kRegPotX || idx == kRegPotY || idx == kRegOsc3 || idx == kRegEnv3)
            return 0;
        return regs_[idx];
    }

    void Sid::write(uint16_t address, uint8_t value)
    {
        if (!isSidAddress(address))
            return;
        std::lock_guard<std::mutex> lock(mtx_);
        regs_[address - kRegBase] = value;
    }

    void Sid::reset()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        regs_.fill(0);
        voices_ = {};
    }

    // Advance the ADSR envelope one sample. Gate transitions are detected from the
    // control register's gate bit each sample, so no edge tracking is needed on the
    // register-write side. Decay and release approximate the SID's exponential
    // curve with a geometric decay toward the target level.
    void Sid::advanceEnvelope(Voice &v, const uint8_t *vr)
    {
        const bool gate = (vr[kOffControl] & kCtrlGate) != 0;
        const uint8_t ad = vr[kOffAttackDecay];
        const uint8_t sr = vr[kOffSustainRelease];
        const double attack_step = stepPerSample(kAttackMs[ad >> 4]);
        const double decay_ms = kAttackMs[ad & 0x0F] * 3.0;
        const double release_ms = kAttackMs[sr & 0x0F] * 3.0;
        const double sustain = (sr >> 4) / 15.0; // sustain level 0..1

        // Gate edges drive the state machine.
        if (gate && !v.last_gate)
            v.env_phase = EnvPhase::Attack;
        else if (!gate && v.last_gate)
            v.env_phase = EnvPhase::Release;
        v.last_gate = gate;

        switch (v.env_phase)
        {
        case EnvPhase::Attack:
            v.env += attack_step;
            if (v.env >= 1.0)
            {
                v.env = 1.0;
                v.env_phase = EnvPhase::Decay;
            }
            break;
        case EnvPhase::Decay:
            // Exponential-ish approach toward the sustain level.
            v.env += (sustain - v.env) * stepPerSample(decay_ms) * 2.0;
            if (std::abs(v.env - sustain) < 0.001)
                v.env = sustain;
            break;
        case EnvPhase::Release:
            v.env += (0.0 - v.env) * stepPerSample(release_ms) * 2.0;
            if (v.env < 0.0005)
            {
                v.env = 0.0;
                v.env_phase = EnvPhase::Idle;
            }
            break;
        case EnvPhase::Idle:
            v.env = 0.0;
            break;
        }
    }

    // Produce one oscillator sample in [-1,1] for the selected waveform(s), and
    // advance the voice's phase. Combined waveforms are ANDed (a coarse but
    // recognizable approximation of the SID's combined outputs).
    double Sid::oscillatorOutput(Voice &v, const uint8_t *vr)
    {
        const uint8_t ctrl = vr[kOffControl];
        const uint16_t freq = static_cast<uint16_t>(vr[kOffFreqLo] | (vr[kOffFreqHi] << 8));
        const double hz = freq * (kSidClock / 16777216.0);

        // TEST bit holds the oscillator reset at zero.
        if (ctrl & kCtrlTest)
        {
            v.phase = 0.0;
            return 0.0;
        }

        v.phase += hz / kSampleRate;
        if (v.phase >= 1.0)
            v.phase -= std::floor(v.phase);

        const double p = v.phase;
        const int pw = ((vr[kOffPwHi] & 0x0F) << 8) | vr[kOffPwLo]; // 0..4095
        const double duty = pw / 4096.0;

        // Each selected waveform contributes; combine by multiplication (AND-like).
        bool any = false;
        double out = 1.0;
        if (ctrl & kCtrlTriangle)
        {
            const double tri = (p < 0.5) ? (4.0 * p - 1.0) : (3.0 - 4.0 * p);
            out = any ? out * tri : tri;
            any = true;
        }
        if (ctrl & kCtrlSawtooth)
        {
            const double saw = 2.0 * p - 1.0;
            out = any ? out * saw : saw;
            any = true;
        }
        if (ctrl & kCtrlPulse)
        {
            const double pulse = (p < duty) ? 1.0 : -1.0;
            out = any ? out * pulse : pulse;
            any = true;
        }
        if (ctrl & kCtrlNoise)
        {
            // Clock the 23-bit LFSR at roughly the oscillator rate; sample-and-hold.
            v.noise_phase += hz / kSampleRate;
            while (v.noise_phase >= 1.0)
            {
                v.noise_phase -= 1.0;
                const uint32_t bit = ((v.lfsr >> 22) ^ (v.lfsr >> 17)) & 1;
                v.lfsr = ((v.lfsr << 1) | bit) & 0x7FFFFF;
                // Take 8 spread bits as the output byte (per the SID noise tap set).
                const uint32_t l = v.lfsr;
                const uint32_t byte =
                    ((l >> 22) & 1) << 7 | ((l >> 20) & 1) << 6 |
                    ((l >> 16) & 1) << 5 | ((l >> 13) & 1) << 4 |
                    ((l >> 11) & 1) << 3 | ((l >> 7) & 1) << 2 |
                    ((l >> 4) & 1) << 1 | ((l >> 2) & 1);
                v.noise_level = (byte / 127.5) - 1.0;
            }
            out = any ? out * v.noise_level : v.noise_level;
            any = true;
        }

        return any ? out : 0.0;
    }

    void Sid::generateSamples(int16_t *out, int frames)
    {
        // Snapshot the registers so we don't hold the lock while synthesizing.
        std::array<uint8_t, kNumRegs> regs;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            regs = regs_;
        }

        const double master = (regs[kRegModeVol] & 0x0F) / 15.0;

        for (int i = 0; i < frames; ++i)
        {
            double mix = 0.0;
            for (int vi = 0; vi < kNumVoices; ++vi)
            {
                const uint8_t *vr = &regs[vi * kVoiceRegs];
                Voice &v = voices_[vi];
                advanceEnvelope(v, vr);
                const double osc = oscillatorOutput(v, vr);
                mix += osc * v.env;
            }
            // Average the voices, apply master volume, leave headroom, and clamp.
            double sample = (mix / kNumVoices) * master * 0.9;
            sample = std::clamp(sample, -1.0, 1.0);
            out[i] = static_cast<int16_t>(sample * 32767.0);
        }
    }
} // namespace Computer
