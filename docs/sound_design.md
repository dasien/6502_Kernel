# Sound — the MFC SID sound chip

MFC has a software sound chip modelled on the MOS 6581 and 8580 SID. It provides three
voices, per-voice ADSR envelopes, four waveforms and a multimode filter. It is
register-faithful to the real SID, keeping the same 29-register layout and only
relocating it from `$D400` to the free I/O block at `$FE38`, so existing SID knowledge
and music transfer over directly.

The synthesizer is written from scratch from public SID documentation. It is
musically faithful (register-compatible, familiar pitches) but not cycle-exact:
envelopes use a float exponential approximation, the filter is a TPT
state-variable filter, and combined waveforms use a bitwise-AND approximation.
Ring/sync modulation are not modeled. No reSID (or other GPL) code is used.

## Architecture

Mirrors the ACIA/Modem split:

- `SID` is the headless core, in `include/computer/SID.h` and
  `src/computer/SID.cpp`. It holds the mutex-guarded register array and synthesises
  44.1 kHz mono PCM on demand through `generateSamples()`. It uses no Qt and is fully
  unit-tested by `tests/test_sid.cpp`. Its oscillators use a phase accumulator at a
  nominal 1 MHz SID clock, so standard SID frequency values give familiar pitches.
- `SidAudio` is the Qt bridge and exists only in the GUI build, in
  `include/computer/SidAudio.h` and `src/computer/SidAudio.cpp`. It is a pull-mode
  `QAudioSink` whose `QIODevice` calls `generateSamples()` on the audio thread. It is
  built only when Qt Multimedia is present, which `HAVE_SID_AUDIO` records. A Qt build
  without it still compiles and simply runs silent.
- `Memory` handles dispatch, routing `$FE38-$FE54` to the `SID` through
  `isSidAddress`, exactly as it does for the VIC and ACIA register ports.

## Register map (`$FE38-$FE54`)

There are three voices, and voice n has its base at `$FE38 + n*7`.

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

The kernel uses voice 1 for system sound (`kernel.asm`):

- Printing ASCII `$07` through `PRINT_CHAR` rings a short beep of roughly 130 ms. It
  gates a tone on and arms `BEEP_TIMER`, and the 60 Hz timer IRQ counts that down and
  gates the tone off again. The beep does not block, so a burst of BELs, such as
  EhBASIC hitting a full input buffer, simply holds and re-triggers the tone instead
  of stalling the machine.
- Two jump-table entries make up the sound ABI. `K_SOUND_TONE` at `$FF33` plays a
  sustained tone on voice 1, taking the frequency low byte in `A` and the high byte in
  `X`, and it plays until something stops it. `K_SOUND_OFF` at `$FF36` stops voice 1
  by gating it off.
- `SOUND_ENABLE`, in zero page at `$29` and defaulting to 1, is a master mute that
  both the BEL beep and the sound ABI honour. It is the hook a future settings
  facility would use to turn sound off.

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

`examples/sid_filter_sweep.asm` is a fuller demonstration. It pokes the voice and
filter registers directly and sweeps the low-pass cutoff.
