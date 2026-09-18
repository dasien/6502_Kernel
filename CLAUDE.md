# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the MFC 6502 ("My First Computer") project: a **software-defined 8-bit computer**
that does not correspond to any machine that ever shipped. Two halves:

- a **6502/65C02 kernel, DOS, monitor and programs** in assembly and cc65 C, and
- a **Qt host emulator** in C++ that provides the CPU and the virtual chipset.

**It is not a Commodore 64 emulator.** The memory map, the chipset and the character set
are all MFC's own: there is no VIC-II, no CIA, no `$00/$01` processor port and no PETSCII.
The display is **80x25 CP437** living behind a VIC *register port*, not a screen-memory
range. The CPU target is the **WDC W65C02S**, so the CMOS additions (`RMB`/`SMB`,
`BBR`/`BBS`, `WAI`, `STP`, valid decimal flags) are all in scope.

## Build System

The project uses CMake and Ninja:

```bash
cmake -G Ninja -DBUILD_TESTS=ON -B cmake-build-debug -S .
ninja -C cmake-build-debug
```

Targets that matter:

| Target | What it does |
|---|---|
| `6502-kernel` | the Qt host emulator (`src/CMakeLists.txt`, from `src/main.cpp`) |
| `disk` | builds every program from `programs/catalog.txt`, then assembles `disk.img` |
| `everything` | every ROM, program, tool and the app |
| `run` | `everything`, then boots it |
| `mkdisk` / `mkfat16` / `mkprg` | host-side tools in `tools/` |

`programs/catalog.txt` is the **single source of truth** for what can go on a disk —
adding a program is one entry there, not three edits in three files. `.PRG` files are
build outputs and are gitignored.

`ninja run` must launch from `cmake-build-debug/bin`, because the machine resolves
`../kernel/kernel.rom` and `../disk.img` relative to its working directory; the target
already handles this.

## Testing

### Test Organization

```
/tests/                     # 29 source-controlled test files, GoogleTest
├── CMakeLists.txt          # one target per area
├── test_cpu_*.cpp          # ALU, interrupts, cycle counts
├── test_vic_*.cpp          # char plane, soft font, fine scroll, sprites
├── test_dos_*.cpp          # block I/O, FAT16, boot-time STARTUP.CFG
├── test_venture.cpp        # a .PRG game, driven through the emulated machine
├── test_kpanic.cpp         # ditto
└── scripts/                # cmake-script validators (ROM, memory layout)

cmake-build-debug/bin/      # built test executables (gitignored)
```

### Running Tests

```bash
ctest --test-dir cmake-build-debug              # all 32 targets
ctest --test-dir cmake-build-debug -R vic_      # one area
ctest --test-dir cmake-build-debug --output-on-failure -R kpanic
```

A full run takes roughly two and a half minutes.

### Test Types

- **Device/unit** — `kernel_unit_tests`, `cpu_*`, `vic_*`, `sid`, `rtc`, `pia_keystate`,
  `block_device_*`, `clock`: exercise one class against its register contract.
- **Integration** — `monitor_integration`, `dos_*`, `fat16_roundtrip`, `acia_xmodem`,
  `term_*`, `irc`, `edit_splash`: drive real 6502 code on the emulated machine.
- **Program tests** — `venture`, `kpanic`: load a `.PRG` and assert on gameplay state.
  Note these exist; the older claim that `.PRG` programs cannot be tested is obsolete.
- **Validators** — `validate_kernel_rom`, `validate_memory_layout`,
  `opcode_table_current`, `kernel_bios_monitor_split`: cmake scripts and drift guards.
  `opcode_table_current` regenerates the 65C02 table and fails if the committed copy is
  stale, so the assembler's table can never silently diverge from the CPU.

## Architecture and Code Organization

### Core Components

1. **`src/kernel/kernel.asm`** — the BIOS in a 4 KB window at `$F000-$FFFF`: reset and
   interrupt vectors, zero-page and stack setup, the jiffy IRQ, and the `$FF00` kernel
   ABI jump table. The monitor is **not** here — it is module bank 4.

