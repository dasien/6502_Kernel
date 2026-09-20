# TODO List

## Open

### Modem: a raw mode for non-telnet connections (2026-09-20)

- [ ] **The bridge applies telnet framing to connections that are not telnet.**
  `ModemProtocol` does IAC doubling in both directions: a literal `$FF` from the
  6502 goes out as `FF FF`, an inbound `FF FF` collapses to one `$FF`, and a
  lone `FF` is read as the start of a negotiation command and consumed with the
  byte after it.
  - **That is correct telnet, and it is why TERM's XMODEM works.** A BBS is a
    telnet server, so it un-doubles what the bridge doubles and doubles what the
    bridge un-doubles. Binary survives end to end.
  - **It is wrong for a raw-TCP peer.** A Gopher server on port 70 speaks raw
    TCP, so binary content containing `$FF` loses two bytes to a negotiation
    that was never sent. IRC is on the same footing and never trips it, because
    IRC is 7-bit text -- and so is Gopher, right up until a type `9` item.
  - **What it blocks.** GOPHER reports type `9` (binary files: images, archives)
    as unsupported rather than transferring them. With a raw mode it could spool
    one to a FAT16 file with `dputb`, which is most of the work already done.
  - **Shape of the fix.** An `AT` command before dialling, which fits the
    in-band Hayes control the bridge already parses (`ATD`, `ATDT`, `ATH`,
    `ATZ`, `ATE`) -- `ATB1` for binary and `ATB0` to return, cleared on hangup
    so a stale mode cannot leak into the next call. A port heuristic (70 means
    raw) needs no protocol change but is implicit and would surprise anyone
    running Gopher on another port.
  - Host-side only, in `ModemProtocol` with a unit test through `ModemHost`; the
    protocol logic is already Qt-free and tested that way. Fixes the same latent
    hazard for any future raw-TCP client, not just Gopher.

### GOPHER — a network document browser (2026-09-19)

- [ ] **A Gopher client, `GOPHER.PRG`.** A web browser was considered first and
  set aside: essentially all of the web is HTTPS, and a 4 MHz 65C02 cannot do an
  ECDHE handshake plus AES-GCM per record. That is orders of magnitude, not a
  tuning problem. Lynx specifically is out three times over — roughly 200k lines
  against a 32 KB address space, dependencies on ncurses, libwww, zlib and
  OpenSSL, and a GPLv2 licence this project does not port from (see reSID in
  `docs/references.md`). Gopher has none of those problems: RFC 1436 is a dozen
  lines, it is text-native, menus map exactly onto 80x25, gopherspace is alive,
  and there is no TLS.
  - **Standalone, not folded into TERM.** IRC and TERM both drive the ACIA, both
    keep a server list, both share `programs/common/scrollback.c`, and they are
    separate programs. This is the third of that family. TERM is a dumb terminal
    that scrolls; Gopher needs a selection cursor on a menu, which is a different
    interaction model. `programs/term/glue.s` already exports everything needed:
    `acia_init/get/put`, the VIC primitives, `dopen_read`/`dputb`/`dgetb`/
    `dclose`, `jiffies` and `INCH`.
  - **Do not hold the document in RAM.** This is the decision that matters, and
    it is the same one that sank HTTP. Storing display text, selector, host and
    port per menu item runs about 180 bytes, so a 100-item menu is 18 KB of a
    30 KB budget. Instead spool the response to a FAT16 temp file, keep only line
    offsets, and parse a single line on demand when the cursor lands on it. RAM
    then holds one screen plus the selected item. The disk is 2 MB and EDIT
    already streams from it.
  - **The protocol.** Connect to port 70, send the selector and CRLF, read until
    close. A menu line is `<type><display>` TAB `<selector>` TAB `<host>` TAB
    `<port>`, and the response ends with a lone `.`. Types to handle: `0` text,
    `1` menu, `7` search, `i` info, `9` binary.
  - **Phase 1, the spike: DONE 2026-09-19.** `GOPHER.PRG`, 3,169 bytes. Dials
    `host:70`, sends a selector, and dumps the response into a scrolling body
    region. Confirmed against `gopher.floodgap.com`: the menu arrives, the body
    scrolls, and the response terminates cleanly. It reads a line at a time and
    stops on either the Gopher `.` terminator or the bridge's `NO CARRIER`,
    keeping both out of the body, with an idle counter only as a backstop.
    Tabs render as spaces and non-printables as dots, deliberately, so the raw
    wire format is visible.
  - **Phases 2 and 3: DONE 2026-09-19.** 7,309 bytes, confirmed navigating
    floodgap. Menu lines split on tabs and show only the display text, with `/`
    marking a submenu and `?` a search. Info lines render in white and are not
    selectable. Arrows move the highlight, Enter or Right follows, Backspace or
    Left unwinds a 16-deep stack of `(host, port, selector)`, Home returns to
    the top, Q quits. Type `7` prompts and sends the query after a TAB. Type `0`
    text came along nearly free, so phase 3 is folded in: every line becomes an
    unselectable item and scrolls the same way.
    - Menus live in a 10 KB arena with a 150-item cap rather than being spooled,
      because the filesystem has no seek so line offsets would buy nothing.
      Overflow marks the status line `[truncated]`. Info lines store only their
      display text, which is what keeps a typical menu inside the budget.
    - Two bugs worth remembering. Gopher closes the connection itself, so the
      bridge's `NO CARRIER` and the modem's `OK` from our own `ATH` land in the
      ACIA FIFO *after* the response terminator; the next dial then matched them
      and called the connection failed. Every request now drains the line first.
      And the "connection failed" notice was being painted before `draw_all()`,
      which promptly erased it, so a failure looked like a blank screen.
  - **Phase 4: DONE 2026-09-19.** Bookmarks in `SYSTEM/GOPHER.LST`, picked 1-9
    from a numbered list after the splash exactly as IRC picks a server, with
    `0` to type a host and a straight fall-through to the prompt when there is
    no list file. The format follows `DIAL.LST` and `IRC.LST` -- address token,
    rest of the line as the label -- extended to `host[:port][/selector]` so an
    entry can point at something other than a root menu. `docs/GOPHER.md` is
    written and indexed. The location bar now carries host and selector, which
    was the other item noted here.
  - **Paging fixed 2026-09-20**, and it turned up a real bug behind it. A text
    file is all info lines, so nothing in it is selectable, so the arrow keys
    found no target and returned -- meaning a document longer than 23 lines
    could not be read past the first screen. The arrows now pan the window when
    there is no selection. PgUp and PgDn move a screenful in one repaint rather
    than looping the single-step move, End was added, and a single step now
    repaints the two rows that changed instead of the whole body. Play-tested
    2026-09-20, including the paging via Fn+Up and Fn+Down on a MacBook.
  - **Left undone.** Binary retrieval (type `9`), which needs the modem raw mode
    logged separately above.
  - **Known limit: no binary retrieval.** The modem bridge always runs a telnet
    IAC filter, escaping outbound `$FF` as `IAC IAC` and reading inbound `$FF` as
    negotiation. Gopher text is 7-bit so this is invisible, exactly as it is for
    IRC, but type `9` transfers would be corrupted. Out of scope unless the modem
    grows a raw mode.
  - Size should land near IRC, 12-16 KB. Test it the way `tests/test_irc.cpp`
    does: feed canned menu bytes through the ACIA headless and assert on the
    rendered screen.

### DOS / filesystem — no seek, no bulk read

