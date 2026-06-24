; ============================================================================
; ScottFree on MFC-DOS -- cc65 runtime glue
; ============================================================================
;   void OUTCH(char c)  -- print a char (LF -> newline)
;   char INCH(void)     -- blocking raw key (ESC -> quit to DOS); no echo
;   void CLS(void)      -- clear screen + home
;   int  RND(void)      -- 16-bit Galois LFSR
;   void QUITDOS(void)  -- return to the DOS ] prompt
; cc65 convention: char arg in A; char/int result in A / A:X.
; ============================================================================

.export _OUTCH, _INCH, _CLS, _RND, _QUITDOS

K_PRINT_CHAR    = $FF00
K_PRINT_NEWLINE = $FF06
K_GET_KEYSTROKE = $FF09         ; non-blocking: C set + A=char (uppercased)
K_CLEAR_SCREEN  = $FF0C
DOS_WARM        = $AF1E

.segment "DATA"
rndseed:        .word   $C0DE

.segment "CODE"

; ---- void OUTCH(char c) -- c in A ------------------------------------------
.proc _OUTCH
        cmp     #10             ; engine emits LF for newline
        bne     @ch
        jmp     K_PRINT_NEWLINE
@ch:    jmp     K_PRINT_CHAR
.endproc

; ---- char INCH(void) -- raw key in A (X=0); ESC exits to DOS ----------------
.proc _INCH
@wait:  jsr     K_GET_KEYSTROKE
        bcc     @wait
        cmp     #$1B            ; ESC -> back to the DOS prompt
        beq     _QUITDOS
        ldx     #$00
        rts
.endproc

; ---- void CLS(void) --------------------------------------------------------
.proc _CLS
        jmp     K_CLEAR_SCREEN
.endproc

; ---- void QUITDOS(void) ----------------------------------------------------
.proc _QUITDOS
        jsr     K_PRINT_NEWLINE
        jmp     DOS_WARM
.endproc

; ---- int RND(void) -- 16-bit Galois LFSR (poly $B400), returns A:X ----------
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
