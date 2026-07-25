# MFC 6502 assembly examples

Small, self-contained 6502 programs for learning to program MFC in assembly.
Every program loads and runs at **$0800** and ends with `RTS` to return to the
monitor. Each file has commented source plus a verified "Enter these bytes" block
(all byte blocks here are confirmed byte-identical to a real `ca65` assembly).

## Two ways to run an example

**A. Type the bytes into the monitor** (works for every example):

```
]MON              ; enter the monitor
W:0800            ; write mode at $0800 -- then type the byte block, Enter on a blank line
G:0800            ; run it
```

**B. Assemble the source in the built-in assembler** (for the `.asm` source):

```
]ASM              ; launch the assembler module
L                 ; Load -- pick the .asm file in the host dialog
B                 ; Build to memory (should assemble with no "? LINE" error)
<ESC>             ; back to DOS
]MON              ; enter the monitor
G:0800            ; run it
```

The assembler is case-insensitive and understands `.ORG`/`*=`, `.END`,
`.BYTE`/`.DB`, `.WORD`/`.DW`, `.ASCII`/`.TX`, labels, and the `<`/`>` byte
selectors.

## The examples (easiest first)

| File | Teaches | Kernel calls used |
|------|---------|-------------------|
| `add_print.asm` | add two numbers, print one digit | `K_PRINT_CHAR` |
| `hello_world.asm` | print strings, wait for a key, echo it | `K_PRINT_MESSAGE`, `K_GET_KEYSTROKE`, `K_PRINT_CHAR` |
| `count_loop.asm` | a counter loop; print numbers in decimal | `K_PRINT_DEC` |
| `typewriter.asm` | a non-blocking input loop (echo until ESC) | `K_GET_KEYSTROKE`, `K_PRINT_CHAR` |
| `colors.asm` | the color/attribute byte | `K_SET_ATTR`, `K_PRINT_MESSAGE` |
| `memdump.asm` | indexed addressing; print bytes as hex | `K_PRINT_HEX_BYTE` |
| `lowercase_test.asm` | assembler case-folding (lowercase + `_`) | `K_PRINT_CHAR` |
| `multiply.asm` | 8x8 multiply by shift-and-add | `K_PRINT_DEC` |
| `times_table.asm` | nested loops (a 9x9 table) | `K_PRINT_DEC` |
| `random_numbers.asm` | the RNG; print numbers in decimal | `K_GET_RAND_NUM`, `K_PRINT_DEC` |
| `guess.asm` | a game: random + read + parse + compare | `K_GET_RAND_NUM`, `K_READ_LINE`, `K_PARSE_DEC` |
| `sid_filter_sweep.asm` | SID sound + polling the keyboard | SID/PIA registers direct |

## Kernel ABI quick reference

The kernel exposes a stable jump table at `$FF00`; call these with `JSR`:

| Address | Name | Contract |
|---------|------|----------|
| `$FF00` | `K_PRINT_CHAR` | print `A` as a character (preserves X, Y) |
| `$FF03` | `K_PRINT_MESSAGE` | print the null-terminated string pointed to by `$16/$17` (`MON_MSG_PTR`) |
| `$FF06` | `K_PRINT_NEWLINE` | print CR/LF |
| `$FF09` | `K_GET_KEYSTROKE` | non-blocking: carry SET + `A` = key when one is waiting |
| `$FF0C` | `K_CLEAR_SCREEN` | clear the screen, home the cursor |
| `$FF0F` | `K_GET_RAND_NUM` | `A` = random `1..RNG_MAX` (set `RNG_MAX` at `$24` first) |
| `$FF15` | `K_READ_LINE` | read an edited line into `MON_CMDBUF` ($0200); `A` = length |
| `$FF1B` | `K_PRINT_HEX_BYTE` | print `A` as two hex digits |
| `$FF27` | `K_PRINT_DEC` | print a 32-bit value: `A`/`X` = pointer to 4 little-endian bytes, `Y` = field width |
| `$FF2A` | `K_PARSE_DEC` | parse a decimal number from `MON_CMDBUF` at offset `X`; result in `$14/$15`, carry SET if invalid |
| `$FF2D` | `K_SET_ATTR` | latch `A` as the color/attribute for following characters |

**Attribute byte** (for `K_SET_ATTR`): bit 7 = reverse, bit 6 = bright,
bits 5-3 = background (0-7), bits 2-0 = foreground (0-7). Colors: 0 black,
1 red, 2 green, 3 yellow, 4 blue, 5 magenta, 6 cyan, 7 white. Default is `$02`
(green on black).

Useful zero-page locations the examples use: `$14/$15` `MON_CURRADDR` (also
`K_PARSE_DEC`'s result), `$16/$17` `MON_MSG_PTR` (string pointer for
`K_PRINT_MESSAGE`), `$24` `RNG_MAX`.
