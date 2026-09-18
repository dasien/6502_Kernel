# BASIC — MFC BASIC Manual

MFC BASIC is the built-in BASIC interpreter, derived from EhBASIC, which is Enhanced
6502 BASIC by the late Lee Davison. It runs as a bank-switched ROM module in the
`$B000-$EFFF` window, so it loads instantly with no disk access. It has full floating
point, string handling, arrays, and the usual BASIC statements and functions. Anything
an EhBASIC reference documents works here.

## Quick reference

| At the `]` prompt | Action |
|-------------------|--------|
| `BASIC` | Launch MFC BASIC |
| `BANKS` | List ROM modules (BASIC is bank 1) |

| In BASIC | Action |
|----------|--------|
| `RUN` | Run the program (`RUN n` from line n) |
| `LIST` | List the program (`LIST a-b` for a range) |
| `NEW` | Erase the program in memory |
| `SAVE` | Save as a host `.bas` file (macOS Save dialog) |
| `LOAD` | Load a host `.bas` file (macOS Open dialog) |
| `RND(1)` | Next random number, 0 to 1 |
| `BYE` | Exit BASIC, return to the DOS `]` prompt |

MFC BASIC is derived from EhBASIC by Lee Davison. Any EhBASIC reference describes the
complete language.

## Starting

From the DOS `]` prompt, type its name:

```
]BASIC
```

The DOS launches the BASIC module by name. `BANKS` lists the ROM modules, and BASIC is
bank 1. BASIC clears the screen and shows its sign-on followed by the ready prompt.

```
Ready
.
```

The `.`, or the blank line after `Ready`, is where you type. Nothing needs a line
number to run interactively. Type a statement and press Enter to run it immediately,
or prefix it with a line number to store it as part of a program.

## Writing and running a program

Type numbered lines to build a program, then `RUN` it:

```
10 FOR I = 1 TO 5
20 PRINT I, I * I
30 NEXT I
RUN
```

Useful editing commands at the `Ready` prompt:

- `LIST` shows the program, and `LIST 10-30` lists a range of it.
- `RUN` runs from the first line, while `RUN 100` starts at line 100.
- `NEW` erases the program from memory.
- Retyping a line number with new text replaces that line, and typing the line number
  on its own deletes it.

Because MFC BASIC has floating point, decimals and math functions work normally:

```
PRINT SQR(2), SIN(3.14159), 1/3
```

## LOAD and SAVE — host `.bas` files

`LOAD` and `SAVE` use the host macOS file picker rather than the FAT16 disk. They read
and write plain-text `.bas` files on your Mac, and those are host files, separate from
the disk that the DOS `CATALOG`, `TYPE` and `COPY` commands see.

- `SAVE` opens a macOS Save dialog. Choose a name and a location, and BASIC writes the
  current program out as ASCII source text.
- `LOAD` opens a macOS Open dialog. Pick a `.bas` file and BASIC reads it back in line
  by line, exactly as though you had typed it. That means it merges into whatever is
  already in memory, so run `NEW` first if you want a clean load.

If you cancel the dialog, or it fails, BASIC reports `ERROR?` and returns to the
`Ready` prompt. Neither command takes a filename argument, because the picker chooses
the file.

> To move a BASIC program onto the FAT16 disk, save it as `.bas` on the host and then
> bring it in through the DOS file tools separately.

## Random numbers — `RND`

`RND` returns a floating-point value. As in EhBASIC:

- `RND(1)` (any positive argument) returns the next pseudo-random number in the
  range 0 to 1.
- Use it in expressions, e.g. a dice roll: `PRINT INT(RND(1) * 6) + 1`.

The generator is a fixed sequence, so a program that only uses `RND` produces the
same numbers each session unless you re-seed it (see an EhBASIC reference for the
`RND` seeding conventions).

## Returning to DOS — `BYE`

Type `BYE` to leave BASIC:

```
BYE
```

`BYE` exits the BASIC module, unmapping the ROM bank, and returns you to the `]`
prompt. Your program is not preserved across a `BYE`, so `SAVE` it first if you want
to keep it.

## Compatibility

MFC BASIC is EhBASIC under the hood, so standard **EhBASIC documentation
applies** for the full statement and function set (`PRINT`, `INPUT`, `IF/THEN`,
`FOR/NEXT`, `GOSUB/RETURN`, `DIM`, string functions, math functions, `PEEK`/
`POKE`, and so on). The MFC-specific differences are just the three things above:
how you launch it (`BASIC` from DOS), how `LOAD`/`SAVE` work (host `.bas`
picker), and how you exit (`BYE`).