- [ ] **The filesystem has no seek and no bulk read.** Investigated 2026-09-14 while
  building the boot config; logged rather than built, because nothing is asking yet
  and the signature should be settled by a real caller rather than guessed.
  - **Both are period-correct and their absence is the odd case.** CP/M had
    read/write-random driven by a record number in the FCB; MS-DOS 1.x did random
    records the same way and 2.0 added the handle-based `LSEEK` everything since
    inherits; Commodore DOS had `POSITION` for relative files; Apple DOS 3.3 had
    `POSITION` and a record/byte parameter; Atari DOS had `NOTE` and `POINT`.
  - **They solve different problems, and it is worth not confusing them.** A *seek*
    fixes resuming — re-opening and re-scanning to get back where you were, which is
    O(n²). A *bulk read* fixes throughput. `_FS_GETB` costs about **40 instructions a
    byte**: a 32-bit `DOS_F_LEFT` test, a 32-bit decrement in `_DOS_DEC_LEFT`, a
    sector-boundary check, and the call itself — nearly all of it per-call overhead
    that a `_FS_READ(buf, n)` loop would amortise away.
  - **Seek would NOT have helped the case that surfaced this.** The startup config
    spends its time reading every byte of a comment to find the newline, and you
    cannot seek past something whose length you do not know yet. That wanted a bulk
    read. The O(n²) half was removed instead by only re-opening for a line that is
    actually dispatched.
  - **What seek needs, concretely.** Two pieces of state the open cursor does not
    keep: `DOS_F_CLUS` is mutated as you read so the FIRST cluster is lost, and
    `DOS_F_LEFT` counts down so the file SIZE is lost — seek needs both to land
    anywhere and recompute the remainder. FAT16 has no block index, so a seek is a
    chain walk, O(clusters); that is true of every FAT and not a flaw in ours.
  - **The revival condition:** a caller that settles the shape. Most likely a
    persistent score table (KERNEL PANIC wants one, and it is the natural
    read-modify-write case) or an EDIT that pages a document too large for RAM.
    `LSEEK`-style — an offset plus a from-start/current/end selector — is the
    expected landing point, since it composes better than record numbers and is
    where the handle-based era converged.

### KERNEL PANIC — steps 7 and 8

- [ ] **KERNEL PANIC** (`programs/kpanic/`, `KPANIC.PRG` 14,125 bytes) — original
  real-time vertical scroller; manual on the disk as `GAMES/KPANIC.TXT`. Build steps 1-6
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
  - **The decisions above were made before KPANIC had a test harness.** It has one now
    (`tests/test_kpanic.cpp`, the `kpanic` ctest target) — but when this work was done
    nothing could test a `.PRG`, so they were made by
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
  - **Steps 7-8, part done and MERGED to `main`** (`9e83229`, `9af5cac`). Parked
    2026-09-05 at Brian's call, mid-way through the juice. The note here previously said
    this sat unmerged on a `feat/kpanic-steps-7-8` branch; that branch was merged and
    deleted, so nothing is outstanding and there is no branch to go looking for.
    - **Done and play-approved:** a death blast (three rings expanding from the craft
      with the world frozen, ~⅓ s, then the panel — a run ending previously had no
      visible cause at all); the wall-hit flash, which was drawing the ship in *black*
      because `A_WARN | 0x80` set the reverse bit and a sprite has no cell behind it to
      swap with, so bit 7 could only ever darken it; and an end screen whose headline
      states the cause rather than repeating the game's name.
    - **Done, not needing eyes:** `KPANIC.TXT` on the disk beside the game; a saturating
      `add_score()`; and a real test harness — `kpanic_bin` plus eight tests in
      `tests/test_kpanic.cpp`, which is what the "no `.PRG` can be tested" note above
      was waiting for.
    - **Dropped on measurement:** the 2-word/BCD score. `unsigned long` cost 650 bytes
      and a myriad-pair 1,054 (cc65 emits a division helper per constant divide), and
      the harness put a generous score ceiling in the low thousands — five to ten times
      short of 65,535. See the score note in `kpanic.c`.
    - **Still open:** cell-offset screen shake; SID cues; the final balance pass, which
      wants doing last because juice changes how harsh the game feels without changing
      a number. A persistent score *table* is also still absent and would need the game
      to open a disk file, which it never does today.

### Display themes (potential, not planned)
- [ ] **Colour themes for the display, from kitty theme files.** Investigated
  2026-09-03; logged rather than started. The fit is a 1:1 map, not a conversion:
  `DisplayWidget::initPalette()` hardcodes `palette_[16]` in standard ANSI/CGA order
  (black, red, green, yellow, blue, magenta, cyan, white, then eight brights), which
  is exactly what a kitty `.conf` lists as `color0`..`color15`. The VIC attribute byte
  can only select those sixteen anyway -- bits 2-0 fg, bits 5-3 bg, bit 6 brightens.
  `github.com/dexpota/kitty-themes` is MIT, ported from iTerm2-Color-Schemes with
  authors cited per file, so vendoring a handful means carrying the licence and
  attribution in `NOTICE` the way EhBASIC already is. `selection_background` and
  `selection_foreground` have no meaning here; `cursor` is optional.
  - **There is no DOS command for this and there never was.** Checked the command
    table and searched history for `THEME`/`SETTINGS` across `src/kernel/`: nothing.
    Recollection of one is probably of a conversation. This matters because it is what
    decides the shape below.
  - **There is also no palette register.** The VIC exposes $FE2D-$FE37, the font port
    at $FE62-$FE64 and sprites at $FE65-$FECA; nothing lets the 6502 say what RGB an
    index means. So the two options are genuinely different pieces of work:
    - *Host-only*: the GUI reads a `.conf` into `palette_`, driven by a menu item or a
      `--theme` flag. ~50 lines, no new hardware, no ABI -- and no DOS verb is possible,
      because the guest cannot reach it.
    - *A palette port, then a real `THEME` verb.* An index/data register triple in the
      idiom the soft font already proved. Historically right (the VGA DAC and Amiga
      copper both worked this way) and it buys effects the index model cannot express:
      KPANIC's per-sector board colour becomes an actual fade rather than an index swap.
      **Preferred**, with one trick to keep the 6502 cheap -- do not parse `.conf` on
      the 6502. Keep the kitty file as the repo's source of truth, convert it at build
      time to 48 bytes (16 x RGB) with a host tool shaped like `mkprg`, stage it through
      the disk catalog, and let the DOS command blit 48 bytes at the port. Text parsing
      would otherwise be the expensive part of an otherwise cheap feature.
  - **Open question if this is built:** kitty separates `background` from `color0`
    (Dracula is `#1e1f28` against `#000000`), but here a cell with bg bits 0 *is* the
    background. Mapping bg 0 to `color0` is faithful to the attribute model; mapping it
    to `background` is what terminals do and what makes a theme look like its
    screenshot. The second is probably right and it changes how every theme reads.
  - **Two things to respect rather than solve:** green-on-black is the machine's face
    (the default attribute is $02, so a theme decides what the boot screen looks like),
    and programs pick colours by index deliberately -- VENTURE's per-level recolour,
    KPANIC's per-sector board, EDIT's status line, all tuned against the CGA palette.
    A low-contrast theme can make a game element hard to read, which is a reason to
    curate a few themes rather than ship the 200+ in that repo.

### Memory map (future, not urgent)

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

### Assembler and monitor extras

- [ ] Remaining from post-Phase-4: assembler macros + more directives; single-step/breakpoints in the monitor.

## Done

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
  - ASSEMBLER.md folded into MONITOR.md, left as a pointer, and **removed**
    2026-09-17. Its justification had gone circular: the only things still linking
    to it were the docs index row that existed to list it and this note. Everything
    it said -- 16-character identifiers, quoted `.BYTE`, the Supermon/HESMON
    rationale, the move of base conversion to `#:`/`$:` -- is in MONITOR.md, and it
    was an UPPERCASE file (a manual, by the docs convention) that documented nothing
    anyone uses. In git history if it is ever wanted.

### CPU emulation accuracy (surfaced by the Klaus2m5 / amb5l functional tests, 2026-06)

The emulated CPU is now a full **WDC W65C02S**. Validated against all three amb5l ca65 ports (kept local, GPL, never committed): 6502_functional_test ($3469), 6502_decimal_test built for 65C02 (ERROR=0, incl. invalid BCD), and 65C02_extended_opcodes_test with wdc_op/rkwl_wdc_op ($24F1). Locked by unit tests in tests/test_cpu_alu.cpp.

