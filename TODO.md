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

### Monitor out of the kernel (done)
Splitting the kernel ROM into a true BIOS (the machine) and the monitor (an interactive
debugger that happens to ship with it). The monitor ends up a bank module, not a disk
program: a program loads at $0800 and so collides with the very code it is meant to
debug, whereas a bank costs no user RAM and is reachable with a dead disk. The blind
spot it accepts is that a banked monitor cannot inspect its own window ($B000-$EFFF)
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
    cannot show its own window as RAM (it is standing there) or inspect a sibling bank.
    The window was $B000-$DFFF at the time of this step and is $B000-$EFFF now.
  - `kernel_bios_monitor_split` now checks the thing the assembler cannot see -- that
    monitor.asm's $FF00 equates still match the kernel's jump table. Renumber the
    table and every equate below the insertion point silently points one slot off.
- [x] Kernel after the move fits a 4 KB window comfortably, as predicted (a 2 KB window
  would have left only 1,536 usable and was never worth aiming for). Actual: CODE
  $F000-$F610 = **1,553 bytes**, with 2,031 free below IORESV at $FE00. The move itself
  is recorded under "Memory map" below.

## Deferred correctness work (2026-07)

Surfaced while adding the host-interop and interrupt coverage. None of it blocks
anything today; all four are recorded so they are not rediscovered the hard way.

