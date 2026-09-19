# MFC-DOS and Filesystem Internals

How MFC-DOS and the FAT16 filesystem are built, covering the command shell, the
block device layer, directory and file handling, and the on-disk layout. The DOS
user manual is [DOS.md](DOS.md), and how `disk.img` is assembled is in
[disk_image.md](disk_image.md).

---

### Where the arc stands

All five phases of the original plan are complete. The machine boots into the
MFC/OS shell at the `]` prompt with the full file verb set, the monitor is
launched by `MON` and exited with `Q`, and launch-by-name runs both ROM modules
and disk `.PRG` programs, with `&` forcing the disk version. The assemble, save
and run loop is closed, and the OS identity is MFC/OS with the `]` prompt.

Beyond the original plan, a set of utility commands was added. `COPY SRC,DST`
works through a RAM buffer, because the filesystem allows only one open file at a
time, so COPY reads the source fully into user RAM at `$0800` and then writes it
out. Files larger than that 32 KB buffer report `FILE TOO BIG`. `DISKFREE` reports
free space in decimal bytes and KB, scanning each FAT sector once. `MEMMAP` prints
the full memory map with region sizes. `VERSION` and `MORE NAME` were added, along
with `*` and `?` wildcards against 8.3 names in `CATALOG` and `CAT`. `CATALOG`
prints a `NAME / BYTES` header with decimal sizes in aligned columns.

Paging is system-wide as of kernel v3.18 and DOS 1.9. It moved out of the DOS's
own `MORE` into the kernel's single `PRINT_CHAR` path, in `PAGE_ADVANCE`, which
counts newlines and pauses every `LINES_PER_PAGE` lines with a
`--MORE-- (SPACE, ESC=STOP)` prompt. It is gated by `PAGE_ENABLE`, on by default,
and reset once per command in `GET_KEYSTROKE` when the submitting CR arrives.
Because every text program prints through `K_PRINT_CHAR`, the DOS shell, the
monitor, BASIC and FORTH are all paged with no code of their own, so long `TYPE`,
`CATALOG`, `LIST`, `WORDS` and memory dumps pause each screenful. SPACE or any
other key advances and ESC stops. As a result `MORE` is now identical to `TYPE`
and dispatches to it, and the DOS carries no pager of its own. A future settings
facility will expose `PAGE_ENABLE` so paging can be turned off.

`DATE` shows the date and time from the host-time RTC at `$FE55-$FE60`. The same
clock timestamps files, so `CATALOG` shows a date and time per entry. `BANKS`
lists the ROM modules. Decimal conversion was promoted to the BIOS ABI in kernel
v3.12 as `K_PRINT_DEC` at `$FF27`, which prints a 32-bit value in decimal and can
right-justify it, and `K_PARSE_DEC` at `$FF2A`, which parses decimal into 16 bits.
The monitor's `#:` and `$:` conversions and the DOS uses in `CATALOG` sizes and
`DISKFREE` all share these, so there is one implementation of each.

Drawers arrived in DOS v1.2 as one level of subdirectories, with `NEWDRAWER name`,
`OPEN name`, `CLOSE` and `DROPDRAWER name`, the last of which requires the drawer
to be empty. The implementation is a current-directory pointer, `DOS_CWD_CLUS`
with 0 meaning root, plus a unified directory iterator that walks either the fixed
root region or a subdirectory's cluster chain. The chain is growable, so a full
drawer directory chains another cluster. Path resolution is wired into `_FS_OPEN`,
`_FS_DELETE` and `_FS_RENAME`, where a bare `FILE` means the current directory,
`DRAWER/FILE` means a named root drawer and `/FILE` means the root. The file verbs
therefore act in the current drawer, and bare names keep the `FS_OPEN` ABI working
for launched programs. `CATALOG` tags drawers `<D>` and hides `.` and `..`, and
the prompt shows the open drawer as `UTILS]`. Drawers cannot nest. `mkfat16` can
place files in one with an `@DRAWER` argument, which its default sample disk
already uses for `SYSTEM/`. This
work surfaced and fixed a latent overlap where `DOS_TMP2` aliased `DOS_ENTRY+0`,
which was harmless until subdirectory enumeration interleaved a FAT read with
entry inspection.

