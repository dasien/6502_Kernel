# MONITOR — Machine-Language Monitor Manual

The **monitor** is MFC's low-level machine-language console: examine and change
memory, run code, transfer files, and convert numbers. It's where the system
began, and it's still the fastest way to poke at the machine directly.

## Where the monitor lives

The monitor is **module bank 4**, not part of the kernel ROM. Type `MON` at the `]`
prompt and the kernel maps the bank and jumps into it; `Q` unmaps it and returns
you to the DOS. Pressing the **STOP** key (NMI) breaks in from anywhere — the
handler lives in always-mapped kernel ROM, so even a program that has scribbled on
the bank register cannot lock you out.

It is a bank rather than a disk program for a specific reason: a `.PRG` loads at
`$0800`, which is exactly the memory you would be trying to inspect, so the monitor
would overwrite the program under test. A bank costs no user RAM and needs no disk.

**The one blind spot:** the monitor occupies the module window, so it cannot show
you that window. `R:B000-EFFF` displays the monitor's own ROM rather than bank-0
RAM, and the other banks (BASIC, DEV TOOLS, FORTH) are invisible for the same
reason. Everything else reads normally — zero page, the stack, all of user RAM,
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

From the DOS `]` prompt type `MON`. The monitor prompts with the **current
address** followed by `>`:

```
0000>
```

Type `?` for the built-in command list, and `Q` to return to DOS. **ESC** exits
the current mode or aborts a paged/interactive operation.

## Command syntax

- A command is a single letter, usually followed by a colon: `R:`, `W:`, `F:` …
- Addresses are 4-digit **hex**, no `$` prefix, leading zeros required
  (`0400`, not `400`); case-insensitive.
- Ranges use a dash: `8000-8FFF`. Extra parameters follow commas: `8000-8FFF,FF`.
- Errors: `ERROR?` (bad syntax), `RANGE?` (end before start, or a protected range —
  see `F:`/`M:` below), `VALUE?` (bad hex/value).

## Memory commands

**`R:` — Read / display memory.** `R:xxxx` shows one line at an address; `R:xxxx-yyyy`
dumps a range (8 bytes per line, hex + ASCII), paged with ESC to abort. `R:` also
sets the current address without changing mode.

**`W:` — Write memory.** `W:xxxx` enters write mode (prompt `W:XXXX>`) for
interactive hex entry — type bytes, ESC to leave. You can also write inline:
`W:xxxx aa bb cc`. This is how you hand-enter a program (see `examples/`).

**`F:` — Fill.** `F:start-end,bb` fills the range with byte `bb`
(e.g. `F:8000-8FFF,00`).

**`M:` — Move / copy.** `M:start-end,dest,mode` copies (`mode 0`) or moves
(`mode 1`, clearing the source) a block, handling overlap; reports the byte count.

> **`F:` and `M:` refuse `$0014-$027C`, and report `RANGE?`.** That span holds the
> monitor's own live pointer, loop bound and fill byte, plus the buffer holding the
> command being executed — and both commands re-read that state on every iteration.
> A fill or copy across it rewrites the loop as it runs: `F:0000-00FF,00` used to
> reset its own pointer and hang the machine, and `F:0200-02FF,AA` used to set the
> bound to `$AAAA` and wipe all of user RAM before printing `OK`. `M:` also refuses a
> destination whose end would carry past `$FFFF`, since its loops stop on the source
> address only. Individual bytes in the span are still reachable with `W:`, and
> `Z:`/`T:` still display them.

**`X:` — Search.** `X:start-end,pattern` searches for a 1–16-byte hex pattern
(space-separated), printing each match address; paged, ESC aborts.

## Program commands

**`G:` — Go / run.** `G:xxxx` executes code at `xxxx`. A program returns to the
monitor with `RTS`.

> **`L:` and `S:` are retired.** They opened a host file dialog, which predates the
> filesystem; both now report `ERROR?`. Use the DOS instead — `LOAD name,addr` and
> `SAVE name,start-end` at the `]` prompt work against the disk, and unlike the old
> host dialog they can be scripted and tested.

## Number conversion

**`#:` — Decimal → hex.** `#:nnnnn` (0–65535) prints the value with a `$` prefix.

**`$:` — Hex → decimal.** `$:xxxx` (0000–FFFF) prints the value with a `#` prefix.

> These were `D:` and `H:` until the assembler was folded in. `D` went to the
> disassembler — the letter every period monitor uses for it — and `H` is freed for
> a future hunt/search. The symbols read the way they work: `#:` takes a decimal
> number, `$:` takes a hex one.

