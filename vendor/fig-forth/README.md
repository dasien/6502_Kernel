# fig-FORTH 6502 — source of the MFC `FORTH` module (bank 3)

This directory holds the pristine upstream listing of **fig-FORTH for the 6502,
Release 1.1**, translated from the FIG model by **William F. Ragsdale** and
distributed by the **FORTH Interest Group (FIG)**. The FIG model is public
domain ("Further distribution must include the above notice" — see the header
of the listing). It is preserved here unmodified so its authorship and origin
stay clear.

## Files

- `figforth-6502-r1.1.lst.txt` — the original assembler listing, verbatim
  (LINE#/LOC/CODE/source columns, `ERRORS=0000`). The object bytes in the CODE
  column make a byte-exact verification possible.
- `reconstruct.py` — rebuilds the reference binary (`figforth-ref.bin`) straight
  from the listing's LOC/CODE columns: `$0200–$1A73`, 6260 bytes, one 2-byte gap
  at `$0222` (the documented `*=*+2` code-field offset).
- `convert.py` — converts the listing's source column to ca65 syntax (adds `;`
  to trailing comments, expands single-quoted strings to per-char constants,
  maps `*=`/`*=*+2`, drops `.FILE`/`.END`). Output: `figforth.s`.
- `check.sh` — assembles `figforth.s` at `$0200` and diffs against
  `figforth-ref.bin`. **Result: BYTE-IDENTICAL** — proof the transcription is
  faithful before any relocation.
- `make_module.py` — applies the minimal, size-preserving patches that turn the
  verified `figforth.s` into the bank module `src/kernel/forth/forth.s`:
    * origin `$0200 → $B000`;
    * initial FENCE/DP → `$0802` (new words compile to RAM, not ROM);
    * NOP the two single-step debug calls (`JSR TRACE`, `JSR TCOLON`);
    * `XQTER` (`?TERMINAL`) → push 0 (no break port on this host);
    * `MON` word → `JMP $FF12` (kernel `RETURN_FROM_MODULE`, exit to the DOS);
    * `OUTCH/INCH/TCR` → thunks to the kernel `$FF00` jump table;
    * ROMable-vocabulary fix: the FORTH vocabulary's "latest word" thread cell
      is relocated to RAM (`FORTHLATEST = $0800`) via a private `DOVOCF`
      does-routine, so `:` definitions link and un-smudge correctly. (User
      vocabularies are unaffected — their thread cell is already in RAM.)

## Reproduce

```sh
python3 reconstruct.py figforth-6502-r1.1.lst.txt figforth-ref.bin
python3 convert.py     figforth-6502-r1.1.lst.txt figforth.s   # listing -> ca65
./check.sh                                                     # -> BYTE-IDENTICAL
python3 make_module.py figforth.s ../../src/kernel/forth/forth.s
```

`forth.s` is checked in under `src/kernel/forth/` and built into `forth.rom`
by CMake (`forth_rom` target). The generated intermediates here
(`figforth.s`, `figforth-src-raw.txt`, `figforth-ref.bin`, `*.o`, `*.bin`,
`*.map`) are build artifacts and are git-ignored.

## Known limitation

Disk block words (`BLOCK`, `R/W`, `-DISC`, screens) are stubbed — there is no
System-65 floppy here. The boot "no disk" flag is set. Interactive use and
`INCLUDE`-from-text-file (planned) do not need them.
