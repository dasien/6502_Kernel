; ================================================================
; dos.asm - MFC-DOS resident ROM ($9000-$AFFF, always mapped)
; ================================================================
; The resident operating system for MFC-DOS: the FAT16 filesystem driver and
; (later) the DOS command shell. See docs/dos_design.md.
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

MON_CMDBUF       = $0200                ; BIOS command-line buffer (page aligned)
MON_CMDLEN       = $026A                ; current command length
MON_MSG_PTR_LO   = $16                  ; message pointer for K_PRINT_MESSAGE
MON_MSG_PTR_HI   = $17
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
DOS_TMP2         = $031F                ; word: general 16-bit scratch
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

; FAT16 end-of-chain threshold (>= this means last cluster)
FAT_EOC          = $FFF8

; FAT16 directory-entry field offsets (within DOS_ENTRY / on disk)
DIR_NAME         = $00                  ; 11 bytes: 8.3 name (space padded)
DIR_ATTR         = $0B                  ; attribute byte
DIR_CLUSTER_LO   = $1A                  ; word: first cluster (low; high is 0 on FAT16)
DIR_SIZE         = $1C                  ; 4 bytes: file size

ATTR_LFN         = $0F                  ; (attr & $0F)==$0F -> long-file-name entry
ATTR_VOLUME      = $08                  ; volume-label bit
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
DOS_VERSION:
    .BYTE $00, $01                      ; version 0.1 (major, minor)

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
    LDA #<MSG_DOS_BANNER
    STA MON_MSG_PTR_LO
    LDA #>MSG_DOS_BANNER
    STA MON_MSG_PTR_HI
    JSR K_PRINT_MESSAGE
    JMP _DOS_PROMPT

; ----------------------------------------------------------------
; _DOS_WARM - re-entry from the monitor (no banner)
; ----------------------------------------------------------------
_DOS_WARM:
    LDX #$FF
    TXS
_DOS_PROMPT:
    LDA #'>'
    JSR K_PRINT_CHAR
    JSR K_READ_LINE                     ; -> MON_CMDBUF, length MON_CMDLEN
    LDA MON_CMDLEN
    BNE @run
    JMP _DOS_PROMPT                     ; empty line
@run:
    JSR _DOS_DISPATCH
    JMP _DOS_PROMPT

; ----------------------------------------------------------------
; _DOS_DISPATCH - match the typed verb and run its handler
; ----------------------------------------------------------------
_DOS_DISPATCH:
    LDA #<KW_HELP
    LDX #>KW_HELP
    JSR _DOS_VERB_MATCH
    BCS @n1
    JMP _DOS_DO_HELP
@n1:
    LDA #<KW_MON
    LDX #>KW_MON
    JSR _DOS_VERB_MATCH
    BCS @n2
    JMP _DOS_DO_MON
@n2:
    LDA #<KW_CATALOG
    LDX #>KW_CATALOG
    JSR _DOS_VERB_MATCH
    BCS @n3
    JMP _DOS_DO_CAT
@n3:
    LDA #<KW_CAT
    LDX #>KW_CAT
    JSR _DOS_VERB_MATCH
    BCS @n4
    JMP _DOS_DO_CAT
@n4:
    LDA #<KW_TYPE
    LDX #>KW_TYPE
    JSR _DOS_VERB_MATCH
    BCS @n5
    JMP _DOS_DO_TYPE
@n5:
    LDA #<KW_SAVE
    LDX #>KW_SAVE
    JSR _DOS_VERB_MATCH
    BCS @n6
    JMP _DOS_DO_SAVE
@n6:
    LDA #<KW_LOAD
    LDX #>KW_LOAD
    JSR _DOS_VERB_MATCH
    BCS @n7
    JMP _DOS_DO_LOAD
@n7:
    LDA #<KW_ERASE
    LDX #>KW_ERASE
    JSR _DOS_VERB_MATCH
    BCS @n8
    JMP _DOS_DO_ERASE
@n8:
    LDA #<KW_RENAME
    LDX #>KW_RENAME
    JSR _DOS_VERB_MATCH
    BCS @n9
    JMP _DOS_DO_RENAME
@n9:
    ; unknown verb
    JSR K_PRINT_NEWLINE
    LDA #<MSG_DOS_BADCMD
    STA MON_MSG_PTR_LO
    LDA #>MSG_DOS_BADCMD
    STA MON_MSG_PTR_HI
    JMP K_PRINT_MESSAGE                 ; tail (RTS to _DOS_PROMPT)

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
; _DOS_DO_HELP - list the built-in commands
; ----------------------------------------------------------------
_DOS_DO_HELP:
    JSR K_PRINT_NEWLINE
    LDA #<MSG_DOS_HELP
    STA MON_MSG_PTR_LO
    LDA #>MSG_DOS_HELP
    STA MON_MSG_PTR_HI
    JMP K_PRINT_MESSAGE