- [x] Decimal-mode N/V/Z flags: 65C02 ADC/SBC now set N/V/Z validly. addValues/subtractValues are faithful ports of the documented hardware algorithm, matching a real W65C02S even for invalid BCD inputs.
- [x] Complete the 65C02 opcode set in CPU6502: added RMB/SMB/BBR/BBS (Rockwell/WDC), the standard multi-byte NOP opcodes, BRK clearing the decimal flag, and JMP-indirect WDC timing.
  - [x] WAI/STP are no longer benign stubs (they used to decode and fall through, so a program using either just ran on). WAI now halts until IRQ/NMI is signalled — including a *masked* IRQ, which resumes without vectoring — and STP halts until reset. Covered by tests/test_cpu_interrupts.cpp.
  - [x] Bare "(zp)" indirect opcodes ($12/$32/$52/$72/$92/$B2/$D2/$F2) now decode as 1-byte zero-page-indirect (calculateZeroPageIndirectAddress), not 2-byte absolute-indirect.
- [x] Cycle-count accuracy: every defined 65C02 opcode now matches its datasheet cost. The bus helpers (readByte/readWord/pushByte/pullByte) each charged cycles while every handler ALSO added the instruction's full published count, so the opcode fetch alone put all 211 opcodes one cycle over and JSR cost 11 instead of 6. Nothing read the counter, so it never surfaced. Helpers no longer charge; PHA/PHP/PLA/PLP (written to the old remainder convention) were corrected to full counts. Locked by tests/test_cpu_cycles.cpp against datasheet reference data.
- [x] **Retired 2026-09-04** — Klaus2m5 interrupt test. Not done and not planned: it would re-cover ground `test_cpu_interrupts.cpp` already holds directly, for the price of an as65 toolchain and a harness extension. Worth revisiting only if the ca65 port appears upstream, at which point it is nearly free. Original note: (Optional) still not available in the amb5l ca65 port (as65 source only) and uses a memory-mapped IRQ/NMI feedback register, so it'd need an as65 build (or manual ca65 port) plus a harness extension. Lower value now that the v2.2 IRQ/NMI path has direct coverage: tests/test_cpu_interrupts.cpp asserts masking, level-sensitive IRQ re-entry, edge-triggered NMI and its priority over IRQ, the B bit distinguishing hardware entry from BRK, D cleared on entry, and RTI restore. That found one real bug — reset() left a latched NMI pending, so it vectored through $FFFA before the reset handler ran an instruction.

### Host file I/O (these two are one piece of work)
- [x] **`PIA::closeStream()` error reporting has no test.** **Fixed 2026-09-04**, and testing it found the reporting was being thrown away at the other end. `closeStream()` set the status correctly, but neither caller read it: DOS `EXPORT` wrote `FIO_CLOSE` and printed `EXPORTED` unconditionally, and BASIC `SAVE` returned without looking -- so a write onto an unwritable target reported success and lost the file, which is the exact failure the 2026-07 fix was for. `EXPORT` now checks and reports `HOST I/O ERROR`, proved by a test that fails against the unfixed ROM. **BASIC `SAVE`'s check ships untested**: `SAVE` takes no filename, so it cannot be driven without a GUI, and giving EhBASIC one is its own piece of work in a parser this project decided not to disturb. Recorded rather than pretended.
- [x] **Decide whether the PIA should honour the 6502-supplied filename at `$FE14-$FE1F`.** **Done 2026-09-04**, with a fallback rather than a replacement: a blank buffer still opens the dialog, so nothing that worked before changed. `IMPORT name[,host]` / `EXPORT name[,host]` write the second field to `$FE14`, and the PIA resolves it against the working directory (`bin/`, already where the ROMs and `disk.img` come from). Twelve bytes with no path syntax cannot escape that directory, and a name carrying a separator is refused rather than trimmed. The buffer had been dead on both sides -- `FILE_NAME_BUF` was defined and referenced nowhere, the PIA stored writes and never read them -- ever since v2.2.8 dropped the filename from `L:`/`S:` *because* the dialog owned the path. The bigger gain turned out not to be testability: all four host paths live behind `#ifdef QT_GUI`, so a console build could not load or save anything at all, and a named file is the first way host I/O works headless. **Not carried over:** BASIC `LOAD`/`SAVE` and the monitor's `L` still take no filename, so they remain dialog-only.

### Games
- [x] **VENTURE** (`programs/venture/`, `VENTURE.PRG` 18,227 bytes) — a port of Exidy's Venture
  (1981); manual shipped on the disk as `GAMES/VENTURE.TXT`. All ten
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
  - **Why this machine suited the port**, recorded before the build and borne out: the
    protagonist already ships in the character ROM (CP437 `$01` is an outline smiley,
    `$02` the filled one — see `venture.h`, which has the more current story of what
    happened to that two-frame animation); Venture predates twitch play, so one cell per
    tick at 15 ticks/sec is faithful rather than a compromise; and eight-way movement
    while firing is exactly the case the `$FE0F` control port exists for. The arcade
    zoomed from dungeon map to room, we switch screens — cheaper, and it reads better at
    80x25.
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
- [x] **FRONTIER FORTUNE** (`programs/frontier/`, `FRONTIER.PRG` 27,009 bytes) — a
  Wild-West trading game in the *Taipan!* / *Drug Wars* lineage; player manual on the
  disk as `GAMES/FRONTIER.TXT`. A port of the author's own 2008 Objective-C iPhone
  prototype — original game and design by Brian Gentry (Trestle Development), no
  third-party code involved. Menu-driven and turn-based: no real-time loop, no
  scrolling, no timing, which is the genre this machine was built to run.
  - **The economy is persistent world state, not dice — the one real departure.**
    `price[NTOWNS][NGOODS]` holds a live price for every good in every town at all
    times; nothing is regenerated on arrival. Towns have a *character* (short of a
    good, in surplus, both, or unremarkable) that reshuffles at ~3%/town/day, prices
    drift daily toward their town's normal with a ±3% walk, and **your own trades move
    the price on screen as you trade**, healing over about a week.
  - **Why that replaced the first attempt.** The original rerolled prices across their
    full range on arrival and layered a hidden ±60% "pressure" on top. Self-defeating
    twice over: the reroll noise was far larger than the effect, so the player could
    never perceive having caused anything, and pressure was applied only *inside* the
    reroll, so the price did not move until the next visit. A `glut` label appeared
    with no visible cause. Verified on the host after the change: selling 100 Water at
    $124 takes it to $69 immediately and it recovers to $114 over ten days, and the
    two-town shuttle earns $7.7k on trip one, $1.4k on trip two, and is **losing $16.6k
    a trip by trip ten** — it dismantles itself instead of printing money.
  - **Information rots, deliberately.** Everything you know about a town is from your
    last visit, prices *and* character, snapshotted into `seen_*`. The Ledger shows the
    remembered grid with staleness in days. Reading live values there would be
    clairvoyance and would destroy the reason to keep moving.
  - **Genre note:** Drug Wars, Dope Wars and Taipan! all reroll randomly each visit and
    have no persistent per-town character at all. The living economy is a deliberate
    addition, not a port of anything — pure rerolls punish the player for buying without
    any way to know where to sell.
  - **The loan shark is what makes the clock matter.** Debt compounds ~2% per day
    travelled (`debt += debt / 50`), so an untouched $5,500 becomes ~$17,800 by day 60.
    The prototype charged no interest, which left it with no pressure at all. Savings
    deliberately earn nothing — the Bank exists to keep cash away from road agents.
  - **Engine traps worth not rediscovering:**
    - **Money must be 32-bit `long`.** 100 units of Gold at $29,999 is ~$3,000,000 and
      cc65's `int` caps at 65,535, so every cash/debt/price-total path overflows if this
      is missed. `K_PRINT_DEC` (`$FF27`) already takes a pointer to 4 little-endian
      bytes, so 32-bit display is a solved problem.
    - No float anywhere; percentages are integer division (2% = `/50`).
    - Menus read the **keystroke buffer**, not the control port — the port is for
      real-time programs ([[control-port-keystate]] applies to KPANIC/VENTURE, not here).
    - Screens redraw wholesale on entry, so none of KPANIC's diff-rendering machinery is
      needed. Fixed arrays throughout, no `malloc`.
  - **Prototype bugs deliberately not carried over:** `getRandomItem()` and
    `getRandomEvent()` used `rand() % 9 - 1` / `rand() % 11 - 1`, which return **-1** and
    can also run past the end — out of bounds at both ends; `eLoseCargo` was an empty
    `case`; `customizeDescription:` returned nothing, so the `|X|` item-name placeholder
    in event text never substituted; and there was no interest, no day-60 ending and no
    score, so the prototype never closed its loop.
  - **Deferred: value-based market depth.** `MARKET_DEPTH` (200) is measured in **units**,
    so saturation only polices the cheap end. Ten bars of Gold move a price 10/200 = 5%;
    a hundred barrels of Water move it 50%. High-value goods dodge saturation entirely
    because you can never afford enough volume to shift the market. The fix is to move
    the price by **dollar volume traded** rather than unit count. Still unimplemented —
    `frontier.c` computes `p * qty / MARKET_DEPTH`.
    - Why it eventually matters: combined with the $100,000 debt ceiling, one leveraged
      trade on a crashed price is game-ending. Measured — Gold crashed $9,000 → $22,500
      with 0 extra wagons nets **+$133,000 in one day**, against Whiskey's +$82,475 (12
      wagons) and Feed's −$500. From a starting net worth of −$3,500 that ends the run on
      day two. The crash event is only ~0.4% per journey, which is why it is deferred
      rather than urgent.
    - What the same analysis **validated** and any fix must preserve: space binds below
      **$965/unit** and cash binds above it, giving three genuine tiers — high value
      (Gold, Lumber) borrow, wagons wasted; mid (Whiskey, Medicine, Guns) borrow *and*
      buy wagons, both bind; bulk (Feed, Water, Food) neither, since interest on max debt
      exceeds the whole margin. That answers whether borrowing is ever correct: it is,
      and knowing when is the skill.
