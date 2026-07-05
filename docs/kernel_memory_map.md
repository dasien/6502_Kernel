# 6502 Kernel System Memory Map

This is a **software-based 6502 computer**, not a Commodore 64 emulator. It
uses ASCII (not PETSCII), a flat 64K address space (no memory banking), and a
small set of memory-mapped devices. This document reflects the actual kernel
(`kernel.asm`) and BASIC (`basic.asm`) source and the linker configs
(`memory.cfg`, `basic_memory.cfg`).

## Overall Address Space

| Address Range | Size | Purpose |
|---------------|------|---------|
| `$0000-$00FF` | 256 B | **Zero page** — shared between EhBASIC and the monitor (see split below) |
| `$0100-$01FF` | 256 B | **Stack** — grows down from `$01FF` |
| `$0200-$03FF` | 512 B | **System variables** — BASIC page-2 vars + monitor variables/buffers |
| `$0400-$07FF` | 1 KB | **Free RAM** — formerly the 40×25 screen; the screen now lives behind the VIC register port (see below), so this is unused RAM |
| `$0800-$8FFF` | ~34 KB | **Free RAM** — user programs; BASIC program/variables/strings when BASIC runs; the assembler reserves `$8000-$8FFF` (source) and `$7E00-$7FFF` (symbols) while building |
| `$9000-$AFFF` | 8 KB | **DOS ROM** — always-mapped MFC-DOS resident ROM (FAT16 filesystem; DOS shell later) |
| `$B000-$DFFF` | 12 KB | **Module window** — bank 0 = RAM, banks 1..255 = ROM modules (BASIC is bank 1) |
| `$E000-$FFFF` | 8 KB | **Kernel ROM** (monitor) |
| `$FE00-$FE28` | — | **PIA** I/O + `MODULE_BANK` ($FE23) + block-device registers ($FE24-$FE28) — within the kernel region |

There is no SID / CIA. The **VIC** is an 80×25 color text chip whose character
and color planes live *inside the chip* (not in the 64K map) and are reached
through a VDC-style register port at `$FE2D-$FE37` (see the I/O section). The
keyboard and file I/O are exposed through a small PIA-style register block at
`$FE00`. The `$B000-$DFFF` window is
a bank-switched **module slot**: the `MODULE_BANK` register (`$FE23`) selects
RAM (bank 0) or one of up to 255 pre-loaded ROM modules. See
`module_slot_design.md`. The `$9000-$AFFF` **DOS ROM** is an always-mapped (never
banked) read-only region holding the resident filesystem; see `dos_design.md`.

## Zero Page

The monitor's zero-page variables were relocated to `$14-$39` to avoid the
EhBASIC interpreter, which uses page zero heavily. The split:

| Range | Owner | Notes |
|-------|-------|-------|
| `$00-$13` | EhBASIC | warm-start vector, USR vector, FAC temporaries, etc. |
| `$14-$39` | **Monitor** | see table below (free when BASIC is not the active workspace) |
| `$3A-$5A` | free | unused gap |
| `$5B-$FF` | EhBASIC | descriptor stack, program/var/array/string pointers, FACs, PRNG, decimal workspace |

### Monitor zero-page variables (`$14-$39`)

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
| `$23` | `RNG_SEED` | PRNG seed |
| `$24` | `RNG_MAX` | PRNG max value |
| `$25-$34` | `HEX_LOOKUP_TABLE` | 16-byte hex digit table |
| `$35-$36` | `DEC_TEMP_LO/HI` | Decimal-conversion temp |
| `$37` | `DEC_DIGIT_IDX` | Decimal digit index |
| `$38-$39` | `DEC_RESULT_LO/HI` | Decimal-conversion result |

## Stack (`$0100-$01FF`)

System stack, growing downward from `$01FF`. The stack pointer is initialized
to `$FF` at reset.

## System Variables (`$0200-$03FF`)

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

### Monitor variables (`$0269-$028D`)

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

## Video (VIC) register port (`$FE2D-$FE37`)

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
| `$FE32` | `VREG_CMD` | `1`=clear, `2`=scroll up, `3`=scroll down, `4`=fill row |
| `$FE33` | `VREG_STATUS` | `0` = ready |
| `$FE34` | `VREG_CURSOR_LO` | Hardware cursor cell low |
| `$FE35` | `VREG_CURSOR_HI` | Cursor cell high; bit 7 = cursor hidden |
| `$FE36` | `VREG_CMD_PARAM` | Command parameter / fill character |
| `$FE37` | `VREG_SCROLL_BOT` | Scroll-region bottom row; scroll/fill affect rows 0..this (default 24). IRC pins its input/status rows below the region |

Attribute byte: `[R][BR][bg:3][fg:3]` — bit 7 reverse, bit 6 bright, bits 5–3
background (0–7), bits 2–0 foreground (0–7). Power-on/clear default is `$02`
(green on black). The kernel tracks the logical cursor in `CURSOR_X/Y`
(`$0276/$0277`) and writes the screen through this port; `K_SET_ATTR` (`$FF2D`)
sets the color latch.

