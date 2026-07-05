# Sound — the MFC SID sound chip

MFC has a software sound chip modeled on the **MOS 6581/8580 SID**: three voices,
per-voice ADSR envelopes, four waveforms, and a multimode filter. It is
**register-faithful** to the real SID — the same 29-register layout, just
relocated from `$D400` to the free I/O block at **`$FE38`** — so SID knowledge and
music transfer over directly.

The synthesizer is written from scratch from public SID documentation. It is
musically faithful (register-compatible, familiar pitches) but not cycle-exact:
envelopes use a float exponential approximation, the filter is a TPT
state-variable filter, and combined waveforms use a bitwise-AND approximation.
Ring/sync modulation are not modeled. **No reSID (or other GPL) code is used.**

## Architecture

Mirrors the ACIA/Modem split:

- **`Sid` (headless core)** — `include/computer/Sid.h`, `src/computer/Sid.cpp`.
  Holds the register array (mutex-guarded) and synthesizes 44.1 kHz mono PCM on
  demand via `generateSamples()`. No Qt; fully unit-tested (`tests/test_sid.cpp`).
  Oscillators use a phase accumulator at a nominal 1 MHz SID clock, so standard
  SID frequency values give familiar pitches.
- **`SidAudio` (Qt bridge, GUI-only)** — `include/computer/SidAudio.h`,
  `src/computer/SidAudio.cpp`. A pull-mode `QAudioSink` whose `QIODevice` calls
  `generateSamples()` on the audio thread. Built only when Qt Multimedia is
  present (`HAVE_SID_AUDIO`); a Qt build without it still compiles and runs silent.
- **Dispatch** — `Memory` routes `$FE38-$FE54` to the `Sid` (`isSidAddress`),
  exactly like the VIC/ACIA register ports.

## Register map (`$FE38-$FE54`)

Three voices; voice *n* base = `$FE38 + n*7`:

| Offset | Register | Notes |
|--------|----------|-------|
| +0/+1 | `FREQ_LO/HI` | 16-bit frequency; `Fout = FREQ * clock / 2^24` |
| +2/+3 | `PW_LO/HI` | 12-bit pulse width (bits 0-11) |
| +4 | `CONTROL` | b0 gate, b1 sync, b2 ring, b3 test, b4 triangle, b5 sawtooth, b6 pulse, b7 noise |
| +5 | `ATK/DEC` | attack (hi nibble) / decay (lo nibble) |
| +6 | `SUS/REL` | sustain (hi nibble) / release (lo nibble) |

Global registers:

| Address | Register | Notes |
|---------|----------|-------|
| `$FE4D/4E` | `FC_LO/HI` | 11-bit filter cutoff (FC_LO bits 0-2, FC_HI 8 bits) |
| `$FE4F` | `RES_FILT` | resonance (hi nibble); lo nibble routes voices 1-3 (bits 0-2) through the filter |
| `$FE50` | `MODE_VOL` | b4 LP, b5 BP, b6 HP, b7 voice-3 off; lo nibble = master volume |
| `$FE53` | `OSC3` | voice-3 oscillator read-back (read-only) |
| `$FE54` | `ENV3` | voice-3 envelope read-back (read-only) |

Sync/ring bits and the paddle registers (`POTX/POTY`) are accepted but inert.

## Kernel integration

The kernel uses **voice 1** for system sound (`kernel.asm`):

- **BEL** — printing ASCII `$07` through `PRINT_CHAR` rings a short (~130 ms)
  non-blocking beep: it gates a tone on and arms `BEEP_TIMER`; the ~60 Hz timer
  IRQ counts down and gates it off. Non-blocking so a burst of BELs (e.g. EhBASIC
  on a full input buffer) just holds/re-triggers the tone instead of stalling.
- **Sound ABI** (jump table):
  - `K_SOUND_TONE` (`$FF33`) — play a sustained tone on voice 1.
    Input: `A` = frequency low, `X` = frequency high. Plays until stopped.
  - `K_SOUND_OFF` (`$FF36`) — stop voice 1 (gate off).
- **`SOUND_ENABLE`** (zero page `$29`, default 1) — master mute honored by the BEL
  beep and the sound ABI. This is the hook for a future `SETTINGS` sound on/off.

## Trying it

From the monitor (`MON`):

```
; short beep (LDA #$07 : JSR K_PRINT_CHAR : RTS)
W:0800    then enter:  A9 07 20 00 FF 60      then G:0800

; sustained tone ~440 Hz (LDA #$D6 : LDX #$1C : JSR K_SOUND_TONE : RTS)
W:0800    then enter:  A9 D6 A2 1C 20 33 FF 60   then G:0800
; stop it (JSR K_SOUND_OFF : RTS)
W:0810    then enter:  20 36 FF 60               then G:0810
```

`examples/sid_filter_sweep.asm` is a fuller demo: it pokes the voice and filter
registers directly and sweeps the low-pass cutoff.
