# MFC/OS — DOS Manual

**MFC/OS** is the machine's resident disk operating system: it boots to the `]`
prompt, manages files on a real FAT16 disk image, and launches every other
program by name. Think Apple II / CP/M — a command shell with a filesystem, not a
menu. BASIC, the assembler, the monitor, and the disk programs (EDIT, TERM, IRC,
CHESS, VAULT, …) are all things you type at this prompt.

## Quick reference

| Command | Action |
|---------|--------|
| `name` | launch a DOS command, ROM program, or disk `.PRG` |
| `&name` | force the disk `.PRG` over a same-named ROM program |
| `MON` | enter the monitor (`Q` returns to DOS) |
| `BANKS` | list the built-in ROM programs |
| `CATALOG` / `CAT` `[pat]` | list files (supports `*` / `?` wildcards) |
| `TYPE` / `MORE` `name` | display a text file |
| `COPY src,dst` | copy a file |
| `MOVE src,dst` | move or rename a file |
| `RENAME old,new` | rename a file |
| `ERASE name` | delete a file |
| `LOAD name[,addr]` | load a file into memory |
| `SAVE name,ssss-eeee` | save a memory range to a file |
| `IMPORT name` / `EXPORT name` | host ↔ disk file exchange |
| `NEWDRAWER name` | create a drawer |
| `OPEN name` / `CLOSE` | enter / leave a drawer |
| `DROPDRAWER name` | remove an empty drawer |
| `DRAWER/FILE`, `/FILE` | cross-drawer / root path |
| `DISKFREE` | free space (bytes + KB) |
| `MEMMAP` | memory map |
| `VERSION` | OS version |
| `DATE` | date and time |
| `CLS` / `CLEAR` | clear the screen |
| `HELP` | list built-in commands |

## Signing on

At power-up the machine prints a sign-on box and drops you at the prompt:

```
╔══════════════════════════════════════╗
║        MFC 6502  OPERATIONAL         ║
║  MFC/OS 1.20      32768 BYTES FREE   ║
╚══════════════════════════════════════╝

]
```

The `]` prompt is where you type commands. When you have a drawer open, its name
appears in front of the bracket (`GAMES]`) so you always know where you are.

## Launching programs

Type a program's name and press Enter. MFC/OS resolves it in this order:

1. A built-in DOS command (below) or the monitor (`MON`).
2. A built-in ROM program: `BASIC`, `MON`, `FORTH`.
3. A program file (`.PRG`) on the disk.

You may leave off the `.PRG` extension — typing `EDIT` runs `EDIT.PRG`. Programs
run and, when they finish, return you to the `]` prompt.

```
EDIT              launch the editor
TERM              launch the serial terminal
IRC               launch the IRC client
BASIC             enter BASIC
ASM               enter the assembler
```

A built-in ROM program shadows a disk file of the same name. To force the disk
version instead, prefix the name with `&` (for example `&BASIC` runs a disk
program called `BASIC.PRG` rather than the ROM BASIC).

Unknown names print `COMMAND NOT FOUND`.

## The monitor (`MON`)

Type `MON` to enter the machine-code monitor — a low-level debugger for reading
and writing memory, filling and searching, and jumping to code. Press `Q` inside
the monitor to return to the `]` prompt.

## Listing ROM modules (`BANKS`)

`BANKS` lists the built-in ROM programs (BASIC, ASM, FORTH, …). Disk files are
listed with `CATALOG`.

## Working with files

### Listing — `CATALOG` / `CAT`

`CATALOG` prints the files in the current directory with a `NAME / BYTES` header
and decimal sizes in aligned columns; drawers are tagged `<D>` instead of a size.

```
CATALOG           list everything here
CAT               short alias
CATALOG *.PRG     only files ending in .PRG
CATALOG SAV??.PRG wildcard match
```

Wildcards use `*` (any run of characters) and `?` (any single character) against
8.3 names.

### Reading — `TYPE` / `MORE`