2. **`src/kernel/dos/dos.asm`** — MFC/OS at `$8800-$AFFF`: the shell, FAT16 filesystem,
   program loader, and the boot-time `SYSTEM/STARTUP.CFG` runner.

3. **`src/kernel/monitor.asm`** — the machine-language monitor, with the two-pass
   assembler folded in. Module bank 4.

4. **`src/computer/`** — the virtual chipset in C++: `CPU6502`, `Memory`, `PIA`, `VIC`,
   `SID`, `RTC`, `ACIA`, `BlockDevice`, `PowerSwitch`, `TimingCircuit`. Each owns an
   `is*Address()` predicate; `Memory` dispatches through those, which is why adding a
   register range to a chip needs no change to `Memory`.

5. **`src/ui/`** — the Qt front end. `DisplayWidget` renders the character plane,
   sprites and the cursor.

6. **`programs/`** — cc65 C and assembly programs built to `.PRG` (EDIT, TERM, IRC,
   VENTURE, KPANIC, the Sunless Vault, FRONTIER, CHESS...).

7. **`docs/`** — `architecture.md` is the consolidated internals reference (memory map,
   zero page, the `$FF00` ABI, bank switching); `board.md` is the chipset as a
   single-board computer with the authoritative I/O decode table.

### Memory Architecture

The memory map is MFC's own; it is not a Commodore 64 layout. `docs/architecture.md`
is authoritative — this is the shape of it:

| Range | Contents |
|---|---|
| `$0000-$00FF` | Zero page — kernel/monitor/DOS workspace. **Not** a processor port |
| `$0100-$01FF` | System stack, growing down from `$01FF` |
| `$0200-$03FF` | Kernel data structures and I/O buffers |
| `$0400-$07FF` | `T:`/`Z:` page snapshot (`$0400`) + the assembler's identifier buffers and symbol table (`$0500-$07FF`) |
| `$0800-$87FF` | **User RAM** — where `.PRG` programs load and run |
| `$8800-$AFFF` | DOS ROM |
| `$B000-$EFFF` | Bank-switched module window, 16 KB |
| `$F000-$FFFF` | Kernel BIOS ROM |
| `$FE00-$FECA` | Memory-mapped I/O (inside the kernel window, reserved by the `IORESV` linker segment) |
| `$FFFA-$FFFF` | NMI / RESET / IRQ vectors |

**The screen is not in the map.** The 80x25 CP437 character plane and its attributes
live behind the VIC register port and are reached by writing a cell index then streaming
glyphs; there is no screen-memory range to poke.

cc65 `.cfg` files **must** put the C stack at `$8700`, since user RAM ends at `$87FF`.

## 6502 Monitor Program

The kernel includes a complete interactive monitor program for debugging and programming. The monitor provides a command-line interface for memory manipulation, program execution, and system inspection.

### Monitor Memory Layout ($0200-$0261)

The monitor uses system RAM starting at $0200 for variables and buffers:

- **$0200-$024F**: Command input buffer (80 bytes)
- **$0250-$0251**: Command buffer pointer and length
- **$0252-$0258**: Monitor state variables (mode, addresses)
- **$0259-$025E**: Parser variables and temporary storage
- **$0260-$0261**: Message pointer for optimized string printing
- **$0262-$027E**: Extended monitor variables for F:, M:, X: commands

### Monitor Commands

The monitor provides a comprehensive set of commands for memory operations, program execution, and system debugging:

#### Memory Operations
- **R:xxxx[-yyyy]** - Read and display memory contents (range optional)
- **W:xxxx [data]** - Write mode: Write hex bytes to memory at address xxxx
- **F:xxxx-yyyy,zz** - Fill memory range with specified byte value
- **M:xxxx-yyyy,zzzz,b** - Move/Copy memory (b=0:copy, b=1:move with source clear)
- **X:xxxx-yyyy,pattern** - Search memory range for hex byte pattern (up to 16 bytes)

#### Program Execution
- **G:xxxx** - Go/Run mode: Execute program starting at address xxxx
- **L:xxxx** - Load program from file at specified address
- **S:xxxx-yyyy** - Save memory range to file

