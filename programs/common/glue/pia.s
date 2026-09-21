; ============================================================================
; pia.s -- cc65 glue for the PIA's live key-state port
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
; ============================================================================

.export _keystate

.include "mfc.inc"

.PC02
.segment "CODE"

; unsigned char keystate(void) -- bitmask of the control keys currently held.
; bit0 up, bit1 down, bit2 left, bit3 right, bit4 fire (space), bit5 button2
; (left shift). Non-destructive: poll it every frame for as long as the key is
; down. Reads 0 when the host has no GUI (console build, headless tests).
.proc _keystate
        lda     KEY_STATE
        ldx     #$00
        rts
.endproc
