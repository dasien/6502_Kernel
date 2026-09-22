; ============================================================================
; palette.s -- cc65 glue for the VIC soft-palette port
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; Sixteen colour slots of three bytes each, reached through an index/data pair
; exactly like the soft font. Every attribute byte names SLOTS rather than
; colours, so these decide what the whole machine looks like.
;
; A program only touches this if it has an opinion about its colours. Load them
; on entry and stop there: the DOS reloads the theme when it takes the screen
; back, so there is nothing to restore on the way out. A program with no opinion
; inherits whatever is loaded, which is what you want from anything that is
; mostly text.
;
; The index counts BYTES, so slot n starts at n*3 and both data calls advance
; it -- a slot is a seek followed by three writes.
; ============================================================================

.export _vpseek, _vpread, _vpwrite

.include "mfc.inc"

.PC02
.segment "CODE"

; void vpseek(unsigned char byte_index) -- point the palette data port at a byte.
.proc _vpseek
        sta     VREG_PAL_IDX
        rts
.endproc

; unsigned char vpread(void) -- read a palette byte; the port then advances.
.proc _vpread
        lda     VREG_PAL_DATA
        ldx     #$00
        rts
.endproc

; void vpwrite(unsigned char b) -- write a palette byte; the port then advances.
.proc _vpwrite
        sta     VREG_PAL_DATA
        rts
.endproc
