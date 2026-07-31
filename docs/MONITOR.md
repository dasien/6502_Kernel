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
| `D:nnnnn` / `H:xxxx` | Decimal↔hex conversion |
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

**`D:` — Decimal → hex.** `D:nnnnn` (0–65535) prints the value with a `$` prefix.

**`H:` — Hex → decimal.** `H:xxxx` (0000–FFFF) prints the value with a `#` prefix.

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

## Running the ROM modules

The monitor has no module menu (the old `B:` command was retired). The BASIC,
assembler (`ASM`), and FORTH modules launch **by name from the DOS `]` prompt** —
press `Q` to return to DOS, then type `BASIC`, `ASM`, or `FORTH`; DOS `BANKS` lists
the module catalog. See `BASIC.md`, `ASSEMBLER.md`, `FORTH.md`.

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
