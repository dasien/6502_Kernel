; ============================================================================
; kernel.s -- cc65 glue for the $FF00 kernel ABI
; ============================================================================
; Part of libmfcglue. Every program links the library; ld65 pulls a module
; only to resolve a symbol nothing else defines, so a program that never calls
; these does not carry them.
;
; _INCH and _QUITDOS are NOT here. Programs override those, and a module is
; pulled whole, so an overridable symbol sharing a module with one the program
; also needs would collide at link time. They each get a module of their own:
; inch.s and quitdos.s. The character-console pair is in console.s, because
; only the two ported text programs use it and everything else polls keys.
; ============================================================================

.export _INCH_NB, _jiffies

.include "mfc.inc"

.PC02
.segment "CODE"

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

; unsigned int jiffies(void) -- the 60 Hz tick counter (A=lo, X=hi).
.proc _jiffies
        jmp     K_GET_JIFFIES
.endproc
