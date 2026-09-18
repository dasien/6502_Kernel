# MONITOR — Machine-Language Monitor Manual

The monitor is MFC's low-level machine-language console. It examines and
changes memory, runs code, assembles and disassembles, and converts numbers.
It's where the system began, and it's still the fastest way to poke at the
machine directly.

## Where the monitor lives

The monitor is module bank 4, not part of the kernel ROM. Type `MON` at the `]`
prompt and the kernel maps the bank and jumps into it. `Q` unmaps it and returns
you to the DOS. Pressing the STOP key (NMI) breaks in from anywhere, because the
handler lives in always-mapped kernel ROM, so even a program that has scribbled
on the bank register cannot lock you out.

It is a bank rather than a disk program for a specific reason. A `.PRG` loads at
`$0800`, which is exactly the memory you would be trying to inspect, so the
monitor would overwrite the program under test. A bank costs no user RAM and
needs no disk.

The one blind spot is that the monitor occupies the module window, so it cannot
show you that window. `R:B000-EFFF` displays the monitor's own ROM rather than
bank-0 RAM, and the BASIC and FORTH banks are invisible for the same reason.
Everything else reads normally, including zero page, the stack, all of user RAM,
the DOS ROM and the kernel BIOS. To examine bank-0 RAM in that range, copy it
somewhere below `$8800` first from a program running outside the window.

## Quick reference

| Command | Action |
|---------|--------|
| `R:xxxx[-yyyy]` | Read / dump memory |
| `W:xxxx [bb …]` | Write mode / inline write |
| `F:start-end,bb` | Fill range with a byte |
| `M:start-end,dest,mode` | Move (1) or copy (0) a block |
| `X:start-end,pat` | Search for a byte pattern |
| `G:xxxx` | Run code at xxxx |
| `A:xxxx` / `D:xxxx` | Line assemble / disassemble |
| `L:` / `B:` | Load source / build it |
| `#:nnnnn` / `$:xxxx` | Decimal↔hex conversion |
| `C:` `T:` `Z:` | Clear / stack dump / zero-page dump |
| `?` `.` `ESC` `Q` | Help / recall / exit-abort / quit to DOS |

## Entering and leaving

From the DOS `]` prompt type `MON`. The monitor prompts with the current address
followed by `>`.

```
0000>
```

Type `?` for the built-in command list, and `Q` to return to DOS. ESC exits
the current mode or aborts a paged/interactive operation.

## Command syntax

- A command is a single letter, usually followed by a colon, as in `R:`, `W:`
  and `F:`.
- Addresses are 4-digit hex with no `$` prefix and leading zeros required, so
  `0400` rather than `400`. Case does not matter.
- Ranges use a dash, as in `8000-8FFF`, and extra parameters follow commas, as
  in `8000-8FFF,FF`.
- There are three errors. `ERROR?` means bad syntax, `RANGE?` means the end came
  before the start or the range is protected (see `F:` and `M:` below), and
  `VALUE?` means a bad hex digit or value.

## Memory commands

`R:` reads and displays memory. `R:xxxx` shows one line at an address and
`R:xxxx-yyyy` dumps a range, sixteen bytes to the line, paged with ESC to abort.
`R:` also sets the current address without changing mode.

`W:` writes memory. `W:xxxx` enters write mode with the prompt `W:XXXX>` for
interactive hex entry, where you type bytes and press ESC to leave. You can also
write inline as `W:xxxx aa bb cc`. This is how you hand-enter a program, and
`examples/` has several.

`F:` fills. `F:start-end,bb` fills the range with the byte `bb`, as in
`F:8000-8FFF,00`.

`M:` moves and copies. `M:start-end,dest,mode` copies with mode 0 or moves with
mode 1, clearing the source. It handles overlap and reports the byte count.

