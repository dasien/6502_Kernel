# TODO List

- [x] Z: & T: commands are updating the current address to 00FF and 01FF respectively and they shouldn't.
- [x] Fix BASIC token parsing (e.g. enter 10 FOR I = 1 TO 10) and that is not what prints when you LIST
## CPU emulation accuracy (surfaced by the Klaus2m5 / amb5l functional tests, 2026-06)

The emulated CPU is now a full **WDC W65C02S**. Validated against all three amb5l ca65 ports (kept local, GPL, never committed): 6502_functional_test ($3469), 6502_decimal_test built for 65C02 (ERROR=0, incl. invalid BCD), and 65C02_extended_opcodes_test with wdc_op/rkwl_wdc_op ($24F1). Locked by unit tests in tests/test_cpu_alu.cpp.

- [x] Decimal-mode N/V/Z flags: 65C02 ADC/SBC now set N/V/Z validly. addValues/subtractValues are faithful ports of the documented hardware algorithm, matching a real W65C02S even for invalid BCD inputs.
- [x] Complete the 65C02 opcode set in CPU6502: added RMB/SMB/BBR/BBS (Rockwell/WDC), the standard multi-byte NOP opcodes, BRK clearing the decimal flag, and JMP-indirect WDC timing. WAI/STP remain benign stubs (not exercised by the test).
  - [x] Bare "(zp)" indirect opcodes ($12/$32/$52/$72/$92/$B2/$D2/$F2) now decode as 1-byte zero-page-indirect (calculateZeroPageIndirectAddress), not 2-byte absolute-indirect.
- [ ] (Optional) Interrupt test: not available in the amb5l ca65 port (as65 source only) and uses a memory-mapped IRQ/NMI feedback register, so it'd need an as65 build (or manual ca65 port) plus a harness extension to assert the IRQ/NMI lines. Would validate the v2.2 IRQ/NMI path.

## Deferred from the kernel/BASIC deep scan (2026-06)

### BASIC integration fixes
- [x] LOAD/SAVE I/O vectors (PG2_TABS) pointed at $FF0F = the RNG routine. Resolved by implementing real BASIC SAVE/LOAD: SAVE writes the program as ASCII .bas text and LOAD reads it back (via a new byte-stream mode on the PIA file I/O). VEC_SV/VEC_LD now point at BASIC_SAVE/BASIC_LOAD, so the RNG bug is gone.
- [x] INIT_BASIC_IO removed (dead code); PG2_TABS is the single source of truth for the BASIC I/O vectors.
- [x] IRQ/NMI wired (v2.2): CPU IRQ/NMI dispatch + a ~60Hz PIA interval timer (BASIC ON IRQ) + NMI stop key (BASIC ON NMI / break to monitor). The kernel ISRs set EhBASIC's "happened" bit.

### BASIC label rewrite
- [ ] Rename cryptic basic.asm labels (~570 LAB_<hexaddr> labels with no semantic content; meaning already exists in the block comment above each). Recommended approach from the analysis: (B) add a symbol-glossary header + (D) ca65 aliases (NEW_NAME = OLD_NAME) for the hottest labels, keeping LAB_<hex> as the canonical name to preserve EhBASIC upstream parity; optionally (C) targeted rename of the ~20-40 most-referenced labels. Avoid a full rename (A) unless we accept a permanent upstream fork. MUST gate any change on a byte-identical ROM diff. Special-care subset: the ".word LAB_xxx-1" RTS-dispatch tables, the LAB_2A9A/B/C "+1" chains, and LAB_1C18p2 = LAB_1C18+2.

### Kernel code-quality refactors (ROM has ~4KB free; these are maintainability, not space)
- [x] Factor duplicated idioms: added PRINT_HEX_BYTE (byte->2 hex digits to screen), PRINT_MSG_AY (set MON_MSG_PTR from A/Y and print, replacing 13 inline copies), and shared SKIP_SPACES/EXPECT_COMMA parser helpers (replacing the skip-spaces/comma preamble duplicated across the F:, M: (x2), X:, and L:/S: filename parsers). CODE segment dropped from ~4185 to 3946 bytes; all tests pass.
- [x] Remove dead code: deleted unreferenced NIBBLE_TO_HEX_CHAR/NIBBLE_DIGIT, unused constants (MON_HEX_DIGITS, CURSOR_CHAR, ASCII_0/9/A/F, FILE_IDLE, FILE_ERROR), and the MOVE copy-vs-move branch that printed identical text. HELP_MSG_COUNT was kept and wired into the help loop (replacing a magic #30) rather than deleted.

### Documentation
- [x] docs/kernel_memory_map.md and the kernel.asm header rewritten to match the actual system ($E000 ROM, $14-$39 monitor ZP, relocated page-2 vars, PIA I/O, no C64 banking/VIC/SID). DEC_DIGIT_BUFFER now defined as "= MON_SEARCH_PATTERN" instead of a literal.