; ----------------------------------------------------------------
; _DOS_DO_MON - launch the monitor (returns to DOS_WARM via its Q command)
; ----------------------------------------------------------------
_DOS_DO_MON:
    JMP K_MON_ENTRY

; ----------------------------------------------------------------
; _DOS_DO_CAT - list files with sizes
; ----------------------------------------------------------------
_DOS_DO_CAT:
    JSR K_PRINT_NEWLINE
    JSR _FS_DIR_FIRST
    BCS @none
@loop:
    JSR _DOS_PRINT_ENTRY
    JSR _FS_DIR_NEXT
    BCC @loop
    RTS
@none:
    LDA #<MSG_DOS_NOFILES
    STA MON_MSG_PTR_LO
    LDA #>MSG_DOS_NOFILES
    STA MON_MSG_PTR_HI
    JMP K_PRINT_MESSAGE

; Print one DOS_ENTRY as "NAME.EXT  <8 hex size>" + newline.
_DOS_PRINT_ENTRY:
    LDX #$00                            ; base name (stop at first space)
@base:
    LDA DOS_ENTRY,X
    CMP #ASCII_SPACE
    BEQ @ext
    JSR K_PRINT_CHAR
    INX
    CPX #$08
    BNE @base
@ext:
    LDA DOS_ENTRY+8                     ; extension?
    CMP #ASCII_SPACE
    BEQ @size
    LDA #'.'
    JSR K_PRINT_CHAR
    LDX #$08
@extloop:
    LDA DOS_ENTRY,X
    CMP #ASCII_SPACE
    BEQ @size
    JSR K_PRINT_CHAR
    INX
    CPX #$0B
    BNE @extloop
@size:
    LDA #ASCII_SPACE
    JSR K_PRINT_CHAR
    JSR K_PRINT_CHAR
    LDA DOS_ENTRY+$1F                   ; 32-bit size, high byte first
    JSR K_PRINT_HEX_BYTE
    LDA DOS_ENTRY+$1E
    JSR K_PRINT_HEX_BYTE
    LDA DOS_ENTRY+$1D
    JSR K_PRINT_HEX_BYTE
    LDA DOS_ENTRY+$1C
    JSR K_PRINT_HEX_BYTE
    JMP K_PRINT_NEWLINE

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
    LDX MON_CMDLEN                      ; null-terminate the line
    LDA #$00
    STA MON_CMDBUF,X
    TYA                                 ; A/X = &MON_CMDBUF[Y]
    CLC
    ADC #<MON_CMDBUF
    PHA
    LDA #>MON_CMDBUF
    ADC #$00
    TAX
    PLA
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

; Shared error printers (newline + message, tail-call print).
_DOS_PERR_USAGE:
    LDA #<MSG_DOS_USAGE
    LDX #>MSG_DOS_USAGE
    BRA _DOS_PERR
_DOS_PERR_NOFILE:
    LDA #<MSG_DOS_NOFILE
    LDX #>MSG_DOS_NOFILE
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
    JSR _DOS_ARGSTART
    BCS @usage
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
@usage:
    JMP _DOS_PERR_USAGE

; ----------------------------------------------------------------
; _DOS_DO_RENAME - RENAME OLD,NEW
; ----------------------------------------------------------------
_DOS_DO_RENAME:
    JSR _DOS_ARGSTART
    BCS @usage
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
    JSR _DOS_ARGSTART
    BCC :+
    JMP @usage
:
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
    JMP _DOS_PERR_WRITE
@usage:
    JMP _DOS_PERR_USAGE

; ----------------------------------------------------------------
; _DOS_DO_LOAD - LOAD NAME[,AAAA]  (load addr from header unless overridden)
; ----------------------------------------------------------------
_DOS_DO_LOAD:
    JSR _DOS_ARGSTART
    BCC :+
    JMP @usage
:
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
; DOS shell strings
; ----------------------------------------------------------------
MSG_DOS_BANNER:  .BYTE $0D, $0A, "MFC/OS", $0D, $0A, 0
MSG_DOS_HELP:    .BYTE "CATALOG TYPE SAVE LOAD ERASE RENAME", $0D, $0A
                 .BYTE "MON HELP", $0D, $0A, 0
