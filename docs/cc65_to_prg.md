# Porting C programs to MFC-DOS `.PRG` with cc65

This documents the pipeline for compiling C into a disk program the DOS can
launch by name. Two ports use it today:

| Program       | Source            | Pattern                              |
|---------------|-------------------|--------------------------------------|
| `CHESS.PRG`   | `programs/micromax` | self-contained (engine + UI in C)  |
| Scott Adams   | `programs/scottfree`| engine in C + **host-pre-parsed data** |

## Toolchain

Install cc65 (`brew install cc65`). The pipeline uses the **`none` target**
(no host OS runtime) plus a custom `ld65` config, and produces a raw image that
runs at `$0800` in user RAM. A 2-byte little-endian load-address header is
prepended afterward to make the `.PRG`.

```
cl65 -t none --signed-chars -O -C <prog>.cfg <sources...> glue.s -o <prog>.bin
printf '\000\010' > NAME.PRG          # $0800 load header, little-endian
cat <prog>.bin >> NAME.PRG
```

Each program ships a `build.sh` that runs exactly this. The built `.PRG` is
copied onto a FAT16 image with `tools` → `mkfat16 disk.img NAME.PRG`.

## The four pieces

1. **C source** — ordinary C, but mind the cc65 gotchas below.
2. **`glue.s`** — maps the kernel/DOS ABI onto the C runtime. Minimum is
   `OUTCH`/`INCH`/`CLS`; add `RND`, a quit path, and file calls as needed.
   Exports use a leading underscore (cc65 calling convention): `_OUTCH`, etc.
   Char arg arrives in `A`; char/int results return in `A` / `A:X`.
3. **`<prog>.cfg`** — `ld65` memory map (below).
4. **`build.sh`** — the three commands above.

### Kernel / DOS ABI used by glue

| Symbol            | Addr    | Use                                   |
|-------------------|---------|---------------------------------------|
| `K_PRINT_CHAR`    | `$FF00` | print A to screen                     |
| `K_PRINT_NEWLINE` | `$FF06` | newline                               |
| `K_GET_KEYSTROKE` | `$FF09` | non-blocking: C set + A=char (uppercased) |
| `K_CLEAR_SCREEN`  | `$FF0C` | clear + home                          |
| `FS_OPEN`         | `$AF03` | open file by name (ptr in `DOS_PTR` `$3C`) |
| `FS_GETB`         | `$AF06` | read next byte of open file           |
| `FS_PUTB`         | `$AF09` | write byte (create/append)            |
| `FS_CLOSE`        | `$AF0C` | close                                 |
| `FS_DIR_FIRST/NEXT`| `$AF0F/$AF12` | enumerate the catalog          |
| `DOS_WARM`        | `$AF1E` | return to the `]` prompt              |

A program is entered with `JMP`, runs at `$0800`, and returns to DOS via
`JMP DOS_WARM` (or by `RTS` if launched that way).

### Memory map (`.cfg`)

User RAM is `$0800–$8FFF` (~34 KB). The standard layout:

```
$0800 ..   code + rodata + data + bss   (the loaded image, writable)
$8F00      C stack top (2 KB, grows down)   __STACKSTART__ / __STACKSIZE__
$0080      cc65 zero-page runtime          __ZPSTART__ (free in a .PRG:
                                            BASIC/monitor aren't resident)
```

Heap only if the program calls `malloc`. Neither current port does — chess uses
fixed arrays; Scott Adams links its data in (see below) — so no heap is
configured.

## Two data patterns

### Self-contained (chess)
Everything is in the C/asm. `build.sh` just compiles and prepends the header.

### Host-pre-parsed data (Scott Adams)
The game database is **parsed on the host at build time**, not on the 6502.
`dat2c` reads a Scott Adams `.dat` and emits a C file of initialized tables
(`Items[]`, `Rooms[]`, `Actions[]`, strings, …). That C is compiled together
with the engine, so the 6502 binary carries **no parser, no `fscanf`, no heap**,
and the tables live in the loaded (writable) image as the working copy.

```
game.dat ──(host: dat2c)──▶ game_data.c ──┐
                                           ├─ cl65 --signed-chars ─▶ NAME.PRG
scott.c + glue.s ──────────────────────────┘
```

This pattern is the right call whenever a program would otherwise parse a large
text database at runtime: it trades a little disk (the engine is duplicated into
each game `.PRG`) for far less code and RAM on the target, and it sidesteps
cc65's weaker `scanf`/heap support entirely.

## cc65 gotchas (learned the hard way)

- **`--signed-chars` is mandatory.** cc65 defaults `char` to *unsigned*; most C
  assumes signed. Omitting it silently breaks logic (it cost us a day on
  micro-Max's move generator). Always pass it.
- **`int` is 16-bit.** Shifts past bit 15 are undefined: `1 << 16` is `0`, and
  `1 << 15` is negative and sign-extends when widened to `long`. For 32-bit bit
  sets (e.g. Scott Adams `BitFlags`), write `1L << n`.
- **"Too many local variables."** cc65 caps a function's local frame. A big auto
  array (`char buf[256]`) trips it. Hoist large buffers to `static` file scope.
- **The C library has more than you'd think.** `strcasecmp`/`strncasecmp` are in
  cc65's `<string.h>` — don't redefine them (conflicting types). But `printf`
  family is heavy; prefer hand-rolled number/word output for size.
- **K&R-style functions warn** ("implicit int", "control reaches end of
  non-void") but compile fine; not worth rewriting ported code to silence.

## Build & test loop

```
cd programs/<prog> && ./build.sh [args]          # -> NAME.PRG
cd cmake-build-debug && ./bin/mkfat16 disk.img ../programs/<prog>/NAME.PRG
# then relaunch the GUI, or drive it headlessly from a temp test in
# tests/test_monitor_integration.cpp (mountDisk + addKeypress + screen dump)
```

Headless screen dumps mask reverse-video (bit 7) bytes; account for that when
reading them (chess White pieces show blank/lowercase in a dump).