#### Number Conversion
- **D:nnnnn** - Convert decimal (0-65535) to hexadecimal with '$' prefix
- **H:xxxx** - Convert hexadecimal (0000-FFFF) to decimal with '#' prefix

#### System Information
- **C:** - Clear screen
- **T:** - Display stack memory ($0100-$01FF) with paging
- **Z:** - Display zero page memory ($0000-$00FF) with paging
- **?** - Display help with all available commands (no colon required)

#### Navigation
- **ESC** - Exit current mode and return to command prompt

### Monitor Features

#### Command Line Interface
- 80-character input buffer with full editing support
- Backspace/delete character support
- Command history and error handling
- Syntax validation and error reporting

#### Memory Operations
- Hex address parsing (supports both uppercase and lowercase)
- Range operations (R:8000-80FF displays memory range)
- Sequential write operations with old/new value display
- 8-byte-per-line formatted output for readability

#### String Optimization System
The monitor uses an advanced null-terminated string system for efficient message display:

```assembly
; Message data stored as null-terminated strings
MSG_HELP_HEADER:     .BYTE "6502 MONITOR COMMANDS", 0
MSG_HELP_WRITE:      .BYTE "W:XXXX WRITE", 0

; Generic print routine using indirect indexed addressing
PRINT_MESSAGE:
    LDY #$00                    ; Initialize string index
PRINT_MSG_LOOP:
    LDA (MON_MSG_PTR),Y         ; Load character using indirect indexed
    BEQ PRINT_MSG_DONE          ; If null terminator, done
    JSR PRINT_CHAR              ; Print the character
    INY                         ; Move to next character
    BNE PRINT_MSG_LOOP          ; Continue (strings < 256 chars)
PRINT_MSG_DONE:
    RTS

; Optimized function calls (8 bytes each)
PRINT_HELP_HEADER:
    LDA #<MSG_HELP_HEADER       ; Load low byte of message address
    STA MON_MSG_PTR             ; Store in message pointer
    LDA #>MSG_HELP_HEADER       ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message
    RTS
```

This optimization achieves ~80-88% code size reduction compared to individual character loading.

## Enhanced Monitor Commands (F:, M:, X:)

The kernel includes three powerful new commands for advanced memory operations:

### F: Fill Memory Command

**Syntax**: `F:start-end,value`

Fills a memory range with a specified byte value.

**Examples**:
- `F:8000-8FFF,00` - Fill $8000-$8FFF with zeros
- `F:C000-C0FF,A9` - Fill $C000-$C0FF with $A9 (LDA immediate opcode)

**Features**:
- Supports full 64KB address range
- Efficient forward-fill algorithm
- Progress indication for large ranges
- Comprehensive error checking

### M: Move/Copy Memory Command  

**Syntax**: `M:start-end,dest,mode`

Copies or moves a block of memory from source to destination.

**Parameters**:
- `start-end`: Source memory range
- `dest`: Destination start address  
- `mode`: `0` = Copy (preserve source), `1` = Move (clear source after copy)

**Examples**:
- `M:8000-8FFF,9000,0` - Copy $8000-$8FFF to $9000-$9FFF 
- `M:E000-EFFF,2000,1` - Move ROM to RAM, clear original location
- `M:1000-10FF,1001,0` - Shift memory block up by one byte

**Features**:
- Handles overlapping memory regions correctly
- Forward/backward copy direction optimization
- Source memory clearing for move operations
- Byte count reporting (`COPIED XXXX BYTES` / `MOVED XXXX BYTES`)

### X: Memory Search Command

**Syntax**: `X:start-end,pattern`

Searches memory for a specific byte pattern.

**Parameters**:
- `start-end`: Memory range to search
- `pattern`: 1-16 hex bytes separated by spaces

**Examples**:
- `X:8000-FFFF,4C` - Search for JMP absolute opcode
- `X:8000-8FFF,A9 20 4C` - Search for "LDA #$20 / JMP" sequence
- `X:C000-CFFF,00 00 00` - Find blocks of three zero bytes