### DOS / filesystem
- [x] **Dev-loop hazard: the host can rewrite the disk image under a running machine.** **Mitigated 2026-09-02.** `BlockDevice` takes a shared advisory claim on its image for the session and `mkdisk create`/`update` take an exclusive one before writing, so `ninja disk` now refuses with a message naming the cause instead of silently swapping media under a running machine; `--force` overrides, and `mkdisk read` is unaffected. Advisory, so nothing that ignores it breaks, and the claim dies with the process so there is no stale lock to explain. Best-effort on the machine side by design -- a missing image or a filesystem without working locks leaves the hazard as it was rather than refusing to boot. `host/ImageLock.h` carries the reasoning; four tests in `test_block_device.cpp` cover it, including one that spawns the real `mkdisk` and was proved to fail against the unfixed tool. The Windows branch (`LockFileEx`) is written but not compiled -- no toolchain here. The period-correct fix below is still the proper one and still hard to justify. Original analysis: Not a correctness gap in the DOS and not reachable by anyone playing it -- there is no eject. `setImagePath()` is called only from tests, so nothing in the GUI can swap media. The exposure is the build: `ninja disk` / `ninja everything` / `./build.sh` rewrite `cmake-build-debug/disk.img` with `std::ios::trunc`, and `BlockDevice` re-opens by path on every sector access, so a rebuild while the machine is up is adopted instantly with nothing to signal it. **What it costs.** Geometry is not the risk: `mkdisk` always builds at `kHostFat16Clusters` (4096), a fixed 2,122,752 bytes whatever the catalog holds, and the guest allocates within a BPB fixed at format time -- so neither catalog growth nor anything the 6502 writes can invalidate the cached layout. The risk is writes in flight. DOS carries live per-file state across the swap (`DOS_W_FIRST_CLUS`, `DOS_W_PREV_CLUS`, `DOS_F_CLUS`, `DOS_F_LBA`, and the directory position at $030D), all allocated against the old FAT, so a save or an assembler output landing across a rebuild stamps a directory entry pointing at a chain the new image does not agree with -- and the corruption is in the NEW image. The assembler is exposed at its input too: it streams source through `FS_GETB` and would happily assemble the first half of the old file joined to whatever now occupies those sectors. Edited text is safe; EDIT holds the document in RAM and only touches the disk on Ctrl-O/Ctrl-S. **Holding an open handle would not help**, which is worth recording because it looks like it should: `mkdisk` truncates in place rather than writing a temp and renaming, so a handle sees the mutation anyway -- and briefly sees a zero-length file mid-rebuild. The block device is nowhere near a hot path (disk access only on CATALOG/LOAD/SAVE and assembler I/O), so the syscalls saved are not a reason either. A handle would only isolate the machine if `mkdisk` wrote temp-and-rename; the two changes are a pair or neither. **Cheapest real mitigation** is on the host side: have the build refuse to rewrite `disk.img`, or warn, while an emulator has it open. The period-correct fix -- a media-changed bit in the block device's status register ($FE27, today only Ready/Error) plus a remount when the DOS sees it, the software equivalent of a card-detect line -- is real but hard to justify against a hazard only the build can trigger. Prior evidence that the DOS half is genuine: a test once mounted a 4096-cluster image over a 128-cluster one and the DOS wrote root directory sectors on top of FAT #1 (`REUSE   BIN` at FAT offset 512). The integration harness clears `DOS_MOUNTED` on swap as a stand-in.
- [x] **Most FAT16 tests run on a geometry no host would mount.** **Fixed 2026-09-02** by raising `kDefaultDataClusters` to `kHostFat16Clusters`, so a test image is now the same kind of volume the machine runs on. The speed argument the small default rested on had evaporated: it cost 0.4 s (`dos_fat16` 4.88 s -> 5.28 s) and no production code changed, since `mkdisk`/`mkfat16` already passed the host count explicitly. One test needed pinning rather than raising -- `testDosSaveDiskFullReclaims` is about running out of room, and 124 clusters of 4,096 leaves plenty, so it now asks for 128 explicitly and was confirmed to fail without that. `fsck.fat` on a default image now reads "16 bit entries" and exits clean, where before it invented corruption on a healthy volume. Still uncovered: the 17th bit of the FAT byte offset needs more than 32,768 clusters (a 16 MB volume) and is unreached at any geometry the suite builds. Original analysis: `kDefaultDataClusters` is 128 for test speed (83 KB per image vs 2.1 MB at `kHostFat16Clusters`), but FAT type is derived from the cluster count, not declared — under 4085 clusters the standard says FAT12, so host tools parse the 16-bit table as 12-bit and `fsck` output off those images is meaningless. It also skips real driver paths: at 128 clusters every cluster number fits in a byte and the whole FAT is one sector, while at 4096 the FAT spans 17 sectors and cluster numbers exceed 255, which is different arithmetic in `_DOS_FAT_SEEK`/`_DOS_READ_FAT_ENTRY`. Only two of the 42 `dos_fat16` tests pass `kHostFat16Clusters` (plus `fat16_roundtrip` throughout and the new interop check). Consider raising the default, or adding a host-geometry pass over the write-path tests.

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
- [x] **VENTURE** (`programs/venture/`, `VENTURE.PRG` 18,227 bytes) — a port of Exidy's Venture
  (1981); design in `programs/venture/DESIGN.md`, manual in `docs/VENTURE.md`. All ten
  build steps done: the dungeon hall, six themed rooms dealt four at a time per level,
  Hallmonsters patrolling the hall and coming through room doors if you dawdle, the
  three-level loop that speeds up and never ends, and SID cues. It fit the machine as
  well as hoped — Winky was already CP437 glyph $01 with $02 as his second frame, and
  8-way movement while firing is the exact case the $FE0F control port was added for.
  - Map and room are one code path (one grid, one `restore()`, one `chase()`), which
    is why the hall was nearly free and a Hallmonster entering a room needed no new
    machinery: it is a chaser that ignores bodies and cannot be shot.
  - Greedy pursuit needed a wander fallback — pressed against the hall's long wall
    bands with no second axis to try, a chaser stood still forever.
  - `tests/test_venture.cpp` reads the room pictures back out of
    `venture.c` and flood-fills them, so a one-character typo that seals a treasure
    off fails the build instead of shipping an unwinnable room. Two turned up that
    way. It also has to retry when looking for Winky: `step()` erases before it
    redraws, and a cycle budget can stop the CPU in that window.
  - Then a **fidelity pass against ten screenshots of arcade play**, which is where
    most of the above got rewritten. Four mechanics a written summary does not convey
    were sitting in plain sight: a looted room **seals itself solid** (entrances gone,
    block filled in), Hallmonsters **accumulate** as you loot (one wakes per room),
    rooms have **two doorways** on opposite sides, and Winky carries a **visible
    facing pip**. Plus the per-level recolour, the treasure roster with
    `PLAYER 1 GET READY`, the between-levels bonus tally, and room interiors redrawn
    as open arenas rather than mazes. The pip fixes a readability problem this port
    invented for itself — facing persists after you release the key and nothing showed
    it. 23 tests, `VENTURE.PRG` 10,488 bytes.
  - A **third pass** on what screenshots cannot show — behaviour. The room intruder
    now **walks through walls** (it cannot be cornered or lost behind a wall; the only
    answer is to leave, which is what makes it a deadline rather than an enemy), and
    room monsters **route around corpses** instead of stalling next to them. That
    second one was a real bug with a specific shape: the corpse was vetoed *after* the
    greedy step had been chosen, so the monster did not move at all — obstacles have to
    be part of the pathing, not a filter on its output. Rooms in the hall are now drawn
    as hollow outlines that fill in when looted, as the arcade draws them. 24 tests,
    `VENTURE.PRG` 11,274 bytes.
    - Making the monsters better **broke a test route**: the 160-tick loot route
      through room 0 only ever survived because serpents stalled. The harness moved to
      the spider room (two clear columns, under 40 ticks) and stopped counting ticks
      for room legs — the accumulator and the jiffy budget drift by a tick with phase,
      so a leg lands one cell short and walks past its treasure.
  - A **fourth pass**, from actually playing it. Two bugs that were invisible in the
    code and in the tests and obvious in a minute at the keyboard: the hall's entrances
    and the rooms' doorways **did not line up** (they were matched by scan order, which
    is arbitrary), and a shot **passed through a monster** about a third of the time.
    The second is an ordering hazard: the arrow moves before the monsters, so inside one
    tick the arrow can advance past a monster's cell and the monster step into the
    arrow's, with nothing looking again. It only bit on ticks where monsters move, which
    is why it read as bad luck. Monsters also no longer stack on each other. The
    entrances now get cut at runtime on the sides matching whichever room a slot holds.
    26 tests, `VENTURE.PRG` 12,152 bytes.
  - A **fifth pass** onto the upgraded VIC — soft font, sprites and fine scroll — and
    then onto movement, which took most of it. The dungeon now draws in **its own
    hand-drawn glyphs** rather than borrowing CP437 shapes, and every mover is a
    **sprite** instead of a character cell: 2x2 composed and magnified, which lands
    exactly on a double-size cell, so the playfield geometry did not have to change.
    Sprites also buy **sub-cell motion** — a mover glides between grid steps instead
    of jumping a whole cell — and that is what let the tick slow from 15 to 10 a
    second without reading as a strobe. `HALL_ROOM_TICKS` went 260 -> 170 to keep the
    dawdle deadline at the same ~17 s across that change.
    - Room monsters stopped hunting and now **hold strategic ground**, darting only
      once you come inside `MON_AGGRO`, which is what the arcade does and reads as
      guarding rather than chasing. The hall starts at `HALL_BASE` 3 and Hallmonsters
      no longer share a cell. The intruder **comes at you diagonally**; an L-shaped
      approach was slower and read as indecision. And an arrow is now the same shape
      and the same pixel speed in **every direction** — `ARROW_STEP` 2 across against
      `ARROW_STEP_V` 1 down, because a playfield cell is 16 px wide and 32 tall.
    - Most of this pass came from **playing it, not measuring it**. Every genuine
      diagnosis started from something seen on screen; a whole turn spent
      instrumenting sprite positions frame by frame produced nothing. Two standing
      budget tests came out of it — a frame of drawing under 9,000 cycles, a tick
      under 30,000 — because those are facts a harness can settle and smoothness is
      not. 37 tests, `VENTURE.PRG` 18,227 bytes.
  - **Two gaps, deliberate** — down from three. The between-levels tally needs all
    four rooms of a level looted, i.e. four bespoke routes through four layouts. The
    other is the arrow-swap fix, which has no signature on screen: the arrow is drawn
    over the monster it is sitting on, so provoking the one observable frame needs a
    monster to step onto a live arrow, on a tick monsters move, with Winky alive to
    watch — every setup tried passed on the broken build as often as the fixed one,
    and a test that passes on the broken build is worse than none. Both are written up
    in `test_venture.cpp` with what they leave unverified.
    - **Closed since:** the room intruder. Outlasting `HALL_ROOM_TICKS` with serpents
      converging was the blocker, and a `dawdleUntilIntruder()` helper now gets there
      reliably; two tests ride on it, covering the far-doorway arrival and the
      diagonal approach. The wall-phasing path came along for free — the intruder in
      those tests is walking through walls to reach Winky.
  - **Open: vertical motion covers twice the screen distance of horizontal.** A
    playfield cell is 16 nominal pixels wide and 32 tall, so one cell up is two cells'
    worth of travel across. Gating vertical to every other step evens the average and
    reads as start/stop; spreading the glide over both ticks removed the hold but left
    a periodic snap of about +19 then -13 px every six frames, cause never found. Both
    attempts were reverted, so what ships is smooth and asymmetric. It was only ever
    noticed on the arrow, which `ARROW_STEP_V` already fixes. Recommendation: leave it.
  - **Open: the loot-route tests are not reproducible.** `rng_seed()` is RTC-derived,
    so no two runs see the same monster rolls and a route can land a cell short of its
    treasure. They were noted as failing about one run in seven; 10 consecutive runs
    on 2026-09-02 all passed, so treat that rate as unverified rather than current.
    Making the seed injectable is the fix either way — it removes the question instead
    of re-measuring it.
