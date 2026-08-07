# MFC 6502

<img src="assets/mfc6502-128.png" align="right" width="128" alt="MFC 6502 icon">

**MFC** is **My First Computer**: a software-defined WDC 65C02 machine with an
interactive monitor, a resident FAT16 filesystem, BASIC, FORTH, and a built-in
assembler — comprehensive enough to program itself, small enough to read.

The icon is the machine's own boot prompt, `]` and its cursor, set in the CP437
character ROM the VIC renders text with (`tools/make_icon.py` reads the glyphs
straight out of `include/computer/Cp437Font.h`, so the mark cannot drift from the
typeface it is drawn in).

This project started as a continuation of a CPU/assembler/disassembler I wrote in Python.  I wanted to create an actual
running environment to enter code directly or load from a file and run.

I also wanted to test the abilities of AI as part of the development and documentation process.

## Project Overview

This project implements a complete 6502-based computer system kernel for emulated environments. The kernel provides low-level system initialization, hardware control, and most importantly, a powerful interactive monitor program for direct system interaction.

**Key Features:**
- Complete 6502 assembly language kernel optimized for emulated environments
- Cycle-stepped WDC 65C02 CPU emulator (full CMOS instruction set, validated against the Klaus2m5/amb5l functional, decimal, and 65C02-extended test suites)
- Interactive monitor with comprehensive debugging tools
- **MFC/OS** DOS shell (`]` prompt) with a resident FAT16 filesystem and launch-by-name for disk programs
- Disk applications: **EDIT** (full-screen editor), **TERM** (ANSI/telnet terminal with XMODEM), **IRC** (chat client), plus games (**CHESS**, **KERNEL PANIC**, **VENTURE**, **The Sunless Vault** roguelike, and the Scott Adams adventures) — TERM and IRC keep a RAM **scrollback** buffer you page with **PgUp/PgDn**
- Built-in **MFC BASIC** interpreter (derived from EhBASIC), launched by typing `BASIC` at the DOS prompt (with human-readable `.bas` LOAD/SAVE via a host file dialog)
- **System-wide `--More--` pager**: long output from any program (DOS, monitor, BASIC, FORTH) pauses each screenful (SPACE advances, ESC stops)
- Memory manipulation and program execution capabilities
- Streamlined architecture with universal commands and simplified modes
- File I/O operations for loading and saving programs
- Comprehensive search, fill, move, and copy operations
- **Assembly examples** in `examples/` — a dozen runnable 6502 programs (loops, keyboard input, color, hex dump, an 8×8 multiply, a guess-the-number game) with a guide and ABI quick reference (`examples/README.md`)

## Monitor Program

The heart of this system is the **6502 Monitor** - a complete interactive debugging and programming environment that provides direct control over the computer's memory and execution. The monitor offers a command-line interface with powerful tools for memory operations, program execution, and system inspection.

### Architecture Overview

The monitor features a streamlined architecture with:
- **Two primary modes**: Command mode (default) and Write mode for interactive editing  
- **Simplified command processing** with consistent syntax and error handling
- **Command repeatability** recall last command for quick replay or modification
### Getting Started

The machine boots into the **MFC/OS** DOS shell, which shows a sign-on splash and
the `]` prompt:
```
]
```
From the DOS prompt you run disk programs by name (`EDIT`, `TERM`, `IRC`, `CHESS`, …),
manage files (`CATALOG`, `TYPE`, `COPY`, …), and launch the **monitor** with `MON`.
The monitor prompts with the current address followed by `>` (`?` for help, `Q` to
return to DOS):

```
0000>
```

The `NNNN>` prompt indicates you're in monitor command mode. You can now enter any monitor command.

## Monitor Commands

For complete command documentation including syntax and examples, see the
**[Monitor manual](docs/MONITOR.md)**.

### Quick Command Summary

| Category | Commands | Description |
|----------|----------|-------------|
| **Memory Operations** | R:, W:, F:, M:, X: | Read, write, fill, move/copy, and search memory |
| **Program Operations** | G:, L:, S: | Execute, load, and save programs |
| **Number Conversion** | D:, H: | Convert between decimal and hexadecimal |
| **Display Commands** | C:, T:, Z: | Clear screen, show stack, show zero page |
| **System Commands** | ?, ESC, . | Help, exit mode, command recall |

### Key Command Features

Commands are listed alphabetically by command letter (matching the on-screen `?` help):

