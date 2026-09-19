# FORTH — Forth Manual

FORTH is a port of the public-domain fig-FORTH 6502 model, from the Forth Interest
Group, W. F. Ragsdale Release 1.1. It runs as an MFC bank-switched module and gives you
an interactive Forth interpreter and compiler. It is a stack machine that you drive by
typing words. The standard fig-FORTH vocabulary applies, so the FIG glossary is the
reference to reach for.

## Quick reference

| Input | Effect |
|-------|--------|
| `FORTH` (at DOS `]`) | Launch the FORTH module |
| `n m + .` | Push `n`, `m`, add, print result |
| `: NAME … ;` | Define a new word |
| `VLIST` / `WORDS` | List the dictionary |
| `HEX` / `DECIMAL` | Set the number base |
| `MON` | Quit back to DOS |

## Starting

FORTH lives in module bank 3. Launch it from the DOS `]` prompt by name:

```
FORTH
```

It maps in the module and prints its sign-on banner:

```
MFC FORTH   (FIG-FORTH)   MON=QUIT
```

`FORTH` also appears in the `BANKS` listing alongside the other ROM modules.

## Using it

FORTH reads a line at a time. Type words separated by spaces and press Enter. It
interprets the line and then prints ` OK` if it finished without an error. Numbers are
pushed onto the data stack, and words operate on what is there.

The default number base is decimal. `.` prints (and removes) the top of the
stack.

```
2 3 + .        prints: 5 OK
10 20 * .      prints: 200 OK
```

Define your own words between `:` and `;`, then run them.

```
: SQUARE  DUP * ;      OK
5 SQUARE .             prints: 25 OK
```

Other everyday words include `DUP`, `DROP`, `SWAP` and `OVER` for the stack,
`VLIST` to list the dictionary, `HEX` and `DECIMAL` to
change base, and `VARIABLE` and `CONSTANT`. The fig-FORTH glossary has the full set.

If a line has an error, FORTH reports it and returns to the prompt, so you can fix the
line and retype it.

## Quitting

Type the word:

```
MON
```

`MON` exits FORTH and returns you to the DOS `]` prompt, unmapping the module as it
goes. It is the top word in the dictionary, which is what the `MON=QUIT` reminder in
the banner is telling you.

## Notes

- FORTH shares user RAM from `$0800` upward with the other modules, so run one tool at
  a time and save any work before switching. Words you define exist only for the
  current session and are gone once you exit or relaunch.
- This is a faithful fig-FORTH port. The behaviour, the words and the error messages
  all match the FIG model, and there are no MFC-specific Forth words to learn apart
  from `MON` to exit.