- [ ] **KERNEL PANIC** (`programs/kpanic/`, `KPANIC.PRG` 13,679 bytes) — original
  real-time vertical scroller; design in `programs/kpanic/DESIGN.md`. Build steps 1-6
  done and play-tested good, plus a full weapon/feel rework. Steps 7-8 open (below).
  It is the program the VIC's **soft font, fine vertical scroll and sprites** were added
  for — see `docs/video_design.md`; every one of those exists because a character-cell
  chip scrolling in whole 16 px quanta reads as a strobe rather than motion.
  - **Read the original's source, not summaries of it.** `riverraid.asm` for the 2600
    settled three questions that guesswork and screenshots had got wrong:
    - **Speed is not a difficulty dial.** `speedY` is written only by the joystick
      (`+2` up, `-2` down, clamped) and `level` never touches it. We had made scroll
      rate our primary escalation AND given the player up/down for it, so two controls
      wrote one variable and the sector stomped whatever had been chosen. All four of
      the original's real dials are density-shaped: enemy share ~48%->88% while fuel
      falls ~24%->6%, planes withheld to level 3, `valleyWidth` unlocking narrow river
      only from level 5, geometry alternating on level parity.
    - **Fuel is the DEFAULT object, not a lottery win.** `LDY #ID_FUEL` loads first and
      the code branches *away* to enemy or house: ~24% of object slots at level 1. We
      were rolling *for* a node at 1-in-90 per row, ~1.1%.
    - **Bridge cadence is measurable**: `SECTION_BLOCKS(16) * BLOCK_SIZE(32)` = 512
      scanlines against a 160-line display, so ~3.2 screens. Ours is `FW_ROWS` = 80
      because 3.2 * `PLAY_H` = 77. Not a guess.
    - Also: their refuelling is *gradual*, `fuelHi += 1` per frame of contact, which is
      what the throttle is FOR — you slow down over a depot. Ours grants a flat refill.
      A real difference, deliberately not copied.
  - **The boss was cut** for that bridge: a barrier with one port, built as *terrain*
    (one flagged row in the same ring the walls live in) so it rides the hardware scroll
    for free and `blocked()` already stops the craft on it. A boss has to stop the world
    to avoid sawtoothing against the fine-scroll offset, which turns a scrolling game
    into a set-piece fight it is not shaped for. **Overclock was cut** too — it bought
    speed you do not want in a game about precision, and its absence made "one step is
    one row" a structural invariant (`WORLD_STEP`) instead of a threaded variable.
  - **Two bug families accounted for nearly every real defect.** Worth checking first
    in any similar program:
    1. *Span collision.* Anything moving more than one row per step must resolve across
       the span it swept, not at its landing row — and check WIDTH too, not just rows.
       Instances: pellets/daemons over the craft (`swept_craft`), shots vs enemies
       (`shots_enemies_resolve`), shots vs *terrain* (`scroll_world()` runs first, so
       terrain moves onto the shot and it crosses `speed + WORLD_STEP` rows while
       testing fewer — this is why a firewall port ignored damage on 1 in 3 approach
       alignments, and a craft sitting *on* the port landed zero shots), and the craft
       vs a two-cell enemy body, which tested one column for months while the shot side
       correctly tested both.
    2. *Statics outliving the run that set them.* `speed_px`, `pop_t`, `fw_next`, and
       `sector_apply()` running *after* the playfield pre-fill — so a second run built
       its whole opening screen with the previous run's sector. **None are reachable
       without playing twice without reloading.** Always test a second run.
  - **A comment asserting an invariant is not an invariant.** `terrain_cell()` and
    `draw_row()` were duplicate copies of one cell ladder, both carrying notes saying
    they must agree, and had diverged so far that `draw_row()` had no firewall branch at
    all: barriers were solid, damaging and completely invisible. One definition now
    (`row_cell()`), with the board phase hoisted into file scope so the duplicate's real
    justification — 80 16-bit modulos a row — survives. Removing the duplication made
    the binary 572 bytes *smaller*.
  - **KPANIC has no test harness, but VENTURE now shows how to build one.** When the
    work above was done nothing could test a `.PRG`, so decisions here were made by
    replicating the logic in throwaway host C and measuring — see the note in
    `~/.claude` memory. That caught things reading could not: a generator guard leaving
    a 2-wide lane where it promised 3 (608 rows per million), an economy where a
    competent player bled to death by arithmetic, and a composed soft-font glyph
    sitting hard against its frame because CP437 capitals occupy scanlines 2..11 of 16.
    - That constraint is gone. `tests/test_venture.cpp` runs 37 tests against the real
      game: a `venture_bin` target links a flat $0800 blob, the harness writes it into
      memory and sets `PC` there, and it reads live state by name through the `-Ln`
      label file cl65 emits (non-`static` variables only, which is the one thing to
      design for). Steps 7-8 should get a `kpanic_bin` target and the same treatment
      rather than another round of throwaway C — the span-collision and stale-static
      bug families above are exactly what a harness pins, and the budget tests VENTURE
      grew (a frame under 9,000 cycles, a tick under 30,000) apply here too.
  - **Rejected alternatives, with the numbers, so they are not re-proposed:**
    - *Node minimum spacing* — kills the luck-death tail, but even a floor as loose as
      4N doubled a good player's distance and pushed perfect play toward never dying,
      which breaks "how far can you go" as a score. The scarcity cut subsumed it.
    - *A fixed cooldown for spread Lv3* — 150 volleys against the broken version's 164.
      It fires *less* while looking like a fix, because refuse-and-retry is already a
      more generous self-pacing limiter than any constant.
    - *Enlarging the chip's sprite block* — works, but pool 20 is the threshold (19
      refuses exactly as much as 16) and it spends 24 of the 53 remaining I/O-page
      bytes on one weapon's peak. Spread became short-ranged instead, which fits by
      construction and buys a weapon role rather than a compromise.
    - *Per-sector palette re-skin* — proposed from a screenshot that turned out to be
      the one frame of a bridge explosion. The original runs one palette throughout.
      What shipped is a per-sector *board* colour, which is our own idea, not theirs.
  - **Tuning lesson:** `FRAG_CHANCE` was halved to 12 reasoning that one-shot kills had
    doubled the kill rate. Wrong quantity — what a player feels is the drop *cadence*,
    which at 12 was ~29 s against runs of 60-90 s, so power-ups stopped appearing at
    all. Reason about the cadence the player experiences, not the rate the mechanic
    fires at.
  - **Remaining (steps 7-8):** juice — explosions beyond the debris scatter, cell-offset
    screen shake, SID cues; a 2-word/BCD score (`unsigned int` caps at 65,535); a proper
    outcome screen; a final balance pass; and a companion `KPANIC.TXT` manual shipped on
    the disk beside the game the way VAULT ships `STORY.TXT` — not a `docs/*.md`.
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