Drawers phase 2 in DOS v1.3 added cross-drawer file motion. `COPY` takes qualified
paths on either side, as in `COPY GAMES/CHESS.PRG,/CHESS.PRG`, and it gets this
for free because path resolution lives in `_FS_OPEN`. `MOVE SRC,DST` shares COPY's
RAM-buffer path and then deletes the source, so within one directory it is
effectively a rename and across drawers it is a move. It is guarded against
`MOVE A,A`. Path resolution has its own scratch byte, `DOS_RES_SLASH`, so it does not clobber
the source offset COPY and MOVE hold in `DOS_SH_NAMEIDX`. A FAT
allocation rover, `DOS_ALLOC_HINT`, was added so cluster allocation resumes where
the last one stopped instead of rescanning from cluster 2. That turns a
multi-cluster write from order file-size times used-clusters FAT reads into
roughly order file-size, and copying a several-KB program is now snappy instead of
taking seconds.

The pivot behind all of this was that the machine boots into a DOS, a command
shell with a filesystem, the way an Apple II, a TRS-80 or a CP/M Kaypro does.
BASIC, the assembler and disassembler, the monitor and an editor became programs
you launch by name from the DOS rather than the front door. That turned the
project from a monitor with ROM modules into a small 6502 disk operating system.

Three references shaped it. cpm65 is a 6502 OS with BIOS, BDOS and CCP layering,
disk as a host file, and relocatable programs. The X16 emulator's `sdcard.c` backs
a host `.img` file in 512-byte blocks. And mike42's 6502 SD reader informed the
block device. See [references.md](references.md) for the full list.

---

### The model: a resident OS + apps

```
Apps        ── BASIC, monitor, assembler/disassembler, editor, games
              (ROM banks in the $B000-$EFFF window, or program files on disk)
─────────────────────────────────────────────────────────────────────────
Resident OS ── DOS shell  (prompt, commands, launch-by-name)        [DOS ROM]
            ── Filesystem (FAT16 over the block device)             [DOS ROM]
            ── BIOS       (boot/init, I/O, the $FF00 ABI, banking)  [kernel ROM]
            ── Block device driver (512-byte sectors)               [DOS ROM]
─────────────────────────────────────────────────────────────────────────
Hardware    ── host disk.img (a real FAT16 volume the Mac can mount too)
```

This is the CP/M shape of a BIOS, a BDOS and a CCP. The BIOS is the machine and
its I/O, the BDOS is the filesystem, and the CCP is the DOS command shell.

### BIOS vs. monitor

The BIOS is the resident foundation. It stays in the kernel ROM and is never
banked.

- `RESET` and init, covering decimal mode, interrupts, the stack, the zero-page
  clear, the screen clear, the module-window clear and `MODULE_BANK`, plus the
  IRQ and NMI handlers and the timer.
- Screen and keyboard I/O, meaning `PRINT_CHAR`, `PRINT_MESSAGE`,
  `PRINT_NEWLINE`, `SCROLL_SCREEN`, `CLEAR_SCREEN`, the cursor,
  `GET_KEYSTROKE`, `READ_COMMAND_LINE`, hex parsing and `PRINT_HEX_BYTE`.
- The `$FF00` jump-table ABI and the bank mechanism, meaning `MODULE_DIR`, bank
  launch and the `$FF12` return.
- The vectors and the RNG. The block device driver, the FAT16 code and the FS
  entries all live in the DOS ROM instead, reached through its own `$AF00`
  table.
- The vectors and the RNG.

The monitor is a debugger tool rather than the front door.