MSG_DOS_BADCMD:  .BYTE "COMMAND NOT FOUND", $0D, $0A, 0
MSG_DOS_NOFILES: .BYTE "NO FILES", $0D, $0A, 0
MSG_DOS_NOFILE:  .BYTE "FILE NOT FOUND", $0D, $0A, 0
MSG_DOS_USAGE:   .BYTE "USAGE: SAVE F,SSSS-EEEE / LOAD F[,AAAA]", $0D, $0A
                 .BYTE "       ERASE F / RENAME OLD,NEW", $0D, $0A, 0
MSG_DOS_SAVED:   .BYTE "SAVED", $0D, $0A, 0
MSG_DOS_LOADED:  .BYTE "LOADED", $0D, $0A, 0
MSG_DOS_ERASED:  .BYTE "ERASED", $0D, $0A, 0
MSG_DOS_RENAMED: .BYTE "RENAMED", $0D, $0A, 0
MSG_DOS_WRITEERR:.BYTE "WRITE ERROR (DISK FULL?)", $0D, $0A, 0
KW_HELP:         .BYTE "HELP", 0
KW_MON:          .BYTE "MON", 0
KW_CATALOG:      .BYTE "CATALOG", 0
KW_CAT:          .BYTE "CAT", 0
KW_TYPE:         .BYTE "TYPE", 0
KW_SAVE:         .BYTE "SAVE", 0
KW_LOAD:         .BYTE "LOAD", 0
KW_ERASE:        .BYTE "ERASE", 0
KW_RENAME:       .BYTE "RENAME", 0

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
    LDA #BLK_CMD_WRITE
    STA BLK_CMD                         ; flush the buffer to the sector
    LDA BLK_STATUS
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
    STZ DOS_MOUNTED                     ; read failed -> not mounted
    SEC
    RTS
@read_ok:
    ; seek to BPB offset $0B (BytesPerSector)
    LDA #$0B
    STA DOS_TMP
    STZ DOS_TMP+1
    JSR _DOS_SKIP_BYTES
    LDA BLK_DATA                        ; $0B BytesPerSector lo (assume 512)
    LDA BLK_DATA                        ; $0C BytesPerSector hi
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
    LDA DOS_ROOT_START
    STA DOS_DIR_LBA
    LDA DOS_ROOT_START+1
    STA DOS_DIR_LBA+1
    STZ DOS_DIR_IDX
    LDA DOS_ROOT_ENTS
    STA DOS_DIR_LEFT
    LDA DOS_ROOT_ENTS+1
    STA DOS_DIR_LEFT+1
    BRA _FS_DIR_NEXT
@err:
    SEC
    RTS

; ----------------------------------------------------------------
; _FS_DIR_NEXT - return the next valid root-directory entry
; ----------------------------------------------------------------
; Skips deleted, long-file-name, and volume-label entries; stops at the
; end-of-directory marker. Out: carry clear and DOS_ENTRY filled, or carry set
; when no more entries.
_FS_DIR_NEXT:
@loop:
    LDA DOS_DIR_LEFT                    ; entries remaining?
    ORA DOS_DIR_LEFT+1
    BNE @have
    SEC
    RTS
@have:
    JSR _DOS_READ_DIR_ENTRY             ; DOS_DIR_LBA/IDX -> DOS_ENTRY
    BCS @stop                           ; read error -> end
    JSR _DOS_DIR_ADVANCE                ; bump cursor, decrement DOS_DIR_LEFT
    LDA DOS_ENTRY+DIR_NAME
    BNE @notend
    ; end-of-directory marker: no more entries
    STZ DOS_DIR_LEFT
    STZ DOS_DIR_LEFT+1
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
    CLC                                 ; valid entry
    RTS
@stop:
    SEC
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
    ; skip DOS_DIR_IDX * 32 bytes
    LDA DOS_DIR_IDX
    STA DOS_TMP
    STZ DOS_TMP+1
    LDX #5                              ; * 32 = << 5
@sh:
    ASL DOS_TMP
    ROL DOS_TMP+1
    DEX
    BNE @sh
    JSR _DOS_SKIP_BYTES
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
_DOS_DIR_ADVANCE:
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
    JSR _DOS_PARSE_NAME83               ; -> DOS_NAME83 (11-byte 8.3)
    LDA DOS_W_MODE
    BEQ @read_mode
    JMP _FS_OPEN_WRITE
