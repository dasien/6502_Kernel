; ============================================================================
; inch.s -- blocking key read
; ============================================================================
; Alone in its own module, not folded into kernel.s, because programs override
; it: reading a key is where input policy lives. micro-Max echoes the key,
; folds letters to lowercase and treats Q as quit; ScottFree quits on ESC.
; A module is pulled whole, so a symbol a program might replace has to sit by
; itself or the override collides with whatever else shared its module.
; ============================================================================

.export _INCH

.include "mfc.inc"

.PC02
.segment "CODE"

; char INCH(void) -- blocking key read; returns the key as typed (X=0).
.proc _INCH
@wait:  jsr     K_GET_KEYSTROKE
        bcc     @wait
        ldx     #$00
        rts
.endproc