- `MONITOR_LOOP`, the prompt, the dispatch, and the letter commands.
- Launched from the DOS by `MON`, and exits back to the DOS.
- Module bank 4, with the assembler folded into it, which leaves the kernel ROM
  a lean 4 KB BIOS at `$F000-$FFFF`. Host transfer and the bank catalog are DOS
  commands.

### Boot flow

`RESET` runs BIOS init and then jumps to the DOS shell entry rather than to
`MONITOR_MAIN`, so the user lands at the DOS prompt.

### Storage backing + block device (emulator)

A single host file, `disk.img`, holding a real FAT16 volume and opened `r+b`. The
6502 talks to it through a simple memory-mapped block-device register interface
rather than SPI and SD protocol emulation. The X16 emulates SPI only because it is
real hardware with a physical SD port, and this machine is software-defined, so
the SPI ceremony would buy nothing.

The registers sit in the I/O page, just after `MODULE_BANK` at `$FE23`.

| Addr | Name | Purpose |
|------|------|---------|
| `$FE24-$FE25` | `BLK_LBA` | 16-bit sector number (→ 32 MB image; widen to 3 bytes later if needed) |
| `$FE26` | `BLK_CMD` | write `1` = read sector→buffer, `2` = write buffer→sector |
| `$FE27` | `BLK_STATUS` | `0` = ready, `$FF` = I/O error. Transfers are synchronous, so there is no busy state, and a missing image is created rather than refused |
| `$FE28` | `BLK_DATA` | 512-byte data port, auto-incrementing index |

To read, set `BLK_LBA`, write 1 to `BLK_CMD`, then read `BLK_DATA` 512 times. To
write, set `BLK_LBA` first, then write `BLK_DATA` 512 times, then write 2 to
`BLK_CMD`. The LBA must come first, because writing either LBA byte resets the
data-port index. On
the emulator side this is a small block-device class that opens `disk.img`, keeps
a 512-byte buffer, and wires four registers through `Memory`.

### Filesystem (resident FAT16, in the DOS ROM)

FAT16 was chosen for host interoperability. `disk.img` is a normal FAT16 volume,
so the Mac can mount it and exchange the same files the machine sees. It is
simpler than FAT32 and a few-MB image is plenty. FAT32, CP/M and custom formats
were all considered and set aside, because FAT16 is the sweet spot for size and
for being mountable on the host.

The driver mounts by parsing the boot sector and BPB to find the FAT, the root
directory and the data region. It reads by finding a file by 8.3 name and
following the cluster chain, and writes by allocating clusters and updating both
the FAT and the directory entry. It enumerates directories. The starting
simplifications were the root directory only, one open file at a time, 8.3 names
and FAT16 only, which is enough for the whole workflow.

The FS ABI is a byte stream reached through the jump table, with
`FS_OPEN(name, mode)`, `FS_GETB` returning carry set at EOF, `FS_PUTB(byte)`,
`FS_CLOSE`, and `FS_DIR_FIRST` with `FS_DIR_NEXT`. Filenames are supplied by the
6502, so the filesystem finds the file and there is no host dialog.

### The DOS shell (CCP)

It boots to a prompt, reads a line by reusing the BIOS `READ_COMMAND_LINE`,
parses a verb and its arguments, and dispatches. The vocabulary is full words with
optional short aliases.

