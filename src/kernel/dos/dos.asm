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
; DOS zero page (the free $3A-$5A gap; clear of monitor $14-$39,
; BASIC $00-$13/$5B-$FF, and dev-tools $C0-$DF)
; ----------------------------------------------------------------
BLK_BUF_PTR      = $3A                  ; $3A-$3B: caller's 512-byte sector buffer

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
; FILESYSTEM ABI - STUBS (implemented in step 2.3)
; ================================================================
; FAT16 mount, directory walk, and cluster-chain read are not implemented yet.
; Each stub reports failure (carry set). FS_GETB's contract is carry = EOF, so a
; carry-set stub reads as "immediate EOF" until the real driver lands.
_FS_OPEN:
_FS_GETB:
_FS_PUTB:
_FS_CLOSE:
_FS_DIR_FIRST:
_FS_DIR_NEXT:
    SEC
    RTS

; ----------------------------------------------------------------
; _DOS_COLD - DOS shell cold entry (placeholder; wired in phase 4)
; ----------------------------------------------------------------
_DOS_COLD:
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