`F:` and `M:` both refuse `$0014-$027C` and report `RANGE?`. That span holds the
monitor's own live pointer, loop bound and fill byte, plus the buffer holding the
command being executed, and both commands re-read that state on every iteration.
A fill or copy across it rewrites the loop as it runs. `F:0000-00FF,00` used to
reset its own pointer and hang the machine, and `F:0200-02FF,AA` used to set the
bound to `$AAAA` and wipe all of user RAM before printing `OK`. `M:` also refuses
a destination whose end would carry past `$FFFF`, since its loops stop on the
source address only. Individual bytes in the span are still reachable with `W:`,
and `Z:` and `T:` still display them.

`X:` searches. `X:start-end,pattern` searches for a hex pattern of one to
sixteen space-separated bytes and prints each match address. The output is paged
and ESC aborts it.

## Program commands

`G:` goes, or runs. `G:xxxx` executes code at `xxxx`, and a program returns to
the monitor with `RTS`.

The monitor's old binary load and save are retired. They opened a host file
dialog, which predates the filesystem. `S:` now reports `ERROR?`, and `L:` was
reused by the assembler to load source text. Use the DOS instead, where
`LOAD name,addr` and `SAVE name,start-end` at the `]` prompt work against the
disk, and unlike the old host dialog they can be scripted and tested.

## Number conversion

`#:nnnnn` converts decimal to hex. The value runs from 0 to 65535 and prints
with a `$` prefix.

`$:xxxx` converts hex to decimal. The value runs from 0000 to FFFF and prints
with a `#` prefix.

These were `D:` and `H:` until the assembler was folded in. `D` went to the
disassembler, which is the letter every period monitor uses for it, and `H` is
free for a future hunt command. The symbols read the way they work, so `#:` takes
a decimal number and `$:` takes a hex one.

## Display commands

`C:` clears the screen.

`T:` dumps the stack, paging through `$0100-$01FF`.

`Z:` dumps zero page, paging through `$0000-$00FF`, which holds the system
variables and workspace.

`T:` and `Z:` snapshot the page before printing it, so they show what was in
memory when you typed the command. Reading live, they reported their own working
state where the kernel's workspace lives. The dump walks its cursor through
`$14` and `$15`, and printing rewrites `$16`, `$17` and `$1A-$1D` between bytes,
so those cells came back as the dump's current values rather than yours. The
snapshot uses free RAM at `$0400-$04FF`, so bear that in mind if you are
inspecting that range.

Paged output advances with SPACE or ENTER and aborts with ESC.

## Assembler commands

The assembler and disassembler are part of the monitor. They were a separate
`ASM` module until the monitor itself moved into a bank, at which point keeping
two prompts only meant crossing the DOS twice per build-and-test cycle. Every
monitor of the period bundled them the same way, including Supermon, HESMON, and
the Apple II ROM monitor with its mini-assembler.

| Command | Action |
|---------|--------|
| `D:xxxx` | Disassemble 16 instructions from `xxxx` |
| `A:xxxx` | Line assembler from `xxxx` (empty line or ESC exits) |
| `L:` | Load a source file from the host |
| `B:` | Build the loaded source (`OK`, or `? LINE nnnn`) |

### `D:xxxx` — Disassemble

Decodes one screenful of sixteen instructions from `xxxx`, showing the address,
the raw bytes, and the mnemonic with its operand.

```
>D:E000
E000: A2 FF     LDX #$FF
E002: 9A        TXS
```

Run `D:` again at the address the listing stopped at to continue.

### `A:xxxx` — Line assembler

One instruction per line, written straight to memory, with the address advancing
as you go. Operands must be numeric, since there are no labels or expressions
here, and `B:` is the command for those. Pressing Enter on an empty line, or ESC,
leaves. A bad line prints `?` and re-prompts at the same address.

```
>A:0800
0800: LDA #$05
0802: RTS
0803:
```

It picks zero-page or absolute addressing automatically. Branch operands are the
destination address, and the assembler computes the offset itself, reporting an
error if the target is out of the 128-byte range.

### `L:` — Load source

Reads a text source file into the source buffer so `B:` can build it. The host
shows a file-open dialog. It prints `LOADED`, or `?` if you cancel or the file is
too large. The buffer holds just under 4 KB at `$7800-$87FF`.

### `B:` — Build

