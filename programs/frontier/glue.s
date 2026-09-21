; ============================================================================
; FRONTIER FORTUNE -- cc65 runtime glue
; ============================================================================
; A menu-driven, turn-based game: it owns the 80x25 screen and paints positioned,
; coloured panels through the VIC register port, exactly as VAULT does. It does
; NOT use the kernel's PRINT_CHAR path, because that writes at the kernel cursor
; and would fight with positioned output; for the same reason it does not use
; K_READ_LINE or K_PRINT_DEC, and formats numbers itself.
;
; It also does NOT use the $FE0F control port -- that is for real-time programs
; that need to know a key is held. Here a keystroke is exactly the right model.
; ============================================================================

.export _vshowcur

.include "mfc.inc"

.PC02                           ; WDC 65C02, as the kernel, monitor and DOS declare.
                                ; Stated here as well as on the ca65 command line so
                                ; the file is right however it is assembled.

.segment "CODE"

; void vshowcur(unsigned int cell) -- park the hardware cursor on a cell and show
; it (A=lo, X=hi). Used while typing an amount, so there is a real caret to aim at
; instead of the game faking one.
.proc _vshowcur
        sta     VREG_CURSOR_LO
        stx     VREG_CURSOR_HI          ; bit7 clear -> visible
        rts
.endproc