**Superseded, kept because the conclusion changed.** This used to read: shrink the
kernel from 8 KB ($E000-$FFFF) to 4 KB ($F000-$FFFF) *and* move the BASIC ROM up
(e.g. $B000-$DFFF -> $C000-$EFFF), so the reclaimed 4 KB lands contiguous with the
user RAM below BASIC. It also called the kernel shrink a blocking prerequisite
needing ~111 bytes freed by a structural change.

Both halves are obsolete:

- **The kernel shrink is done** — see "Memory map" directly above. The BIOS came in
  at 1,562 bytes once the monitor became a bank, so the ~111-byte problem evaporated
  rather than being solved, and no monitor dispatch table was needed. Kernel CODE is
  now $F000-$F610 (1,553 bytes) with 2,031 free below IORESV.
- **The relocation would no longer buy user RAM at all.** That plan assumed the
  module window sat directly above user RAM. It does not: the **DOS ROM is between
  them**. The map is now user RAM $0800-$87FF, DOS ROM $8800-$AFFF, module window
  $B000-$EFFF, kernel $F000-$FFFF — so anything freed at the bottom of the module
  window is stranded above the DOS ROM and cannot extend one usable block.

- [ ] The only remaining lever that actually grows user RAM is the **DOS ROM base**,
  because the DOS is the thing directly above user RAM. It currently occupies
  $8800-$A6D5 (7,894 bytes) with **2,090 bytes free** below DOSJUMP at $AF00, so the
  base could move up to about $8C00 — returning 1 KB to every program and still
  leaving the DOS ~1 KB. Note this is exactly the boundary that moved *down* in the
  other direction on 2026-07-31 ($9000 -> $8800) to relieve a DOS that had 145 bytes
  left; moving it back is cheap in code (the routing derives from
  `Memory::kDosRomStart`) but touches basic.asm `Ram_top`, the dos.asm COPY guard and
  MEMMAP text, the assembler workspace, all five cc65 `.cfg` files, and the boundary
  tests. Worth doing only when something concrete needs the KB.
  - **The concrete case has already happened once.** That $9000 -> $8800 move took
    2 KB from every program and VAULT, which had ~387 bytes of margin, stopped
    linking. It was paid for on the program side instead (commit 8fd99b9: packed
    visibility bits and deleted a render shadow buffer, freeing 3,080 bytes), which
    was the right call there — but it is the kind of bill that comes due again, and
    the next program may not have 3 KB of fat to cut.

One constraint from the old plan outlives it: the reset/IRQ/NMI vectors ($FFFA) and
the $FF00 jump table pin the kernel to the top of the map regardless, so the kernel
can only ever shrink its window, never move off the top. The rest of that plan's
checklist — rebuilding the EhBASIC ROM at a new base, memory.cfg / basic_memory.cfg,
the emulator's ROM load addresses — went away with it; BASIC keeps its $B000 base and
only `Ram_top` ever moves.
