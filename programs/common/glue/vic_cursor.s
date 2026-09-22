; ============================================================================
; vic_cursor.s -- cc65 glue for the VIC register port
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; The 80x25 CP437 character and colour planes live inside the chip, not in the
; 64K map, which is why these are assembly rather than an array in C.
;
; The displayed hardware cursor. A program drawing its own block cursor hides
; this one and never moves it again.
; ============================================================================

.export _vcursor, _vhidecur, _vshowcur

.include "mfc.inc"

.PC02
.segment "CODE"

; void vcursor(unsigned int cell) -- position the displayed cursor (A=lo, X=hi).
.proc _vcursor
        sta     VREG_CURSOR_LO
        stx     VREG_CURSOR_HI
        rts
.endproc

; void vhidecur(void) -- hide the hardware cursor, for a program drawing its
; own. Returning to DOS re-shows it on the next PRINT_CHAR.
.proc _vhidecur
        lda     #$80
        sta     VREG_CURSOR_HI
        rts
.endproc

; void vshowcur(unsigned int cell) -- park the hardware cursor on a cell and
; show it (A=lo, X=hi). For a program that hid it and now wants a real caret,
; while typing into a field.
.proc _vshowcur
        sta     VREG_CURSOR_LO
        stx     VREG_CURSOR_HI          ; bit7 clear -> visible
        rts
.endproc
