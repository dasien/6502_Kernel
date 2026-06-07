# TODO List

- [x] Z: & T: commands are updating the current address to 00FF and 01FF respectively and they shouldn't.
- [x] Fix BASIC token parsing (e.g. enter 10 FOR I = 1 TO 10) and that is not what prints when you LIST
- [ ] Emulator: 65C02 bare "(zp)" indirect opcodes ($12/$32/$52/$72/$92/$B2/$D2/$F2) are decoded as 2-byte absolute-indirect instead of 1-byte zero-page-indirect in CPU6502 (calculateAbsoluteIndirectAddress). Dormant: the EhBASIC ROM doesn't use these forms, so it's unexercised today, but it's a latent correctness gap for other 65C02 code.

## Deferred from the kernel/BASIC deep scan (2026-06)

### BASIC integration fixes
- [x] LOAD/SAVE I/O vectors (PG2_TABS) pointed at $FF0F = the RNG routine. Resolved by implementing real BASIC SAVE/LOAD: SAVE writes the program as ASCII .bas text and LOAD reads it back (via a new byte-stream mode on the PIA file I/O). VEC_SV/VEC_LD now point at BASIC_SAVE/BASIC_LOAD, so the RNG bug is gone.
- [ ] INIT_BASIC_IO (kernel.asm:1894) is dead code: cold-start's PG2_TABS copy overwrites the vectors it sets. Delete it and treat PG2_TABS as the single source of truth (or keep them in sync with a cross-reference comment).
- [ ] IRQ/NMI/RETIRQ/RETNMI machinery is inert: the kernel ISRs are bare RTI and never set BASIC's "happened" bit, so these tokens can't fire from hardware. Either implement (kernel ISRs set the bit) or strip the feature.

### BASIC label rewrite
- [ ] Rename cryptic basic.asm labels (~570 LAB_<hexaddr> labels with no semantic content; meaning already exists in the block comment above each). Recommended approach from the analysis: (B) add a symbol-glossary header + (D) ca65 aliases (NEW_NAME = OLD_NAME) for the hottest labels, keeping LAB_<hex> as the canonical name to preserve EhBASIC upstream parity; optionally (C) targeted rename of the ~20-40 most-referenced labels. Avoid a full rename (A) unless we accept a permanent upstream fork. MUST gate any change on a byte-identical ROM diff. Special-care subset: the ".word LAB_xxx-1" RTS-dispatch tables, the LAB_2A9A/B/C "+1" chains, and LAB_1C18p2 = LAB_1C18+2.

### Kernel code-quality refactors (ROM has ~4KB free; these are maintainability, not space)
- [ ] Factor duplicated idioms: a PRINT_HEX_BYTE helper (~5 sites), a message-print helper (~10 inline copies of the set-MON_MSG_PTR/JSR PRINT_MESSAGE block), and a shared SKIP_SPACES/EXPECT_COMMA parser preamble (duplicated across ~6 parsers — the biggest maintenance drag).
- [ ] Remove dead code: NIBBLE_TO_HEX_CHAR/NIBBLE_DIGIT (unreferenced), unused constants, and the MOVE "copy vs move" branch that prints identical text (the documented "COPIED/MOVED N BYTES" output was never implemented — implement or drop).

### Documentation
- [x] docs/kernel_memory_map.md and the kernel.asm header rewritten to match the actual system ($E000 ROM, $14-$39 monitor ZP, relocated page-2 vars, PIA I/O, no C64 banking/VIC/SID). DEC_DIGIT_BUFFER now defined as "= MON_SEARCH_PATTERN" instead of a literal.