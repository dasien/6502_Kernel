# S.A.M. (Software Automatic Mouth) -- the C64 source, as published

S.A.M. was written by **Mark Barton** and published in 1982 by **Don't Ask
Software** (later SoftVoice, Inc.): the first commercial all-software speech
synthesizer, on the Apple II, Atari 8-bit and Commodore 64. `RECITER` turns English
text into SAM's phonemes; SAM turns phonemes into sound.

These files are **DLehenbauer/c64-sam** exactly as published, at commit
`eff6ed0d021ebefb0357398619a67a9922d83464` (2022-06-22):
<https://github.com/DLehenbauer/c64-sam>. That repository is a disassembly of the
C64 version in ca65 syntax, documented with the help of earlier work -- Groepaz's
C64 disassembly, Sebastian Macke's C port and its refactorings by Vidar Hokstad and
Aidan Dodds -- and released, its README says, with the author's permission.
`README.upstream.md` and `docs/` are that repository's own notes and phoneme table.

| File | What it is |
|---|---|
| `sam.s` | SAM and RECITER, 6502, ca65 |
| `startup.s` | the stub that installs SAM's BASIC wedge |
| `c64/c64.cfg` | the linker config that puts them at the C64 addresses ($7D00, $9500) |
| `lkg/sam.c64`, `lkg/sam.c64.lbl` | that repository's build of `sam.s` (a C64 `.PRG` at $0801) and its labels |

`lkg/sam.c64` is what the port is checked against: `tests/test_say.cpp` runs it on
MFC's CPU, with no ROMs mapped, and requires the port's RECITER to produce the same
phonemes for every word. It is also how the port's one real bug was found -- see the
character map note at the top of `programs/say/sam.s`.

MFC's port is `programs/say/sam.s`, made from this `sam.s`; the differences are
listed at the top of that file. Keep this copy unmodified, so the port can always be
compared against what it came from.

S.A.M. is out of print and its publisher no longer exists in that form. Neither
this repository nor the ports it builds on carries a licence; MFC treats S.A.M. as
abandonware, as those projects do, and credits its author and the people who
recovered it.

The JavaScript port by Christian Schiffler (<https://github.com/discordier/sam>)
supplied the reciter test fixtures the MFC tests check against.
