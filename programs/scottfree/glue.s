; ============================================================================
; ScottFree on MFC-DOS -- cc65 runtime glue
; ============================================================================
; OUTCH, CLS, RND and the directory walk come from libmfcglue. What is left is
; the two routines this port needs to behave differently: INCH takes a raw
; unechoed key and treats ESC as quit, and QUITDOS leaves the screen alone
; rather than clearing it, because an adventure ends on text worth reading.
; ============================================================================

.export _INCH, _QUITDOS

.include "mfc.inc"

.PC02                           ; WDC 65C02, as the kernel, monitor and DOS declare.
                                ; Stated here as well as on the ca65 command line so
                                ; the file is right however it is assembled.

.segment "CODE"

; ---- char INCH(void) -- raw key in A (X=0); ESC exits to DOS ----------------
.proc _INCH
@wait:  jsr     K_GET_KEYSTROKE
        bcc     @wait
        cmp     #$1B            ; ESC -> back to the DOS prompt
        beq     _QUITDOS
        ldx     #$00
        rts
.endproc

; ---- void QUITDOS(void) ----------------------------------------------------
.proc _QUITDOS
        jsr     K_PRINT_NEWLINE
        jmp     DOS_WARM
.endproc
