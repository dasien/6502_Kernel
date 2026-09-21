# MFC 6502

<img src="assets/mfc6502-128.png" align="right" width="128" alt="MFC 6502 icon">

**MFC** stands for **My First Computer**: a software-defined WDC 65C02 machine with an interactive monitor, a resident FAT16 filesystem, BASIC, FORTH, and a built-in assembler along with a handful of applications and games.

This project started as a continuation of a CPU/assembler/disassembler I wrote in Python.  I wanted to create an actual running environment to enter code directly or load from a file and run.

I also wanted to test the abilities of AI as part of the development and documentation process.

## Project Overview

This project implements a complete 6502-based computer system kernel. The kernel provides low-level system initialization, hardware control, and an interactive monitor program for direct system interaction.

**Key Features:**
- Complete 6502 assembly language kernel 
- Cycle-stepped WDC 65C02 CPU emulator (full CMOS instruction set, validated against the Klaus2m5/amb5l functional, decimal, and 65C02-extended test suites)
- **6502 Monitor**: A complete interactive debugging and programming environment that provides direct control over the computer's memory and execution. 
- DOS shell with a resident FAT16 filesystem and launch-by-name for disk programs
- Disk applications: **EDIT** (full-screen editor), **TERM** (ANSI/telnet terminal with XMODEM), **IRC** (chat client), **GOPHER** (Gopher document browser), plus games (**CHESS**, **KERNEL PANIC**, **VENTURE**, **The Sunless Vault** roguelike, and the Scott Adams adventures) 
- Built-in **MFC BASIC** interpreter (derived from EhBASIC), launched by typing `BASIC` at the DOS prompt (with human-readable `.bas` LOAD/SAVE via a host file dialog)
- Memory manipulation and program execution capabilities
- File I/O operations for loading and saving programs
- Comprehensive search, fill, move, and copy operations
- **Assembly examples** in `examples/` — a dozen runnable 6502 programs (loops, keyboard input, color, hex dump, an 8×8 multiply, a guess-the-number game) with a guide and ABI quick reference (`examples/README.md`)

## Documentation

There is a complete documentation indexed in **[docs/](docs/README.md)** — a manual per program, the
architecture and memory map, and the design notes. 
Docs are split by audience:`UPPERCASE.md` is a manual (how to *use* something), `lowercase.md` is reference
or design (how it *works*).

Start with [DOS.md](docs/DOS.md) to drive the machine, or
[MONITOR.md](docs/MONITOR.md) to poke at memory. 

## Building the system

### Prerequisites

| Dependency | Needed for | If missing |
|---|---|---|
| **CMake** 3.20+, **Ninja** | the build itself | nothing builds |
| **C++20 compiler** (GCC 10+, Clang 10+, MSVC 2019+) | the emulator | nothing builds |
| **cc65** (`ca65`, `ld65`, `cl65`) | assembling every ROM and `.PRG` | configuration fails — the emulator cannot run without its ROMs |
| **Qt6** or **Qt5** (Core, Widgets, Network) | the GUI machine; Network drives TERM/IRC | you get a non-interactive console demo, not the computer |
| **Qt Multimedia** | SID audio | everything works, silently |
| **Python 3** | the two sync tests | these prevent assembler's opcode table and the monitor's kernel ABI from getting out of sync with the executable versions |

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
# Option 1: the build script -- configures, builds every program, assembles disk.img
./build.sh
./build.sh --fresh              # after installing a new dependency

# Option 2: CMake presets directly (see CMakePresets.json)
cmake --preset dev              # dev | debug | release | no-gui
cmake --build --preset dev
cmake --build --preset dev-everything   # programs + ROMs + disk.img
```

Once configured, two targets cover the whole pipeline:

```bash
ninja -C cmake-build-debug everything   # every program, every ROM, the app, the disk
ninja -C cmake-build-debug run          # ...then boot the machine
```

The `.PRG` files are build outputs, not committed artifacts — they are produced into
`cmake-build-debug/programs/<name>/<entry>/` and staged from there, exactly like the
ROMs. That includes the twelve Scott Adams adventures, which are twelve builds of one
engine against twelve committed game databases.

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
(kernel, dos, basic, monitor, forth), `kernel/kernel.map`, and `disk.img`.

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
ctest --test-dir cmake-build-debug        # 32 tests: CPU, banking, FAT16, ACIA/XMODEM, SID, RTC, VIC, ROM layout, disk programs
```

### Disk image

The disk is assembled from `programs/catalog.txt`, which decides what can go on
it and how each program is built:

```bash
ninja disk          # build the programs, then assemble the image
ninja everything    # ...and the ROMs and the app as well
```

Both are explicit — a plain `ninja` never rewrites the disk. It will refuse while
a machine is using the image; quit the emulator first.

See **[docs/disk_image.md](docs/disk_image.md)** for the catalog format, the
`mkdisk` tool, and the image geometry.

### Project Structure
```
6502-kernel/
├── src/                   # C++ emulator sources
│   ├── computer/          # CPU, memory, VIC, PIA, ACIA, SID, RTC, block device
│   ├── ui/                # Qt GUI (MainWindow, DisplayWidget)
│   └── kernel/            # 6502 assembly: kernel.asm, basic.asm, dos/, assembler/, forth/
├── include/               # C++ headers
├── programs/              # cc65/asm disk programs: edit, term, irc, gopher, venture,
│                         #   kpanic, frontier, micromax, scottfree, vault
│                         #   common/ holds mfc.inc and the shared glue library
│                         #   (catalog.txt lists every one and where it lands on disk)
├── examples/              # Runnable 6502 assembly examples (+ README.md)
├── vendor/                # Pristine upstream sources we port/derive from
├── tools/                 # Host tools: cmake modules, mkdisk, mkfat16, mkprg, dat2c
├── docs/                  # Documentation
└── tests/                 # Unit and integration tests (GoogleTest)
```

For detailed development information and project context, see:

- **[docs/README.md](docs/README.md)** - Documentation index: program manuals (MONITOR, DOS, BASIC, FORTH, EDIT, TERM, IRC, GOPHER), the architecture reference (architecture.md), and the internals docs (kernel_internals.md, monitor_internals.md,
  dos_internals.md, basic_internals.md, host_gui.md)


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
  the host tool `tools/dat2c` pre-parses a game `.dat` into linkable C tables.
- **Scott Adams** / **Adventure International** — author and publisher of the
  twelve classic text adventures (Adventureland, Pirate Adventure, … The Golden
  Voyage). The shareware `.dat` databases are freely shareable and are committed
  under `programs/scottfree/dat/`.
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
  and **Sword of Fargoal** by **Jeff McCord** — as design influences. See `programs/vault/`.

See `docs/cc65_to_prg.md` for the C-to-`.PRG` build pipeline.

Where we port or adapt third-party code, the original, unmodified source is
preserved under `vendor/` so its authorship and licensing remain clear.
