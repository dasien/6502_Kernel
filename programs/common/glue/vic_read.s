; ============================================================================
; vic_read.s -- cc65 glue for the VIC register port
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; The 80x25 CP437 character and colour planes live inside the chip, not in the
; 64K map, which is why these are assembly rather than an array in C.
;
; Reading cells back. Used for the reverse-video read-modify-write and for
; snapshotting rows into scrollback, so far from universal.
; ============================================================================

.export _vgetc, _vgetcolor

.include "mfc.inc"

.PC02
.segment "CODE"

; unsigned char vgetc(void) -- read the glyph at the current cell (auto-inc).
.proc _vgetc
        lda     VREG_CHAR
        ldx     #$00
        rts
.endproc

; unsigned char vgetcolor(void) -- read the attribute at the current cell.
.proc _vgetcolor
        lda     VREG_COLOR
        ldx     #$00
        rts
.endproc
