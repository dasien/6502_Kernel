# MFC Architecture & Reference

This document is the consolidated internals reference for MFC. It covers the system
overview, the full memory map, the kernel API at the `$FF00` jump table, and the
bank-switched module design.

### Table of Contents

- [Part 1 — System overview](#part-1--system-overview)
  - [Overview](#overview)
  - [Logical Block diagram](#logical-block-diagram)
  - [Components](#components)
  - [Memory map (summary)](#memory-map-summary)
  - [Data flow](#data-flow)
  - [System integration](#system-integration)
- [Part 2 — Memory & zero-page map](#part-2--memory--zero-page-map)
  - [Overall Address Space](#overall-address-space)
  - [Zero Page](#zero-page)
  - [Stack (`$0100-$01FF`)](#stack-0100-01ff)
  - [System Variables (`$0200-$03FF`)](#system-variables-0200-03ff)
  - [Video (VIC) register port (`$FE2D-$FE37`, `$FE62-$FECA`)](#video-vic-register-port-fe2d-fe37-fe62-feca)
  - [Sound (SID) register port (`$FE38-$FE54`)](#sound-sid-register-port-fe38-fe54)
  - [Real-time clock (RTC) register port (`$FE55-$FE60`)](#real-time-clock-rtc-register-port-fe55-fe60)
  - [I/O — PIA (`$FE00-$FE23`)](#io--pia-fe00-fe23)
  - [ROM Layout](#rom-layout)
  - [Interrupt Vectors (`$FFFA-$FFFF`)](#interrupt-vectors-fffa-ffff)
  - [Free RAM for User Programs](#free-ram-for-user-programs)
  - [Key Constants (from `kernel.asm`)](#key-constants-from-kernelasm)
- [Part 3 — Kernel API ($FF00 jump table)](#part-3--kernel-api-ff00-jump-table)
  - [Overview](#overview-1)
  - [Calling convention](#calling-convention)
  - [Jump table (summary)](#jump-table-summary)
  - [Details](#details)

---

## Part 1 — System overview


### Overview

MFC is a software-defined computer built around a virtual WDC 65C02 CPU. The C++ and
Qt host emulates that CPU along with a set of memory-mapped peripherals. On top of it
run a 6502 kernel ROM, which holds the BIOS and MFC-DOS, and a set of bank-switched
ROM modules for BASIC, FORTH, and the monitor with its built-in assembler. The display
is 80 by 25 CP437 text in 16 colours.

The rest of this part describes those components and how they connect.

### Logical Block diagram

```
        ┌────────────┐   ┌──────────────┐   ┌────────────────────────────┐
        │ Reset      │──▶│ Timing       │──▶│ CPU6502 (WDC 65C02)        │
        │ Circuit    │   │ Circuit ~1MHz│   │ A/X/Y/SP/P, full CMOS ISA  │
        └────────────┘   └──────────────┘   └─────────────┬──────────────┘
                                                          │ 16-bit bus
        ┌─────────────────────────────────────────────────┴────────────────┐
        │                          Memory (64K)                            │
        │  RAM  •  ROM overlay ($F000-$FFFF)  •  bank window ($B000-$EFFF) │
        │  •  I/O page routed to peripherals ($FE00-$FECA)                 │
        └───┬──────┬───────┬───────┬───────┬───────┬───────────────────────┘
            ▼      ▼       ▼       ▼       ▼       ▼
        ┌──────┐┌─────┐┌──────┐┌─────┐┌─────┐┌───────────┐
        │ VIC  ││ PIA ││ ACIA ││ SID ││ RTC ││BlockDevice│
        │video ││kbd/ ││6551  ││sound││clock││ FAT16 disk│
        │port  ││FIO/ ││serial││     ││     ││  image    │
        │      ││timer││      ││     ││     ││           │
        └──┬───┘└──┬──┘└──┬───┘└─────┘└─────┘└───────────┘
           ▼       ▼      ▼
       80x25    keyboard  Modem 
       display
```

> The same machine drawn as a physical board is in [board.md](board.md).

### Components

All emulated devices live in `src/computer/`, with their headers in
`include/computer/`. The `Computer6502` class wires them together.

- `CPU6502` is a cycle-stepped WDC 65C02 with the full CMOS instruction set and
  correct decimal-mode flags. It has been validated against the Klaus2m5 and amb5l
  functional, decimal, and 65C02-extended test suites.
- `Memory` is the 64K store and the address decoder. The memory map in Part 2
  describes the layout it enforces.
- `VIC` holds the 80 by 25 screen and its per-cell colour and attribute plane inside
  the chip itself, reached through a small register port at `$FE2D-$FE37`. A program
  sets a cell index and then streams glyphs and attributes. Chip-side commands for
  clear, scroll and fill-row avoid per-cell CPU writes. It renders the full 8-bit
  CP437 character set.
- `PIA` provides keyboard input through a circular buffer, the host file I/O ports
  that DOS and the monitor use for `LOAD` and `SAVE`, and the interval-timer IRQ at
  roughly 60 Hz that the system calls the jiffy.
- `ACIA` is an emulated 6551 serial port, and both the terminal and the IRC client
  talk through it. On the GUI build it is paired with `Modem`, a Hayes-AT and telnet
  bridge over `QTcpSocket`, so the machine can contact real BBSes and IRC servers.
- `SID` is a software MOS 6581 or 8580 with three voices, ADSR envelopes and a
  filter, at `$FE38-$FE54`. `SidAudio` streams its PCM to the host audio output when
  Qt Multimedia is present.
- `RTC` is the real-time clock at `$FE55-$FE60`. It backs the DOS `DATE` and `TIME`
  commands, FAT16 file timestamps, and the kernel's RNG seed.
- `BlockDevice` presents a FAT16 disk image named `disk.img`, which the resident DOS
  filesystem reads and writes. Images are built by the host `mkdisk` tool from a
  diskmap bundle.
- `ResetCircuit` handles power-on and warm reset, which loads the `$FFFC` vector
  and sets the startup CPU state. The clock is `Computer6502::kDefaultClockHz`,
  4 MHz, and `MainWindow` runs the CPU against a time budget derived from it.
- `MapFileParser` loads the assembled kernel and module ROM segments into memory from
  their `.map` and binary outputs at startup.

On the GUI build, `src/ui/` adds `MainWindow`, which provides the menus and the zoom
control, and `DisplayWidget`, which blits the VIC screen using the embedded CP437
font.

### Memory map (summary)

```
$0000-$00FF  Zero page (kernel/monitor/DOS workspace)
$0100-$01FF  Stack
$0200-$03FF  System variables (command buffer, DOS/monitor state)
$0800-$87FF  User RAM — disk programs load and run at $0800 (2 KB C stack near the top)
$B000-$EFFF  Bank-switched module window (BASIC 1, FORTH 3, MONITOR 4; 2 free)
$F000-$FFFF  Kernel BIOS; jump table at $FF00, vectors at $FFFA
$FE00-$FECA  Memory-mapped I/O — PIA, VIC port, ACIA, SID, RTC, power, VIC font + sprites
             (carved out of the ROM window; $FECB-$FEFF free)
```

See `Part 2 (Memory and zero-page map)` for the full zero-page allocation, the I/O
register layout, and the `$FF00` kernel ABI jump table.

### CPU clock

The machine runs at 4 MHz, which is set by `Computer6502::kDefaultClockHz`, and
everything timed derives from that figure. `runCycles()` generates the 60 Hz
interval-timer IRQ every `clockHz / 60` cycles, so a second of machine time contains
sixty jiffies whatever the host happens to be doing. `setClockHz()` changes the rate.
The games express their pacing constants in jiffies per tick and so follow the clock
automatically, but they were tuned at this speed, so a large change would mean
retuning them.

The classic home 6502 ran near 1 MHz. The Apple II was 1.023, and the PET, VIC-20 and
C64 were all close to 1.0. The era was not uniform, though. The Atari 800 and the NES
clocked theirs at 1.79, and the BBC Micro at 2. The MFC is a WDC 65C02, a part that
was sold at 1, 2, 4, 8 and 14 MHz, and it drives an 80 by 25 soft-font display with
sprites and a FAT16 disc. A 4 MHz clock suits that machine.

It is also close to the effective speed the games were written against, so the
clock is a decision rather than a side effect of a loop bound.

VENTURE is the most demanding thing on the disc, and two tests pin its cost. They are
`ATickStaysWithinItsBudget` and `AFrameOfDrawingStaysWithinItsBudget`.

| | cycles | per second |
|---|---|---|
| simulation tick | 24,000 | × 10 = 240,000 |
| frame of sprite drawing | 7,700 | × 60 = 464,000 |
| | | about 704,000, or 0.7 MHz |

Productive work therefore accounts for about 18% of the 4 MHz clock. The remainder is
absorbed by the game loop's busy-wait, which is what a game loop is for. The floor is
not 0.7 MHz, though. The loop tests for a due tick only once per pass, and a pass can
contain a whole frame, so a machine with little slack starts servicing ticks late and
the accumulator then discards the backlog. Measured at a true 1 MHz, VENTURE managed a
third of its intended pace. The realistic floor is around 2 MHz, and 4 MHz is
comfortable.

### Data flow

- A keyboard press travels from the host key event into the PIA input buffer, out
  through the kernel's `K_GET_KEYSTROKE` at `$FF09`, and into the running program.
- Display output goes from the program through the `PRINT_CHAR` or blit ABI to the VIC
  register port, into the chip's screen buffer, and finally to `DisplayWidget`, which
  renders it.
- Serial traffic runs from `TERM` or `IRC` through the ACIA to `Modem`, which carries
  it over TCP or telnet to the remote host, and back again the same way. Sending
  `+++ATH` hangs up.
- Disk access runs from the DOS FAT16 driver through `BlockDevice` sectors to
  `disk.img`. That covers files, drawers, which are one-level subdirectories that can
  grow across FAT clusters, and timestamps.
- Sound is produced when a program writes SID registers directly or calls the sound
  ABI. `SID` synthesises it and passes it to the host audio output.

### System integration

`Computer6502`, in `src/computer/Computer6502.cpp`, constructs the chips, connects the
`Memory` decoder to the peripherals, loads the ROMs through `MapFileParser`, and
triggers the power-on reset. The host then steps the CPU and the timer. The result
keeps a realistic 65C02 environment while layering modern conveniences on top of it,
including a FAT16 disk, a working terminal and modem, sound, and development tools.

---

## Part 2 — Memory & zero-page map


This is a software-based 6502 computer rather than a Commodore 64 emulator. It uses
ASCII rather than PETSCII, a 64K address space whose only banked region is the
`$B000-$EFFF` module window, and a small set of memory-mapped devices. The map below reflects the actual kernel source in
`kernel.asm`, the BASIC source in `basic.asm`, and the linker configurations in
`memory.cfg` and `basic_memory.cfg`.

### Overall Address Space

| Address Range | Size | Purpose |
|---------------|------|---------|
| `$0000-$00FF` | 256 B | Zero page, shared between EhBASIC and the monitor. The split is described below |
| `$0100-$01FF` | 256 B | Stack, growing down from `$01FF` |
| `$0200-$03FF` | 512 B | System variables. BASIC's page-2 variables live here alongside the monitor's variables and buffers |
| `$0400-$07FF` | 1 KB | Formerly the 40 by 25 screen. The screen now lives behind the VIC register port, described below. This range is not free. `$0400` holds the `T:` and `Z:` page snapshot, and `$0500-$07FF` holds the assembler's identifier buffers and symbol table. A program that uses neither may treat it as scratch |
| `$0800-$87FF` | 32 KB | Free RAM for user programs. BASIC keeps its program, variables and strings here when it runs, and the assembler reserves `$7800-$87FF` for source, with its symbol table down at `$0520-$07FF` while it builds |
| `$8800-$AFFF` | 10 KB | The DOS ROM, which is always mapped and holds the FAT16 filesystem and the DOS shell |
| `$B000-$EFFF` | 16 KB | The module window. Bank 0 is RAM and banks 1 to 255 are ROM modules, of which BASIC is bank 1 |
| `$F000-$FFFF` | 4 KB | The kernel BIOS. The monitor is module bank 4 and is not here |
| `$FE00-$FECA` | | PIA I/O, the `MODULE_BANK` register at `$FE23`, and the block-device registers at `$FE24-$FE28`. All of it sits within the kernel region |

There is no CIA, and the SID is not a Commodore part. The VIC is an 80 by 25 colour
text chip whose character and colour planes live inside the chip rather than in the
64K map, and they are reached through a VDC-style register port at `$FE2D-$FE37` that
the I/O section below describes. The keyboard and file I/O are exposed through a small
PIA-style register block at `$FE00`.

The `$B000-$EFFF` window is a bank-switched module slot. The `MODULE_BANK` register at
`$FE23` selects either RAM, which is bank 0, or one of up to 255 pre-loaded ROM
modules. `TODO.md` records the design argument for that mechanism in full, under
the bank-switched module slot. The DOS ROM at `$8800-$AFFF` is an
always-mapped read-only region holding the resident filesystem, and it can never be
banked out. `dos_internals.md` describes what lives in it.

### Zero Page

The monitor's zero-page variables were relocated to `$14-$39` to avoid the
EhBASIC interpreter, which uses page zero heavily. The split:

| Range | Owner | Notes |
|-------|-------|-------|
| `$00-$13` | EhBASIC | warm-start vector, USR vector, FAC temporaries, etc. |
| `$14-$39` | Monitor | see table below (free when BASIC is not the active workspace) |
| `$3A-$5A` | free | unused gap |
| `$5B-$FF` | EhBASIC | descriptor stack, program/var/array/string pointers, FACs, PRNG, decimal workspace |

#### Monitor zero-page variables (`$14-$39`)

| Address | Symbol | Purpose |
|---------|--------|---------|
| `$14-$15` | `MON_CURRADDR_LO/HI` | Current memory address |
| `$16-$17` | `MON_MSG_PTR_LO/HI` | Message string pointer |
| `$18-$19` | `JUMP_VECTOR` | Indirect jump / ZP pointer scratch |
| `$1A-$1B` | `VID_CELL_LO/HI` | Computed VIC cell index (`CURSOR_Y*80 + X`) |
| `$1C-$1D` | `VID_TMP_LO/HI` | Cell-index computation scratch |
| `$1E` | `PAGE_ENABLE` | System pager master switch (1 = on; a future setting toggles it) |
| `$1F` | `PAGE_SUSPEND` | ESC at `--MORE--` suspends paging for the rest of this output |
| `$20` | `PAGE_IN_BREAK` | Guard: inside the page-break prompt (don't re-count) |
| `$21` | `CMD_LINE_COUNT` | Lines printed since the last pause/command (paging) |
| `$22` | `PAGE_ABORT_FLAG` | Set when ESC pressed during paging (cooperative abort) |
| `$23` | `RNG_STATE_LO` | RNG 16-bit LFSR state, low byte (RTC-seeded at boot) |
| `$24` | `RNG_MAX` | RNG upper bound for `GET_RANDOM_NUMBER` |
| `$25-$28` | `MOVE_DEST_LO/HI`, `MOVE_DEND_LO/HI` | M: move/copy scratch (dest + dest-end) |
| `$29` | `SOUND_ENABLE` | 1 = system sound on (BEL + sound ABI); 0 = muted |
| `$2A` | `BEEP_TIMER` | jiffies until the BEL beep auto-gates off (0 = idle) |
| `$2B` | `RNG_STATE_HI` | RNG 16-bit LFSR state, high byte |
| `$2F-$30` | `RNG_TMP/RNG_TMP2` | RNG range-reduction scratch (multiplier / product) |
| `$31-$32` | `JIFFY_LO/JIFFY_HI` | 60 Hz monotonic tick counter, advanced by the timer IRQ (`K_GET_JIFFIES`) |
| `$35-$36` | `DEC_TEMP_LO/HI` | Decimal-conversion temp |
| `$37` | `DEC_DIGIT_IDX` | Decimal digit index |
| `$38-$39` | `DEC_RESULT_LO/HI` | Decimal-conversion result |

### Stack (`$0100-$01FF`)

System stack, growing downward from `$01FF`. The stack pointer is initialized
to `$FF` at reset.

### System Variables (`$0200-$03FF`)

When BASIC is running it owns the low part of this region, and the monitor's variables
live above it. The monitor's command buffer overlaps BASIC's area, which is safe
because the two never run at the same time.

| Range | Owner | Purpose |
|-------|-------|---------|
| `$0200-$020C` | EhBASIC | I/O vectors (`ccflag`, `VEC_IN`/`VEC_OUT`/`VEC_LD`/`VEC_SV`) |
| `$0221-$0268` | EhBASIC | input line buffer (`Ibuff`) |
| `$0200-$024F` | Monitor | `MON_CMDBUF`, an 80-byte command input buffer. It overlaps BASIC, and the two are mutually exclusive |
| `$0269-$028D` | Monitor | Monitor variables, relocated above BASIC's `$0268`. They are listed below |
| `$028E-$02DD` | Monitor | `MON_LAST_CMD_BUF`, an 80-byte last-command buffer that `.` recalls |
| `$02DE` | Monitor | `MON_LAST_CMD_LEN` |
| `$02DF` | Monitor | `MON_DUMP_SNAP`, the flag that makes a dump read the `T:`/`Z:` snapshot |
| `$02E0-$03FF` | free | available system RAM |

#### Monitor variables (`$0269-$028D`)

| Address | Symbol | Purpose |
|---------|--------|---------|
| `$026A` | `MON_CMDLEN` | Command length |
| `$026B` | `MON_MODE` | Monitor mode (0=Command, 1=Write) |
| `$026C-$026D` | `MON_STARTADDR_LO/HI` | Range start address |
| `$026E-$026F` | `MON_ENDADDR_LO/HI` | Range end address |
| `$0270` | `MON_PARSE_PTR` | Parser position |
| `$0272` | `MON_HEX_TEMP` | Hex conversion temp |
| `$0273` | `MON_BYTE_COUNT` | Byte counter |
| `$0275` | `MON_ERROR_FLAG` | Error flag |
| `$0276` | `CURSOR_X` | Cursor X (0-79) |
| `$0277` | `CURSOR_Y` | Cursor Y (0-24) |
| `$0279` | `MON_FILL_VALUE` | Fill (F:) byte value |
| `$027A-$027B` | `MON_DEST_ADDR_LO/HI` | Move/copy (M:) destination |
| `$027C` | `MON_COPY_MODE` | Move/copy mode (0=copy, 1=move) |
| `$027D-$028C` | `MON_SEARCH_PATTERN` | The search pattern for `X:`, up to 16 bytes |
| `$028D` | `MON_PATTERN_LEN` | Search pattern length |

`DEC_DIGIT_BUFFER` at `$027D` deliberately aliases `MON_SEARCH_PATTERN`. That is safe
because the `D:`, `H:` and `X:` commands never run at the same time.

### Video (VIC) register port (`$FE2D-$FE37`, `$FE62-$FECA`)

The 80 by 25 screen is not in the 64K address space. The VIC owns two parallel cell
planes, a character plane holding one byte per cell and a colour plane holding one
attribute byte per cell, and both are reached through an auto-incrementing register
port. That is the same idiom the block device uses. A program sets a cell index through
`VREG_ADDR_LO` and `VREG_ADDR_HI`, then reads or writes the `VREG_CHAR` and
`VREG_COLOR` data ports. Each access advances the index, which wraps at 2000.
`VREG_CMD` runs chip-side block operations so the CPU never has to copy the screen
itself.

| Address | Register | Purpose |
|---------|----------|---------|
| `$FE2D` | `VREG_ADDR_LO` | Cell index low (0–1999) |
| `$FE2E` | `VREG_ADDR_HI` | Cell index high |
| `$FE2F` | `VREG_CHAR` | Char data port, a full 8-bit CP437 code point; auto-increments |
| `$FE30` | `VREG_COLOR` | Color/attribute data port; auto-increments |
| `$FE31` | `VREG_ATTR` | Current attribute latch applied to `VREG_CHAR` writes |
| `$FE32` | `VREG_CMD` | `1`=clear, `2`=scroll up, `3`=scroll down, `4`=fill row, `5`=set row size (param: bit 7 double, bits 4–0 row), `6`=all rows normal, `7`=set scroll-region top row (param), `8`=render from font ROM, `9`=render from font RAM, `10`=reload CP437 into every font set, `11`=select font set (param), `12`=fine scroll offset in pixels (param) |
| `$FE33` | `VREG_STATUS` | `0` = ready |
| `$FE34` | `VREG_CURSOR_LO` | Hardware cursor cell low |
| `$FE35` | `VREG_CURSOR_HI` | Cursor cell high; bit 7 = cursor hidden |
| `$FE36` | `VREG_CMD_PARAM` | Command parameter / fill character |
| `$FE37` | `VREG_SCROLL_BOT` | Scroll-region bottom row (default 24) |
| `$FE62` | `VREG_FONT_LO` | Font byte index low |
| `$FE63` | `VREG_FONT_HI` | Font byte index high (spans all font sets) |
| `$FE64` | `VREG_FONT_DATA` | Font data port; auto-increments |
| `$FE65-$FECA` | sprites | 17 records of 6 bytes: X lo, X hi (bits 1–0 pos, bits 4–2 width−1, bit 5 magnify X), Y lo, Y hi (bits 1–0 pos, bits 4–2 height−1, bit 5 magnify Y, bit 7 = enable), glyph, attribute |

The soft-font port and the sprite block sit above the RTC rather than beside the
rest because the port block ends at `$FE37` with the SID immediately after.
`$FECB-$FEFF` is the remaining free space in the I/O page.

A sprite occupies one cell of 8 by 16 nominal pixels unless its size bits say
otherwise, and it may reach 8 by 8 cells. A multi-cell sprite draws consecutive glyph
codes, row-major from the base code. A 2 by 2 sprite at code g therefore uses g and
g+1 across the top and g+2 and g+3 across the bottom, wrapping at 255. That means a
larger sprite gains real detail instead of merely magnifying one pattern, and it
remains a single sprite with one position to update. The size is stored as size minus
one, in bits that a position never uses, so code written before sizes existed leaves
them zero and still gets a single cell. A clear returns every sprite to one cell and
switches it off.

Sprite positions are nominal pixels on an 8 by 16 grid and take no account of row
doubling. On a screen with double-size rows, the cell plane and the sprite plane do not
agree about where a given row is.

The scroll region runs from a top row to a bottom row inclusive. Scroll commands shift
only those rows and leave everything outside them untouched, so a program can pin a
header above the region and a status line below it. A clear, which is `VREG_CMD` `1`,
resets the region to the whole screen, so anything that depends on a region has to
reprogram it after every clear.

The bottom row has a register of its own, while the top row rides the command engine as
command `7`. That asymmetry exists only because the port block ends at `$FE37` with the
SID immediately after it. Commands `5`, `7`, `11` and `12` all consume
`VREG_CMD_PARAM`, which doubles as the fill character, so a program must set the fill
character again before its next clear, scroll or fill-row. IRC pins its input and
status rows below the region, EDIT pins its status line, and TERM maps the pair onto
ANSI's DECSTBM sequence.

Glyph shapes are RAM rather than a fixed ROM. Font storage lives inside the chip, and
like the cell planes it is not in the 64K map. It holds 16 complete sets of 256 glyphs,
reached through the index and data port described above. Command `11` picks which set
the renderer reads, so a program can upload its variants once and then switch between
them with a single write. That is the equivalent of repointing the C64's `$D018`, and
it is what makes pixel-smooth character scrolling affordable.

The constructor seeds every set from CP437, and a clear selects the ROM font. Redefining a
handful of glyphs therefore leaves the other 248 readable, and no program can strand
the shell with a font nobody can read.

Command `12` slides the whole scroll region down by a pixel count, so the world can
move in steps finer than a character cell. The C64 called the same idea `YSCROLL`.

The region's top row becomes a hidden staging row as a consequence. Sliding down opens
a gap at the top, and what belongs in that gap is the row that does not exist yet, so
the renderer clips the region one row in. When the offset reaches a full cell height,
the program issues a real scroll, resets the offset and writes a fresh hidden top row.
The technique costs one row of the display. A clear turns it off.

There are 17 sprites, positioned in pixels on the nominal 8 by 16 grid, which gives a
range of 0 to 639 horizontally and 0 to 399 vertically. They are drawn over the cell
planes with the glyph's background bits left transparent.

The important property is that neither the scroll region nor the fine offset moves
them, and that is the whole reason they exist. Anything drawn into the cell plane rides
the fine offset, so a screen-fixed object such as a player's craft sawtooths by a cell
on every scroll. A sprite does not. Their shapes come from the same font storage the
cells use, and a clear disables all of them.

The attribute byte is laid out as `[R][BR][bg:3][fg:3]`. Bit 7 selects reverse video,
bit 6 selects bright, bits 5 to 3 hold the background colour from 0 to 7, and bits 2 to
0 hold the foreground colour. The default at power-on and after a clear is `$02`, which
is green on black. The kernel tracks the logical cursor in `CURSOR_X` and `CURSOR_Y` at
`$0276` and `$0277` and writes the screen through this port. `K_SET_ATTR` at `$FF2D`
sets the colour latch.

### Sound (SID) register port (`$FE38-$FE54`)

This is a software sound chip modelled on the MOS 6581 and 8580 SID. It keeps the real
29-register layout of three voices and a filter, relocated from `$D400` to `$FE38`, so
existing SID knowledge and music transfer across directly.

Each voice has seven registers, and the three voices start at `$FE38`, `$FE3F` and
`$FE46`. They are `FREQ_LO` and `FREQ_HI`, `PW_LO` and `PW_HI` for the 12-bit pulse
width, `CONTROL`, `ATK_DEC` and `SUS_REL`. In `CONTROL`, bit 0 is the gate, bit 1 is
sync, bit 2 is ring modulation, bit 3 is test, and bits 4 to 7 select triangle,
sawtooth, pulse and noise respectively.

The global registers are `FC_LO` and `FC_HI` at `$FE4D` and `$FE4E`, which hold the
11-bit cutoff, `RES_FILT` at `$FE4F`, which holds resonance and per-voice routing, and
`MODE_VOL` at `$FE50`, which holds the filter mode and the master volume. `OSC3` and
`ENV3` at `$FE53` and `$FE54` are read-only read-back registers for voice 3.

The host synthesises 44.1 kHz PCM from the register state and plays it through Qt,
using `SidAudio` and `QAudioSink`. Ring and sync modulation are not modelled. The
kernel uses voice 1 for the system beep and for the `K_SOUND_TONE` and `K_SOUND_OFF`
ABI calls. An ASCII BEL at `$07` rings a short beep that does not block, because the
timer IRQ gates it off. All kernel sound honours the `SOUND_ENABLE` zero-page flag at
`$29`, which defaults to on.

### Real-time clock (RTC) register port (`$FE55-$FE60`)

This is a read-only real-time clock that mirrors the host's local wall-clock time. It
is always correct and cannot be set, so the machine needs no battery-backed
persistence. The DOS `DATE` command reads it.

| Address | Register | Notes |
|---------|----------|-------|
| `$FE55` | `RTC_LATCH` | Writing any value snapshots the host time into the fields. Reading returns 0 |
| `$FE56` | `RTC_SEC` | seconds, BCD 00–59 |
| `$FE57` | `RTC_MIN` | minutes, BCD 00–59 |
| `$FE58` | `RTC_HOUR` | hours, BCD 00–23 (24-hour) |
| `$FE59` | `RTC_DAY` | day of month, BCD 01–31 |
| `$FE5A` | `RTC_MONTH` | month, BCD 01–12 |
| `$FE5B` | `RTC_YEAR` | year mod 100, BCD 00–99 (add 2000) |
| `$FE5C` | `RTC_DOW` | day of week, 0=Sunday … 6=Saturday |
| `$FE5D-$FE5E` | `RTC_FATTIME_LO/HI` | current time pre-packed as a FAT16 time word |
| `$FE5F-$FE60` | `RTC_FATDATE_LO/HI` | current date pre-packed as a FAT16 date word |

Writing `RTC_LATCH` before reading the fields keeps a multi-register read from
straddling a second boundary (the reason real RTCs have a latch). The host time
source is injectable so tests can pin a known timestamp.

The `RTC_FATTIME`/`RTC_FATDATE` registers are an MFC convenience (not on a real
chip): the host pre-packs the current time into the FAT16 directory format
(time = `[hour:5][min:6][sec/2:5]`, date = `[year-1980:7][month:4][day:5]`), so
DOS stamps a new file's directory entry by copying four bytes instead of doing
the bit-packing itself. DOS `CATALOG` unpacks these fields to show each file's
`YYYY-MM-DD HH:MM` modification time.

### I/O — PIA (`$FE00-$FE23`)

The I/O page sits at `$FE00-$FEFF`, inside the kernel ROM region, which the kernel
simply avoids placing code in. Putting it here keeps `$B000-$EFFF` a clean,
I/O-free, bank-switched module slot.

A single PIA-style device provides keyboard input and host file I/O. It offers two file
models. The block model, which the kernel's `L:` and `S:` commands use, moves a whole
memory range in or out at once. The byte-stream model, which BASIC's `LOAD` and `SAVE`
use, moves one byte at a time through the data register.

A block device at `$FE24-$FE28` is a separate thing again. It presents a host
`disk.img` as 512-byte sectors and is the storage layer beneath the MFC-DOS FAT16
filesystem, which `dos_internals.md` describes. It is independent of both PIA file
models.

| Address | Register | Purpose |
|---------|----------|---------|
| `$FE00` | `PIA_DATA` | Keyboard data (read consumes a key) |
| `$FE02` | `PIA_CONTROL` | Status flags (bit 0 = data available) |
| `$FE0E` | `TIMER_IRQ_ACK` | Write to acknowledge the ~60 Hz periodic timer IRQ |
| `$FE0F` | `KEY_STATE` | A read-only bitmask of the keys held right now. Described below |
| `$FE10` | `FILE_COMMAND` | The file operation. Load and save are block operations, and open-read, open-write and close are stream operations |
| `$FE11` | `FILE_STATUS` | Idle / in-progress / success / stream-open / EOF / error |
| `$FE12-$FE13` | `FILE_ADDR_LO/HI` | Block load/save target/start address |
| `$FE14-$FE1F` | `FILE_NAME_BUF` | Filename buffer (12 bytes) |
| `$FE20-$FE21` | `FILE_END_ADDR_LO/HI` | Block save end address |
| `$FE22` | `FILE_DATA` | Byte-stream data register (read next / write byte) |
| `$FE23` | `MODULE_BANK` | Selects the module bank. Bank 0 is RAM, and banks 1 to 255 are ROM modules mapped at `$B000-$EFFF` |
| `$FE24-$FE25` | `BLK_LBA` | The block device's 16-bit sector number, little-endian |
| `$FE26` | `BLK_CMD` | The block device command. 1 reads a sector and 2 writes one |
| `$FE27` | `BLK_STATUS` | The block device status. 0 means ready and `$FF` means error |
| `$FE28` | `BLK_DATA` | The block device's 512-byte sector data port, which auto-increments |

#### `KEY_STATE` (`$FE0F`) — the control port

`PIA_DATA` is a queue of what was typed. `KEY_STATE` is a snapshot of what is held down
at this moment. It reads as an active-high bitmask.

| Bit | Key | | Bit | Key |
|-----|-----|-|-----|-----|
| 0 | Up | | 3 | Right |
| 1 | Down | | 4 | Fire (Space) |
| 2 | Left | | 5 | Button 2 (Left Shift) |

Bits 6 and 7 are reserved and read as 0. The read is non-destructive, so a program can
poll it every frame for as long as a key is down.

An action game cannot work from the keystroke queue alone. That queue carries no
key-up event, so the only evidence that a key is still held is host auto-repeat. Auto-
repeat stalls for roughly 500 ms before it starts, and on most platforms it repeats
only the most recently pressed key, which means pressing fire silently cancels a held
direction. The bits in this register are independent of each other, so steering and
firing at the same time is expressible at all, and movement is as smooth as the polling
rate rather than the repeat rate.

The register reads 0 when nothing sets it, which is the case for the console build and
the headless test harness. A program that uses it therefore degrades to receiving no
input rather than misbehaving. `programs/kpanic/glue.s` shows the accessor.

### ROM Layout

#### Kernel BIOS (`$F000-$FFFF`, 4 KB)

| Segment | Range | Purpose |
|---------|-------|---------|
| `CODE` | `$F000-$F610` (1553 B) | BIOS code and data |
| `IORESV` | `$FE00-$FEFF` (256 B) | Reserved I/O page (PIA + `MODULE_BANK` + VIC + SID) |
| `JUMPS` | `$FF00-$FF41` (66 B) | Kernel API jump table (22 entries) |
| `VECS` | `$FFFA-$FFFF` (6 B) | Interrupt/reset vectors |
| (free) | `$F611-$FDFF` | ~2.0 KB unused |

#### Kernel API jump table (`$FF00`)

| Address | Symbol | Routine |
|---------|--------|---------|
| `$FF00` | `K_PRINT_CHAR` | `PRINT_CHAR` |
| `$FF03` | `K_PRINT_MESSAGE` | `PRINT_MESSAGE` |
| `$FF06` | `K_PRINT_NEWLINE` | `PRINT_NEWLINE` |
| `$FF09` | `K_GET_KEYSTROKE` | `GET_KEYSTROKE` |
| `$FF0C` | `K_CLEAR_SCREEN` | `CLEAR_SCREEN` |
| `$FF0F` | `K_GET_RAND_NUM` | `GET_RANDOM_NUMBER` |
| `$FF12` | `K_RETURN_MODULE` | `RETURN_FROM_MODULE` — unmaps the bank, returns to the DOS prompt (BASIC `BYE`) |
| `$FF15` | `K_READ_LINE` | `READ_COMMAND_LINE` — edited line input (backspace/ESC) → `MON_CMDBUF`/`MON_CMDLEN` |
| `$FF18` | `K_PARSE_HEX` | `HEX_QUAD_TO_ADDR` — X = offset in `MON_CMDBUF` → `MON_CURRADDR`, carry set if invalid |
| `$FF1B` | `K_PRINT_HEX_BYTE` | `PRINT_HEX_BYTE` — print A as two hex digits |
| `$FF1E` | `K_MON_ENTRY` | `MON_LAUNCH` — DOS launches the monitor here (`MON`) |
| `$FF21` | `K_LAUNCH_BY_NAME` | `LAUNCH_BY_NAME` — DOS launches a module by name |
| `$FF24` | `K_LIST_MODULES` | `LIST_MODULES` — print the module catalog (`BANKS`) |
| `$FF27` | `K_PRINT_DEC` | `PRINT_DEC` — print a 32-bit value in decimal |
| `$FF2A` | `K_PARSE_DEC` | `PARSE_DEC_ABI` — parse a decimal string from `MON_CMDBUF` |
| `$FF2D` | `K_SET_ATTR` | `SET_ATTR` — set the color/attribute latch (`VREG_ATTR`) |
| `$FF30` | `K_PRINT_HELP_LINE` | `PRINT_HELP_LINE` — print `"syntax"`<TAB>`"desc"` (TAB pads to a fixed column) for two-column help listings |
| `$FF33` | `K_SOUND_TONE` | `SOUND_TONE` — play a tone on SID voice 1 (A = freq low, X = freq high); honors `SOUND_ENABLE` |
| `$FF36` | `K_SOUND_OFF` | `SOUND_OFF` — stop voice 1 (gate off) |
| `$FF39` | `K_GET_JIFFIES` | `GET_JIFFIES` — read the 60 Hz monotonic tick counter (returns A = low, X = high) |
| `$FF3C` | `K_HEX_PAIR` | `HEX_PAIR_TO_BYTE` — parse two hex digits into a byte |
| `$FF3F` | `K_PARSE_DEC_VAL` | `PARSE_DECIMAL_VALUE` — parse a decimal value |

The jump table is also the module ABI. A ROM module reaches kernel services only
through these entries, so it is independent of where the kernel's internal
routines live. The `$FF15` and `$FF18` services share the monitor's command buffer
`MON_CMDBUF` and `MON_CURRADDR` as scratch, which is safe because the monitor is
suspended while a module runs and that state is saved and restored across the
launch.

#### Module window (`$B000-$EFFF`, 16 KB)

A bank-switched slot selected by `MODULE_BANK` at `$FE23`. Bank 0 is RAM, the
boot and default state, zeroed by `RESET`. Banks 1 to 255 are read-only ROM
modules pre-loaded by the host. The kernel owns a `MODULE_DIR` catalog holding a
bank number, an entry address and a name for each one. The DOS `BANKS` command
prints the catalog through `K_LIST_MODULES`, and launching a module by name goes
through `K_LAUNCH_BY_NAME`, which writes `MODULE_BANK` and jumps to the module
entry. A module exits with `JMP $FF12`, which unmaps the bank and returns to the
DOS prompt.

BASIC is module bank 1, EhBASIC 2.22p5 with project additions, and its cold start
`LAB_COLD` is at `$B000`. BASIC I/O is routed through the kernel by the page-2
vectors, with `VEC_IN` and `VEC_OUT` pointing at the keyboard and screen and
`VEC_LD` and `VEC_SV` at the file-stream LOAD and SAVE routines.

### Interrupt Vectors (`$FFFA-$FFFF`)

| Address | Vector | Handler |
|---------|--------|---------|
| `$FFFA-$FFFB` | NMI | `NMI_HANDLER` — STOP key: sets BASIC's `ON NMI` flag if armed, else breaks to the monitor (clearing the pager guard and unmapping any module bank) |
| `$FFFC-$FFFD` | RESET | `RESET` (power-on entry) |
| `$FFFE-$FFFF` | IRQ | `IRQ_HANDLER` — acknowledges the ~60 Hz timer, advances the `K_GET_JIFFIES` counter, sets BASIC's `ON IRQ` flag if armed |

### Free RAM for User Programs

- `$3A-$5A` is a small free zero-page gap, useful for fast addressing when BASIC
  is not in use.
- `$02E0-$03FF` is leftover system-variable space.
- `$0800-$87FF` is the main user RAM, 32 KB. Below it, `$0400` holds the `T:` and
  `Z:` page snapshot and `$0500-$07FF` holds the assembler's identifier buffers
  and symbol table, and above it `$8800-$AFFF` is the DOS ROM. When BASIC is
  active this region is its program, variable and string space, with
  `Ram_base=$0800` and `Ram_top=$8800`. The assembler reserves the top of it while
  building, using `$7800-$87FF` for the source text.

### Key Constants (from `kernel.asm`)

| Symbol | Value | Purpose |
|--------|-------|---------|
| `STACK_TOP` | `$FF` | Initial stack pointer |
| `SCREEN_WIDTH` | `80` | Characters per line |
| `SCREEN_HEIGHT` | `25` | Lines on screen |
| `LINES_PER_PAGE` | `24` | Paging threshold |

The screen is not memory-mapped. The kernel writes it through the VIC register
port at `$FE2D-$FE37` and tracks the logical cursor in `CURSOR_X` and
`CURSOR_Y`.

---

## Part 3 — Kernel API ($FF00 jump table)


### Overview

The MFC kernel exposes a stable jump table at `$FF00`. User programs and bank
modules call these routines with `JSR` to the fixed addresses below. The entries
never move, so a program built today keeps working as the kernel evolves.

### What is in the BIOS, and what is not

The kernel ROM holds the machine, and everything that is merely software shipped
with the machine lives in a bank module or on disk.

| In the BIOS (`$F000-$FFFF`) | Elsewhere |
|---|---|
| Screen output, cursor, scrolling, the pager | The monitor, module bank 4 |
| Keyboard input and line editing (`K_READ_LINE`, `.` recall) | BASIC, bank 1 |
| Hex and decimal conversion (`K_PARSE_HEX`, `K_PRINT_DEC`, …) | FORTH, bank 3 |
| IRQ/NMI handlers, the 60 Hz tick, NMI break-in | EDIT, TERM, IRC and the games, disk `.PRG` files |
| Sound (`K_SOUND_TONE`) and the RNG | The filesystem and shell, MFC-DOS at `$8800` |
| Bank launching (`K_LAUNCH_BY_NAME`, `RETURN_FROM_MODULE`) | |
| The `$FF00` table and the `$FFFA` vectors | |

The monitor is a bank module rather than a disk program because a disk program
would load at `$0800`, which is precisely the memory a monitor exists to inspect,
so it would overwrite the program under test. A bank costs no user RAM, maps
instantly, and works with no disk present. The trade is that the monitor
cannot show its own window. `R:B000-EFFF` displays the monitor's ROM rather than
bank-0 RAM, and sibling banks are invisible for the same reason.

The boundary is enforced rather than aspirational. Because the BIOS and the
monitor are separate link units, neither can name the other's labels, and the
assembler rejects the attempt. What the assembler cannot see is that `monitor.asm`
reaches the BIOS through hand-written equates to `$FF00` addresses. Insert an
entry in the middle of the table and every equate below it still assembles while
pointing one slot off. The `kernel_bios_monitor_split` test in
`tests/scripts/check_kernel_split.py` checks every equate against the table below,
and the bank's entry addresses against the constants the kernel jumps to.

Two consequences are worth knowing when adding to the kernel.

- Append to the jump table and never insert. Existing entries are an ABI that
  disk programs and every module bind to by address.
- The BIOS may not call into a module. The window may not be mapped, and if it is,
  it may hold a different bank. Anything the BIOS needs must live in the BIOS,
  which is why `.` recall and the boot-time window clear were moved down out of
  the monitor rather than published.

### Calling convention

- Parameters and results are passed in the A, X and Y registers unless noted.
- The carry flag often signals success or failure, and each entry below says which
  way round it runs.
- A few zero-page locations are part of the ABI.
  - `$14` and `$15` hold `MON_CURRADDR`, the result of `K_PARSE_HEX` and
    `K_PARSE_DEC`.
  - `$16` and `$17` hold `MON_MSG_PTR`, the string pointer for
    `K_PRINT_MESSAGE`.
  - `$24` holds `RNG_MAX`, the upper bound for `K_GET_RAND_NUM`.
  - `$0200` is `MON_CMDBUF`, the line buffer filled by `K_READ_LINE`.

### Jump table (summary)

| Addr | Name | Purpose |
|------|------|---------|
| `$FF00` | `K_PRINT_CHAR` | Print A as a character (handles CR/BS) |
| `$FF03` | `K_PRINT_MESSAGE` | Print null-terminated string at (`$16/$17`) |
| `$FF06` | `K_PRINT_NEWLINE` | Print CR/LF |
| `$FF09` | `K_GET_KEYSTROKE` | Non-blocking key read (carry set + A = key when ready) |
| `$FF0C` | `K_CLEAR_SCREEN` | Clear screen, home cursor |
| `$FF0F` | `K_GET_RAND_NUM` | A = random `1..RNG_MAX` |
| `$FF12` | `K_RETURN_MODULE` | Bank module exit → return to the DOS prompt |
| `$FF15` | `K_READ_LINE` | Edited line input → `MON_CMDBUF`; A = length |
| `$FF18` | `K_PARSE_HEX` | Parse hex at `MON_CMDBUF`+X → `MON_CURRADDR` |
| `$FF1B` | `K_PRINT_HEX_BYTE` | Print A as two hex digits |
| `$FF1E` | `K_MON_ENTRY` | Cold-enter the monitor (DOS uses this) |
| `$FF21` | `K_LAUNCH_BY_NAME` | Launch a disk program/module by name (DOS) |
| `$FF24` | `K_LIST_MODULES` | Print the module/bank catalog (`BANKS`) |
| `$FF27` | `K_PRINT_DEC` | Print a 32-bit value in decimal |
| `$FF2A` | `K_PARSE_DEC` | Parse decimal at `MON_CMDBUF`+X → `MON_CURRADDR` |
| `$FF2D` | `K_SET_ATTR` | Set the color/attribute latch from A |
| `$FF30` | `K_PRINT_HELP_LINE` | Print a TAB-aligned "syntax / description" help line |
| `$FF33` | `K_SOUND_TONE` | Play a tone on SID voice 1 (A = freq lo, X = freq hi) |
| `$FF36` | `K_SOUND_OFF` | Stop SID voice 1 (gate off) |
| `$FF39` | `K_GET_JIFFIES` | Read the 60 Hz monotonic tick counter (A = lo, X = hi) |
| `$FF3C` | `K_HEX_PAIR` | Parse two hex digits into a byte |
| `$FF3F` | `K_PARSE_DEC_VAL` | Parse a decimal value |

### Details

#### Output

`K_PRINT_CHAR` at `$FF00` prints A as a character. `$0D`, a carriage return,
moves to the start of the next line, and `$08`, a backspace, backspaces and
clears. It preserves X and Y.

`K_PRINT_MESSAGE` at `$FF03` prints a null-terminated string of fewer than 256
bytes. Put the address in `MON_MSG_PTR`, low byte at `$16` and high byte at `$17`,
first.

```assembly
    LDA #<MSG
    STA $16
    LDA #>MSG
    STA $17
    JSR $FF03
MSG: .BYTE "HELLO WORLD", 0
```

`K_PRINT_NEWLINE` at `$FF06` prints a carriage return and line feed.

`K_PRINT_HEX_BYTE` at `$FF1B` prints A as two hex digits. It preserves X and Y.

`K_PRINT_DEC` at `$FF27` prints a 32-bit little-endian value in decimal. A and X
are the low and high bytes of a pointer to a 4-byte value in memory, and Y is the
field width, where 0 means no padding and a larger value right-aligns with leading
spaces. It copies the value into its own workspace, so the caller's bytes are
untouched.

```assembly
    LDA #<NUM      ; NUM holds the value, 4 bytes little-endian
    LDX #>NUM
    LDY #4         ; right-align in a 4-column field
    JSR $FF27
```

`K_SET_ATTR` at `$FF2D` latches A as the colour attribute for characters printed
afterwards. In the attribute byte, bit 7 is reverse, bit 6 is bright, bits 5 to 3
are the background colour and bits 2 to 0 the foreground. The eight colours are 0
black, 1 red, 2 green, 3 yellow, 4 blue, 5 magenta, 6 cyan and 7 white. The
default is `$02`, green on black.

`K_PRINT_HELP_LINE` at `$FF30` prints a command's syntax and description as one
line, with the description padded to column 22. The help screens use it. The
string pointer goes in `MON_MSG_PTR` at `$16` and `$17`, as for
`K_PRINT_MESSAGE`, and the string itself is
`"<syntax>",$09,"<description>",0`.

`K_GET_KEYSTROKE` at `$FF09` does not block. It returns carry set with the key in
A when one is waiting, and carry clear when the buffer is empty. To wait for a
key, spin with `@w JSR $FF09 : BCC @w`.

`K_READ_LINE` at `$FF15` reads one edited line into `MON_CMDBUF` at `$0200`,
handling backspace and ESC itself. The length is left in `MON_CMDLEN` and returned
in A, with the zero flag set for an empty line.

`K_PARSE_HEX` at `$FF18` parses a hex address from `MON_CMDBUF` starting at offset
X, leaves the result in `MON_CURRADDR` at `$14` and `$15`, and sets carry if the
text is invalid.

`K_PARSE_DEC` at `$FF2A` parses a decimal number the same way, with the same
result location and the same carry convention. Pair it with `K_READ_LINE` to read
a number the user typed.

#### Screen / system

`K_CLEAR_SCREEN` at `$FF0C` clears the screen and homes the cursor.

`K_GET_RAND_NUM` at `$FF0F` returns a random integer from 1 to `RNG_MAX` in A.
Store the inclusive upper bound in `RNG_MAX` at `$24` first. The generator is an
RTC-seeded 16-bit LFSR, so sequences differ from run to run.

`K_MON_ENTRY` at `$FF1E` cold-enters the monitor. DOS jumps here to start the
`MON` command.

`K_RETURN_MODULE` at `$FF12` is called from within a bank module such as BASIC or
FORTH. It unmaps the module bank, clears BASIC's interrupt-enable flags, resets
the stack and returns to the DOS prompt.

`K_LAUNCH_BY_NAME` at `$FF21` and `K_LIST_MODULES` at `$FF24` are DOS-internal.
They launch a disk program or bank module by name, and print the module catalog
for the `BANKS` command.

#### Sound

`K_SOUND_TONE` at `$FF33` plays a sustained tone on SID voice 1, with A the
frequency low byte and X the frequency high byte, where
`Fout = FREQ * clock / 2^24`. It honours the `SOUND_ENABLE` mute at `$29`.

`K_SOUND_OFF` at `$FF36` stops voice 1 by gating it off.

`K_GET_JIFFIES` at `$FF39` reads the monotonic 60 Hz tick counter, returning the
low byte in A and the high byte in X. It starts at 0 on RESET, is advanced by the
timer IRQ, and wraps every 65536 ticks, about 18.2 minutes. Compare deltas with
unsigned subtraction and the wrap is harmless. The read is `SEI`-guarded
internally so the two bytes cannot tear, and the caller's interrupt-enable state
is preserved. Use it for frame pacing in real-time programs, as a fixed-tick
accumulator loop, rather than counting instructions, which drifts with host
speed.

---

See `examples/` for runnable programs that use these calls, and
`Part 2 (Memory and zero-page map)` for the full memory/zero-page map.