- **C: Clear Screen** - Clear the display
- **D: Decimal to Hex** - Convert decimal (0-65535) to hexadecimal format
- **F: Fill Memory** - High-performance memory filling with progress feedback
- **G: Go/Run** - Direct program execution with return to monitor
- **H: Hex to Decimal** - Convert hexadecimal (0000-FFFF) to decimal format
- **L: Load File** - Load a host-selected file to an address: `L:8000` (host shows a file dialog)
- **M: Move/Copy** - Smart memory operations with overlap detection (`M:src-end,dest,B` where B: 0=copy, 1=move)
- **R: Read Memory** - Display bytes in memory, supports single addresses or ranges
- **S: Save File** - Save a memory range to a host-selected file: `S:8000-8FFF` (host shows a file dialog)
- **T: Stack** - Display the stack page ($0100-$01FF), paged
- **W: Write Memory** - Interactive hex editing with address advancement
- **X: Search Memory** - Multi-byte pattern search with paged output
- **Z: Zero Page** - Display zero page ($0000-$00FF), paged
- **ESC** - Exit the current mode and return to the command prompt

### Error Handling

The monitor provides clear, consistent error messages:
- **`ERROR?`** - Invalid command syntax or parameters
- **`RANGE?`** - Invalid or out-of-bounds address range
- **`VALUE?`** - Invalid hexadecimal characters in input

### **📖 [Detailed Architecture Reference](docs/ARCHITECTURE.md)**

### Memory Layout

- **$0000-$00FF**: Zero Page (system workspace; monitor uses $14-$39, EhBASIC uses the rest)
- **$0100-$01FF**: Stack memory
- **$0200-$03FF**: Monitor variables and command buffers
- **$0400-$07FF**: Formerly the screen (the 80×25 color screen now lives behind the VIC register port at `$FE2D-$FE37`, not in the address map). Claimed by the monitor: `$0400` is the `T:`/`Z:` snapshot, `$0500-$07FF` the assembler's symbol table
- **$0800-$AFFF**: User RAM (module working RAM; EhBASIC program/variable space)
- **$B000-$EFFF**: Module window (16 KB; bank 0 = RAM, banks 1..255 = ROM modules — BASIC 1, FORTH 3, MONITOR 4; bank 2 free since the assembler joined the monitor)

The chipset drawn as a board — bus, chips, I/O decode and interrupt lines — is in
[docs/BOARD.md](docs/BOARD.md).
- **$F000-$FFFF**: Kernel BIOS (4 KB; CODE ~1,560 bytes, rest free for growth). The monitor is bank 4, not here.
- **$FE00-$FE22**: PIA registers (keyboard, file I/O, timer) — an I/O page reserved within the kernel region

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full memory map and zero-page allocation.

### **📖 [Kernel Services Guide](docs/ARCHITECTURE.md)**


User programs can access kernel services via the jump table at $FF00:

| Address | Service | Description |
|---------|---------|-------------|
| $FF00 | PRINT_CHAR | Print single character |
| $FF03 | PRINT_MESSAGE | Print null-terminated string |
| $FF06 | PRINT_NEWLINE | Print carriage return/line feed |
| $FF09 | GET_KEYSTROKE | Wait for key press |
| $FF0C | CLEAR_SCREEN | Clear display |
| $FF0F | GET_RANDOM_NUMBER | Generate random byte |
| $FF12 | RETURN_FROM_MODULE | Module exit point — unmaps the bank, returns to DOS (BASIC `BYE`) |
| $FF2D | SET_ATTR | Set the color/attribute latch for subsequent output (A = `[R][BR][bg:3][fg:3]`) |

(Abridged — see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full 20-entry ABI table, including the decimal-conversion, module-launch, sound, and timing services.)

### File I/O Interface

The kernel provides memory-mapped file I/O at:
- **$FE10**: File command register
- **$FE11**: File status register  
- **$FE12-$FE13**: Address registers
- **$FE14-$FE1F**: Filename buffer
- **$FE20-$FE21**: End address (for save operations)

## Building and Development

### Prerequisites

| Dependency | Needed for | If missing |
|---|---|---|
| CMake 3.20+, Ninja | the build itself | nothing builds |
| C++20 compiler (GCC 10+, Clang 10+, MSVC 2019+) | the emulator | nothing builds |
| **cc65** (`ca65`, `ld65`, `cl65`) | assembling every ROM and `.PRG` | configuration fails — the emulator cannot run without its ROMs |
| **Qt6** or Qt5 — Core, Widgets, **Network** | the GUI machine; Network drives TERM/IRC | you get a non-interactive console demo, not the computer |
| **Qt Multimedia** | SID audio | everything works, silently |
| Python 3 | opcode-table drift test | that one test is skipped |

GoogleTest is **not** a prerequisite — the build fetches v1.14.0 itself when tests are enabled.

