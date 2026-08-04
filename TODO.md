# TODO List

- [x] Z: & T: commands are updating the current address to 00FF and 01FF respectively and they shouldn't.
- [x] Fix BASIC token parsing (e.g. enter 10 FOR I = 1 TO 10) and that is not what prints when you LIST
## CPU emulation accuracy (surfaced by the Klaus2m5 / amb5l functional tests, 2026-06)

The emulated CPU is now a full **WDC W65C02S**. Validated against all three amb5l ca65 ports (kept local, GPL, never committed): 6502_functional_test ($3469), 6502_decimal_test built for 65C02 (ERROR=0, incl. invalid BCD), and 65C02_extended_opcodes_test with wdc_op/rkwl_wdc_op ($24F1). Locked by unit tests in tests/test_cpu_alu.cpp.

- [x] Decimal-mode N/V/Z flags: 65C02 ADC/SBC now set N/V/Z validly. addValues/subtractValues are faithful ports of the documented hardware algorithm, matching a real W65C02S even for invalid BCD inputs.
- [x] Complete the 65C02 opcode set in CPU6502: added RMB/SMB/BBR/BBS (Rockwell/WDC), the standard multi-byte NOP opcodes, BRK clearing the decimal flag, and JMP-indirect WDC timing.
  - [x] WAI/STP are no longer benign stubs (they used to decode and fall through, so a program using either just ran on). WAI now halts until IRQ/NMI is signalled — including a *masked* IRQ, which resumes without vectoring — and STP halts until reset. Covered by tests/test_cpu_interrupts.cpp.
  - [x] Bare "(zp)" indirect opcodes ($12/$32/$52/$72/$92/$B2/$D2/$F2) now decode as 1-byte zero-page-indirect (calculateZeroPageIndirectAddress), not 2-byte absolute-indirect.
- [x] Cycle-count accuracy: every defined 65C02 opcode now matches its datasheet cost. The bus helpers (readByte/readWord/pushByte/pullByte) each charged cycles while every handler ALSO added the instruction's full published count, so the opcode fetch alone put all 211 opcodes one cycle over and JSR cost 11 instead of 6. Nothing read the counter, so it never surfaced. Helpers no longer charge; PHA/PHP/PLA/PLP (written to the old remainder convention) were corrected to full counts. Locked by tests/test_cpu_cycles.cpp against datasheet reference data.
- [ ] (Optional) Klaus2m5 interrupt test: still not available in the amb5l ca65 port (as65 source only) and uses a memory-mapped IRQ/NMI feedback register, so it'd need an as65 build (or manual ca65 port) plus a harness extension. Lower value now that the v2.2 IRQ/NMI path has direct coverage: tests/test_cpu_interrupts.cpp asserts masking, level-sensitive IRQ re-entry, edge-triggered NMI and its priority over IRQ, the B bit distinguishing hardware entry from BRK, D cleared on entry, and RTI restore. That found one real bug — reset() left a latched NMI pending, so it vectored through $FFFA before the reset handler ran an instruction.

## Deferred from the kernel/BASIC deep scan (2026-06)

### BASIC integration fixes
- [x] LOAD/SAVE I/O vectors (PG2_TABS) pointed at $FF0F = the RNG routine. Resolved by implementing real BASIC SAVE/LOAD: SAVE writes the program as ASCII .bas text and LOAD reads it back (via a new byte-stream mode on the PIA file I/O). VEC_SV/VEC_LD now point at BASIC_SAVE/BASIC_LOAD, so the RNG bug is gone.
- [x] INIT_BASIC_IO removed (dead code); PG2_TABS is the single source of truth for the BASIC I/O vectors.
- [x] IRQ/NMI wired (v2.2): CPU IRQ/NMI dispatch + a ~60Hz PIA interval timer (BASIC ON IRQ) + NMI stop key (BASIC ON NMI / break to monitor). The kernel ISRs set EhBASIC's "happened" bit.

