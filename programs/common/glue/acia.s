; ============================================================================
; acia.s -- cc65 glue for the 6551 ACIA (the serial line)
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; Polled, not interrupt-driven: the jiffy IRQ already owns the vector, and at
; the rates these programs run the poll keeps up.
; ============================================================================

.export _acia_init, _acia_get, _acia_put, _acia_carrier

.include "mfc.inc"

.PC02
.segment "CODE"

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

; unsigned char acia_carrier(void) -- 1 while /DCD says a call is up, else 0.
;
; Status bit 5 is ACTIVE LOW on a 6551: set means NO carrier. This is the only
; sound way to see a call end. "NO CARRIER" is a result code for a human, and
; matching it in the data stream breaks on any binary containing those bytes.
.proc _acia_carrier
        ldx     #$00
        lda     ACIA_STATUS
        and     #$20            ; bit 5: set = no carrier
        bne     @down
        lda     #$01
        rts
@down:  lda     #$00
        rts
.endproc
