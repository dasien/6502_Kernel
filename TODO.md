# TODO List

- [x] Z: & T: commands are updating the current address to 00FF and 01FF respectively and they shouldn't.
- [x] Fix BASIC token parsing (e.g. enter 10 FOR I = 1 TO 10) and that is not what prints when you LIST
## CPU emulation accuracy (surfaced by the Klaus2m5 / amb5l functional tests, 2026-06)

These are dormant fidelity gaps — none affect software we actually run (EhBASIC is NMOS-only and doesn't use CPU decimal mode; the monitor's D:/H: are software conversion). The NMOS functional test passes fully (incl. BCD accumulator+carry results) after the BRK fix.

- [ ] Decimal-mode N/V/Z flags: ADC/SBC in decimal mode produce the correct accumulator and carry (verified exact vs. the amb5l 6502_decimal_test, including invalid-BCD inputs), but the N/V/Z flags don't match the NMOS quirk behavior, so the full decimal test trips its flag checks (ERROR=1; A+C-only checks pass). Low priority: these flags are officially "undefined" after a decimal op on NMOS and are unused by our software.
- [ ] Complete the 65C02 opcode set in CPU6502. The amb5l 65C02_extended_opcodes_test halts immediately on UNKNOWN OPCODE $0F (RMB0): our CPU is only a partial 65C02 — missing the Rockwell/WDC bit ops (RMB/SMB/BBR/BBS) and others. Includes the known bare-"(zp)" indirect gap below.
  - Bare "(zp)" indirect opcodes ($12/$32/$52/$72/$92/$B2/$D2/$F2) are decoded as 2-byte absolute-indirect instead of 1-byte zero-page-indirect (calculateAbsoluteIndirectAddress). Unexercised by EhBASIC today.
- [ ] (Optional) Interrupt test: not available in the amb5l ca65 port (as65 source only) and uses a memory-mapped IRQ/NMI feedback register, so it'd need an as65 build (or manual ca65 port) plus a harness extension to assert the IRQ/NMI lines. Would validate the v2.2 IRQ/NMI path.

## Deferred from the kernel/BASIC deep scan (2026-06)

### BASIC integration fixes
- [x] LOAD/SAVE I/O vectors (PG2_TABS) pointed at $FF0F = the RNG routine. Resolved by implementing real BASIC SAVE/LOAD: SAVE writes the program as ASCII .bas text and LOAD reads it back (via a new byte-stream mode on the PIA file I/O). VEC_SV/VEC_LD now point at BASIC_SAVE/BASIC_LOAD, so the RNG bug is gone.
- [x] INIT_BASIC_IO removed (dead code); PG2_TABS is the single source of truth for the BASIC I/O vectors.
- [x] IRQ/NMI wired (v2.2): CPU IRQ/NMI dispatch + a ~60Hz PIA interval timer (BASIC ON IRQ) + NMI stop key (BASIC ON NMI / break to monitor). The kernel ISRs set EhBASIC's "happened" bit.

### BASIC label rewrite
- [ ] Rename cryptic basic.asm labels (~570 LAB_<hexaddr> labels with no semantic content; meaning already exists in the block comment above each). Recommended approach from the analysis: (B) add a symbol-glossary header + (D) ca65 aliases (NEW_NAME = OLD_NAME) for the hottest labels, keeping LAB_<hex> as the canonical name to preserve EhBASIC upstream parity; optionally (C) targeted rename of the ~20-40 most-referenced labels. Avoid a full rename (A) unless we accept a permanent upstream fork. MUST gate any change on a byte-identical ROM diff. Special-care subset: the ".word LAB_xxx-1" RTS-dispatch tables, the LAB_2A9A/B/C "+1" chains, and LAB_1C18p2 = LAB_1C18+2.

### Kernel code-quality refactors (ROM has ~4KB free; these are maintainability, not space)
- [ ] Factor duplicated idioms: a PRINT_HEX_BYTE helper (~5 sites), a message-print helper (~10 inline copies of the set-MON_MSG_PTR/JSR PRINT_MESSAGE block), and a shared SKIP_SPACES/EXPECT_COMMA parser preamble (duplicated across ~6 parsers — the biggest maintenance drag).
- [ ] Remove dead code: NIBBLE_TO_HEX_CHAR/NIBBLE_DIGIT (unreferenced), unused constants, and the MOVE "copy vs move" branch that prints identical text (the documented "COPIED/MOVED N BYTES" output was never implemented — implement or drop).

### Documentation
- [x] docs/kernel_memory_map.md and the kernel.asm header rewritten to match the actual system ($E000 ROM, $14-$39 monitor ZP, relocated page-2 vars, PIA I/O, no C64 banking/VIC/SID). DEC_DIGIT_BUFFER now defined as "= MON_SEARCH_PATTERN" instead of a literal.