Two-pass assembles the loaded source into memory. Pass 1 collects labels and
sizes the code, and pass 2 emits bytes and prints a listing. It finishes with
`OK`, or stops at `? LINE nnnn`.

Because the assembler lives in the monitor, the whole loop stays in one place.
The source buffer and symbol table survive `G:`, so a crash-and-patch cycle costs
you nothing.

```
> L:            load          > R:0900-090F   inspect
> B:            build         > W:0805 EA     patch
> D:0800        check         > G:0800        run again
> G:0800        run
```

## Writing source for `B:`

A source file is one statement per line. Blank lines and comments are ignored.

### Comments

Everything from a semicolon to the end of the line is a comment.

```asm
    LDA #$41        ; load 'A'
```

### Labels

A label is a name followed by a colon at the start of a line. It takes the value
of the current address, so you can branch to it or reference it.

```asm
LOOP:
    DEX
    BNE LOOP
```

A label may sit on its own line or in front of an instruction on the same line.

### Constants (`NAME = expr`)

Define a named value with `=`.

```asm
SCREEN  = $0400
CHROUT  = $FF00
COUNT   = 10
```

Identifiers (labels and constant names) are case-insensitive, may contain
letters, digits, and underscores, and are up to 16 characters long.

### Expressions

Operands and directive values can be simple expressions.

- A term is hex with a `$` prefix such as `$D020`, plain decimal such as `4096`,
  or the name of a label or constant.
- `*` means the current address, the program counter.
- Add and subtract with `+` and `-`, as in `LDA MESSAGE+1` and `BNE *-3`.
- `<expr` takes the low byte and `>expr` takes the high byte, which is how you
  load a 16-bit address into a pointer.

```asm
    LDA #<MESSAGE       ; low byte of MESSAGE
    STA PTR
    LDA #>MESSAGE       ; high byte of MESSAGE
    STA PTR+1
```

### Directives

| Directive | Aliases | Meaning |
|-----------|---------|---------|
| `.ORG expr` | `*= expr` | Set the assembly address (origin) |
| `.END` | | Stop assembling |
| `.BYTE v[,v…]` | `.DB` | Emit bytes; a value may be a `"quoted string"` |
| `.WORD v[,v…]` | `.DW` | Emit 16-bit words (low byte first) |
| `.ASCII "text"` | `.TX` | Emit the bytes of a quoted string |

Directive names are case-insensitive (`.org`, `.byte`, and `.ORG`, `.BYTE` are
equivalent). Text inside a `"…"` string keeps its original case.

If a file has no `.ORG` or `*=`, assembly starts at `$0800`. That is the address
the DOS loads and runs a `.PRG` at, so a source with no origin still lands
somewhere useful and runnable.

The origin must be ordinary user RAM, `$0800-$77FF`. Pass 2 emits with ordinary
stores, so anything else destroys the machine, does nothing at all, or destroys
the build itself.

| Origin | Why it is refused |
|--------|-------------------|
| below `$0800` | zero page, stack, system variables, the `T:`/`Z:` snapshot buffer, and the assembler's own identifier buffers and symbol table at `$0500-$07FF` |
| `$7800-$87FF` | the source buffer — the text being assembled |
| `$8800-$AFFF` | the always-mapped DOS ROM — writes are discarded |
| `$B000-$EFFF` | the module window, where the monitor itself is running |
| `$F000-$FFFF` | the kernel BIOS ROM |

An out-of-range origin reports `? LINE nnnn` at the offending `.ORG` rather than
letting the build appear to succeed while emitting nothing.

#### Why the assembler's own storage is off limits

Two of those regions are the assembler's own, and they are the cases that look
legal and are not.

- The source buffer at `$7800-$87FF`. Pass 2 would emit over the very text it is
  walking, so the generated code and the line numbers in any diagnostic are both
  wrong from the first emitted byte, and the further the build gets the less of
  the source survives to report against.
- The symbol table at `$0520-$07FF`. This one is worse because it fails quietly.
  Pass 2 resolves labels out of that table while overwriting it, so expressions
  evaluate to garbage and the build still reports success.