- [x] **Retired 2026-09-04** — **OPCODE**. Not cancelled as a bad idea, retired as the
  wrong shape: the blocker below is not a hard part of the game, it is a different
  project standing in front of it. Two things would revive it, and neither is this
  game's work to do. If the machine ever grows a **watchdog timer and a write-protect
  range register** in the I/O page -- the sort of thing added routinely for the soft
  font, sprites and scroll regions -- then the sandbox is hardware and the interpreter
  is unnecessary. Or if OPCODE were built as a **host-side tool** rather than a `.PRG`,
  the problem never arises, because `CPU6502` is already the interpreter it needs.
  Kept in full because the four existing pieces are the interesting part of the idea.
  Original note: set aside, recorded so it is not lost. A puzzle game where each
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

### DOS / filesystem
- [x] **Dev-loop hazard: the host can rewrite the disk image under a running machine.** **Mitigated 2026-09-02.** `BlockDevice` takes a shared advisory claim on its image for the session and `mkdisk create`/`update` take an exclusive one before writing, so `ninja disk` now refuses with a message naming the cause instead of silently swapping media under a running machine; `--force` overrides, and `mkdisk read` is unaffected. Advisory, so nothing that ignores it breaks, and the claim dies with the process so there is no stale lock to explain. Best-effort on the machine side by design -- a missing image or a filesystem without working locks leaves the hazard as it was rather than refusing to boot. `host/ImageLock.h` carries the reasoning; four tests in `test_block_device.cpp` cover it, including one that spawns the real `mkdisk` and was proved to fail against the unfixed tool. The Windows branch (`LockFileEx`) is written but not compiled -- no toolchain here. The period-correct fix below is still the proper one and still hard to justify. Original analysis: Not a correctness gap in the DOS and not reachable by anyone playing it -- there is no eject. `setImagePath()` is called only from tests, so nothing in the GUI can swap media. The exposure is the build: `ninja disk` / `ninja everything` / `./build.sh` rewrite `cmake-build-debug/disk.img` with `std::ios::trunc`, and `BlockDevice` re-opens by path on every sector access, so a rebuild while the machine is up is adopted instantly with nothing to signal it. **What it costs.** Geometry is not the risk: `mkdisk` always builds at `kHostFat16Clusters` (4096), a fixed 2,122,752 bytes whatever the catalog holds, and the guest allocates within a BPB fixed at format time -- so neither catalog growth nor anything the 6502 writes can invalidate the cached layout. The risk is writes in flight. DOS carries live per-file state across the swap (`DOS_W_FIRST_CLUS`, `DOS_W_PREV_CLUS`, `DOS_F_CLUS`, `DOS_F_LBA`, and the directory position at $030D), all allocated against the old FAT, so a save or an assembler output landing across a rebuild stamps a directory entry pointing at a chain the new image does not agree with -- and the corruption is in the NEW image. The assembler is exposed at its input too: it streams source through `FS_GETB` and would happily assemble the first half of the old file joined to whatever now occupies those sectors. Edited text is safe; EDIT holds the document in RAM and only touches the disk on Ctrl-O/Ctrl-S. **Holding an open handle would not help**, which is worth recording because it looks like it should: `mkdisk` truncates in place rather than writing a temp and renaming, so a handle sees the mutation anyway -- and briefly sees a zero-length file mid-rebuild. The block device is nowhere near a hot path (disk access only on CATALOG/LOAD/SAVE and assembler I/O), so the syscalls saved are not a reason either. A handle would only isolate the machine if `mkdisk` wrote temp-and-rename; the two changes are a pair or neither. **Cheapest real mitigation** is on the host side: have the build refuse to rewrite `disk.img`, or warn, while an emulator has it open. The period-correct fix -- a media-changed bit in the block device's status register ($FE27, today only Ready/Error) plus a remount when the DOS sees it, the software equivalent of a card-detect line -- is real but hard to justify against a hazard only the build can trigger. Prior evidence that the DOS half is genuine: a test once mounted a 4096-cluster image over a 128-cluster one and the DOS wrote root directory sectors on top of FAT #1 (`REUSE   BIN` at FAT offset 512). The integration harness clears `DOS_MOUNTED` on swap as a stand-in.
- [x] **Most FAT16 tests run on a geometry no host would mount.** **Fixed 2026-09-02** by raising `kDefaultDataClusters` to `kHostFat16Clusters`, so a test image is now the same kind of volume the machine runs on. The speed argument the small default rested on had evaporated: it cost 0.4 s (`dos_fat16` 4.88 s -> 5.28 s) and no production code changed, since `mkdisk`/`mkfat16` already passed the host count explicitly. One test needed pinning rather than raising -- `testDosSaveDiskFullReclaims` is about running out of room, and 124 clusters of 4,096 leaves plenty, so it now asks for 128 explicitly and was confirmed to fail without that. `fsck.fat` on a default image now reads "16 bit entries" and exits clean, where before it invented corruption on a healthy volume. Still uncovered: the 17th bit of the FAT byte offset needs more than 32,768 clusters (a 16 MB volume) and is unreached at any geometry the suite builds. Original analysis: `kDefaultDataClusters` is 128 for test speed (83 KB per image vs 2.1 MB at `kHostFat16Clusters`), but FAT type is derived from the cluster count, not declared — under 4085 clusters the standard says FAT12, so host tools parse the 16-bit table as 12-bit and `fsck` output off those images is meaningless. It also skips real driver paths: at 128 clusters every cluster number fits in a byte and the whole FAT is one sector, while at 4096 the FAT spans 17 sectors and cluster numbers exceed 255, which is different arithmetic in `_DOS_FAT_SEEK`/`_DOS_READ_FAT_ENTRY`. Only two of the 42 `dos_fat16` tests pass `kHostFat16Clusters` (plus `fat16_roundtrip` throughout and the new interop check). Consider raising the default, or adding a host-geometry pass over the write-path tests.

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

### Monitor out of the kernel (2026-07-31)
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

### Memory map (2026-07-31)
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

