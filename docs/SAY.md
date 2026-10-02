# SAY — S.A.M., the Software Automatic Mouth

S.A.M. was written by Mark Barton and published by Don't Ask Software in 1982. It was
the first speech synthesizer sold as software alone, for the Apple II, the Atari 8-bit
and the Commodore 64. `SAY.PRG` is the C64 version, moved to MFC's sound chip. It reads
English, or its own phonetic spelling, aloud.

## Saying something

At the `]` prompt:

```
SAY HELLO, HOW ARE YOU?
```

says it and comes back to the prompt. Only the first 47 characters of a command line
reach a program, so for anything longer start `SAY` on its own and type at its `>`
prompt:

```
SAY
> I AM THE SOFTWARE AUTOMATIC MOUTH.
> .                          (says the last line again)
>                            (an empty line, or ESC, leaves)
```

A line can be up to 120 characters. Punctuation matters: a full stop lets the voice
fall and pause, and a question mark makes it rise.

## Phonemes

English goes through RECITER, which guesses how each word is spoken. When it guesses
wrong, spell the sound yourself: start the line with `]` and write SAM's phonemes, with
a digit after a vowel for stress (1 is strongest, 9 weakest).

```
> ]/HEH4LOW.
> ]DHIHS IHZ EY3 TEH4ST.
```

| Kind | Phonemes |
|---|---|
| Vowels | `IY IH EH AE AA AH AO OH UH UX ER AX IX` |
| Diphthongs | `EY` (made) `AY` (high) `OY` (boy) `AW` (how) `OW` (slow) `UW` (crew) |
| Voiced consonants | `R L W WH Y M N NX` (song) `B D G J Z ZH` (pleasure) `V DH` (then) |
| Unvoiced consonants | `S SH F TH` (thin) `P T K CH /H` (ahead) |
| Special | `UL` (settle) `UM` (astronomy) `UN` (function) `Q` (glottal stop) |
| Rule forms | `YX WX` (soft Y, W), `RX LX` (gliding R, L), `/X` (the h in who), `DX` (the flap, as in pity) |

If SAM cannot read a phoneme it says so, and points at where it stopped. The full chart,
with an example word for every sound, is in
[S.A.M.'s manual](https://github.com/discordier/sam/blob/master/docs/manual.md).

## The voice

| Setting | Effect |
|---|---|
| `/SPEED n` | 1-255, higher is slower. 72 is S.A.M.'s own |
| `/PITCH n` | 1-255, higher is **lower**. 64 is S.A.M.'s own |
| `/THROAT n`, `/MOUTH n` | the throat and mouth shapes, 1-255; 128 each is S.A.M. |
| `/VOICE name` | a voice from S.A.M.'s manual (below) |
| `/SHOW` | the current settings |
| `/HELP` | the list of settings |
| `/QUIT` | leave |

| Voice | Speed | Pitch | Throat | Mouth |
|---|---|---|---|---|
| `SAM` | 72 | 64 | 128 | 128 |
| `ELF` | 72 | 64 | 110 | 160 |
| `ROBOT` | 92 | 60 | 190 | 190 |
| `STUFFY` | 82 | 72 | 110 | 105 |
| `LADY` | 82 | 32 | 145 | 145 |
| `ET` | 100 | 64 | 150 | 200 |

Settings last until you leave `SAY`.

## How it works

The SID has no sample channel. S.A.M. holds one voice at a steady level and changes the
master volume many thousands of times a second, so the volume control becomes a 4-bit
loudspeaker driver (`sound_design.md`, "Sampled sound"). Its timing is its own program
loops, written for a 1 MHz 6502, so while it speaks MFC drops to 1 MHz (`CPU_SPEED`,
`$FED4`) and comes back to 4 afterwards. It also speaks with interrupts off, as on the
C64, so the jiffy clock stands still while it talks. If system sound is muted, `SAY`
stays quiet.

## Thanks

S.A.M. by Mark Barton, © 1982 Don't Ask Software. The source `SAY` is built from was
disassembled and documented by DLehenbauer
([c64-sam](https://github.com/DLehenbauer/c64-sam)), drawing on Groepaz's disassembly and
Sebastian Macke's C port. The original is kept in `vendor/c64-sam`, and the port is
`programs/say`.
