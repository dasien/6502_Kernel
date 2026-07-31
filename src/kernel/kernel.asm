; ================================================================
; MFC 6502 KERNEL MONITOR
; ================================================================
; Filename:     kernel.asm
; Author:       Brian Gentry
; Date:         2026-06-08
; Version:      3.27
; Assembler:    ca65
;
; Description:  Machine language monitor for MFC 6502 system
;               Provides memory examination, modification, and
;               program execution capabilities via serial terminal
;
; ================================================================
; MEMORY USAGE SUMMARY
; ================================================================
; ROM (Reserved):  $F000-$FFFF (4096 bytes)
; ROM (Used):      ~4273 bytes
;   CODE segment:  $E000-$EF6E (3951 bytes)
;   IORESV segment:$FE00-$FEFF (256 bytes) - reserved I/O page (shadowed by host)
;   JUMPS segment: $FF00-$FF3B (60 bytes) - kernel API jump table (20 entries)
;   VECS segment:  $FFFA-$FFFF (6 bytes)  - NMI/RESET/IRQ vectors
;
; Zero Page:    placed above EhBASIC's $00-$13 and below its ~$5B-$FF (~21 bytes used)
;   Monitor:    $14-$1B core pointers; $21-$28 counters/flags/RNG + M: scratch;
;               $35-$39 decimal workspace
;   (free):     $1C-$20 (former scroll vars) and $29-$34 (former hex table)
;
; RAM Usage:    $0200-$02DD
;   Cmd buffer: $0200-$024F (80 bytes) - overlaps BASIC's page-2 area,
;               but the monitor and BASIC never run concurrently
;   Variables:  $0269-$028D - relocated above BASIC's $0200-$0268
;   Last cmd:   $028E-$02DD (80 bytes)
;
; Stack:        $0100-$01FF (256 bytes)
; Screen RAM:   $0400-$07E7 (1000 bytes, 40x25 text)
; I/O page:     $FE00-$FE23 (PIA: keyboard, file I/O, timer; $FE23 MODULE_BANK).
;               Moved here from the old $DC00 so $B000-$EFFF is a clean, bankable
;               module window (see docs/ARCHITECTURE.md, Part 4).
;
; ================================================================
; FEATURES
; ================================================================
; - Interactive command processor
; - Memory read/write/dump/fill/move/search operations
; - Program execution (G) and file load/save (L/S)
; - Hex<->decimal conversion (D/H)
; - Screen scrolling with paging
; - Built-in help system
; - Bankable module slot: B: is a menu that maps a ROM module (BASIC = bank 1)
;   into the $B000-$EFFF window and runs it; modules return via $FF12
; - Kernel API at $FF00 for user programs (the module ABI)
;
; Commands:     R:(read) W:(write) F:(fill) M:(move/copy) X:(search)
;               G:(go) L:(load) S:(save) D:(dec->hex) H:(hex->dec)
;               C:(clear) T:(stack) Z:(zero page) B:(bank menu) ?:(help)
;               ESC (exit current mode)
;
; ================================================================
; BUILD INFORMATION
; ================================================================
; ROM base:     $F000 (RESET label); reset vector at $FFFC
; Vectors:      NMI ($FFFA), RESET ($FFFC), IRQ ($FFFE)
; API entry:    $FF00 (jump table)
; CPU:          65C02 (.PC02)
;
; Assembly:     ca65 kernel.asm -o kernel.o && ld65 -C memory.cfg kernel.o -o kernel.rom
;
; ================================================================
; REVISION HISTORY
; ================================================================
; 2025-06-01  v1.0  Initial monitor release
; 2025-10-18  v2.0  EhBASIC interpreter integration; D:/H: conversion commands
; 2026-06-06  v2.1  Working BASIC (LIST/FOR-NEXT/launch fixes) + LOAD/SAVE
;                   file I/O; monitor & CPU correctness fixes; test suites
; 2026-06-07  v2.2  Hardware IRQ/NMI support: periodic timer IRQ (BASIC ON IRQ),
;                   NMI stop key (BASIC ON NMI / break to monitor); removed dead
;                   INIT_BASIC_IO
; 2026-06-07  v2.2.1 Code-quality cleanup (behavior-preserving): removed dead
;                   code and unused constants; factored shared helpers
;                   (PRINT_HEX_BYTE, PRINT_MSG_AY, SKIP_SPACES/EXPECT_COMMA).
;                   Alphabetized the ? help list and clarified the M: mode
;                   digit (B:0=COPY 1=MOVE).
; 2026-06-07  v2.2.2 H: hex-to-decimal now converts via the double-dabble
;                   binary->BCD algorithm using the CPU's decimal mode (fixed
;                   16 iterations) instead of repeated DIVIDE_BY_10 subtraction
;                   (up to ~7300 passes for $FFFF); removed the dead DIVIDE_BY_10.
; 2026-06-08  v2.2.3 Bug fixes: D: decimal overflow no longer corrupts the stack
;                   (unbalanced PLA); M: now copies AND clears correctly when the
;                   destination overlaps the source (incl. dest == source-end) so
;                   no bytes are lost; bare ESC at the prompt is a clean no-op.
;                   Optimizations: reclaimed 16 zero-page bytes ($25-$34) by
;                   replacing the runtime hex table with computed NIBBLE_TO_ASCII;
;                   removed dead ADDR_TO_HEX_QUAD; used 65C02 (zp) indirect on the
;                   PRINT_CHAR / scroll hot paths; moved cursor-save slots to RAM.
; 2026-06-08  v2.2.4 65C02 idiom modernization (behavior-preserving): PHX/PHY/PLX/PLY
;                   register saves; INC A; (zp) zero-page-indirect on the fill,
;                   write, dump, and show-address paths; BRA for in-range jumps;
;                   and a complete STZ pass for zeroing memory/variables.
; 2026-06-08  v2.2.5 Size/structure cleanup (behavior-preserving): 5 inline hex-print
;                   idioms folded into PRINT_HEX_BYTE (dead BYTE_TO_HEX_PAIR removed);
;                   message-printer wrappers and CMD_CLEAR_SCREEN/CMD_RUN_PROGRAM
;                   collapsed to tail calls; shared SCROLL_AND_HOME_BOTTOM helper.
;                   SCROLL_SCREEN rewritten as absolute,X page copies (~18 bytes
;                   smaller, faster, frees the $1C-$20 scroll zero-page vars).
; 2026-06-08  v2.2.6 Maintainability: the ? help now also lists the ? and .
;                   commands; named ASCII_COMMA and the '>'/'W' literals; fixed
;                   stale MON_MODE / jump-table / FILL+MOVE comments; inlined the
;                   READ_MEMORY_RANGE / SHOW_READ_ADDRESS pass-through wrappers.
; 2026-06-08  v2.2.7 Relocated memory-mapped I/O from $DC00 to a reserved page at
;                   $FE00-$FEFF (inside the kernel region) so $B000-$EFFF is a
;                   clean, I/O-free window. Phase 1 of the bankable module-slot
;                   plan (docs/ARCHITECTURE.md, Part 4). Behavior-preserving.
; 2026-06-08  v2.2.8 SCROLL_SCREEN page copies made strictly sequential (P0..P3):
;                   the interleaved form corrupted bytes spanning a screen page
;                   boundary on every scroll (seen via Z:/T:/repeat-? scrolling).
;                   Added a scroll-integrity regression test. L:/S: no longer take
;                   a filename (L:XXXX / S:XXXX-YYYY): the host file dialog owns the
;                   filesystem path, so the kernel name was ignored anyway; removed
;                   the now-dead PARSE_FILENAME routine.
; 2026-06-08  v2.2.9 Phase 2 of the module slot: bank-switching infrastructure.
;                   Added the MODULE_BANK register ($FE23) - write n to map bank n
;                   into the $B000-$EFFF window (0=RAM, 1..255=ROM modules), read to
;                   query. RESET maps the window to RAM (slot starts empty). Emulator
;                   Memory routes the window per the selected bank and provides a host
;                   bank table (loadBank). No behavior change yet: BASIC still loads
;                   into bank-0 RAM at $B000; conversion to a real bank is Phase 3.
; 2026-06-09  v3.0  Phase 3 of the module slot: BASIC becomes module bank 1 (the host
;                   installs basic.rom as a bank instead of flat RAM). B: is now the
;                   module bank menu - it lists MODULE_DIR (kernel-side catalog of
;                   bank#/entry/name), and a selection maps the bank and JMPs in.
;                   RETURN_FROM_BASIC -> RETURN_FROM_MODULE ($FF12) now unmaps the
;                   bank (window back to RAM) on exit. RESET zeroes the $B000-$EFFF
;                   window so bank 0 boots clean. Factored FILL_RANGE_CORE out of the
;                   F: command and reused it for the window clear. Folded the three
;                   duplicate command-buffer clears (ESC-cancel, '.' recall, module
;                   return) into one CLEAR_CMD_BUFFER routine.
; 2026-06-09  v3.1  Phase 4: dev-tools module (bank 2) with a disassembler. Extended
;                   the module ABI with three services so modules reuse the kernel
;                   instead of duplicating it: K_READ_LINE ($FF15, edited line input),
;                   K_PARSE_HEX ($FF18), K_PRINT_HEX_BYTE ($FF1B). These share the
;                   monitor's command buffer and MON_CURRADDR as scratch; the launch
;                   save/restore now also preserves MON_CURRADDR so a module's use of
;                   it is invisible to the monitor on return.
; 2026-06-11  v3.1.1 Monitor prompt starts at the left margin: PRINT_MONITOR_PROMPT
;                   emits a newline first when the cursor is mid-line (e.g. after a
;                   G:-run program left output without a trailing CR), so the prompt
;                   no longer trails program output. No blank line when already at
;                   column 0.
; 2026-06-11  v3.2  Release: the bankable module slot is feature-complete. The DEV
;                   TOOLS module (bank 2, devtools.rom) adds a native 65C02
;                   disassembler and a line + two-pass assembler (labels, expressions,
;                   .ORG/.END/.BYTE/.WORD/.ASCII/=, host .s load, listing). The kernel
;                   ROM is unchanged from v3.1.1; v3.2 is the system version for the
;                   release that includes the module.
; 2026-06-14  v3.3  MFC-DOS phase 2: a temporary '@' monitor command previews the
;                   resident FAT16 filesystem in the always-mapped DOS ROM ($8800-
;                   $AFFF) - '@' catalogs the disk.img, '@NAME' types a file. The
;                   monitor calls the DOS ABI ($AF.. : FS_DIR_FIRST/NEXT, FS_OPEN/
;                   GETB/CLOSE) directly. Phase 4 replaces '@' with the real DOS shell.
; 2026-06-14  v3.3.1 MFC-DOS phase 3a adds FAT16 write, so FS_OPEN now takes its
;                   mode in Y (0 = read). The '@' TYPE command sets Y = 0 before
;                   calling FS_OPEN. ROM is otherwise unchanged.
; 2026-06-14  v3.4  MFC-DOS phase 3b: the '@' preview gains write commands -
;                   '@-NAME' erases a file (FS_DELETE), and '@SSSS-EEEE=NAME' saves
;                   a memory range to a file (FS_OPEN-write + FS_PUTB + FS_CLOSE).
;                   Still a temporary preview; phase 4 brings the real DOS shell.
; 2026-06-14  v3.5  MFC-DOS phase 4.1 - the boot pivot: RESET now boots into the
;                   MFC/OS DOS shell (JMP DOS_COLD) instead of the monitor. The
;                   monitor is launched by the DOS 'MON' command (K_MON_ENTRY at
;                   $FF1E) and exited with 'Q', which returns to the shell (DOS_WARM).
; 2026-06-15  v3.5.1 Phase 4.2a: MONITOR_MAIN now resets its display state
;                   (MON_CURRADDR, MON_MODE) on entry, so a monitor launched by
;                   MON starts clean rather than inheriting the DOS shell's scratch
;                   use of MON_CURRADDR. (DOS file verbs themselves live in dos.rom.)
; 2026-06-17  v3.6  Phase 4.2b: retire the temporary '@' preview command. The DOS
;                   shell now owns the file commands (CATALOG/TYPE/SAVE/LOAD/ERASE/
;                   RENAME), so the monitor is a pure debugger again. Removes the
;                   '@' dispatch, its CMD_* routines, the FS_*/DOS_DIR_ENTRY equates,
;                   and the MSG_DOS_* strings.
; 2026-06-17  v3.7  Phase 4.2c: host <-> filesystem transfer moves to the DOS as
;                   IMPORT/EXPORT (host file picker <-> a FAT16 file). The monitor's
;                   L:/S: host load/save are retired - dropped from CMD_INDEX_MAP and
;                   help, and their handlers (PARSE_CMD_LOAD/SAVE_CHECK, CMD_LOAD_FILE,
;                   CMD_SAVE_FILE) excised; the freed jump-table slots map to no-ops.
; 2026-06-20  v3.8  Phase 4.3a: launch-by-name for ROM modules. RETURN_FROM_MODULE
;                   ($FF12) now re-enters the DOS shell (DOS_WARM) instead of the
;                   monitor, so BASIC/ASM launched from the DOS return to the ']'
;                   prompt. New K_LAUNCH_BY_NAME ABI ($FF21) scans MODULE_DIR (the
;                   assembler's name is now "ASM"). The monitor's B: bank menu is
;                   retired (CMD_BANK_MENU/PARSE_CMD_BASIC excised, B -> invalid).
; 2026-06-24  v3.9  GET_KEYSTROKE ($FF09) now returns the key AS TYPED instead of
;                   force-folding a-z to uppercase, so lowercase-aware programs
;                   (the upcoming editor) get real case. Shell command lines stay
;                   uppercase (READ_COMMAND_LINE already folds), and BASIC's input
;                   vector points at a folding wrapper (KEY_UC) for its tokenizer.
; 2026-06-25  v3.10 New K_LIST_MODULES ABI ($FF24) prints the module catalog
;                   (bank + name per MODULE_DIR record), backing the DOS 'BANKS'
;                   command.
; 2026-06-25  v3.11 Registered FIG-Forth 6502 as module bank 3 ("FORTH") in
;                   MODULE_DIR. Launched by name from the DOS; listed by BANKS.
; 2026-06-26  v3.12 Promoted decimal conversion to the BIOS ABI: K_PRINT_DEC
;                   ($FF27, 32-bit value -> decimal, right-justifiable) and
;                   K_PARSE_DEC ($FF2A, decimal string -> 16-bit). The monitor
;                   H:/D: now share these; the DOS uses K_PRINT_DEC for CATALOG
;                   sizes and DISKFREE. PARSE_DECIMAL_VALUE no longer prints (it
;                   returns an error code); H: dropped its private double-dabble.
; 2026-06-27  v3.13 80-column display (phase B): the screen moved out of the 64K
;                   map and behind the VIC register port ($FE2D-$FE36). SCREEN_WIDTH
;                   is now 80; the old 16-bit SCREEN_PTR is gone (its ZP is now the
;                   VID_CELL/VID_TMP cell-index scratch). PRINT_CHAR/newline/backspace
;                   write through VREG_CHAR at cell = CURSOR_Y*80+CURSOR_X; SCROLL and
;                   CLEAR are single chip-side commands (VCMD_SCROLL_UP / VCMD_CLEAR).
;                   The displayed cursor is pushed to VREG_CURSOR. BASIC TWidth -> 80.
; 2026-06-27  v3.14 80-column display (phase C): exposed the color/attribute latch
;                   as K_SET_ATTR ($FF2D, A -> VREG_ATTR). Subsequent PRINT_CHAR
;                   output takes that attribute (byte = [R][BR][bg:3][fg:3]; default
;                   $02 = green on black). Appended to the jump table so all prior
;                   $FF00 offsets stay byte-stable. The ANSI terminal will use this.
; 2026-06-27  v3.15 80-column display (phase E): re-centered the boot welcome
;                   message for 80 columns (28 leading spaces); R:/T:/Z: memory
;                   dumps now show 16 bytes per line (was 8) to use the width.
; 2026-06-28  v3.16 Boot is now silent: the kernel drops its OPERATIONAL welcome
;                   and hands straight to the DOS shell, which draws the unified
;                   sign-on splash box (it owns the OS version + free-memory
;                   figures). See _DOS_SPLASH in dos.asm.
; 2026-06-28  v3.17 Consistent app sign-on banners (MFC <NAME> [vVER] + key hints).
;                   The monitor clears the screen on cold entry (MONITOR_COLD) and
;                   prints "MFC MONITOR   ?=HELP  Q=QUIT"; its ? help now tabs each
;                   description to a fixed column (PRINT_HELP_LINE, 80-col aware).
;                   Companion banner edits live in the modules/programs (ASM, FORTH,
;                   CHESS, ScottFree, EDIT, TERM).
; 2026-07-02  v3.18 System-wide pager: PRINT_CHAR now counts newlines and pauses
;                   every LINES_PER_PAGE with the --MORE-- prompt (PAGE_ADVANCE),
;                   gated by PAGE_ENABLE (default on) and reset per command in
;                   GET_KEYSTROKE. So any program that prints through K_PRINT_CHAR
;                   (DOS shell, MON, BASIC, ASM, FORTH) is paged with no code of
;                   its own. MON's per-command counter and DOS's separate MORE
;                   pager collapse into this one core.
; 2026-07-02  v3.19 Exposed PRINT_HELP_LINE in the ABI as K_PRINT_HELP_LINE
;                   ($FF30): prints "syntax"<TAB>"description" with the TAB padded
;                   to a fixed column. Lets the DOS HELP list use the monitor's
;                   two-column layout instead of a flat list (no duplicated code).
; 2026-07-02  v3.20 CLEAR_SCREEN clears in the default attribute ($02): a program
;                   that left the color latch on another color (e.g. TERM after a
;                   BBS) now hands back a default-colored screen, so the cursor
;                   (drawn from its cell's stored color) isn't a stale color.
; 2026-07-05  v3.21 SID sound-chip integration: ASCII BEL ($07) rings a short
;                   non-blocking beep (auto-gated-off by the timer IRQ); new sound
;                   ABI K_SOUND_TONE ($FF33) / K_SOUND_OFF ($FF36) plays/stops a
;                   tone on voice 1. Both honor SOUND_ENABLE (default on; the hook
;                   for a future SETTINGS mute).
; 2026-07-25  v3.22 RNG rework: seeded from the RTC instead of a constant, a
;                   16-bit Galois LFSR (period 65535) replaces the 8-bit one, and
;                   GET_RANDOM_NUMBER reduces to 1..RNG_MAX by multiply-high
;                   instead of rejection sampling (no more short cycles).
; 2026-07-28  v3.23 K_GET_JIFFIES ($FF39): the timer IRQ now advances a 16-bit
;                   monotonic tick counter (JIFFY_LO/HI at $31/$32) that programs
;                   read for frame pacing. The read is SEI-guarded so the two
;                   bytes can't tear. First consumer: real-time games.
; 2026-07-29  v3.27 T: and Z: snapshot the page before dumping it. The dump walks
;                   MON_CURRADDR ($14/$15) as its cursor and PRINT_CHAR rewrites its
;                   own scratch ($16/$17, $1A-$1D) between bytes, so a live Z: showed
;                   the dump's own state where the kernel's workspace lives: poke
;                   $14/$15 with W: and Z: reported "14 00", not what was poked.
;                   DUMP_ONE_PAGE now copies the page to MON_SNAP_BUF ($0400, free
;                   RAM) with absolute,X -- any zero-page pointer used for the copy
;                   would appear in its own dump -- and arms MON_DUMP_SNAP so
;                   DUMP_MEMORY_RANGE reads the copy. Disarmed on exit, so R: and W:
;                   always read live memory.
; 2026-07-29  v3.26 ABI contract fixes. K_READ_LINE ($FF15) now really returns the
;                   line length in A with Z set for an empty line, as documented --
;                   both exits tail-jumped to PRINT_NEWLINE, and PRINT_CHAR preserves
;                   A, so callers always got $0D. PAGE_ADVANCE now saves MON_MSG_PTR
;                   alongside X/Y: a message containing an embedded $0D that filled
;                   the page had its pointer re-pointed at the --MORE-- prompt by
;                   HANDLE_PAGE_BREAK and printed the prompt's tail instead of its own
;                   remaining lines (confirmed, not theoretical). Also corrects the
;                   ARCHITECTURE.md claim that NMI/IRQ are "a bare RTI" and documents
;                   K_PRINT_HELP_LINE's input.
; 2026-07-29  v3.25 Size pass, behaviour-preserving: 178 bytes freed (4067 -> 3889).
;                   Deleted SAVE_MONITOR_STATE/RESTORE_MONITOR_STATE, which no
;                   longer had a single caller, and their four save variables.
;                   Turned 16 "JSR cmd + JMP PARSE_CMD_DONE" pairs and 17
;                   "JSR x + RTS" pairs into tail jumps (PARSE_CMD_DONE is a bare
;                   RTS, so those were tail calls written the long way). Merged T:
;                   and Z: into DUMP_ONE_PAGE, which took a page number -- they
;                   differed only in the page and in how they preserved the current
;                   address (T: parked it in M:'s destination variable; both use
;                   the stack now). Dropped an unreachable duplicate end-of-command
;                   test in PARSE_COLON_COMMAND and three stores to variables
;                   nothing reads (MON_LINE_COUNT, MON_CMDPTR, MON_PARSE_LEN), plus
;                   the never-referenced MON_MSG_TMP_POS.
; 2026-07-28  v3.24 Bulk-write and break-path safety. F: and M: now refuse a range
;                   that covers the monitor's own loop state (RANGE_HITS_STATE /
;                   DEST_HITS_STATE, span MON_STATE_FIRST..MON_STATE_LAST): those
;                   commands re-read their pointer, bound and fill byte from RAM
;                   every iteration, so F:0000-00FF,00 rewrote its own pointer and
;                   looped forever, and F:0200-02FF,AA set the bound to $AAAA and
;                   wiped all of user RAM before printing OK. M: also rejects a
;                   destination whose end carries past $FFFF (its loops end on the
;                   source address only, so the write wrapped into zero page).
;                   NMI_HANDLER_BREAK now clears PAGE_IN_BREAK and MODULE_BANK:
;                   STOP pressed at a --MORE-- prompt left the pager disabled for
;                   the rest of the session, and STOP inside a module left that
;                   ROM mapped so the monitor showed ROM while reporting RAM.
;
; ================================================================

.PC02                               ; ca65 directive to enable 65C02 instructions.

.org $F000                          ; ROM start address (4 KB BIOS window).

; Shared addresses (zero page, page 2, I/O registers, hardware constants).
.include "kernel_vars.inc"

; ================================================================
; KERNEL PROGRAM START
; ================================================================

; Reset vector entry point.
RESET:
    CLD                         ; Clear decimal mode flag
    SEI                         ; Set interrupt disable flag
    LDX #STACK_TOP              ; Initialize stack pointer to top of stack page
    TXS                         ; Transfer X to stack pointer

    ; Map the module window ($B000-$EFFF) to RAM at boot. The slot starts empty
    ; (no module auto-loaded); modules are mapped in later via the bank register.
    STZ MODULE_BANK

; ================================================================
; ZERO PAGE INITIALIZATION
; ================================================================

    LDX #$00                    ; Start at beginning of zero page

ZP_CLEAR_LOOP:
    STZ $00,X               ; Clear zero page location
    INX                     ; Increment address
    CPX #$F0                ; Stop at $F0 to leave BASIC's high zero page ($F0-$FF) alone
    BNE ZP_CLEAR_LOOP       ; Loop until done

; ================================================================
; RAM INITIALIZATION
; ================================================================

    JSR CLEAR_SCREEN            ; Clear screen memory ($0400-$07FF)

    ; Clear the module window ($B000-$EFFF) so bank 0 boots as clean scratch RAM.
    ; MODULE_BANK was set to 0 above, so these writes land in the window RAM (not
    ; any module ROM); modules live in separate banks. The monitor variables used
    ; here are scratch at this point - they get cleared in the monitor-init step
    ; just below.
    ;
    ; This used to call the monitor's F: fill engine (FILL_RANGE_CORE). The monitor
    ; is a bank module now and the BIOS cannot call into the window -- at this point
    ; in boot it holds whatever the host installed, and mapping bank 4 to borrow a
    ; fill loop would mean clearing the window from inside it. So: a private page
    ; loop, 48 pages of $B0..$DF.
    STZ MON_CURRADDR_LO
    LDA #>MODULE_WINDOW_START
    STA MON_CURRADDR_HI
    LDA #$00                    ; fill value
@win_page:
    LDY #$00
@win_byte:
    STA (MON_CURRADDR_LO),Y
    INY
    BNE @win_byte
    INC MON_CURRADDR_HI
    LDY MON_CURRADDR_HI
    CPY #(>MODULE_WINDOW_END)+1 ; past $DF -> done
    BNE @win_page
    ; The loop left MON_CURRADDR past the window end; the ZP clear already ran, so
    ; reset it here to $0000 for the initial prompt address.
    STZ MON_CURRADDR_LO
    STZ MON_CURRADDR_HI

; ================================================================
; MONITOR INITIALIZATION
; ================================================================

    ; Seed the 16-bit RNG LFSR from the real-time clock so sequences differ each
    ; boot (was a constant $01). Fold the clock + sub-second FAT-time bits into
    ; both state bytes; the LFSR forbids the all-zero state, so force low=$01 then.
    STA RTC_LATCH               ; snapshot the live clock (written value irrelevant)
    LDA RTC_SEC
    EOR RTC_HOUR
    EOR RTC_FATTIME_LO
    STA RNG_STATE_LO
    LDA RTC_MIN
    EOR RTC_HOUR
    EOR RTC_FATTIME_HI
    STA RNG_STATE_HI
    ORA RNG_STATE_LO            ; both state bytes zero?
    BNE RNG_SEED_OK
    LDA #$01                    ; avoid the forbidden all-zero LFSR state
    STA RNG_STATE_LO
RNG_SEED_OK:
    STA PAGE_ENABLE             ; pager on by default (a future setting can disable it)
    STZ CMD_LINE_COUNT          ; pager state starts clean
    STZ PAGE_SUSPEND
    STZ PAGE_ABORT_FLAG
    STZ PAGE_IN_BREAK
    STA SOUND_ENABLE            ; A still $01: system sound on by default
    STZ BEEP_TIMER              ; no beep pending

    ; Initialize monitor variables and state
    LDX #$E9                    ; Clear monitor area $0200-$02E9 (234 bytes)

CLEAR_MON_VAR_LOOP:
    STZ $0200,X                 ; Store zero to monitor variable area
    DEX                         ; Decrement counter
    BPL CLEAR_MON_VAR_LOOP      ; Continue while X >= 0 (branch on plus)

    ; Initialize cursor position to top-left of screen
    STZ CURSOR_X                ; Set cursor X to 0
    STZ CURSOR_Y                ; Set cursor Y to 0
    JSR UPDATE_CURSOR           ; position the displayed hardware cursor

    CLI                         ; Enable interrupts

; ================================================================
; HAND OFF TO THE DOS SHELL
; ================================================================
    ; The kernel boots silently; the MFC/OS shell draws the sign-on splash box
    ; (it owns the OS version + free-memory figures). See _DOS_SPLASH in dos.asm.

    ; Boot into the MFC/OS DOS shell (always-mapped DOS ROM). The monitor is now
    ; a tool launched from the DOS by MON; it returns here via DOS_WARM.
    JMP DOS_COLD

; ================================================================
; RANDOM NUMBER GENERATOR ROUTINES
; ================================================================

; Get random number from 1 to RNG_MAX
; Input: RNG_MAX = maximum value
; Output: A = random number from 1 to RNG_MAX
; Modifies: A
; Return a random number 1..RNG_MAX. Uses multiply-high range reduction --
; result = hi(raw * RNG_MAX) + 1 -- so every 1..MAX value can appear and the
; output rides the full 65535 LFSR period. (The old rejection method exposed a
; tiny fixed cycle for small MAX -- e.g. only 9 values for d10.) RNG_MAX preserved.
GET_RANDOM_NUMBER:
    PHX
    JSR GET_RANDOM          ; A = raw 0..255
    STA RNG_TMP             ; multiplier (consumed low-bit-first below)
    LDA #$00
    STA RNG_TMP2            ; product low = 0
                            ; A = product high (accumulator) = 0
    LDX #$08
GRN_MUL:
    LSR RNG_TMP             ; next multiplier bit -> carry
    BCC GRN_NOADD
    CLC
    ADC RNG_MAX             ; product-high += multiplicand
GRN_NOADD:
    ROR A                   ; product-high >> 1, ADC carry into bit7 (65C02 ROR A)
    ROR RNG_TMP2            ; product-low  >> 1, receives product-high's LSB
    DEX
    BNE GRN_MUL
    ; A = hi(raw * RNG_MAX) = 0..RNG_MAX-1
    INC A                   ; 1..RNG_MAX (65C02 INC A)
    PLX
    RTS

; Basic random number generator using 8-bit LFSR
; Uses polynomial: x^8 + x^6 + x^5 + x^4 + 1
; Output: A = pseudo-random byte (1-255, never 0)
; Modifies: A
; Preserves: X, Y
; 16-bit Galois LFSR (taps 16,14,13,11; left-shift feedback mask $002D, i.e. EOR
; the low byte with $2D when bit15 shifts out). Period 65535 -- verified full and
; near-uniform after range reduction. Advances the state; returns the low byte.
GET_RANDOM:
    ASL RNG_STATE_LO        ; 16-bit shift left; old bit15 -> carry
    ROL RNG_STATE_HI
    BCC NO_XOR              ; no feedback unless a 1 shifted out
    LDA RNG_STATE_LO
    EOR #$2D                ; only the low byte has taps ($002D)
    STA RNG_STATE_LO

NO_XOR:
    LDA RNG_STATE_LO        ; return the low byte as the random value
    RTS

; ================================================================
; MONITOR HEX CONVERSION ROUTINES
; ================================================================

; Convert ASCII hex character to 4-bit binary value
; Input: A = ASCII character ('0'-'9', 'A'-'F') - lowercase already converted by input processing
; Output: A = 4-bit value (0-15), Carry clear if valid, set if invalid
; Modifies: A
; Note: Fundamental parsing routine used by all hex conversion functions
HEX_CHAR_TO_NIBBLE:

    SEC                         ; Set carry
    SBC #$30                    ; Subtract '0' (ASCII $30)

    ; Check if it was 0-9 (result will be 0-9)
    CMP #$0A                    ; Is it less than 10?
    BCC HEX_CHAR_VALID          ; Yes, it's 0-9, we're done

    ; Check if it's A-F (after subtracting '0', 'A' becomes $11)
    CMP #$11                    ; 'A' - '0' = $41 - $30 = $11
    BCC HEX_CHAR_INVALID        ; Less than 'A'
    CMP #$17                    ; 'F' - '0' + 1 = $46 - $30 + 1 = $17
    BCS HEX_CHAR_INVALID        ; Greater than 'F'

    ; It's A-F, subtract 7 more to get 10-15
    SEC
    SBC #$07                    ; 'A' - '0' - 7 = 10

HEX_CHAR_VALID:
    CLC                         ; Clear carry for success
    RTS

HEX_CHAR_INVALID:
    SEC                         ; Set carry for error
    RTS

; Convert two ASCII hex characters to a byte value
; Input: X = pointer to first hex character in MON_CMDBUF
; Output: A = byte value (0-255), X = X + 2 (points after hex pair), Carry clear if valid, set if invalid
; Modifies: A, X, MON_HEX_TEMP
; Note: Validates both characters are valid hex (0-9, A-F), used by multi-byte parsing
HEX_PAIR_TO_BYTE:
    ; Convert first character (high nibble)
    LDA MON_CMDBUF,X            ; Load first hex character
    JSR HEX_CHAR_TO_NIBBLE      ; Convert to nibble
    BCS HEX_PAIR_ERROR          ; If invalid, return error

    ASL A                       ; Shift left 4 times to make high nibble
    ASL A
    ASL A
    ASL A
    STA MON_HEX_TEMP            ; Store high nibble

    ; Convert second character (low nibble)
    INX                         ; Move to second character
    LDA MON_CMDBUF,X            ; Load second hex character
    JSR HEX_CHAR_TO_NIBBLE      ; Convert to nibble
    BCS HEX_PAIR_ERROR          ; If invalid, return error

    ; Combine nibbles
    ORA MON_HEX_TEMP            ; OR with high nibble
    INX                         ; Move past the hex pair
    CLC                         ; Clear carry for success
    RTS

HEX_PAIR_ERROR:
    RTS

; Convert a nibble (A = 0-15) to its ASCII hex character ('0'-'9' or 'A'-'F').
; Replaces the old runtime HEX_LOOKUP_TABLE. Modifies A (and flags); preserves X, Y.
NIBBLE_TO_ASCII:
    CMP #$0A                    ; 0-9 or A-F?
    BCC NIBBLE_IS_DIGIT
    ADC #$06                    ; carry set => +7 total, maps $0A-$0F to $11-$16
NIBBLE_IS_DIGIT:
    ADC #'0'                    ; +$30 => '0'-'9' or 'A'-'F'
    RTS

; Print the byte in A as two ASCII hex digits directly to the screen.
; Input: A = byte value. Modifies A, X. Preserves Y (PRINT_CHAR preserves Y).
PRINT_HEX_BYTE:
    PHA                         ; Save the byte for the low nibble
    LSR A                       ; High nibble -> low 4 bits
    LSR A
    LSR A
    LSR A
    JSR NIBBLE_TO_ASCII         ; High-nibble hex char
    JSR PRINT_CHAR
    PLA                         ; Restore the byte
    AND #$0F                    ; Low nibble
    JSR NIBBLE_TO_ASCII         ; Low-nibble hex char
    JMP PRINT_CHAR              ; Tail call: PRINT_CHAR's RTS returns to caller

; Convert four ASCII hex characters to 16-bit address
; Input: X = pointer to first hex character in MON_CMDBUF
; Output: MON_CURRADDR_HI/LO = 16-bit address value, X = X + 4 (points after hex quartet), Carry clear if valid, set if invalid
; Modifies: A, X, Y
; Note: Validates each character is valid hex (0-9, A-F), used by address parsing routines
HEX_QUAD_TO_ADDR:
    JSR HEX_PAIR_TO_BYTE        ; Convert first two hex chars
    BCS HEX_QUAD_ERROR          ; If invalid, return error
    STA MON_CURRADDR_HI         ; Store high byte
    JSR HEX_PAIR_TO_BYTE        ; Convert next two hex chars
    BCS HEX_QUAD_ERROR          ; If invalid, return error
    STA MON_CURRADDR_LO         ; Store low byte
    CLC                         ; Clear carry for success
    RTS

HEX_QUAD_ERROR:
    RTS


; ================================================================
; MONITOR SCREEN OUTPUT AND INPUT ROUTINES
; ================================================================

; Compute the linear cell index for the logical cursor: cell = CURSOR_Y*80 + X.
; Result in VID_CELL_LO/HI. Uses A and the VID_TMP scratch only (X/Y preserved,
; so it is safe to call from PRINT_CHAR which must preserve Y for PRINT_MESSAGE).
; 80 = 64 + 16, so Y*80 = (Y<<6) + (Y<<4): build Y<<4, copy it, shift on to Y<<6,
; then add the saved Y<<4 and finally CURSOR_X.
COMPUTE_CELL:
    LDA CURSOR_Y
    STA VID_CELL_LO
    STZ VID_CELL_HI
    ASL VID_CELL_LO             ; *2
    ROL VID_CELL_HI
    ASL VID_CELL_LO             ; *4
    ROL VID_CELL_HI
    ASL VID_CELL_LO             ; *8
    ROL VID_CELL_HI
    ASL VID_CELL_LO             ; *16  (= Y<<4)
    ROL VID_CELL_HI
    LDA VID_CELL_LO             ; save Y*16
    STA VID_TMP_LO
    LDA VID_CELL_HI
    STA VID_TMP_HI
    ASL VID_CELL_LO             ; *32
    ROL VID_CELL_HI
    ASL VID_CELL_LO             ; *64  (= Y<<6)
    ROL VID_CELL_HI
    CLC                         ; Y*64 + Y*16 = Y*80
    LDA VID_CELL_LO
    ADC VID_TMP_LO
    STA VID_CELL_LO
    LDA VID_CELL_HI
    ADC VID_TMP_HI
    STA VID_CELL_HI
    CLC                         ; + CURSOR_X
    LDA VID_CELL_LO
    ADC CURSOR_X
    STA VID_CELL_LO
    LDA VID_CELL_HI
    ADC #$00
    STA VID_CELL_HI
    RTS

; Point the data-port cell index (VREG_ADDR) at the logical cursor cell so the
; next VREG_CHAR write lands there. Preserves X/Y.
SET_VREG_ADDR:
    JSR COMPUTE_CELL
    LDA VID_CELL_LO
    STA VREG_ADDR_LO
    LDA VID_CELL_HI
    STA VREG_ADDR_HI
    RTS

; Move the displayed hardware cursor to the logical cursor cell. Preserves X/Y.
UPDATE_CURSOR:
    JSR COMPUTE_CELL
    LDA VID_CELL_LO
    STA VREG_CURSOR_LO
    LDA VID_CELL_HI
    STA VREG_CURSOR_HI         ; bit7 clear => cursor visible
    RTS

; Set the current color/attribute latch (VREG_ATTR). Subsequent VREG_CHAR writes
; (i.e. PRINT_CHAR output) take this attribute. Byte = [R][BR][bg:3][fg:3]; the
; power-on/clear default is $02 (green on black). Exposed as K_SET_ATTR ($FF2D)
; so color-aware programs and the forthcoming ANSI terminal can set colors.
; Input: A = attribute byte. Preserves X/Y (A is consumed).
SET_ATTR:
    STA VREG_ATTR
    RTS

; Scroll the screen up one line. The VIC does the row shift and blanks the bottom
; line chip-side (one command), so the CPU no longer copies ~2000 bytes. Uses A.
SCROLL_SCREEN:
    LDA #ASCII_SPACE
    STA VREG_CMD_PARAM          ; bottom line filled with spaces
    LDA #VCMD_SCROLL_UP
    STA VREG_CMD
    RTS

; Clear the whole screen (chip-side fill) and home the cursor. Uses A.
; Clears in the default attribute ($02, green on black): the VIC fills the color
; plane from the latch, so a program that left the latch on another color (e.g.
; TERM after a colored BBS) hands back a default-colored screen -- including the
; blank cell under the cursor, whose stored color the host draws the cursor from.
; Programs that clear in their own color drive VREG_CMD directly, not this ABI.
CLEAR_SCREEN:
    LDA #$02
    STA VREG_ATTR
    LDA #ASCII_SPACE
    STA VREG_CMD_PARAM
    LDA #VCMD_CLEAR
    STA VREG_CMD
    STZ CURSOR_X                ; Reset cursor position to (0, 0)
    STZ CURSOR_Y
    JMP UPDATE_CURSOR

; Scroll up one line and re-home the cursor to the bottom line.
; Shared by PRINT_CHAR's line-wrap and carriage-return scroll paths.
SCROLL_AND_HOME_BOTTOM:
    JSR SCROLL_SCREEN           ; Scroll everything up one line
    LDA #SCREEN_HEIGHT-1        ; Stay on bottom line (Y = 24)
    STA CURSOR_Y
    STZ CURSOR_X                ; start of the bottom line
    RTS

; Print a single character to screen at current cursor position
; Input: A = character to print (ASCII value)
; Output: Character written through the VIC port; CURSOR_X/Y and displayed cursor
;         advanced.
; Modifies: CURSOR_X/Y, VID_CELL/TMP scratch. PRESERVES A, X and Y. A is returned
;           unchanged because callers depend on it: EhBASIC's LAB_PRNA does
;           `JSR V_OUTP / CMP #$0D` to detect the CR it just printed (and reset
;           TPos), and PRINT_MESSAGE relies on Y. The input character is held on
;           the stack across the whole routine and restored at every exit.
; Note: Handles special characters (CR, backspace), automatic scrolling, wrapping
PRINT_CHAR:
    PHA                         ; preserve the character across the whole routine
    CMP #ASCII_CR               ; Is it carriage return?
    BEQ PRINT_CHAR_NEWLINE      ; Handle newline

    CMP #ASCII_LF               ; Is it line feed?
    BNE PRINT_CHAR_CHECK_BS     ; Not LF; keep checking
    PLA                         ; Ignore LF (CR alone performs the newline, so a
    RTS                         ; CR+LF sequence yields a single newline); restore A

PRINT_CHAR_CHECK_BS:
    CMP #ASCII_BACKSPACE        ; Is it backspace?
    BEQ PRINT_CHAR_BACKSPACE    ; Handle backspace

    CMP #ASCII_BEL              ; Is it BEL ($07)?
    BEQ PRINT_CHAR_BELL         ; ring the bell (non-blocking beep), print nothing

    ; Other control characters (< space) produce no glyph -- terminal behavior.
    ; This drops things like BELL ($07), which EhBASIC emits on every keypress
    ; once its 71-char input buffer is full; rendering it would show garbage.
    CMP #ASCII_SPACE            ; Is it below space ($20)?
    BCC PRINT_CHAR_DONE         ; If so, ignore it (PRINT_CHAR_DONE restores A)

    JSR SET_VREG_ADDR           ; point the port at the cursor cell
    PLA                         ; recover the character...
    PHA                         ; ...keeping a copy on the stack for the exit
    STA VREG_CHAR               ; write the glyph (chip auto-advances its index)

    INC CURSOR_X                ; Advance cursor X position
    LDA CURSOR_X
    CMP #SCREEN_WIDTH           ; Check if X >= 80
    BCC PRINT_CHAR_ADV_DONE     ; If not, just reposition the cursor

    ; Line wrap: reset X to 0, advance to the next line, scroll if past the bottom
    STZ CURSOR_X
    INC CURSOR_Y
    LDA CURSOR_Y
    CMP #SCREEN_HEIGHT          ; Past line 24?
    BCC PRINT_CHAR_ADV_DONE
    JSR SCROLL_AND_HOME_BOTTOM  ; scroll up one line and re-home to the bottom line

PRINT_CHAR_ADV_DONE:
    JSR UPDATE_CURSOR           ; move the displayed cursor to the new position
PRINT_CHAR_DONE:
    PLA                         ; restore A = the character that was printed
    RTS

PRINT_CHAR_BELL:
    JSR BEEP                    ; start a short beep (preserves X and Y)
    PLA                         ; restore A = $07 (BEL)
    RTS

PRINT_CHAR_NEWLINE:
    ; Carriage return: go to the start of the next line, scrolling if needed.
    STZ CURSOR_X
    INC CURSOR_Y
    LDA CURSOR_Y
    CMP #SCREEN_HEIGHT          ; Past line 24?
    BCC PRINT_CHAR_NEWLINE_DONE
    JSR SCROLL_AND_HOME_BOTTOM  ; scroll up one line and re-home to the bottom line

PRINT_CHAR_NEWLINE_DONE:
    JSR UPDATE_CURSOR
    JSR PAGE_ADVANCE            ; count this line; pause with --MORE-- at a full page
    PLA                         ; restore A = $0D (EhBASIC's CMP #$0D depends on it)
    RTS

PRINT_CHAR_BACKSPACE:
    ; Handle backspace - move cursor back and clear the character there.
    LDA CURSOR_X
    BNE PRINT_BACKSPACE_SAME_LINE ; If X>0, backspace within current line

    ; At beginning of line - check if we can go to the previous line
    LDA CURSOR_Y
    BEQ PRINT_CHAR_DONE         ; If Y=0, we're at top-left, can't go back (restores A)

    DEC CURSOR_Y                ; Move up one line
    LDA #SCREEN_WIDTH-1         ; Move to end of line (position 79)
    STA CURSOR_X
    JMP PRINT_BACKSPACE_CLEAR_CHAR

PRINT_BACKSPACE_SAME_LINE:
    DEC CURSOR_X                ; Move cursor back one position on the same line

PRINT_BACKSPACE_CLEAR_CHAR:
    ; Clear the character at the new position by writing a space through the port.
    JSR SET_VREG_ADDR
    LDA #ASCII_SPACE
    STA VREG_CHAR
    JSR UPDATE_CURSOR
    PLA                         ; restore A = the backspace character
    RTS                         ; Done with backspace

; Print a newline (carriage return)
; Modifies: A
PRINT_NEWLINE:
    LDA #ASCII_CR
    JMP PRINT_CHAR              ; tail call (PRINT_CHAR's RTS returns to our caller)

; PRINT_NEWLINE_PAGED - alias: paging is now handled centrally in PRINT_CHAR
; (PAGE_ADVANCE), so it is identical to PRINT_NEWLINE. Kept for its MON call sites.
PRINT_NEWLINE_PAGED = PRINT_NEWLINE

; PAGE_ADVANCE - called from PRINT_CHAR after every newline. When paging is
; enabled and not suspended for this output, count the line and, every
; LINES_PER_PAGE lines, pause with the --MORE-- prompt. This is the one shared
; pager for the whole system (DOS shell, MON, BASIC, ASM, FORTH) -- any program
; that prints through K_PRINT_CHAR is paged with no code of its own. The counter
; is reset per command in GET_KEYSTROKE (on the submitting CR). Preserves X and Y
; (the PRINT_CHAR contract; PRINT_MESSAGE loops on Y); A is restored by the caller.
PAGE_ADVANCE:
    LDA PAGE_ENABLE
    BEQ @ret                    ; paging off (a future setting)
    LDA PAGE_SUSPEND
    BNE @ret                    ; user pressed ESC: let the rest scroll
    LDA PAGE_IN_BREAK
    BNE @ret                    ; printing the prompt itself; don't recurse
    INC CMD_LINE_COUNT
    LDA CMD_LINE_COUNT
    CMP #LINES_PER_PAGE
    BCC @ret                    ; not a full page yet
    STZ CMD_LINE_COUNT
    INC PAGE_IN_BREAK
    PHX
    PHY
    ; PRINT_MESSAGE ($FF03) and PRINT_HELP_LINE ($FF30) keep their string cursor in
    ; MON_MSG_PTR as well as in Y, and the prompt printed below re-points it at
    ; MSG_PAGE_PROMPT. Without this, a message containing an embedded $0D that
    ; happened to fill the page resumed reading from the prompt string instead of
    ; its own text -- silently printing the tail of "--MORE--" and then whatever
    ; ROM followed it. Multi-line single-call messages exist (MSG_DOS_USAGE,
    ; MSG_DOS_MEM), so save the pointer alongside X and Y.
    LDA MON_MSG_PTR_LO
    PHA
    LDA MON_MSG_PTR_HI
    PHA
    JSR HANDLE_PAGE_BREAK
    PLA
    STA MON_MSG_PTR_HI
    PLA
    STA MON_MSG_PTR_LO
    PLY
    PLX
    STZ PAGE_IN_BREAK
@ret:
    RTS

HANDLE_PAGE_BREAK:
    ; Save cursor state (printing the prompt below moves it)
    LDA CURSOR_X
    PHA
    LDA CURSOR_Y
    PHA

    ; Print prompt on current line (before it scrolls away)
    LDA #<MSG_PAGE_PROMPT
    LDY #>MSG_PAGE_PROMPT
    JSR PRINT_MSG_AY

PAGE_WAIT_KEY:
    JSR GET_KEYSTROKE           ; Check for key pressed
    BCC PAGE_WAIT_KEY           ; Loop if no key available
    CMP #ASCII_ESC              ; ESC aborts; SPACE/ENTER/any other key advances a page
    BNE PAGE_CONTINUE

PAGE_ABORT:
    LDA #1
    STA PAGE_ABORT_FLAG         ; cooperative callers (MON dumps) stop entirely
    STA PAGE_SUSPEND            ; others (BASIC/FORTH/TYPE) stop pausing, rest scrolls

PAGE_CONTINUE:
    ; Restore cursor state and reposition the displayed cursor
    PLA
    STA CURSOR_Y
    PLA
    STA CURSOR_X
    JMP UPDATE_CURSOR

; Print null-terminated string using indirect addressing
; Input: MON_MSG_PTR_LO/HI = pointer to null-terminated string
; Output: String displayed on screen, cursor advanced
; Modifies: A, Y, MON_MSG_TMP_POS
; Note: Core string display routine, handles newlines and special characters
PRINT_MESSAGE:
    LDY #$00                    ; Initialize string index

PRINT_MSG_LOOP:
    LDA (MON_MSG_PTR_LO),Y      ; Load character using indirect indexed
    BEQ PRINT_MSG_DONE          ; If null terminator, done
    JSR PRINT_CHAR              ; Print the character
    INY                         ; Move to next character
    BNE PRINT_MSG_LOOP          ; Continue if Y hasn't wrapped (strings < 256 chars)

PRINT_MSG_DONE:
    RTS

; Set the message pointer from A/Y and print the string. Lets callers print a
; message in 3 instructions (LDA #<MSG / LDY #>MSG / JSR PRINT_MSG_AY) instead
; of repeating the four-store MON_MSG_PTR setup at every site.
; Input: A = low byte of message address, Y = high byte
PRINT_MSG_AY:
    STA MON_MSG_PTR_LO
    STY MON_MSG_PTR_HI
    JMP PRINT_MESSAGE           ; tail call: PRINT_MESSAGE's RTS returns to caller

; Scan keyboard without blocking - standard non-blocking input routine
; Input: None
; Output: A = ASCII character if available, Carry SET if character available, Carry CLEAR if no character
; Modifies: A
; Note: Non-blocking keyboard scan, compatible with BASIC interpreter expectations
GET_KEYSTROKE:
    LDA PIA_CONTROL         ; Read PIA control register
    AND #PIA_DATA_AVAIL     ; Check data available bit
    BEQ GET_NO_KEY          ; Branch if no data available
    LDA PIA_DATA            ; Read character from PIA -- returned AS TYPED.
    ; Case is preserved here so lowercase-aware programs (e.g. the editor) can
    ; see it. Consumers that want uppercase fold it themselves: the shell line
    ; editor READ_COMMAND_LINE uppercases command lines, and BASIC's input
    ; vector points at a folding wrapper (KEY_UC) since its tokenizer needs
    ; uppercase keywords.
    ; A submitted line (CR) starts a fresh command: reset the pager so each
    ; command pages independently and the prompt/echo never accumulate. This is
    ; the one universal "new command" signal (MON/DOS/ASM via READ_COMMAND_LINE,
    ; BASIC/FORTH via their own $FF09 readers). PAGE_ENABLE is left untouched so a
    ; future persistent "paging off" setting survives. Does not fire on no-key
    ; polls, so a command that scans for a key mid-output won't reset itself.
    CMP #ASCII_CR
    BNE GET_KEY_HAVE
    STZ CMD_LINE_COUNT
    STZ PAGE_SUSPEND
    STZ PAGE_ABORT_FLAG
GET_KEY_HAVE:
    SEC                     ; Set carry to indicate character available
    RTS
GET_NO_KEY:
    CLC                     ; Clear carry to indicate no character available
    RTS

; ================================================================
; MONITOR COMMAND LINE INPUT HANDLER
; ================================================================

; Read a complete command line from keyboard with editing support
; Input: None (reads from keyboard until ENTER pressed)
; Output: Command stored in MON_CMDBUF, length in MON_CMDLEN, command echoed to screen
; Modifies: A, X, Y, MON_CMDBUF, MON_CMDLEN
; Note: Supports backspace editing, 80 char limit, lowercase to uppercase conversion, '.' recall
READ_COMMAND_LINE:
    STZ MON_CMDLEN              ; Reset command length
    LDX #$00                    ; Reset buffer index

READ_CMD_LOOP:
    JSR GET_KEYSTROKE           ; Check for keystroke
    BCC READ_CMD_LOOP           ; Loop if no key available
    CMP #ASCII_CR               ; Is it Enter/Return?
    BEQ READ_CMD_DONE_CR_JMP    ; If so, command is complete (use local jump)
    CMP #ASCII_BACKSPACE        ; Is it backspace?
    BEQ READ_CMD_BACKSPACE      ; Handle backspace
    CMP #ASCII_DELETE           ; Is it delete?
    BEQ READ_CMD_BACKSPACE      ; Handle delete same as backspace
    CMP #ASCII_ESC              ; Is it escape?
    BEQ READ_CMD_ESCAPE         ; Handle escape (cancel command)
    CMP #ASCII_DOT              ; Is it a dot?
    BNE CHECK_BUFFER_FULL       ; If not, continue normal processing

    ; Only process dot if it's the first character AND we're in command mode
    CPX #$00                    ; Is buffer empty?
    BNE CHECK_BUFFER_FULL       ; If not, treat as normal character
    LDA MON_MODE                ; Load current mode
    BNE CHECK_BUFFER_FULL       ; If not command mode (0), treat dot as normal character
    JSR RECALL_LAST_COMMAND     ; Recall and display last command
    JMP READ_CMD_LOOP           ; Continue normal input processing

READ_CMD_DONE_CR_JMP:
    JMP READ_CMD_DONE_CR        ; Jump to actual CR handler

CHECK_BUFFER_FULL:
    CPX #MON_CMDBUF_LEN-1       ; Check if at max length (leave room for null)
    BCS READ_CMD_LOOP           ; If full, ignore additional characters
    CMP #ASCII_SPACE            ; Is it less than space?
    BCC READ_CMD_LOOP           ; If so, ignore it
    CMP #$7F                    ; Is it greater than tilde?
    BCS READ_CMD_LOOP           ; If so, ignore it

    ; Convert to uppercase if it's a lowercase letter
    CMP #$61                    ; Compare with 'a' ($61)
    BCC NOT_LOWERCASE_INPUT     ; If less than 'a', not lowercase
    CMP #$7B                    ; Compare with '{' (one after 'z')
    BCS NOT_LOWERCASE_INPUT     ; If greater than 'z', not lowercase
    AND #$5F                    ; Clear bit 5 to convert to uppercase

NOT_LOWERCASE_INPUT:
    STA MON_CMDBUF,X            ; Store character in buffer (now uppercase if it was lowercase)
    JSR PRINT_CHAR              ; Echo character to screen
    INX                         ; Increment buffer position
    STX MON_CMDLEN              ; Update command length
    JMP READ_CMD_LOOP           ; Continue reading

READ_CMD_BACKSPACE:
    CPX #$00                    ; Is buffer empty?
    BEQ READ_CMD_LOOP           ; If empty, ignore backspace
    DEX                         ; Move back one position
    STX MON_CMDLEN              ; Update command length
    STZ MON_CMDBUF,X            ; Clear the character in buffer
    LDA #ASCII_BACKSPACE        ; Print backspace
    JSR PRINT_CHAR              ; PRINT_CHAR now handles backspace correctly
    JMP READ_CMD_LOOP           ; Continue reading

READ_CMD_ESCAPE:
    CPX #$00                    ; Check if buffer is empty
    BNE READ_CMD_CANCEL         ; If not empty, clear buffer

    LDA #ASCII_ESC              ; Buffer is empty - treat like we got ESC command
    STA MON_CMDBUF              ; Put ESC as the only character
    LDA #$01
    STA MON_CMDLEN              ; Length = 1
    JMP READ_CMD_EXIT           ; shared exit: returns the length in A

READ_CMD_CANCEL:
    ; Abandon the line in place: destructively backspace over the characters
    ; typed (X of them) so the line collapses back to just the prompt, then keep
    ; reading on the same line. The prompt itself was printed by the caller and
    ; is left intact, so ESC clears your input and leaves you at a fresh prompt.
READ_CMD_CANCEL_ERASE:
    CPX #$00                    ; any typed characters left to erase?
    BEQ READ_CMD_CANCEL_DONE
    LDA #ASCII_BACKSPACE        ; destructive backspace (PRINT_CHAR clears the char)
    JSR PRINT_CHAR
    DEX
    BRA READ_CMD_CANCEL_ERASE
READ_CMD_CANCEL_DONE:
    JSR CLEAR_CMD_BUFFER        ; reset buffer bytes, length, and pointer
    LDX #$00                    ; buffer index back to start
    JMP READ_CMD_LOOP           ; keep reading on the same line

READ_CMD_DONE_CR:
    ; Command is complete - null terminate it
    STZ MON_CMDBUF,X            ; Null terminate the command
READ_CMD_EXIT:
    ; Shared exit. The published $FF15 contract is "length in A, zero flag set for
    ; an empty line", but both exits used to end with a tail jump to PRINT_NEWLINE,
    ; and PRINT_CHAR deliberately preserves A -- so callers always got $0D with Z
    ; clear. A conforming caller doing "JSR $FF15 / BEQ empty" could never see an
    ; empty line, and one using A as the length read 13 bytes of a 0-byte buffer.
    JSR PRINT_NEWLINE           ; Move to next line on screen
    LDA MON_CMDLEN              ; A = length, Z set when the line is empty
    RTS

; CLEAR_CMD_BUFFER - Reset the command input buffer to empty
; Input: None
; Output: MON_CMDBUF[0..MON_CMDBUF_LEN-1] zeroed; MON_CMDLEN = 0
; Modifies: A is preserved; X = $FF on return
; Note: Single source of truth for clearing the command buffer - shared by the
;       ESC-cancel and '.' recall paths.
; ----------------------------------------------------------------
CLEAR_CMD_BUFFER:
    STZ MON_CMDLEN              ; command length
    LDX #MON_CMDBUF_LEN-1
CLEAR_CMD_BUFFER_LOOP:
    STZ MON_CMDBUF,X
    DEX
    BPL CLEAR_CMD_BUFFER_LOOP
    RTS
BANK_LAUNCH:
    PHA                         ; save index
    ASL A
    ASL A                       ; index * 4
    STA JUMP_VECTOR             ; scratch (overwritten with entry below)
    PLA
    CLC
    ADC JUMP_VECTOR             ; index*4 + index = index*5
    TAX                         ; X = byte offset into MODULE_DIR

    LDA MODULE_DIR,X            ; bank number
    STA MODULE_BANK             ; map the module into $B000-$EFFF
    LDA MODULE_DIR+1,X          ; entry address low
    STA JUMP_VECTOR
    LDA MODULE_DIR+2,X          ; entry address high
    STA JUMP_VECTOR+1

    ; Generic not-loaded guard: an unpopulated bank reads as $00 (BRK). A real
    ; module never starts with BRK, so treat a $00 entry byte as "not loaded".
    LDA (JUMP_VECTOR)           ; first opcode at the entry (65C02 zp indirect)
    BEQ BANK_NOT_LOADED

    JSR CLEAR_SCREEN            ; clean transition
    JMP (JUMP_VECTOR)           ; run the module (returns via $FF12)

BANK_NOT_LOADED:
    STZ MODULE_BANK             ; unmap -> window back to RAM
    LDA #<MSG_MODULE_FAIL
    LDY #>MSG_MODULE_FAIL
    JSR PRINT_MSG_AY            ; report the error...
    JMP DOS_WARM                ; ...then back to the DOS prompt

; ----------------------------------------------------------------
; K_LAUNCH_BY_NAME - launch a ROM module whose MODULE_DIR name matches A/X
; ----------------------------------------------------------------
; The DOS shell calls this to resolve a typed name against the module registry.
; Reached via the $FF00 ABI entry K_LAUNCH_BY_NAME ($FF21).
; In:  A/X = pointer to a null-terminated UPPERCASE name (e.g. "BASIC", "ASM").
; Out: on a match, maps the bank and runs the module (which returns to the DOS
;      via $FF12) - does NOT return here. On no match, carry set, RTS (the DOS
;      then tries a disk program). Uses LBN_NAME_PTR/LBN_MOD_PTR zero page.
LBN_NAME_PTR = $40             ; $40/$41: the typed name
LBN_MOD_PTR  = $42             ; $42/$43: a module's name (from MODULE_DIR)
LBN_IDX      = $44             ; 0-based module index (LBN_STRCMP clobbers Y, so
                               ;   the index can't live in Y across the compare)
LAUNCH_BY_NAME:
    STA LBN_NAME_PTR
    STX LBN_NAME_PTR+1
    LDX #$00                    ; X = byte offset into MODULE_DIR
    STX LBN_IDX                 ; index = 0
@rec:
    CPX #(MODULE_DIR_COUNT * MODULE_DIR_RECSIZE)
    BCS @nomatch
    LDA MODULE_DIR+3,X          ; this module's name pointer
    STA LBN_MOD_PTR
    LDA MODULE_DIR+4,X
    STA LBN_MOD_PTR+1
    JSR LBN_STRCMP              ; equal? (carry clear = match). Preserves X/LBN_IDX.
    BCC @found
    INC LBN_IDX                 ; advance to the next record
    TXA
    CLC
    ADC #MODULE_DIR_RECSIZE
    TAX
    BRA @rec
@found:
    LDA LBN_IDX                 ; A = matched index
    JMP BANK_LAUNCH             ; maps + runs (returns to DOS via $FF12)
@nomatch:
    SEC
    RTS

; Compare the null-terminated strings at LBN_NAME_PTR and LBN_MOD_PTR.
; Out: carry clear if equal (including matching terminators), set otherwise.
LBN_STRCMP:
    LDY #$00
@loop:
    LDA (LBN_NAME_PTR),Y
    CMP (LBN_MOD_PTR),Y
    BNE @ne
    CMP #$00                    ; equal so far - both terminate here? -> match
    BEQ @eq
    INY
    BNE @loop
@ne:
    SEC
    RTS
@eq:
    CLC
    RTS

; ----------------------------------------------------------------
; RETURN_FROM_MODULE
; Return handler a module jumps to when it exits (e.g. BASIC's BYE).
; Entry point: $FF12 (kernel API jump table). Reachable from any bank because
; the kernel ROM ($F000-$FFFF) is always mapped regardless of MODULE_BANK.
; Input: None (called via JMP from the module)
; Output: Returns to monitor prompt with the window unmapped (bank 0 = RAM)
; Modifies: A, X, Y
; ----------------------------------------------------------------
RETURN_FROM_MODULE:
    STZ MODULE_BANK             ; unmap the module -> window back to RAM

    ; Clear BASIC's interrupt-enable flags so a later NMI doesn't try to dispatch
    ; a stale BASIC ON NMI/IRQ handler.
    STZ BASIC_NMI_FLAGS
    STZ BASIC_IRQ_FLAGS

    ; Reset the stack to a clean state, then re-enter the MFC/OS shell. (Modules
    ; are launched from the DOS now, so they return to the DOS prompt, not the
    ; monitor; DOS_WARM reprints the prompt without the banner.)
    LDX #$FF
    TXS
    JSR CLEAR_SCREEN
    JMP DOS_WARM


; ----------------------------------------------------------------
; RECALL_LAST_COMMAND - '.' at the prompt: replay the previous command line
; ----------------------------------------------------------------
; BIOS, not monitor. READ_COMMAND_LINE ($FF15) calls this directly, and the BIOS
; cannot reach into the monitor's bank -- so it lives here. It also belongs here on
; the merits: it is line editing, and it touches nothing but the shared page-2
; buffers (MON_LAST_CMD_BUF/MON_CMDBUF, declared once in kernel_vars.inc) plus
; PRINT_CHAR. Its counterpart SAVE_COMMAND stays in the monitor, which is the side
; that decides what counts as a command worth remembering.
; Recall last command and display it in the current command buffer
; Called when '.' is entered as first character in command line
; Modifies: A, X, Y
RECALL_LAST_COMMAND:
    ; Check if we have a last command to recall
    LDA MON_LAST_CMD_LEN        ; Load last command length
    BEQ RECALL_NOTHING          ; If zero, nothing to recall

    ; Clear current command buffer first
    JSR CLEAR_CMD_BUFFER

    ; Copy last command to current command buffer
    LDX #$00                    ; Initialize copy index

RECALL_COPY_LOOP:
    CPX MON_LAST_CMD_LEN        ; Have we copied all characters?
    BCS RECALL_COPY_DONE        ; If so, we're done copying

    LDA MON_LAST_CMD_BUF,X      ; Load character from last command buffer
    STA MON_CMDBUF,X            ; Store in current command buffer
    JSR PRINT_CHAR              ; Echo character to screen
    INX                         ; Move to next character
    BRA RECALL_COPY_LOOP        ; Continue copying

RECALL_COPY_DONE:
    ; Update current command length
    STX MON_CMDLEN              ; Set current command length
    ; X now contains the number of characters copied
    RTS

RECALL_NOTHING:
    ; No previous command to recall - just continue input normally
    RTS
; ================================================================
; PARSE_DEC_ABI - decimal string -> 16-bit value (ABI $FF2A)
; ================================================================
; In:  X = index into MON_CMDBUF where the decimal digits start.
; Out: MON_CURRADDR_LO/HI = the 16-bit value; X = index past the last digit;
;      carry clear = ok, carry set = error with A = code (1 = invalid/no digits,
;      2 = overflow > 65535). Prints nothing - the caller decides how to react.
PARSE_DEC_ABI:
    STX MON_PARSE_PTR
    STZ DEC_RESULT_LO
    STZ DEC_RESULT_HI
    STZ DEC_DIGIT_IDX
    JSR PARSE_DECIMAL_VALUE
    BCS @err
    LDA DEC_RESULT_LO
    STA MON_CURRADDR_LO
    LDA DEC_RESULT_HI
    STA MON_CURRADDR_HI
    LDX MON_PARSE_PTR
    CLC
    RTS
@err:
    LDX MON_PARSE_PTR           ; A = error code preserved from the parser
    SEC
    RTS

; ================================================================
; PARSE_DECIMAL_VALUE - Parse decimal digits from command buffer
; ================================================================
; Converts ASCII decimal string to 16-bit binary value
; Input: MON_PARSE_PTR = position in MON_CMDBUF to start parsing
;        DEC_RESULT_HI/LO = current accumulated value (usually 0)
; Output: DEC_RESULT_HI/LO = 16-bit result
;         DEC_DIGIT_IDX = number of digits parsed
;         Carry clear if success, set if error
; Errors: VALUE? if invalid decimal digit
;         RANGE? if result > 65535
; Algorithm: For each digit: result = result × 10 + digit
; ================================================================
PARSE_DECIMAL_VALUE:
    LDX MON_PARSE_PTR

PARSE_DEC_LOOP:
    CPX MON_CMDLEN
    BCS PARSE_DEC_DONE

    LDA MON_CMDBUF,X

    ; Check if it's a decimal digit (0-9)
    CMP #'0'
    BCC PARSE_DEC_DONE          ; Not a digit, we're done
    CMP #'9'+1
    BCS PARSE_DEC_INVALID       ; > '9', invalid character

    ; Valid digit - convert to binary value
    SEC
    SBC #'0'
    PHA

    ; Multiply current result by 10 and add digit
    JSR MULTIPLY_BY_10
    BCS PARSE_DEC_OVERFLOW      ; If overflow, error

    ; Add digit to result
    PLA
    CLC
    ADC DEC_RESULT_LO
    STA DEC_RESULT_LO
    BCC PARSE_DEC_NO_CARRY

    ; Handle carry to high byte
    INC DEC_RESULT_HI
    BEQ PARSE_DEC_RANGE_ERR     ; If wrapped to 0, overflow (digit already popped)

PARSE_DEC_NO_CARRY:
    INX
    INC DEC_DIGIT_IDX
    BRA PARSE_DEC_LOOP

PARSE_DEC_INVALID:
    ; Invalid character. No printing here (this is an ABI primitive); return a
    ; value-error code so the caller can react. PARSE_ERR_VALUE = 1.
    STX MON_PARSE_PTR
    LDA #$01
    SEC
    RTS

PARSE_DEC_OVERFLOW:
    ; Reached from the MULTIPLY_BY_10 overflow path with the digit still pushed.
    PLA                         ; discard the pushed digit to balance the stack
PARSE_DEC_RANGE_ERR:
    ; Overflow detected. Entered directly (no PLA) from the high-byte-carry path,
    ; where the digit has already been pulled. PARSE_ERR_RANGE = 2.
    STX MON_PARSE_PTR
    LDA #$02
    SEC
    RTS

PARSE_DEC_DONE:
    ; Check if we parsed at least one digit
    LDA DEC_DIGIT_IDX
    BEQ PARSE_DEC_NO_DIGITS     ; No digits parsed

    STX MON_PARSE_PTR
    CLC                         ; Clear carry for success
    RTS

PARSE_DEC_NO_DIGITS:
    ; No digits found - value error (code 1)
    STX MON_PARSE_PTR
    LDA #$01
    SEC
    RTS

; ================================================================
; MULTIPLY_BY_10 - Multiply 16-bit value by 10
; ================================================================
; Multiplies DEC_RESULT_HI/LO by 10 using shift-and-add
; Formula: value × 10 = (value × 8) + (value × 2)
; Input: DEC_RESULT_HI/LO = 16-bit value to multiply
; Output: DEC_RESULT_HI/LO = value × 10
;         Carry set if overflow (result > 65535)
; Uses: DEC_TEMP_LO/HI for intermediate storage
; Preserves: X, Y
; ================================================================
MULTIPLY_BY_10:
    ; Shift left to get × 2
    ASL DEC_RESULT_LO
    ROL DEC_RESULT_HI
    BCS MULT10_OVERFLOW

    ; Save × 2 value in temp
    LDA DEC_RESULT_LO
    STA DEC_TEMP_LO
    LDA DEC_RESULT_HI
    STA DEC_TEMP_HI

    ; Shift left to get × 4
    ASL DEC_RESULT_LO
    ROL DEC_RESULT_HI
    BCS MULT10_OVERFLOW

    ; Shift left to get × 8
    ASL DEC_RESULT_LO
    ROL DEC_RESULT_HI
    BCS MULT10_OVERFLOW

    ; Add × 2 to × 8 to get × 10
    CLC
    LDA DEC_TEMP_LO
    ADC DEC_RESULT_LO
    STA DEC_RESULT_LO
    LDA DEC_TEMP_HI
    ADC DEC_RESULT_HI
    STA DEC_RESULT_HI
    RTS                         ; Carry already reflects overflow

MULT10_OVERFLOW:
    SEC
    RTS

; ================================================================
; PRINT_DEC - print a 32-bit value in decimal, right-justified (ABI $FF27)
; ================================================================
; In:  A/X = pointer to a 4-byte little-endian value
;      Y   = minimum field width (0 = none; pad with leading spaces)
; Out: the decimal number is printed (leading zeros suppressed; 0 prints "0")
; Modifies: A, X, Y, JUMP_VECTOR, and the $35-$39 decimal workspace.
; Method: bit-serial divide-by-10, pushing digits onto the stack, then padding
;         to the field width and emitting them high-to-low.
PRINT_DEC:
    STA JUMP_VECTOR             ; source pointer for the copy
    STX JUMP_VECTOR+1
    PHY                         ; save the field width
    LDY #$00
@cp:
    LDA (JUMP_VECTOR),Y         ; copy the value into the workspace (destructive divide)
    STA DEC32_VAL,Y
    INY
    CPY #$04
    BNE @cp
    PLY                         ; restore the field width
    STZ DEC32_CNT
@gen:
    JSR DIV10_32                ; A = next decimal digit (low first); value /= 10
    ORA #'0'
    PHA
    INC DEC32_CNT
    LDA DEC32_VAL               ; quotient == 0 -> done generating
    ORA DEC32_VAL+1
    ORA DEC32_VAL+2
    ORA DEC32_VAL+3
    BNE @gen
@pad:
    CPY DEC32_CNT               ; pad with spaces while width > digit count
    BCC @emit
    BEQ @emit
    LDA #ASCII_SPACE
    JSR PRINT_CHAR              ; preserves X and Y
    DEY
    BRA @pad
@emit:
    PLA                         ; emit the digits high-to-low
    JSR PRINT_CHAR
    DEC DEC32_CNT
    BNE @emit
    RTS

; DIV10_32 - divide DEC32_VAL (32-bit) by 10 in place; remainder -> A. Clobbers X.
; Bit-serial long division: shift the dividend left 32 times into the remainder;
; whenever the remainder >= 10, subtract 10 and set the freed low (quotient) bit.
DIV10_32:
    LDA #$00                    ; remainder
    LDX #32
@l:
    ASL DEC32_VAL
    ROL DEC32_VAL+1
    ROL DEC32_VAL+2
    ROL DEC32_VAL+3
    ROL                         ; remainder = (remainder << 1) | carry
    CMP #10
    BCC @s
    SBC #10                     ; remainder -= 10 (carry set by CMP)
    INC DEC32_VAL               ; set the quotient's low bit (was 0)
@s:
    DEX
    BNE @l
    RTS



; Show help command - Display comprehensive list of all monitor commands
; Input: None (help is context-independent)
; Output: Multi-page help text displayed to screen with command syntax and descriptions
; Modifies: A, X, Y
; Note: Uses paging - user can press ESC to abort or ENTER to continue pages
; Print one help line that lines its description up in a column: like
; PRINT_MESSAGE, but a TAB byte ($09) is rendered as enough spaces to advance the
; cursor to HELP_DESC_COL. Each help string is "<syntax>",$09,"<description>",0,
; so every description starts at the same column regardless of the syntax width
; (now that the screen is 80 columns wide). Ptr in MON_MSG_PTR_LO/HI.
HELP_DESC_COL = 22
PRINT_HELP_LINE:
    LDY #$00
@loop:
    LDA (MON_MSG_PTR_LO),Y
    BEQ @done                   ; null terminator
    CMP #$09                    ; TAB -> pad to the description column
    BNE @putc
@pad:
    LDA CURSOR_X
    CMP #HELP_DESC_COL
    BCS @padded                 ; already at/past the column
    LDA #' '
    JSR PRINT_CHAR              ; (preserves Y, like the PRINT_MESSAGE loop relies on)
    BRA @pad
@padded:
    INY
    BNE @loop
@putc:
    JSR PRINT_CHAR
    INY
    BNE @loop
@done:
    RTS

; Interrupt service routines
;
; IRQ: driven by the ~60Hz periodic timer. Always acknowledge the timer (or it
; would re-fire immediately and storm); if BASIC has ON IRQ enabled, set the
; "happened" bit so BASIC's interpreter loop dispatches the handler.
IRQ_HANDLER:
    PHA                         ; preserve A (X/Y untouched)
    STA TIMER_IRQ_ACK           ; acknowledge the timer (value ignored)

    ; Advance the monotonic jiffy counter (K_GET_JIFFIES). INC touches no
    ; register, and RTI restores the flags, so this is free of side effects.
    INC JIFFY_LO
    BNE IRQ_JIFFY_DONE
    INC JIFFY_HI                ; carry into the high byte (wraps ~every 18 min)
IRQ_JIFFY_DONE:

    ; Count down an in-progress BEL beep and gate it off when it expires.
    LDA BEEP_TIMER
    BEQ IRQ_CHECK_BASIC         ; 0 = no beep running
    DEC BEEP_TIMER
    BNE IRQ_CHECK_BASIC         ; still sounding
    STZ SID_V1_CTRL             ; duration elapsed: gate off voice 1 (silence)

IRQ_CHECK_BASIC:
    LDA BASIC_IRQ_FLAGS         ; is BASIC's ON IRQ enabled?
    AND #INT_ENABLED
    BEQ IRQ_HANDLER_DONE        ; no -> nothing more to do
    LDA BASIC_IRQ_FLAGS
    ORA #INT_HAPPENED           ; flag the interrupt for BASIC's poll
    STA BASIC_IRQ_FLAGS
IRQ_HANDLER_DONE:
    PLA
    RTI

; NMI: the "stop" key. If BASIC has ON NMI enabled, flag it for BASIC; otherwise
; abandon whatever is running and return to the monitor command loop.
NMI_HANDLER:
    PHA
    LDA BASIC_NMI_FLAGS         ; is BASIC's ON NMI enabled?
    AND #INT_ENABLED
    BEQ NMI_HANDLER_BREAK       ; no -> break to the monitor
    LDA BASIC_NMI_FLAGS
    ORA #INT_HAPPENED           ; flag the interrupt for BASIC's poll
    STA BASIC_NMI_FLAGS
    PLA
    RTI

NMI_HANDLER_BREAK:
    LDX #STACK_TOP              ; reset the stack (discard interrupted context)
    TXS
    ; STOP can land anywhere, including places that own global state which their
    ; normal exit would have restored. Reset that state here, or it stays wrong for
    ; the rest of the session:
    STZ PAGE_IN_BREAK           ; STOP pressed AT a --MORE-- prompt skipped the
                                ;   clear after HANDLE_PAGE_BREAK, which left the
                                ;   pager permanently disabled system-wide
    ; The monitor itself is bank MON_BANK now, so break-in maps it rather than
    ; unmapping whatever was there. That is also what makes STOP reliable: a program
    ; that scribbles on MODULE_BANK can no longer lock you out of the monitor,
    ; because the NMI handler lives in always-mapped kernel ROM and re-maps on the
    ; way in. The cost is that the monitor can never show $B000-$EFFF as RAM -- it
    ; is standing in that window. Sibling banks are invisible for the same reason.
    LDA #MON_BANK
    STA MODULE_BANK
    CLI                         ; monitor runs with interrupts enabled
    JMP MON_ENTRY_BREAK         ; fresh prompt in the monitor bank, no banner

; ================================================================
; SOUND ROUTINES (SID voice 1)
; ================================================================
; All honor SOUND_ENABLE (0 = muted). Voice 1 is configured for a clean tone:
; instant attack, full sustain, no filter, master volume 15.

; SOUND_VOICE1 - set voice 1's envelope + volume for a sustained tone. Leaves the
; control register alone (the caller gates it). Uses A only; preserves X and Y.
SOUND_VOICE1:
    LDA #$00
    STA SID_V1_AD               ; attack 0, decay 0
    LDA #$F0
    STA SID_V1_SR               ; sustain 15, release 0
    LDA #$0F
    STA SID_MODEVOL             ; volume 15, filter off
    RTS

; BEEP - start a short (~130 ms) fixed-pitch beep on voice 1. Non-blocking: the
; timer IRQ gates it off after BEEP_LEN_JIFFIES ticks. Preserves X and Y (called
; from PRINT_CHAR for BEL).
BEEP:
    LDA SOUND_ENABLE
    BEQ BEEP_DONE               ; muted -> do nothing
    LDA #BEEP_FREQ_LO
    STA SID_V1_FREQLO
    LDA #BEEP_FREQ_HI
    STA SID_V1_FREQHI
    JSR SOUND_VOICE1
    LDA #SID_CTRL_TONE          ; triangle + gate on
    STA SID_V1_CTRL
    LDA #BEEP_LEN_JIFFIES
    STA BEEP_TIMER              ; the timer IRQ gates it off
BEEP_DONE:
    RTS

; SOUND_TONE ($FF33) - play a sustained tone on voice 1 until SOUND_OFF.
; Input: A = frequency low byte, X = frequency high byte. Clobbers A.
SOUND_TONE:
    STA SID_V1_FREQLO           ; store the frequency first (harmless if muted)
    STX SID_V1_FREQHI
    STZ BEEP_TIMER              ; not a timed beep; the caller controls duration
    LDA SOUND_ENABLE
    BEQ SOUND_TONE_MUTE         ; muted -> leave it silent
    JSR SOUND_VOICE1            ; envelope + volume
    LDA #SID_CTRL_TONE          ; triangle + gate on
    STA SID_V1_CTRL
    RTS
SOUND_TONE_MUTE:
    STZ SID_V1_CTRL             ; ensure the voice stays off
    RTS

; SOUND_OFF ($FF36) - stop voice 1 (gate off, silence).
SOUND_OFF:
    STZ BEEP_TIMER
    STZ SID_V1_CTRL
    RTS

; GET_JIFFIES ($FF39) - read the monotonic 60 Hz tick counter.
; Returns: A = low byte, X = high byte. Counts up from 0 at RESET and wraps
; every 65536 ticks (~18.2 minutes); callers should compare deltas with unsigned
; subtraction so the wrap is harmless. The timer IRQ increments it, so the read
; is made atomic (SEI) to avoid tearing between the two bytes; PHP/PLP preserves
; the caller's interrupt-enable state. Real-time programs use this for frame
; pacing (a fixed-tick accumulator loop).
GET_JIFFIES:
    PHP                         ; save the caller's I flag
    SEI                         ; block the timer IRQ across the 16-bit read
    LDA JIFFY_LO
    LDX JIFFY_HI
    PLP                         ; restore interrupt state
    RTS

; (The boot sign-on lives in the DOS shell now -- see _DOS_SPLASH in dos.asm.)
MSG_PAGE_PROMPT:     .BYTE "--MORE-- (SPACE, ESC=STOP)", 0
MSG_MODULE_FAIL:     .BYTE "MODULE NOT LOADED", $0D, $0A, 0

; ----------------------------------------------------------------
; LIST_MODULES - print the module catalog as "<bank>  <NAME>" lines, one per
; MODULE_DIR record. Exposed at $FF24 for the DOS 'BANKS' command. X holds the
; record offset across the print calls (saved on the stack, which clobber it).
; ----------------------------------------------------------------
LIST_MODULES:
    LDX #$00                    ; X = byte offset into MODULE_DIR
@rec:
    CPX #(MODULE_DIR_COUNT * MODULE_DIR_RECSIZE)
    BCS @done
    LDA MODULE_DIR,X            ; bank number
    PHX
    JSR PRINT_HEX_BYTE          ; (clobbers X)
    LDA #' '
    JSR PRINT_CHAR
    LDA #' '
    JSR PRINT_CHAR
    PLX
    LDA MODULE_DIR+3,X          ; name pointer (low/high) -> PRINT_MSG_AY
    LDY MODULE_DIR+4,X
    PHX
    JSR PRINT_MSG_AY            ; (clobbers X)
    JSR PRINT_NEWLINE
    PLX
    TXA
    CLC
    ADC #MODULE_DIR_RECSIZE
    TAX
    BRA @rec
@done:
    RTS

; ----------------------------------------------------------------
; Module directory: one 5-byte record per launchable module. The DOS resolves a
; typed name against this table (K_LAUNCH_BY_NAME) and maps + runs the bank.
;   byte 0      bank number (written to MODULE_BANK to map the module)
;   bytes 1-2   entry address (little-endian) - JMP target after mapping
;   bytes 3-4   pointer to the null-terminated launch name (typed at the DOS ])
; Adding a module = add a record + name string here and register its ROM image
; as that bank in the host bank table (Computer6502). See docs/ARCHITECTURE.md, Part 4.
; ----------------------------------------------------------------
MODULE_DIR_RECSIZE = 5
MODULE_DIR:
    .BYTE 1                     ; bank 1
    .WORD $B000                 ; entry (BASIC LAB_COLD at the window base)
    .WORD NAME_BASIC
    .BYTE 2                     ; bank 2
    .WORD $B000                 ; entry (DEVT_MAIN at the window base)
    .WORD NAME_ASM
    .BYTE 3                     ; bank 3
    .WORD $B000                 ; entry (FIG-Forth ENTER at the window base)
    .WORD NAME_FORTH
    .BYTE MON_BANK              ; bank 4 - the monitor
    .WORD MON_ENTRY_COLD        ; cold entry at the window base
    .WORD NAME_MON
MODULE_DIR_COUNT = (* - MODULE_DIR) / MODULE_DIR_RECSIZE

NAME_BASIC:          .BYTE "BASIC", 0
NAME_ASM:            .BYTE "ASM", 0
NAME_FORTH:          .BYTE "FORTH", 0
NAME_MON:            .BYTE "MON", 0


; ----------------------------------------------------------------
; MON_LAUNCH - map the monitor bank and enter it (ABI K_MON_ENTRY, $FF1E)
; ----------------------------------------------------------------
; The monitor is module bank MON_BANK. The DOS 'MON' verb and anything else that
; wants the monitor comes through $FF1E, so this is the only place that knows
; where it lives. Guarded the same way BANK_LAUNCH guards a module: an
; uninstalled bank reads as $00 (BRK) and a real entry never starts with one, so
; a missing monitor.rom reports itself instead of executing the empty window.
MON_LAUNCH:
    LDA #MON_BANK
    STA MODULE_BANK             ; map the monitor into $B000-$EFFF
    LDA MON_ENTRY_COLD          ; first opcode of the entry table
    BEQ MON_LAUNCH_MISSING      ; $00 = bank not installed
    JMP MON_ENTRY_COLD

MON_LAUNCH_MISSING:
    STZ MODULE_BANK             ; unmap -> window back to RAM
    LDA #<MSG_MODULE_FAIL
    LDY #>MSG_MODULE_FAIL
    JSR PRINT_MSG_AY
    JMP DOS_WARM

; ================================================================
; RESERVED I/O PAGE ($FE00-$FEFF)
; ================================================================
; Memory-mapped I/O (keyboard, file I/O, timer, bank select) is shadowed here by
; the emulator. Reserve the page so the linker errors if CODE ever grows into it.
.segment "IORESV"
.res $100

; ================================================================
; KERNEL API JUMP TABLE
; ================================================================
.segment "JUMPS"
.org $FF00

; These are indirect jumps to the actual routines
K_PRINT_CHAR:    JMP PRINT_CHAR         ; $FF00
K_PRINT_MESSAGE: JMP PRINT_MESSAGE      ; $FF03
K_PRINT_NEWLINE: JMP PRINT_NEWLINE      ; $FF06
K_GET_KEYSTROKE: JMP GET_KEYSTROKE      ; $FF09
K_CLEAR_SCREEN:  JMP CLEAR_SCREEN       ; $FF0C
K_GET_RAND_NUM:  JMP GET_RANDOM_NUMBER  ; $FF0F
K_RETURN_MODULE: JMP RETURN_FROM_MODULE ; $FF12 - module exit point (BASIC BYE, etc.)
K_READ_LINE:     JMP READ_COMMAND_LINE  ; $FF15
K_PARSE_HEX:     JMP HEX_QUAD_TO_ADDR   ; $FF18
K_PRINT_HEX_BYTE:JMP PRINT_HEX_BYTE     ; $FF1B
K_MON_ENTRY:     JMP MON_LAUNCH         ; $FF1E - DOS launches the monitor here
K_LAUNCH_BY_NAME:JMP LAUNCH_BY_NAME     ; $FF21 - DOS launches a module by name
K_LIST_MODULES:  JMP LIST_MODULES       ; $FF24 - print the module catalog (BANKS)
K_PRINT_DEC:     JMP PRINT_DEC          ; $FF27 - print a 32-bit value in decimal
K_PARSE_DEC:     JMP PARSE_DEC_ABI      ; $FF2A - parse a decimal string from MON_CMDBUF
K_SET_ATTR:      JMP SET_ATTR           ; $FF2D - set the color/attribute latch (VREG_ATTR)
K_PRINT_HELP_LINE: JMP PRINT_HELP_LINE  ; $FF30 - print "syntax"<TAB>"desc" (TAB pads to col 22)
K_SOUND_TONE:    JMP SOUND_TONE         ; $FF33 - play a tone on voice 1 (A=freq lo, X=freq hi)
K_SOUND_OFF:     JMP SOUND_OFF          ; $FF36 - stop voice 1 (gate off)
K_GET_JIFFIES:   JMP GET_JIFFIES        ; $FF39 - read the 60 Hz tick counter (A=lo, X=hi)
K_HEX_PAIR:      JMP HEX_PAIR_TO_BYTE   ; $FF3C - 2 hex digits -> byte (K_PARSE_HEX does 4)
K_PARSE_DEC_VAL: JMP PARSE_DECIMAL_VALUE; $FF3F - decimal digits -> DEC_RESULT, no side effects
; ================================================================
; RESET VECTORS
; ================================================================
.segment "VECS"
.org $FFFA

    .WORD NMI_HANDLER           ; NMI vector ($FFFA-$FFFB)
    .WORD RESET                 ; Reset vector ($FFFC-$FFFD)
    .WORD IRQ_HANDLER           ; IRQ vector ($FFFE-$FFFF)