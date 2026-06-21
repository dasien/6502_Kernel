# MFC-DOS — BIOS + DOS + Filesystem Design

**Status:** in progress (the major arc after v3.2). Phase 1 (block device) done;
**Phase 2 (FAT16 read) COMPLETE** — 2.1 memory-map shift, 2.2 block primitives +
`$AF00` DOS ABI table, 2.3 FAT16 read (mount + directory + cluster-chain file read),
2.4 the `mkfat16` tool + a temporary `@` catalog/type monitor command (kernel v3.3).
**Phase 3 (FAT16 write) COMPLETE** (3a write engine + 3b FS_DELETE/`@` save-erase).
**Phase 4 (DOS shell) underway** — 4.1 boot pivot + 4.2 file verbs done: the machine
boots into the **MFC/OS** shell (`]` prompt) with `CATALOG`/`TYPE`/`SAVE`/`LOAD`/`ERASE`/
`RENAME`/`IMPORT`/`EXPORT`/`MON`/`HELP`; the monitor is launched by `MON`, exited with
`Q`, and is a pure debugger (the `@` preview and the host `L:`/`S:` are retired - host
transfer is now DOS `IMPORT`/`EXPORT`). **Phase 4 COMPLETE:** launch-by-name runs
`BASIC`/`ASM` and disk `.PRG` programs (`&` forces the disk version), each returning to
`]`; the `B:` bank menu is retired. Kernel v3.8. **The assemble → SAVE → run loop is
closed.** Next: Phase 5 (the editor). Identity: OS = **MFC/OS**, `]` prompt.

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
| `IMPORT name` | copy a host (macOS) file, via the file picker, into a FAT16 file |
| `EXPORT name` | copy a FAT16 file out to a host file, via the save dialog |

### Launch by name (unified — ROMs *and* programs)

There is **no `BANKS` menu and no `RUN` verb.** You type a **name** at the DOS prompt
and the DOS resolves it, in order:

1. **Built-in DOS command** (`CATALOG`, `ERASE`, …; includes `MON`).
2. **Built-in ROM program** — `BASIC`, `ASM` (from the `MODULE_DIR` registry) → map
   the bank, jump to its entry; returns to the DOS on exit.
3. **Program file on disk** → load it into RAM and execute.

**Settled (2026-06):** the assembler's launch name is **`ASM`** (its `MODULE_DIR`
name). **ROM-module-first** resolution (a disk file of the same name is shadowed),
with an **override prefix `&`**: typing `&NAME` skips the module check and runs the
disk file `NAME` (so a disk program can intentionally replace a built-in). A launched
program returns to the DOS by a plain **`RTS`** — the DOS runs it as a subroutine
(pushes a `DOS_WARM` return), so a normal `RTS` lands back at the `]` prompt; a
program that takes over the machine just needs a reset.

So "ROMs" vs "programs" is just an implementation detail (ROM = fast, always present,
not editable; file = on disk, can be assembled/saved). The old `B:` bank menu is
retired; `MODULE_DIR` remains the registry the DOS consults (via a kernel ABI). A
`HELP`/built-ins listing surfaces the resident programs; `CATALOG` lists disk files.

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

**Settled (2026-06-14):** the OS is **MFC/OS**; boot shows a sign-on banner and a
an **`]`** prompt. The monitor is launched by `MON` and exited with **`Q`** (back to
the DOS prompt). (The dos.rom signature string stays "MFC-DOS" as an internal marker.)

## Phased build plan

1. **Block device** — emulator `disk.img` + the `$FE24` registers + `Memory` routing;
   a 6502 sector read/write smoke test. (Small, foundational.) **— DONE.** `BlockDevice`
   (`include/computer/BlockDevice.h`, `src/computer/BlockDevice.cpp`) backs a host
   `disk.img` (lazily opened, auto-created, grows on write, reads zeros past EOF);
   `Memory` routes `$FE24-$FE28` to it; `Computer6502` owns it (default image
   `../disk.img`). Covered by `tests/test_block_device.cpp` (`block_device_unit_tests`).
