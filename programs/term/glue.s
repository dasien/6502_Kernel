; ============================================================================
; TERM (serial ANSI terminal) -- cc65 runtime glue
; ============================================================================
; Bridges the C terminal to the kernel + the VIC video port + the 6551 ACIA.
;   INCH/INCH_NB/QUITDOS  -- key input + return to DOS (as in EDIT).
;   vaddr/vputc/vattr      -- drive the VIC port: set the cell index, write a
;                             glyph (auto-increments), set the color latch.
;   vcursor                -- position the displayed hardware cursor.
;   vfill/vcmd             -- chip-side block ops (clear / scroll / fill-row).
;   acia_init/acia_get/acia_put -- polled 6551 driver (the serial line).
; ============================================================================

.export _INCH, _INCH_NB, _QUITDOS
.export _vaddr, _vputc, _vattr, _vcursor, _vfill, _vcmd
.export _acia_init, _acia_get, _acia_put

K_GET_KEYSTROKE = $FF09         ; non-blocking: C set + A=char
K_CLEAR_SCREEN  = $FF0C         ; clear + home (resets the kernel cursor)
DOS_WARM        = $AF1E         ; DOS shell re-entry

; VIC video register port (the 80x25 screen lives behind these).
VREG_ADDR_LO    = $FE2D
VREG_ADDR_HI    = $FE2E
VREG_CHAR       = $FE2F         ; bit7 => reverse; auto-increments the cell index
VREG_ATTR       = $FE31         ; color/attribute latch [R][BR][bg:3][fg:3]
VREG_CMD        = $FE32         ; 1=clear 2=scroll-up 3=scroll-down 4=fill-row
VREG_CURSOR_LO  = $FE34
VREG_CURSOR_HI  = $FE35         ; bit7 = cursor hidden
VREG_CMD_PARAM  = $FE36         ; fill char for commands

; 6551 ACIA registers.
ACIA_DATA       = $FE29
ACIA_STATUS     = $FE2A
ACIA_COMMAND    = $FE2B
ACIA_CONTROL    = $FE2C

.segment "CODE"

; char INCH(void) -- blocking key read; returns the key as typed (X=0).
.proc _INCH
@wait:  jsr     K_GET_KEYSTROKE
        bcc     @wait
        ldx     #$00
        rts
.endproc

; int INCH_NB(void) -- non-blocking: next key 0..255, or -1 if none.
.proc _INCH_NB
        jsr     K_GET_KEYSTROKE
        bcs     @got
        lda     #$ff
        ldx     #$ff
        rts
@got:   ldx     #$00
        rts
.endproc

; void QUITDOS(void) -- clear the screen and return to the DOS ] prompt.
.proc _QUITDOS
        jsr     K_CLEAR_SCREEN
        jmp     DOS_WARM
.endproc

; void vaddr(unsigned int cell) -- point the data port at a cell (A=lo, X=hi).
.proc _vaddr
        sta     VREG_ADDR_LO
        stx     VREG_ADDR_HI
        rts
.endproc

; void vputc(unsigned char ch) -- write a glyph at the current cell (auto-inc).
.proc _vputc
        sta     VREG_CHAR
        rts
.endproc

; void vattr(unsigned char a) -- set the color/attribute latch for next writes.
.proc _vattr
        sta     VREG_ATTR
        rts
.endproc

; void vcursor(unsigned int cell) -- position the displayed cursor (A=lo, X=hi).
.proc _vcursor
        sta     VREG_CURSOR_LO
        stx     VREG_CURSOR_HI
        rts
.endproc

; void vfill(unsigned char ch) -- set the fill char for the next chip command.
.proc _vfill
        sta     VREG_CMD_PARAM
        rts
.endproc

; void vcmd(unsigned char cmd) -- run a chip-side block op (clear/scroll/fill).
.proc _vcmd
        sta     VREG_CMD
        rts
.endproc

; void acia_init(void) -- 19.2k/8N1, no parity, RX IRQ off, DTR active.
.proc _acia_init
        lda     #$1f
        sta     ACIA_CONTROL
        lda     #$0b
        sta     ACIA_COMMAND
        rts
.endproc

; int acia_get(void) -- non-blocking: next received byte 0..255, or -1 if none.
.proc _acia_get
        lda     ACIA_STATUS
        and     #$08            ; receiver full?
        beq     @none
        lda     ACIA_DATA
        ldx     #$00
        rts
@none:  lda     #$ff
        ldx     #$ff
        rts
.endproc

; void acia_put(unsigned char c) -- transmit a byte (wait for TX-empty).
.proc _acia_put
        pha
@wait:  lda     ACIA_STATUS
        and     #$10            ; transmitter empty?
        beq     @wait
        pla
        sta     ACIA_DATA
        rts
.endproc
