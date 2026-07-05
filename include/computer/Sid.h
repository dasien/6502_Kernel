/**
 * @file Sid.h
 * @brief Software sound chip modeled on the MOS 6581/8580 SID.
 * @author 6502 Kernel Project
 */

#ifndef SID_H
#define SID_H

#include <cstdint>
#include <array>
#include <atomic>
#include <mutex>

namespace Computer
{
    /**
     * @class Sid
     * @brief A register-faithful software SID (6581/8580) sound chip.
     *
     * The real SID exposes 29 registers ($D400-$D41C). This model relocates that
     * exact register layout to the free I/O block at $FE38-$FE54 (just past the VIC
     * port) so SID knowledge and music transfer over directly. Three voices, each
     * with a 16-bit frequency, 12-bit pulse width, a control register (gate + four
     * waveforms), and an ADSR envelope; plus a master volume (the multimode filter
     * and OSC3/ENV3 read-back arrive in phase 2).
     *
     * The chip is headless (no Qt): it holds register state written by the CPU and
     * synthesizes PCM on demand via generateSamples(), which the GUI's QAudioSink
     * bridge pulls on the audio thread. Register writes and sample generation may
     * run on different threads, so a mutex guards the register array; the per-voice
     * oscillator/envelope state is owned solely by the audio thread.
     *
     * This synthesizer is written from scratch from public SID documentation. It is
     * musically faithful (register-compatible, familiar pitches) but not cycle-exact
     * -- envelopes use a float exponential approximation rather than the segmented
     * hardware counter, the filter is a Chamberlin state-variable filter, and
     * combined waveforms use a bitwise-AND approximation. Ring/sync modulation are
     * not modeled. No reSID (or other GPL) code is used.
     *
     * @see Memory, Computer6502, Acia
     */
    class Sid
    {
    public:
        // --- Register map: 29 registers at $FE38-$FE54, in real-SID order. ---
        static constexpr uint16_t kRegBase = 0xFE38;
        static constexpr int kNumRegs = 29;
        static constexpr uint16_t kRegLast = kRegBase + kNumRegs - 1; // $FE54

        // Per-voice register offsets (voice n base = n*7).
        static constexpr int kVoiceRegs = 7;
        static constexpr int kOffFreqLo = 0;
        static constexpr int kOffFreqHi = 1;
        static constexpr int kOffPwLo = 2;
        static constexpr int kOffPwHi = 3;   ///< low nibble = PW bits 8-11
        static constexpr int kOffControl = 4;
        static constexpr int kOffAttackDecay = 5;  ///< hi nibble attack, lo nibble decay
        static constexpr int kOffSustainRelease = 6; ///< hi nibble sustain, lo nibble release

        // Global registers (offsets from kRegBase).
        static constexpr int kRegFcLo = 21;    ///< filter cutoff low (bits 0-2)
        static constexpr int kRegFcHi = 22;    ///< filter cutoff high (8 bits)
        static constexpr int kRegResFilt = 23; ///< resonance (hi nibble) + routing (lo)
        static constexpr int kRegModeVol = 24; ///< filter mode (hi) + master volume (lo)
        static constexpr int kRegPotX = 25;    ///< paddle X (read-only, unused)
        static constexpr int kRegPotY = 26;    ///< paddle Y (read-only, unused)
        static constexpr int kRegOsc3 = 27;    ///< voice-3 oscillator read-back (RO)
        static constexpr int kRegEnv3 = 28;    ///< voice-3 envelope read-back (RO)

        // Control-register bits.
        static constexpr uint8_t kCtrlGate = 0x01;
        static constexpr uint8_t kCtrlSync = 0x02;
        static constexpr uint8_t kCtrlRing = 0x04;
        static constexpr uint8_t kCtrlTest = 0x08;
        static constexpr uint8_t kCtrlTriangle = 0x10;
        static constexpr uint8_t kCtrlSawtooth = 0x20;
        static constexpr uint8_t kCtrlPulse = 0x40;
        static constexpr uint8_t kCtrlNoise = 0x80;

        // RES_FILT ($FE4F+... i.e. kRegResFilt): low nibble routes voices 1-3
        // (bit0=v1, bit1=v2, bit2=v3, bit3=external) through the filter; high
        // nibble is resonance.
        static constexpr uint8_t kFiltVoice1 = 0x01;
        static constexpr uint8_t kFiltVoice2 = 0x02;
        static constexpr uint8_t kFiltVoice3 = 0x04;

        // MODE_VOL: low nibble = master volume; high nibble selects filter mode
        // and voice-3 disconnect.
        static constexpr uint8_t kModeLowPass = 0x10;
        static constexpr uint8_t kModeBandPass = 0x20;
        static constexpr uint8_t kModeHighPass = 0x40;
        static constexpr uint8_t kModeVoice3Off = 0x80;

        // Synthesis constants.
        static constexpr int kSampleRate = 44100;
        static constexpr double kSidClock = 1000000.0; ///< nominal SID master clock (Hz)
        static constexpr int kNumVoices = 3;

        Sid();

        // --- Memory-mapped register port ($FE38-$FE54) ---
        [[nodiscard]] static bool isSidAddress(uint16_t address);
        [[nodiscard]] uint8_t read(uint16_t address) const;
        void write(uint16_t address, uint8_t value);

        // --- Audio backend interface (called on the audio thread) ---
        /**
         * @brief Synthesize @p frames mono 16-bit samples into @p out.
         *
         * Takes a snapshot of the registers under the lock, then synthesizes without
         * holding it (oscillator/envelope state is audio-thread-private).
         */
        void generateSamples(int16_t *out, int frames);

        /// @brief Reset all registers and synthesis state (silence).
        void reset();

    private:
        // Envelope state machine (one per voice).
        enum class EnvPhase { Idle, Attack, Decay, Release };

        struct Voice
        {
            double phase = 0.0;   ///< oscillator phase in [0,1)
            uint32_t lfsr = 0x7FFFFF; ///< 23-bit noise shift register
            double noise_phase = 0.0; ///< accumulator for noise clocking
            double noise_level = 0.0; ///< current sample-and-hold noise output
            EnvPhase env_phase = EnvPhase::Idle;
            double env = 0.0;     ///< envelope level [0,1]
            bool last_gate = false;
        };

        // Register array (guarded by mtx_). Indexed 0..kNumRegs-1.
        std::array<uint8_t, kNumRegs> regs_{};
        mutable std::mutex mtx_;

        // Audio-thread-private synthesis state.
        std::array<Voice, kNumVoices> voices_{};
        double filt_ic1_ = 0.0;   ///< TPT state-variable filter integrator 1 state
        double filt_ic2_ = 0.0;   ///< TPT state-variable filter integrator 2 state

        // Voice-3 read-back, published from the audio thread for the OSC3/ENV3
        // read-only registers (read on the CPU thread; slight staleness is fine).
        std::atomic<uint8_t> osc3_val_{0};
        std::atomic<uint8_t> env3_val_{0};

        // Per-voice synthesis helpers (operate on a register snapshot).
        static double oscillatorOutput(Voice &v, const uint8_t *vr);
        static void advanceEnvelope(Voice &v, const uint8_t *vr);
    };
} // namespace Computer

#endif // SID_H