2. **FAT16 mount + read** — mount, `CATALOG`, read a file by name; the FS ABI. (At
   this point, mount-on-Mac authoring already works.) Sub-steps:
   - **2.1 Memory-map shift — DONE.** The always-mapped **DOS ROM at `$9000-$AFFF`**
     was pulled forward to here (rather than phase 4) so the FS has its permanent
     home from the start. Emulator routes `$9000-$AFFF` to a `dos.rom` image (writes
     ignored; falls through to RAM if absent); user RAM is now `$0800-$8FFF`; BASIC
     `Ram_top → $9000`; the assembler's buffers moved to `$8000` (source) / `$7E00`
     (symbols). Stub `src/kernel/dos/dos.asm` (signature only) builds `dos.rom`.
     Covered by DOS-ROM cases in `tests/test_memory_banking.cpp`.
   - **2.2 — DONE.** Block-device equates + 512-byte sector read/write primitives
     (`BLK_READ_SECTOR`/`BLK_WRITE_SECTOR`, the 6502 side of `$FE24-$FE28`) + FS ABI
     stubs, all in `src/kernel/dos/dos.asm`. Stable entry points live in a **DOS ABI
     jump table at `$AF00`** (mirrors the kernel `$FF00` table): `DOS_COLD`, `FS_OPEN`/
     `FS_GETB`/`FS_PUTB`/`FS_CLOSE`/`FS_DIR_FIRST`/`FS_DIR_NEXT` (stubs, carry=error),
     `BLK_READ_SECTOR` (`$AF15`), `BLK_WRITE_SECTOR` (`$AF18`). DOS zero page uses the
     free `$3A-$5A` gap (`BLK_BUF_PTR=$3A`). Covered by `tests/test_dos_blockio.cpp`
     (`dos_blockio_tests`) which runs the real `dos.rom` routines.
   - **2.3 — DONE.** FAT16 read driver in `src/kernel/dos/dos.asm`, validated by
     `tests/test_dos_fat16.cpp` against host-built images (`tests/support/fat16_image.h`).
     - *2.3a:* auto-mount (parse boot-sector BPB → sectors/cluster, FAT/root/data
       start, root entry count, cached in the `$0300` DOS state block) +
       `FS_DIR_FIRST`/`FS_DIR_NEXT` (root-dir walk, skipping deleted/LFN/volume
       entries, leaving the 32-byte entry in `DOS_ENTRY`).
     - *2.3b:* `FS_OPEN` (parse 8.3 name, scan dir, arm the open-file cursor),
       `FS_GETB` (stream bytes across sector boundaries, following the FAT16
       cluster chain at cluster boundaries; carry=EOF), `FS_CLOSE`. Reads stream
       through the block device's sector buffer (no 512B RAM buffer); FAT lookups
       and dir scans use bounded skip-reads. `FS_PUTB` remains a stub (phase 3).
   - **2.4 — DONE.** Interactive surface + tooling:
     - `tools/mkfat16` creates a FAT16 `disk.img` (sample files by default, or host
       files added under derived 8.3 names), reusing the shared image builder. It
       sizes the volume as a genuine FAT16 (>= 4085 clusters), so macOS mounts it
       read/write and exchanges files with the machine. A `sample_disk` CMake
       target writes `<build>/disk.img`.
     - Temporary monitor command **`@`** (kernel.asm, v3.3): `@` catalogs the disk
       (names + sizes), `@NAME` types a file. It calls the DOS ABI at `$AF..`
       directly (the DOS ROM is always mapped), so no kernel `$FF00` change was
       needed; phase 4 replaces `@` with the real DOS shell.
     - Covered by `monitor_integration` (catalog, type, missing-file) against a
       mounted FAT16 image.
3. **FAT16 write** — create / `ERASE` / `SAVE`; full round-trip on the machine.
   - **3a — DONE.** The write engine in `dos.asm`: cluster allocation (scan the FAT
     for a free entry, mark EOC), FAT-entry **read-modify-write** (read the sector,
     skip to the entry, overwrite 2 bytes in the buffered sector, flush — no RAM
     sector buffer), free-chain, directory-slot find (reuse same-name + free old
     chain, else append), and `FS_OPEN(write)` / `FS_PUTB` / `FS_CLOSE` (stream
     bytes, allocate + chain clusters across boundaries, pad + flush the final
     sector, finalize the dir entry). Single + multi-cluster; truncate-on-reopen.
     `FS_OPEN` mode is passed in Y (0 = read, 1 = write). A C++ FAT16 parser
     (`Fat16ImageReader`) independently validates the on-disk format; covered by
     write/round-trip cases in `tests/test_dos_fat16.cpp`. (Single FAT copy;
     deleted-slot reclaim deferred.)
   - **3b — DONE.** `FS_DELETE` (scan, free the cluster chain, mark the directory
     entry `$E5`) at DOS ABI `$AF1B`. The temporary `@` preview gains write
     commands (kernel v3.4): `@-NAME` erases, and `@SSSS-EEEE=NAME` saves a memory
     range to a file (`FS_OPEN`-write + `FS_PUTB` loop + `FS_CLOSE`). Covered by
     erase/free-and-reuse cases in `dos_fat16_tests` and save/erase round-trip in
     `monitor_integration`. (Full machine round-trip: poke memory -> `@..=F` save
     -> `@` catalog -> `@F` type back.)
