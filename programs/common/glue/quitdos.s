; ============================================================================
; quitdos.s -- return to the DOS prompt
; ============================================================================
; Alone in its own module for the reason given in inch.s. ScottFree overrides
; it: an adventure ends with text on screen worth reading, so it prints a
; newline and leaves the screen alone instead of clearing it.
; ============================================================================

.export _QUITDOS

.include "mfc.inc"

.PC02
.segment "CODE"

; void QUITDOS(void) -- clear the screen and return to the DOS ] prompt.
.proc _QUITDOS
        jsr     K_CLEAR_SCREEN
        jmp     DOS_WARM
.endproc
