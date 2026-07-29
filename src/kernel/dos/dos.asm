; ================================================================
; dos.asm - MFC-DOS resident ROM ($9000-$AFFF, always mapped)
; ================================================================
; The resident operating system for MFC-DOS: the FAT16 filesystem driver and
; (later) the DOS command shell. See docs/SYSTEM_INTERNALS.md.
;
; This region is always mapped by the emulator (it is NOT in the bankable
; $B000-$DFFF module window), so its routines are reachable at all times. The
; stable entry points live in a jump table at $AF00 (the "DOS ABI"), mirroring
; the kernel's own $FF00 table - callers bind to those fixed addresses, never to
; the moving internals below.
;
; STEP 2.2 (this commit): block-device equates, the 512-byte sector read/write
; primitives (the 6502 side of the $FE24-$FE28 registers), and FS ABI stubs.
; The FAT16 mount / directory walk / cluster-chain read fill in the FS_* stubs
; in step 2.3; the DOS shell cold entry is wired in phase 4.
; ================================================================

.PC02                                   ; WDC 65C02 instruction set

; ----------------------------------------------------------------
; Block-device registers (I/O page, just past MODULE_BANK $FE23)
; ----------------------------------------------------------------
BLK_LBA          = $FE24                ; 16-bit sector number ($FE24 lo / $FE25 hi)
BLK_CMD          = $FE26                ; write: command code
BLK_STATUS       = $FE27                ; read: 0 = ready, non-zero = error
BLK_DATA         = $FE28                ; 512-byte sector data port (auto-incrementing)

BLK_CMD_READ     = $01                  ; read sector -> device buffer
BLK_CMD_WRITE    = $02                  ; device buffer -> sector
BLK_READY        = $00                  ; BLK_STATUS: ready / last op OK

SECTOR_SIZE      = 512

; ----------------------------------------------------------------
; BIOS ABI (kernel $FF00 jump table) + BIOS RAM the DOS shell uses
; ----------------------------------------------------------------
K_PRINT_CHAR     = $FF00
K_PRINT_MESSAGE  = $FF03                ; MON_MSG_PTR_LO/HI -> string
K_PRINT_NEWLINE  = $FF06
K_CLEAR_SCREEN   = $FF0C
K_READ_LINE      = $FF15                ; read a line -> MON_CMDBUF, len MON_CMDLEN
K_PARSE_HEX      = $FF18                ; X = MON_CMDBUF index -> MON_CURRADDR, X += 4
K_PRINT_HEX_BYTE = $FF1B                ; A -> two hex digits
K_MON_ENTRY      = $FF1E                ; launch the monitor (returns via Q -> DOS_WARM)
K_LAUNCH_BY_NAME = $FF21                ; A/X=name -> launch a ROM module, or carry set
K_LIST_MODULES   = $FF24                ; print the module catalog (BANKS command)
K_GET_KEYSTROKE  = $FF09                ; non-blocking key read: C set + A = key
K_PRINT_DEC      = $FF27                ; A/X = ptr to 4-byte LE value, Y = field width
K_SET_ATTR       = $FF2D                ; A = color/attribute latch for next chars
K_PRINT_HELP_LINE = $FF30               ; MON_MSG_PTR -> "syntax"<TAB>"desc" (TAB pads to col)
CURSOR_X         = $0276                ; current cursor column (for box padding)

; RTC (real-time clock) registers in the I/O page, read directly. Write RTC_LATCH
; to snapshot the host time; the field registers are BCD (day-of-week is 0=Sun..6).
RTC_LATCH        = $FE55
RTC_SEC          = $FE56
RTC_MIN          = $FE57
RTC_HOUR         = $FE58
RTC_DAY          = $FE59
RTC_MONTH        = $FE5A
RTC_YEAR         = $FE5B
RTC_DOW          = $FE5C
RTC_FATTIME_LO   = $FE5D                ; host-packed FAT time/date (see Rtc.h)
RTC_FATTIME_HI   = $FE5E
RTC_FATDATE_LO   = $FE5F
RTC_FATDATE_HI   = $FE60

MON_CMDBUF       = $0200                ; BIOS command-line buffer (page aligned)
MON_CMDLEN       = $026A                ; current command length
MON_MSG_PTR_LO   = $16                  ; message pointer for K_PRINT_MESSAGE
MON_MSG_PTR_HI   = $17
CMD_LINE_COUNT   = $21                  ; kernel pager line counter (reset after a command)
MON_CURRADDR_LO  = $14                  ; K_PARSE_HEX result / our mem cursor (zp)
MON_CURRADDR_HI  = $15
MON_STARTADDR_LO = $026C                ; range start (SAVE) / addr override (LOAD)
MON_STARTADDR_HI = $026D
MON_ENDADDR_LO   = $026E                ; range end (SAVE)
MON_ENDADDR_HI   = $026F

ASCII_CR         = $0D
ASCII_LF         = $0A
ASCII_SPACE      = $20

; ----------------------------------------------------------------
; Host byte-stream file I/O (PIA) - used by IMPORT/EXPORT to bridge a host
; (macOS) file and a FAT16 file via the file-picker dialog.
; ----------------------------------------------------------------
FIO_COMMAND      = $FE10                ; file command register
FIO_STATUS       = $FE11                ; file status register
FIO_DATA         = $FE22                ; byte-stream data register
FIO_OPEN_RD      = $03                  ; open host file for reading (open dialog)
FIO_OPEN_WR      = $04                  ; open host file for writing (save dialog)
FIO_CLOSE        = $05                  ; close/flush the host stream
FIO_INPROG       = $01                  ; status: operation in progress
FIO_EOF          = $04                  ; status (read): no more bytes
FIO_ERROR        = $FF                  ; status: error / cancelled

; ----------------------------------------------------------------
; DOS zero page (the free $3A-$5A gap; clear of monitor $14-$39,
; BASIC $00-$13/$5B-$FF, and dev-tools $C0-$DF)
; ----------------------------------------------------------------
BLK_BUF_PTR      = $3A                  ; $3A-$3B: caller's 512-byte sector buffer
DOS_PTR          = $3C                  ; $3C-$3D: filename pointer (FS_OPEN)
DOS_PTR2         = $3E                  ; $3E-$3F: second name pointer (FS_RENAME)