### MFC-DOS phased build log (2026-07)

> Moved here from `docs/dos_internals.md`. The build record as it was written,
> with the addresses, kernel versions and module names that were in play at each
> step. Several have since moved, most notably the DOS ROM base, the assembler's
> buffers, and the assembler's own existence as a separate `ASM` module.
> `docs/dos_internals.md` and Part 2 of `docs/architecture.md` are current.

1. Block device — emulator `disk.img` + the `$FE24` registers + `Memory` routing;
   a 6502 sector read/write smoke test. (Small, foundational.) — DONE. `BlockDevice`
   (`include/computer/BlockDevice.h`, `src/computer/BlockDevice.cpp`) backs a host
   `disk.img` (lazily opened, auto-created, grows on write, reads zeros past EOF);
   `Memory` routes `$FE24-$FE28` to it; `Computer6502` owns it (default image
   `../disk.img`). Covered by `tests/test_block_device.cpp` (`block_device_unit_tests`).
2. FAT16 mount + read — mount, `CATALOG`, read a file by name; the FS ABI. (At
   this point, mount-on-Mac authoring already works.) Sub-steps:
   - **2.1 Memory-map shift — DONE.** The always-mapped DOS ROM at `$9000-$AFFF`
     was pulled forward to here (rather than phase 4) so the FS has its permanent
     home from the start. Emulator routes `$9000-$AFFF` to a `dos.rom` image (writes
     ignored; falls through to RAM if absent); user RAM is now `$0800-$8FFF`; BASIC
     `Ram_top → $9000`; the assembler's buffers moved to `$8000` (source) / `$7E00`
     (symbols). Stub `src/kernel/dos/dos.asm` (signature only) builds `dos.rom`.
     Covered by DOS-ROM cases in `tests/test_memory_banking.cpp`.
   - **2.2 — DONE.** Block-device equates + 512-byte sector read/write primitives
     (`BLK_READ_SECTOR`/`BLK_WRITE_SECTOR`, the 6502 side of `$FE24-$FE28`) + FS ABI
     stubs, all in `src/kernel/dos/dos.asm`. Stable entry points live in a **DOS ABI
     jump table at `$AF00`** (mirrors the kernel `$FF00` table): `DOS_COLD`, `FS_OPEN`/
     `FS_GETB`/`FS_PUTB`/`FS_CLOSE`/`FS_DIR_FIRST`/`FS_DIR_NEXT` (stubs, carry=error),
     `BLK_READ_SECTOR` (`$AF15`), `BLK_WRITE_SECTOR` (`$AF18`).

     **Register contract for the `$AF00` FS entries:** results come back in **A and
     the carry (carry set = error / EOF). `FS_GETB` preserves X and Y** — it used
     to destroy X only on the path that crosses a 512-byte sector boundary, which is
     the worst kind of bug: a read loop indexing with X passed every small-file test
     and corrupted itself on the 513th byte, so it is now explicitly preserved and
     pinned by `FsGetbPreservesXAcrossSectorBoundaries`. Every other FS entry
     leaves X undefined (`FS_PUTB` clobbers it at entry testing `DOS_W_MODE`, and
     the open/close/delete/rename paths run cluster and directory arithmetic through
     X); assume only A and carry survive those. The cc65 glue in `programs/*/glue.s`
     reloads X on return, which is why this went unnoticed for so long. DOS zero page uses the
     free `$3A-$5A` gap (`BLK_BUF_PTR=$3A`). Covered by `tests/test_dos_blockio.cpp`
     (`dos_blockio_tests`) which runs the real `dos.rom` routines.
   - **2.3 — DONE.** FAT16 read driver in `src/kernel/dos/dos.asm`, validated by
     `tests/test_dos_fat16.cpp` against host-built images (`tests/support/fat16_image.h`).
     - 2.3a: auto-mount (parse boot-sector BPB → sectors/cluster, FAT/root/data
       start, root entry count, cached in the `$0300` DOS state block) +
       `FS_DIR_FIRST`/`FS_DIR_NEXT` (root-dir walk, skipping deleted/LFN/volume
       entries, leaving the 32-byte entry in `DOS_ENTRY`).
     - 2.3b: `FS_OPEN` (parse 8.3 name, scan dir, arm the open-file cursor),
       `FS_GETB` (stream bytes across sector boundaries, following the FAT16
       cluster chain at cluster boundaries; carry=EOF), `FS_CLOSE`. Reads stream
       through the block device's sector buffer (no 512B RAM buffer); FAT lookups
       and dir scans use bounded skip-reads. `FS_PUTB` remains a stub (phase 3).
   - **2.4 — DONE.** Interactive surface + tooling:
     - `tools/mkfat16` creates a FAT16 `disk.img` (sample files by default, or host
       files added under derived 8.3 names), reusing the shared image builder. It
       sizes the volume as a genuine FAT16 (>= 4085 clusters), so macOS mounts it
       read/write and exchanges files with the machine. A `sample_disk` CMake
       target writes `<build>/disk.img`.
     - Temporary monitor command `@` (kernel.asm, v3.3): `@` catalogs the disk
       (names + sizes), `@NAME` types a file. It calls the DOS ABI at `$AF..`
       directly (the DOS ROM is always mapped), so no kernel `$FF00` change was
       needed; phase 4 replaces `@` with the real DOS shell.
     - Covered by `monitor_integration` (catalog, type, missing-file) against a
       mounted FAT16 image.
3. FAT16 write — create / `ERASE` / `SAVE`; full round-trip on the machine.
   - **3a — DONE.** The write engine in `dos.asm`: cluster allocation (scan the FAT
     for a free entry, mark EOC), FAT-entry read-modify-write (read the sector,
     skip to the entry, overwrite 2 bytes in the buffered sector, flush — no RAM
     sector buffer), free-chain, directory-slot find (reuse same-name + free old
     chain, else append), and `FS_OPEN(write)` / `FS_PUTB` / `FS_CLOSE` (stream
     bytes, allocate + chain clusters across boundaries, pad + flush the final
     sector, finalize the dir entry). Single + multi-cluster; truncate-on-reopen.
     `FS_OPEN` mode is passed in Y (0 = read, 1 = write). A C++ FAT16 parser
     (`Fat16ImageReader`) independently validates the on-disk format; covered by
     write/round-trip cases in `tests/test_dos_fat16.cpp`. (Single FAT copy;
     deleted-slot reclaim deferred.)
   - **3b — DONE.** `FS_DELETE` (scan, free the cluster chain, mark the directory
     entry `$E5`) at DOS ABI `$AF1B`. The temporary `@` preview gains write
     commands (kernel v3.4): `@-NAME` erases, and `@SSSS-EEEE=NAME` saves a memory
     range to a file (`FS_OPEN`-write + `FS_PUTB` loop + `FS_CLOSE`). Covered by
     erase/free-and-reuse cases in `dos_fat16_tests` and save/erase round-trip in
     `monitor_integration`. (Full machine round-trip: poke memory -> `@..=F` save
     -> `@` catalog -> `@F` type back.)
