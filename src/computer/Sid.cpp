/**
 * @file Sid.cpp
 * @brief Software SID (6581/8580) synthesis implementation.
 *
 * Three voices (triangle/sawtooth/pulse/noise) with per-voice ADSR envelopes and
 * master volume (phase 1), plus a Chamberlin multimode filter (LP/BP/HP with
 * resonance + per-voice routing), OSC3/ENV3 voice-3 read-back, and bitwise-AND
 * combined waveforms (phase 2). Ring/sync modulation are not modeled.
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
        constexpr double kPi = 3.14159265358979323846;

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
        const int idx = address - kRegBase;
        // Voice-3 oscillator/envelope read-back reflect the live synthesis state
        // (published atomically from the audio thread). Paddles are unmodeled.
        if (idx == kRegOsc3)
            return osc3_val_.load(std::memory_order_relaxed);
        if (idx == kRegEnv3)
            return env3_val_.load(std::memory_order_relaxed);
        if (idx == kRegPotX || idx == kRegPotY)
            return 0;
        std::lock_guard<std::mutex> lock(mtx_);
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
        filt_ic1_ = 0.0;
        filt_ic2_ = 0.0;
        osc3_val_.store(0, std::memory_order_relaxed);
        env3_val_.store(0, std::memory_order_relaxed);
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
    // advance the voice's phase. Each waveform is generated as a 12-bit unsigned
    // value and combined waveforms are bitwise-ANDed -- a recognizable
    // approximation of the SID's combined outputs.
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

        // Build each selected waveform as a 12-bit value (0..4095) and AND them.
        int acc = 0x0FFF;
        bool any = false;
        if (ctrl & kCtrlTriangle)
        {
            const double t = (p < 0.5) ? (p * 2.0) : (2.0 - p * 2.0); // 0..1..0
            acc &= static_cast<int>(t * 4095.0);
            any = true;
        }
        if (ctrl & kCtrlSawtooth)
        {
            acc &= static_cast<int>(p * 4095.0);
            any = true;
        }
        if (ctrl & kCtrlPulse)
        {
            acc &= (p < duty) ? 0x0FFF : 0x000;
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
                // Take 8 spread bits as the output byte (per the SID noise tap set),
                // then scale to the 12-bit domain.
                const uint32_t l = v.lfsr;
                const uint32_t byte =
                    ((l >> 22) & 1) << 7 | ((l >> 20) & 1) << 6 |
                    ((l >> 16) & 1) << 5 | ((l >> 13) & 1) << 4 |
                    ((l >> 11) & 1) << 3 | ((l >> 7) & 1) << 2 |
                    ((l >> 4) & 1) << 1 | ((l >> 2) & 1);
                v.noise_level = static_cast<double>(byte << 4);
            }
            acc &= static_cast<int>(v.noise_level);
            any = true;
        }

        if (!any)
            return 0.0;
        // Map the 12-bit unsigned result to a bipolar [-1,1] sample.
        return (acc / 2047.5) - 1.0;
    }

    void Sid::generateSamples(int16_t *out, int frames)
    {
        // Snapshot the registers so we don't hold the lock while synthesizing.
        std::array<uint8_t, kNumRegs> regs;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            regs = regs_;
        }

        const uint8_t mode_vol = regs[kRegModeVol];
        const uint8_t res_filt = regs[kRegResFilt];
        const double master = (mode_vol & 0x0F) / 15.0;

        // Filter coefficients from the 11-bit cutoff and 4-bit resonance. Cutoff
        // maps roughly 30 Hz .. 12 kHz across the range. This is a TPT
        // (topology-preserving-transform) state-variable filter, which is stable
        // for any cutoff up to Nyquist -- unlike a naive Chamberlin SVF, which
        // blows up above ~fs/6.
        const int fc = ((regs[kRegFcHi] << 3) | (regs[kRegFcLo] & 0x07)); // 0..2047
        const double cutoff = 30.0 + (fc / 2047.0) * (12000.0 - 30.0);
        const double res = (res_filt >> 4) / 15.0;
        const double g = std::tan(kPi * cutoff / kSampleRate);
        const double k = 1.4 - res * 1.3; // damping (1/Q): ~1.4 (low Q) .. ~0.1 (high Q)
        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double a2 = g * a1;
        const double a3 = g * a2;
        const bool filt_on = (mode_vol & (kModeLowPass | kModeBandPass | kModeHighPass)) != 0;

        double v3_osc = 0.0; // last voice-3 oscillator sample, for OSC3 read-back
        for (int i = 0; i < frames; ++i)
        {
            double direct = 0.0; // voices routed straight to the output
            double filt_in = 0.0; // voices routed through the filter
            for (int vi = 0; vi < kNumVoices; ++vi)
            {
                const uint8_t *vr = &regs[vi * kVoiceRegs];
                Voice &v = voices_[vi];
                advanceEnvelope(v, vr);
                const double osc = oscillatorOutput(v, vr);
                const double vout = osc * v.env;
                if (vi == 2)
                    v3_osc = osc;

                // Voice 3 can be disconnected from the mix (used as a mod source).
                if (vi == 2 && (mode_vol & kModeVoice3Off))
                    continue;

                if (res_filt & (kFiltVoice1 << vi))
                    filt_in += vout;
                else
                    direct += vout;
            }

            // TPT state-variable filter on the routed voices.
            double filtered = 0.0;
            if (filt_on)
            {
                const double v3 = filt_in - filt_ic2_;
                const double v1 = a1 * filt_ic1_ + a2 * v3;
                const double v2 = filt_ic2_ + a2 * filt_ic1_ + a3 * v3;
                filt_ic1_ = 2.0 * v1 - filt_ic1_;
                filt_ic2_ = 2.0 * v2 - filt_ic2_;
                if (mode_vol & kModeLowPass)
                    filtered += v2;                       // low-pass
                if (mode_vol & kModeBandPass)
                    filtered += v1;                       // band-pass
                if (mode_vol & kModeHighPass)
                    filtered += filt_in - k * v1 - v2;    // high-pass
            }
            else
            {
                // No filter mode selected: routed voices pass through unaltered.
                filtered = filt_in;
            }

            double sample = ((direct + filtered) / kNumVoices) * master * 0.9;
            sample = std::clamp(sample, -1.0, 1.0);
            out[i] = static_cast<int16_t>(sample * 32767.0);
        }

        // Publish voice-3 oscillator/envelope for the OSC3/ENV3 read-back registers.
        osc3_val_.store(static_cast<uint8_t>((v3_osc + 1.0) * 127.5), std::memory_order_relaxed);
        env3_val_.store(static_cast<uint8_t>(voices_[2].env * 255.0), std::memory_order_relaxed);
    }
} // namespace Computer
