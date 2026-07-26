# MONITOR — Machine-Language Monitor Manual

The **monitor** is MFC's low-level machine-language console: examine and change
memory, run code, transfer files, and convert numbers. It's where the system
began, and it's still the fastest way to poke at the machine directly.

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
- Errors: `ERROR?` (bad syntax), `RANGE?` (end before start), `VALUE?` (bad hex/value).

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

**`X:` — Search.** `X:start-end,pattern` searches for a 1–16-byte hex pattern
(space-separated), printing each match address; paged, ESC aborts.

## Program commands

**`G:` — Go / run.** `G:xxxx` executes code at `xxxx`. A program returns to the
monitor with `RTS`.

**`L:` — Load a file.** `L:xxxx` loads a binary into memory at `xxxx`; the host
shows a file-picker dialog to choose the file.

**`S:` — Save a range.** `S:start-end` saves that memory range to a file via the
host save dialog.

## Number conversion

**`D:` — Decimal → hex.** `D:nnnnn` (0–65535) prints the value with a `$` prefix.

**`H:` — Hex → decimal.** `H:xxxx` (0000–FFFF) prints the value with a `#` prefix.

## Display commands

**`C:` — Clear screen.**

**`T:` — Stack dump.** Pages through the stack page `$0100-$01FF`.

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
- `S:` a working range before experimenting, so you can `L:` it back.
- `T:`/`Z:` are quick windows into stack and zero-page state while debugging.
- `.` saves retyping when iterating on the same command.

## Quick reference

| Command | Action |
|---------|--------|
| `R:xxxx[-yyyy]` | Read / dump memory |
| `W:xxxx [bb …]` | Write mode / inline write |
| `F:start-end,bb` | Fill range with a byte |
| `M:start-end,dest,mode` | Move (1) or copy (0) a block |
| `X:start-end,pat` | Search for a byte pattern |
| `G:xxxx` | Run code at xxxx |
| `L:xxxx` / `S:start-end` | Load / save a file (host dialog) |
| `D:nnnnn` / `H:xxxx` | Decimal↔hex conversion |
| `C:` `T:` `Z:` | Clear / stack dump / zero-page dump |
| `?` `.` `ESC` `Q` | Help / recall / exit-abort / quit to DOS |