Neither failure points at the origin as its cause, which is why this is enforced
rather than left to the programmer.

`$0800` is the floor because everything below it belongs to the system or the
assembler, and `$0800` is where the DOS loads and runs a `.PRG` anyway, so the
default origin and the lowest legal one are the same address.

### Diagnostics

The assembler reports `? LINE nnnn` and never silently assembles something other
than what you wrote.

- An operand that is too wide is an error. `LDA #$100` does not become `A9 00`
  and `.BYTE 300` does not become `$2C`. Use `.WORD` for 16-bit data, or
  `#<value` and `#>value` to take a specific byte of an address.
- A hex constant wider than four digits is an error, so `LDA $12345` does not
  quietly become `LDA $2345`.
- Code pushed past column 79 is refused rather than truncated. A cut-off token
  used to assemble as something else entirely, often a label-only line, which
  dropped the instruction and shifted every later label. A long trailing comment
  is fine, since only lost code is an error, so the wide comments in `examples/`
  still assemble.
- A branch out of range is reported, as it always was.

### A complete example

```asm
; Add 5 + 3 and print the result
.ORG $0800

CHROUT = $FF00          ; kernel print-a-character routine

START:
    LDA #$05
    CLC
    ADC #$03
    CLC
    ADC #$30            ; convert to an ASCII digit
    JSR CHROUT
    LDA #$0D            ; carriage return
    JSR CHROUT
    RTS
.END
```

Build it with `B:`, then exit (ESC), enter the monitor with `MON`, and run it with
`G:0800`.

## Memory used

The assembler shares the machine with everything else, so mind what it touches.

- The source buffer is `$7800-$87FF`, holding the text loaded by `L:`.
- The symbol table is `$0520-$07FF`, holding up to 40 labels and constants, with
  the identifier buffers just below it at `$0500-$051F`. This sits in the free
  page below `Ram_base`, so it costs user programs nothing. It used to take 512
  bytes out of user RAM.
- Working RAM is `$0800-$77FF` and it is yours. The assembler reserves only the
  source buffer above it.

Assemble your programs into free user RAM below `$7800`, `$0800` for instance,
clear of the source buffer and symbol table. `.ORG` enforces this and refuses
any origin at `$7800` or above (see [Directives](#directives)). Only one module
is mapped at a time, so save your work on the host before switching banks, since
the buffers are not preserved across module loads.

## Example programs

The repository's `examples/` directory has ready-to-load `.asm` sources such as
`add_print.asm`, `hello_world.asm`, `count_loop.asm`, `colors.asm` and
`multiply.asm`. Load one with `L:`, build it with `B:`, then run it from the
monitor with `G:` at the program's origin.

## Running the ROM modules

The monitor has no module menu. The old one was a `B:` bank picker, and that
letter now builds the loaded source. The BASIC and FORTH modules launch by name
from the DOS `]` prompt, so press `Q` to return to DOS and then type `BASIC` or
`FORTH`. DOS `BANKS` lists the module catalog, which holds BASIC in bank 1,
FORTH in bank 3 and the monitor itself in bank 4. See `BASIC.md` and
`FORTH.md`.

## System commands

`?` prints the built-in command list. It takes no colon.

`.` recalls the last command, which you can edit before pressing Enter.

`ESC` exits or aborts. It leaves write mode, stops a paged dump, and cancels an
entry.

## Modes

- Command mode is the default. The prompt is `NNNN>` and all commands are
  available.
- Write mode is entered by `W:xxxx`. The prompt is `W:XXXX>` for interactive hex
  entry, and ESC returns to command mode.
- Assemble mode is entered by `A:xxxx`. The prompt is the address being
  assembled, and an empty line or ESC returns to command mode.

## Tips

- Use `R:` to verify memory before and after a `W:`.
- `SAVE` a working range from the DOS before experimenting, so you can `LOAD` it
  back.
- `T:` and `Z:` are quick windows into stack and zero-page state while debugging.
- `.` saves retyping when iterating on the same command.