```bash
# Debian / Ubuntu / Mint  (verified)
sudo apt install build-essential cmake ninja-build cc65 qt6-base-dev qt6-multimedia-dev

# Fedora        (untested)
sudo dnf install gcc-c++ cmake ninja-build cc65 qt6-qtbase-devel qt6-qtmultimedia-devel
# Arch          (untested)
sudo pacman -S base-devel cmake ninja cc65 qt6-base qt6-multimedia
# macOS         (untested)
brew install cmake ninja cc65 qt
```

> **If you install a dependency later, delete the build directory before
> rebuilding.** CMake caches "not found" results, so adding Qt or Qt Multimedia
> to an existing build tree leaves the emulator GUI-less or mute with no error.
> `./build.sh --fresh` does this for you.

### Build Instructions

```bash
# Option 1: the build script -- configures, builds, and assembles disk.img
./build.sh
./build.sh --fresh              # after installing a new dependency

# Option 2: CMake presets directly (see CMakePresets.json)
cmake --preset dev              # dev | debug | release | no-gui
cmake --build --preset dev
cmake --build --preset dev-disk # assemble disk.img
```

Configuration ends with a summary of what was actually enabled — check it before
filing a bug about missing sound or a missing window:

```
======== 6502-kernel configuration ========
  Qt GUI ......... yes (Qt6)
  SID audio ...... yes
  cc65 ROMs ...... yes
  Tests .......... yes
===========================================
```

Build outputs land in `cmake-build-debug/`: `bin/6502-kernel`, `kernel/*.rom`
(kernel, dos, basic, assembler, forth), `kernel/kernel.map`, and `disk.img`.

### Running

```bash
cd cmake-build-debug/bin && ./6502-kernel
```

**Run it from `bin/`.** The ROMs and disk image are opened by relative path
(`../kernel/*.rom`, `../disk.img`), so launching from anywhere else fails with
"Could not open kernel.rom". A healthy boot lands at the MFC/OS prompt:

```
              MFC 6502  OPERATIONAL
           MFC/OS 1.21   32768 BYTES FREE
]
```

Type `CATALOG` to list the disk, or `HELP` for the command set.

### Verifying the build

```bash
ctest --test-dir cmake-build-debug        # 20 tests: CPU, banking, FAT16, ACIA/XMODEM, SID, RTC, ROM layout
```

### Disk image (`mkdisk`)

The GUI loads `cmake-build-debug/disk.img`. It is assembled from a **diskmap
bundle** — the repo `disk/` directory holds `diskmap.txt` (the disk layout, one
path per line; `DRAWER/NAME` = a one-level drawer) plus the stable content
(`SYSTEM/` config lists, `GAMES/` the Scott Adams games). The rebuildable apps
(`EDIT`/`TERM`/`IRC`, `GAMES/CHESS`, and the `SVAULT/` drawer holding `VAULT.PRG`
and `STORY.TXT`) are staged from their fresh builds. Drawers grow across as many
FAT16 clusters as they need, so a drawer is not capped at one cluster of files.

```bash
ninja disk                                  # (re)assemble cmake-build-debug/disk.img
```
`ninja disk` is explicit — a plain `ninja` never rewrites the disk. After changing
a disk program, rebuild its `.PRG` (`programs/<x>/build.sh`) then `ninja disk`.

The `mkdisk` host tool (`cmake-build-debug/bin/mkdisk`) also works standalone:
```bash
mkdisk create <image> <diskmap.txt>   # build a fresh image from a bundle
mkdisk read   <image> <outdir>        # extract an image into a bundle (+ diskmap.txt)
mkdisk update <image> <diskmap.txt>   # replace/add listed files, keep the rest
```

### Project Structure
```
6502-kernel/
├── src/                   # C++ emulator sources
│   ├── computer/          # CPU, memory, VIC, PIA, ACIA, SID, RTC, block device
│   ├── ui/                # Qt GUI (MainWindow, DisplayWidget)
│   └── kernel/            # 6502 assembly: kernel.asm, basic.asm, dos/, assembler/, forth/
├── include/               # C++ headers
├── programs/              # cc65/asm disk programs: edit, term, irc, micromax, scottfree, vault, common
├── examples/              # Runnable 6502 assembly examples (+ README.md)
├── disk/                  # diskmap.txt + committed disk content (SYSTEM/, GAMES/)
├── vendor/                # Pristine upstream sources we port/derive from
├── tools/                 # Host tools: cmake modules, mkdisk, mkfat16, dat2c
├── docs/                  # Documentation
└── tests/                 # Unit and integration tests (GoogleTest)
```

For detailed development information and project context, see:
- **[CLAUDE.md](CLAUDE.md)** - Development guidelines and architecture documentation  
- **[docs/README.md](docs/README.md)** - Documentation index: program manuals (MONITOR, DOS, BASIC, ASSEMBLER, FORTH, EDIT, TERM, IRC), the architecture reference (ARCHITECTURE.md), and the internals deep-dive (SYSTEM_INTERNALS.md)

