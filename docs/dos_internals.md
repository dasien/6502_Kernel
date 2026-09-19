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

