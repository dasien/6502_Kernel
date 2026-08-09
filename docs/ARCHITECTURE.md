# MFC Architecture & Reference

The consolidated internals reference for MFC: the system overview, the full
memory/zero-page map, the kernel API ($FF00 jump table), and the bank-switched
module design. (User-facing manuals live in the `UPPERCASE.md` docs.)

**Contents:** Part 1 System overview · Part 2 Memory & zero-page map ·
Part 3 Kernel API · Part 4 Bank-switched modules

### Table of Contents

- [Part 1 — System overview](#part-1--system-overview)
  - [Overview](#overview)
  - [Block diagram](#block-diagram)
  - [Components](#components)
  - [Memory map (summary)](#memory-map-summary)
  - [Data flow](#data-flow)
  - [System integration](#system-integration)
- [Part 2 — Memory & zero-page map](#part-2--memory--zero-page-map)
  - [Overall Address Space](#overall-address-space)
  - [Zero Page](#zero-page)
  - [Stack (`$0100-$01FF`)](#stack-0100-01ff)
  - [System Variables (`$0200-$03FF`)](#system-variables-0200-03ff)
  - [Video (VIC) register port (`$FE2D-$FE37`)](#video-vic-register-port-fe2d-fe37)
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
- [Part 4 — Bank-switched modules](#part-4--bank-switched-modules)
  - [Goal](#goal)
  - [Memory map (target)](#memory-map-target)
  - [Prerequisite: relocate I/O out of the module window (`$DC00` → `$FE00`)](#prerequisite-relocate-io-out-of-the-module-window-dc00--fe00)
  - [Bank-select register — `MODULE_BANK = $FE23`](#bank-select-register--module_bank--fe23)
  - [Emulator changes (`Memory`)](#emulator-changes-memory)
  - [Module contract](#module-contract)
  - [Module directory (in the kernel ROM)](#module-directory-in-the-kernel-rom)
  - [`B:` — Bank menu (replaces per-module commands)](#b--bank-menu-replaces-per-module-commands)
  - [Host-side bank registry](#host-side-bank-registry)
  - [Settled decisions](#settled-decisions)
  - [Migration](#migration)
  - [Implementation phases](#implementation-phases)

---

## Part 1 — System overview


### Overview

MFC -- **My First Computer** -- is a software-defined computer built around a
cycle-stepped **WDC 65C02** CPU.
The C++/Qt host emulates the CPU and a set of memory-mapped peripherals; a 6502
kernel ROM (BIOS + MFC/OS DOS) and bank-switched ROM modules (BASIC, the monitor, an
the monitor with its built-in assembler, FORTH) run on top. It is **not** a Commodore 64 and
does not use PETSCII — the display is 80×25 CP437 text with 16 colors.

This document describes the components and how they interconnect. For the exact
memory/zero-page/I-O addresses, `Part 2 (Memory and zero-page map)` is authoritative.

### Block diagram

> For the same machine drawn as a physical board — chips on a bus, the I/O decode
> chip by chip, interrupt lines, and the host-side backing for each peripheral —
> see **[BOARD.md](BOARD.md)**.

```
        ┌────────────┐   ┌──────────────┐   ┌────────────────────────────┐
        │ Reset      │──▶│ Timing       │──▶│ CPU6502 (WDC 65C02)        │
        │ Circuit    │   │ Circuit ~1MHz│   │ A/X/Y/SP/P, full CMOS ISA  │
        └────────────┘   └──────────────┘   └─────────────┬──────────────┘
                                                          │ 16-bit bus
        ┌─────────────────────────────────────────────────┴────────────────┐
        │                          Memory (64K)                            │
        │  RAM  •  ROM overlay ($F000-$FFFF)  •  bank window ($B000-$EFFF) │
        │  •  I/O page routed to peripherals ($FE00-$FE60)                 │
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

### Components

All emulated devices live in `src/computer/` (headers in `include/computer/`);
the `Computer6502` class wires them together.

- **CPU6502** — cycle-stepped WDC 65C02: full CMOS instruction set including
  `RMB`/`SMB`/`BBR`/`BBS`, `WAI`, `STP`, and correct decimal-mode flags. Validated
  against the Klaus2m5/amb5l functional, decimal, and 65C02-extended suites.
- **Memory** — 64K store plus the address decoder: it overlays the kernel ROM at
  `$F000-$FFFF`, routes the I/O page (`$FE00-$FE60`) to the peripherals, and drives
  the bank-switched module window at `$B000-$EFFF` (BASIC / FORTH / MONITOR,
  selected via `MODULE_BANK` at `$FE23`).
- **VIC** — text video. The 80×25 screen and its per-cell color/attribute plane
  live **inside the chip**, reached through a small register port (`$FE2D-$FE37`):
  set a cell index, then stream glyphs/attributes; chip-side clear/scroll/fill-row
  commands avoid per-cell CPU writes. Renders full 8-bit CP437.
- **PIA** — keyboard input (circular buffer), the host file I/O ports the DOS/
  monitor use for `LOAD`/`SAVE`, and the ~60 Hz interval-timer IRQ ("jiffy").
- **Acia** — emulated 6551 serial port; the terminal (`TERM`) and IRC clients talk
  through it. Paired on the GUI build with **Modem**, a Hayes-AT/telnet bridge over
  `QTcpSocket` so the machine can "dial" real BBSes/IRC servers.
- **Sid** — software MOS 6581/8580 SID (three voices, ADSR, filter) at
  `$FE38-$FE54`; `SidAudio` streams its PCM to the host audio out when Qt
  Multimedia is present.
- **Rtc** — real-time clock (`$FE55-$FE60`); backs the DOS `DATE`/`TIME`, FAT16
  file timestamps, and the kernel RNG seed.
- **BlockDevice** — a FAT16 disk image (`disk.img`); the resident DOS filesystem
  reads/writes it. Images are built by the host `mkdisk` tool from a diskmap bundle.
- **ResetCircuit / TimingCircuit** — power-on/warm reset (loads the `$FFFC` vector,
  sets the startup CPU state) and ~1 MHz cycle pacing.
- **MapFileParser** — loads the assembled kernel/module ROM segments into memory
  from their `.map`/binary outputs at startup.

On the GUI build, `src/ui/` adds **MainWindow** (menus, Control/View, zoom) and
**DisplayWidget** (blits the VIC screen with the embedded CP437 font).

### Memory map (summary)

```
$0000-$00FF  Zero page (kernel/monitor/DOS workspace)
$0100-$01FF  Stack
$0200-$03FF  System variables (command buffer, DOS/monitor state)
$0800-$87FF  User RAM — disk programs load and run at $0800 (2 KB C stack near the top)
$B000-$EFFF  Bank-switched module window (BASIC 1, FORTH 3, MONITOR 4; 2 free)
$F000-$FFFF  Kernel BIOS; jump table at $FF00, vectors at $FFFA
$FE00-$FE60  Memory-mapped I/O — PIA, VIC port, ACIA, SID, RTC (carved out of the ROM window)
```

See `Part 2 (Memory and zero-page map)` for the full zero-page allocation, the I/O
register layout, and the `$FF00` kernel ABI jump table.

### Data flow

- **Keyboard:** host key → PIA input buffer → kernel `K_GET_KEYSTROKE` (`$FF09`) →
  the running program.
- **Display:** program → `PRINT_CHAR`/blit ABI → VIC register port → chip screen
  buffer → `DisplayWidget` renders it.
- **Serial / dialing:** `TERM`/`IRC` → ACIA → Modem → TCP/telnet → remote host, and
  back. `+++ATH` hangs up.
- **Disk:** DOS FAT16 driver → BlockDevice sectors → `disk.img`. Files, drawers
  (one-level subdirectories, growable across FAT clusters), timestamps.
- **Sound:** program writes SID registers (or calls the sound ABI) → Sid synthesis
  → host audio.

### System integration

`Computer6502` (`src/computer/Computer6502.cpp`) constructs the chips, connects the
Memory decoder to the peripherals, loads the ROMs via `MapFileParser`, and triggers
the power-on reset; the host then steps the CPU/timer. This keeps a realistic
65C02 environment while layering on modern conveniences — a FAT16 disk, a real
terminal/modem, sound, and development tools.

---

## Part 2 — Memory & zero-page map


This is a **software-based 6502 computer**, not a Commodore 64 emulator. It
uses ASCII (not PETSCII), a flat 64K address space (no memory banking), and a
small set of memory-mapped devices. This document reflects the actual kernel
(`kernel.asm`) and BASIC (`basic.asm`) source and the linker configs
(`memory.cfg`, `basic_memory.cfg`).

### Overall Address Space

| Address Range | Size | Purpose |
|---------------|------|---------|
| `$0000-$00FF` | 256 B | **Zero page** — shared between EhBASIC and the monitor (see split below) |
| `$0100-$01FF` | 256 B | **Stack** — grows down from `$01FF` |
| `$0200-$03FF` | 512 B | **System variables** — BASIC page-2 vars + monitor variables/buffers |
| `$0400-$07FF` | 1 KB | Formerly the 40×25 screen; the screen now lives behind the VIC register port (see below). **Not free**: `$0400` is the `T:`/`Z:` page snapshot and `$0500-$07FF` is the assembler's identifier buffers and symbol table. Usable as scratch by a program that uses neither |
| `$0800-$87FF` | 32 KB | **Free RAM** — user programs; BASIC program/variables/strings when BASIC runs; the assembler reserves `$7800-$87FF` (source) and `$7600-$77FF` (symbols) while building |
| `$8800-$AFFF` | 10 KB | **DOS ROM** — always-mapped MFC-DOS resident ROM (FAT16 filesystem + DOS shell) |
| `$B000-$EFFF` | 16 KB | **Module window** — bank 0 = RAM, banks 1..255 = ROM modules (BASIC is bank 1) |
| `$F000-$FFFF` | 4 KB | **Kernel BIOS** — the monitor is module bank 4, not here |
| `$FE00-$FE28` | — | **PIA** I/O + `MODULE_BANK` ($FE23) + block-device registers ($FE24-$FE28) — within the kernel region |

There is no SID / CIA. The **VIC** is an 80×25 color text chip whose character
and color planes live *inside the chip* (not in the 64K map) and are reached
through a VDC-style register port at `$FE2D-$FE37` (see the I/O section). The
keyboard and file I/O are exposed through a small PIA-style register block at
`$FE00`. The `$B000-$EFFF` window is
a bank-switched **module slot**: the `MODULE_BANK` register (`$FE23`) selects
RAM (bank 0) or one of up to 255 pre-loaded ROM modules. See
`Part 4 (Bank-switched modules)`. The `$8800-$AFFF` **DOS ROM** is an always-mapped (never
banked) read-only region holding the resident filesystem; see `SYSTEM_INTERNALS.md`.

### Zero Page

The monitor's zero-page variables were relocated to `$14-$39` to avoid the
EhBASIC interpreter, which uses page zero heavily. The split:

| Range | Owner | Notes |
|-------|-------|-------|
| `$00-$13` | EhBASIC | warm-start vector, USR vector, FAC temporaries, etc. |
| `$14-$39` | **Monitor** | see table below (free when BASIC is not the active workspace) |
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

When BASIC is running it owns the low part of this region; the monitor's
variables live above it. The monitor command buffer overlaps BASIC's area but
the two never run at the same time.

| Range | Owner | Purpose |
|-------|-------|---------|
| `$0200-$020C` | EhBASIC | I/O vectors (`ccflag`, `VEC_IN`/`VEC_OUT`/`VEC_LD`/`VEC_SV`) |
| `$0221-$0268` | EhBASIC | input line buffer (`Ibuff`) |
| `$0200-$024F` | Monitor | `MON_CMDBUF` — 80-byte command input buffer (overlaps BASIC; mutually exclusive) |
| `$0269-$028D` | Monitor | monitor variables (relocated above BASIC's `$0268`) — see below |
| `$028E-$02DD` | Monitor | `MON_LAST_CMD_BUF` — 80-byte last-command buffer (`.` recall) |
| `$02DE-$03FF` | free | available system RAM |

#### Monitor variables (`$0269-$028D`)

| Address | Symbol | Purpose |
|---------|--------|---------|
| `$0269` | `MON_CMDPTR` | Command buffer position |
| `$026A` | `MON_CMDLEN` | Command length |
| `$026B` | `MON_MODE` | Monitor mode (0=Command, 1=Write) |
| `$026C-$026D` | `MON_STARTADDR_LO/HI` | Range start address |
| `$026E-$026F` | `MON_ENDADDR_LO/HI` | Range end address |
| `$0270` | `MON_PARSE_PTR` | Parser position |
| `$0271` | `MON_PARSE_LEN` | Remaining parse length |
| `$0272` | `MON_HEX_TEMP` | Hex conversion temp |
| `$0273` | `MON_BYTE_COUNT` | Byte counter |
| `$0274` | `MON_LINE_COUNT` | Display line counter |
| `$0275` | `MON_ERROR_FLAG` | Error flag |
| `$0276` | `CURSOR_X` | Cursor X (0-39) |
| `$0277` | `CURSOR_Y` | Cursor Y (0-24) |
| `$0278` | `MON_MSG_TMP_POS` | Temp message position |
| `$0279` | `MON_FILL_VALUE` | Fill (F:) byte value |
| `$027A-$027B` | `MON_DEST_ADDR_LO/HI` | Move/copy (M:) destination |
| `$027C` | `MON_COPY_MODE` | Move/copy mode (0=copy, 1=move) |
| `$027D-$028C` | `MON_SEARCH_PATTERN` | Search (X:) pattern, up to 16 bytes |
| `$028D` | `MON_PATTERN_LEN` | Search pattern length |

Note: `DEC_DIGIT_BUFFER` ($027D) deliberately aliases `MON_SEARCH_PATTERN` —
the D:/H: and X: commands never run at the same time.

### Video (VIC) register port (`$FE2D-$FE37`)

The 80×25 screen is **not** in the 64K address space. The VIC owns two parallel
cell planes — a character plane (one 7-bit ASCII byte per cell) and a color
plane (one attribute byte per cell) — reached through an auto-incrementing
register port, the same idiom as the block device. Set a cell index via
`VREG_ADDR_LO/HI`, then read/write the `VREG_CHAR` / `VREG_COLOR` data ports
(each access advances the index, wrapping at 2000). `VREG_CMD` runs chip-side
block ops so the CPU never copies the screen.

| Address | Register | Purpose |
|---------|----------|---------|
| `$FE2D` | `VREG_ADDR_LO` | Cell index low (0–1999) |
| `$FE2E` | `VREG_ADDR_HI` | Cell index high |
| `$FE2F` | `VREG_CHAR` | Char data port (bit 7 → reverse); auto-increments |
| `$FE30` | `VREG_COLOR` | Color/attribute data port; auto-increments |
| `$FE31` | `VREG_ATTR` | Current attribute latch applied to `VREG_CHAR` writes |
| `$FE32` | `VREG_CMD` | `1`=clear, `2`=scroll up, `3`=scroll down, `4`=fill row, `5`=set row size (param: bit 7 double, bits 4–0 row), `6`=all rows normal, `7`=set scroll-region top row (param) |
| `$FE33` | `VREG_STATUS` | `0` = ready |
| `$FE34` | `VREG_CURSOR_LO` | Hardware cursor cell low |
| `$FE35` | `VREG_CURSOR_HI` | Cursor cell high; bit 7 = cursor hidden |
| `$FE36` | `VREG_CMD_PARAM` | Command parameter / fill character |
| `$FE37` | `VREG_SCROLL_BOT` | Scroll-region bottom row (default 24) |

The scroll region is rows *top*..*bottom* inclusive; scroll commands shift only
those rows and leave everything outside them untouched, so an app can pin a
header above and a status line below. A clear (`VREG_CMD` `1`) resets it to the
whole screen, so anything holding a region has to reprogram it after a clear.
The bottom has a register of its own; the top rides the command engine (`7`)
only because the port block ends at `$FE37` with the SID immediately after.
Commands `5` and `7` consume `VREG_CMD_PARAM`, which is also the fill
character — set the fill char again before the next clear, scroll or fill-row.
IRC pins its input/status rows below the region, EDIT pins its status line, and
TERM maps the pair onto ANSI's DECSTBM (`ESC [ top ; bot r`).

Attribute byte: `[R][BR][bg:3][fg:3]` — bit 7 reverse, bit 6 bright, bits 5–3
background (0–7), bits 2–0 foreground (0–7). Power-on/clear default is `$02`
(green on black). The kernel tracks the logical cursor in `CURSOR_X/Y`
(`$0276/$0277`) and writes the screen through this port; `K_SET_ATTR` (`$FF2D`)
sets the color latch.

### Sound (SID) register port (`$FE38-$FE54`)

A software sound chip modeled on the MOS 6581/8580 SID: the real 29-register
layout (three voices + filter) relocated from `$D400` to `$FE38`, so SID
knowledge and music transfer directly. Per voice (V1 `$FE38`, V2 `$FE3F`,
V3 `$FE46`; 7 registers each): `FREQ_LO/HI`, `PW_LO/HI` (12-bit pulse width),
`CONTROL` (bit0 gate, bit1 sync, bit2 ring, bit3 test, bit4 triangle, bit5
sawtooth, bit6 pulse, bit7 noise), `ATK/DEC`, `SUS/REL`. Global: `FC_LO`/`FC_HI`
(11-bit cutoff, `$FE4D/4E`), `RES_FILT` (resonance + per-voice routing, `$FE4F`),
`MODE_VOL` (filter mode LP/BP/HP + master volume, `$FE50`), and read-only
`OSC3`/`ENV3` voice-3 read-back (`$FE53/54`).

The host synthesizes 44.1 kHz PCM from the register state and plays it through
Qt (`SidAudio`/`QAudioSink`). Ring/sync modulation are not modeled. The kernel
uses voice 1 for the system beep and the `K_SOUND_TONE`/`K_SOUND_OFF` ABI; ASCII
BEL (`$07`) rings a short non-blocking beep (gated off by the timer IRQ). All
kernel sound honors the `SOUND_ENABLE` zero-page flag (`$29`, default on).

### Real-time clock (RTC) register port (`$FE55-$FE60`)

A read-only real-time clock that mirrors the host's local wall-clock time (always
correct, not settable — so no battery-backed persistence is needed). The DOS
`DATE` command reads it.

| Address | Register | Notes |
|---------|----------|-------|
| `$FE55` | `RTC_LATCH` | write any value → snapshot host time into the fields; read = 0 |
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

The I/O page sits at `$FE00-$FEFF`, inside the kernel ROM region (the kernel just
avoids placing code there). It was moved here from the old `$DC00` so the
`$B000-$EFFF` region is a clean, I/O-free, bank-switched module slot (see
`Part 4 (Bank-switched modules)`).

A single PIA-style device provides keyboard input and host file I/O. There are
two file models: **block** (kernel `L:`/`S:` — whole memory range in/out) and
**byte stream** (BASIC `LOAD`/`SAVE` — one byte at a time via the data register).
Separately, a **block device** ($FE24-$FE28) presents a host `disk.img` as
512-byte sectors — the storage layer beneath the MFC-DOS FAT16 filesystem (see
`SYSTEM_INTERNALS.md`); it is independent of the PIA file models above.

| Address | Register | Purpose |
|---------|----------|---------|
| `$FE00` | `PIA_DATA` | Keyboard data (read consumes a key) |
| `$FE02` | `PIA_CONTROL` | Status flags (bit 0 = data available) |
| `$FE0E` | `TIMER_IRQ_ACK` | Write to acknowledge the ~60 Hz periodic timer IRQ |
| `$FE0F` | `KEY_STATE` | Live held-key bitmask (read-only) — see below |
| `$FE10` | `FILE_COMMAND` | File op: load/save (block), open-read/open-write/close (stream) |
| `$FE11` | `FILE_STATUS` | Idle / in-progress / success / stream-open / EOF / error |
| `$FE12-$FE13` | `FILE_ADDR_LO/HI` | Block load/save target/start address |
| `$FE14-$FE1F` | `FILE_NAME_BUF` | Filename buffer (12 bytes) |
| `$FE20-$FE21` | `FILE_END_ADDR_LO/HI` | Block save end address |
| `$FE22` | `FILE_DATA` | Byte-stream data register (read next / write byte) |
| `$FE23` | `MODULE_BANK` | Module bank select: 0 = RAM, 1..255 = ROM module mapped at `$B000-$EFFF` |
| `$FE24-$FE25` | `BLK_LBA` | Block device: 16-bit sector number (little-endian) |
| `$FE26` | `BLK_CMD` | Block device: 1 = read sector, 2 = write sector |
| `$FE27` | `BLK_STATUS` | Block device: 0 = ready, $FF = error |
| `$FE28` | `BLK_DATA` | Block device: 512-byte sector data port (auto-incrementing) |

#### `KEY_STATE` (`$FE0F`) — the control port

`PIA_DATA` is a queue of what was *typed*; `KEY_STATE` is a snapshot of what is
*held*. Read it as a bitmask, active-high:

| Bit | Key | | Bit | Key |
|-----|-----|-|-----|-----|
| 0 | Up | | 3 | Right |
| 1 | Down | | 4 | Fire (Space) |
| 2 | Left | | 5 | Button 2 (Left Shift) |

Bits 6–7 are reserved and read 0. The read is non-destructive — poll it every
frame for as long as the key is down.

An action game cannot work from the keystroke queue alone. That queue carries no
key-up, so the only evidence a key is still held is host auto-repeat, which stalls
for ~500 ms before starting and — on most platforms — repeats only the *most
recently pressed* key, so pressing fire silently cancels a held direction. These
bits are independent, so steering and firing at once is expressible at all, and
movement is as smooth as the polling rate rather than the repeat rate.

The register reads 0 when nothing sets it, which is the case for the console
build and the headless test harness; programs that use it degrade to "no input"
rather than misbehaving. `programs/kpanic/glue.s` shows the accessor.

### ROM Layout

#### Kernel BIOS (`$F000-$FFFF`, 4 KB)

| Segment | Range | Purpose |
|---------|-------|---------|
| `CODE` | `$F000-$F619` (1562 B) | BIOS code and data |
| `IORESV` | `$FE00-$FEFF` (256 B) | Reserved I/O page (PIA + `MODULE_BANK` + VIC + SID) |
| `JUMPS` | `$FF00-$FF41` (66 B) | Kernel API jump table (22 entries) |
| `VECS` | `$FFFA-$FFFF` (6 B) | Interrupt/reset vectors |
| (free) | ~`$EF45-$FDFF` | ~3.6 KB unused |

#### Kernel API jump table (`$FF00`)

| Address | Symbol | Routine |
|---------|--------|---------|
| `$FF00` | `K_PRINT_CHAR` | `PRINT_CHAR` |
| `$FF03` | `K_PRINT_MESSAGE` | `PRINT_MESSAGE` |
| `$FF06` | `K_PRINT_NEWLINE` | `PRINT_NEWLINE` |
| `$FF09` | `K_GET_KEYSTROKE` | `GET_KEYSTROKE` |
| `$FF0C` | `K_CLEAR_SCREEN` | `CLEAR_SCREEN` |
| `$FF0F` | `K_GET_RAND_NUM` | `GET_RANDOM_NUMBER` |
| `$FF12` | `K_RETURN_MODULE` | `RETURN_FROM_MODULE` — unmaps the bank, returns to monitor (BASIC `BYE`) |
| `$FF15` | `K_READ_LINE` | `READ_COMMAND_LINE` — edited line input (backspace/ESC) → `MON_CMDBUF`/`MON_CMDLEN` |
| `$FF18` | `K_PARSE_HEX` | `HEX_QUAD_TO_ADDR` — X = offset in `MON_CMDBUF` → `MON_CURRADDR`, carry set if invalid |
| `$FF1B` | `K_PRINT_HEX_BYTE` | `PRINT_HEX_BYTE` — print A as two hex digits |
| `$FF1E` | `K_MON_ENTRY` | `MONITOR_MAIN` — DOS launches the monitor here (`MON`) |
| `$FF21` | `K_LAUNCH_BY_NAME` | `LAUNCH_BY_NAME` — DOS launches a module by name |
| `$FF24` | `K_LIST_MODULES` | `LIST_MODULES` — print the module catalog (`BANKS`) |
| `$FF27` | `K_PRINT_DEC` | `PRINT_DEC` — print a 32-bit value in decimal |
| `$FF2A` | `K_PARSE_DEC` | `PARSE_DEC_ABI` — parse a decimal string from `MON_CMDBUF` |
| `$FF2D` | `K_SET_ATTR` | `SET_ATTR` — set the color/attribute latch (`VREG_ATTR`) |
| `$FF30` | `K_PRINT_HELP_LINE` | `PRINT_HELP_LINE` — print `"syntax"`<TAB>`"desc"` (TAB pads to a fixed column) for two-column help listings |
| `$FF33` | `K_SOUND_TONE` | `SOUND_TONE` — play a tone on SID voice 1 (A = freq low, X = freq high); honors `SOUND_ENABLE` |
| `$FF36` | `K_SOUND_OFF` | `SOUND_OFF` — stop voice 1 (gate off) |
| `$FF39` | `K_GET_JIFFIES` | `GET_JIFFIES` — read the 60 Hz monotonic tick counter (returns A = low, X = high) |

The jump table is also the **module ABI**: a ROM module reaches kernel services
only through these entries, so it is independent of where the kernel's internal
routines live. The `$FF15`/`$FF18` services share the monitor's command buffer
(`MON_CMDBUF`) and `MON_CURRADDR` as scratch — safe because the monitor is
suspended while a module runs and that state is saved/restored across the launch.

#### Module window (`$B000-$EFFF`, 16 KB)

A bank-switched slot selected by `MODULE_BANK` (`$FE23`). Bank 0 is RAM (the
boot/default state, zeroed by `RESET`); banks 1..255 are read-only ROM modules
pre-loaded by the host. The kernel owns a `MODULE_DIR` catalog (bank #, entry
address, name); the `B:` menu lists it and, on selection, writes `MODULE_BANK`
and `JMP`s to the module entry. A module exits with `JMP $FF12`, which unmaps
the bank.

**BASIC is module bank 1.** EhBASIC 2.22p5 with project additions; cold start
(`LAB_COLD`) is at `$B000`. BASIC I/O is routed through the kernel via the
page-2 vectors (`VEC_IN`/`OUT` → keyboard/screen; `VEC_LD`/`SV` → the
file-stream LOAD/SAVE routines).

### Interrupt Vectors (`$FFFA-$FFFF`)

| Address | Vector | Handler |
|---------|--------|---------|
| `$FFFA-$FFFB` | NMI | `NMI_HANDLER` — STOP key: sets BASIC's `ON NMI` flag if armed, else breaks to the monitor (clearing the pager guard and unmapping any module bank) |
| `$FFFC-$FFFD` | RESET | `RESET` (power-on entry) |
| `$FFFE-$FFFF` | IRQ | `IRQ_HANDLER` — acknowledges the ~60 Hz timer, advances the `K_GET_JIFFIES` counter, sets BASIC's `ON IRQ` flag if armed |

### Free RAM for User Programs

- `$3A-$5A` — small free zero-page gap (fast addressing) when BASIC is not in use.
- `$02DE-$03FF` — leftover system-variable space.
- `$0800-$87FF` — main user RAM (32 KB). Avoid `$0400-$07E7` (screen) and
  `$8800-$AFFF` (DOS ROM). When BASIC is active this is its program/variable/string
  space (`Ram_base=$0800`, `Ram_top=$8800`). The assembler reserves the top of this
  region while building (`$7800-$87FF` source, `$7600-$77FF` symbols).

### Key Constants (from `kernel.asm`)

| Symbol | Value | Purpose |
|--------|-------|---------|
| `STACK_TOP` | `$FF` | Initial stack pointer |
| `SCREEN_WIDTH` | `80` | Characters per line |
| `SCREEN_HEIGHT` | `25` | Lines on screen |
| `LINES_PER_PAGE` | `24` | Paging threshold |

(The screen is no longer memory-mapped; the kernel writes it through the VIC
register port at `$FE2D-$FE37` and tracks the logical cursor in `CURSOR_X/Y`.)

---

## Part 3 — Kernel API ($FF00 jump table)


### Overview

The MFC kernel exposes a stable jump table at `$FF00`. User programs and bank
modules call these routines with `JSR` to the fixed addresses below; the entries
never move, so a program built today keeps working as the kernel evolves.

### What is in the BIOS, and what is not

The kernel ROM holds the **machine**; everything that is merely *software shipped
with the machine* lives in a bank module or on disk. Concretely:

| In the BIOS (`$F000-$FFFF`) | Elsewhere |
|---|---|
| Screen output, cursor, scrolling, the pager | The monitor — **module bank 4** |
| Keyboard input and line editing (`K_READ_LINE`, `.` recall) | BASIC — bank 1 |
| Hex and decimal conversion (`K_PARSE_HEX`, `K_PRINT_DEC`, …) | FORTH — bank 3 |
| IRQ/NMI handlers, the 60 Hz tick, NMI break-in | FORTH — bank 3 |
| Sound (`K_SOUND_TONE`) and the RNG | EDIT / TERM / IRC — disk `.PRG` files |
| Bank launching (`K_LAUNCH_BY_NAME`, `RETURN_FROM_MODULE`) | The filesystem and shell — MFC-DOS at `$8800` |
| The `$FF00` table and the `$FFFA` vectors | |

The monitor used to be two thirds of the kernel. It is a bank module because a
disk program would load at `$0800` — precisely the memory a monitor exists to
inspect — so it would overwrite the program under test. A bank costs no user RAM,
maps instantly, and works with no disk present. The trade is that the monitor
cannot show its own window: `R:B000-EFFF` displays the monitor's ROM rather than
bank-0 RAM, and sibling banks are invisible for the same reason.

**The boundary is enforced, not aspirational.** Because the BIOS and the monitor
are separate link units, neither can name the other's labels — the assembler
rejects it. What the assembler cannot see is that `monitor.asm` reaches the BIOS
through hand-written equates to `$FF00` addresses; insert an entry in the middle
of the table and every equate below it still assembles while pointing one slot
off. The `kernel_bios_monitor_split` test
(`tests/scripts/check_kernel_split.py`) checks every equate against the table
below, and the bank's entry addresses against the constants the kernel jumps to.

Two consequences worth knowing when adding to the kernel:

- **Append to the jump table, never insert.** Existing entries are an ABI that
  disk programs and every module bind to by address.
- **The BIOS may not call into a module.** The window may not be mapped, and if it
  is, it may hold a different bank. Anything the BIOS needs must live in the BIOS —
  which is why `.`-recall and the boot-time window clear were moved down out of the
  monitor rather than published.

### Calling convention

- Parameters and results are passed in registers (A, X, Y) unless noted.
- The carry flag often signals success/failure (carry set = error/none, per entry).
- A few zero-page locations are part of the ABI:
  - `$14/$15` `MON_CURRADDR` — result of `K_PARSE_HEX` / `K_PARSE_DEC`.
  - `$16/$17` `MON_MSG_PTR` — string pointer for `K_PRINT_MESSAGE`.
  - `$24` `RNG_MAX` — upper bound for `K_GET_RAND_NUM`.
  - `$0200` `MON_CMDBUF` — line buffer filled by `K_READ_LINE`.

### Jump table (summary)

| Addr | Name | Purpose |
|------|------|---------|
| `$FF00` | `K_PRINT_CHAR` | Print A as a character (handles CR/BS) |
| `$FF03` | `K_PRINT_MESSAGE` | Print null-terminated string at (`$16/$17`) |
| `$FF06` | `K_PRINT_NEWLINE` | Print CR/LF |
| `$FF09` | `K_GET_KEYSTROKE` | Non-blocking key read (carry set + A = key when ready) |
| `$FF0C` | `K_CLEAR_SCREEN` | Clear screen, home cursor |
| `$FF0F` | `K_GET_RAND_NUM` | A = random `1..RNG_MAX` |
| `$FF12` | `K_RETURN_MODULE` | Bank module exit → return to the monitor |
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

### Details

#### Output

**`K_PRINT_CHAR` — `$FF00`** — print A as a character. `$0D` (CR) moves to the
start of the next line; `$08` (BS) backspaces and clears. Preserves X and Y.

**`K_PRINT_MESSAGE` — `$FF03`** — print a null-terminated string (< 256 bytes).
Put the address in `MON_MSG_PTR` (`$16` low, `$17` high) first.

```assembly
    LDA #<MSG
    STA $16
    LDA #>MSG
    STA $17
    JSR $FF03
MSG: .BYTE "HELLO WORLD", 0
```

**`K_PRINT_NEWLINE` — `$FF06`** — print CR/LF.

**`K_PRINT_HEX_BYTE` — `$FF1B`** — print A as two hex digits. Preserves X, Y.

**`K_PRINT_DEC` — `$FF27`** — print a 32-bit little-endian value in decimal.
Input: `A`/`X` = pointer (low/high) to a 4-byte value in memory; `Y` = field
width (0 = no padding; larger = right-align with leading spaces). Copies the value
into its own workspace, so the caller's bytes are untouched.

```assembly
    LDA #<NUM      ; NUM holds the value, 4 bytes little-endian
    LDX #>NUM
    LDY #4         ; right-align in a 4-column field
    JSR $FF27
```

**`K_SET_ATTR` — `$FF2D`** — latch A as the color/attribute for characters printed
afterward. Attribute byte: bit7 reverse, bit6 bright, bits5-3 background (0-7),
bits2-0 foreground (0-7). Colors: 0 black, 1 red, 2 green, 3 yellow, 4 blue,
5 magenta, 6 cyan, 7 white. Default `$02` (green on black).

**`K_PRINT_HELP_LINE` — `$FF30`** — print a command's `syntax`<TAB>`description`
line with the description padded to column 22; used by the `?`/help screens. The
string pointer goes in `MON_MSG_PTR` (`$16/$17`), as for `K_PRINT_MESSAGE`, and the
string is `"<syntax>",$09,"<description>",0`.

**`K_GET_KEYSTROKE` — `$FF09`** — **non-blocking**. Returns carry set with the key
in A when one is waiting, carry clear when the buffer is empty. To wait for a key,
spin: `@w JSR $FF09 : BCC @w`.

**`K_READ_LINE` — `$FF15`** — read one edited line (backspace/ESC handled) into
`MON_CMDBUF` (`$0200`); the length is left in `MON_CMDLEN` and returned in A (zero
flag set for an empty line).

**`K_PARSE_HEX` — `$FF18`** — parse a hex address from `MON_CMDBUF` starting at
offset X; result in `MON_CURRADDR` (`$14/$15`), carry set if invalid.

**`K_PARSE_DEC` — `$FF2A`** — parse a decimal number from `MON_CMDBUF` starting at
offset X; result in `MON_CURRADDR` (`$14/$15`), carry set if invalid. Pair with
`K_READ_LINE` to read a number the user typed.

#### Screen / system

**`K_CLEAR_SCREEN` — `$FF0C`** — clear the screen (fill with spaces) and home the
cursor.

**`K_GET_RAND_NUM` — `$FF0F`** — return a random integer `1..RNG_MAX` in A. Store
the inclusive upper bound in `RNG_MAX` (`$24`) first. The generator is an
RTC-seeded 16-bit LFSR, so sequences differ from run to run.

**`K_MON_ENTRY` — `$FF1E`** — cold-enter the monitor. (DOS jumps here to start the
`MON` command.)

**`K_RETURN_MODULE` — `$FF12`** — from within a bank module (e.g. BASIC, DEV
TOOLS), unmap the module bank and return control to the monitor.

**`K_LAUNCH_BY_NAME` — `$FF21`** / **`K_LIST_MODULES` — `$FF24`** — DOS-internal:
launch a disk program or bank module by name, and print the module/bank catalog
(the `BANKS` command).

#### Sound

**`K_SOUND_TONE` — `$FF33`** — play a sustained tone on SID voice 1; `A` = frequency
low byte, `X` = frequency high (`Fout = FREQ * clock / 2^24`). Honors the
`SOUND_ENABLE` mute (`$29`).

**`K_SOUND_OFF` — `$FF36`** — stop voice 1 (gate off).

**`K_GET_JIFFIES` — `$FF39`** — read the monotonic 60 Hz tick counter; returns
`A` = low byte, `X` = high byte. It starts at 0 on RESET, is advanced by the timer
IRQ, and wraps every 65536 ticks (~18.2 minutes) — compare deltas with unsigned
subtraction and the wrap is harmless. The read is `SEI`-guarded internally so the
two bytes can't tear, and the caller's interrupt-enable state is preserved. Use it
for frame pacing in real-time programs (a fixed-tick accumulator loop) rather than
counting instructions, which drifts with host speed.

---

See `examples/` for runnable programs that use these calls, and
`Part 2 (Memory and zero-page map)` for the full memory/zero-page map.

---

## Part 4 — Bank-switched modules

> **Historical.** This part records the design of the module slot as it was argued at the time, including the addresses and sizes then in play (`$B000-$DFFF`, 12 KB, an 8 KB kernel at `$E000`). The window is now `$B000-$EFFF` (16 KB) under a 4 KB BIOS at `$F000`, and the monitor is bank 4. Part 2 is the authoritative current map; the reasoning below is left as written.


**Status:** Phases 1–5 implemented (kernel v3.27). I/O is at `$FE00`, the module
window is a clean bank-switched slot (`MODULE_BANK` `$FE23`), **BASIC is module
bank 1**, and a **DEV TOOLS module is bank 2** (`src/kernel/assembler/`,
`assembler.rom`). `B:` is the module bank menu (driven by the kernel `MODULE_DIR`
catalog), modules return via `$FF12` (`RETURN_FROM_MODULE`, which unmaps the bank),
and `RESET` zeroes the window so bank 0 boots clean.

The dev-tools module (v0.7) provides:
- **Disassembler** (`D xxxx`) — decodes via the canonical 65C02 table generated
  from the CPU emulator (`tools/gen_opcode_table.py` → `opcodes_65c02.inc`).
- **Line assembler** (`A xxxx`) — immediate, no-file, numeric operands; quick patches.
- **Two-pass assembler** (`B`) — labels, `NAME = expr`, expressions (`+`/`-`,
  `<`/`>`), pseudo-ops `.ORG`/`*=`, `.END`, `.BYTE`/`.DB`, `.WORD`/`.DW`,
  `.ASCII`/`.TX`; build listing; `? LINE nnnn` errors.
- **Source load** (`L`) — reads a host `.s` file into the `$A000` source buffer via
  the byte-stream file interface; symbol table at `$9E00`.

It reaches the system only through the `$FF00` jump table (extended in Phase 4 with
`K_READ_LINE`/`K_PARSE_HEX`/`K_PRINT_HEX_BYTE`). Source can be authored either on the
host (`L`) or on the machine: the resident FAT16 filesystem and the full-screen
`EDIT` program (see [EDIT.md](EDIT.md)) close the loop, so **edit → assemble → SAVE →
run by name** all happen at the `]` prompt without host involvement.

### Goal

Stop growing (or shrinking) the kernel ROM to add big features. Instead, make the
12 KB region currently occupied by EhBASIC a **bank-switched module window**: a slot
into which the kernel maps one ROM "module" at a time (BASIC, an assembler/
disassembler package, a Z-machine to play Zork, a text editor, …). BASIC becomes
just *one* module rather than a permanent resident.

This mirrors how real 6502 machines did it — cartridge ROMs, bank-switched ROM,
the Apple II language card.

#### Why not just grow / shrink the kernel?

- A debugger-grade monitor wants a disassembler (~1–1.5 KB) and mini-assembler
  (~1–1.5 KB). Those don't belong in the always-resident kernel.
- Shrinking the kernel to 4 KB to reclaim user RAM would be undone the moment we
  add a disassembler. Modules sidestep the whole question.

The kernel stays at `$E000–$FFFF` (8 KB), unchanged in start address.

### Memory map (target)

```
$0000–$07FF   Zero page / stack / system vars / screen      (unchanged)
$0800–$AFFF   User RAM (~42 KB)                              (module working RAM)
$B000–$DFFF   MODULE WINDOW (12 KB) — backed by selected bank; clean, no I/O hole
$E000–$EF6E   Kernel CODE (~3.9 KB at v3.27)                 (start unchanged)
$EF6F–$FDFF   free kernel ROM (~3.6 KB)                      (kernel growth room)
$FE00–$FEFF   I/O page (relocated here from $DC00)
$FF00–$FFF9   Kernel API jump table (grows upward; ~83 entries possible, 20 used)
$FFFA–$FFFF   NMI / RESET / IRQ vectors
```

Key property: **the module window contains no I/O** — any ROM assembled at `$B000`
runs in a clean, contiguous 12 KB with no addresses to avoid.

### Prerequisite: relocate I/O out of the module window (`$DC00` → `$FE00`)

Today the PIA/file-I/O lives at `$DC00–$DC22`, *inside* the module window (a vestige
of the C64-style map). That was tolerable when BASIC was the only, hand-authored
occupant. For arbitrary module ROMs we can't enforce a "don't touch this 36-byte
window" rule, so we remove the constraint by moving the I/O.

The I/O shadow doesn't vanish — it moves from the module window (third-party ROM
territory) into the kernel ROM's unused space (our territory), where avoiding it is
trivial: the kernel's CODE ends at `$EEC3`, nowhere near `$FE00`. I/O is fixed at
**one page** (`$FE00–$FEFF`) — current usage is ~36 registers and even generous
expansion stays far under 256; page-aligned I/O is also natural to decode on real
hardware.

New I/O page layout (re-based 1:1 from the old `$DCxx` block):

| Addr | Register |
|------|----------|
| `$FE00` | `PIA_DATA` — keyboard data |
| `$FE02` | `PIA_CONTROL` |
| `$FE0E` | timer IRQ acknowledge |
| `$FE10` | `FILE_COMMAND` |
| `$FE11` | `FILE_STATUS` |
| `$FE12/$FE13` | `FILE_ADDR_LO/HI` |
| `$FE14–$FE1F` | `FILE_NAME_BUF` (12 bytes) |
| `$FE20/$FE21` | `FILE_END_ADDR_LO/HI` |
| `$FE22` | `FIO_DATA` — BASIC byte-stream LOAD/SAVE |
| `$FE23` | **`MODULE_BANK`** — bank-select register |
| `$FE61` | **`POWER`** — soft power switch; write $5A then $A5 to switch off |

Touched by the relocation:
- `kernel.asm`: re-base the `PIA_*`, `FILE_*`, timer-ack equates.
- `basic.asm`: re-base `FIO_COMMAND`/`FIO_STATUS`/`FIO_DATA`.
- `src/computer/PIA.*` / `Memory`: update `isPiaAddress` (and any screen routing).
- `memory.cfg`: bound the `CODE` segment at `$FDFF` so the linker errors rather than
  growing into the I/O page.

Phase 1 (this relocation) is self-contained and worth doing on its own.

### Bank-select register — `MODULE_BANK = $FE23`

- **Write `n`:** map bank `n` into `$B000–$DFFF`.
  - `0` = RAM (slot is plain read/write RAM — the boot/default state).
  - `1…255` = read-only module ROM banks.
- **Read:** returns the current bank (kernel can save/restore).
- **Reset:** forced to `0`. **BASIC is not auto-loaded**; the slot starts empty.
- Lives in the always-mapped I/O page, so it's reachable regardless of what's mapped.

Bank capacity is bounded only by the register width: one byte → 256 banks × 12 KB
(~3 MB). We define a handful and leave the rest open.

### Emulator changes (`Memory`)

Bank-switched (not copy-on-demand): the host pre-loads each module image into a
`bankROM[1..N]` array at startup; switching is a pointer change — instant, and each
bank retains its own contents.

```
read(addr):
    if I/O addr ($FE00–$FEFF)              -> device / bank-register handler
    else if screen addr                    -> VIC
    else if $B000<=addr<=$DFFF and bank!=0  -> bankROM[bank][addr-$B000]   # read-only
    else                                   -> ram_[addr]

write(addr, v):
    if I/O addr                            -> device / bank-register handler
    else if screen addr                    -> VIC
    else if $B000<=addr<=$DFFF and bank!=0  -> ignored (ROM)
    else                                   -> ram_[addr]                    # bank 0 = RAM
```

### Module contract

A "module" is a 6502 ROM **ported to this system**:
1. Assembled to run from the module window (entry recorded in the directory below;
   `$B000` by default).
2. Reaches kernel services (character I/O, etc.) **only through the `$FF00` jump
   table** — the stable module ABI. (BASIC already does this via its `PG2_TABS`
   vectors.)
3. Returns to the monitor with `JMP $FF12` (`RETURN_FROM_MODULE`), which resets
   `MODULE_BANK = 0` and re-enters the command loop.
4. Uses `$0800–$AFFF` as working RAM, shared with all other modules → one tool at a
   time; "save your work before switching." Each module documents its RAM footprint.

A module is **not** required to reserve any specific bytes — there is no embedded
header or signature. Naming/entry metadata lives in the kernel (see below), so even
hard-to-modify third-party ROMs (a Z-machine, an off-the-shelf assembler) only need
the unavoidable port (re-base + retarget I/O), nothing more.

### Module directory (in the kernel ROM)

The kernel owns a curated catalog of known modules — like the `$FF00` jump table.
The `B:` menu and launcher read from it; the module ROMs stay untouched.

```
; One record per module: bank#, entry address, name pointer.
MODULE_DIR:
    .byte 1  : .word $B000 : .word NAME_BASIC      ; bank 1
    .byte 2  : .word $B000 : .word NAME_DEVTOOLS   ; bank 2
    .byte 3  : .word $B000 : .word NAME_ZORK       ; bank 3
MODULE_DIR_COUNT = 3

NAME_BASIC:    .byte "BASIC", 0
NAME_DEVTOOLS: .byte "ASSEMBLER / DISASSEMBLER", 0
NAME_ZORK:     .byte "ZORK (Z-MACHINE)", 0
```

Adding a module = add one record + name string, and add the ROM image to the host
bank set, then rebuild the kernel. The directory + names are tiny — well within the
~3.9 KB of kernel headroom.

### `B:` — Bank menu (replaces per-module commands)

`B:` is repurposed from "launch BASIC" to **"Bank"**: it lists the directory and lets
you pick a module to map + run.

```
BANKS:
  1  BASIC
  2  ASSEMBLER / DISASSEMBLER
  3  ZORK (Z-MACHINE)
  ?
```

- Build the menu by walking `MODULE_DIR` and printing each name.
- On a numeric selection: store the record's bank in `MODULE_BANK`, then `JMP`
  (record's entry address). ESC cancels back to the monitor.
- Adding a module never needs a new kernel command — it just appears in the menu.

```
; selection -> record index
LAUNCH_FROM_DIR:
    ; A = directory index chosen
    ; load bank#, entry from MODULE_DIR record
    STA MODULE_BANK          ; map the bank in
    JMP (entry)              ; run the module

; $FF12 handler
RETURN_FROM_MODULE:
    STZ MODULE_BANK          ; unmap (slot back to RAM)
    ...                      ; return to the monitor command loop
```

(Optional sanity byte-check after mapping is allowed but not required — the directory
is the source of truth.)

### Host-side bank registry

At startup the emulator loads module images into the bank table instead of writing
BASIC into flat RAM:
- bank 1 ← `basic.rom`
- bank 2 ← `assembler.rom`
- (3–255 reserved)

A small name→file map (config or convention). Bank 0 is RAM (no image).

### Settled decisions

1. **Naming/metadata → kernel-side `MODULE_DIR` table** (not embedded headers, no
   per-module signature). Works for hard-to-modify third-party ROMs; BASIC is just
   directory entry 1, no special-casing.
2. **First module → one combined "DEV TOOLS" ROM** (bank 2): assembler **and**
   disassembler together (they share the opcode/mnemonic tables).
3. **Feature placement → size-based split.** Big debugger machinery (disassembler,
   mini-assembler, single-step, breakpoints) lives in modules. Small always-useful
   commands (register display, hex add/subtract, memory compare) stay resident in the
   kernel.
4. **Bank 0 = RAM**, usable as scratch (not persistent across module loads).
5. **Module working RAM** = documented per-module footprint in `$0800–$AFFF`; one tool
   at a time, save before switching.
6. **`B:` = Bank menu**, replacing the old `B:` and any per-module command.

### Migration

- EhBASIC → **bank 1**, unchanged content (same `$B000` entry); the host registers it
  as a bank instead of loading it at boot. Its file-I/O equates move with the I/O
  relocation. It becomes directory entry 1.
- Kernel grows only: the I/O relocation, `MODULE_BANK` handling, `MODULE_DIR`, and the
  `B:` menu/launcher.

### Implementation phases

1. **[DONE, v2.2.7/8]** **Relocate I/O** `$DC00` → `$FE00` (kernel + basic + emulator),
   reserve the I/O page via an `IORESV` segment so the linker errors if `CODE` grows
   into it. Re-tested (integration suite + BASIC LOAD/SAVE). Window is now clean.
2. **[DONE, v2.2.9]** **Banking infrastructure**: `MODULE_BANK` register (`$FE23`) +
   `Memory` window routing (bank 0 = RAM, 1..255 = read-only ROM) + host bank table
   (`Memory::loadBank`). `RESET` maps the window to RAM. Behavior-preserving: BASIC
   still loads into bank-0 RAM at `$B000`. Covered by `tests/test_memory_banking.cpp`
   (11 cases) and the unchanged integration suite.
3. **[DONE, v3.0]** **Convert BASIC to bank 1**: the host installs `basic.rom` as a
   bank (`Memory::loadBank(1, …)`) instead of flat RAM. Added the kernel `MODULE_DIR`
   catalog + the `B:` bank menu/launcher; `RETURN_FROM_BASIC` became
   `RETURN_FROM_MODULE` (`$FF12`) and now unmaps the bank on exit. `RESET` zeroes
   `$B000–$DFFF` so bank 0 boots clean (safe now that BASIC is a ROM bank). Factored
   `FILL_RANGE_CORE` out of `F:` and reused it for the window clear. Covered by
   `testBankMenu`/`testBankLaunch` in the integration suite.
4. **[DONE, v3.1/3.1.1]** **First new module**: combined assembler + disassembler
   in bank 2 (`assembler.rom`). Disassembler, line assembler, and a two-pass
   assembler (labels, expressions, `.ORG`/`.END`/`.BYTE`/`.WORD`/`.ASCII`, `=`),
   with host `.s` source load and a build listing. The module ABI was extended
   (`K_READ_LINE`/`K_PARSE_HEX`/`K_PRINT_HEX_BYTE`) so the module reuses the kernel
   instead of duplicating input/parsing/printing.

5. **[DONE]** **In-machine authoring**: the resident FAT16 filesystem (MFC-DOS,
   `$8800-$AFFF`) and the full-screen `EDIT` program mean source is written and
   saved on the machine rather than host-loaded. Self-hosting is complete.

Still open: a richer assembler (macros, more directives); single-step and
breakpoints in the monitor; more modules; and the undecided monitor-to-bank
relocation.
