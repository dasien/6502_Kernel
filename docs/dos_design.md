# MFC-DOS — BIOS + DOS + Filesystem Design

**Status:** in progress (the major arc after v3.2). Phase 1 (block device) is built
and tested; phases 2-5 remain. *("MFC-DOS" and the boot prompt are provisional — see
[Identity](#identity).)*

The pivot: the machine **boots into a DOS** — a command shell with a filesystem,
like an Apple II / TRS-80 / Kaypro (CP/M). BASIC, the assembler/disassembler, the
monitor, and an editor become **programs you launch by name** from the DOS, not the
front door. This turns the project from "a monitor with ROM modules" into a small
**6502 disk operating system**.

References that shape this: cpm65 (a 6502 OS with BIOS/BDOS/CCP layering, disk-as-a-
host-file, relocatable programs), the X16 emulator's `sdcard.c` (host `.img` file,
512-byte blocks), and mike42's 6502 SD reader (the block device). See
`research_references.md`.

---

## The model: a resident OS + apps

```
Apps        ── BASIC, monitor, assembler/disassembler, editor, games
              (ROM banks in the $B000-$DFFF window, or program files on disk)
─────────────────────────────────────────────────────────────────────────
Resident OS ── DOS shell  (prompt, commands, launch-by-name)        [DOS ROM]
            ── Filesystem (FAT16 over the block device)             [DOS ROM]
            ── BIOS       (boot/init, I/O, the $FF00 ABI, banking)  [kernel ROM]
            ── Block device driver (512-byte sectors)               [BIOS]
─────────────────────────────────────────────────────────────────────────
Hardware    ── host disk.img (a real FAT16 volume the Mac can mount too)
```

This is the CP/M BIOS / BDOS / CCP shape: **BIOS** = machine + I/O, **BDOS** = the
filesystem, **CCP** = the DOS command shell.

## Kernel refactor: BIOS vs. monitor

Today's `kernel.asm` is really two things glued together. We separate them:

**BIOS — the resident foundation (stays in the kernel ROM, never banked):**
- `RESET` + init (decimal/interrupt/stack, ZP clear, screen clear, window clear,
  `MODULE_BANK` init), IRQ/NMI handlers + timer.
- Screen/keyboard I/O: `PRINT_CHAR`, `PRINT_MESSAGE`, `PRINT_NEWLINE`,
  `SCROLL_SCREEN`, `CLEAR_SCREEN`, cursor; `GET_KEYSTROKE`, `READ_COMMAND_LINE`,
  hex parse, `PRINT_HEX_BYTE`.
- The **`$FF00` jump-table ABI** and the **bank mechanism** (`MODULE_DIR`, bank
  launch, `$FF12` return).
- **New:** block-device driver; **FS ABI** entries (the FAT16 code lives in the DOS
  ROM, reached through these).
- Vectors, RNG.

**Monitor — a debugger *tool* (no longer the front door):**
- `MONITOR_LOOP`, prompt, dispatch, and the single-letter commands `R`/`W`/`F`/`M`/
  `X`/`G`/`T`/`Z`/`C`, `D`/`H`, `?`, `.`.
- Launched from the DOS by `MON`; exits back to the DOS. (`L`/`S` and the old `B:`
  bank menu move to the DOS.)
- Stays in the kernel ROM for now; can be relocated to a bank later (see
  [Memory](#memory-budget--creating-space)).

## Boot flow

`RESET` → BIOS init (as today) → **`JMP` to the DOS shell entry** (instead of
`MONITOR_MAIN`). The user lands at the DOS prompt.

## Storage backing + block device (emulator)

A single host file, `disk.img` (a real FAT16 volume), opened `r+b`. The 6502 talks
to it through a **simple memory-mapped block-device register interface** — not SPI/SD
protocol emulation (the X16 emulates SPI only because it's real hardware with a
physical SD port; we're software-defined, so SPI ceremony buys nothing).

Registers in the I/O page, after `MODULE_BANK` (`$FE23`):

| Addr | Name | Purpose |
|------|------|---------|
| `$FE24-$FE25` | `BLK_LBA` | 16-bit sector number (→ 32 MB image; widen to 3 bytes later if needed) |
| `$FE26` | `BLK_CMD` | write `1` = read sector→buffer, `2` = write buffer→sector |
| `$FE27` | `BLK_STATUS` | `0` = ready, non-zero = busy/error/no-disk |
| `$FE28` | `BLK_DATA` | 512-byte data port, auto-incrementing index |

Read = set `BLK_LBA`, `BLK_CMD=1`, read `BLK_DATA` ×512. Write = write `BLK_DATA`
×512, set `BLK_LBA`, `BLK_CMD=2`. Emulator side: a small block-device class (open
`disk.img`, a 512-byte buffer, four registers wired through `Memory`). ~50-80 lines.

## Filesystem (resident FAT16, in the DOS ROM)

**FAT16**, chosen for **host interoperability**: `disk.img` is a normal FAT16 volume,
so the Mac can mount it and exchange the same files the machine sees. Simpler than
FAT32; a few-MB image is plenty. (FAT32 / CP/M / custom formats were considered and
set aside — FAT16 is the sweet spot for size and Mac-mountability.)

Driver scope: mount (boot sector/BPB → FAT, root dir, data region); read (find by
8.3 name, follow the cluster chain); write (allocate clusters, update FAT + dir
entry); directory enumeration. **Starting simplifications:** root directory only,
one open file at a time, 8.3 names, FAT16 only — enough for the whole workflow.

**FS ABI** (via the `$FF00` table, byte-stream like today's LOAD/SAVE):
`FS_OPEN(name, mode)`, `FS_GETB` (carry=EOF), `FS_PUTB(byte)`, `FS_CLOSE`,
`FS_DIR_FIRST`/`FS_DIR_NEXT`. Filenames are supplied by the 6502 (the FS finds the
file, no host dialog).

## The DOS shell (CCP)

Boots to a prompt, reads a line (reuses the BIOS `READ_COMMAND_LINE`), parses a
**verb + args**, and dispatches. Verbs (our vocabulary — full words, optional short
aliases):

| Command | Does |
|---------|------|
| `CATALOG` (`CAT`) | list files (name, size) + free space |
| `LOAD name[,addr]` | load a file into memory (addr from the file's header if omitted) |
| `SAVE name,start-end` | save a memory range to a file (writes the load-address header) |
| `ERASE name` | delete a file |
| `RENAME old,new` | rename a file |
| `TYPE name` | display a text file |

### Launch by name (unified — ROMs *and* programs)

There is **no `BANKS` menu and no `RUN` verb.** You type a **name** at the DOS prompt
and the DOS resolves it, in order:

1. **Built-in DOS command** (`CATALOG`, `ERASE`, …).
2. **Built-in ROM program** — `BASIC`, `ASM`, `MON`, `EDIT` (from the `MODULE_DIR`
   registry) → map the bank, jump to its entry.
3. **Program file on disk** → load it into RAM and execute.

So "ROMs" vs "programs" is just an implementation detail (ROM = fast, always present,
not editable; file = on disk, can be assembled/saved). From the user's seat
everything is *a named program you run*. The old `B:` bank menu becomes "type the
name"; `MODULE_DIR` remains as the registry the DOS consults. A `HELP`/built-ins
listing surfaces the resident programs for discoverability; `CATALOG` lists disk
files.

### Program file format

A runnable program file begins with a **2-byte little-endian load address** (exactly
like a C64 `.PRG`). To launch a file program: read the 2-byte header, load the body
there, `JMP` to the load address (entry = start). This dovetails with the assembler:
`.ORG $0800` → the saved program carries `$0800` as its header → launching it puts it
back at `$0800`. Closes the loop: **assemble → SAVE (with header) → run by name.**
(Relocatable, cpm65-style position-independent loading is a possible future upgrade.)

### `RUN` vs the monitor's `G:`

Different layers, both kept: the monitor's **`G:addr`** *jumps to an address* where
code already sits (low-level debug "go"); **launch-by-name** *loads a program then
executes* (≈ load + go). `RUN` is dropped — typing the name is the launch.

## Memory budget — creating space

The resident OS (BIOS + monitor + FS + DOS) won't fit the 8 KB kernel ROM, and the
kernel can't grow in place (the 12 KB BASIC window sits below `$E000`; vectors pin the
top). So we **add a second always-mapped ROM** — sanctioned ("don't be afraid to
create space"):

- **`DOS ROM` at `$9000-$AFFF` (8 KB), always mapped**, holding the **FAT16 FS + DOS
  shell**. Routed by the emulator like the kernel/BASIC ROMs (but always mapped, not
  banked). Boot: BIOS init → `JMP` DOS entry; FS ABI entries in `$FF00` jump into it.
- **User RAM becomes `$0800-$8FFF` (~34 KB)** — still ample; the assembler's source
  buffer / symbol table relocate below `$9000`.

This keeps the **kernel ROM = BIOS + monitor** untouched, and gives FS+DOS a roomy
home. (An alternative is the bigger coordinated memory-map overhaul; the second-ROM
approach is lower-risk and is the plan.)

Later optional tidy-up: relocate the **monitor to a bank** (porting it to the `$FF00`
ABI), leaving the kernel ROM as a lean pure-BIOS — not required for function.

## Identity

Provisional, to confirm: the OS is **MFC-DOS**; boot shows a sign-on banner and a
prompt. Prompt candidates: Apple-style `]`, CP/M-style `A>` (odd with no drive
letters), or a plain `MFC>`/`>`. **TBD.**

## Phased build plan

1. **Block device** — emulator `disk.img` + the `$FE24` registers + `Memory` routing;
   a 6502 sector read/write smoke test. (Small, foundational.) **— DONE.** `BlockDevice`
   (`include/computer/BlockDevice.h`, `src/computer/BlockDevice.cpp`) backs a host
   `disk.img` (lazily opened, auto-created, grows on write, reads zeros past EOF);
   `Memory` routes `$FE24-$FE28` to it; `Computer6502` owns it (default image
   `../disk.img`). Covered by `tests/test_block_device.cpp` (`block_device_unit_tests`).
2. **FAT16 mount + read** — mount, `CATALOG`, read a file by name; the FS ABI. (At
   this point, mount-on-Mac authoring already works.)
3. **FAT16 write** — create / `ERASE` / `SAVE`; full round-trip on the machine.
4. **DOS ROM + shell as boot target** — the pivot: add the `$9000-$AFFF` DOS ROM,
   boot into the DOS prompt, the command set above, launch-by-name (command → ROM
   module → file), program-file loader. `MON`/`BASIC`/`ASM` launch their banks; the
   monitor is entered as a tool and returns to DOS.
5. **Editor** (module, bank) — full-screen, generic; edit/save FS files → full
   in-machine self-hosting (edit → assemble → run, all at the DOS).
6. *(Later/optional)* relocate the monitor to a bank; kernel ROM becomes a lean BIOS.

The FS phases (1–3) are foundational and unchanged regardless of the DOS framing; the
pivot mainly reshapes phase 4 (a DOS shell, not file-verbs bolted onto the monitor)
and flips the boot target.

## Settled decisions
- Boot into the **DOS**; monitor becomes a launchable tool (`MON`).
- **BIOS / DOS / monitor** three-way split; BIOS always resident, never banked.
- Storage = host `disk.img`, 512-byte sectors, simple block-device registers (not SPI).
- Filesystem = **FAT16** (host-mountable).
- DOS commands: **`CATALOG`, `LOAD`, `SAVE`, `ERASE`, `RENAME`, `TYPE`**.
- **Unified launch-by-name** (command → ROM module → disk file); no `BANKS` menu, no
  `RUN` verb.
- Program file format = **2-byte load-address header** (`.PRG`-style).
- Space = add an always-mapped **DOS ROM at `$9000-$AFFF`**; user RAM → `$0800-$8FFF`.
- Editor = full-screen, generic, a bank.

## Open questions
- OS name + boot prompt (Identity above).
- Image creation/format — a host tool (`diskutil`) vs an in-machine `FORMAT` later.
- `BLK_LBA` width (16-bit/32 MB vs wider).
- Subdirectories, multiple open files, long names — deferred.
- Editor cursor addressing — a small `K_SET_CURSOR` ABI entry vs direct screen-RAM
  writes (decided at phase 5).
- Whether/when to do the monitor-to-bank relocation.