## Tips for Effective Use

1. **Start with Help**: Use `?` to see all available commands
2. **Use Command Recall**: The `.` command saves time when refining commands
3. **File Operations**: `L:8000` loads and `S:8000-8FFF` saves; the host shows a file dialog to pick the file
4. **Search Effectively**: Use X: with multiple byte patterns for precise matching
5. **Number Conversion**: Use D: and H: commands to convert between decimal and hex
6. **Program Development**: Load programs with L:, test with G:, save modifications with S:

## Acknowledgments

This project stands on the shoulders of the classic 6502 and free-software
community. With thanks to the authors whose work we have ported, derived from,
or studied:

- **micro-Max** by **H.G. Muller** — the remarkably small but complete chess
  engine (full FIDE rules and move legality) behind `CHESS.PRG`. We compile the
  freely published 1.6 source with cc65 and wrap it in a console front-end for
  MFC-DOS. Pristine upstream sources are kept under `vendor/micromax/`.
  <https://home.hccnet.nl/h.g.muller/max-src2.html>
- **ScottFree** by **Alan Cox** / Swansea University Computer Society — the
  GPL Scott Adams adventure interpreter we port to run the classic Adventure
  International games on MFC-DOS. The interpreter is in `programs/scottfree`;
  the host tool `dat2c` pre-parses a game `.dat` into linkable C tables.
- **Scott Adams** / **Adventure International** — author and publisher of the
  twelve classic text adventures (Adventureland, Pirate Adventure, … The Golden
  Voyage). The shareware `.dat` databases are obtained separately and are not
  redistributed here.
- **kilo** by **Salvatore Sanfilippo (antirez)** — the inspiration for the
  `EDIT` text editor. EDIT is our own implementation (it renders straight to
  screen RAM rather than a terminal), but its structure and the incremental
  search are lifted from kilo's design. <https://github.com/antirez/kilo>
- **EhBASIC** (Enhanced 6502 BASIC) by the late **Lee Davison** — the basis for
  the built-in MFC BASIC interpreter.
- **fig-FORTH for the 6502** by **William F. Ragsdale** and the **FORTH Interest
  Group (FIG)** — the public-domain FIG model behind the `FORTH` module (bank 3).
  We mechanically convert the original assembler listing to ca65 (verified
  byte-identical at its native `$0200` origin), then relocate it into the ROM
  module window and wire its I/O to the kernel. The pristine listing and the
  conversion/verification tooling live under `vendor/fig-forth/`.
- **cc65** — the 6502 C cross-compiler and toolchain used to build the C
  programs (`CHESS.PRG`, the Scott Adams games). <https://cc65.github.io/>
- **XMODEM/CRC for the 65C02** by **Daryl Rictor** (2002) — the serial
  file-transfer routine behind the emulated 6551 ACIA spike. We retarget its
  built-in 6551 driver to our memory-mapped ACIA and relocate it for the host
  test harness; the pristine original is kept under `vendor/xmodem/`.
- **IBM VGA 8×16 CP437 font** — the character generator ROM behind the 80×25
  display (full CP437: box-drawing, blocks, accented, symbols). The raw bitmap
  of the IBM VGA ROM font is public domain (U.S. copyright protects scalable
  outline programs, not bitmap font data). The dump comes from **VileR**'s
  `vga-text-mode-fonts` collection (<https://int10h.org/>), gratefully
  acknowledged; the pristine `VGA8.F16` and the header generator are under
  `vendor/cp437font/`.
- **MOS 6581/8580 SID** — the sound chip our software SID is modeled on (three
  voices, ADSR, multimode filter). The synthesizer is written from scratch from
  public SID documentation (register layout, envelope rates, filter behavior) —
  **no reSID or other GPL code is used**. With thanks to the SID/C64 community
  whose datasheets and reverse-engineering notes made a faithful model possible.
  See `docs/sound_design.md`.
- **The Sunless Vault** (`VAULT.PRG`) — an original text roguelike written from
  scratch for MFC (no ported code). Its integer, turn-based, data-driven engine
  follows the design of the author's own **Dungeon of Yacor**, and its play draws
  inspiration from two classics of the genre — **Telengard** by **Daniel Lawrence**
  and **Sword of Fargoal** by **Jeff McCord** — as design influences only; no code
  or assets from those games are used. See `programs/vault/`.

See `docs/cc65_to_prg.md` for the C-to-`.PRG` build pipeline.

Where we port or adapt third-party code, the original, unmodified source is
preserved under `vendor/` so its authorship and licensing remain clear.

The monitor is designed for both interactive exploration and efficient program development workflows.