@read_mode:
    JSR _FS_DIR_FIRST                   ; mounts; first entry in DOS_ENTRY
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
    BRA @found                          ; all 11 bytes matched
@nextent:
    JSR _FS_DIR_NEXT
    BCC @check
@err:
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
    JSR _DOS_DIR_FIND_FOR_WRITE         ; sets DOS_W_DIRENT_*; frees old chain if reusing
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
    SEC
    RTS

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
    LDA #BLK_CMD_WRITE
    STA BLK_CMD
    LDA BLK_STATUS
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
    LDA DOS_NEW_CLUS
    STA DOS_ARG_VAL
    LDA DOS_NEW_CLUS+1
    STA DOS_ARG_VAL+1
    JSR _DOS_WRITE_FAT_ENTRY
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
_DOS_ALLOC_CLUSTER:
    LDA #$02                            ; clusters start at 2
    STA DOS_ARG_CLUS
    STZ DOS_ARG_CLUS+1
@scan:
    LDA DOS_ARG_CLUS+1                  ; DOS_ARG_CLUS <= DOS_MAX_CLUS ?
    CMP DOS_MAX_CLUS+1
    BCC @check
    BNE @full
    LDA DOS_ARG_CLUS
    CMP DOS_MAX_CLUS
    BEQ @check
    BCS @full
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
; otherwise appends at the end-of-directory slot. Sets DOS_W_DIRENT_LBA/IDX.
; Carry set if the directory is full. (Deleted slots are not reclaimed yet.)
_DOS_DIR_FIND_FOR_WRITE:
    LDA DOS_ROOT_START
    STA DOS_DIR_LBA
    LDA DOS_ROOT_START+1
    STA DOS_DIR_LBA+1
    STZ DOS_DIR_IDX
    LDA DOS_ROOT_ENTS
    STA DOS_DIR_LEFT
    LDA DOS_ROOT_ENTS+1
    STA DOS_DIR_LEFT+1
@loop:
    LDA DOS_DIR_LEFT
    ORA DOS_DIR_LEFT+1
    BEQ @full
    JSR _DOS_READ_DIR_ENTRY            ; DOS_ENTRY = slot at DOS_DIR_LBA/IDX
    BCS @full
    LDA DOS_ENTRY+DIR_NAME
    BEQ @usehere                        ; $00 end-of-directory -> append here
    CMP #DIRENT_DELETED
    BEQ @next                           ; deleted -> skip (no reclaim yet)
    LDX #$00                            ; live entry: compare the 8.3 name
@cmp:
    LDA DOS_ENTRY,X
    CMP DOS_NAME83,X
    BNE @next
    INX
    CPX #11
    BNE @cmp
    ; name matches: reuse this slot, free its old chain (truncate)
    JSR _DOS_SAVE_DIRENT_POS
    LDA DOS_ENTRY+DIR_CLUSTER_LO
    STA DOS_ARG_CLUS
    LDA DOS_ENTRY+DIR_CLUSTER_LO+1
    STA DOS_ARG_CLUS+1
    JSR _DOS_FREE_CHAIN
    CLC
    RTS
@next:
    JSR _DOS_DIR_ADVANCE
    BRA @loop
@usehere:
    JSR _DOS_SAVE_DIRENT_POS
    CLC
    RTS
@full:
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
    JSR _DOS_PARSE_NAME83
    JSR _DOS_DIR_FIND_EXISTING          ; -> DOS_W_DIRENT_*, DOS_ENTRY
    BCS @err
    LDA DOS_ENTRY+DIR_CLUSTER_LO        ; free the cluster chain
    STA DOS_ARG_CLUS
    LDA DOS_ENTRY+DIR_CLUSTER_LO+1
    STA DOS_ARG_CLUS+1
    JSR _DOS_FREE_CHAIN
    JMP _DOS_MARK_DELETED               ; mark the entry deleted (tail call)
@err:
    SEC
    RTS

; _DOS_DIR_FIND_EXISTING - scan the root directory for DOS_NAME83.
; Out: carry clear with DOS_W_DIRENT_* = the slot and DOS_ENTRY = the entry, or
; carry set if not mounted / not found. (Shared by delete and rename.)
_DOS_DIR_FIND_EXISTING:
    JSR _FS_ENSURE_MOUNT
    BCS @no
    LDA DOS_ROOT_START
    STA DOS_DIR_LBA
    LDA DOS_ROOT_START+1
    STA DOS_DIR_LBA+1
    STZ DOS_DIR_IDX
    LDA DOS_ROOT_ENTS
    STA DOS_DIR_LEFT
    LDA DOS_ROOT_ENTS+1
    STA DOS_DIR_LEFT+1