### BASIC label rewrite
- [x] Resolved via a glossary rather than a rename. EhBASIC's upstream is unmaintained (Lee Davison deceased) so parity is no longer a goal, but a full in-place rename of ~780 code labels was judged not worth the risk/effort. Instead, docs/basic_label_glossary.md (now part of docs/SYSTEM_INTERNALS.md) maps the cryptic LAB_<hex> labels (and the named handlers) to their meaning, drawn from the source comments. The ROM is left untouched. (Also added the required "Derived from EhBASIC" attribution: in the BASIC sign-on banner and the root NOTICE file.)

### Kernel code-quality refactors (ROM has ~4KB free; these are maintainability, not space)
- [x] Factor duplicated idioms: added PRINT_HEX_BYTE (byte->2 hex digits to screen), PRINT_MSG_AY (set MON_MSG_PTR from A/Y and print, replacing 13 inline copies), and shared SKIP_SPACES/EXPECT_COMMA parser helpers (replacing the skip-spaces/comma preamble duplicated across the F:, M: (x2), X:, and L:/S: filename parsers). CODE segment dropped from ~4185 to 3946 bytes; all tests pass.
- [x] Remove dead code: deleted unreferenced NIBBLE_TO_HEX_CHAR/NIBBLE_DIGIT, unused constants (MON_HEX_DIGITS, CURSOR_CHAR, ASCII_0/9/A/F, FILE_IDLE, FILE_ERROR), and the MOVE copy-vs-move branch that printed identical text. HELP_MSG_COUNT was kept and wired into the help loop (replacing a magic #30) rather than deleted.

### Documentation
- [x] docs/kernel_memory_map.md (now consolidated into docs/ARCHITECTURE.md, Part 2) and the kernel.asm header rewritten to match the actual system ($E000 ROM, $14-$39 monitor ZP, relocated page-2 vars, PIA I/O, no C64 banking/VIC/SID). DEC_DIGIT_BUFFER now defined as "= MON_SEARCH_PATTERN" instead of a literal.
- [x] Done via the #65 docs consolidation: docs/system_architecture.md was merged into docs/ARCHITECTURE.md (Part 1 — System overview) and its stale C64-style $D000 I/O / VIC-II / SID / CIA / banking description was dropped. The authoritative memory map now lives in docs/ARCHITECTURE.md, Part 2.

### Bankable module slot (docs/ARCHITECTURE.md, Part 4)
- [x] Phase 1 (v2.2.7/8): relocate I/O $DC00 -> $FE00, reserve the I/O page (IORESV), clean the $B000-$DFFF window.
- [x] Phase 2 (v2.2.9): banking infrastructure - MODULE_BANK register ($FE23), emulator Memory window routing (bank 0=RAM, 1..255=ROM), host bank table (Memory::loadBank), RESET maps window to RAM. Behavior-preserving; BASIC still in bank-0 RAM. Covered by tests/test_memory_banking.cpp.
- [x] Phase 3 (v3.0): BASIC is now module bank 1 (host installs basic.rom as a bank, not flat RAM). Added the kernel MODULE_DIR catalog + the B: bank menu/launcher; RETURN_FROM_BASIC -> RETURN_FROM_MODULE ($FF12) unmaps the bank on exit; RESET zeroes the $B000-$DFFF window so bank 0 boots clean. Factored FILL_RANGE_CORE out of F: and reused it. Covered by testBankMenu/testBankLaunch; integration harness now returns non-zero on failure so ctest catches regressions.
- [x] Phase 4 (v3.1/3.1.1): DEV TOOLS module in bank 2 (src/kernel/devtools/, devtools.rom). Disassembler (D), line assembler (A), two-pass assembler (B) with labels/expressions/.ORG/.END/.BYTE/.WORD/.ASCII/=, host .s source load (L), and a build listing. Canonical 65C02 opcode table generated from CPU6502 (tools/gen_opcode_table.py) with a drift-guard test. Module ABI extended: K_READ_LINE/K_PARSE_HEX/K_PRINT_HEX_BYTE ($FF15/$FF18/$FF1B). Sub-steps 1-6 committed on feat/devtools-module.
  - [x] In-machine generic text editor + resident filesystem: both shipped. MFC-DOS ($9000-$AFFF) is the resident FAT16 filesystem, and EDIT (programs/edit, docs/EDIT.md) is the full-screen editor. Self-hosting is complete — edit -> assemble -> SAVE -> run by name, all at the `]` prompt.
  - [ ] Remaining from post-Phase-4: assembler macros + more directives; single-step/breakpoints in the monitor.

### Monitor out of the kernel (in progress)
Splitting the kernel ROM into a true BIOS (the machine) and the monitor (an interactive
debugger that happens to ship with it). The monitor ends up a bank module, not a disk
program: a program loads at $0800 and so collides with the very code it is meant to
debug, whereas a bank costs no user RAM and is reachable with a dead disk. The blind
spot it accepts is that a banked monitor cannot inspect its own window ($B000-$DFFF)
or a sibling bank.
- [x] **Step 1 — separate the source, one ROM.** monitor.inc (1,878 lines) and the
  shared kernel_vars.inc split out of kernel.asm; still one assembly unit, CODE
  unchanged at 3951 bytes. `kernel_bios_monitor_split` (tests/scripts/check_kernel_split.py)
  makes the boundary an enforced invariant instead of a comment.
- [x] **Step 2 — make it a bank.** monitor.asm is its own link unit at $B000,
  installed by the host as bank 4. Kernel CODE 3951 -> **1562 bytes**; the monitor is
  2438 bytes of a 12 KB window. All four BIOS -> monitor wires cut: MONITOR_COLD and
  MONITOR_MAIN became MON_LAUNCH (map the bank, check the entry is not $00, jump) and
  an NMI break that maps bank 4 instead of unmapping -- so a program that scribbles on
  MODULE_BANK can no longer lock you out of the monitor. RECALL_LAST_COMMAND moved to
  the BIOS where it belonged (it is line editing over shared page-2 buffers), which
  also removed the monitor's last need for CLEAR_CMD_BUFFER. Boot got a private
  20-byte page loop instead of borrowing the F: fill engine. Two ABI entries added
  (K_HEX_PAIR $FF3C, K_PARSE_DEC_VAL $FF3F) and PRINT_MSG_AY became a 4-byte private
  copy. Q exits via RETURN_FROM_MODULE so the window returns to RAM.
  - Accepted blind spot, asserted in the tests so it reads as a decision: the monitor
    cannot show $B000-$DFFF as RAM (it is standing there) or inspect a sibling bank.
  - `kernel_bios_monitor_split` now checks the thing the assembler cannot see -- that
    monitor.asm's $FF00 equates still match the kernel's jump table. Renumber the
    table and every equate below the insertion point silently points one slot off.
- [ ] Kernel after the move: ~1,400 bytes (BIOS ~1,300 + a bank-entry stub), which fits
  a 4 KB window comfortably (3,584 usable below the I/O page). A 2 KB window leaves only
  1,536 and is too tight to aim for.

## Deferred correctness work (2026-07)

Surfaced while adding the host-interop and interrupt coverage. None of it blocks
anything today; all four are recorded so they are not rediscovered the hard way.

### DOS / filesystem
- [ ] **The DOS cannot detect a media change.** `_FS_MOUNT` caches the BPB geometry (sectors/cluster, FAT start, root start, data start, FAT size, root entry count) and `_FS_ENSURE_MOUNT` short-circuits on `DOS_MOUNTED` ($0300) permanently after the first mount. There is no remount path and no `MOUNT` verb. Swap the backing store mid-session and every subsequent write goes through the *old* layout: found this the hard way when a test mounted a 4096-cluster image over a 128-cluster one and the DOS wrote root directory sectors on top of FAT #1 (`REUSE   BIN` sitting at FAT offset 512). The integration harness now clears `DOS_MOUNTED` on swap as a stand-in for a media-change signal, but real hardware has no such host. Options: a `MOUNT` verb, a block-device media-change/dirty bit the DOS polls, or re-reading the BPB whenever the volume serial or a sector-0 checksum changes. Only worth doing if swapping storage while running is meant to be supported.
- [ ] **Most FAT16 tests run on a geometry no host would mount.** `kDefaultDataClusters` is 128 for test speed (83 KB per image vs 2.1 MB at `kHostFat16Clusters`), but FAT type is derived from the cluster count, not declared — under 4085 clusters the standard says FAT12, so host tools parse the 16-bit table as 12-bit and `fsck` output off those images is meaningless. It also skips real driver paths: at 128 clusters every cluster number fits in a byte and the whole FAT is one sector, while at 4096 the FAT spans 17 sectors and cluster numbers exceed 255, which is different arithmetic in `_DOS_FAT_SEEK`/`_DOS_READ_FAT_ENTRY`. Only two of the 42 `dos_fat16` tests pass `kHostFat16Clusters` (plus `fat16_roundtrip` throughout and the new interop check). Consider raising the default, or adding a host-geometry pass over the write-path tests.

### Host file I/O (these two are one piece of work)
- [ ] **`PIA::closeStream()` error reporting has no test.** It is the one fix from the 2026-07 review that landed inspection-only, with no test verified to fail against the unfixed build. Untestable while the filename comes from the host dialog — see below.
- [ ] **Decide whether the PIA should honour the 6502-supplied filename at `$FE14-$FE1F`.** Today the host file dialog owns the filename, so `L:`/`S:`/`IMPORT` cannot be driven from a script or a test. Honouring the guest-supplied name would make those verbs scriptable *and* make `closeStream` testable, closing both items at once. **User decision pending** — it is a behaviour change to the host I/O contract, not just a fix.

### Monitor and assembler consolidated (2026-07-31)
- [x] The assembler/disassembler (bank 2, "DEV TOOLS") folded into the monitor and the
  ASM module retired. The split only existed because the monitor used to be resident
  in kernel ROM; once it became a bank too, building and testing crossed the DOS twice
  per iteration and lost the source buffer each time. Period monitors (Supermon,
  HESMON, the Apple II ROM monitor and its mini-assembler) all bundled them. Merged
  module is 6,841 bytes of the 16 KB window; bank 2 is free.
  - Commands take the monitor's colon grammar: `A:xxxx` `B:` `D:xxxx` `L:`. `D` went to
    the disassembler, so base conversion moved to `#:nnnnn` and `$:xxxx`. `B:` and `L:`
    reuse dispatch slots the retired bank menu and host-load command left empty.
  - assembler.asm -> assembler.inc, included by monitor.asm; its eight duplicated
    address definitions deleted in favour of kernel_vars.inc.
  - ASSEMBLER.md folded into MONITOR.md and left as a pointer (existing links).

### Assembler v0.9: the shipped examples actually assemble (2026-08-02)
- [x] Three bugs, found because `L:`+`B:` on examples/colors.asm reported `? LINE 0010`.
  - **Identifiers were capped at 8 characters**, so nine of the twelve examples would
    not build (`K_PRINT_CHAR` is 12, `K_PRINT_MESSAGE` is 15). Raised to 16.
  - **`.BYTE` rejected strings** — five examples use `.byte "TEXT", 0`, which every
    other 6502 assembler takes. It now accepts a quoted string anywhere a value goes.
  - **`? LINE nnnn` was off by one after the first blank line.** The reader consumed a
    terminator then swallowed the next byte if it was also CR or LF (meant for CRLF
    pairs), which cannot tell `$0A$0A` from `$0D$0A` — so a blank line vanished from
    the count and the diagnostic pointed at the wrong source line. That is what made
    a bad identifier on line 17 report line 16, and it defeated the whole purpose of
    the v0.7 diagnostics work.
  - Room for 16-character names came from moving the symbol table and identifier
    buffers OUT of user RAM into the free page at $0500 (the old 40x25 screen, unused
    since the display moved behind the VIC port). That is below Ram_base, so it costs
    user programs nothing and actually **returned 512 bytes** to them: the `.ORG`
    ceiling rose from $7600 to $77FF. The bank's free space could not be used --
    that window is ROM while a module is mapped, and a symbol table must be written.
    Cost: 40 symbols instead of 51, since entries grew from 10 to 18 bytes.
- [x] `testShippedExamplesAssemble` builds all twelve every run, so the promise in
  examples/README.md ("assemble the source in the built-in assembler") cannot rot
  again. `testLineNumbersCountBlankLines` pins the line numbering.

### Games
- [ ] **VENTURE** (`programs/venture/`) — a port of Exidy's Venture (1981); design in
  `programs/venture/DESIGN.md`. Planned as the final game. Fits the machine unusually
  well: Winky is already CP437 glyph $01 (with $02 as a second animation frame), the
  slow real-time pacing is a quarter of what KERNEL PANIC sustains, and 8-way movement
  while firing is the exact case the $FE0F control port was added for. Movement model
  adopted wholesale from KPANIC (fixed-tick accumulator off jiffies, keystate() once
  per tick, one cell per tick), with `tickrate` doubling as the difficulty ramp.
  Build order in the design doc is ten steps; steps 1-5 give a playable single room,
  which is the point to stop and judge whether it feels like Venture.
- [ ] **OPCODE** — set aside, recorded so it is not lost. A puzzle game where each
  level is a spec plus a byte/cycle budget and you write real 65C02 to satisfy it,
  scored on size and speed. Extremely on-brand for "My First Computer", and four
  pieces already exist: the two-pass assembler, EDIT, a real 65C02, and (as of the
  v0.9 cycle work) datasheet-exact cycle counts, which is what makes scoring by
  cycles meaningful rather than approximate.
  - **Why it is not the next game:** sandboxing 6502 from 6502 is the whole problem.
    ROM write-protection means player code cannot hurt the kernel or DOS, but nothing
    protects game RAM from a stray STA, an infinite loop needs a watchdog the game
    cannot run from inside itself, and exact per-instruction cycle counting means not
    letting the real CPU execute it at all. The honest resolution is a 65C02
    interpreter in C -- a second emulator inside the game, ~600-1000 lines before the
    first puzzle. That is an engine project, not a game project.

### Memory map
- [x] **Kernel to a 4 KB window; banks grow to 16 KB.** With the monitor gone the BIOS
  is 1,562 bytes, so the kernel moved from $E000-$FFFF (8 KB) to $F000-$FFFF (4 KB) and
  the reclaimed $E000-$EFFF went to the module window, now $B000-$EFFF. Nothing needed
  rebasing: the modules keep their $B000 base and simply have more room, which matters
  for BASIC (10,613 bytes, 86% of the old 12 KB ceiling, now 65%). No ABI change.
  - This exposed a latent host bug worth remembering: Computer6502 loaded kernel.rom by
    subtracting a hardcoded $E000 from each segment address. At $F000 that offset ran
    past the end of the (now 4 KB) file, the out-of-range iterators loaded nothing, and
    the machine sat at $0000 with no diagnostic. The base now comes from
    Memory::kKernelRomStart, the file size is checked against the window, and every
    segment is bounds-checked. testRomWindowBoundaries pins it.

### Memory map (future, not urgent)
- [ ] Reclaim ROM address space for user RAM by a *coordinated* relocation: shrink the kernel from 8KB ($E000-$FFFF) to a 4KB window ($F000-$FFFF) AND move the BASIC ROM up (e.g. $B000-$DFFF -> $C000-$EFFF). Done together, the reclaimed 4KB lands contiguous with the user RAM below BASIC, growing one usable block (shrinking the kernel alone just strands an isolated 4KB island between BASIC and the kernel — not worth it).
  - **Prerequisite (blocking):** the kernel must first shrink to fit a 4KB ROM. CODE is 3951 bytes at v3.27 ($E000-$EF6E), but the $FF00 API jump table caps contiguous code at $F000-$FEFF = 3840 bytes, so we must free **at least ~111 bytes** (more for headroom) before this is even possible. The v3.25 size pass already took 178 bytes out mechanically (dead SAVE/RESTORE_MONITOR_STATE, 33 JSR+JMP/JSR+RTS tail calls, T:/Z: merged into DUMP_ONE_PAGE, dead stores) — that is the end of the easy wins. The remaining ~111 needs a structural change, and note the obvious candidate is harder than it looks: table-driving the monitor's command dispatch is not the clean win the DOS verb table was, because the per-command stubs are NOT uniform (C:/T:/Z: share a shape, but D:/H: carry their own validation), so the table would need a parse-kind field and two mechanisms.
  - Also requires: rebuilding the EhBASIC ROM at the new base (Ram_top + any absolute self-references), updating memory.cfg / basic_memory.cfg, and the emulator's ROM load addresses + Memory region routing. Gate on a byte-diff sanity check. The reset/IRQ/NMI vectors ($FFFA) and jump table ($FF00) pin the kernel to the top regardless, so the kernel can only shrink the window, not move off the top.