## Display commands

**`C:` — Clear screen.**

**`T:` — Stack dump.** Pages through the stack page `$0100-$01FF`.

> `T:` and `Z:` **snapshot** the page before printing it, so they show what was in
> memory when you typed the command. Reading live, they reported their own working
> state where the kernel's workspace lives: the dump walks its cursor through
> `$14/$15` and printing rewrites `$16/$17` and `$1A-$1D` between bytes, so those
> cells came back as the dump's current values rather than yours. The snapshot uses
> free RAM at `$0400-$04FF`, so bear that in mind if you are inspecting that range.

**`Z:` — Zero-page dump.** Pages through `$0000-$00FF` (system variables/workspace).

Paged output advances with SPACE/ENTER and aborts with ESC.

## Assembler commands

The assembler and disassembler are part of the monitor — they were a separate
`ASM` module until the monitor itself moved into a bank, at which point keeping
two prompts only meant crossing the DOS twice per build-and-test cycle. Every
monitor of the period bundled them the same way: Supermon, HESMON, and the Apple
II ROM monitor with its mini-assembler.

| Command | Action |
|---------|--------|
| `D:xxxx` | Disassemble 16 instructions from `xxxx` |
| `A:xxxx` | Line assembler from `xxxx` (empty line or ESC exits) |
| `L:` | Load a source file (host dialog) |
| `B:` | Build the loaded source (`OK`, or `? LINE nnnn`) |

### `D:xxxx` — Disassemble

Decodes one screenful (16 instructions) from `xxxx`, showing address, raw bytes,
mnemonic and operand:

```
>D:E000
E000: A2 FF     LDX #$FF
E002: 9A        TXS
```

Run `D:` again at the address the listing stopped at to continue.

### `A:xxxx` — Line assembler

One instruction per line, written straight to memory, address advancing as you go.
Numeric operands only — no labels or expressions; use `B:` for those. Enter on an
empty line (or ESC) leaves. A bad line prints `?` and re-prompts at the same
address.

```
>A:0800
0800: LDA #$05
0802: RTS
0803:
```

It picks zero-page vs absolute automatically, and branch operands are the
**destination address** — the assembler computes the offset and reports an error
if it is out of the ±128-byte range.

### `L:` — Load source

Reads a text source file into the source buffer so `B:` can build it. The host
shows a file-open dialog. Prints `LOADED`, or `?` if you cancel or the file is too
large. The buffer holds just under 4 KB.

### `B:` — Build

Two-pass assembles the loaded source into memory: pass 1 collects labels and sizes
the code, pass 2 emits bytes and prints a listing. Finishes with `OK`, or stops at
`? LINE nnnn`.

Because the assembler lives in the monitor, the whole loop stays in one place —
and the source buffer and symbol table survive `G:`, so a crash-and-patch cycle
costs you nothing:

```
> L:            load          > R:0900-090F   inspect
> B:            build         > W:0805 EA     patch
> D:0800        check         > G:0800        run again
> G:0800        run
```

## Writing source for `B:`

A source file is one statement per line. Blank lines and comments are ignored.

### Comments

Everything from a semicolon `;` to the end of the line is a comment:

```asm
    LDA #$41        ; load 'A'
```

### Labels

A label is a name followed by a colon at the start of a line. It takes the value
of the current address, so you can branch to it or reference it:

```asm
LOOP:
    DEX
    BNE LOOP
```

A label may sit on its own line or in front of an instruction on the same line.

### Constants (`NAME = expr`)

Define a named value with `=`:

```asm
SCREEN  = $0400
CHROUT  = $FF00
COUNT   = 10
```

Identifiers (labels and constant names) are **case-insensitive**, may contain
letters, digits, and underscores, and are up to 16 characters long.

### Expressions

Operands and directive values can be simple expressions:

- **Hex** with `$` (`$D020`), **decimal** (`4096`), or a **label/constant** name.
- `*` means the **current address** (the program counter).
- Add and subtract with `+` and `-`: `LDA MESSAGE+1`, `BNE *-3`.
- `<expr` takes the **low byte**, `>expr` takes the **high byte** — handy for
  loading a 16-bit address into a pointer:

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

If a file has no `.ORG` (or `*=`), assembly starts at **$0800** — the address the
DOS loads and runs a `.PRG` at, so a source with no origin still lands somewhere
useful and runnable.

**The origin must be ordinary user RAM, `$0800-$77FF`.**
Pass 2 emits with ordinary stores, so anything else destroys the machine, does
nothing at all, or destroys the build itself:

| Origin | Why it is refused |
|--------|-------------------|
| below `$0800` | zero page, stack, system variables, the `T:`/`Z:` snapshot buffer, and the assembler's own identifier buffers and symbol table at `$0500-$07FF` |
| `$7800-$87FF` | **the source buffer** — the text being assembled |
| `$8800-$AFFF` | the always-mapped DOS ROM — writes are discarded |
| `$B000-$EFFF` | the module window, where the monitor itself is running |
| `$F000-$FFFF` | the kernel BIOS ROM |

An out-of-range origin reports `? LINE nnnn` at the offending `.ORG` rather than
letting the build appear to succeed while emitting nothing.

#### Why the assembler's own storage is off limits

Two of those regions are the assembler's, and they are the cases that *look* legal
and are not:

- **The source buffer (`$7800-$87FF`):** pass 2 would emit over the very text it is
  walking, so the generated code and the line numbers in any diagnostic are both
  wrong from the first emitted byte — and the further the build gets, the less of
  the source survives to report against.
- **The symbol table (`$0520-$07FF`):** worse, because it fails *quietly*. Pass 2
  resolves labels out of that table while overwriting it, so expressions evaluate to
  garbage and the build still reports success.

Neither failure points at the origin as its cause, which is why this is enforced
rather than left to the programmer.

`$0800` is the floor because everything below it belongs to the system or the
assembler, and `$0800` is where the DOS loads and runs a `.PRG` anyway — so the
default origin and the lowest legal one are the same address.

### Diagnostics

The assembler reports `? LINE nnnn` — it never silently assembles something other
than what you wrote. In particular:

- **Operand too wide.** `LDA #$100` is an error, not `A9 00`; `.BYTE 300` is an
  error, not `$2C`. Use `.WORD` for 16-bit data, or `#<value` / `#>value` to take a
  specific byte of an address.
- **Hex constant wider than four digits.** `LDA $12345` is an error rather than
  quietly becoming `LDA $2345`.
- **Code pushed past column 79.** The line is refused rather than truncated: a
  cut-off token used to assemble as something else entirely (often a label-only
  line), which dropped the instruction and shifted every later label. A long
  *trailing comment* is fine — only lost code is an error, so wide comments (as in
  `examples/`) still assemble.
- **Branch out of range** is reported, as it always was.

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

The Dev Tools share the machine with everything else, so mind what they touch:

- **Source buffer:** `$7800–$87FF` (the text loaded by `L:`).
- **Symbol table:** `$0520–$07FF` (labels and constants; up to 40 symbols), with the
  identifier buffers just below it at `$0500–$051F`. This sits in the free page
  below `Ram_base`, so it costs user programs nothing — it used to take 512 bytes
  out of user RAM at `$7600`.
- **Working RAM:** `$0800–$77FF` is yours; the assembler only reserves the source
  buffer above it.

Assemble your programs into free user RAM below `$7600` (for example `$0800`),
clear of the source buffer and symbol table — `.ORG` enforces this, refusing any
origin at `$7600` or above (see [Directives](#directives)). Only one module is
mapped at a time —
save your work on the host before switching banks, since the buffers are not
preserved across module loads.

## Example programs

The repository's `examples/` directory has ready-to-load `.asm` sources —
`add_print.asm`, `hello_world.asm`, `count_loop.asm`, `colors.asm`,
`multiply.asm`, and more. Load one with `L:`, build it with `B:`, then run it from
the monitor with `G:` at the program's origin.

## Running the ROM modules

The monitor has no module menu (the old `B:` command was retired). The BASIC,
assembler (`ASM`), and FORTH modules launch **by name from the DOS `]` prompt** —
press `Q` to return to DOS, then type `BASIC`, `ASM`, or `FORTH`; DOS `BANKS` lists
the module catalog. See `BASIC.md` and `FORTH.md`.

## System commands

**`?` — Help** (no colon): the built-in command list.

**`.` — Recall:** re-run the last command; it can be edited before you press Enter.

**`ESC` — Exit / abort:** leaves write mode, stops a paged dump, cancels an entry.

## Modes

- **Command mode** (default): prompt `NNNN>`, all commands available.
- **Write mode**: prompt `W:XXXX>`, interactive hex entry; ESC returns to command mode.

## Tips

- `R:` to verify memory before and after a `W:`.
- `SAVE` a working range from the DOS before experimenting, so you can `LOAD` it back.
- `T:`/`Z:` are quick windows into stack and zero-page state while debugging.
- `.` saves retyping when iterating on the same command.