4. DOS shell as boot target — the pivot: fill the (already-present) `$9000-$AFFF`
   DOS ROM with the command shell, boot into the DOS prompt, the command set above,
   launch-by-name (command → ROM module → file), program-file loader. `MON`/`BASIC`/
   `ASM` launch their banks; the monitor is entered as a tool and returns to DOS.
   - **4.1 — DONE.** The boot pivot. RESET now `JMP DOS_COLD`; the machine boots into
     the MFC/OS shell (banner + `]` prompt) in `dos.asm`: read a line (BIOS
     `READ_COMMAND_LINE`), match a verb, dispatch. Verbs: `HELP`, `MON` (launches the
     monitor via the new `K_MON_ENTRY` `$FF1E` BIOS entry), `CATALOG`/`CAT`, `TYPE
     NAME`. The monitor gains `Q` (quit → `DOS_WARM` `$AF1E`). Kernel v3.5. The `@`
     preview + `B:` menu remain reachable through `MON` (retired in 4.2/4.3).
   - **4.2a — DONE.** DOS file verbs in the shell: `SAVE name,SSSS-EEEE` (writes the
     `.PRG` 2-byte load-address header then the range), `LOAD name[,AAAA]` (loads to
     the header's address, or an override), `ERASE name`, `RENAME old,new`. New
     `FS_RENAME` (DOS ABI `$AF21`) + a shared `_DOS_DIR_FIND_EXISTING` helper.
     Kernel v3.5.1 (MONITOR_MAIN resets its display state on launch). Covered by
     DOS round-trip cases in `monitor_integration` and `FS_RENAME` cases in
     `dos_fat16_tests`.
   - **4.2c — DONE.** Host bridge moved into the DOS as `IMPORT name` / `EXPORT name`
     (host file picker <-> a FAT16 file, reusing the PIA byte-stream that `L:`/`S:`
     used, now bridged to the FS via `FS_PUTB`/`FS_GETB`). The monitor's `L:`/`S:`
     host load/save are retired: removed from `CMD_INDEX_MAP` + help, and their
     handlers (`PARSE_CMD_LOAD`/`SAVE_CHECK`, `CMD_LOAD_FILE`, `CMD_SAVE_FILE`)
     excised - freeing ~180 bytes of kernel ROM (the two vacated jump-table slots
     map to a no-op). Kernel v3.7.
   - **4.2b — DONE.** Retired the temporary `@` preview: removed its dispatch,
     `CMD_CATALOG`/`CMD_TYPE`/`CMD_SAVE`/`CMD_ERASE` routines, the `FS_*`/`DOS_DIR_ENTRY`
     equates, and the `MSG_DOS_*` strings from `kernel.asm` (~550 bytes freed). The
     monitor is a pure debugger again; the DOS shell owns the file verbs. Kernel v3.6.
     The obsolete monitor `@` tests were removed (coverage is the DOS-level tests).
   - **4.3** — launch-by-name. Decisions: `ASM` launch name, ROM-module-first with
     `&NAME` override to force a disk program, programs return via `RTS`.
     - 4.3a — DONE. `RETURN_FROM_MODULE` (`$FF12`) now re-enters `DOS_WARM` (monitor-
       state save/restore dropped); new `K_LAUNCH_BY_NAME` ABI (`$FF21`) scans
       `MODULE_DIR` and `BANK_LAUNCH`es a match (assembler's name is `ASM`); the DOS
       resolves an unmatched verb to a module. `BASIC`/`ASM` run from `]` and return
       to `]`. The monitor `B:` bank menu is excised (`CMD_BANK_MENU`/`PARSE_CMD_BASIC`
       removed). Kernel v3.8.
     - 4.3b — DONE. Disk `.PRG` launch (`_DOS_RUN_FILE`): `FS_OPEN` the name, read
       the 2-byte load-address header, load the body there, then run it as a
       subroutine — clean stack with a `DOS_WARM` return pushed, so the program's
       `RTS` returns to `]`. A leading `&` forces this disk path over a same-named
       module. Unknown name → `COMMAND NOT FOUND`. Closes the loop: assemble in
       `ASM` → `SAVE NAME,start-end` → type `NAME` to run it.
5. Editor (module, bank) — full-screen, generic; edit/save FS files → full
   in-machine self-hosting (edit → assemble → run, all at the DOS).
6. (Later/optional) relocate the monitor to a bank; kernel ROM becomes a lean BIOS.

The filesystem phases, 1 through 3, are foundational and unchanged regardless of
the DOS framing. The pivot mainly reshaped phase 4 into a DOS shell rather than
file verbs bolted onto the monitor, and flipped the boot target.

### Bank-switched module slot: the design as argued (2026-06)

> Moved here from `docs/architecture.md` Part 4. This is the design of the module slot as it was argued at the time, including the addresses and sizes then in play (`$B000-$DFFF`, 12 KB, an 8 KB kernel at `$E000`). The window is now `$B000-$EFFF` (16 KB) under a 4 KB BIOS at `$F000`, and the monitor is bank 4. Part 2 of `docs/architecture.md` is the authoritative current map; the reasoning below is left as written.


**Status:** Phases 1–5 implemented (kernel v3.27). I/O is at `$FE00`, the module
window is a clean bank-switched slot (`MODULE_BANK` `$FE23`), **BASIC is module
bank 1, and a DEV TOOLS module is bank 2** (`src/kernel/assembler/`,
`assembler.rom`). `B:` is the module bank menu (driven by the kernel `MODULE_DIR`
catalog), modules return via `$FF12` (`RETURN_FROM_MODULE`, which unmaps the bank),
and `RESET` zeroes the window so bank 0 boots clean.

The dev-tools module (v0.7) provides:
- **Disassembler** (`D xxxx`) — decodes via the canonical 65C02 table generated
  from the CPU emulator (`tools/gen_opcode_table.py` → `opcodes_65c02.inc`).
- **Line assembler** (`A xxxx`) — immediate, no-file, numeric operands; quick patches.
- **Two-pass assembler** (`B`) — labels, `NAME = expr`, expressions (`+`/`-`,
  `<`/`>`), pseudo-ops `.ORG`/`*=`, `.END`, `.BYTE`/`.DB`, `.WORD`/`.DW`,
  `.ASCII`/`.TX`; build listing; `? LINE nnnn` errors.
- **Source load** (`L`) — reads a host `.s` file into the `$A000` source buffer via
  the byte-stream file interface; symbol table at `$9E00`.

It reaches the system only through the `$FF00` jump table (extended in Phase 4 with
`K_READ_LINE`/`K_PARSE_HEX`/`K_PRINT_HEX_BYTE`). Source can be authored either on the
host (`L`) or on the machine: the resident FAT16 filesystem and the full-screen
`EDIT` program (see [EDIT.md](EDIT.md)) close the loop, so **edit → assemble → SAVE →
run by name** all happen at the `]` prompt without host involvement.

### Goal

Stop growing (or shrinking) the kernel ROM to add big features. Instead, make the
12 KB region currently occupied by EhBASIC a bank-switched module window: a slot
into which the kernel maps one ROM "module" at a time (BASIC, an assembler/
disassembler package, a Z-machine to play Zork, a text editor, …). BASIC becomes
just one module rather than a permanent resident.

This mirrors how real 6502 machines did it — cartridge ROMs, bank-switched ROM,
the Apple II language card.

#### Why not just grow / shrink the kernel?

- A debugger-grade monitor wants a disassembler (~1–1.5 KB) and mini-assembler
  (~1–1.5 KB). Those don't belong in the always-resident kernel.
- Shrinking the kernel to 4 KB to reclaim user RAM would be undone the moment we
  add a disassembler. Modules sidestep the whole question.

The kernel stays at `$E000–$FFFF` (8 KB), unchanged in start address.

### Memory map (target)

```
$0000–$07FF   Zero page / stack / system vars / screen      (unchanged)
$0800–$AFFF   User RAM (~42 KB)                              (module working RAM)
$B000–$DFFF   MODULE WINDOW (12 KB) — backed by selected bank; clean, no I/O hole
$E000–$EF6E   Kernel CODE (~3.9 KB at v3.27)                 (start unchanged)
$EF6F–$FDFF   free kernel ROM (~3.6 KB)                      (kernel growth room)
$FE00–$FEFF   I/O page (relocated here from $DC00)
$FF00–$FFF9   Kernel API jump table (grows upward; ~83 entries possible, 20 used)
$FFFA–$FFFF   NMI / RESET / IRQ vectors
```

Key property: the module window contains no I/O — any ROM assembled at `$B000`
runs in a clean, contiguous 12 KB with no addresses to avoid.

### Prerequisite: relocate I/O out of the module window (`$DC00` → `$FE00`)

Today the PIA/file-I/O lives at `$DC00–$DC22`, inside the module window (a vestige
of the C64-style map). That was tolerable when BASIC was the only, hand-authored
occupant. For arbitrary module ROMs we can't enforce a "don't touch this 36-byte
window" rule, so we remove the constraint by moving the I/O.

The I/O shadow doesn't vanish — it moves from the module window (third-party ROM
territory) into the kernel ROM's unused space (our territory), where avoiding it is
trivial: the kernel's CODE ends at `$EEC3`, nowhere near `$FE00`. I/O is fixed at
**one page** (`$FE00–$FEFF`) — current usage is ~36 registers and even generous
expansion stays far under 256; page-aligned I/O is also natural to decode on real
hardware.

New I/O page layout (re-based 1:1 from the old `$DCxx` block):

| Addr | Register |
|------|----------|
| `$FE00` | `PIA_DATA` — keyboard data |
| `$FE02` | `PIA_CONTROL` |
| `$FE0E` | timer IRQ acknowledge |
| `$FE10` | `FILE_COMMAND` |
| `$FE11` | `FILE_STATUS` |
| `$FE12/$FE13` | `FILE_ADDR_LO/HI` |
| `$FE14–$FE1F` | `FILE_NAME_BUF` (12 bytes) |
| `$FE20/$FE21` | `FILE_END_ADDR_LO/HI` |
| `$FE22` | `FIO_DATA` — BASIC byte-stream LOAD/SAVE |
| `$FE23` | `MODULE_BANK` — bank-select register |
| `$FE61` | `POWER` — soft power switch; write $5A then $A5 to switch off |
| `$FE62–$FE64` | `VREG_FONT_LO/HI/DATA` — VIC soft-font port; index + auto-incrementing data. Font storage is inside the chip, not in the 64K map (see `docs/video_design.md`) |
| `$FE65–$FECA` | VIC sprites — 17 sprites x 6 bytes: X lo/hi, Y lo/hi (bit 7 = enable), glyph, attribute. Positions are nominal pixels on the 8x16 grid; a sprite is drawn over the cell planes and is NOT moved by the scroll region or the fine offset |

Touched by the relocation:
- `kernel.asm`: re-base the `PIA_*`, `FILE_*`, timer-ack equates.
- `basic.asm`: re-base `FIO_COMMAND`/`FIO_STATUS`/`FIO_DATA`.
- `src/computer/PIA.*` / `Memory`: update `isPiaAddress` (and any screen routing).
- `memory.cfg`: bound the `CODE` segment at `$FDFF` so the linker errors rather than
  growing into the I/O page.

Phase 1 (this relocation) is self-contained and worth doing on its own.

### Bank-select register — `MODULE_BANK = $FE23`

- **Write `n`:** map bank `n` into `$B000–$DFFF`.
  - `0` = RAM (slot is plain read/write RAM — the boot/default state).
  - `1…255` = read-only module ROM banks.
- **Read:** returns the current bank (kernel can save/restore).
- **Reset:** forced to `0`. BASIC is not auto-loaded; the slot starts empty.
- Lives in the always-mapped I/O page, so it's reachable regardless of what's mapped.

Bank capacity is bounded only by the register width: one byte → 256 banks × 12 KB
(~3 MB). We define a handful and leave the rest open.

### Emulator changes (`Memory`)

Bank-switched (not copy-on-demand): the host pre-loads each module image into a
`bankROM[1..N]` array at startup; switching is a pointer change — instant, and each
bank retains its own contents.

```
read(addr):
    if I/O addr ($FE00–$FEFF)              -> device / bank-register handler
    else if screen addr                    -> VIC
    else if $B000<=addr<=$DFFF and bank!=0  -> bankROM[bank][addr-$B000]   # read-only
    else                                   -> ram_[addr]

write(addr, v):
    if I/O addr                            -> device / bank-register handler
    else if screen addr                    -> VIC
    else if $B000<=addr<=$DFFF and bank!=0  -> ignored (ROM)
    else                                   -> ram_[addr]                    # bank 0 = RAM
```

### Module contract

A "module" is a 6502 ROM ported to this system:
1. Assembled to run from the module window (entry recorded in the directory below;
   `$B000` by default).
2. Reaches kernel services (character I/O, etc.) **only through the `$FF00` jump
   table** — the stable module ABI. (BASIC already does this via its `PG2_TABS`
   vectors.)
3. Returns to the monitor with `JMP $FF12` (`RETURN_FROM_MODULE`), which resets
   `MODULE_BANK = 0` and re-enters the command loop.
4. Uses `$0800–$AFFF` as working RAM, shared with all other modules → one tool at a
   time; "save your work before switching." Each module documents its RAM footprint.

A module is not required to reserve any specific bytes — there is no embedded
header or signature. Naming/entry metadata lives in the kernel (see below), so even
hard-to-modify third-party ROMs (a Z-machine, an off-the-shelf assembler) only need
the unavoidable port (re-base + retarget I/O), nothing more.

### Module directory (in the kernel ROM)

The kernel owns a curated catalog of known modules — like the `$FF00` jump table.
The `B:` menu and launcher read from it; the module ROMs stay untouched.

```
; One record per module: bank#, entry address, name pointer.
MODULE_DIR:
    .byte 1  : .word $B000 : .word NAME_BASIC      ; bank 1
    .byte 2  : .word $B000 : .word NAME_DEVTOOLS   ; bank 2
    .byte 3  : .word $B000 : .word NAME_ZORK       ; bank 3
MODULE_DIR_COUNT = 3

NAME_BASIC:    .byte "BASIC", 0
NAME_DEVTOOLS: .byte "ASSEMBLER / DISASSEMBLER", 0
NAME_ZORK:     .byte "ZORK (Z-MACHINE)", 0
```

Adding a module = add one record + name string, and add the ROM image to the host
bank set, then rebuild the kernel. The directory + names are tiny — well within the
~3.9 KB of kernel headroom.

### `B:` — Bank menu (replaces per-module commands)

`B:` is repurposed from "launch BASIC" to "Bank": it lists the directory and lets
you pick a module to map + run.

```
BANKS:
  1  BASIC
  2  ASSEMBLER / DISASSEMBLER
  3  ZORK (Z-MACHINE)
  ?
```

- Build the menu by walking `MODULE_DIR` and printing each name.
- On a numeric selection: store the record's bank in `MODULE_BANK`, then `JMP`
  (record's entry address). ESC cancels back to the monitor.
- Adding a module never needs a new kernel command — it just appears in the menu.

```
; selection -> record index
LAUNCH_FROM_DIR:
    ; A = directory index chosen
    ; load bank#, entry from MODULE_DIR record
    STA MODULE_BANK          ; map the bank in
    JMP (entry)              ; run the module

; $FF12 handler
RETURN_FROM_MODULE:
    STZ MODULE_BANK          ; unmap (slot back to RAM)
    ...                      ; return to the monitor command loop
```

(Optional sanity byte-check after mapping is allowed but not required — the directory
is the source of truth.)

### Host-side bank registry

At startup the emulator loads module images into the bank table instead of writing
BASIC into flat RAM:
- bank 1 ← `basic.rom`
- bank 2 ← `assembler.rom`
- (3–255 reserved)

A small name→file map (config or convention). Bank 0 is RAM (no image).

### Settled decisions

1. Naming/metadata → kernel-side `MODULE_DIR` table (not embedded headers, no
   per-module signature). Works for hard-to-modify third-party ROMs; BASIC is just
   directory entry 1, no special-casing.
2. First module → one combined "DEV TOOLS" ROM (bank 2): assembler and
   disassembler together (they share the opcode/mnemonic tables).
3. Feature placement → size-based split. Big debugger machinery (disassembler,
   mini-assembler, single-step, breakpoints) lives in modules. Small always-useful
   commands (register display, hex add/subtract, memory compare) stay resident in the
   kernel.
4. Bank 0 = RAM, usable as scratch (not persistent across module loads).
5. Module working RAM = documented per-module footprint in `$0800–$AFFF`; one tool
   at a time, save before switching.
6. `B:` = Bank menu, replacing the old `B:` and any per-module command.

### Migration

- EhBASIC → bank 1, unchanged content (same `$B000` entry); the host registers it
  as a bank instead of loading it at boot. Its file-I/O equates move with the I/O
  relocation. It becomes directory entry 1.
- Kernel grows only: the I/O relocation, `MODULE_BANK` handling, `MODULE_DIR`, and the
  `B:` menu/launcher.

### Implementation phases

1. [DONE, v2.2.7/8] Relocate I/O `$DC00` → `$FE00` (kernel + basic + emulator),
   reserve the I/O page via an `IORESV` segment so the linker errors if `CODE` grows
   into it. Re-tested (integration suite + BASIC LOAD/SAVE). Window is now clean.
2. [DONE, v2.2.9] Banking infrastructure: `MODULE_BANK` register (`$FE23`) +
   `Memory` window routing (bank 0 = RAM, 1..255 = read-only ROM) + host bank table
   (`Memory::loadBank`). `RESET` maps the window to RAM. Behavior-preserving: BASIC
   still loads into bank-0 RAM at `$B000`. Covered by `tests/test_memory_banking.cpp`
   (11 cases) and the unchanged integration suite.
3. [DONE, v3.0] Convert BASIC to bank 1: the host installs `basic.rom` as a
   bank (`Memory::loadBank(1, …)`) instead of flat RAM. Added the kernel `MODULE_DIR`
   catalog + the `B:` bank menu/launcher; `RETURN_FROM_BASIC` became
   `RETURN_FROM_MODULE` (`$FF12`) and now unmaps the bank on exit. `RESET` zeroes
   `$B000–$DFFF` so bank 0 boots clean (safe now that BASIC is a ROM bank). Factored
   `FILL_RANGE_CORE` out of `F:` and reused it for the window clear. Covered by
   `testBankMenu`/`testBankLaunch` in the integration suite.
4. [DONE, v3.1/3.1.1] First new module: combined assembler + disassembler
   in bank 2 (`assembler.rom`). Disassembler, line assembler, and a two-pass
   assembler (labels, expressions, `.ORG`/`.END`/`.BYTE`/`.WORD`/`.ASCII`, `=`),
   with host `.s` source load and a build listing. The module ABI was extended
   (`K_READ_LINE`/`K_PARSE_HEX`/`K_PRINT_HEX_BYTE`) so the module reuses the kernel
   instead of duplicating input/parsing/printing.

5. [DONE] In-machine authoring: the resident FAT16 filesystem (MFC-DOS,
   `$8800-$AFFF`) and the full-screen `EDIT` program mean source is written and
   saved on the machine rather than host-loaded. Self-hosting is complete.

Still open: a richer assembler (macros, more directives); single-step and
breakpoints in the monitor; more modules; and the undecided monitor-to-bank
relocation.

### Early monitor and BASIC fixes

- [x] Z: & T: commands are updating the current address to 00FF and 01FF respectively and they shouldn't.
- [x] Fix BASIC token parsing (e.g. enter 10 FOR I = 1 TO 10) and that is not what prints when you LIST

### BASIC integration fixes
- [x] LOAD/SAVE I/O vectors (PG2_TABS) pointed at $FF0F = the RNG routine. Resolved by implementing real BASIC SAVE/LOAD: SAVE writes the program as ASCII .bas text and LOAD reads it back (via a new byte-stream mode on the PIA file I/O). VEC_SV/VEC_LD now point at BASIC_SAVE/BASIC_LOAD, so the RNG bug is gone.
- [x] INIT_BASIC_IO removed (dead code); PG2_TABS is the single source of truth for the BASIC I/O vectors.
- [x] IRQ/NMI wired (v2.2): CPU IRQ/NMI dispatch + a ~60Hz PIA interval timer (BASIC ON IRQ) + NMI stop key (BASIC ON NMI / break to monitor). The kernel ISRs set EhBASIC's "happened" bit.

### BASIC label rewrite
- [x] Resolved via a glossary rather than a rename. EhBASIC's upstream is unmaintained (Lee Davison deceased) so parity is no longer a goal, but a full in-place rename of ~780 code labels was judged not worth the risk/effort. Instead, docs/basic_label_glossary.md (now part of docs/basic_internals.md) maps the cryptic LAB_<hex> labels (and the named handlers) to their meaning, drawn from the source comments. The ROM is left untouched. (Also added the required "Derived from EhBASIC" attribution: in the BASIC sign-on banner and the root NOTICE file.)

### Kernel code-quality refactors (ROM has ~4KB free; these are maintainability, not space)
- [x] Factor duplicated idioms: added PRINT_HEX_BYTE (byte->2 hex digits to screen), PRINT_MSG_AY (set MON_MSG_PTR from A/Y and print, replacing 13 inline copies), and shared SKIP_SPACES/EXPECT_COMMA parser helpers (replacing the skip-spaces/comma preamble duplicated across the F:, M: (x2), X:, and L:/S: filename parsers). CODE segment dropped from ~4185 to 3946 bytes; all tests pass.
- [x] Remove dead code: deleted unreferenced NIBBLE_TO_HEX_CHAR/NIBBLE_DIGIT, unused constants (MON_HEX_DIGITS, CURSOR_CHAR, ASCII_0/9/A/F, FILE_IDLE, FILE_ERROR), and the MOVE copy-vs-move branch that printed identical text. HELP_MSG_COUNT was kept and wired into the help loop (replacing a magic #30) rather than deleted.

### Documentation
- [x] docs/kernel_memory_map.md (now consolidated into docs/architecture.md, Part 2) and the kernel.asm header rewritten to match the actual system ($E000 ROM, $14-$39 monitor ZP, relocated page-2 vars, PIA I/O, no C64 banking/VIC/SID). DEC_DIGIT_BUFFER now defined as "= MON_SEARCH_PATTERN" instead of a literal.
- [x] Done via the #65 docs consolidation: docs/system_architecture.md was merged into docs/architecture.md (Part 1 — System overview) and its stale C64-style $D000 I/O / VIC-II / SID / CIA / banking description was dropped. The authoritative memory map now lives in docs/architecture.md, Part 2.

### Bankable module slot
- [x] Phase 1 (v2.2.7/8): relocate I/O $DC00 -> $FE00, reserve the I/O page (IORESV), clean the $B000-$DFFF window.
- [x] Phase 2 (v2.2.9): banking infrastructure - MODULE_BANK register ($FE23), emulator Memory window routing (bank 0=RAM, 1..255=ROM), host bank table (Memory::loadBank), RESET maps window to RAM. Behavior-preserving; BASIC still in bank-0 RAM. Covered by tests/test_memory_banking.cpp.
- [x] Phase 3 (v3.0): BASIC is now module bank 1 (host installs basic.rom as a bank, not flat RAM). Added the kernel MODULE_DIR catalog + the B: bank menu/launcher; RETURN_FROM_BASIC -> RETURN_FROM_MODULE ($FF12) unmaps the bank on exit; RESET zeroes the $B000-$DFFF window so bank 0 boots clean. Factored FILL_RANGE_CORE out of F: and reused it. Covered by testBankMenu/testBankLaunch; integration harness now returns non-zero on failure so ctest catches regressions.
- [x] Phase 4 (v3.1/3.1.1): DEV TOOLS module in bank 2 (src/kernel/devtools/, devtools.rom). Disassembler (D), line assembler (A), two-pass assembler (B) with labels/expressions/.ORG/.END/.BYTE/.WORD/.ASCII/=, host .s source load (L), and a build listing. Canonical 65C02 opcode table generated from CPU6502 (tools/gen_opcode_table.py) with a drift-guard test. Module ABI extended: K_READ_LINE/K_PARSE_HEX/K_PRINT_HEX_BYTE ($FF15/$FF18/$FF1B). Sub-steps 1-6 committed on feat/devtools-module.
  - [x] In-machine generic text editor + resident filesystem: both shipped. MFC-DOS ($9000-$AFFF) is the resident FAT16 filesystem, and EDIT (programs/edit, docs/EDIT.md) is the full-screen editor. Self-hosting is complete — edit -> assemble -> SAVE -> run by name, all at the `]` prompt.
