# Porting C programs to MFC-DOS `.PRG` with cc65

This documents the pipeline for compiling C into a disk program the DOS can
launch by name. Every catalog entry carrying a build recipe comes through it:
EDIT, TERM, IRC, GOPHER, the Sunless Vault, CHESS, KERNEL PANIC, VENTURE,
FRONTIER FORTUNE and the twelve Scott Adams adventures. Two shapes recur:

| Program       | Source            | Pattern                              |
|---------------|-------------------|--------------------------------------|
| `CHESS.PRG`   | `programs/micromax` | self-contained (engine + UI in C)  |
| Scott Adams   | `programs/scottfree`| engine in C + host-pre-parsed data |

## Toolchain

Install cc65 (`brew install cc65`). The pipeline uses the `none` target
(no host OS runtime) plus a custom `ld65` config, and produces a raw image that
runs at `$0800` in user RAM. A 2-byte little-endian load-address header is
prepended afterward to make the `.PRG`.

```
cc65 -t none --signed-chars -O -o <src>.s <src>.c       # once per source
ca65 -t none -o <src>.o <src>.s
cl65 -t none -C <prog>.cfg <objects...> -o <prog>.bin   # link
mkprg 0800 <prog>.bin NAME.PRG                          # 2-byte load header
```

CMake does all of this for you, driven by the program's entry in
`programs/catalog.txt`. The machinery lives in `tools/cmake/Programs.cmake`. There is
no per-program build script for anything the catalog builds. `ninja programs`
builds every `.PRG`, `ninja <name>_prg` builds a single one, and `ninja disk`
puts them on an image. The load address in the header is read out of the `.cfg`
file's `STARTADDRESS`, so the two cannot drift apart.

## The four pieces

1. The C source is ordinary C, but mind the cc65 gotchas described below.
2. `glue.s` maps the kernel and DOS ABI onto the C runtime. A character-oriented
   program needs `INCH` and a quit path at minimum, and the two legacy ports
   also export `OUTCH` and `CLS`. Everything written since drives the VIC port
   directly instead, through `vaddr`, `vputc`, `vattr`, `vfill` and `vcmd`.
   Exports carry a leading underscore because that is the cc65 calling convention, so
   `OUTCH` is exported as `_OUTCH`. A char argument arrives in `A`, a char result
   returns in `A`, and an int result returns in `A` and `X`.
3. `<prog>.cfg` is the `ld65` memory map, covered below. Its `STARTADDRESS` is also
   where the `.PRG` load header comes from.
4. An entry in `programs/catalog.txt` names the `sources`, the `config` and the
   `program` line giving the `.PRG`. That is the whole build definition.

### Kernel / DOS ABI used by glue

| Symbol            | Addr    | Use                                   |
|-------------------|---------|---------------------------------------|
| `K_PRINT_CHAR`    | `$FF00` | print A to screen                     |
| `K_PRINT_NEWLINE` | `$FF06` | newline                               |
| `K_GET_KEYSTROKE` | `$FF09` | non-blocking: C set + A=char, case preserved |
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

User RAM is `$0800–$87FF` (32 KB). The standard layout:

```
$0800 ..   code + rodata + data + bss   (the loaded image, writable)
$8700      C stack top (2 KB, grows down)   __STACKSTART__ / __STACKSIZE__
$0080      cc65 zero-page runtime          __ZPSTART__ (free in a .PRG:
                                            BASIC/monitor aren't resident)
```

`__STACKSTART__` must stay at or below `$8700`, because the DOS ROM begins at `$8800`
and a stack placed above that writes into ROM, where the stores are silently
discarded. The whole image plus the stack has to fit in the 32 KB, and that is tighter
than it looks. FRONTIER, the largest, uses about 26 KB of it.

A heap is needed only if the program calls `malloc`. Neither current port does. Chess
uses fixed arrays, and Scott Adams links its data in, as described below, so no heap
is configured.

## Two data patterns

### Self-contained (chess)
Everything is in the C/asm. The build just compiles, links and prepends the header.

### Host-pre-parsed data (Scott Adams)
The game database is parsed on the host at build time, not on the 6502.
`dat2c` reads a Scott Adams `.dat` and emits a C file of initialized tables
(`Items[]`, `Rooms[]`, `Actions[]`, strings, …). That C is compiled together
with the engine, so the 6502 binary carries no parser, no `fscanf`, no heap,
and the tables live in the loaded (writable) image as the working copy.

```
dat/advNN.dat ──(host: dat2c)──▶ game_data.c ──┐
                                                ├─ cl65 ─▶ NAME.PRG
scott.c + glue.s ───────────────────────────────┘
```

A catalog entry asks for this with a `generate` line, and the twelve adventures are
twelve entries over one engine and one `ld65` config, differing only in which `.dat`
they name. Because one source directory then backs twelve builds, each entry compiles
into its own directory under the build tree.

This pattern is the right call whenever a program would otherwise parse a large text
database at run time. It trades a little disk space, since the engine is duplicated
into each game `.PRG`, for far less code and RAM on the target, and it sidesteps
cc65's weaker `scanf` and heap support entirely.

## cc65 gotchas (learned the hard way)

- `--signed-chars` is mandatory. cc65 defaults `char` to unsigned where most C
  assumes signed, and omitting the flag breaks logic silently. It cost a day on
  micro-Max's move generator. Always pass it.
- `int` is 16-bit, so shifts past bit 15 are undefined. `1 << 16` evaluates to `0`,
  and `1 << 15` is negative and sign-extends when widened to `long`. Write `1L << n`
  for 32-bit bit sets such as Scott Adams's `BitFlags`.
- cc65 caps a function's local frame, and a large automatic array such as
  `char buf[256]` trips it with "Too many local variables." Hoist big buffers out to
  `static` file scope.
- The C library has more in it than you would expect. `strcasecmp` and `strncasecmp`
  are already in cc65's `<string.h>`, so redefining them gives you conflicting types.
  The `printf` family is heavy, though, so prefer hand-rolled number and word output
  when size matters.
- K&R-style functions warn about implicit int and about control reaching the end of a
  non-void function, but they compile correctly. It is not worth rewriting ported code
  to silence those.

## Build & test loop

```
cd cmake-build-debug && ninja <prog>_prg         # -> <build>/programs/<prog>/NAME.PRG
ninja disk                                       # ...or rebuild the whole image
# then relaunch the GUI, or drive it headlessly from a temp test in
# tests/test_monitor_integration.cpp (mountDisk + addKeypress + screen dump)
```

Headless screen dumps mask the reverse-video bit, which is bit 7, so account for that
when reading them. In chess, for example, the White pieces show as blank or lowercase
in a dump.