`TYPE name` prints a text file to the screen. `MORE name` does the same thing.
Long output pauses each screenful with a `--MORE--` prompt; press SPACE to
continue or ESC to stop.

```
TYPE README.TXT
MORE SVAULT/STORY.TXT
```

### Copying, moving, renaming, deleting

```
COPY src,dst      copy a file (paths may cross drawers)
MOVE src,dst      move or rename a file (same drawer = rename)
RENAME old,new    rename a file in place
ERASE name        delete a file
```

`COPY` reads the whole file through RAM, so a file larger than about 32 KB
reports `FILE TOO BIG`.

### Loading and saving memory

```
LOAD name[,addr]  load a file into memory (address from its header if omitted)
SAVE name,ssss-eeee  save a memory range to a file (with a .PRG load-address header)
```

Program files begin with a 2-byte load address, exactly like a Commodore `.PRG`.
This is how the assemble → `SAVE` → run-by-name loop closes: assemble in `ASM`,
`SAVE NAME,start-end`, then type `NAME` to run it.

### Host file exchange — `IMPORT` / `EXPORT`

Because the disk is a genuine FAT16 image, you can move files between the machine
and your Mac two ways:

- **In MFC/OS:** `IMPORT name` copies a host file (chosen from a host file
  picker) onto the disk; `EXPORT name` copies a disk file out to a host file.
- **On the host:** mount `disk.img` directly and drag files in and out (macOS
  Finder, `mount -o loop` on Linux, or any tool that reads FAT16).

## Drawers (subdirectories)

Drawers are one level of subdirectories — a way to group files (`GAMES/`,
`SYSTEM/`, `SVAULT/`). They can't nest, but they grow as needed across multiple
disk clusters, so there is no small file limit inside a drawer.

```
NEWDRAWER name    create a drawer
OPEN name         enter a drawer (the prompt shows its name)
CLOSE             return to the root
DROPDRAWER name   remove a drawer (must be empty)
```

Inside a drawer, bare filenames refer to that drawer. You can also reach files
without opening the drawer using a path:

```
DRAWER/FILE       a file inside a named root drawer
/FILE             a file at the root
```

For example, to play the vault game and read its backstory:

```
OPEN SVAULT
VAULT
TYPE STORY.TXT
```

Or reach the same files from the root without opening the drawer:

```
CATALOG SVAULT
TYPE SVAULT/STORY.TXT
COPY GAMES/CHESS.PRG,/CHESS.PRG
```

## Disk info and system commands

```
DISKFREE          free space in bytes and KB
MEMMAP            the full memory map with region sizes
VERSION           the MFC/OS version
DATE              current weekday, date, and time (from the RTC)
CLS / CLEAR       clear the screen
HELP              list the built-in commands
```

`DATE` prints one line such as `Wed 2026-07-26 14:30:05` — it shows both the date
and the time, so there is no separate time command.

## The disk

MFC/OS stores everything on a single FAT16 disk image (`disk.img`). Because it's
a standard FAT16 volume, your host can mount the same image read/write — macOS,
Linux and Windows all can — so files you create on the machine appear on the host
and vice versa. Files use 8.3 names (up to eight characters, a dot, and a
three-character extension).

The root directory holds up to **512 files and drawers**; a drawer has no fixed
limit and grows as you add files, so deep collections belong in drawers rather than
at the root. (Root capacity used to be 16, one sector's worth.)

**The image must be FAT16 with 512-byte sectors.** MFC/OS checks the volume when
it mounts and refuses anything else, because driving a FAT12 or FAT32 volume as
FAT16 would corrupt it on the very first write. If you format an image yourself,
ask for FAT16 explicitly and make it at least ~2 MB — every formatter silently
chooses FAT12 for small volumes:

```
mkfs.fat -F 16 disk.img          # Linux
newfs_msdos -F 16 disk.img       # macOS
```

Two FATs (the default for both of those tools) are fine: MFC/OS keeps every copy
in step, so a host filesystem check won't find them disagreeing.
