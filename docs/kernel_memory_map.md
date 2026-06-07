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
| `$0400-$07E7` | 1000 B | **Screen RAM** — 40×25 text (`$07E8-$07FF` is unused padding) |
| `$0800-$AFFF` | ~42 KB | **Free RAM** — user programs; BASIC program/variables/strings when BASIC runs |
| `$B000-$DFFF` | 12 KB | **EhBASIC ROM** (loaded by the host) |
| `$DC00-$DC22` | — | **PIA** — keyboard input and file I/O registers |
| `$E000-$FFFF` | 8 KB | **Kernel ROM** (monitor) |

There is **no** VIC-II / SID / CIA / color memory / cartridge / banking. The
screen is plain RAM at `$0400` rendered by the host display; the keyboard and
file I/O are exposed through a small PIA-style register block at `$DC00`.

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
| `$1A-$1B` | `SCREEN_PTR_LO/HI` | Current screen position |
| `$1C-$1D` | `SCRL_SRC_ADDR_LO/HI` | Scroll source |
| `$1E-$1F` | `SCRL_DEST_ADDR_LO/HI` | Scroll destination |
| `$20` | `SCRL_BYTE_CNT` | Scroll byte counter |
| `$21` | `CMD_LINE_COUNT` | Lines printed by current command (paging) |
| `$22` | `PAGE_ABORT_FLAG` | Set when ESC pressed during paging |
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

## Screen RAM (`$0400-$07E7`)

| Range | Purpose |
|-------|---------|
| `$0400-$07E7` | 1000 bytes — 40×25 character display (written as ASCII) |
| `$07E8-$07FF` | 24 bytes — unused padding to the page boundary |

## I/O — PIA (`$DC00-$DC22`)

A single PIA-style device provides keyboard input and host file I/O. There are
two file models: **block** (kernel `L:`/`S:` — whole memory range in/out) and
**byte stream** (BASIC `LOAD`/`SAVE` — one byte at a time via the data register).

| Address | Register | Purpose |
|---------|----------|---------|
| `$DC00` | `PIA_DATA` | Keyboard data (read consumes a key) |
| `$DC02` | `PIA_CONTROL` | Status flags (bit 0 = data available) |
| `$DC10` | `FILE_COMMAND` | File op: load/save (block), open-read/open-write/close (stream) |
| `$DC11` | `FILE_STATUS` | Idle / in-progress / success / stream-open / EOF / error |
| `$DC12-$DC13` | `FILE_ADDR_LO/HI` | Block load/save target/start address |
| `$DC14-$DC1F` | `FILE_NAME_BUF` | Filename buffer (12 bytes) |
| `$DC20-$DC21` | `FILE_END_ADDR_LO/HI` | Block save end address |
| `$DC22` | `FILE_DATA` | Byte-stream data register (read next / write byte) |

## ROM Layout

### Kernel ROM (`$E000-$FFFF`, 8 KB)

| Segment | Range | Purpose |
|---------|-------|---------|
| `CODE` | `$E000-$F051` (~4178 B) | Monitor code and data |
| `JUMPS` | `$FF00-$FF14` (21 B) | Kernel API jump table |
| `VECS` | `$FFFA-$FFFF` (6 B) | Interrupt/reset vectors |
| (free) | ~`$F052-$FEFF` | ~3.9 KB unused |

### Kernel API jump table (`$FF00`)

| Address | Symbol | Routine |
|---------|--------|---------|
| `$FF00` | `K_PRINT_CHAR` | `PRINT_CHAR` |
| `$FF03` | `K_PRINT_MESSAGE` | `PRINT_MESSAGE` |
| `$FF06` | `K_PRINT_NEWLINE` | `PRINT_NEWLINE` |
| `$FF09` | `K_GET_KEYSTROKE` | `GET_KEYSTROKE` |
| `$FF0C` | `K_CLEAR_SCREEN` | `CLEAR_SCREEN` |
| `$FF0F` | `K_GET_RAND_NUM` | `GET_RANDOM_NUMBER` |
| `$FF12` | `K_RETURN_BASIC` | `RETURN_FROM_BASIC` (BASIC `BYE` exit point) |

### EhBASIC ROM (`$B000-$DFFF`, 12 KB)

EhBASIC 2.22p5 with project additions. Cold start (`LAB_COLD`) is at `$B000`;
the kernel's `B:` command checks the `$A0 $0C` signature there and jumps to it.
BASIC I/O is routed through the kernel via the page-2 vectors (`VEC_IN`/`OUT`
→ keyboard/screen; `VEC_LD`/`SV` → the file-stream LOAD/SAVE routines).

## Interrupt Vectors (`$FFFA-$FFFF`)

| Address | Vector | Handler |
|---------|--------|---------|
| `$FFFA-$FFFB` | NMI | `NMI_HANDLER` (currently a bare RTI) |
| `$FFFC-$FFFD` | RESET | `RESET` (power-on entry) |
| `$FFFE-$FFFF` | IRQ | `IRQ_HANDLER` (currently a bare RTI) |

## Free RAM for User Programs

- `$3A-$5A` — small free zero-page gap (fast addressing) when BASIC is not in use.
- `$02DE-$03FF` — leftover system-variable space.
- `$0800-$AFFF` — main user RAM (~42 KB). Avoid `$0400-$07E7` (screen). When
  BASIC is active this is its program/variable/string space (`Ram_base=$0800`,
  `Ram_top=$B000`).

## Key Constants (from `kernel.asm`)

| Symbol | Value | Purpose |
|--------|-------|---------|
| `STACK_TOP` | `$FF` | Initial stack pointer |
| `SCREEN_START` | `$0400` | Start of screen RAM |
| `SCREEN_WIDTH` | `40` | Characters per line |
| `SCREEN_HEIGHT` | `25` | Lines on screen |
| `LINES_PER_PAGE` | `24` | Paging threshold |