@loop:
    LDA DOS_DIR_LEFT
    ORA DOS_DIR_LEFT+1
    BEQ @no
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
    JSR _DOS_PARSE_NAME83               ; old -> DOS_NAME83
    JSR _DOS_DIR_FIND_EXISTING          ; locate the slot
    BCS @err
    LDA DOS_PTR2                        ; new -> DOS_NAME83
    STA DOS_PTR
    LDA DOS_PTR2+1
    STA DOS_PTR+1
    JSR _DOS_PARSE_NAME83
    JMP _DOS_DIR_WRITE_NAME             ; overwrite the 11-byte name (tail call)
@err:
    SEC
    RTS

; _DOS_DIR_WRITE_NAME - rmw the 11-byte name field of the slot at DOS_W_DIRENT_*
; with DOS_NAME83. Out: carry set on error.
_DOS_DIR_WRITE_NAME:
    LDA DOS_W_DIRENT_LBA
    LDX DOS_W_DIRENT_LBA+1
    JSR _DOS_READ_SECTOR
    BCS @err
    LDA DOS_W_DIRENT_IDX                ; skip to slot * 32
    STA DOS_TMP
    STZ DOS_TMP+1
    LDX #$05
@sh:
    ASL DOS_TMP
    ROL DOS_TMP+1
    DEX
    BNE @sh
    JSR _DOS_SKIP_BYTES
    LDY #$00
@nm:
    LDA DOS_NAME83,Y
    STA BLK_DATA
    INY
    CPY #11
    BNE @nm
    LDA #BLK_CMD_WRITE
    STA BLK_CMD
    LDA BLK_STATUS
    BNE @err
    CLC
    RTS
@err:
    SEC
    RTS

; _DOS_MARK_DELETED - write $E5 over name[0] of the slot at DOS_W_DIRENT_*
_DOS_MARK_DELETED:
    LDA DOS_W_DIRENT_LBA
    LDX DOS_W_DIRENT_LBA+1
    JSR _DOS_READ_SECTOR
    BCS @err
    LDA DOS_W_DIRENT_IDX                ; skip to slot * 32
    STA DOS_TMP
    STZ DOS_TMP+1
    LDX #$05
@sh:
    ASL DOS_TMP
    ROL DOS_TMP+1
    DEX
    BNE @sh
    JSR _DOS_SKIP_BYTES
    LDA #DIRENT_DELETED
    STA BLK_DATA                        ; overwrite name[0]
    LDA #BLK_CMD_WRITE
    STA BLK_CMD
    LDA BLK_STATUS
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
    LDA DOS_W_DIRENT_LBA
    LDX DOS_W_DIRENT_LBA+1
    JSR _DOS_READ_SECTOR
    BCS @err
    LDA DOS_W_DIRENT_IDX                ; skip to slot * 32
    STA DOS_TMP
    STZ DOS_TMP+1
    LDX #$05
@sh:
    ASL DOS_TMP
    ROL DOS_TMP+1
    DEX
    BNE @sh
    JSR _DOS_SKIP_BYTES
    LDY #$00                            ; name (11 bytes)
@nm:
    LDA DOS_NAME83,Y
    STA BLK_DATA
    INY
    CPY #11
    BNE @nm
    LDA #$20                            ; $0B attribute = archive
    STA BLK_DATA
    LDX #14                             ; $0C-$19 reserved/time/date + cluster-hi = 0
@z:
    STZ BLK_DATA
    DEX
    BNE @z
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
    LDA #BLK_CMD_WRITE                  ; flush the directory sector
    STA BLK_CMD
    LDA BLK_STATUS
    BNE @err
    CLC
    RTS
@err:
    SEC
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
_DOS_WRITE_FAT_ENTRY:
    JSR _DOS_FAT_SEEK
    BCS @err
    LDA DOS_ARG_VAL
    STA BLK_DATA                        ; overwrite entry low
    LDA DOS_ARG_VAL+1
    STA BLK_DATA                        ; overwrite entry high
    LDA #BLK_CMD_WRITE
    STA BLK_CMD                         ; flush the FAT sector (BLK_LBA unchanged)
    LDA BLK_STATUS
    BNE @err
    CLC
    RTS
@err:
    SEC
    RTS

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
    BNE @ok
    LDA DOS_F_CLUS
    CMP #<FAT_EOC
    BCS @err                            ; $FFF8..$FFFF -> EOC
@ok:
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