; ----------------------------------------------------------------
; DOS filesystem state block ($0300-$034F)
; ----------------------------------------------------------------
; A resident scratch/state area for the FAT16 driver. $0300-$03FF is unused by
; the kernel, BASIC, and dev-tools, so it is safe across every FS caller. The
; driver streams sectors through the block device's own buffer (no 512-byte RAM
; sector buffer), so this block only holds mount info, cursors, and the current
; 32-byte directory entry.
DOS_MOUNTED      = $0300                ; 0 = not mounted, 1 = mounted
DOS_SEC_PER_CLUS = $0301                ; sectors per cluster
DOS_NUMFATS      = $0302                ; number of FATs
DOS_FATSIZE      = $0303                ; word: sectors per FAT
DOS_FAT_START    = $0305                ; word: first FAT sector (LBA)
DOS_ROOT_START   = $0307                ; word: root directory first sector (LBA)
DOS_DATA_START   = $0309                ; word: first data sector (LBA)
DOS_ROOT_ENTS    = $030B                ; word: root directory entry count
; directory enumeration cursor
DOS_DIR_LBA      = $030D                ; word: current directory sector (LBA)
DOS_DIR_IDX      = $030F                ; entry index within the sector (0-15)
DOS_DIR_LEFT     = $0310                ; word: root entries remaining
; open-file state (used in step 2.3b)
DOS_F_CLUS       = $0312                ; word: current cluster
DOS_F_LBA        = $0314                ; word: current data sector (LBA)
DOS_F_SIC        = $0316                ; sector index within the cluster
DOS_F_OFF        = $0317                ; word: byte offset within the sector (0-511)
DOS_F_LEFT       = $0319                ; 4 bytes: file bytes remaining ($0319-$031C)
; scratch + buffers
DOS_TMP          = $031D                ; word: general 16-bit scratch
; NOTE: DOS_TMP2 must NOT live at $031F: its high byte ($0320) would alias
; DOS_ENTRY+0. Subdirectory enumeration does a FAT read (which uses DOS_TMP2)
; between reading a directory entry and inspecting it, so any overlap corrupts
; the entry's name. Relocated below DOS_ENTRY's neighbours, into free state.
DOS_TMP2         = $037C                ; word: general 16-bit scratch ($037C-$037D)
DOS_ENTRY        = $0320                ; 32-byte current directory entry ($0320-$033F)
DOS_NAME83       = $0340                ; 11-byte 8.3 match buffer ($0340-$034A)
; mount geometry for allocation
DOS_TOTAL_SEC    = $034B                ; word: total sectors (BPB TotalSectors16)
DOS_MAX_CLUS     = $034D                ; word: highest valid cluster number
; open-for-write state
DOS_W_MODE       = $034F                ; open mode: 0 = read, non-zero = write
DOS_W_DIRENT_LBA = $0350                ; word: dir sector holding the file's entry
DOS_W_DIRENT_IDX = $0352                ; byte: slot index within that sector (0-15)
DOS_W_FIRST_CLUS = $0353                ; word: first cluster (0 = none allocated yet)
DOS_W_PREV_CLUS  = $0355                ; word: previous cluster (for chaining)
DOS_W_SIZE       = $0357                ; 4 bytes: bytes written so far ($0357-$035A)
; FAT helper parameters / scratch
DOS_ARG_CLUS     = $035B                ; word: cluster argument for FAT helpers
DOS_ARG_VAL      = $035D                ; word: FAT entry value argument/result
DOS_NEW_CLUS     = $035F                ; word: freshly allocated cluster (chaining stash)
DOS_FREE_NEXT    = $0361                ; word: next-cluster stash (free-chain walk)
DOS_SH_NAMEIDX   = $0363                ; shell: index of an argument name in MON_CMDBUF
DOS_SH_HASADDR   = $0364                ; shell: LOAD given an explicit address?
DOS_SH_NAMEIDX2  = $0365                ; shell: second arg index (COPY destination)
DOS_FREE_CNT     = $0366                ; word: FREE free-cluster accumulator ($0366-$0367)
; ---- drawers: current directory + directory-iterator + path-resolution state ----
DOS_CWD_CLUS     = $0368                ; word: current dir first cluster (0 = root)
DOS_CWD_NAME     = $036A                ; 9 bytes: prompt display name ($036A-$0372, NUL-term)
DOS_DIR_MODE     = $0373                ; dir iterator mode: 0 = root region, 1 = subdir chain
DOS_DIR_CLUS     = $0374                ; word: subdir iterator's current cluster
DOS_DIR_SIC      = $0376                ; byte: sector index within the dir cluster
DOS_TGT_CLUS     = $0377                ; word: resolved target dir cluster (0 = root)
DOS_RES_NAMEPTR  = $0379                ; word: ptr to bare 8.3 name after path prefix
DOS_W_ATTR       = $037B                ; byte: attribute for the next dir entry written
DOS_RES_SLASH    = $037E                ; byte: path-resolution slash-index scratch
DOS_ALLOC_HINT   = $037F                ; word: next-free-cluster rover ($037F-$0380)
DOS_ALLOC_WRAP   = $0381                ; byte: alloc scan has wrapped past the end
; Launch argument: the command tail after a program name (e.g. the "SYSTEM/DIAL.LST"
; in "EDIT SYSTEM/DIAL.LST"), null-terminated, empty if none. DOS fills it before
; every launch-by-name; a launched program reads it here. Placed ABOVE the FS
; working vars so _FS_OPEN during the launch can't clobber it, and below $0800 so
; it survives into the program (cc65 programs don't touch $03xx).
DOS_ARGBUF       = $0382                ; launch argument, null-terminated ($0382-$03B1)
DOS_ARGBUF_MAX   = 48                   ; buffer size (47 chars + NUL)

; FAT timestamp snapshot (read from the RTC's FAT-format registers on write)
DOS_FTIME        = $03B2                ; word: packed FAT time (hh:mm:ss/2)
DOS_FDATE        = $03B4                ; word: packed FAT date (y-1980:month:day)
DOS_W_FREE_LBA   = $03B6                ; word: sector of the first reclaimable ($E5)
                                        ;   directory slot seen this scan (0 = none)
DOS_W_FREE_IDX   = $03B8                ; byte: slot index within DOS_W_FREE_LBA

; FAT16 end-of-chain threshold (>= this means last cluster)
FAT_EOC          = $FFF8

; FAT16 directory-entry field offsets (within DOS_ENTRY / on disk)
DIR_NAME         = $00                  ; 11 bytes: 8.3 name (space padded)
DIR_ATTR         = $0B                  ; attribute byte
DIR_CLUSTER_LO   = $1A                  ; word: first cluster (low; high is 0 on FAT16)
DIR_SIZE         = $1C                  ; 4 bytes: file size

ATTR_LFN         = $0F                  ; (attr & $0F)==$0F -> long-file-name entry
ATTR_VOLUME      = $08                  ; volume-label bit
ATTR_DIRECTORY   = $10                  ; subdirectory ("drawer") bit
DIRENT_END       = $00                  ; name[0]: end of directory
DIRENT_DELETED   = $E5                  ; name[0]: deleted entry

.segment "CODE"

; ----------------------------------------------------------------
; DOS_SIGNATURE - first bytes of the ROM (at $9000)
; ----------------------------------------------------------------
; A recognizable marker so the emulator/tests can confirm the DOS ROM is
; mapped at $9000, and so a future loader can sanity-check the image.
DOS_SIGNATURE:
    .BYTE "MFC-DOS", $00
; DOS version (major, minor). History:
;   0.1  initial resident ROM stub (phase 2.1)
;   0.2  FAT16 read: mount, directory, file read (phase 2)
;   0.3  FAT16 write + erase (phase 3)
;   1.0  the DOS shell: boot-into-MFC/OS, file verbs, IMPORT/EXPORT,
;        launch-by-name (phase 4)
;   1.1  utility commands (COPY, DISKFREE, MEMMAP, VERSION, MORE, wildcard
;        CATALOG) + shared decimal conversion via the kernel ABI
;   1.2  one-level subdirectories ("drawers"): NEWDRAWER/OPEN/CLOSE/DROPDRAWER,
;        a unified root/subdir directory iterator, and path resolution
;        (FILE / DRAWER/FILE / /FILE) for the file verbs
;   1.3  cross-drawer COPY (qualified src/dst paths) + new MOVE command
;   1.4  blank line between a command's output and the next prompt (shell polish)
;   1.5  MEMMAP reflects 80-col: screen moved behind the VIC port; $0400 is free RAM
;   1.6  boot sign-on splash box (CP437 + color): OPERATIONAL + OS version + free RAM
;   1.7  path resolution no longer clobbers the caller's buffer: DRAWER/FILE
;        restores the '/' after parsing, so a reused path (e.g. re-reading
;        SYSTEM/IRC.LST every redial) resolves correctly on the 2nd+ call
;   1.8  return-to-DOS polish: the prompt reclaims the default colour (a program
;        that left the VIC attribute latch on another colour, e.g. TERM after a
;        coloured BBS, no longer bleeds into the ] prompt), and a blank line is
;        printed on re-entry so the prompt clears row 0
;   1.9  MORE folds into TYPE: the kernel now pages all output (system-wide
;        --MORE--), so DOS's separate MORE pager was removed and CATALOG/TYPE
;        page automatically
;   1.10 HELP is now a two-column verb/description list (like the monitor's ?),
;        rendered via the kernel's new K_PRINT_HELP_LINE ($FF30)
;   1.11 launch-by-name defaults the ".PRG" extension: "EDIT" runs "EDIT.PRG"
;        (tries the bare name first; only appends when the name has no extension)
;   1.12 launch-by-name passes the command tail to the program in DOS_ARGBUF
;        ($0382), so e.g. "EDIT SYSTEM/DIAL.LST" opens that file
;   1.13 kernel SID sound chip: BEL beeps; K_SOUND_TONE/OFF ABI (kernel v3.21)
;   1.14 DATE command shows the date/time from the new RTC device ($FE55-$FE5C)
;   1.15 files are timestamped from the RTC on write; CATALOG shows the date/time
;   1.16 kernel RNG reworked: RTC-seeded 16-bit LFSR (kernel v3.22)
;   1.17 kernel K_GET_JIFFIES ($FF39): 60 Hz monotonic tick counter (kernel v3.23)
;   1.18 host-interop safety: FAT writes are mirrored into every FAT copy (hosts
;        format with two by default, and a checker "repairing" the divergence
;        discards everything the machine wrote), and _FS_MOUNT validates the BPB
;        instead of trusting it -- a FAT12/FAT32 or non-512-byte-sector image is
;        refused rather than driven as FAT16 and destroyed on the first write
DOS_VERSION:
    .BYTE $01, $12                      ; version 1.18 (major, minor)

; ================================================================
; DOS SHELL (CCP) - the MFC/OS front door
; ================================================================
; RESET (BIOS) hands control here after init. The shell prints a sign-on
; banner, then loops: print the '>' prompt, read a line via the BIOS, parse a
; verb, and dispatch. Built-in verbs: HELP, MON (launch the monitor), CATALOG /
; CAT (list files), TYPE NAME (print a file). Phase 4.2 adds the write verbs;
; 4.3 adds launch-by-name. Reached via the DOS ABI: DOS_COLD ($AF00, cold boot)
; and DOS_WARM ($AF1E, re-entry from the monitor's Q command).

; ----------------------------------------------------------------
; _DOS_COLD - cold entry: banner, then the prompt loop
; ----------------------------------------------------------------
_DOS_COLD:
    LDX #$FF
    TXS                                 ; clean stack
    JSR _DOS_SPLASH                     ; the sign-on box (OPERATIONAL + ver + free)
    JMP _DOS_PROMPT

; ----------------------------------------------------------------
; _DOS_SPLASH - the boot sign-on: a centred CP437 box framing the OPERATIONAL
; banner, the OS version (from DOS_VERSION), and the free-RAM figure. The box is
; 40 wide, indented 20 (centred on 80 cols): left border at column 20, right at
; column 59. Content lines pad to the right border using the live cursor column
; (CURSOR_X), so a variable-width version number still lines up.
; ----------------------------------------------------------------
BOX_INDENT_COL  = 20
BOX_RIGHT_COL   = 59
ATTR_BORDER     = $40 | $06             ; bright cyan
ATTR_TITLE      = $40 | $07             ; bright white
ATTR_INFO       = $02                   ; green

; The box is just four rows: top border, OPERATIONAL, version/free, bottom
; border. Borders are always drawn in ATTR_BORDER (cyan); only the content text
; takes its own color (_BOX_OPEN/_BOX_PADCLOSE reset to cyan around each line).
_DOS_SPLASH:
    JSR K_PRINT_NEWLINE
    LDA #ATTR_BORDER
    JSR K_SET_ATTR
    LDA #$C9                            ; top corners
    LDX #$BB
    JSR _BOX_RULE
    JSR _BOX_OPER                       ; "MFC 6502  OPERATIONAL" (white text)
    JSR _BOX_VERFREE                    ; "MFC/OS x.y   NNNNN BYTES FREE" (green)
    LDA #ATTR_BORDER
    JSR K_SET_ATTR
    LDA #$C8                            ; bottom corners
    LDX #$BC
    JSR _BOX_RULE
    LDA #ATTR_INFO                      ; restore the normal text colour
    JSR K_SET_ATTR
    JMP K_PRINT_NEWLINE

; Print BOX_INDENT_COL spaces (lands the cursor on the box's left column).
_BOX_PAD_INDENT:
    LDX #BOX_INDENT_COL
@p: LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    DEX
    BNE @p
    RTS

; A horizontal rule: indent, left corner (A), 38 horizontals, right corner (X).
; Drawn in whatever attribute the caller set (always cyan for the box).
_BOX_RULE:
    PHA
    PHX
    JSR _BOX_PAD_INDENT
    PLA                                 ; right corner (pushed last) -> X
    TAX
    PLA                                 ; left corner -> A
    JSR K_PRINT_CHAR                    ; left corner
    PHX                                 ; save right corner
    LDX #38
@h: LDA #$CD
    JSR K_PRINT_CHAR
    DEX
    BNE @h
    PLA                                 ; right corner
    JSR K_PRINT_CHAR
    JMP K_PRINT_NEWLINE

; Open an interior content line: cyan left border (indent + box bar). The caller
; then sets its text color and prints content, finishing with _BOX_PADCLOSE.
_BOX_OPEN:
    LDA #ATTR_BORDER
    JSR K_SET_ATTR
    JSR _BOX_PAD_INDENT
    LDA #$BA
    JSR K_PRINT_CHAR
    RTS

; Pad to the right border column, then draw the cyan right border + newline.
_BOX_PADCLOSE:
@l: LDA CURSOR_X
    CMP #BOX_RIGHT_COL
    BCS @d
    LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    BRA @l
@d: LDA #ATTR_BORDER                    ; border is always cyan, not the text color
    JSR K_SET_ATTR
    LDA #$BA
    JSR K_PRINT_CHAR
    JMP K_PRINT_NEWLINE

; Centred OPERATIONAL line (bright white text, cyan borders).
_BOX_OPER:
    JSR _BOX_OPEN
    LDA #ATTR_TITLE
    JSR K_SET_ATTR
    LDX #8                              ; left padding to centre the 21-char text
@s: LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    DEX
    BNE @s
    LDA #<MSG_BOX_OPER
    LDX #>MSG_BOX_OPER
    JSR _DOS_PMSG
    JMP _BOX_PADCLOSE

; "MFC/OS <ver>   NNNNN BYTES FREE" line (green text, cyan borders).
_BOX_VERFREE:
    JSR _BOX_OPEN
    LDA #ATTR_INFO
    JSR K_SET_ATTR
    LDX #5
@s: LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    DEX
    BNE @s
    LDA #<MSG_DOS_VER                   ; "MFC/OS "
    LDX #>MSG_DOS_VER
    JSR _DOS_PMSG
    LDA DOS_VERSION
    JSR _DOS_PRINT_BYTE_DEC
    LDA #'.'
    JSR K_PRINT_CHAR
    LDA DOS_VERSION+1
    JSR _DOS_PRINT_BYTE_DEC
    LDA #<MSG_BOX_FREE                  ; "   34816 BYTES FREE"
    LDX #>MSG_BOX_FREE
    JSR _DOS_PMSG
    JMP _BOX_PADCLOSE

; ----------------------------------------------------------------
; _DOS_WARM - re-entry from the monitor (no banner)
; ----------------------------------------------------------------
_DOS_WARM:
    LDX #$FF
    TXS
    JSR K_PRINT_NEWLINE                 ; one blank line on return so the prompt
                                        ; clears row 0 (a program just cleared+homed)
_DOS_PROMPT:
    LDA #$02                            ; reclaim the default colour (green on black):
    JSR K_SET_ATTR                     ; a launched program (TERM following a BBS's
                                       ; ANSI colours, EDIT, a game) may have left the
                                       ; VIC attribute latch on another colour
    LDA DOS_CWD_NAME                    ; in a drawer? show its name before ']'
    BEQ @bracket
    LDA #<DOS_CWD_NAME
    STA MON_MSG_PTR_LO
    LDA #>DOS_CWD_NAME
    STA MON_MSG_PTR_HI
    JSR K_PRINT_MESSAGE
@bracket:
    LDA #']'                            ; ']' distinguishes the DOS prompt from the monitor's
    JSR K_PRINT_CHAR
    JSR K_READ_LINE                     ; -> MON_CMDBUF, length MON_CMDLEN
    LDA MON_CMDLEN
    BNE @run
    JMP _DOS_PROMPT                     ; empty line
@run:
    STZ CMD_LINE_COUNT                  ; a command's output starts at line 0: the
                                        ; prompt and the echoed command (incl. its CR)
                                        ; must not count toward the pager
    JSR _DOS_DISPATCH
    STZ CMD_LINE_COUNT                  ; ...and its count must not bleed into the
                                        ; trailing blank + next prompt (spurious --MORE--)
    JSR K_PRINT_NEWLINE                 ; blank line between a command's output
    JMP _DOS_PROMPT                     ; and the next prompt (not on empty input)

; ----------------------------------------------------------------
; _DOS_DISPATCH - match the typed verb and run its handler
; ----------------------------------------------------------------
_DOS_DISPATCH:
    ; Walk DOS_VERB_TAB: try each keyword, and on a match jump to its handler. If
    ; none matches, fall through to launch-by-name below.
    ;
    ; Table-driven rather than 25 open-coded compare-and-jump blocks (~300 bytes of
    ; near-identical code). Adding a verb is now one .word pair instead of five
    ; instructions plus a uniquely-named local label, and the long chain of forward
    ; branches that shape produced was a standing branch-range hazard.
    ;
    ; ORDER IS BEHAVIOUR and is preserved exactly from the old chain: a keyword that
    ; is a prefix of another must come second (CATALOG before CAT), because
    ; _DOS_VERB_MATCH accepts a keyword that ends at a space or end-of-line.
    STZ DOS_TMP                         ; byte offset into the table
@try:
    LDX DOS_TMP
    LDA DOS_VERB_TAB+1,X                ; keyword pointer high
    BEQ @nomatch                        ; $0000 terminator: no built-in verb
    TAX                                 ; matcher wants the pointer in A/X
    LDY DOS_TMP
    LDA DOS_VERB_TAB,Y                  ; keyword pointer low
    JSR _DOS_VERB_MATCH
    BCC @hit
    LDA DOS_TMP                         ; next entry (4 bytes each)
    CLC
    ADC #$04
    STA DOS_TMP
    BRA @try
@hit:
    LDX DOS_TMP                         ; tail-jump to the handler, so its RTS
    LDA DOS_VERB_TAB+2,X                ;   returns to _DOS_DISPATCH's caller just
    STA DOS_PTR                         ;   as the old direct JMPs did
    LDA DOS_VERB_TAB+3,X
    STA DOS_PTR+1
    JMP (DOS_PTR)

@nomatch:
    ; No built-in command matched - launch a program by name. A leading '&'
    ; forces the disk path (skip the ROM-module check); otherwise modules win.
    LDX #$00                            ; name starts at offset 0...
    LDA MON_CMDBUF
    CMP #'&'
    BNE @noamp
    LDX #$01                            ; ...or just past a leading '&'
@noamp:
    STX DOS_SH_NAMEIDX
    TXA                                 ; isolate the first word: null-terminate it
    TAY
@end:
    CPY MON_CMDLEN
    BCS @term
    LDA MON_CMDBUF,Y
    CMP #ASCII_SPACE
    BEQ @term
    INY
    BRA @end
@term:
    LDA #$00
    STA MON_CMDBUF,Y
    JSR _DOS_CAPTURE_ARG                ; stash the command tail for the launched program
                                        ; (must be before the .PRG-append mangles MON_CMDBUF)
    JSR K_PRINT_NEWLINE
    LDA DOS_SH_NAMEIDX                  ; '&' present? -> skip the module check
    BNE @disk
    LDA #<MON_CMDBUF                    ; try a ROM module (BASIC/ASM)
    LDX #>MON_CMDBUF
    JSR K_LAUNCH_BY_NAME               ; runs it (no return) on a match
@disk:
    LDA DOS_SH_NAMEIDX                  ; name ptr = MON_CMDBUF + offset
    LDX #>MON_CMDBUF                    ; (low byte = offset, MON_CMDBUF is page-aligned)
    JSR _DOS_RUN_FILE                  ; loads + runs a disk .PRG (no return on success)
    ; Not found as typed. If the name has no extension, default it to ".PRG"
    ; (so "EDIT" runs "EDIT.PRG") and try once more before giving up.
    LDX DOS_SH_NAMEIDX
@extscan:
    LDA MON_CMDBUF,X
    BEQ @adddot                        ; reached the terminator with no '.' -> add ".PRG"
    CMP #'.'
    BEQ @bad                           ; already has an extension -> don't guess
    INX
    BNE @extscan
@adddot:
    LDY #$00                            ; append ".PRG",0 at the terminator (X)
@cpext:
    LDA DOS_EXT_PRG,Y
    STA MON_CMDBUF,X
    BEQ @extdone                        ; the trailing null was just copied
    INX
    INY
    BRA @cpext
@extdone:
    LDA DOS_SH_NAMEIDX
    LDX #>MON_CMDBUF
    JSR _DOS_RUN_FILE                  ; retry as "<name>.PRG"
@bad:
    LDA #<MSG_DOS_BADCMD
    STA MON_MSG_PTR_LO
    LDA #>MSG_DOS_BADCMD
    STA MON_MSG_PTR_HI
    JMP K_PRINT_MESSAGE                 ; tail (RTS to _DOS_PROMPT)

; ----------------------------------------------------------------
; _DOS_DO_CLS - CLS / CLEAR: clear the screen (kernel CLEAR_SCREEN at $FF0C,
; the same routine the monitor's C: uses)
; ----------------------------------------------------------------
_DOS_DO_CLS:
    JMP K_CLEAR_SCREEN                  ; tail (its RTS returns to _DOS_PROMPT)

; ----------------------------------------------------------------
; _DOS_DO_BANKS - BANKS: list the ROM module catalog (bank + name)
; ----------------------------------------------------------------
_DOS_DO_BANKS:
    JMP K_LIST_MODULES                 ; tail (its RTS returns to _DOS_PROMPT)

; ----------------------------------------------------------------
; _DOS_VERB_MATCH - does MON_CMDBUF start with the keyword in A/X?
; ----------------------------------------------------------------
; In: A/X = ptr to a null-terminated UPPERCASE keyword. Input in MON_CMDBUF is
; already uppercased by READ_COMMAND_LINE. Out: carry clear = match, with Y =
; index of the delimiter (space or end) just past the verb; carry set = no match.
_DOS_VERB_MATCH:
    STA DOS_PTR
    STX DOS_PTR+1
    LDY #$00
@loop:
    LDA (DOS_PTR),Y
    BEQ @kwend                          ; keyword exhausted
    CMP MON_CMDBUF,Y
    BNE @no
    INY
    BRA @loop
@kwend:
    LDA MON_CMDBUF,Y                    ; must be a delimiter (else it's a longer word)
    BEQ @yes
    CMP #ASCII_SPACE
    BEQ @yes
@no:
    SEC
    RTS
@yes:
    CLC
    RTS

; ----------------------------------------------------------------
; _DOS_DO_HELP - list the built-in commands, two-column (verb / description),
; the same layout as the monitor's ? help. Each entry is "syntax"<TAB>"desc"; the
; kernel's K_PRINT_HELP_LINE renders the TAB as padding to a fixed column.
; ----------------------------------------------------------------
_DOS_DO_HELP:
    LDA #<MSG_DOS_HELP_HDR              ; header (its own CR/LF)
    STA MON_MSG_PTR_LO
    LDA #>MSG_DOS_HELP_HDR
    STA MON_MSG_PTR_HI
    JSR K_PRINT_MESSAGE
    LDX #$00
@loop:
    LDA DOS_HELP_TABLE,X               ; MON_MSG_PTR = table[X] (2 bytes per entry)
    STA MON_MSG_PTR_LO
    INX
    LDA DOS_HELP_TABLE,X
    STA MON_MSG_PTR_HI
    INX
    JSR K_PRINT_HELP_LINE              ; "verb"<TAB>"desc", TAB padded to the column
    JSR K_PRINT_NEWLINE
    CPX #(DOS_HELP_COUNT * 2)
    BNE @loop
    RTS

; ----------------------------------------------------------------
; _DOS_DO_MON - launch the monitor (returns to DOS_WARM via its Q command)
; ----------------------------------------------------------------
_DOS_DO_MON:
    JMP K_MON_ENTRY

; ----------------------------------------------------------------
; _DOS_DO_CAT - list files with sizes; optional wildcard pattern
; ----------------------------------------------------------------
; "CATALOG" / "CAT" lists everything; "CAT *.TXT" / "CAT AB?.*" filters by an
; 8.3 wildcard ('*' = rest of field, '?' = any one char). On entry Y = index of
; the delimiter after the verb (from _DOS_VERB_MATCH).
_DOS_DO_CAT:
    JSR _DOS_ARGSTART                   ; Y -> first arg char (carry set = no arg)
    BCC @havearg
    LDX #$00                            ; no pattern: match-all template ("???????????")
@allf:
    LDA #'?'
    STA DOS_NAME83,X
    INX
    CPX #11
    BNE @allf
    BRA @scan
@havearg:
    JSR _DOS_ARGPTR                     ; terminate the line, DOS_PTR = &arg
    JSR _DOS_PARSE_PATTERN83            ; -> DOS_NAME83 (template, '?' = wildcard)
@scan:
    JSR K_PRINT_NEWLINE
    JSR _FS_DIR_FIRST
    BCS @none
    LDA #<MSG_DOS_CATHDR                ; column header
    STA MON_MSG_PTR_LO
    LDA #>MSG_DOS_CATHDR
    STA MON_MSG_PTR_HI
    JSR K_PRINT_MESSAGE
@loop:
    LDA DOS_ENTRY+DIR_NAME              ; hide the '.'/'..' drawer pseudo-entries
    CMP #'.'
    BEQ @skip
    JSR _DOS_NAME_MATCH                 ; DOS_NAME83 template vs DOS_ENTRY
    BCS @skip
    JSR _DOS_PRINT_ENTRY
@skip:
    JSR _FS_DIR_NEXT
    BCC @loop
    RTS
@none:
    LDA #<MSG_DOS_NOFILES
    STA MON_MSG_PTR_LO
    LDA #>MSG_DOS_NOFILES
    STA MON_MSG_PTR_HI
    JMP K_PRINT_MESSAGE

; Print one DOS_ENTRY as "NAME.EXT" (left, padded to col 13) + decimal byte size
; (right-justified in an 8-col field) + newline. Y counts characters printed so
; far on the line (K_PRINT_CHAR preserves X and Y).
_DOS_PRINT_ENTRY:
    LDY #$00                            ; chars printed on this line
    LDX #$00                            ; base name (stop at first space)
@base:
    LDA DOS_ENTRY,X
    CMP #ASCII_SPACE
    BEQ @ext
    JSR K_PRINT_CHAR
    INY
    INX
    CPX #$08
    BNE @base
@ext:
    LDA DOS_ENTRY+8                     ; extension?
    CMP #ASCII_SPACE
    BEQ @pad
    LDA #'.'
    JSR K_PRINT_CHAR
    INY
    LDX #$08
@extloop:
    LDA DOS_ENTRY,X
    CMP #ASCII_SPACE
    BEQ @pad
    JSR K_PRINT_CHAR
    INY
    INX
    CPX #$0B
    BNE @extloop
@pad:
    CPY #13                             ; pad the name field to column 13
    BCS @size
    LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    INY
    BRA @pad
@size:
    LDA DOS_ENTRY+DIR_ATTR              ; a drawer prints "<D>" instead of a size
    AND #ATTR_DIRECTORY
    BNE @drawer
    LDA DOS_ENTRY+$1C                   ; copy the 32-bit size to DOS_W_SIZE
    STA DOS_W_SIZE
    LDA DOS_ENTRY+$1D
    STA DOS_W_SIZE+1
    LDA DOS_ENTRY+$1E
    STA DOS_W_SIZE+2
    LDA DOS_ENTRY+$1F
    STA DOS_W_SIZE+3
    LDA #<DOS_W_SIZE                    ; print size, right-justified in 8 columns
    LDX #>DOS_W_SIZE
    LDY #$08
    JSR K_PRINT_DEC
    JMP _DOS_PRINT_DATE_EOL
@drawer:
    LDX #5                              ; right-justify "<D>" in the 8-col field
@dsp:
    LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    DEX
    BNE @dsp
    LDA #<MSG_DOS_DIRTAG
    LDX #>MSG_DOS_DIRTAG
    JSR _DOS_PMSG
    JMP _DOS_PRINT_DATE_EOL

; ----------------------------------------------------------------
; _DOS_PRINT_DATE_EOL - print "  YYYY-MM-DD HH:MM" from the entry, then newline.
; ----------------------------------------------------------------
_DOS_PRINT_DATE_EOL:
    JSR _DOS_PRINT_FILEDATE
    JMP K_PRINT_NEWLINE

; _DOS_PRINT_FILEDATE - print the current DOS_ENTRY's last-write date/time as
; "  YYYY-MM-DD HH:MM", or "  (no date)" if the entry was never stamped.
_DOS_PRINT_FILEDATE:
    LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    LDA DOS_ENTRY+$18                   ; last-write date word (0 = unstamped)
    ORA DOS_ENTRY+$19
    BNE @have
    LDA #<MSG_DOS_NODATE
    LDX #>MSG_DOS_NODATE
    JMP _DOS_PMSG                       ; tail: "(no date)"
@have:
    LDA #'2'                            ; year "20YY" (files are always 20xx)
    JSR K_PRINT_CHAR
    LDA #'0'
    JSR K_PRINT_CHAR
    LDA DOS_ENTRY+$19                   ; yoff = date_hi >> 1; year-2000 = yoff-20
    LSR A
    SEC
    SBC #20
    JSR _DOS_PRINT_2DEC                 ; "YY"
    LDA #'-'
    JSR K_PRINT_CHAR
    LDA DOS_ENTRY+$18                   ; month = ((hi & 1) << 3) | (lo >> 5)
    LSR A
    LSR A
    LSR A
    LSR A
    LSR A
    STA DOS_TMP
    LDA DOS_ENTRY+$19
    AND #$01
    ASL A
    ASL A
    ASL A
    ORA DOS_TMP
    JSR _DOS_PRINT_2DEC                 ; "MM"
    LDA #'-'
    JSR K_PRINT_CHAR
    LDA DOS_ENTRY+$18                   ; day = date_lo & 0x1F
    AND #$1F
    JSR _DOS_PRINT_2DEC                 ; "DD"
    LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    LDA DOS_ENTRY+$17                   ; hour = time_hi >> 3
    LSR A
    LSR A
    LSR A
    JSR _DOS_PRINT_2DEC                 ; "HH"
    LDA #':'
    JSR K_PRINT_CHAR
    LDA DOS_ENTRY+$16                   ; min = ((time_hi & 7) << 3) | (time_lo >> 5)
    LSR A
    LSR A
    LSR A
    LSR A
    LSR A
    STA DOS_TMP
    LDA DOS_ENTRY+$17
    AND #$07
    ASL A
    ASL A
    ASL A
    ORA DOS_TMP
    JMP _DOS_PRINT_2DEC                 ; "MM" (tail)

; _DOS_PRINT_2DEC - print A (0-99) as two zero-padded decimal digits.
_DOS_PRINT_2DEC:
    LDX #$FF
@t:
    INX
    SEC
    SBC #10
    BCS @t
    CLC
    ADC #10                             ; restore the ones digit (over-subtracted)
    PHA
    TXA                                 ; tens
    CLC
    ADC #'0'
    JSR K_PRINT_CHAR
    PLA
    CLC
    ADC #'0'
    JMP K_PRINT_CHAR                    ; tail

; ----------------------------------------------------------------
; _DOS_DO_TYPE - print the contents of "TYPE NAME"
; ----------------------------------------------------------------
; On entry Y = index of the delimiter after the verb (from _DOS_VERB_MATCH).
_DOS_DO_TYPE:
@skip:
    LDA MON_CMDBUF,Y                    ; skip spaces to the filename
    CMP #ASCII_SPACE
    BNE @name
    INY
    CPY MON_CMDLEN
    BCC @skip
    JMP @noname                         ; nothing after TYPE
@name:
    JSR _DOS_ARGPTR                     ; terminate the line, A/X = &arg
    LDY #$00                            ; read mode
    JSR _FS_OPEN
    BCS @notfound
    JSR K_PRINT_NEWLINE
@rd:
    JSR _FS_GETB
    BCS @eof
    CMP #ASCII_CR                       ; ignore CR; newline on LF (handles LF/CRLF)
    BEQ @rd
    CMP #ASCII_LF
    BNE @putc
    JSR K_PRINT_NEWLINE
    BRA @rd
@putc:
    JSR K_PRINT_CHAR
    BRA @rd
@eof:
    JMP _FS_CLOSE                       ; tail (returns to _DOS_PROMPT)
@notfound:
@noname:
    JSR K_PRINT_NEWLINE
    LDA #<MSG_DOS_NOFILE
    STA MON_MSG_PTR_LO
    LDA #>MSG_DOS_NOFILE
    STA MON_MSG_PTR_HI
    JMP K_PRINT_MESSAGE

; ----------------------------------------------------------------
; _DOS_ARGSTART - skip spaces from Y to the first argument character
; ----------------------------------------------------------------
; In: Y = index just past the verb. Out: Y = first non-space char, carry clear;
; carry set if the line ended (no argument).
_DOS_ARGSTART:
@l:
    CPY MON_CMDLEN
    BCS @none
    LDA MON_CMDBUF,Y
    CMP #ASCII_SPACE
    BNE @ok
    INY
    BRA @l
@ok:
    CLC
    RTS
@none:
    SEC
    RTS

; _DOS_ARG_OR_USAGE - _DOS_ARGSTART, but a missing argument abandons the command.
; Every verb that requires an argument wrote the same 8-byte preamble:
;     JSR _DOS_ARGSTART / BCC :+ / JMP @usage / :
; (the JMP rather than a branch because @usage sits out of range at the end of the
; handler). On failure this discards its own return address and tail-jumps to the
; usage message, so _DOS_PERR_USAGE's RTS returns to the handler's caller -- exactly
; what "JMP @usage" did -- and the rest of the handler does not run.
_DOS_ARG_OR_USAGE:
    JSR _DOS_ARGSTART
    BCS @nousage
    RTS                                 ; Y -> first argument character
@nousage:
    PLA                                 ; drop this call's return address
    PLA
    JMP _DOS_PERR_USAGE

; Shared error printers (newline + message, tail-call print).
_DOS_PERR_USAGE:
    LDA #<MSG_DOS_USAGE
    LDX #>MSG_DOS_USAGE
    BRA _DOS_PERR
_DOS_PERR_NOFILE:
    LDA #<MSG_DOS_NOFILE
    LDX #>MSG_DOS_NOFILE
    BRA _DOS_PERR
_DOS_PERR_HOST:
    LDA #<MSG_DOS_HOSTERR
    LDX #>MSG_DOS_HOSTERR
    BRA _DOS_PERR
_DOS_PERR_WRITE:
    LDA #<MSG_DOS_WRITEERR
    LDX #>MSG_DOS_WRITEERR
_DOS_PERR:
    STA MON_MSG_PTR_LO
    STX MON_MSG_PTR_HI
    JSR K_PRINT_NEWLINE
    JMP K_PRINT_MESSAGE

; ----------------------------------------------------------------
; _DOS_DO_ERASE - ERASE NAME
; ----------------------------------------------------------------
_DOS_DO_ERASE:
    JSR _DOS_ARG_OR_USAGE
    LDX MON_CMDLEN                      ; null-terminate the name
    LDA #$00
    STA MON_CMDBUF,X
    TYA                                 ; A/X = &MON_CMDBUF[Y]
    LDX #>MON_CMDBUF
    JSR _FS_DELETE
    BCS @notfound
    LDA #<MSG_DOS_ERASED
    LDX #>MSG_DOS_ERASED
    JMP _DOS_PERR
@notfound:
    JMP _DOS_PERR_NOFILE

; ----------------------------------------------------------------
; _DOS_DO_RENAME - RENAME OLD,NEW
; ----------------------------------------------------------------
_DOS_DO_RENAME:
    JSR _DOS_ARG_OR_USAGE
    STY DOS_SH_NAMEIDX                  ; old name start
@findc:
    CPY MON_CMDLEN
    BCS @usage
    LDA MON_CMDBUF,Y
    CMP #','
    BEQ @gotc
    INY
    BRA @findc
@gotc:
    LDA #$00
    STA MON_CMDBUF,Y                    ; terminate old name
    INY                                 ; new name start
    TYA
    STA DOS_PTR2
    LDA #>MON_CMDBUF
    STA DOS_PTR2+1
    LDX MON_CMDLEN                      ; terminate new name
    LDA #$00
    STA MON_CMDBUF,X
    LDA DOS_SH_NAMEIDX                  ; old ptr in A/X
    LDX #>MON_CMDBUF
    JSR _FS_RENAME
    BCS @notfound
    LDA #<MSG_DOS_RENAMED
    LDX #>MSG_DOS_RENAMED
    JMP _DOS_PERR
@notfound:
    JMP _DOS_PERR_NOFILE
@usage:
    JMP _DOS_PERR_USAGE

; ----------------------------------------------------------------
; _DOS_DO_SAVE - SAVE NAME,SSSS-EEEE  (writes a 2-byte load-address header)
; ----------------------------------------------------------------
_DOS_DO_SAVE:
    JSR _DOS_ARG_OR_USAGE
    STY DOS_SH_NAMEIDX
@findc:
    CPY MON_CMDLEN
    BCC :+
    JMP @usage
:
    LDA MON_CMDBUF,Y
    CMP #','
    BEQ @gotc
    INY
    BRA @findc
@gotc:
    LDA #$00
    STA MON_CMDBUF,Y                    ; terminate the name at the comma
    INY                                 ; first hex char
    TYA
    TAX
    JSR K_PARSE_HEX                     ; start -> MON_CURRADDR, X += 4
    BCS @usage
    LDA MON_CURRADDR_LO
    STA MON_STARTADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_STARTADDR_HI
    LDA MON_CMDBUF,X                    ; expect '-'
    CMP #'-'
    BNE @usage
    INX
    JSR K_PARSE_HEX                     ; end -> MON_CURRADDR
    BCS @usage
    LDA MON_CURRADDR_LO
    STA MON_ENDADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_ENDADDR_HI
    LDA DOS_SH_NAMEIDX                  ; open NAME for writing
    LDX #>MON_CMDBUF
    LDY #$01
    JSR _FS_OPEN
    BCS @werr
    LDA MON_STARTADDR_LO               ; 2-byte load-address header
    JSR _FS_PUTB
    BCS @werr
    LDA MON_STARTADDR_HI
    JSR _FS_PUTB
    BCS @werr
    LDA MON_STARTADDR_LO               ; read cursor = start
    STA MON_CURRADDR_LO
    LDA MON_STARTADDR_HI
    STA MON_CURRADDR_HI
@wl:
    LDY #$00
    LDA (MON_CURRADDR_LO),Y
    JSR _FS_PUTB
    BCS @werr
    LDA MON_CURRADDR_LO
    CMP MON_ENDADDR_LO
    BNE @winc
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BEQ @wdone
@winc:
    INC MON_CURRADDR_LO
    BNE @wl
    INC MON_CURRADDR_HI
    BRA @wl
@wdone:
    JSR _FS_CLOSE
    BCS @werr
    LDA #<MSG_DOS_SAVED
    LDX #>MSG_DOS_SAVED
    JMP _DOS_PERR
@werr:
    JSR _FS_CLOSE                       ; a PUTB that failed mid-file leaves the
    JMP _DOS_PERR_WRITE                 ; chain allocated but the dir entry at
@usage:                                 ; cluster 0, so ERASE could never free it.
    JMP _DOS_PERR_USAGE                 ; (No-op if the close already happened.)

; ----------------------------------------------------------------
; _DOS_DO_LOAD - LOAD NAME[,AAAA]  (load addr from header unless overridden)
; ----------------------------------------------------------------
_DOS_DO_LOAD:
    JSR _DOS_ARG_OR_USAGE
    STY DOS_SH_NAMEIDX
    STZ DOS_SH_HASADDR
@findc:
    CPY MON_CMDLEN
    BCC :+
    JMP @noaddr
:
    LDA MON_CMDBUF,Y
    CMP #','
    BEQ @gotc
    INY
    BRA @findc
@gotc:
    LDA #$00
    STA MON_CMDBUF,Y                    ; terminate name at comma
    INY
    TYA
    TAX
    JSR K_PARSE_HEX                     ; override addr -> MON_CURRADDR
    BCS @usage
    LDA MON_CURRADDR_LO
    STA MON_STARTADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_STARTADDR_HI
    LDA #$01
    STA DOS_SH_HASADDR
    BRA @open
@noaddr:
    LDX MON_CMDLEN                      ; terminate name at end
    LDA #$00
    STA MON_CMDBUF,X
@open:
    LDA DOS_SH_NAMEIDX
    LDX #>MON_CMDBUF
    LDY #$00
    JSR _FS_OPEN
    BCS @notfound
    JSR _FS_GETB                        ; header low
    BCS @close
    STA MON_CURRADDR_LO
    JSR _FS_GETB                        ; header high
    BCS @close
    STA MON_CURRADDR_HI
    LDA DOS_SH_HASADDR                  ; override?
    BEQ @body
    LDA MON_STARTADDR_LO
    STA MON_CURRADDR_LO
    LDA MON_STARTADDR_HI
    STA MON_CURRADDR_HI
@body:
    JSR _FS_GETB
    BCS @close
    LDY #$00
    STA (MON_CURRADDR_LO),Y
    INC MON_CURRADDR_LO
    BNE @body
    INC MON_CURRADDR_HI
    BRA @body
@close:
    JSR _FS_CLOSE
    LDA #<MSG_DOS_LOADED
    LDX #>MSG_DOS_LOADED
    JMP _DOS_PERR
@notfound:
    JMP _DOS_PERR_NOFILE
@usage:
    JMP _DOS_PERR_USAGE

; ----------------------------------------------------------------
; _DOS_DO_IMPORT - IMPORT NAME : host file (picker) -> FAT16 file NAME
; ----------------------------------------------------------------
_DOS_DO_IMPORT:
    JSR _DOS_ARG_OR_USAGE
    STY DOS_SH_NAMEIDX
    LDX MON_CMDLEN
    LDA #$00
    STA MON_CMDBUF,X                    ; null-terminate the name
    LDA #FIO_OPEN_RD                    ; open the host file (open dialog)
    STA FIO_COMMAND
@wait:
    LDA FIO_STATUS
    CMP #FIO_INPROG
    BEQ @wait
    CMP #FIO_ERROR
    BNE :+
    JMP @hosterr
:
    LDA DOS_SH_NAMEIDX                  ; open the FAT16 file for writing
    LDX #>MON_CMDBUF
    LDY #$01
    JSR _FS_OPEN
    BCS @abort
@copy:
    LDA FIO_STATUS
    CMP #FIO_EOF
    BEQ @done
    LDA FIO_DATA                        ; host byte -> FAT16 file
    JSR _FS_PUTB
    BCS @abort
    BRA @copy
@done:
    LDA #FIO_CLOSE
    STA FIO_COMMAND
    JSR _FS_CLOSE
    BCS @abort2
    LDA #<MSG_DOS_IMPORTED
    LDX #>MSG_DOS_IMPORTED
    JMP _DOS_PERR
@abort:                                 ; FS error mid-transfer: close host stream
    LDA #FIO_CLOSE
    STA FIO_COMMAND
@abort2:
    JSR _FS_CLOSE                       ; finalize, so a part-written file is not
    JMP _DOS_PERR_WRITE                 ; an unreclaimable orphan chain
@hosterr:
    JMP _DOS_PERR_HOST

; ----------------------------------------------------------------
; _DOS_DO_EXPORT - EXPORT NAME : FAT16 file NAME -> host file (save dialog)
; ----------------------------------------------------------------
_DOS_DO_EXPORT:
    JSR _DOS_ARG_OR_USAGE
    LDX MON_CMDLEN
    LDA #$00
    STA MON_CMDBUF,X
    TYA                                 ; open the FAT16 file for reading
    LDX #>MON_CMDBUF
    LDY #$00
    JSR _FS_OPEN
    BCS @notfound
    LDA #FIO_OPEN_WR                    ; open the host file (save dialog)
    STA FIO_COMMAND
@wait:
    LDA FIO_STATUS
    CMP #FIO_INPROG
    BEQ @wait
    CMP #FIO_ERROR
    BEQ @hosterr
@copy:
    JSR _FS_GETB
    BCS @done
    STA FIO_DATA                        ; FAT16 byte -> host stream
    BRA @copy
@done:
    LDA #FIO_CLOSE
    STA FIO_COMMAND
    JSR _FS_CLOSE
    LDA #<MSG_DOS_EXPORTED
    LDX #>MSG_DOS_EXPORTED
    JMP _DOS_PERR
@hosterr:
    JSR _FS_CLOSE                       ; close the FAT16 file we opened
    JMP _DOS_PERR_HOST
@notfound:
    JMP _DOS_PERR_NOFILE

; ----------------------------------------------------------------
; _DOS_DO_VER - print the OS version, read from the DOS_VERSION bytes
; ----------------------------------------------------------------
; "MFC/OS <major>.<minor>", with major/minor taken from DOS_VERSION (the single
; source of truth) and formatted via the shared decimal printer K_PRINT_DEC.
_DOS_DO_VER:
    JSR K_PRINT_NEWLINE
    LDA #<MSG_DOS_VER                   ; "MFC/OS "
    LDX #>MSG_DOS_VER
    JSR _DOS_PMSG
    LDA DOS_VERSION                     ; major
    JSR _DOS_PRINT_BYTE_DEC
    LDA #'.'
    JSR K_PRINT_CHAR
    LDA DOS_VERSION+1                   ; minor
    JSR _DOS_PRINT_BYTE_DEC
    JMP K_PRINT_NEWLINE

; _DOS_PRINT_BYTE_DEC - print the byte in A as decimal (via K_PRINT_DEC).
_DOS_PRINT_BYTE_DEC:
    STA DOS_W_SIZE                      ; zero-extend the byte to 32 bits
    STZ DOS_W_SIZE+1
    STZ DOS_W_SIZE+2
    STZ DOS_W_SIZE+3
    LDA #<DOS_W_SIZE
    LDX #>DOS_W_SIZE
    LDY #$00                            ; no field padding
    JMP K_PRINT_DEC                     ; tail

; ----------------------------------------------------------------
; _DOS_DO_DATE - print the current date and time from the RTC ($FE55-$FE5C).
; ----------------------------------------------------------------
; "Ddd 20YY-MM-DD HH:MM:SS" -- weekday abbreviation + date + 24-hour time. Writing
; RTC_LATCH snapshots the host time so the multi-register read is consistent.
_DOS_DO_DATE:
    STA RTC_LATCH                       ; snapshot host time (written value ignored)
    JSR K_PRINT_NEWLINE
    ; weekday abbreviation: index DOW_NAMES at DOW*3
    LDA RTC_DOW
    CMP #$07
    BCC @dow                            ; guard an unexpected value
    LDA #$00
@dow:
    ASL A                               ; DOW*2 (DOW<=6, no carry)
    CLC
    ADC RTC_DOW                         ; + DOW = DOW*3
    TAX
    LDA DOW_NAMES,X
    JSR K_PRINT_CHAR
    LDA DOW_NAMES+1,X
    JSR K_PRINT_CHAR
    LDA DOW_NAMES+2,X
    JSR K_PRINT_CHAR
    LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    ; 20YY-MM-DD
    LDA #'2'
    JSR K_PRINT_CHAR
    LDA #'0'
    JSR K_PRINT_CHAR
    LDA RTC_YEAR
    JSR _DOS_PRINT_BCD
    LDA #'-'
    JSR K_PRINT_CHAR
    LDA RTC_MONTH
    JSR _DOS_PRINT_BCD
    LDA #'-'
    JSR K_PRINT_CHAR
    LDA RTC_DAY
    JSR _DOS_PRINT_BCD
    LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    ; HH:MM:SS
    LDA RTC_HOUR
    JSR _DOS_PRINT_BCD
    LDA #':'
    JSR K_PRINT_CHAR
    LDA RTC_MIN
    JSR _DOS_PRINT_BCD
    LDA #':'
    JSR K_PRINT_CHAR
    LDA RTC_SEC
    JSR _DOS_PRINT_BCD
    JMP K_PRINT_NEWLINE

; _DOS_PRINT_BCD - print the packed-BCD byte in A as two decimal digits.
_DOS_PRINT_BCD:
    PHA
    LSR A
    LSR A
    LSR A
    LSR A
    CLC
    ADC #'0'
    JSR K_PRINT_CHAR
    PLA
    AND #$0F
    CLC
    ADC #'0'
    JMP K_PRINT_CHAR                     ; tail

DOW_NAMES: .BYTE "SUNMONTUEWEDTHUFRISAT"

; ----------------------------------------------------------------
; _DOS_DO_MEM - print the memory map
; ----------------------------------------------------------------
_DOS_DO_MEM:
    LDA #<MSG_DOS_MEM
    LDX #>MSG_DOS_MEM
    JMP _DOS_PERR

; ----------------------------------------------------------------
; _DOS_DO_NEWDRAWER - NEWDRAWER name: create a drawer in root
; ----------------------------------------------------------------
_DOS_DO_NEWDRAWER:
    LDA DOS_CWD_CLUS                    ; one level: only from root
    ORA DOS_CWD_CLUS+1
    BEQ :+
    JMP @notroot
:
    JSR _DOS_ARG_OR_USAGE
    JSR _DOS_ARG_TO_NAME83              ; null-terminate at EOL, Y -> DOS_NAME83
    JSR _FS_ENSURE_MOUNT
    BCS @diskerr
    STZ DOS_TGT_CLUS                    ; reject if the name already exists in root
    STZ DOS_TGT_CLUS+1
    JSR _DOS_DIR_FIND_EXISTING
    BCC @exists
    JSR _DOS_ALLOC_CLUSTER              ; allocate the drawer's first cluster
    BCS @diskerr
    LDA DOS_ARG_CLUS
    STA DOS_NEW_CLUS
    STA DOS_DIR_CLUS
    LDA DOS_ARG_CLUS+1
    STA DOS_NEW_CLUS+1
    STA DOS_DIR_CLUS+1
    JSR _DOS_DIRCLUS_TO_LBA
    JSR _DOS_INIT_DRAWER_CLUSTER        ; write '.'/'..' + zero-fill
    BCS @freeerr
    STZ DOS_TGT_CLUS                    ; create the root entry (attr $10)
    STZ DOS_TGT_CLUS+1
    JSR _DOS_DIR_FIND_FOR_WRITE
    BCS @freeerr                        ; root full -> free the orphan cluster
    LDA DOS_NEW_CLUS
    STA DOS_W_FIRST_CLUS
    LDA DOS_NEW_CLUS+1
    STA DOS_W_FIRST_CLUS+1
    STZ DOS_W_SIZE
    STZ DOS_W_SIZE+1
    STZ DOS_W_SIZE+2
    STZ DOS_W_SIZE+3
    LDA #ATTR_DIRECTORY
    STA DOS_W_ATTR
    JSR _DOS_DIR_WRITE_ENTRY
    BCS @freeerr
    LDA #<MSG_DOS_DRAWER_NEW
    LDX #>MSG_DOS_DRAWER_NEW
    JMP _DOS_PERR
@freeerr:
    LDA DOS_NEW_CLUS                    ; free the orphaned cluster
    STA DOS_ARG_CLUS
    LDA DOS_NEW_CLUS+1
    STA DOS_ARG_CLUS+1
    JSR _DOS_FREE_CHAIN
@diskerr:
    JMP _DOS_PERR_WRITE
@exists:
    LDA #<MSG_DOS_EXISTS
    LDX #>MSG_DOS_EXISTS
    JMP _DOS_PERR
@notroot:
    LDA #<MSG_DOS_NOTROOT
    LDX #>MSG_DOS_NOTROOT
    JMP _DOS_PERR

; ----------------------------------------------------------------
; _DOS_DO_OPEN - OPEN name: enter a drawer (resolves in root)
; ----------------------------------------------------------------
_DOS_DO_OPEN:
    JSR _DOS_ARG_OR_USAGE
    JSR _DOS_ARG_TO_NAME83
    STZ DOS_TGT_CLUS                    ; find the drawer in root
    STZ DOS_TGT_CLUS+1
    JSR _DOS_DIR_FIND_EXISTING
    BCS @nodrawer
    LDA DOS_ENTRY+DIR_ATTR
    AND #ATTR_DIRECTORY
    BEQ @nodrawer                       ; exists but is a file
    LDA DOS_ENTRY+DIR_CLUSTER_LO        ; switch the current directory
    STA DOS_CWD_CLUS
    LDA DOS_ENTRY+DIR_CLUSTER_LO+1
    STA DOS_CWD_CLUS+1
    JSR _DOS_SET_CWD_NAME
    JMP K_PRINT_NEWLINE                 ; fresh prompt shows the drawer
@nodrawer:
    LDA #<MSG_DOS_NODRAWER
    LDX #>MSG_DOS_NODRAWER
    JMP _DOS_PERR

; ----------------------------------------------------------------
; _DOS_DO_CLOSE - CLOSE: return to the root directory
; ----------------------------------------------------------------
_DOS_DO_CLOSE:
    STZ DOS_CWD_CLUS
    STZ DOS_CWD_CLUS+1
    STZ DOS_CWD_NAME
    JMP K_PRINT_NEWLINE

; ----------------------------------------------------------------
; _DOS_DO_DROPDRAWER - DROPDRAWER name: remove an empty drawer
; ----------------------------------------------------------------
_DOS_DO_DROPDRAWER:
    LDA DOS_CWD_CLUS                    ; one level: only from root
    ORA DOS_CWD_CLUS+1
    BNE @notroot
    JSR _DOS_ARG_OR_USAGE
    JSR _DOS_ARG_TO_NAME83
    STZ DOS_TGT_CLUS
    STZ DOS_TGT_CLUS+1
    JSR _DOS_DIR_FIND_EXISTING          ; find the drawer entry in root
    BCS @nodrawer
    LDA DOS_ENTRY+DIR_ATTR
    AND #ATTR_DIRECTORY
    BEQ @nodrawer
    LDA DOS_ENTRY+DIR_CLUSTER_LO        ; stash the drawer's first cluster
    STA DOS_NEW_CLUS
    STA DOS_TGT_CLUS
    LDA DOS_ENTRY+DIR_CLUSTER_LO+1
    STA DOS_NEW_CLUS+1
    STA DOS_TGT_CLUS+1
    JSR _DOS_DRAWER_EMPTY               ; only '.'/'..' inside?
    BCS @notempty
    STZ DOS_TGT_CLUS                    ; re-find the root entry (empty-scan moved state)
    STZ DOS_TGT_CLUS+1
    JSR _DOS_DIR_FIND_EXISTING
    BCS @nodrawer
    LDA DOS_NEW_CLUS                    ; free the drawer's cluster chain
    STA DOS_ARG_CLUS
    LDA DOS_NEW_CLUS+1
    STA DOS_ARG_CLUS+1
    JSR _DOS_FREE_CHAIN
    JSR _DOS_MARK_DELETED               ; delete the root entry
    LDA #<MSG_DOS_DRAWER_DROP
    LDX #>MSG_DOS_DRAWER_DROP
    JMP _DOS_PERR
@notempty:
    LDA #<MSG_DOS_NOTEMPTY
    LDX #>MSG_DOS_NOTEMPTY
    JMP _DOS_PERR
@nodrawer:
    LDA #<MSG_DOS_NODRAWER
    LDX #>MSG_DOS_NODRAWER
    JMP _DOS_PERR
@notroot:
    LDA #<MSG_DOS_NOTROOT
    LDX #>MSG_DOS_NOTROOT
    JMP _DOS_PERR

; _DOS_ARG_TO_NAME83 - In: Y = arg start index in MON_CMDBUF. Null-terminates the
; line, points DOS_PTR at the arg, parses it to DOS_NAME83. Preserves nothing.
_DOS_ARG_TO_NAME83:
    JSR _DOS_ARGPTR
    JMP _DOS_PARSE_NAME83               ; tail

; ----------------------------------------------------------------
; _DOS_ARGPTR - null-terminate the command line and point at its argument
; ----------------------------------------------------------------
; Terminates MON_CMDBUF at MON_CMDLEN and builds a pointer to MON_CMDBUF[Y], the
; prologue every verb needs before parsing a name. Returned two ways so both
; conventions in the shell can use it: DOS_PTR (for the parsers) and A/X (for the
; ABI calls that take a pointer in A/X). Clobbers A and X.
_DOS_ARGPTR:
    LDX MON_CMDLEN
    LDA #$00
    STA MON_CMDBUF,X
    TYA
    CLC
    ADC #<MON_CMDBUF
    STA DOS_PTR
    LDA #>MON_CMDBUF
    ADC #$00
    STA DOS_PTR+1
    LDX DOS_PTR+1
    LDA DOS_PTR
    RTS

; ----------------------------------------------------------------
; _DOS_DO_FREE - DISKFREE: report free disk space in bytes and KB
; ----------------------------------------------------------------
; Scans the FAT a sector at a time (256 entries each), counting zero (free)
; entries for clusters 2..DOS_MAX_CLUS. This reads each FAT sector once instead
; of re-reading per cluster, so it is far faster than a per-cluster walk.
; State: DOS_FREE_CNT = free clusters, DOS_TMP = current cluster number,
; DOS_TMP2 = current FAT sector LBA, DOS_SH_NAMEIDX = entry-low scratch.
_DOS_DO_FREE:
    JSR _FS_ENSURE_MOUNT
    BCC :+
    JMP @err
:
    STZ DOS_FREE_CNT
    STZ DOS_FREE_CNT+1
    STZ DOS_TMP                         ; cluster number = 0
    STZ DOS_TMP+1
    LDA DOS_FAT_START                   ; first FAT sector
    STA DOS_TMP2
    LDA DOS_FAT_START+1
    STA DOS_TMP2+1
@sector:
    LDA DOS_TMP2
    LDX DOS_TMP2+1
    JSR _DOS_READ_SECTOR
    BCS @done                           ; read error -> stop
    LDY #$00                            ; 256 entries per 512-byte FAT sector
@entry:
    LDA DOS_TMP+1                       ; cluster number > DOS_MAX_CLUS -> done
    CMP DOS_MAX_CLUS+1
    BCC @inrange
    BNE @done
    LDA DOS_TMP
    CMP DOS_MAX_CLUS
    BEQ @inrange
    BCS @done
@inrange:
    LDA BLK_DATA                        ; entry low byte
    STA DOS_SH_NAMEIDX
    LDA BLK_DATA                        ; entry high byte
    ORA DOS_SH_NAMEIDX                  ; entry == 0 (free)?
    BNE @used
    LDA DOS_TMP+1                       ; only count real data clusters (>= 2)
    BNE @free
    LDA DOS_TMP
    CMP #$02
    BCC @used
@free:
    INC DOS_FREE_CNT
    BNE @used
    INC DOS_FREE_CNT+1
@used:
    INC DOS_TMP                         ; cluster number++
    BNE @nc
    INC DOS_TMP+1
@nc:
    INY                                 ; next entry in this sector
    BNE @entry
    INC DOS_TMP2                         ; next FAT sector
    BNE @sector
    INC DOS_TMP2+1
    BRA @sector
@done:
    ; free_sectors (DOS_TMP) = DOS_FREE_CNT * SectorsPerCluster
    STZ DOS_TMP
    STZ DOS_TMP+1
    LDX DOS_SEC_PER_CLUS
    BEQ @kb
@mul:
    CLC
    LDA DOS_TMP
    ADC DOS_FREE_CNT
    STA DOS_TMP
    LDA DOS_TMP+1
    ADC DOS_FREE_CNT+1
    STA DOS_TMP+1
    DEX
    BNE @mul
@kb:
    LDA DOS_TMP+1                       ; KB (DOS_TMP2) = free_sectors / 2 (non-destructive)
    LSR
    STA DOS_TMP2+1
    LDA DOS_TMP
    ROR
    STA DOS_TMP2
    ; free_bytes (DOS_W_SIZE, 32-bit) = free_sectors << 9
    LDA DOS_TMP
    STA DOS_W_SIZE
    LDA DOS_TMP+1
    STA DOS_W_SIZE+1
    STZ DOS_W_SIZE+2
    STZ DOS_W_SIZE+3
    LDX #$09
@shl:
    ASL DOS_W_SIZE
    ROL DOS_W_SIZE+1
    ROL DOS_W_SIZE+2
    ROL DOS_W_SIZE+3
    DEX
    BNE @shl
    JSR K_PRINT_NEWLINE
    LDA #<MSG_DOS_FREE1                 ; "DISK FREE: "
    LDX #>MSG_DOS_FREE1
    JSR _DOS_PMSG
    LDA #<DOS_W_SIZE                    ; bytes (decimal, no padding)
    LDX #>DOS_W_SIZE
    LDY #$00
    JSR K_PRINT_DEC
    LDA #<MSG_DOS_FREE2                 ; " BYTES ("
    LDX #>MSG_DOS_FREE2
    JSR _DOS_PMSG
    LDA DOS_TMP2                        ; KB -> DOS_W_SIZE (32-bit)
    STA DOS_W_SIZE
    LDA DOS_TMP2+1
    STA DOS_W_SIZE+1
    STZ DOS_W_SIZE+2
    STZ DOS_W_SIZE+3
    LDA #<DOS_W_SIZE
    LDX #>DOS_W_SIZE
    LDY #$00
    JSR K_PRINT_DEC
    LDA #<MSG_DOS_FREE3                 ; " KB)" + newline
    LDX #>MSG_DOS_FREE3
    JMP _DOS_PMSG
@err:
    JMP _DOS_PERR_NOFILE                ; not mounted

; _DOS_PMSG - set MON_MSG_PTR from A/X and print (no leading newline).
_DOS_PMSG:
    STA MON_MSG_PTR_LO
    STX MON_MSG_PTR_HI
    JMP K_PRINT_MESSAGE

; ----------------------------------------------------------------
; _DOS_DO_COPY - COPY SRC,DST : duplicate a file via a RAM buffer
; ----------------------------------------------------------------
; The filesystem allows only one open file at a time (reads and writes both
; stream through the block device's single sector buffer), so COPY reads SRC
; fully into user RAM ($0800..$8FFF), then writes it to DST. Files larger than
; the ~34KB buffer report "FILE TOO BIG". On entry Y = delimiter after the verb.
_DOS_DO_COPY:
    STZ DOS_SH_HASADDR                  ; 0 = copy (source kept)
    BRA _DOS_COPY_COMMON
; _DOS_DO_MOVE - MOVE SRC,DST : copy then delete the source (shares COPY's body)
_DOS_DO_MOVE:
    LDA #$01                            ; 1 = move (delete source after copying)
    STA DOS_SH_HASADDR
_DOS_COPY_COMMON:
    JSR _DOS_ARG_OR_USAGE
    STY DOS_SH_NAMEIDX                  ; SRC name start
@findc:
    CPY MON_CMDLEN
    BCC :+
    JMP @usage
:
    LDA MON_CMDBUF,Y
    CMP #','
    BEQ @gotc
    INY
    BRA @findc
@gotc:
    LDA #$00
    STA MON_CMDBUF,Y                    ; terminate SRC at the comma
    INY
@dskip:
    CPY MON_CMDLEN                      ; skip spaces before DST
    BCC :+
    JMP @usage
:
    LDA MON_CMDBUF,Y
    CMP #ASCII_SPACE
    BNE @dgot
    INY
    BRA @dskip
@dgot:
    STY DOS_SH_NAMEIDX2                 ; DST name start
    LDX MON_CMDLEN                      ; terminate DST at end-of-line
    LDA #$00
    STA MON_CMDBUF,X
    LDA DOS_SH_NAMEIDX                  ; open SRC for reading
    LDX #>MON_CMDBUF
    LDY #$00
    JSR _FS_OPEN
    BCC :+
    JMP @notfound
:
    LDA #$00                            ; buffer cursor = $0800
    STA MON_CURRADDR_LO
    LDA #$08
    STA MON_CURRADDR_HI
@rl:
    JSR _FS_GETB
    BCS @rdone
    PHA                                 ; save the byte
    LDA MON_CURRADDR_HI
    CMP #$90                            ; reached $9000 -> buffer full
    BCS @toobig
    PLA
    LDY #$00
    STA (MON_CURRADDR_LO),Y
    INC MON_CURRADDR_LO
    BNE @rl
    INC MON_CURRADDR_HI
    BRA @rl
@rdone:
    JSR _FS_CLOSE
    LDA MON_CURRADDR_LO                 ; remember where the data ends
    STA MON_ENDADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_ENDADDR_HI
    LDA DOS_SH_NAMEIDX2                 ; open DST for writing
    LDX #>MON_CMDBUF
    LDY #$01
    JSR _FS_OPEN
    BCS @werr
    LDA #$00                            ; replay the buffer from $0800
    STA MON_CURRADDR_LO
    LDA #$08
    STA MON_CURRADDR_HI
@wl:
    LDA MON_CURRADDR_LO                 ; cursor == end?
    CMP MON_ENDADDR_LO
    BNE @wput
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BEQ @wdone
@wput:
    LDY #$00
    LDA (MON_CURRADDR_LO),Y
    JSR _FS_PUTB
    BCS @werr
    INC MON_CURRADDR_LO
    BNE @wl
    INC MON_CURRADDR_HI
    BRA @wl
@wdone:
    JSR _FS_CLOSE
    BCS @werr
    LDA DOS_SH_HASADDR                  ; move? delete the source
    BEQ @copied
    JSR _DOS_STR_EQ                     ; ...unless SRC and DST name the same thing
    BCS @moved
    LDA DOS_SH_NAMEIDX
    LDX #>MON_CMDBUF
    JSR _FS_DELETE
@moved:
    LDA #<MSG_DOS_MOVED
    LDX #>MSG_DOS_MOVED
    JMP _DOS_PERR
@copied:
    LDA #<MSG_DOS_COPIED
    LDX #>MSG_DOS_COPIED
    JMP _DOS_PERR
@toobig:
    PLA                                 ; balance the saved byte
    JSR _FS_CLOSE
    LDA #<MSG_DOS_TOOBIG
    LDX #>MSG_DOS_TOOBIG
    JMP _DOS_PERR
@notfound:
    JMP _DOS_PERR_NOFILE
@werr:
    JSR _FS_CLOSE                       ; see SAVE's @werr: never leave an orphan
    JMP _DOS_PERR_WRITE                 ; chain behind on a mid-copy failure
@usage:
    JMP _DOS_PERR_USAGE

; _DOS_STR_EQ - compare the two null-terminated names at MON_CMDBUF[DOS_SH_NAMEIDX]
; and MON_CMDBUF[DOS_SH_NAMEIDX2]. Carry set if identical (guards MOVE A,A from
; deleting its own target). Clobbers A/X/Y.
_DOS_STR_EQ:
    LDX DOS_SH_NAMEIDX
    LDY DOS_SH_NAMEIDX2
@l:
    LDA MON_CMDBUF,X
    CMP MON_CMDBUF,Y
    BNE @ne
    CMP #$00                            ; matched through the terminator -> equal
    BEQ @eq
    INX
    INY
    BRA @l
@eq:
    SEC
    RTS
@ne:
    CLC
    RTS

; MORE is dispatched to _DOS_DO_TYPE: the kernel now pages all output (PAGE_ADVANCE),
; so "TYPE" and "MORE" behave identically. The old _DOS_DO_MORE / _DOS_PAGE_PAUSE
; pager was removed in favour of that single shared pager.

; ----------------------------------------------------------------
; _DOS_PARSE_PATTERN83 - (DOS_PTR) wildcard "NAME.EXT" -> DOS_NAME83 template
; ----------------------------------------------------------------
; Like _DOS_PARSE_NAME83, but '*' fills the rest of the current field (base or
; extension) with '?' so _DOS_NAME_MATCH treats those positions as wildcards.
_DOS_PARSE_PATTERN83:
    LDX #$00
@fill:
    LDA #' '                            ; space-fill (literal-match by default)
    STA DOS_NAME83,X
    INX
    CPX #11
    BNE @fill
    LDY #$00                            ; source index
    LDX #$00                            ; dest index (base 0..7)
@base:
    LDA (DOS_PTR),Y
    BEQ @done
    CMP #'.'
    BEQ @dot
    CMP #'*'
    BEQ @starb
    JSR _DOS_UPCASE
    CPX #8
    BCS @nextb
    STA DOS_NAME83,X
    INX
@nextb:
    INY
    BRA @base
@starb:
    CPX #8                              ; fill the rest of the base with '?'
    BCS @starbd
    LDA #'?'
    STA DOS_NAME83,X
    INX
    BRA @starb
@starbd:
    INY                                 ; consume '*'
    BRA @base
@dot:
    INY
    LDX #8                              ; dest = extension (8..10)
@ext:
    LDA (DOS_PTR),Y
    BEQ @done
    CMP #'*'
    BEQ @stare
    JSR _DOS_UPCASE
    CPX #11
    BCS @nexte
    STA DOS_NAME83,X
    INX
@nexte:
    INY
    BRA @ext
@stare:
    CPX #11                             ; fill the rest of the extension with '?'
    BCS @stared
    LDA #'?'
    STA DOS_NAME83,X
    INX
    BRA @stare
@stared:
    INY
    BRA @ext
@done:
    RTS

; _DOS_NAME_MATCH - compare the DOS_NAME83 template against DOS_ENTRY's 8.3 name.
; '?' in the template matches any character. Out: carry clear = match.
_DOS_NAME_MATCH:
    LDY #$00
@l:
    LDA DOS_NAME83,Y
    CMP #'?'
    BEQ @skip
    CMP DOS_ENTRY,Y
    BNE @no
@skip:
    INY
    CPY #11
    BNE @l
    CLC
    RTS
@no:
    SEC
    RTS

; ----------------------------------------------------------------
; ----------------------------------------------------------------
; _DOS_CAPTURE_ARG - copy the command tail (the argument after the program name)
; into DOS_ARGBUF, null-terminated; empty string if there is no argument. Called
; at launch time, before the .PRG-default append can mangle MON_CMDBUF.
; In: Y = index in MON_CMDBUF of the program name's NUL terminator.
; ----------------------------------------------------------------
_DOS_CAPTURE_ARG:
    LDX #$00                            ; DOS_ARGBUF write index
@skip:
    INY                                 ; past the terminator, then skip leading spaces
    CPY MON_CMDLEN
    BCS @empty
    LDA MON_CMDBUF,Y
    CMP #ASCII_SPACE
    BEQ @skip
@copy:
    STA DOS_ARGBUF,X
    INX
    CPX #DOS_ARGBUF_MAX-1               ; leave room for the terminator
    BCS @done
    INY
    CPY MON_CMDLEN
    BCS @done
    LDA MON_CMDBUF,Y
    BNE @copy
@done:
    LDA #$00
    STA DOS_ARGBUF,X
    RTS
@empty:
    STZ DOS_ARGBUF
    RTS

; ----------------------------------------------------------------
; _DOS_RUN_FILE - load and run a disk program (.PRG) by name
; ----------------------------------------------------------------
; In: A/X = pointer to a null-terminated file name. The file begins with a
; 2-byte little-endian load address (= the entry point), then the body. Loads
; the body there and runs it as a subroutine: a clean stack is set up with a
; DOS_WARM return pushed, so the program's RTS returns to the ] prompt. Does NOT
; return on success; on a missing/short file, carry set + RTS.
; Uses DOS_PTR (load cursor) and DOS_PTR2 (entry, also the indirect-JMP vector).
_DOS_RUN_FILE:
    LDY #$00                            ; read mode
    JSR _FS_OPEN
    BCS @nf
    JSR _FS_GETB                        ; load-address header low (= entry low)
    BCS @badclose
    STA DOS_PTR2
    STA DOS_PTR
    JSR _FS_GETB                        ; load-address header high
    BCS @badclose
    STA DOS_PTR2+1
    STA DOS_PTR+1
@body:
    JSR _FS_GETB                        ; stream the body to (load cursor)
    BCS @done
    LDY #$00
    STA (DOS_PTR),Y
    INC DOS_PTR
    BNE @body
    INC DOS_PTR+1
    BRA @body
@done:
    JSR _FS_CLOSE
    LDX #$FF                            ; clean stack for the program
    TXS
    LDA #>(DOS_WARM-1)                  ; push DOS_WARM-1: the program's RTS lands
    PHA                                 ;   on DOS_WARM (RTS adds 1)
    LDA #<(DOS_WARM-1)
    PHA
    JMP (DOS_PTR2)                      ; run it at the header's load address
@badclose:
    JSR _FS_CLOSE
@nf:
    SEC
    RTS

; ----------------------------------------------------------------
; DOS shell strings
; ----------------------------------------------------------------
MSG_BOX_OPER:    .BYTE "MFC 6502  OPERATIONAL", 0   ; 21 chars, centred in the box
MSG_BOX_FREE:    .BYTE "   34816 BYTES FREE", 0      ; user RAM $0800-$8FFF
MSG_DOS_HELP_HDR: .BYTE "MFC/OS COMMANDS", $0D, $0A, 0

; Two-column HELP entries: "syntax"<TAB>"description", 0. K_PRINT_HELP_LINE
; renders the TAB ($09) as padding to a fixed description column. Keep each
; syntax under that column (22) so the descriptions line up.
DOS_HELP_TABLE:
    .WORD DH_CAT, DH_TYPE, DH_MORE, DH_LOAD, DH_SAVE, DH_COPY, DH_MOVE
    .WORD DH_REN, DH_ERASE, DH_IMPORT, DH_EXPORT, DH_NEWD, DH_OPEN, DH_CLOSE
    .WORD DH_DROPD, DH_FREE, DH_MEMMAP, DH_VER, DH_DATE, DH_CLS, DH_MON, DH_HELP
DOS_HELP_COUNT = (* - DOS_HELP_TABLE) / 2

DH_CAT:    .BYTE "CATALOG [pat]", $09, "list files (CAT)", 0
DH_TYPE:   .BYTE "TYPE name", $09, "show a text file", 0
DH_MORE:   .BYTE "MORE name", $09, "show a file (= TYPE)", 0
DH_LOAD:   .BYTE "LOAD name[,addr]", $09, "load a file to memory", 0
DH_SAVE:   .BYTE "SAVE name,s-e", $09, "save memory to a file", 0
DH_COPY:   .BYTE "COPY src,dst", $09, "copy a file", 0
DH_MOVE:   .BYTE "MOVE src,dst", $09, "move / rename a file", 0
DH_REN:    .BYTE "RENAME old,new", $09, "rename a file", 0
DH_ERASE:  .BYTE "ERASE name", $09, "delete a file", 0
DH_IMPORT: .BYTE "IMPORT name", $09, "host file -> disk", 0
DH_EXPORT: .BYTE "EXPORT name", $09, "disk file -> host", 0
DH_NEWD:   .BYTE "NEWDRAWER name", $09, "make a drawer", 0
DH_OPEN:   .BYTE "OPEN name", $09, "enter a drawer", 0
DH_CLOSE:  .BYTE "CLOSE", $09, "leave the drawer", 0
DH_DROPD:  .BYTE "DROPDRAWER name", $09, "remove an empty drawer", 0
DH_FREE:   .BYTE "DISKFREE", $09, "show free space", 0
DH_MEMMAP: .BYTE "MEMMAP", $09, "show the memory map", 0
DH_VER:    .BYTE "VERSION", $09, "show the OS version", 0
DH_DATE:   .BYTE "DATE", $09, "show the date and time", 0
DH_CLS:    .BYTE "CLS", $09, "clear the screen", 0
DH_MON:    .BYTE "MON", $09, "enter the monitor", 0
DH_HELP:   .BYTE "HELP", $09, "this list", 0
MSG_DOS_BADCMD:  .BYTE "COMMAND NOT FOUND", $0D, $0A, 0
DOS_EXT_PRG:     .BYTE ".PRG", 0        ; default extension for launch-by-name
MSG_DOS_NOFILES: .BYTE "NO FILES", $0D, $0A, 0
MSG_DOS_NOFILE:  .BYTE "FILE NOT FOUND", $0D, $0A, 0
MSG_DOS_USAGE:   .BYTE "USAGE: SAVE F,SSSS-EEEE / LOAD F[,AAAA]", $0D, $0A
                 .BYTE "       ERASE F / RENAME OLD,NEW", $0D, $0A
                 .BYTE "       COPY SRC,DST", $0D, $0A, 0
MSG_DOS_SAVED:   .BYTE "SAVED", $0D, $0A, 0
MSG_DOS_LOADED:  .BYTE "LOADED", $0D, $0A, 0
MSG_DOS_ERASED:  .BYTE "ERASED", $0D, $0A, 0
MSG_DOS_RENAMED: .BYTE "RENAMED", $0D, $0A, 0
MSG_DOS_IMPORTED:.BYTE "IMPORTED", $0D, $0A, 0
MSG_DOS_EXPORTED:.BYTE "EXPORTED", $0D, $0A, 0
MSG_DOS_WRITEERR:.BYTE "WRITE ERROR (DISK FULL?)", $0D, $0A, 0
MSG_DOS_HOSTERR: .BYTE "HOST I/O ERROR", $0D, $0A, 0
MSG_DOS_COPIED:  .BYTE "COPIED", $0D, $0A, 0
MSG_DOS_MOVED:   .BYTE "MOVED", $0D, $0A, 0
MSG_DOS_TOOBIG:  .BYTE "FILE TOO BIG", $0D, $0A, 0
MSG_DOS_VER:     .BYTE "MFC/OS ", 0      ; version number appended from DOS_VERSION
MSG_DOS_MEM:     .BYTE "$0000-$00FF ZERO PAGE", $0D, $0A
                 .BYTE "$0100-$01FF STACK", $0D, $0A
                 .BYTE "$0200-$03FF SYSTEM VARS", $0D, $0A
                 .BYTE "$0400-$07FF FREE RAM   (1K)", $0D, $0A
                 .BYTE "$0800-$8FFF USER RAM   (34K)", $0D, $0A
                 .BYTE "$9000-$AFFF DOS ROM    (8K)", $0D, $0A
                 .BYTE "$B000-$DFFF MODULES    (12K)", $0D, $0A
                 .BYTE "$E000-$FFFF KERNEL ROM (8K)", $0D, $0A
                 .BYTE "$FE2D-$FE36 VIDEO PORT (VIC)", $0D, $0A, 0
MSG_DOS_CATHDR:  .BYTE "NAME            BYTES  MODIFIED", $0D, $0A, 0
MSG_DOS_NODATE:  .BYTE "(no date)", 0
MSG_DOS_FREE1:   .BYTE "DISK FREE: ", 0
MSG_DOS_FREE2:   .BYTE " BYTES (", 0
MSG_DOS_FREE3:   .BYTE " KB)", $0D, $0A, 0
MSG_DOS_DIRTAG:  .BYTE "<D>", 0
MSG_DOS_DRAWER_NEW:  .BYTE "DRAWER CREATED", $0D, $0A, 0
MSG_DOS_DRAWER_DROP: .BYTE "DRAWER DROPPED", $0D, $0A, 0
MSG_DOS_NOTEMPTY:.BYTE "DRAWER NOT EMPTY", $0D, $0A, 0
MSG_DOS_NODRAWER:.BYTE "NO SUCH DRAWER", $0D, $0A, 0
MSG_DOS_NOTROOT: .BYTE "NOT IN ROOT", $0D, $0A, 0
MSG_DOS_EXISTS:  .BYTE "NAME EXISTS", $0D, $0A, 0

; Verb table: keyword pointer, handler address. Terminated by a $0000 keyword.
; Aliases simply point at the same handler (CLEAR=CLS, CAT=CATALOG, MORE=TYPE).
DOS_VERB_TAB:
    .word KW_CLS,         _DOS_DO_CLS
    .word KW_CLEAR,       _DOS_DO_CLS
    .word KW_BANKS,       _DOS_DO_BANKS
    .word KW_HELP,        _DOS_DO_HELP
    .word KW_MON,         _DOS_DO_MON
    .word KW_CATALOG,     _DOS_DO_CAT
    .word KW_CAT,         _DOS_DO_CAT
    .word KW_TYPE,        _DOS_DO_TYPE
    .word KW_SAVE,        _DOS_DO_SAVE
    .word KW_LOAD,        _DOS_DO_LOAD
    .word KW_ERASE,       _DOS_DO_ERASE
    .word KW_RENAME,      _DOS_DO_RENAME
    .word KW_IMPORT,      _DOS_DO_IMPORT
    .word KW_EXPORT,      _DOS_DO_EXPORT
    .word KW_COPY,        _DOS_DO_COPY
    .word KW_DISKFREE,    _DOS_DO_FREE
    .word KW_MORE,        _DOS_DO_TYPE
    .word KW_VERSION,     _DOS_DO_VER
    .word KW_MEMMAP,      _DOS_DO_MEM
    .word KW_NEWDRAWER,   _DOS_DO_NEWDRAWER
    .word KW_OPEN,        _DOS_DO_OPEN
    .word KW_CLOSE,       _DOS_DO_CLOSE
    .word KW_DROPDRAWER,  _DOS_DO_DROPDRAWER
    .word KW_MOVE,        _DOS_DO_MOVE
    .word KW_DATE,        _DOS_DO_DATE
    .word $0000                         ; end of table

KW_CLS:          .BYTE "CLS", 0
KW_CLEAR:        .BYTE "CLEAR", 0
KW_BANKS:        .BYTE "BANKS", 0
KW_HELP:         .BYTE "HELP", 0
KW_MON:          .BYTE "MON", 0
KW_CATALOG:      .BYTE "CATALOG", 0
KW_CAT:          .BYTE "CAT", 0
KW_TYPE:         .BYTE "TYPE", 0
KW_SAVE:         .BYTE "SAVE", 0
KW_LOAD:         .BYTE "LOAD", 0
KW_ERASE:        .BYTE "ERASE", 0
KW_RENAME:       .BYTE "RENAME", 0
KW_IMPORT:       .BYTE "IMPORT", 0
KW_EXPORT:       .BYTE "EXPORT", 0
KW_COPY:         .BYTE "COPY", 0
KW_MOVE:         .BYTE "MOVE", 0
KW_DISKFREE:     .BYTE "DISKFREE", 0
KW_MORE:         .BYTE "MORE", 0
KW_VERSION:      .BYTE "VERSION", 0
KW_MEMMAP:       .BYTE "MEMMAP", 0
KW_NEWDRAWER:    .BYTE "NEWDRAWER", 0
KW_OPEN:         .BYTE "OPEN", 0
KW_CLOSE:        .BYTE "CLOSE", 0
KW_DROPDRAWER:   .BYTE "DROPDRAWER", 0
KW_DATE:         .BYTE "DATE", 0

; ================================================================
; BLOCK DEVICE PRIMITIVES
; ================================================================
; Transfer whole 512-byte sectors between a host disk.img sector and a RAM
; buffer, driving the $FE24-$FE28 registers. The buffer pointer is passed in
; BLK_BUF_PTR ($3A/$3B) and is preserved across the call. The sector buffer
; spans two pages, so the transfer walks Y from 0..255 twice, bumping the
; pointer's high byte between the halves.
;
; In:  A = LBA low, X = LBA high; BLK_BUF_PTR -> 512-byte RAM buffer
; Out: carry clear = OK, carry set = device error (A = BLK_STATUS on error)

; ----------------------------------------------------------------
; _BLK_READ_SECTOR - read sector (A/X) into the RAM buffer
; ----------------------------------------------------------------
_BLK_READ_SECTOR:
    STA BLK_LBA                         ; LBA low (also resets the data-port index)
    STX BLK_LBA+1                       ; LBA high
    LDA #BLK_CMD_READ
    STA BLK_CMD                         ; device reads the sector into its buffer
    LDA BLK_STATUS
    BNE @err                            ; non-zero = error
    LDY #$00
@page0:
    LDA BLK_DATA                        ; first 256 bytes
    STA (BLK_BUF_PTR),Y
    INY
    BNE @page0
    INC BLK_BUF_PTR+1                   ; advance to the second page
@page1:
    LDA BLK_DATA                        ; second 256 bytes
    STA (BLK_BUF_PTR),Y
    INY
    BNE @page1
    DEC BLK_BUF_PTR+1                   ; restore the caller's pointer
    CLC
    RTS
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _BLK_WRITE_SECTOR - write the RAM buffer to sector (A/X)
; ----------------------------------------------------------------
_BLK_WRITE_SECTOR:
    STA BLK_LBA                         ; LBA low (resets the data-port index to 0)
    STX BLK_LBA+1                       ; LBA high
    LDY #$00
@page0:
    LDA (BLK_BUF_PTR),Y                 ; first 256 bytes -> data port
    STA BLK_DATA
    INY
    BNE @page0
    INC BLK_BUF_PTR+1                   ; advance to the second page
@page1:
    LDA (BLK_BUF_PTR),Y                 ; second 256 bytes -> data port
    STA BLK_DATA
    INY
    BNE @page1
    DEC BLK_BUF_PTR+1                   ; restore the caller's pointer
    JSR _DOS_BLK_FLUSH                  ; flush the buffer to the sector
    BNE @err
    CLC
    RTS
@err:
    SEC
    RTS

; ================================================================
; INTERNAL BLOCK/SECTOR HELPERS
; ================================================================

; ----------------------------------------------------------------
; _DOS_READ_SECTOR - read sector A/X into the block device buffer
; ----------------------------------------------------------------
; Unlike BLK_READ_SECTOR this does not copy to RAM; it just triggers the read so
; the caller can stream bytes from BLK_DATA. In: A=LBA lo, X=LBA hi. Out: carry
; set on device error.
_DOS_READ_SECTOR:
    STA BLK_LBA
    STX BLK_LBA+1
    LDA #BLK_CMD_READ
    STA BLK_CMD
    LDA BLK_STATUS
    BEQ @ok
    SEC
    RTS
@ok:
    CLC
    RTS

; ----------------------------------------------------------------
; _DOS_SKIP_BYTES - discard DOS_TMP bytes from the block data port
; ----------------------------------------------------------------
; Advances the block device's data-port index by reading and discarding. Used to
; seek to a field/entry within the buffered sector. Consumes DOS_TMP (16-bit).
_DOS_SKIP_BYTES:
@loop:
    LDA DOS_TMP
    ORA DOS_TMP+1
    BEQ @done
    LDA BLK_DATA                        ; consume one byte
    LDA DOS_TMP
    BNE @declo
    DEC DOS_TMP+1
@declo:
    DEC DOS_TMP
    BRA @loop
@done:
    RTS

; _DOS_SLOT32_SKIP - skip A*32 bytes in the buffered sector (seek to a 32-byte
; directory slot). In: A = slot index (0-15). Consumes DOS_TMP; clobbers A/X.
_DOS_SLOT32_SKIP:
    STA DOS_TMP
    STZ DOS_TMP+1
    LDX #5                              ; * 32 = << 5
@s:
    ASL DOS_TMP
    ROL DOS_TMP+1
    DEX
    BNE @s
    JMP _DOS_SKIP_BYTES                 ; tail

; _DOS_SEEK_DIRENT - read the directory sector at DOS_W_DIRENT_LBA and position
; the data port at slot DOS_W_DIRENT_IDX. Out: carry set on a read error.
_DOS_SEEK_DIRENT:
    LDA DOS_W_DIRENT_LBA
    LDX DOS_W_DIRENT_LBA+1
    JSR _DOS_READ_SECTOR
    BCS @e
    LDA DOS_W_DIRENT_IDX
    JSR _DOS_SLOT32_SKIP
    CLC
@e:
    RTS

; _DOS_BLK_FLUSH - write the buffered sector back (BLK_LBA unchanged). On return
; the Z flag reflects BLK_STATUS (Z set = ready), so callers do `BNE err`.
_DOS_BLK_FLUSH:
    LDA #BLK_CMD_WRITE
    STA BLK_CMD
    LDA BLK_STATUS
    RTS

; ================================================================
; FILESYSTEM: MOUNT
; ================================================================

; ----------------------------------------------------------------
; _FS_ENSURE_MOUNT - mount the volume if not already mounted
; ----------------------------------------------------------------
_FS_ENSURE_MOUNT:
    LDA DOS_MOUNTED
    BEQ _FS_MOUNT                       ; not mounted -> mount (tail call)
    CLC
    RTS

; ----------------------------------------------------------------
; _FS_MOUNT - read the boot sector, parse the BPB, cache geometry
; ----------------------------------------------------------------
; Computes and stores: sectors/cluster, FAT start, root-dir start, data start,
; root entry count. Out: carry set on error (DOS_MOUNTED left 0).
_FS_MOUNT:
    LDA #$00                            ; boot sector = LBA 0
    LDX #$00
    JSR _DOS_READ_SECTOR
    BCC @read_ok
@badgeom:                               ; also the geometry-rejection exit below:
    STZ DOS_MOUNTED                     ;   read failed, or not a 512-byte-sector
    SEC                                 ;   FAT16 volume -> refuse to mount rather
    RTS                                 ;   than destroy it on the first write
@read_ok:
    ; seek to BPB offset $0B (BytesPerSector)
    LDA #$0B
    STA DOS_TMP
    STZ DOS_TMP+1
    JSR _DOS_SKIP_BYTES
    ; Validate the geometry before trusting it. Driving a volume that is not
    ; 512-byte-sector FAT16 wrecks it on the first write: 12-bit FAT entries read as
    ; 16-bit give garbage chains, and marking a "free" pair $FFFF writes two bytes
    ; over a packed FAT12 table. FAT32 is worse -- RootEntCnt and FATSize16 are 0,
    ; so root_start == fat_start and directory entries would be written into the FAT.
    LDA BLK_DATA                        ; $0B BytesPerSector lo -- must be $0200
    BNE @badgeom
    LDA BLK_DATA                        ; $0C BytesPerSector hi
    CMP #$02
    BNE @badgeom
    LDA BLK_DATA                        ; $0D SectorsPerCluster
    STA DOS_SEC_PER_CLUS
    LDA BLK_DATA                        ; $0E ReservedSectorCount lo = FAT start
    STA DOS_FAT_START
    LDA BLK_DATA                        ; $0F ReservedSectorCount hi
    STA DOS_FAT_START+1
    LDA BLK_DATA                        ; $10 NumberOfFATs
    STA DOS_NUMFATS
    LDA BLK_DATA                        ; $11 RootEntryCount lo
    STA DOS_ROOT_ENTS
    LDA BLK_DATA                        ; $12 RootEntryCount hi
    STA DOS_ROOT_ENTS+1
    LDA BLK_DATA                        ; $13 TotalSectors16 lo
    STA DOS_TOTAL_SEC
    LDA BLK_DATA                        ; $14 TotalSectors16 hi
    STA DOS_TOTAL_SEC+1
    LDA BLK_DATA                        ; $15 MediaDescriptor (unused)
    LDA BLK_DATA                        ; $16 FATSize16 lo
    STA DOS_FATSIZE
    LDA BLK_DATA                        ; $17 FATSize16 hi
    STA DOS_FATSIZE+1
    LDA DOS_ROOT_ENTS                   ; a fixed root of 0 entries means FAT32
    ORA DOS_ROOT_ENTS+1
    BEQ @badgeom
    LDA DOS_FATSIZE                     ; FATSize16 == 0 likewise means FAT32
    ORA DOS_FATSIZE+1
    BEQ @badgeom
    ; Distinguish FAT12 from FAT16 by the BPB type string at $36 ("FAT16   " vs
    ; "FAT12   "): the 5th character is the only byte that differs. The spec's own
    ; discriminator is the cluster count (>= 4085 is FAT16), but this project's test
    ; images are deliberately 128 clusters for speed and would fail that, so trust
    ; the label the formatter wrote instead. Cursor is at $18; skip to $3A.
    LDA #$22
    STA DOS_TMP
    STZ DOS_TMP+1
    JSR _DOS_SKIP_BYTES
    LDA BLK_DATA                        ; $3A = 5th char of the FS type string
    CMP #'6'
    BNE @badgeom
    ; root_start = FAT_start + NumFATs * FATSize
    STZ DOS_TMP
    STZ DOS_TMP+1
    LDX DOS_NUMFATS
    BEQ @mul_done
@mul:
    CLC
    LDA DOS_TMP
    ADC DOS_FATSIZE
    STA DOS_TMP
    LDA DOS_TMP+1
    ADC DOS_FATSIZE+1
    STA DOS_TMP+1
    DEX
    BNE @mul
@mul_done:
    CLC
    LDA DOS_FAT_START
    ADC DOS_TMP
    STA DOS_ROOT_START
    LDA DOS_FAT_START+1
    ADC DOS_TMP+1
    STA DOS_ROOT_START+1
    ; root_sectors = (RootEntryCount + 15) >> 4   (32 bytes/entry, 16 entries/sector)
    CLC
    LDA DOS_ROOT_ENTS
    ADC #15
    STA DOS_TMP
    LDA DOS_ROOT_ENTS+1
    ADC #0
    STA DOS_TMP+1
    LDX #4
@sh:
    LSR DOS_TMP+1
    ROR DOS_TMP
    DEX
    BNE @sh
    ; data_start = root_start + root_sectors
    CLC
    LDA DOS_ROOT_START
    ADC DOS_TMP
    STA DOS_DATA_START
    LDA DOS_ROOT_START+1
    ADC DOS_TMP+1
    STA DOS_DATA_START+1
    ; highest valid cluster = 1 + (TotalSectors - data_start) / SectorsPerCluster.
    ; (Assumes TotalSectors16 is set, true for FAT16 volumes < 32MB.)
    SEC
    LDA DOS_TOTAL_SEC
    SBC DOS_DATA_START
    STA DOS_TMP
    LDA DOS_TOTAL_SEC+1
    SBC DOS_DATA_START+1
    STA DOS_TMP+1
    STZ DOS_MAX_CLUS
    STZ DOS_MAX_CLUS+1
@divloop:
    LDA DOS_TMP+1                       ; while DOS_TMP >= SectorsPerCluster
    BNE @divsub
    LDA DOS_TMP
    CMP DOS_SEC_PER_CLUS
    BCC @divdone
@divsub:
    SEC
    LDA DOS_TMP
    SBC DOS_SEC_PER_CLUS
    STA DOS_TMP
    LDA DOS_TMP+1
    SBC #0
    STA DOS_TMP+1
    INC DOS_MAX_CLUS
    BNE @divloop
    INC DOS_MAX_CLUS+1
    BRA @divloop
@divdone:
    INC DOS_MAX_CLUS                    ; clusters numbered 2..(count+1)
    BNE @setmount
    INC DOS_MAX_CLUS+1
@setmount:
    LDA #1
    STA DOS_MOUNTED
    STZ DOS_CWD_CLUS                    ; a freshly mounted volume starts at root
    STZ DOS_CWD_CLUS+1
    STZ DOS_CWD_NAME
    LDA #$02                            ; allocation rover starts at the first data cluster
    STA DOS_ALLOC_HINT
    STZ DOS_ALLOC_HINT+1
    CLC
    RTS

; ================================================================
; FILESYSTEM: DIRECTORY ENUMERATION
; ================================================================

; ----------------------------------------------------------------
; _FS_DIR_FIRST - begin a root-directory scan; return the first entry
; ----------------------------------------------------------------
; Out: carry clear and DOS_ENTRY filled with the first valid 8.3 entry, or carry
; set if the directory is empty / unreadable.
_FS_DIR_FIRST:
    JSR _FS_ENSURE_MOUNT
    BCS @err
    LDA DOS_CWD_CLUS                    ; enumerate the current directory
    STA DOS_TGT_CLUS
    LDA DOS_CWD_CLUS+1
    STA DOS_TGT_CLUS+1
    JSR _DOS_DIR_OPEN
    BRA _FS_DIR_NEXT
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_DIR_OPEN - arm the directory iterator for DOS_TGT_CLUS
; ----------------------------------------------------------------
; In: DOS_TGT_CLUS = directory first cluster (0 = root). Root mode is bounded by
; the entry count (DOS_DIR_LEFT); subdir mode walks the cluster chain and ends on
; the $00 marker / chain EOC. Assumes the volume is already mounted.
_DOS_DIR_OPEN:
    LDA DOS_TGT_CLUS
    ORA DOS_TGT_CLUS+1
    BNE @subdir
    STZ DOS_DIR_MODE                    ; mode 0: fixed root region
    LDA DOS_ROOT_START
    STA DOS_DIR_LBA
    LDA DOS_ROOT_START+1
    STA DOS_DIR_LBA+1
    STZ DOS_DIR_IDX
    LDA DOS_ROOT_ENTS
    STA DOS_DIR_LEFT
    LDA DOS_ROOT_ENTS+1
    STA DOS_DIR_LEFT+1
    RTS
@subdir:
    LDA #$01                            ; mode 1: subdirectory cluster chain
    STA DOS_DIR_MODE
    LDA DOS_TGT_CLUS
    STA DOS_DIR_CLUS
    LDA DOS_TGT_CLUS+1
    STA DOS_DIR_CLUS+1
    STZ DOS_DIR_SIC
    STZ DOS_DIR_IDX
    JMP _DOS_DIRCLUS_TO_LBA             ; DOS_DIR_CLUS -> DOS_DIR_LBA (tail)

; _FS_DIR_FIRST_TGT - like _FS_DIR_FIRST but scans the directory named by
; DOS_TGT_CLUS (already set; volume assumed mounted). Returns the first entry.
_FS_DIR_FIRST_TGT:
    JSR _DOS_DIR_OPEN
    BRA _FS_DIR_NEXT

; ----------------------------------------------------------------
; _FS_DIR_NEXT - return the next valid entry in the open directory
; ----------------------------------------------------------------
; Skips deleted / LFN / volume entries; stops at the end-of-directory marker
; (root mode also stops when the entry count is exhausted; subdir mode also at
; chain EOC, signalled by DOS_DIR_MODE = 2). Out: carry clear and DOS_ENTRY
; filled, or carry set when no more entries.
_FS_DIR_NEXT:
@loop:
    LDA DOS_DIR_MODE
    BEQ @rootcount                      ; mode 0 = root
    CMP #$02
    BEQ @end                            ; mode 2 = subdir chain exhausted
    BRA @have                           ; mode 1 = subdir active
@rootcount:
    LDA DOS_DIR_LEFT                    ; root: entries remaining?
    ORA DOS_DIR_LEFT+1
    BNE @have
@end:
    SEC
    RTS
@have:
    JSR _DOS_READ_DIR_ENTRY             ; DOS_DIR_LBA/IDX -> DOS_ENTRY
    BCS @end                            ; read error -> end
    JSR _DOS_DIR_ADVANCE                ; bump cursor (root: dec LEFT; subdir: chain)
    LDA DOS_ENTRY+DIR_NAME
    BNE @notend
    ; end-of-directory marker: no more entries
    STZ DOS_DIR_LEFT
    STZ DOS_DIR_LEFT+1
    LDA #$02                            ; mark exhausted (harmless in root mode)
    STA DOS_DIR_MODE
    SEC
    RTS
@notend:
    CMP #DIRENT_DELETED
    BEQ @loop                           ; deleted -> skip
    LDA DOS_ENTRY+DIR_ATTR
    AND #$0F
    CMP #ATTR_LFN
    BEQ @loop                           ; long-file-name fragment -> skip
    LDA DOS_ENTRY+DIR_ATTR
    AND #ATTR_VOLUME
    BNE @loop                           ; volume label -> skip
    CLC                                 ; valid entry (file or drawer)
    RTS

; ----------------------------------------------------------------
; _DOS_READ_DIR_ENTRY - load the entry at DOS_DIR_LBA/DOS_DIR_IDX
; ----------------------------------------------------------------
; Re-reads the directory sector and skips to the indexed 32-byte entry, so each
; call is self-contained. Fills DOS_ENTRY (32 bytes). Out: carry set on error.
_DOS_READ_DIR_ENTRY:
    LDA DOS_DIR_LBA
    LDX DOS_DIR_LBA+1
    JSR _DOS_READ_SECTOR
    BCS @err
    LDA DOS_DIR_IDX                     ; skip DOS_DIR_IDX * 32 to the slot
    JSR _DOS_SLOT32_SKIP
    LDY #$00
@rd:
    LDA BLK_DATA
    STA DOS_ENTRY,Y
    INY
    CPY #32
    BNE @rd
    CLC
    RTS
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_DIR_ADVANCE - advance the directory cursor by one entry
; ----------------------------------------------------------------
; Root mode (DOS_DIR_MODE=0): step within the contiguous root region and
; decrement DOS_DIR_LEFT. Subdir mode (1): step the sector within the cluster,
; following the FAT chain at a cluster boundary; set mode 2 at chain EOC.
_DOS_DIR_ADVANCE:
    LDA DOS_DIR_MODE
    BNE @sub
    ; ---- root region ----
    INC DOS_DIR_IDX
    LDA DOS_DIR_IDX
    CMP #16                             ; 16 entries per 512-byte sector
    BCC @dec
    STZ DOS_DIR_IDX
    INC DOS_DIR_LBA
    BNE @dec
    INC DOS_DIR_LBA+1
@dec:
    LDA DOS_DIR_LEFT                    ; DOS_DIR_LEFT--
    BNE @declo
    DEC DOS_DIR_LEFT+1
@declo:
    DEC DOS_DIR_LEFT
    RTS
@sub:
    ; ---- subdirectory cluster chain ----
    INC DOS_DIR_IDX
    LDA DOS_DIR_IDX
    CMP #16
    BCC @sdone                          ; still inside this sector
    STZ DOS_DIR_IDX
    INC DOS_DIR_SIC                     ; next sector of the cluster
    LDA DOS_DIR_SIC
    CMP DOS_SEC_PER_CLUS
    BCC @ssect                          ; still inside this cluster
    ; cluster boundary: follow the FAT chain to the next cluster
    LDA DOS_DIR_CLUS
    STA DOS_ARG_CLUS
    LDA DOS_DIR_CLUS+1
    STA DOS_ARG_CLUS+1
    JSR _DOS_READ_FAT_ENTRY             ; DOS_ARG_VAL = FAT[cluster]
    BCS @sexhaust
    LDA DOS_ARG_VAL+1                   ; end-of-chain ($FFF8..$FFFF)?
    CMP #>FAT_EOC
    BCC @schain
    LDA DOS_ARG_VAL
    CMP #<FAT_EOC
    BCS @sexhaust
@schain:
    LDA DOS_ARG_VAL
    STA DOS_DIR_CLUS
    LDA DOS_ARG_VAL+1
    STA DOS_DIR_CLUS+1
    STZ DOS_DIR_SIC
    JMP _DOS_DIRCLUS_TO_LBA             ; recompute DOS_DIR_LBA (tail)
@ssect:
    INC DOS_DIR_LBA                     ; clusters are contiguous sectors
    BNE @sdone
    INC DOS_DIR_LBA+1
@sdone:
    RTS
@sexhaust:
    LDA #$02                            ; chain ended: next _FS_DIR_NEXT stops
    STA DOS_DIR_MODE
    RTS

; ----------------------------------------------------------------
; _DOS_DIRCLUS_TO_LBA - DOS_DIR_CLUS -> DOS_DIR_LBA (first sector of cluster)
; ----------------------------------------------------------------
; Reuses _DOS_CLUS_TO_LBA's math but preserves the open-file cursor DOS_F_CLUS
; (directory walks must not disturb a file's cluster pointer).
_DOS_DIRCLUS_TO_LBA:
    LDA DOS_F_CLUS
    PHA
    LDA DOS_F_CLUS+1
    PHA
    LDA DOS_DIR_CLUS
    STA DOS_F_CLUS
    LDA DOS_DIR_CLUS+1
    STA DOS_F_CLUS+1
    JSR _DOS_CLUS_TO_LBA                ; -> DOS_F_LBA
    LDA DOS_F_LBA
    STA DOS_DIR_LBA
    LDA DOS_F_LBA+1
    STA DOS_DIR_LBA+1
    PLA
    STA DOS_F_CLUS+1
    PLA
    STA DOS_F_CLUS
    RTS

; ================================================================
; FILESYSTEM: FILE READ (FS_OPEN / FS_GETB / FS_CLOSE)
; ================================================================

; ----------------------------------------------------------------
; _FS_OPEN - open a file by name for reading
; ----------------------------------------------------------------
; In:  A/X = pointer to a null-terminated filename ("NAME.EXT", case-insensitive).
;      (Y = mode is reserved for write; read-only for now.)
; Out: carry clear and the open-file cursor armed (first data sector loaded), or
;      carry set if not mounted / not found. Only one file open at a time.
;
; NOTE: FS_GETB streams bytes straight from the block device's sector buffer, so
; the caller must not issue other block-device operations between FS_OPEN and
; FS_CLOSE (the "one open file at a time" contract).
_FS_OPEN:
    STA DOS_PTR
    STX DOS_PTR+1
    STY DOS_W_MODE                      ; Y = mode: 0 = read, non-zero = write
    JSR _DOS_RESOLVE_PATH               ; full path -> DOS_TGT_CLUS + bare name
    BCS @err
    LDA DOS_RES_NAMEPTR                 ; parse the bare 8.3 name
    STA DOS_PTR
    LDA DOS_RES_NAMEPTR+1
    STA DOS_PTR+1
    JSR _DOS_PARSE_NAME83               ; -> DOS_NAME83 (11-byte 8.3)
    LDA DOS_W_MODE
    BEQ @read_mode
    JMP _FS_OPEN_WRITE                  ; write into DOS_TGT_CLUS
@read_mode:
    JSR _FS_DIR_FIRST_TGT               ; scan DOS_TGT_CLUS; first entry in DOS_ENTRY
    BCS @err
@check:
    LDY #$00
@cmp:
    LDA DOS_ENTRY,Y
    CMP DOS_NAME83,Y
    BNE @nextent
    INY
    CPY #11
    BNE @cmp
    ; name matched: a drawer ($10) is not a file -> keep scanning
    LDA DOS_ENTRY+DIR_ATTR
    AND #ATTR_DIRECTORY
    BNE @nextent
    BRA @found                          ; all 11 bytes matched, and it's a file
@nextent:
    JSR _FS_DIR_NEXT
    BCC @check
@err:
    STZ DOS_W_MODE                      ; nothing open (see _FS_OPEN_WRITE's @err)
    SEC                                 ; not found / not mounted
    RTS
@found:
    LDA DOS_ENTRY+DIR_CLUSTER_LO
    STA DOS_F_CLUS
    LDA DOS_ENTRY+DIR_CLUSTER_LO+1
    STA DOS_F_CLUS+1
    LDA DOS_ENTRY+DIR_SIZE
    STA DOS_F_LEFT
    LDA DOS_ENTRY+DIR_SIZE+1
    STA DOS_F_LEFT+1
    LDA DOS_ENTRY+DIR_SIZE+2
    STA DOS_F_LEFT+2
    LDA DOS_ENTRY+DIR_SIZE+3
    STA DOS_F_LEFT+3
    STZ DOS_F_SIC
    STZ DOS_F_OFF
    STZ DOS_F_OFF+1
    ; empty file: opened, but nothing to load (FS_GETB returns EOF immediately)
    LDA DOS_F_LEFT
    ORA DOS_F_LEFT+1
    ORA DOS_F_LEFT+2
    ORA DOS_F_LEFT+3
    BEQ @ok
    ; load the first data sector
    JSR _DOS_CLUS_TO_LBA
    LDA DOS_F_LBA
    LDX DOS_F_LBA+1
    JSR _DOS_READ_SECTOR
    BCS @err
@ok:
    CLC
    RTS

; ----------------------------------------------------------------
; _FS_GETB - read the next byte of the open file
; ----------------------------------------------------------------
; Out: carry clear and A = byte, or carry set = EOF. Streams from the block
; device buffer; crosses sector and cluster boundaries transparently.
_FS_GETB:
    LDA DOS_F_LEFT
    ORA DOS_F_LEFT+1
    ORA DOS_F_LEFT+2
    ORA DOS_F_LEFT+3
    BNE @have
    SEC                                 ; EOF
    RTS
@have:
    ; need the next sector? (offset reached 512 = $0200)
    LDA DOS_F_OFF
    BNE @read
    LDA DOS_F_OFF+1
    CMP #$02
    BNE @read
    JSR _DOS_NEXT_SECTOR
    BCS @eof                            ; chain ended unexpectedly
@read:
    LDA BLK_DATA
    PHA
    INC DOS_F_OFF                       ; offset++
    BNE @noff
    INC DOS_F_OFF+1
@noff:
    JSR _DOS_DEC_LEFT                   ; bytes-remaining--
    PLA
    CLC
    RTS
@eof:
    SEC
    RTS

; ----------------------------------------------------------------
; _FS_CLOSE - close the open file (read: just clears the cursor)
; ----------------------------------------------------------------
_FS_CLOSE:
    LDA DOS_W_MODE
    BEQ @read_close
    JMP _FS_CLOSE_WRITE                  ; write: flush + finalize the dir entry
@read_close:
    STZ DOS_F_LEFT
    STZ DOS_F_LEFT+1
    STZ DOS_F_LEFT+2
    STZ DOS_F_LEFT+3
    CLC
    RTS

; ================================================================
; FILESYSTEM: FILE WRITE (FS_OPEN write mode / FS_PUTB / FS_CLOSE)
; ================================================================
; Writes stream byte-by-byte: clusters are allocated on demand and chained in
; the FAT, file data fills the block device's sector buffer (flushed at sector
; boundaries), and the directory entry is finalized on close. Like reads, no
; 512-byte RAM buffer is used; FAT / directory updates are read-modify-write of
; the buffered sector (single FAT copy - matches our images).

; ----------------------------------------------------------------
; _FS_OPEN_WRITE - create/truncate DOS_NAME83 for writing
; ----------------------------------------------------------------
_FS_OPEN_WRITE:
    JSR _FS_ENSURE_MOUNT
    BCS @err
    LDA #$20                            ; files carry the archive attribute
    STA DOS_W_ATTR
    JSR _DOS_DIR_FIND_FOR_WRITE         ; sets DOS_W_DIRENT_* in DOS_TGT_CLUS
    BCS @err                            ; directory full
    STZ DOS_W_FIRST_CLUS                ; no data clusters yet
    STZ DOS_W_FIRST_CLUS+1
    STZ DOS_W_PREV_CLUS
    STZ DOS_W_PREV_CLUS+1
    STZ DOS_W_SIZE
    STZ DOS_W_SIZE+1
    STZ DOS_W_SIZE+2
    STZ DOS_W_SIZE+3
    STZ DOS_F_SIC
    STZ DOS_F_OFF
    STZ DOS_F_OFF+1
    JSR _DOS_DIR_WRITE_ENTRY            ; write the (empty) entry so the name exists
    BCS @err
    CLC
    RTS
@err:
    STZ DOS_W_MODE                      ; no file is open: leaving write mode set
    SEC                                 ; would let a later FS_CLOSE finalize the
    RTS                                 ; PREVIOUS file's directory entry

; ----------------------------------------------------------------
; _FS_PUTB - append the byte in A to the open file
; ----------------------------------------------------------------
_FS_PUTB:
    LDX DOS_W_MODE
    BNE @ok_mode
    SEC                                 ; not open for write
    RTS
@ok_mode:
    PHA                                 ; save the byte
    LDA DOS_W_FIRST_CLUS                ; first byte ever? allocate cluster 1
    ORA DOS_W_FIRST_CLUS+1
    BNE @have_cluster
    JSR _DOS_ALLOC_CLUSTER              ; DOS_ARG_CLUS = new cluster
    BCS @err
    LDA DOS_ARG_CLUS
    STA DOS_W_FIRST_CLUS
    STA DOS_F_CLUS
    STA DOS_W_PREV_CLUS
    LDA DOS_ARG_CLUS+1
    STA DOS_W_FIRST_CLUS+1
    STA DOS_F_CLUS+1
    STA DOS_W_PREV_CLUS+1
    STZ DOS_F_SIC
    JSR _DOS_CLUS_TO_LBA                ; DOS_F_CLUS -> DOS_F_LBA
    JSR _DOS_BEGIN_DATA_SECTOR
@have_cluster:
    LDA DOS_F_OFF+1                     ; current sector full? (offset == 512)
    CMP #$02
    BNE @write
    JSR _DOS_WRITE_FLUSH_SECTOR        ; flush this data sector first
    BCS @err
    JSR _DOS_WRITE_ADVANCE_SECTOR      ; next sector (alloc+chain at cluster end)
    BCS @err
@write:
    PLA
    STA BLK_DATA                        ; stream the byte into the sector buffer
    INC DOS_F_OFF
    BNE @noff
    INC DOS_F_OFF+1
@noff:
    JSR _DOS_INC_SIZE
    CLC
    RTS
@err:
    PLA
    SEC
    RTS

; ----------------------------------------------------------------
; _FS_CLOSE_WRITE - flush the final sector + finalize the directory entry
; ----------------------------------------------------------------
_FS_CLOSE_WRITE:
    LDA DOS_W_FIRST_CLUS                ; empty file? nothing to flush
    ORA DOS_W_FIRST_CLUS+1
    BEQ @finalize
@pad:
    LDA DOS_F_OFF+1                     ; pad the partial sector to 512 with zeros
    CMP #$02
    BCS @flush
    STZ BLK_DATA
    INC DOS_F_OFF
    BNE @pad
    INC DOS_F_OFF+1
    BRA @pad
@flush:
    JSR _DOS_BLK_FLUSH
    BNE @err
@finalize:
    JSR _DOS_DIR_WRITE_ENTRY            ; final first-cluster + size
    BCS @err
    STZ DOS_W_MODE
    CLC
    RTS
@err:
    STZ DOS_W_MODE
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_BEGIN_DATA_SECTOR - point the device at DOS_F_LBA, reset the port
; ----------------------------------------------------------------
; Writing BLK_LBA resets the data-port index to 0, so subsequent BLK_DATA
; writes fill the sector from the start; the matching flush writes it back.
_DOS_BEGIN_DATA_SECTOR:
    LDA DOS_F_LBA
    STA BLK_LBA
    LDA DOS_F_LBA+1
    STA BLK_LBA+1
    STZ DOS_F_OFF
    STZ DOS_F_OFF+1
    RTS

; ----------------------------------------------------------------
; _DOS_WRITE_FLUSH_SECTOR - flush the buffered data sector to DOS_F_LBA
; ----------------------------------------------------------------
_DOS_WRITE_FLUSH_SECTOR:
    LDA #BLK_CMD_WRITE                  ; BLK_LBA still = the current data sector
    STA BLK_CMD
    LDA BLK_STATUS
    BNE @err
    CLC
    RTS
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_WRITE_ADVANCE_SECTOR - advance to the next data sector for writing
; ----------------------------------------------------------------
_DOS_WRITE_ADVANCE_SECTOR:
    INC DOS_F_SIC
    LDA DOS_F_SIC
    CMP DOS_SEC_PER_CLUS
    BCC @same
    ; cluster boundary: allocate a new cluster and chain prev -> new
    JSR _DOS_ALLOC_CLUSTER
    BCS @err
    LDA DOS_ARG_CLUS
    STA DOS_NEW_CLUS
    LDA DOS_ARG_CLUS+1
    STA DOS_NEW_CLUS+1
    LDA DOS_W_PREV_CLUS                 ; FAT[prev] = new
    STA DOS_ARG_CLUS
    LDA DOS_W_PREV_CLUS+1
    STA DOS_ARG_CLUS+1
    JSR _DOS_FAT_LINK
    BCS @err
    LDA DOS_NEW_CLUS                    ; current = prev = new
    STA DOS_F_CLUS
    STA DOS_W_PREV_CLUS
    LDA DOS_NEW_CLUS+1
    STA DOS_F_CLUS+1
    STA DOS_W_PREV_CLUS+1
    STZ DOS_F_SIC
    JSR _DOS_CLUS_TO_LBA
    JSR _DOS_BEGIN_DATA_SECTOR
    CLC
    RTS
@same:
    INC DOS_F_LBA
    BNE @begin
    INC DOS_F_LBA+1
@begin:
    JSR _DOS_BEGIN_DATA_SECTOR
    CLC
    RTS
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_INC_SIZE - 32-bit increment of DOS_W_SIZE
; ----------------------------------------------------------------
_DOS_INC_SIZE:
    INC DOS_W_SIZE
    BNE @done
    INC DOS_W_SIZE+1
    BNE @done
    INC DOS_W_SIZE+2
    BNE @done
    INC DOS_W_SIZE+3
@done:
    RTS

; ----------------------------------------------------------------
; _DOS_ALLOC_CLUSTER - find a free FAT entry, mark it EOC, return it
; ----------------------------------------------------------------
; Out: DOS_ARG_CLUS = allocated cluster; carry set if the disk is full.
; Scans from the DOS_ALLOC_HINT rover (the cluster just past the last allocation)
; rather than restarting at cluster 2 every call, so writing a multi-cluster file
; is O(file) FAT reads instead of O(file x used-clusters). On reaching the end it
; wraps once to cluster 2 to pick up clusters freed below the rover.
_DOS_ALLOC_CLUSTER:
    STZ DOS_ALLOC_WRAP
    LDA DOS_ALLOC_HINT                  ; start from the rover
    STA DOS_ARG_CLUS
    LDA DOS_ALLOC_HINT+1
    STA DOS_ARG_CLUS+1
@scan:
    LDA DOS_ARG_CLUS+1                  ; DOS_ARG_CLUS <= DOS_MAX_CLUS ?
    CMP DOS_MAX_CLUS+1
    BCC @check
    BNE @atend
    LDA DOS_ARG_CLUS
    CMP DOS_MAX_CLUS
    BEQ @check                          ; ARG == MAX (valid, inclusive)
    BCC @check                          ; ARG < MAX (valid)
    ; DOS_ARG_CLUS > DOS_MAX_CLUS
@atend:
    LDA DOS_ALLOC_WRAP                  ; already wrapped once -> disk full
    BNE @full
    INC DOS_ALLOC_WRAP                  ; wrap to cluster 2 and rescan the low range
    LDA #$02
    STA DOS_ARG_CLUS
    STZ DOS_ARG_CLUS+1
    BRA @scan
@check:
    JSR _DOS_READ_FAT_ENTRY            ; DOS_ARG_VAL = FAT[DOS_ARG_CLUS]
    BCS @full
    LDA DOS_ARG_VAL
    ORA DOS_ARG_VAL+1
    BEQ @found                          ; free entry
    INC DOS_ARG_CLUS
    BNE @scan
    INC DOS_ARG_CLUS+1
    BRA @scan
@found:
    LDA DOS_ARG_CLUS                    ; advance the rover past this cluster
    CLC
    ADC #$01
    STA DOS_ALLOC_HINT
    LDA DOS_ARG_CLUS+1
    ADC #$00
    STA DOS_ALLOC_HINT+1
    LDA #$FF                            ; mark EOC
    STA DOS_ARG_VAL
    STA DOS_ARG_VAL+1
    JSR _DOS_WRITE_FAT_ENTRY            ; DOS_ARG_CLUS preserved = allocated cluster
    BCS @full
    CLC
    RTS
@full:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_FREE_CHAIN - free the cluster chain starting at DOS_ARG_CLUS
; ----------------------------------------------------------------
_DOS_FREE_CHAIN:
    LDA #$02                            ; rewind the rover so freed clusters get reused
    STA DOS_ALLOC_HINT
    STZ DOS_ALLOC_HINT+1
@loop:
    LDA DOS_ARG_CLUS+1                  ; stop at EOC ($FFF8..$FFFF)
    CMP #>FAT_EOC
    BNE @check_low
    LDA DOS_ARG_CLUS
    CMP #<FAT_EOC
    BCS @done
@check_low:
    LDA DOS_ARG_CLUS+1                  ; stop at an invalid cluster (< 2)
    BNE @go
    LDA DOS_ARG_CLUS
    CMP #$02
    BCC @done
@go:
    JSR _DOS_READ_FAT_ENTRY            ; DOS_ARG_VAL = next cluster
    BCS @done
    LDA DOS_ARG_VAL                     ; stash next
    STA DOS_FREE_NEXT
    LDA DOS_ARG_VAL+1
    STA DOS_FREE_NEXT+1
    STZ DOS_ARG_VAL                     ; free this entry (= 0)
    STZ DOS_ARG_VAL+1
    JSR _DOS_WRITE_FAT_ENTRY
    BCS @done
    LDA DOS_FREE_NEXT                   ; advance to next cluster
    STA DOS_ARG_CLUS
    LDA DOS_FREE_NEXT+1
    STA DOS_ARG_CLUS+1
    BRA @loop
@done:
    RTS

; ----------------------------------------------------------------
; _DOS_DIR_FIND_FOR_WRITE - locate the directory slot for DOS_NAME83
; ----------------------------------------------------------------
; Reuses an existing entry of the same name (and frees its old cluster chain),
; otherwise reclaims the first deleted ($E5) slot, otherwise appends at the
; end-of-directory slot. Scans the directory given by DOS_TGT_CLUS (0 = root). A
; subdirectory that runs out of slots grows by a cluster; the fixed root reports
; DIR FULL. Sets DOS_W_DIRENT_LBA/IDX. Carry set if full / disk full.
;
; A name that matches a DRAWER is refused (carry set) rather than reused: taking
; a directory's slot would free its directory cluster chain and orphan every file
; inside it, so `SAVE GAMES,...` (a missing '/') would destroy the drawer.
_DOS_DIR_FIND_FOR_WRITE:
    STZ DOS_W_FREE_LBA                  ; no reclaimable deleted slot seen yet
    STZ DOS_W_FREE_LBA+1
    JSR _DOS_DIR_OPEN                   ; arm iterator on DOS_TGT_CLUS
@loop:
    LDA DOS_DIR_MODE
    BEQ @rootcount                      ; root: bounded by the entry count
    CMP #$02
    BEQ @grow                           ; subdir chain ended with no free slot
    BRA @read
@rootcount:
    LDA DOS_DIR_LEFT
    ORA DOS_DIR_LEFT+1
    BEQ @exhausted                      ; root is fixed -> reclaim or DIR FULL
@read:
    JSR _DOS_READ_DIR_ENTRY            ; DOS_ENTRY = slot at DOS_DIR_LBA/IDX
    BCS @exhausted
    LDA DOS_ENTRY+DIR_NAME
    BEQ @atend                          ; $00 end-of-directory
    CMP #DIRENT_DELETED
    BEQ @deleted                        ; deleted -> remember it, keep scanning
    LDX #$00                            ; live entry: compare the 8.3 name
@cmp:
    LDA DOS_ENTRY,X
    CMP DOS_NAME83,X
    BNE @next
    INX
    CPX #11
    BNE @cmp
    LDA DOS_ENTRY+DIR_ATTR              ; name matches -- but a drawer is not a
    AND #ATTR_DIRECTORY                 ; file, and freeing its chain would
    BNE @full                           ; orphan everything in it
    ; name matches an existing file: reuse this slot, free its old chain (truncate)
    JSR _DOS_SAVE_DIRENT_POS
    LDA DOS_ENTRY+DIR_CLUSTER_LO
    STA DOS_ARG_CLUS
    LDA DOS_ENTRY+DIR_CLUSTER_LO+1
    STA DOS_ARG_CLUS+1
    JSR _DOS_FREE_CHAIN
    CLC
    RTS
@deleted:
    LDA DOS_W_FREE_LBA                  ; latch the FIRST deleted slot only, so a
    ORA DOS_W_FREE_LBA+1                ; same-name match found later still wins
    BNE @next
    LDA DOS_DIR_LBA
    STA DOS_W_FREE_LBA
    LDA DOS_DIR_LBA+1
    STA DOS_W_FREE_LBA+1
    LDA DOS_DIR_IDX
    STA DOS_W_FREE_IDX
@next:
    JSR _DOS_DIR_ADVANCE
    BRA @loop
@exhausted:
    JSR _DOS_RECLAIM_SLOT               ; no free slot left: reuse a deleted one
    BCC @ok
@full:
    SEC
    RTS
@grow:
    JSR _DOS_RECLAIM_SLOT               ; prefer a deleted slot over growing
    BCC @ok
    JSR _DOS_DIR_GROW                   ; allocate+chain+zero a new dir cluster
    BCS @full
    ; cursor now at the new (zeroed) cluster's first slot
    JSR _DOS_SAVE_DIRENT_POS
    BRA @ok
@atend:
    JSR _DOS_RECLAIM_SLOT               ; prefer a deleted slot over consuming the
    BCC @ok                             ; end-of-directory slot
    JSR _DOS_SAVE_DIRENT_POS
@ok:
    CLC
    RTS

; Commit the latched deleted slot as the write target. Carry clear if one was
; latched (DOS_W_DIRENT_* now points at it), carry set if none was seen.
_DOS_RECLAIM_SLOT:
    LDA DOS_W_FREE_LBA
    ORA DOS_W_FREE_LBA+1
    BEQ @none
    LDA DOS_W_FREE_LBA
    STA DOS_W_DIRENT_LBA
    LDA DOS_W_FREE_LBA+1
    STA DOS_W_DIRENT_LBA+1
    LDA DOS_W_FREE_IDX
    STA DOS_W_DIRENT_IDX
    CLC
    RTS
@none:
    SEC
    RTS

; Record the current directory cursor as the open file's entry location.
_DOS_SAVE_DIRENT_POS:
    LDA DOS_DIR_LBA
    STA DOS_W_DIRENT_LBA
    LDA DOS_DIR_LBA+1
    STA DOS_W_DIRENT_LBA+1
    LDA DOS_DIR_IDX
    STA DOS_W_DIRENT_IDX
    RTS

; ================================================================
; FILESYSTEM: ERASE (FS_DELETE)
; ================================================================
; _FS_DELETE - delete a file by name: free its cluster chain and mark its
; directory entry deleted ($E5). In: A/X = ptr to null-terminated name.
; Out: carry clear on success, carry set if not mounted / not found.
_FS_DELETE:
    STA DOS_PTR
    STX DOS_PTR+1
    JSR _DOS_RESOLVE_PATH               ; path -> DOS_TGT_CLUS + bare name
    BCS @err
    LDA DOS_RES_NAMEPTR
    STA DOS_PTR
    LDA DOS_RES_NAMEPTR+1
    STA DOS_PTR+1
    JSR _DOS_PARSE_NAME83
    JSR _DOS_DIR_FIND_EXISTING          ; -> DOS_W_DIRENT_*, DOS_ENTRY (in DOS_TGT_CLUS)
    BCS @err
    LDA DOS_ENTRY+DIR_ATTR              ; refuse to ERASE a drawer (use DROPDRAWER)
    AND #ATTR_DIRECTORY
    BNE @err
    LDA DOS_ENTRY+DIR_CLUSTER_LO        ; free the cluster chain
    STA DOS_ARG_CLUS
    LDA DOS_ENTRY+DIR_CLUSTER_LO+1
    STA DOS_ARG_CLUS+1
    JSR _DOS_FREE_CHAIN
    JMP _DOS_MARK_DELETED               ; mark the entry deleted (tail call)
@err:
    SEC
    RTS

; _DOS_DIR_FIND_EXISTING - scan the directory DOS_TGT_CLUS (0 = root) for
; DOS_NAME83. Out: carry clear with DOS_W_DIRENT_* = the slot and DOS_ENTRY =
; the entry, or carry set if not mounted / not found. (Shared by open/delete/
; rename and the drawer verbs; the caller sets DOS_TGT_CLUS.)
_DOS_DIR_FIND_EXISTING:
    JSR _FS_ENSURE_MOUNT
    BCS @no
    JSR _DOS_DIR_OPEN                   ; arm iterator on DOS_TGT_CLUS
@loop:
    LDA DOS_DIR_MODE
    BEQ @rootcount
    CMP #$02
    BEQ @no                             ; subdir chain exhausted
    BRA @read
@rootcount:
    LDA DOS_DIR_LEFT
    ORA DOS_DIR_LEFT+1
    BEQ @no
@read:
    JSR _DOS_READ_DIR_ENTRY
    BCS @no
    LDA DOS_ENTRY+DIR_NAME
    BEQ @no                             ; end of directory
    CMP #DIRENT_DELETED
    BEQ @adv
    LDX #$00
@cmp:
    LDA DOS_ENTRY,X
    CMP DOS_NAME83,X
    BNE @adv
    INX
    CPX #11
    BNE @cmp
    JSR _DOS_SAVE_DIRENT_POS            ; record DOS_W_DIRENT_*
    CLC
    RTS
@adv:
    JSR _DOS_DIR_ADVANCE
    BRA @loop
@no:
    SEC
    RTS

; _FS_RENAME - rename a file. In: A/X = old name ptr, DOS_PTR2 = new name ptr.
; Out: carry clear renamed; carry set if old not found / not mounted.
_FS_RENAME:
    STA DOS_PTR
    STX DOS_PTR+1
    JSR _DOS_RESOLVE_PATH               ; old path -> DOS_TGT_CLUS + bare name
    BCS @err
    ; Vet the NEW name FIRST, while DOS_W_DIRENT_* is still unused -- the duplicate
    ; lookup below sets it, so it cannot run after the old slot has been located.
    JSR @parse_new
    JSR _DOS_NAME83_EMPTY               ; an all-space name is unreachable: it shows
    BCS @err                            ;   as a nameless CATALOG row and is not
                                        ;   valid FAT. "RENAME A," produced one,
                                        ;   because the new-name pointer lands on the
                                        ;   NUL the shell writes at MON_CMDLEN.
    JSR _DOS_DIR_FIND_EXISTING          ; is the new name already taken?
    BCC @err                            ;   yes -> refuse. Two entries of one name
                                        ;   each keep a live chain, so a later SAVE
                                        ;   frees one while the other still points
                                        ;   at those clusters -> cross-linked files.
    ; Locate the OLD entry, then overwrite its name field with the new one.
    LDA DOS_RES_NAMEPTR
    STA DOS_PTR
    LDA DOS_RES_NAMEPTR+1
    STA DOS_PTR+1
    JSR _DOS_PARSE_NAME83               ; old -> DOS_NAME83
    JSR _DOS_DIR_FIND_EXISTING          ; locate the slot (in DOS_TGT_CLUS)
    BCS @err
    JSR @parse_new                      ; new -> DOS_NAME83 again (the find above
                                        ;   overwrote it with the old name)
    JMP _DOS_DIR_WRITE_NAME             ; overwrite the 11-byte name (tail call)
@parse_new:
    LDA DOS_PTR2                        ; new (a plain name in the same dir)
    STA DOS_PTR
    LDA DOS_PTR2+1
    STA DOS_PTR+1
    JMP _DOS_PARSE_NAME83               ; tail
@err:
    SEC
    RTS

; _DOS_NAME83_EMPTY - carry set if DOS_NAME83 is all spaces (i.e. no name at all).
_DOS_NAME83_EMPTY:
    LDY #$00
@lp:
    LDA DOS_NAME83,Y
    CMP #ASCII_SPACE
    BNE @notempty
    INY
    CPY #11
    BNE @lp
    SEC
    RTS
@notempty:
    CLC
    RTS

; _DOS_DIR_WRITE_NAME - rmw the 11-byte name field of the slot at DOS_W_DIRENT_*
; with DOS_NAME83. Out: carry set on error.
_DOS_DIR_WRITE_NAME:
    JSR _DOS_SEEK_DIRENT
    BCS @err
    LDY #$00
@nm:
    LDA DOS_NAME83,Y
    STA BLK_DATA
    INY
    CPY #11
    BNE @nm
    JSR _DOS_BLK_FLUSH
    BNE @err
    CLC
    RTS
@err:
    SEC
    RTS

; _DOS_MARK_DELETED - write $E5 over name[0] of the slot at DOS_W_DIRENT_*
_DOS_MARK_DELETED:
    JSR _DOS_SEEK_DIRENT
    BCS @err
    LDA #DIRENT_DELETED
    STA BLK_DATA                        ; overwrite name[0]
    JSR _DOS_BLK_FLUSH
    BNE @err
    CLC
    RTS
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_DIR_WRITE_ENTRY - write the open file's 32-byte directory entry
; ----------------------------------------------------------------
; Read-modify-write the directory sector: seek to the slot, write name +
; archive attr + first cluster (DOS_W_FIRST_CLUS) + size (DOS_W_SIZE), flush.
_DOS_DIR_WRITE_ENTRY:
    STA RTC_LATCH                       ; snapshot host time into the RTC registers
    LDA RTC_FATTIME_LO                  ; the host pre-packs it into FAT format
    STA DOS_FTIME
    LDA RTC_FATTIME_HI
    STA DOS_FTIME+1
    LDA RTC_FATDATE_LO
    STA DOS_FDATE
    LDA RTC_FATDATE_HI
    STA DOS_FDATE+1
    JSR _DOS_SEEK_DIRENT
    BCS @err
    LDY #$00                            ; name (11 bytes)
@nm:
    LDA DOS_NAME83,Y
    STA BLK_DATA
    INY
    CPY #11
    BNE @nm
    LDA DOS_W_ATTR                      ; $0B attribute (archive for files, $10 drawer)
    STA BLK_DATA
    LDX #10                             ; $0C-$15 = 0 (reserved/create/access/clus-hi)
@z:
    STZ BLK_DATA
    DEX
    BNE @z
    LDA DOS_FTIME                       ; $16-$17 last-write time
    STA BLK_DATA
    LDA DOS_FTIME+1
    STA BLK_DATA
    LDA DOS_FDATE                       ; $18-$19 last-write date
    STA BLK_DATA
    LDA DOS_FDATE+1
    STA BLK_DATA
    LDA DOS_W_FIRST_CLUS                ; $1A-$1B first cluster (low word)
    STA BLK_DATA
    LDA DOS_W_FIRST_CLUS+1
    STA BLK_DATA
    LDA DOS_W_SIZE                      ; $1C-$1F size (32-bit)
    STA BLK_DATA
    LDA DOS_W_SIZE+1
    STA BLK_DATA
    LDA DOS_W_SIZE+2
    STA BLK_DATA
    LDA DOS_W_SIZE+3
    STA BLK_DATA
    JSR _DOS_BLK_FLUSH                  ; flush the directory sector
    BNE @err
    CLC
    RTS
@err:
    SEC
    RTS

; ================================================================
; FILESYSTEM: DRAWERS (one-level subdirectories)
; ================================================================

; ----------------------------------------------------------------
; _DOS_RESOLVE_PATH - map a path to (target dir cluster, bare 8.3 name)
; ----------------------------------------------------------------
; In: DOS_PTR -> a null-terminated name, optionally with one '/' separator:
;   FILE        -> current dir (DOS_CWD_CLUS)
;   /FILE       -> root
;   DRAWER/FILE -> the named root-level drawer
; Out: DOS_TGT_CLUS = directory cluster (0=root), DOS_RES_NAMEPTR -> the bare
; name; carry set on error (two-level path, drawer not found / not a drawer).
; The DRAWER/ case temporarily NUL-terminates the drawer name in the buffer to
; parse it, then restores the '/', so resolution does NOT clobber the caller's
; path (it may be reused, e.g. re-reading SYSTEM/IRC.LST every redial). The
; buffer must be writable; ABI callers pass bare names and never hit this path.
_DOS_RESOLVE_PATH:
    JSR _FS_ENSURE_MOUNT
    BCC @mounted
    RTS                                 ; mount failed: carry already set (== @err)
@mounted:
    LDY #$00
@find1:
    LDA (DOS_PTR),Y                     ; first '/'?
    BNE @notend                         ; (long-branch @bare: routine grew past 127)
    JMP @bare                           ; none -> bare name (current dir)
@notend:
    CMP #'/'
    BEQ @gotslash
    INY
    BRA @find1
@gotslash:
    STY DOS_RES_SLASH                  ; slash index
    INY
@find2:
    LDA (DOS_PTR),Y                     ; reject a second '/' (one level only)
    BEQ @oneslash
    CMP #'/'
    BEQ @err
    INY
    BRA @find2
@oneslash:
    LDA DOS_RES_SLASH                  ; DOS_RES_NAMEPTR = DOS_PTR + slashidx + 1
    SEC
    ADC DOS_PTR
    STA DOS_RES_NAMEPTR
    LDA DOS_PTR+1
    ADC #$00
    STA DOS_RES_NAMEPTR+1
    LDA DOS_RES_SLASH
    BNE @drawer                         ; non-zero slash index -> DRAWER/FILE
    STZ DOS_TGT_CLUS                    ; leading '/' -> root
    STZ DOS_TGT_CLUS+1
    CLC
    RTS
@drawer:
    LDY DOS_RES_SLASH                  ; temporarily terminate the drawer name at '/'
    LDA #$00
    STA (DOS_PTR),Y
    JSR _DOS_PARSE_NAME83               ; drawer name -> DOS_NAME83
    LDY DOS_RES_SLASH                  ; restore the '/': resolution must not clobber
    LDA #'/'                            ; the caller's path, which may be reused (e.g.
    STA (DOS_PTR),Y                     ; re-reading SYSTEM/IRC.LST on each redial)
    STZ DOS_TGT_CLUS                    ; look it up in root
    STZ DOS_TGT_CLUS+1
    JSR _DOS_DIR_FIND_EXISTING
    BCS @err
    LDA DOS_ENTRY+DIR_ATTR              ; must be a drawer
    AND #ATTR_DIRECTORY
    BEQ @err
    LDA DOS_ENTRY+DIR_CLUSTER_LO
    STA DOS_TGT_CLUS
    LDA DOS_ENTRY+DIR_CLUSTER_LO+1
    STA DOS_TGT_CLUS+1
    CLC
    RTS
@bare:
    LDA DOS_CWD_CLUS
    STA DOS_TGT_CLUS
    LDA DOS_CWD_CLUS+1
    STA DOS_TGT_CLUS+1
    LDA DOS_PTR
    STA DOS_RES_NAMEPTR
    LDA DOS_PTR+1
    STA DOS_RES_NAMEPTR+1
    CLC
    RTS
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_DIR_GROW - extend the open subdirectory by one zeroed cluster
; ----------------------------------------------------------------
; Allocate a cluster, chain it onto DOS_DIR_CLUS (the chain's last cluster),
; zero it, and point the iterator at its first slot. Carry set on disk-full.
_DOS_DIR_GROW:
    JSR _DOS_ALLOC_CLUSTER              ; DOS_ARG_CLUS = new (marked EOC)
    BCS @full
    LDA DOS_ARG_CLUS
    STA DOS_NEW_CLUS
    LDA DOS_ARG_CLUS+1
    STA DOS_NEW_CLUS+1
    LDA DOS_DIR_CLUS                    ; FAT[old last cluster] = new
    STA DOS_ARG_CLUS
    LDA DOS_DIR_CLUS+1
    STA DOS_ARG_CLUS+1
    JSR _DOS_FAT_LINK
    BCS @full
    LDA DOS_NEW_CLUS                    ; iterator now in the new cluster
    STA DOS_DIR_CLUS
    LDA DOS_NEW_CLUS+1
    STA DOS_DIR_CLUS+1
    STZ DOS_DIR_SIC
    STZ DOS_DIR_IDX
    LDA #$01                            ; back to active subdir mode (was exhausted)
    STA DOS_DIR_MODE
    JSR _DOS_DIRCLUS_TO_LBA
    JMP _DOS_ZERO_DIR_CLUSTER           ; zero it so unused slots read $00 (tail)
@full:
    SEC
    RTS

; _DOS_ZERO_DIR_CLUSTER - zero every sector of the cluster at DOS_DIR_LBA.
_DOS_ZERO_DIR_CLUSTER:
    LDA DOS_DIR_LBA
    STA DOS_TMP2
    LDA DOS_DIR_LBA+1
    STA DOS_TMP2+1
    LDX DOS_SEC_PER_CLUS
    ; fall through

; _DOS_ZERO_SECTORS - zero X consecutive sectors starting at LBA DOS_TMP2.
_DOS_ZERO_SECTORS:
    CPX #$00
    BEQ @done
@sec:
    PHX
    LDA DOS_TMP2
    STA BLK_LBA                         ; resets the data-port index to 0
    LDA DOS_TMP2+1
    STA BLK_LBA+1
    LDA #<512
    STA DOS_TMP
    LDA #>512
    STA DOS_TMP+1
    JSR _DOS_BLKZERO_N
    JSR _DOS_BLK_FLUSH
    BNE @err
    INC DOS_TMP2
    BNE @n
    INC DOS_TMP2+1
@n:
    PLX
    DEX
    BNE @sec
@done:
    CLC
    RTS
@err:
    PLX
    SEC
    RTS

; _DOS_BLKZERO_N - write DOS_TMP (16-bit) zero bytes to the BLK_DATA port.
_DOS_BLKZERO_N:
@l:
    LDA DOS_TMP
    ORA DOS_TMP+1
    BEQ @done
    STZ BLK_DATA
    LDA DOS_TMP
    BNE @declo
    DEC DOS_TMP+1
@declo:
    DEC DOS_TMP
    BRA @l
@done:
    RTS

; ----------------------------------------------------------------
; _DOS_INIT_DRAWER_CLUSTER - write '.'/'..' into a fresh drawer cluster
; ----------------------------------------------------------------
; In: DOS_DIR_CLUS = the drawer's first cluster, DOS_DIR_LBA = its first sector.
; Fills the first sector with the '.' (self) and '..' (root) entries then zeros;
; zeros any remaining sectors of the cluster. Carry set on a write error.
_DOS_INIT_DRAWER_CLUSTER:
    LDA DOS_DIR_LBA
    STA BLK_LBA
    LDA DOS_DIR_LBA+1
    STA BLK_LBA+1
    ; '.' entry: name '.'+10 spaces, attr $10, cluster = self, size 0
    LDA #'.'
    STA BLK_DATA
    LDX #10
@d1:
    LDA #' '
    STA BLK_DATA
    DEX
    BNE @d1
    LDA #ATTR_DIRECTORY
    STA BLK_DATA
    LDX #14                             ; $0C-$19 (incl cluster-hi) = 0
@d1z:
    STZ BLK_DATA
    DEX
    BNE @d1z
    LDA DOS_DIR_CLUS
    STA BLK_DATA
    LDA DOS_DIR_CLUS+1
    STA BLK_DATA
    LDX #4                              ; size = 0
@d1s:
    STZ BLK_DATA
    DEX
    BNE @d1s
    ; '..' entry: name '..'+9 spaces, attr $10, cluster = 0 (root), size 0
    LDA #'.'
    STA BLK_DATA
    LDA #'.'
    STA BLK_DATA
    LDX #9
@d2:
    LDA #' '
    STA BLK_DATA
    DEX
    BNE @d2
    LDA #ATTR_DIRECTORY
    STA BLK_DATA
    LDX #14
@d2z:
    STZ BLK_DATA
    DEX
    BNE @d2z
    STZ BLK_DATA                        ; cluster = 0
    STZ BLK_DATA
    LDX #4
@d2s:
    STZ BLK_DATA
    DEX
    BNE @d2s
    LDA #<448                           ; zero the rest of the sector (512-64)
    STA DOS_TMP
    LDA #>448
    STA DOS_TMP+1
    JSR _DOS_BLKZERO_N
    JSR _DOS_BLK_FLUSH
    BNE @err
    LDA DOS_SEC_PER_CLUS                ; zero any remaining sectors of the cluster
    CMP #2
    BCC @done
    LDA DOS_DIR_LBA
    CLC
    ADC #1
    STA DOS_TMP2
    LDA DOS_DIR_LBA+1
    ADC #0
    STA DOS_TMP2+1
    LDX DOS_SEC_PER_CLUS
    DEX
    JMP _DOS_ZERO_SECTORS               ; tail (carry reflects result)
@done:
    CLC
    RTS
@err:
    SEC
    RTS

; _DOS_DRAWER_EMPTY - is the drawer in DOS_TGT_CLUS empty (only '.'/'..')?
; Out: carry clear = empty, carry set = has a live entry.
_DOS_DRAWER_EMPTY:
    JSR _FS_DIR_FIRST_TGT
    BCS @empty
@chk:
    LDA DOS_ENTRY+DIR_NAME              ; '.' and '..' both start with '.'
    CMP #'.'                            ; (8.3 names can't start with '.')
    BNE @notempty
    JSR _FS_DIR_NEXT
    BCC @chk
@empty:
    CLC
    RTS
@notempty:
    SEC
    RTS

; _DOS_SET_CWD_NAME - copy DOS_NAME83's base (to first space, max 8) to the
; prompt display name DOS_CWD_NAME (null-terminated).
_DOS_SET_CWD_NAME:
    LDX #$00
@c:
    LDA DOS_NAME83,X
    CMP #' '
    BEQ @end
    STA DOS_CWD_NAME,X
    INX
    CPX #8
    BNE @c
@end:
    LDA #$00
    STA DOS_CWD_NAME,X
    RTS

; ================================================================
; FILESYSTEM: INTERNAL READ HELPERS
; ================================================================

; ----------------------------------------------------------------
; _DOS_PARSE_NAME83 - (DOS_PTR) "NAME.EXT" -> DOS_NAME83 (11, padded)
; ----------------------------------------------------------------
_DOS_PARSE_NAME83:
    LDX #$00                            ; space-fill the 11-byte buffer
@fill:
    LDA #' '
    STA DOS_NAME83,X
    INX
    CPX #11
    BNE @fill
    LDY #$00                            ; source index
    LDX #$00                            ; dest index (base = 0..7)
@base:
    LDA (DOS_PTR),Y
    BEQ @done
    CMP #'.'
    BEQ @dot
    JSR _DOS_UPCASE
    CPX #8
    BCS @nextb                          ; base full -> ignore extra chars
    STA DOS_NAME83,X
    INX
@nextb:
    INY
    BRA @base
@dot:
    INY                                 ; skip the '.'
    LDX #8                              ; dest = extension (8..10)
@ext:
    LDA (DOS_PTR),Y
    BEQ @done
    JSR _DOS_UPCASE
    CPX #11
    BCS @nexte
    STA DOS_NAME83,X
    INX
@nexte:
    INY
    BRA @ext
@done:
    RTS

; ----------------------------------------------------------------
; _DOS_UPCASE - fold a lowercase ASCII letter in A to uppercase
; ----------------------------------------------------------------
_DOS_UPCASE:
    CMP #'a'
    BCC @ok
    CMP #'z'+1
    BCS @ok
    AND #$DF
@ok:
    RTS

; ----------------------------------------------------------------
; _DOS_CLUS_TO_LBA - DOS_F_CLUS -> DOS_F_LBA (first sector of cluster)
; ----------------------------------------------------------------
; LBA = DataStart + (cluster - 2) * SectorsPerCluster.
_DOS_CLUS_TO_LBA:
    SEC
    LDA DOS_F_CLUS
    SBC #2
    STA DOS_TMP
    LDA DOS_F_CLUS+1
    SBC #0
    STA DOS_TMP+1
    STZ DOS_TMP2
    STZ DOS_TMP2+1
    LDX DOS_SEC_PER_CLUS
    BEQ @add
@mul:
    CLC
    LDA DOS_TMP2
    ADC DOS_TMP
    STA DOS_TMP2
    LDA DOS_TMP2+1
    ADC DOS_TMP+1
    STA DOS_TMP2+1
    DEX
    BNE @mul
@add:
    CLC
    LDA DOS_DATA_START
    ADC DOS_TMP2
    STA DOS_F_LBA
    LDA DOS_DATA_START+1
    ADC DOS_TMP2+1
    STA DOS_F_LBA+1
    RTS

; ----------------------------------------------------------------
; _DOS_NEXT_CLUSTER - follow the FAT chain: DOS_F_CLUS -> next cluster
; ----------------------------------------------------------------
; Out: carry clear and DOS_F_CLUS = next cluster, or carry set at end-of-chain /
; on a read error. FAT byte offset = cluster*2 (17-bit); split into a sector
; index (offset>>9) and an in-sector offset (offset & $1FF).
; _DOS_FAT_SEEK - read the FAT sector for DOS_ARG_CLUS and position the data
; port at that cluster's 2-byte entry (so the caller can read or overwrite it).
; FAT byte offset = cluster*2 (17-bit) -> sector index (offset>>9) + in-sector
; offset (offset & $1FF). Leaves BLK_LBA = the FAT sector. Carry set on error.
_DOS_FAT_SEEK:
    LDA DOS_ARG_CLUS
    ASL                                 ; lo<<1, C=c0
    STA DOS_TMP                         ; offset low byte (lo2)
    LDA DOS_ARG_CLUS+1
    ROL                                 ; hi<<1 | c0, C=c1 (bit16)
    STA DOS_TMP+1                       ; mid byte (mid2)
    LDA #$00
    ROL                                 ; A = c1
    STA DOS_TMP2                        ; c1
    LDA DOS_TMP+1                       ; offset bit8 = mid2 & 1
    AND #$01
    STA DOS_TMP2+1                      ; offset high byte (0/1)
    LDA DOS_TMP2                        ; sector index = ((c1<<8)|mid2) >> 1
    LSR
    LDA DOS_TMP+1
    ROR
    STA DOS_TMP2                        ; sector index low (high = 0)
    CLC
    LDA DOS_FAT_START
    ADC DOS_TMP2
    PHA
    LDA DOS_FAT_START+1
    ADC #$00
    TAX
    PLA
    JSR _DOS_READ_SECTOR
    BCS @err
    LDA DOS_TMP2+1                      ; skip to the in-sector offset
    STA DOS_TMP+1
    JSR _DOS_SKIP_BYTES
    CLC
    RTS
@err:
    SEC
    RTS

; _DOS_READ_FAT_ENTRY - read FAT[DOS_ARG_CLUS] into DOS_ARG_VAL. Carry on error.
_DOS_READ_FAT_ENTRY:
    JSR _DOS_FAT_SEEK
    BCS @err
    LDA BLK_DATA
    STA DOS_ARG_VAL
    LDA BLK_DATA
    STA DOS_ARG_VAL+1
    CLC
    RTS
@err:
    SEC
    RTS

; _DOS_WRITE_FAT_ENTRY - FAT[DOS_ARG_CLUS] = DOS_ARG_VAL (read-modify-write the
; FAT sector: seek positions the port at the entry, overwrite 2 bytes in the
; buffered sector, then flush it back). Carry on error.
; Write DOS_ARG_VAL into the entry for DOS_ARG_CLUS, in every FAT the volume has.
;
; Hosts format with TWO FATs by default -- both Linux `mkfs.fat -F 16` and macOS
; newfs_msdos/Disk Utility -- and updating only the first leaves the copies
; disagreeing. A host checker then "repairs" the volume by copying FAT #2 over
; FAT #1, which silently discards every file the machine wrote. Our own images use
; one FAT, so this costs nothing there.
_DOS_WRITE_FAT_ENTRY:
    JSR _DOS_FAT_PUT                    ; FAT #1
    BCS @err
    LDA DOS_NUMFATS
    CMP #$02
    BCC @done                           ; single-FAT volume: nothing to mirror
    JSR _DOS_FAT_BASE_ADD               ; base += FATSIZE -> FAT #2
    JSR _DOS_FAT_PUT
    PHP                                 ; keep the result across the restore
    JSR _DOS_FAT_BASE_SUB               ; always put the base back
    PLP
    BCS @err
@done:
    CLC
    RTS
@err:
    SEC
    RTS

; Store DOS_ARG_VAL into DOS_ARG_CLUS's entry in the FAT based at DOS_FAT_START.
_DOS_FAT_PUT:
    JSR _DOS_FAT_SEEK
    BCS @err
    LDA DOS_ARG_VAL
    STA BLK_DATA                        ; overwrite entry low
    LDA DOS_ARG_VAL+1
    STA BLK_DATA                        ; overwrite entry high
    JSR _DOS_BLK_FLUSH                  ; flush the FAT sector (BLK_LBA unchanged)
    BNE @err
    CLC
    RTS
@err:
    SEC
    RTS

; Move the FAT base one FAT forward / back (DOS_FAT_START +/- DOS_FATSIZE).
_DOS_FAT_BASE_ADD:
    CLC
    LDA DOS_FAT_START
    ADC DOS_FATSIZE
    STA DOS_FAT_START
    LDA DOS_FAT_START+1
    ADC DOS_FATSIZE+1
    STA DOS_FAT_START+1
    RTS
_DOS_FAT_BASE_SUB:
    SEC
    LDA DOS_FAT_START
    SBC DOS_FATSIZE
    STA DOS_FAT_START
    LDA DOS_FAT_START+1
    SBC DOS_FATSIZE+1
    STA DOS_FAT_START+1
    RTS
@err:
    SEC
    RTS

; _DOS_FAT_LINK - chain a freshly allocated cluster onto a predecessor:
; FAT[DOS_ARG_CLUS] = DOS_NEW_CLUS. In: DOS_ARG_CLUS = prev cluster, DOS_NEW_CLUS =
; new cluster. Out: carry set on error (tail-calls _DOS_WRITE_FAT_ENTRY).
_DOS_FAT_LINK:
    LDA DOS_NEW_CLUS
    STA DOS_ARG_VAL
    LDA DOS_NEW_CLUS+1
    STA DOS_ARG_VAL+1
    JMP _DOS_WRITE_FAT_ENTRY

; _DOS_NEXT_CLUSTER - follow the FAT chain: DOS_F_CLUS -> next cluster.
; Carry clear and DOS_F_CLUS updated, or carry set at end-of-chain / error.
_DOS_NEXT_CLUSTER:
    LDA DOS_F_CLUS
    STA DOS_ARG_CLUS
    LDA DOS_F_CLUS+1
    STA DOS_ARG_CLUS+1
    JSR _DOS_READ_FAT_ENTRY
    BCS @err
    LDA DOS_ARG_VAL
    STA DOS_F_CLUS
    LDA DOS_ARG_VAL+1
    STA DOS_F_CLUS+1
    CMP #>FAT_EOC                       ; high byte >= $FF ?
    BNE @range
    LDA DOS_F_CLUS
    CMP #<FAT_EOC
    BCS @err                            ; $FFF8..$FFFF -> EOC
@range:
    ; A usable cluster is 2..DOS_MAX_CLUS. 0 and 1 are reserved, $FFF0-$FFF7 are
    ; bad-cluster marks, and anything past the last cluster is outside the image.
    ; _DOS_CLUS_TO_LBA computes (c-2)*spc + data_start with no guard of its own (and
    ; none of its callers check a status), so a 0 in a chain underflows and puts the
    ; LBA *before* the data area -- in the root directory or the FAT. On a read that
    ; is only garbage, but the directory path WRITES, so a stray link would flush a
    ; 32-byte entry over the FAT. Orphaned chains and host-side repairs are exactly
    ; what leaves such links behind, so treat a corrupt one as end-of-chain.
    LDA DOS_F_CLUS+1
    BNE @cmpmax                         ; >= $0100, so certainly >= 2
    LDA DOS_F_CLUS
    CMP #$02
    BCC @err
@cmpmax:
    LDA DOS_MAX_CLUS
    CMP DOS_F_CLUS
    LDA DOS_MAX_CLUS+1
    SBC DOS_F_CLUS+1
    BCC @err                            ; cluster > the volume's last cluster
    CLC
    RTS
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_NEXT_SECTOR - advance the open file to its next data sector
; ----------------------------------------------------------------
; Steps within the current cluster, or follows the FAT chain at a cluster
; boundary. Loads the new sector and resets the offset. Out: carry set at EOC /
; on error.
_DOS_NEXT_SECTOR:
    INC DOS_F_SIC
    LDA DOS_F_SIC
    CMP DOS_SEC_PER_CLUS
    BCC @same
    JSR _DOS_NEXT_CLUSTER
    BCS @err
    STZ DOS_F_SIC
    JSR _DOS_CLUS_TO_LBA
    BRA @load
@same:
    INC DOS_F_LBA
    BNE @load
    INC DOS_F_LBA+1
@load:
    STZ DOS_F_OFF
    STZ DOS_F_OFF+1
    LDA DOS_F_LBA
    LDX DOS_F_LBA+1
    JSR _DOS_READ_SECTOR
    RTS                                 ; carry reflects the read
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_DEC_LEFT - decrement the 32-bit bytes-remaining counter
; ----------------------------------------------------------------
_DOS_DEC_LEFT:
    LDA DOS_F_LEFT
    SEC
    SBC #1
    STA DOS_F_LEFT
    LDA DOS_F_LEFT+1
    SBC #0
    STA DOS_F_LEFT+1
    LDA DOS_F_LEFT+2
    SBC #0
    STA DOS_F_LEFT+2
    LDA DOS_F_LEFT+3
    SBC #0
    STA DOS_F_LEFT+3
    RTS

; ================================================================
; DOS ABI JUMP TABLE ($AF00) - the stable entry points
; ================================================================
; Callers (the kernel BIOS, the monitor, user programs) bind to these fixed
; addresses. New entries are appended at the end so existing addresses never
; move.
.segment "DOSJUMP"
.org $AF00

DOS_COLD:         JMP _DOS_COLD          ; $AF00 - DOS shell cold entry (phase 4)
FS_OPEN:          JMP _FS_OPEN           ; $AF03
FS_GETB:          JMP _FS_GETB           ; $AF06
FS_PUTB:          JMP _FS_PUTB           ; $AF09
FS_CLOSE:         JMP _FS_CLOSE          ; $AF0C
FS_DIR_FIRST:     JMP _FS_DIR_FIRST      ; $AF0F
FS_DIR_NEXT:      JMP _FS_DIR_NEXT       ; $AF12
BLK_READ_SECTOR:  JMP _BLK_READ_SECTOR   ; $AF15
BLK_WRITE_SECTOR: JMP _BLK_WRITE_SECTOR  ; $AF18
FS_DELETE:        JMP _FS_DELETE         ; $AF1B
DOS_WARM:         JMP _DOS_WARM          ; $AF1E - re-enter the shell (no banner)
FS_RENAME:        JMP _FS_RENAME         ; $AF21 - A/X = old, DOS_PTR2 = new
