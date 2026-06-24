; ============================================================================
; micro-Max on MFC-DOS -- cc65 runtime glue
; ============================================================================
; The compiled engine (umax_mfc.c) imports exactly three functions; we map
; them onto the kernel character-console ABI:
;   void OUTCH(char c)  -- print a char  (LF -> newline)
;   char INCH(void)     -- blocking read, echoed (RETURN -> LF; Q/ESC -> quit)
;   int  RND(void)      -- 16-bit Galois LFSR pseudo-random number
; cc65 convention: the single char arg arrives in A; results return in A (char)
; or A/X (int).
; ============================================================================

.export _OUTCH, _INCH, _RND, _CLS

K_PRINT_CHAR    = $FF00         ; A = char -> screen
K_PRINT_NEWLINE = $FF06         ; CR/LF
K_GET_KEYSTROKE = $FF09         ; non-blocking: C set + A=char (uppercased)
K_CLEAR_SCREEN  = $FF0C         ; clear screen + home cursor
DOS_WARM        = $AF1E         ; clean exit back to the DOS ] prompt

.segment "DATA"
rndseed:        .word   $ACE1   ; nonzero LFSR seed (DATA = loaded into RAM)

.segment "CODE"

; ---- void OUTCH(char c) -- c in A ------------------------------------------
.proc _OUTCH
        cmp     #10             ; engine prints LF for newline
        bne     @ch
        jmp     K_PRINT_NEWLINE ; tail call: its RTS returns to the C caller
@ch:    jmp     K_PRINT_CHAR
.endproc

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

; ---- void CLS(void) -- clear screen + home cursor (for the strobe redraw) --
.proc _CLS
        jmp     K_CLEAR_SCREEN  ; tail call: its RTS returns to the C caller
.endproc

; ---- int RND(void) -- 16-bit Galois LFSR (poly $B400), returns A/X ----------
.proc _RND
        lsr     rndseed+1
        ror     rndseed
        bcc     @nofb
        lda     rndseed+1
        eor     #$B4
        sta     rndseed+1
@nofb:  lda     rndseed
        ldx     rndseed+1
        rts
.endproc
