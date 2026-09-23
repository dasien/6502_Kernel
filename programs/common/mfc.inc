; ============================================================================
; mfc.inc -- the MFC machine as a cc65 program sees it
; ============================================================================
; Every programs/*/glue.s includes this. Before it existed each glue file
; carried its own copy of these equates, 189 lines across ten files, and a
; register that moved had to be corrected in every one of them.
;
; Three things live here:
;   - the kernel ABI jump table at $FF00 (src/kernel/kernel.asm, JUMPS segment)
;   - the DOS ABI jump table at $AF00  (src/kernel/dos/dos.asm, DOSJUMP segment)
;   - the I/O registers a .PRG drives directly
;
; The I/O page is also described in src/kernel/kernel_vars.inc, for the ROM
; side. The two must agree; tests/scripts/check_io_equates.cmake compares them
; and fails the build if they drift. docs/board.md holds the full decode table,
; including the registers no program touches.
; ============================================================================

.ifndef MFC_INC
MFC_INC = 1

; ---- kernel ABI, $FF00-$FF41 ----------------------------------------------
; 23 entries, three bytes each. Call these rather than reaching into the ROM:
; the addresses are the contract, the routines behind them are not.
K_PRINT_CHAR       = $FF00      ; A = char -> screen
K_PRINT_MESSAGE    = $FF03      ; A/Y = null-terminated string
K_PRINT_NEWLINE    = $FF06      ; CR/LF
K_GET_KEYSTROKE    = $FF09      ; non-blocking: C set + A = char
K_CLEAR_SCREEN     = $FF0C      ; clear + home (also resets the kernel cursor)
K_GET_RAND_NUM     = $FF0F      ; -> A = pseudo-random byte
K_RETURN_MODULE    = $FF12      ; module exit point (BASIC BYE, and the like)
K_READ_LINE        = $FF15      ; read a command line into MON_CMDBUF
K_PARSE_HEX        = $FF18      ; 4 hex digits -> address
K_PRINT_HEX_BYTE   = $FF1B      ; A = byte -> two hex digits
K_MON_ENTRY        = $FF1E      ; launch the monitor
K_LAUNCH_BY_NAME   = $FF21      ; launch a module by name
K_LIST_MODULES     = $FF24      ; print the module catalog (BANKS)
K_PRINT_DEC        = $FF27      ; print a 32-bit value in decimal
K_PARSE_DEC        = $FF2A      ; parse a decimal string from MON_CMDBUF
K_SET_ATTR         = $FF2D      ; A = colour/attribute latch for following chars
K_PRINT_HELP_LINE  = $FF30      ; "syntax" TAB "description"
K_SOUND_TONE       = $FF33      ; tone on SID voice 1 (A = freq lo, X = freq hi)
K_SOUND_OFF        = $FF36      ; gate voice 1 off
K_GET_JIFFIES      = $FF39      ; 60 Hz monotonic counter -> A = lo, X = hi
K_HEX_PAIR         = $FF3C      ; 2 hex digits -> byte
K_PARSE_DEC_VAL    = $FF3F      ; decimal digits -> DEC_RESULT, no side effects
K_WAIT_FRAME       = $FF42      ; block until the VIC starts a new frame

; ---- DOS ABI, $AF00-$AF23 --------------------------------------------------
; The FS_ entries report failure in carry and leave X undefined.
DOS_COLD           = $AF00      ; shell cold entry
FS_OPEN            = $AF03      ; A/X = name ptr, Y = mode (0 read, 1 write)
FS_GETB            = $AF06      ; -> C clear + A = byte, or C set = EOF
FS_PUTB            = $AF09      ; A = byte; C set = error
FS_CLOSE           = $AF0C      ; C set if the flush or the finalize failed
FS_DIR_FIRST       = $AF0F      ; -> C clear + DOS_ENTRY filled, or C set = none
FS_DIR_NEXT        = $AF12
BLK_READ_SECTOR    = $AF15
BLK_WRITE_SECTOR   = $AF18
FS_DELETE          = $AF1B
DOS_WARM           = $AF1E      ; re-enter the shell, no banner: how a .PRG exits
FS_RENAME          = $AF21      ; A/X = old name, DOS_PTR2 = new

DOS_ENTRY          = $0320      ; 32-byte current directory entry (name = first 11)

; ---- PIA -------------------------------------------------------------------
; Which control keys are held RIGHT NOW. Distinct from the keystroke buffer at
; $FE00, which records what was typed and carries no key-up: polling that cannot
; tell held from released, and the host only auto-repeats the most recent key, so
; moving while firing is impossible through it. Real-time programs read here.
KEY_STATE          = $FE0F

; ---- 6551 ACIA -------------------------------------------------------------
; Status bit 5 is /DCD and is ACTIVE LOW: set means NO carrier.
ACIA_DATA          = $FE29
ACIA_STATUS        = $FE2A
ACIA_COMMAND       = $FE2B
ACIA_CONTROL       = $FE2C

; ---- VIC register port -----------------------------------------------------
; The 80x25 CP437 character and colour planes are inside the VIC, not in the
; 64K map. Set a cell index through VREG_ADDR_LO/HI, then read or write the
; auto-incrementing data ports.
VREG_ADDR_LO       = $FE2D      ; cell index low (0..1999)
VREG_ADDR_HI       = $FE2E      ; cell index high
VREG_CHAR          = $FE2F      ; glyph data port; auto-increments the index
VREG_COLOR         = $FE30      ; colour/attribute data port; auto-increments
VREG_ATTR          = $FE31      ; attribute latch [R][BR][bg:3][fg:3]
VREG_CMD           = $FE32      ; command engine
VREG_STATUS        = $FE33      ; 0 = ready
VREG_CURSOR_LO     = $FE34      ; displayed cursor cell low
VREG_CURSOR_HI     = $FE35      ; cursor cell high; bit7 = hidden
VREG_CMD_PARAM     = $FE36      ; fill char for clear / fill-row / scroll
VREG_SCROLL_BOT    = $FE37      ; scroll-region bottom row

; Frame counter. READ: increments once per frame, wrapping; not a flag and not
; cleared by reading, so any number of readers see the same value and a delta above
; one says frames were missed. WRITE any value: present -- this frame is finished,
; show it now rather than at the next boundary. A program that presents is shown a
; frame sooner; one that never does is repainted at every boundary as before.
VREG_FRAME         = $FECD

; The commands a .PRG issues. The VIC answers more of them -- row sizing, font
; bank select, fine scroll -- which only the kernel drives; see docs/board.md.
VCMD_CLEAR         = $01        ; fill the screen (char = param, colour = latch)
VCMD_SCROLL_UP     = $02        ; scroll up one row, blank the bottom
VCMD_SCROLL_DOWN   = $03        ; scroll down one row, blank the top
VCMD_FILL_ROW      = $04        ; fill the row holding the current cell
VCMD_SCROLL_TOP    = $07        ; set the scroll-region top row (row = param)

; ---- RTC -------------------------------------------------------------------
; Writing RTC_LATCH snapshots the live clock into the read registers. Programs
; use it to seed an RNG so a run differs from the last, and to drive a 1 Hz tick.
RTC_LATCH          = $FE55
RTC_SEC            = $FE56      ; BCD seconds
RTC_MIN            = $FE57      ; BCD minutes
RTC_HOUR           = $FE58      ; BCD hours
RTC_FATTIME_LO     = $FE5D      ; host-packed FAT time, low
RTC_FATTIME_HI     = $FE5E      ; host-packed FAT time, high

; ---- VIC soft font ---------------------------------------------------------
; The glyph shapes are RAM inside the chip, reached through this index/data pair.
; They are NOT in the 6502's address space, so redefining a character costs no
; memory here -- but every byte of font data costs a port write. Sixteen sets.
VREG_FONT_LO       = $FE62      ; font byte index low
VREG_FONT_HI       = $FE63      ; font byte index high
VREG_FONT_DATA     = $FE64      ; font data port; auto-increments

; ---- VIC soft palette ------------------------------------------------------
; Sixteen colour slots, three bytes each. Every attribute byte names SLOTS, not
; colours, so these decide what the whole machine looks like. The index counts
; bytes, so restating one slot is a seek and three writes.
;
; A program only touches this if it has an opinion about its colours: load them
; on entry and the DOS puts the theme back when it gets the screen again. A
; program with no opinion inherits whatever is loaded, which is what you want
; from anything that is mostly text.
VREG_PAL_IDX       = $FECB      ; palette byte index (0..47)
VREG_PAL_DATA      = $FECC      ; palette data port; auto-increments

; ---- VIC sprites -----------------------------------------------------------
; 17 sprites of SPR_STRIDE bytes each, $FE65-$FECA.
SPR0               = $FE65
SPR_STRIDE         = 6

.endif