**Features**:
- Multi-byte pattern matching (up to 16 bytes)
- Paged output with ESC abort capability
- Respects exact range boundaries  
- Preserves current address pointer
- Address-only output format for efficiency

### Key Assembly Patterns

The kernel code follows these patterns:
- Hardware initialization loops for clearing chip registers
- Memory banking through the `MODULE_BANK` register at `$FE23`
- Interrupt vector setup at $FFFA-$FFFF
- Zero page cleared wholesale at reset — there are no processor-port bytes to preserve
- Screen clearing via a VIC **command** (there is no screen or colour memory to fill)
- Null-terminated strings printed through one indirect-indexed routine (see below) —
  the house style for any new message
- Reference material: https://www.masswerk.at/6502/6502_instruction_set.html and
  http://www.6502.org/documents

## Development Guidelines

### Assembly Code Standards
- Use meaningful labels and constants (defined at top of file)
- Include detailed comments explaining hardware interactions
- Follow the existing memory map allocation strictly
- Preserve critical zero page locations during initialization

### Memory Banking Considerations
- **`MODULE_BANK` at `$FE23`** selects which 16 KB module is visible in the
  `$B000-$EFFF` window. Write `n` to map bank `n`.
- Bank 0 is plain scratch RAM and is what boots. BASIC is 1, FORTH is 3, MONITOR
  (assembler included) is 4. Bank 2 is free.
- Banking affects **only** `$B000-$EFFF`. The DOS ROM, kernel and I/O page are always
  visible; nothing can bank them out.
- `Memory` answers `$FE23` itself, before any peripheral is consulted — it is a decoder
  register, not a chip.
- Always restore the previous bank after a temporary change.

### Hardware Initialization Sequence
1. `CLD` / `SEI` — clear decimal mode, disable interrupts
2. Initialize the stack pointer to `STACK_TOP`
3. Point `MODULE_BANK` at bank 0, so the module window boots as clean scratch RAM
4. Clear zero page
5. Clear the screen (a VIC command, not a memory fill) and the module window RAM
6. Initialize the devices and install the interrupt vectors
7. Enable the jiffy IRQ and `CLI`
8. Enter **DOS**, which runs `SYSTEM/STARTUP.CFG` and then signs on

There is no VIC-II, SID-CIA or keyboard-CIA init step: the PIA supplies the keyboard,
the ACIA the serial port, and the SID needs no reset sequence.

### Monitor Development Guidelines
- Always use the null-terminated string system for new messages
- Allocate monitor variables in the $0200-$03FF range per the memory map
- Implement proper hex parsing for addresses and data input
- Provide clear error messages and syntax validation (`ERROR?`, `RANGE?`, `VALUE?`)
- Follow the 8-byte function pattern for message printing optimization
- Use the existing PRINT_CHAR, PRINT_HEX_BYTE, and I/O routines
- Implement paging support for commands that generate multiple output lines
- Preserve `MON_CURR_ADDR` during parsing operations to maintain prompt consistency
- Use dedicated zero page variables to avoid conflicts with system operations
- Test all monitor commands thoroughly in both emulation and hardware

## Critical Memory Locations

Refer to `docs/architecture.md` for complete details, but key locations include:
- **`$FE23`**: `MODULE_BANK` — the bank-select register (there is no `$00/$01` port)
- **`$01FF`**: Initial stack pointer location
- **$B000-$EFFF**: Bank-switched module window, 16 KB (BASIC 1, FORTH 3, MONITOR 4 with the assembler built in; bank 2 free)
- **`$FE00-$FECA`**: Memory-mapped I/O — PIA (incl. the live held-key port at `$FE0F`),
  `MODULE_BANK`, BlockDevice, ACIA, VIC register port, SID, RTC, PowerSwitch, VIC
  soft-font port and VIC sprite block. The 80x25 screen and its colours live behind the
  VIC port, not in the 64K map. **First free byte: `$FECB`** (53 left).
  `docs/board.md` has the authoritative per-chip decode table
- **$F000-$FFFF**: Kernel BIOS ROM (the monitor is module bank 4, not here)
- **$FFFA-$FFFF**: Interrupt vectors (NMI, RESET, IRQ)
