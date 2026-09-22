; ============================================================================
; micro-Max on MFC-DOS -- cc65 runtime glue
; ============================================================================
; OUTCH, CLS, SETATTR and RND come from libmfcglue. Only INCH is here, because
; reading a key is where this port's input policy lives: the engine wants a
; lowercase file letter, an echoed keystroke, LF rather than CR at end of line,
; and Q or ESC to leave. None of that belongs in a shared routine.
; ============================================================================

.export _INCH

.include "mfc.inc"

.PC02                           ; WDC 65C02, as the kernel, monitor and DOS declare.
                                ; Stated here as well as on the ca65 command line so
                                ; the file is right however it is assembled.

.segment "CODE"

; ---- char INCH(void) -- returns char in A (X=0) ----------------------------
.proc _INCH
@wait:  jsr     K_GET_KEYSTROKE ; carry set + A=char when a key is ready
        bcc     @wait
        cmp     #'Q'            ; quit to DOS (the kernel preserves case now,
        beq     @quit           ; so accept both 'Q' and 'q')
        cmp     #'q'
        beq     @quit
        cmp     #$1B            ; ESC also quits
        beq     @quit
        cmp     #$08            ; backspace: hand back raw, do NOT echo here --
        beq     @edit           ; the C reader decides whether a char remains to
        cmp     #$7F            ; erase (delete key maps to backspace too)
        beq     @edit
        cmp     #'A'            ; micro-Max wants lowercase file letters a-h;
        bcc     @nolc           ; fold any uppercase letter the player typed
        cmp     #'Z'+1
        bcs     @nolc
        ora     #$20            ; A-Z -> a-z
@nolc:
        pha
        jsr     K_PRINT_CHAR    ; echo the keystroke
        pla
        cmp     #$0D            ; RETURN -> LF (engine reads a line until <=10)
        bne     @done
        lda     #10
@done:  ldx     #$00
        rts
@edit:  lda     #$08            ; normalized backspace, unechoed
        ldx     #$00
        rts
@quit:  jsr     K_PRINT_NEWLINE ; drop to a fresh line so the returning ] prompt
        jmp     DOS_WARM        ; isn't jammed against the board / current line
.endproc
