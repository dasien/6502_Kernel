# FORTH — Forth Manual

**FORTH** is a port of the public-domain **fig-FORTH 6502 model** (Forth Interest
Group, W. F. Ragsdale Release 1.1) running as an MFC bank-switched module. It gives
you an interactive Forth interpreter/compiler: a stack machine you drive by typing
words. Standard fig-FORTH vocabulary applies — the FIG glossary is the reference.

## Quick reference

| Input | Effect |
|-------|--------|
| `FORTH` (at DOS `]`) | Launch the FORTH module |
| `n m + .` | Push `n`, `m`, add, print result |
| `: NAME … ;` | Define a new word |
| `.S` | Show the data stack (non-destructive) |
| `VLIST` / `WORDS` | List the dictionary |
| `HEX` / `DECIMAL` | Set the number base |
| `MON` | Quit back to DOS |

## Starting

FORTH lives in **module bank 3**. Launch it from the DOS `]` prompt by name:

```
FORTH
```

It maps in the module and prints its sign-on banner:

```
MFC FORTH   (FIG-FORTH)   MON=QUIT
```

`FORTH` also appears in the `BANKS` listing alongside the other ROM modules.

## Using it

FORTH reads a line at a time. Type words separated by spaces and press Enter; it
interprets the line, then prints ` OK` when it finishes without error. Numbers are
pushed onto the data stack; words operate on that stack.

The default number base is **decimal**. `.` prints (and removes) the top of the
stack.

```
2 3 + .        prints: 5 OK
10 20 * .      prints: 200 OK
```

Define your own words with `:` … `;` and run them:

```
: SQUARE  DUP * ;      OK
5 SQUARE .             prints: 25 OK
```

Some other everyday words: `DUP DROP SWAP OVER` (stack), `.S` (show the stack),
`WORDS` or `VLIST` (list the dictionary), `HEX` / `DECIMAL` (change base),
`VARIABLE` and `CONSTANT`. See the fig-FORTH glossary for the full set.

If a line has an error, FORTH reports it and returns to the prompt; fix the line
and retype it.

## Quitting

Type the word:

```
MON
```

**MON** exits FORTH and returns you to the DOS `]` prompt (it unmaps the module).
It is the top word in the dictionary, as shown by the `MON=QUIT` reminder in the
banner.

## Notes

- FORTH shares user RAM (`$0800`–) with the other modules, so run one tool at a
  time and save any work before switching. Words you define exist only for the
  current session — they are gone once you exit or relaunch.
- This is a faithful fig-FORTH port. Behavior, words, and error messages match the
  FIG model; there are no MFC-specific Forth words to learn beyond `MON` for exit.