| Command | Does |
|---------|------|
| `CATALOG` (`CAT`) | list files (name, size, modified date) |
| `LOAD name[,addr]` | load a file into memory (addr from the file's header if omitted) |
| `SAVE name,start-end` | save a memory range to a file (writes the load-address header) |
| `ERASE name` | delete a file |
| `RENAME old,new` | rename a file |
| `TYPE name` | display a text file |
| `IMPORT name[,host]` | copy a host file into a FAT16 file (`host` names it; omitted = file picker) |
| `EXPORT name[,host]` | copy a FAT16 file out to a host file (`host` names it; omitted = save dialog) |

#### Launch by name (unified — ROMs *and* programs)

There is no bank menu and no `RUN` verb. You type a name at the DOS prompt and
the DOS resolves it in order.

1. A built-in DOS command such as `CATALOG` or `ERASE`, which includes `MON`.
2. A built-in ROM program from the `MODULE_DIR` registry, currently `BASIC` in
   bank 1, `FORTH` in bank 3 and the monitor in bank 4. The DOS maps the bank and
   jumps to its entry, and the module returns to the DOS on exit.
3. A program file on disk, which the DOS loads into RAM and executes.

Resolution is ROM-module-first, so a disk file of the same name is shadowed. The
override prefix `&` skips the module check and runs the disk file `NAME`, which
lets a disk program intentionally replace a built-in. A launched program returns
to the DOS by a plain `RTS`, because the DOS runs it as a subroutine with a
`DOS_WARM` return pushed, so a normal `RTS` lands back at the `]` prompt. A
program that takes over the machine just needs a reset.

The difference between a ROM and a program is therefore an implementation detail.
A ROM is fast, always present and not editable, while a file lives on disk and can
be assembled and saved. `MODULE_DIR` remains the registry the DOS consults through
a kernel ABI. `HELP` surfaces the built-ins and `CATALOG` lists the disk files.

#### Program file format

A runnable program file begins with a 2-byte little-endian load address, exactly
like a C64 `.PRG`. To launch one, read the 2-byte header, load the body at that
address, and jump to it, since the entry point is the start. This dovetails with
the assembler, because `.ORG $0800` makes the saved program carry `$0800` as its
header and launching it puts it back at `$0800`. That closes the loop of
assembling, saving with a header, and running by name. Relocatable, cpm65-style
position-independent loading is a possible future upgrade.

#### `RUN` vs the monitor's `G:`

These are different layers and both are kept. The monitor's `G:addr` jumps to an
address where code already sits, which is the low-level debugger's go.
Launch-by-name loads a program and then executes it, so it is closer to load plus
go. `RUN` was dropped, because typing the name is the launch.

### Memory budget — creating space

The resident OS of BIOS, monitor, filesystem and DOS would not fit the 8 KB
kernel ROM, and the kernel could not grow in place, because the 12 KB BASIC window
sat below `$E000` and the vectors pinned the top. So a second always-mapped ROM
was added.

- A DOS ROM at `$9000-$AFFF`, 8 KB and always mapped, holding the FAT16
  filesystem and the DOS shell. The emulator routes it like the kernel and BASIC
  ROMs, except that it is always mapped rather than banked. At boot, BIOS init
  jumps to the DOS entry, and the FS ABI entries in the `$FF00` table jump into
  it.
- User RAM becomes `$0800-$8FFF`, roughly 34 KB, which is still ample. The
  assembler's source buffer and symbol table relocate below `$9000`.

> Later change (2026-07): the DOS base moved down again, to `$8800-$AFFF` (10 KB),
> because the DOS had 145 bytes left below its `$AF00` ABI table while the kernel sat
> on 3.7 KB spare. User RAM is now `$0800-$87FF` (32 KB) and the assembler's buffers
> moved with it (`$7800` source, `$7600` symbols). The addresses in this section and in
> the phase log below record the state at the time and are left as written.

This keeps the kernel ROM of BIOS plus monitor untouched and gives the filesystem
and DOS a roomy home. The alternative was a bigger coordinated memory-map
overhaul, and the second-ROM approach was lower risk.

The optional tidy-up noted here, relocating the monitor to a bank and porting it
to the `$FF00` ABI so the kernel ROM becomes a lean pure BIOS, was later done. The
monitor is bank 4 and the BIOS is 4 KB at `$F000`.

### Identity

The OS is MFC/OS. Boot shows a sign-on banner and an `]` prompt. The monitor is
launched by `MON` and exited with `Q`, which returns to the DOS prompt. The
`dos.rom` signature string stays "MFC-DOS" as an internal marker.

### Phased build plan

> Historical. The phase log below is the build record as it was written, with the
> addresses, kernel versions and module names that were in play at each step.
> Several of them have since moved, most notably the DOS ROM base, the assembler's
> buffers, and the assembler's own existence as a separate `ASM` module. The
> sections above and Part 2 of `architecture.md` are the current picture.

1. Block device — emulator `disk.img` + the `$FE24` registers + `Memory` routing;
   a 6502 sector read/write smoke test. (Small, foundational.) — DONE. `BlockDevice`
   (`include/computer/BlockDevice.h`, `src/computer/BlockDevice.cpp`) backs a host
   `disk.img` (lazily opened, auto-created, grows on write, reads zeros past EOF);
   `Memory` routes `$FE24-$FE28` to it; `Computer6502` owns it (default image
   `../disk.img`). Covered by `tests/test_block_device.cpp` (`block_device_unit_tests`).
2. FAT16 mount + read — mount, `CATALOG`, read a file by name; the FS ABI. (At
   this point, mount-on-Mac authoring already works.) Sub-steps:
   - **2.1 Memory-map shift — DONE.** The always-mapped DOS ROM at `$9000-$AFFF`
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
     `BLK_READ_SECTOR` (`$AF15`), `BLK_WRITE_SECTOR` (`$AF18`).

     **Register contract for the `$AF00` FS entries:** results come back in **A and
     the carry (carry set = error / EOF). `FS_GETB` preserves X and Y** — it used
     to destroy X only on the path that crosses a 512-byte sector boundary, which is
     the worst kind of bug: a read loop indexing with X passed every small-file test
     and corrupted itself on the 513th byte, so it is now explicitly preserved and
     pinned by `FsGetbPreservesXAcrossSectorBoundaries`. Every other FS entry
     leaves X undefined (`FS_PUTB` clobbers it at entry testing `DOS_W_MODE`, and
     the open/close/delete/rename paths run cluster and directory arithmetic through
     X); assume only A and carry survive those. The cc65 glue in `programs/*/glue.s`
     reloads X on return, which is why this went unnoticed for so long. DOS zero page uses the
     free `$3A-$5A` gap (`BLK_BUF_PTR=$3A`). Covered by `tests/test_dos_blockio.cpp`
     (`dos_blockio_tests`) which runs the real `dos.rom` routines.
   - **2.3 — DONE.** FAT16 read driver in `src/kernel/dos/dos.asm`, validated by
     `tests/test_dos_fat16.cpp` against host-built images (`tests/support/fat16_image.h`).
     - 2.3a: auto-mount (parse boot-sector BPB → sectors/cluster, FAT/root/data
       start, root entry count, cached in the `$0300` DOS state block) +
       `FS_DIR_FIRST`/`FS_DIR_NEXT` (root-dir walk, skipping deleted/LFN/volume
       entries, leaving the 32-byte entry in `DOS_ENTRY`).
     - 2.3b: `FS_OPEN` (parse 8.3 name, scan dir, arm the open-file cursor),
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
     - Temporary monitor command `@` (kernel.asm, v3.3): `@` catalogs the disk
       (names + sizes), `@NAME` types a file. It calls the DOS ABI at `$AF..`
       directly (the DOS ROM is always mapped), so no kernel `$FF00` change was
       needed; phase 4 replaces `@` with the real DOS shell.
     - Covered by `monitor_integration` (catalog, type, missing-file) against a
       mounted FAT16 image.
3. FAT16 write — create / `ERASE` / `SAVE`; full round-trip on the machine.
   - **3a — DONE.** The write engine in `dos.asm`: cluster allocation (scan the FAT
     for a free entry, mark EOC), FAT-entry read-modify-write (read the sector,
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
4. DOS shell as boot target — the pivot: fill the (already-present) `$9000-$AFFF`
   DOS ROM with the command shell, boot into the DOS prompt, the command set above,
   launch-by-name (command → ROM module → file), program-file loader. `MON`/`BASIC`/
   `ASM` launch their banks; the monitor is entered as a tool and returns to DOS.
   - **4.1 — DONE.** The boot pivot. RESET now `JMP DOS_COLD`; the machine boots into
     the MFC/OS shell (banner + `]` prompt) in `dos.asm`: read a line (BIOS
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
     - 4.3a — DONE. `RETURN_FROM_MODULE` (`$FF12`) now re-enters `DOS_WARM` (monitor-
       state save/restore dropped); new `K_LAUNCH_BY_NAME` ABI (`$FF21`) scans
       `MODULE_DIR` and `BANK_LAUNCH`es a match (assembler's name is `ASM`); the DOS
       resolves an unmatched verb to a module. `BASIC`/`ASM` run from `]` and return
       to `]`. The monitor `B:` bank menu is excised (`CMD_BANK_MENU`/`PARSE_CMD_BASIC`
       removed). Kernel v3.8.
     - 4.3b — DONE. Disk `.PRG` launch (`_DOS_RUN_FILE`): `FS_OPEN` the name, read
       the 2-byte load-address header, load the body there, then run it as a
       subroutine — clean stack with a `DOS_WARM` return pushed, so the program's
       `RTS` returns to `]`. A leading `&` forces this disk path over a same-named
       module. Unknown name → `COMMAND NOT FOUND`. Closes the loop: assemble in
       `ASM` → `SAVE NAME,start-end` → type `NAME` to run it.
5. Editor (module, bank) — full-screen, generic; edit/save FS files → full
   in-machine self-hosting (edit → assemble → run, all at the DOS).
6. (Later/optional) relocate the monitor to a bank; kernel ROM becomes a lean BIOS.

The filesystem phases, 1 through 3, are foundational and unchanged regardless of
the DOS framing. The pivot mainly reshaped phase 4 into a DOS shell rather than
file verbs bolted onto the monitor, and flipped the boot target.

### Settled decisions

- Boot into the DOS, and the monitor becomes a launchable tool reached by `MON`.
- A three-way split of BIOS, DOS and monitor, with the BIOS always resident and
  never banked.
- Storage is a host `disk.img` with 512-byte sectors behind simple block-device
  registers rather than SPI.
- The filesystem is FAT16, so the host can mount it.
- The DOS commands are `CATALOG`, `LOAD`, `SAVE`, `ERASE`, `RENAME` and `TYPE`.
- Launch-by-name is unified, resolving command, then ROM module, then disk file.
  There is no bank menu and no `RUN` verb.
- The program file format is a 2-byte load-address header, in the `.PRG` style.
- Space comes from an always-mapped DOS ROM, at `$9000-$AFFF` at the time, with
  user RAM at `$0800-$8FFF`.
- The editor is full-screen and generic.

### Open questions

- The OS name and boot prompt, settled under Identity above.
- Image creation and format. The `tools/mkfat16` host tool creates a genuine FAT16
  image of at least 4085 clusters, around 2 MB, confirmed mountable read and write
  by macOS, with `fsck_msdos` clean and `hdiutil attach` exchanging files both
  ways. An in-machine `FORMAT` is still a possible later addition.
- The width of `BLK_LBA`, currently 16 bits for a 32 MB image.
- Nested subdirectories, multiple open files and long names, all deferred. One
  level of drawer is implemented and a drawer may span several clusters.
- Editor cursor addressing, settled. `EDIT` writes through the VIC register port
  and paints its own reverse-video block cursor, hiding the kernel's hardware
  cursor while editing and restoring it on exit. No `K_SET_CURSOR` ABI entry was
  added.
- Whether and when to relocate the monitor to a bank. This was done, and the
  monitor is bank 4.
