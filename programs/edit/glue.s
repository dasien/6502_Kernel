; ============================================================================
; EDIT (kilo spike) -- cc65 runtime glue
; ============================================================================
;   char INCH(void)     -- blocking key read; returns the key AS TYPED (the
;                          kernel preserves case as of v3.9). X=0.
;   void QUITDOS(void)  -- return to the DOS ] prompt.
; The editor writes the 40x25 screen RAM at $0400 directly, so no output glue
; is needed here.
; ============================================================================

.export _INCH, _QUITDOS

K_GET_KEYSTROKE = $FF09         ; non-blocking: C set + A=char
DOS_WARM        = $AF1E

.segment "CODE"

.proc _INCH
@wait:  jsr     K_GET_KEYSTROKE
        bcc     @wait
        ldx     #$00
        rts
.endproc

.proc _QUITDOS
        jmp     DOS_WARM
.endproc