## Sound (SID) register port (`$FE38-$FE54`)

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

## I/O — PIA (`$FE00-$FE23`)

The I/O page sits at `$FE00-$FEFF`, inside the kernel ROM region (the kernel just
avoids placing code there). It was moved here from the old `$DC00` so the
`$B000-$DFFF` region is a clean, I/O-free, bank-switched module slot (see
`module_slot_design.md`).

A single PIA-style device provides keyboard input and host file I/O. There are
two file models: **block** (kernel `L:`/`S:` — whole memory range in/out) and
**byte stream** (BASIC `LOAD`/`SAVE` — one byte at a time via the data register).
Separately, a **block device** ($FE24-$FE28) presents a host `disk.img` as
512-byte sectors — the storage layer beneath the MFC-DOS FAT16 filesystem (see
`dos_design.md`); it is independent of the PIA file models above.

| Address | Register | Purpose |
|---------|----------|---------|
| `$FE00` | `PIA_DATA` | Keyboard data (read consumes a key) |
| `$FE02` | `PIA_CONTROL` | Status flags (bit 0 = data available) |
| `$FE10` | `FILE_COMMAND` | File op: load/save (block), open-read/open-write/close (stream) |
| `$FE11` | `FILE_STATUS` | Idle / in-progress / success / stream-open / EOF / error |
| `$FE12-$FE13` | `FILE_ADDR_LO/HI` | Block load/save target/start address |
| `$FE14-$FE1F` | `FILE_NAME_BUF` | Filename buffer (12 bytes) |
| `$FE20-$FE21` | `FILE_END_ADDR_LO/HI` | Block save end address |
| `$FE22` | `FILE_DATA` | Byte-stream data register (read next / write byte) |
| `$FE23` | `MODULE_BANK` | Module bank select: 0 = RAM, 1..255 = ROM module mapped at `$B000-$DFFF` |
| `$FE24-$FE25` | `BLK_LBA` | Block device: 16-bit sector number (little-endian) |
| `$FE26` | `BLK_CMD` | Block device: 1 = read sector, 2 = write sector |
| `$FE27` | `BLK_STATUS` | Block device: 0 = ready, $FF = error |
| `$FE28` | `BLK_DATA` | Block device: 512-byte sector data port (auto-incrementing) |

## ROM Layout

### Kernel ROM (`$E000-$FFFF`, 8 KB)

| Segment | Range | Purpose |
|---------|-------|---------|
| `CODE` | `$E000-$EF44` (3909 B) | Monitor code and data |
| `IORESV` | `$FE00-$FEFF` (256 B) | Reserved I/O page (PIA + `MODULE_BANK` + VIC + SID) |
| `JUMPS` | `$FF00-$FF38` (57 B) | Kernel API jump table (19 entries) |
| `VECS` | `$FFFA-$FFFF` (6 B) | Interrupt/reset vectors |
| (free) | ~`$EF45-$FDFF` | ~3.6 KB unused |

### Kernel API jump table (`$FF00`)

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

The jump table is also the **module ABI**: a ROM module reaches kernel services
only through these entries, so it is independent of where the kernel's internal
routines live. The `$FF15`/`$FF18` services share the monitor's command buffer
(`MON_CMDBUF`) and `MON_CURRADDR` as scratch — safe because the monitor is
suspended while a module runs and that state is saved/restored across the launch.

### Module window (`$B000-$DFFF`, 12 KB)

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

## Interrupt Vectors (`$FFFA-$FFFF`)

| Address | Vector | Handler |
|---------|--------|---------|
| `$FFFA-$FFFB` | NMI | `NMI_HANDLER` (currently a bare RTI) |
| `$FFFC-$FFFD` | RESET | `RESET` (power-on entry) |
| `$FFFE-$FFFF` | IRQ | `IRQ_HANDLER` (currently a bare RTI) |

## Free RAM for User Programs

- `$3A-$5A` — small free zero-page gap (fast addressing) when BASIC is not in use.
- `$02DE-$03FF` — leftover system-variable space.
- `$0800-$8FFF` — main user RAM (~34 KB). Avoid `$0400-$07E7` (screen) and
  `$9000-$AFFF` (DOS ROM). When BASIC is active this is its program/variable/string
  space (`Ram_base=$0800`, `Ram_top=$9000`). The assembler reserves the top of this
  region while building (`$8000-$8FFF` source, `$7E00-$7FFF` symbols).

## Key Constants (from `kernel.asm`)

| Symbol | Value | Purpose |
|--------|-------|---------|
| `STACK_TOP` | `$FF` | Initial stack pointer |
| `SCREEN_WIDTH` | `80` | Characters per line |
| `SCREEN_HEIGHT` | `25` | Lines on screen |
| `LINES_PER_PAGE` | `24` | Paging threshold |

(The screen is no longer memory-mapped; the kernel writes it through the VIC
register port at `$FE2D-$FE37` and tracks the logical cursor in `CURSOR_X/Y`.)