4. **DOS shell as boot target** — the pivot: fill the (already-present) `$9000-$AFFF`
   DOS ROM with the command shell, boot into the DOS prompt, the command set above,
   launch-by-name (command → ROM module → file), program-file loader. `MON`/`BASIC`/
   `ASM` launch their banks; the monitor is entered as a tool and returns to DOS.
   - **4.1 — DONE.** The boot pivot. RESET now `JMP DOS_COLD`; the machine boots into
     the **MFC/OS** shell (banner + `]` prompt) in `dos.asm`: read a line (BIOS
     `READ_COMMAND_LINE`), match a verb, dispatch. Verbs: `HELP`, `MON` (launches the
     monitor via the new `K_MON_ENTRY` `$FF1E` BIOS entry), `CATALOG`/`CAT`, `TYPE
     NAME`. The monitor gains `Q` (quit → `DOS_WARM` `$AF1E`). Kernel v3.5. The `@`
     preview + `B:` menu remain reachable through `MON` (retired in 4.2/4.3).
   - **4.2a — DONE.** DOS file verbs in the shell: `SAVE name,SSSS-EEEE` (writes the
     `.PRG` 2-byte load-address header then the range), `LOAD name[,AAAA]` (loads to
     the header's address, or an override), `ERASE name`, `RENAME old,new`. New
     `FS_RENAME` (DOS ABI `$AF21`) + a shared `_DOS_DIR_FIND_EXISTING` helper.
     Kernel v3.5.1 (MONITOR_MAIN resets its display state on launch). Covered by
     DOS round-trip cases in `monitor_integration` and `FS_RENAME` cases in
     `dos_fat16_tests`.
   - **4.2c — DONE.** Host bridge moved into the DOS as `IMPORT name` / `EXPORT name`
     (host file picker <-> a FAT16 file, reusing the PIA byte-stream that `L:`/`S:`
     used, now bridged to the FS via `FS_PUTB`/`FS_GETB`). The monitor's `L:`/`S:`
     host load/save are retired: removed from `CMD_INDEX_MAP` + help, and their
     handlers (`PARSE_CMD_LOAD`/`SAVE_CHECK`, `CMD_LOAD_FILE`, `CMD_SAVE_FILE`)
     excised - freeing ~180 bytes of kernel ROM (the two vacated jump-table slots
     map to a no-op). Kernel v3.7.
   - **4.2b — DONE.** Retired the temporary `@` preview: removed its dispatch,
     `CMD_CATALOG`/`CMD_TYPE`/`CMD_SAVE`/`CMD_ERASE` routines, the `FS_*`/`DOS_DIR_ENTRY`
     equates, and the `MSG_DOS_*` strings from `kernel.asm` (~550 bytes freed). The
     monitor is a pure debugger again; the DOS shell owns the file verbs. Kernel v3.6.
     The obsolete monitor `@` tests were removed (coverage is the DOS-level tests).
   - **4.3** — launch-by-name. Decisions: `ASM` launch name, ROM-module-first with
     `&NAME` override to force a disk program, programs return via `RTS`.
     - *4.3a — DONE.* `RETURN_FROM_MODULE` (`$FF12`) now re-enters `DOS_WARM` (monitor-
       state save/restore dropped); new `K_LAUNCH_BY_NAME` ABI (`$FF21`) scans
       `MODULE_DIR` and `BANK_LAUNCH`es a match (assembler's name is `ASM`); the DOS
       resolves an unmatched verb to a module. `BASIC`/`ASM` run from `]` and return
       to `]`. The monitor `B:` bank menu is excised (`CMD_BANK_MENU`/`PARSE_CMD_BASIC`
       removed). Kernel v3.8.
     - *4.3b — DONE.* Disk `.PRG` launch (`_DOS_RUN_FILE`): `FS_OPEN` the name, read
       the 2-byte load-address header, load the body there, then run it as a
       subroutine — clean stack with a `DOS_WARM` return pushed, so the program's
       `RTS` returns to `]`. A leading `&` forces this disk path over a same-named
       module. Unknown name → `COMMAND NOT FOUND`. **Closes the loop:** assemble in
       `ASM` → `SAVE NAME,start-end` → type `NAME` to run it.
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
- Image creation/format — the `tools/mkfat16` host tool creates a genuine FAT16
  image (>= 4085 clusters; ~2 MB), confirmed mountable read/write by macOS
  (`fsck_msdos` clean, `hdiutil attach` exchanges files both ways). An in-machine
  `FORMAT` is still a possible later addition.
- `BLK_LBA` width (16-bit/32 MB vs wider).
- Subdirectories, multiple open files, long names — deferred.
- Editor cursor addressing — a small `K_SET_CURSOR` ABI entry vs direct screen-RAM
  writes (decided at phase 5).
- Whether/when to do the monitor-to-bank relocation.
