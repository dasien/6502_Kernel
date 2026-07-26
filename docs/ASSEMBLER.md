# ASSEMBLER — Dev Tools Manual

The **Dev Tools** module is MFC's built-in 6502 assembler and disassembler. It
disassembles memory, assembles instructions one line at a time for quick patches,
and two-pass assembles a whole source file — with labels, constants, expressions,
and data directives — straight into memory, where you run it from the monitor with
`G:`.

It targets the WDC 65C02 instruction set (so `BRA`, `STZ`, `PHX`, `BBRn`, and the
rest are all available).

## Quick reference

| Command | Action |
|---------|--------|
| `D xxxx` | Disassemble 16 instructions from `xxxx` |
| `A xxxx` | Line assembler from `xxxx` (Enter/ESC on empty line exits) |
| `L` | Load a source file from the host |
| `B` | Build the loaded source (`OK`, or `? LINE nnnn`) |
| ESC | Exit to the monitor |

| Syntax | Example |
|--------|---------|
| Label | `LOOP:` |
| Constant | `SCREEN = $0400` |
| Comment | `; note` |
| Hex / decimal | `$D020` / `4096` |
| Current address | `*` |
| Add / subtract | `MESSAGE+1`, `*-3` |
| Low / high byte | `#<MESSAGE`, `#>MESSAGE` |
| `.ORG` / `*=` | `.ORG $0800` |
| `.BYTE` / `.DB` | `.BYTE $01,$02,$03` |
| `.WORD` / `.DW` | `.WORD $0800,$FF00` |
| `.ASCII` / `.TX` | `.ASCII "HELLO"` |
| `.END` | end of source |

## Starting

The Dev Tools are a bankable ROM module, launched **by name** from the DOS `]`
prompt (the old monitor `B:` menu was retired):

```
]ASM
```

The module clears the screen and shows its banner and command reminder:

```
MFC ASM v0.4
ASSEMBLER / DISASSEMBLER

A XXXX=ASM  D XXXX=DISASM  L=LOAD  B=BUILD  ESC=EXIT
>
```

The `>` is the command prompt. Type a command and press Enter. Press **ESC** on an
empty line to unmap the module and return to DOS. (`BANKS` at the `]` prompt lists
the available ROM modules.)

## Commands

Each command is chosen by the first letter of the line (uppercase or lowercase).

| Command | Action |
|---------|--------|
| `D xxxx` | Disassemble from hex address `xxxx` |
| `A xxxx` | Line assembler: enter instructions starting at `xxxx` |
| `L` | Load a source file from the host (open dialog) |
| `B` | Build (two-pass assemble the loaded source into memory) |
| ESC | Exit to the monitor (on an empty line) |

An unrecognized command prints `?`.

### `D xxxx` — Disassemble

Decodes and prints one screenful (16 instructions) starting at address `xxxx`.
Each line shows the address, the raw instruction bytes, the mnemonic, and the
operand:

```
>D E000
E000: A2 FF     LDX #$FF
E002: 9A        TXS
E003: D8        CLD
E004: 20 30 E1  JSR $E130
...
```

Run `D` again with the address the listing stopped at to continue.

### `A xxxx` — Line assembler

An immediate, one-instruction-per-line assembler for quick patches and short
routines. It prompts with the current address; type an instruction and press
Enter, and the bytes are written to memory and the address advances:

```
>A 0800
0800: LDA #$05
0802: CLC
0803: ADC #$03
0804: JSR $FF00
0807: RTS
0808:
```

Press **Enter on an empty line** (or **ESC**) to leave the line assembler. A bad
line prints `?` and re-prompts at the same address so you can retype it.

The line assembler takes **numeric operands only** — no labels or expressions.
Use hex with a leading `$`, e.g. `LDA #$41`, `STA $D020`, `JSR $FF00`,
`BNE $0800`. It picks zero-page vs. absolute automatically: a value that fits in
one byte (`$00`–`$FF`) uses the zero-page form when the instruction has one;
anything larger forces absolute. Branch targets are the **destination address**
(e.g. `BNE $0810`); the module computes the relative offset and reports an error
if the target is out of the ±128-byte range.

### `L` — Load source

Loads a text source file into the assembler's source buffer so `B` can build it.
The host shows a file open dialog; pick a `.asm`/`.s` file and it is read into
memory. On success you see:

```
>L
LOADED
```

If you cancel the dialog or the file is too large, it prints `?`. The source
buffer holds just under 4 KB of text.

Source authoring happens on the host for now — write the file on your computer,
then `L` to load and `B` to build it.

### `B` — Build

Two-pass assembles the loaded source into memory. Pass 1 collects labels and
sizes the code; pass 2 emits the bytes and prints a listing (each line's address
followed by the source text). It finishes with `OK`:

```
>B
0800: START:
0800:     LDA #$05
0802:     CLC
0803:     ADC #$03
...
0810:     RTS
OK
>
```

If a line won't assemble, the build stops and reports the line number in hex:

```
? LINE 0007
```

Fix that line in your source on the host, `L` again, and `B`.

## Writing source for `B`

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
letters, digits, and underscores, and are up to 8 characters long.

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
| `.BYTE v[,v…]` | `.DB` | Emit one or more bytes |
| `.WORD v[,v…]` | `.DW` | Emit 16-bit words (low byte first) |
| `.ASCII "text"` | `.TX` | Emit the bytes of a quoted string |

Directive names are case-insensitive (`.org`, `.byte`, and `.ORG`, `.BYTE` are
equivalent). Text inside a `"…"` string keeps its original case.

If a file has no `.ORG` (or `*=`), assembly starts at **$0000**. Set an origin in
user RAM so your program lands where you can run it — `$0800` is the usual choice.

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

Build it with `B`, then exit (ESC), enter the monitor with `MON`, and run it with
`G:0800`.

## Running what you assembled

Both the line assembler and `B` write real machine code into memory. To run it:

1. Exit the module with **ESC** (returns to the DOS `]` prompt).
2. Enter the monitor with **`MON`**, then use **`G:addr`** (e.g. `G:0800`) to
   execute from your program's start address.

End your program with `RTS` to return cleanly to the monitor, and use `JSR $FF00`
(`K_PRINT_CHAR`) and the other `$FF00` kernel routines for I/O.

## Memory used

The Dev Tools share the machine with everything else, so mind what they touch:

- **Source buffer:** `$8000–$8FFF` (the text loaded by `L`).
- **Symbol table:** `$7E00–$7FFF` (labels and constants; up to 51 symbols).
- **Working RAM:** `$0800–$8FFF` is the module's scratch space.

Assemble your programs into free user RAM below `$7E00` (for example `$0800`),
clear of the source buffer and symbol table. Only one module is mapped at a time —
save your work on the host before switching banks, since the buffers are not
preserved across module loads.

## Example programs

The repository's `examples/` directory has ready-to-load `.asm` sources —
`add_print.asm`, `hello_world.asm`, `count_loop.asm`, `colors.asm`,
`multiply.asm`, and more. Load one with `L`, build it with `B`, then run it from
the monitor with `G:` at the program's origin.
