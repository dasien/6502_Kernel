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
.export _dopen_read, _dopen_write, _dgetb, _dputb, _dclose
.export _dir_first, _dir_next
.importzp ptr1                  ; cc65 zero-page scratch (above the DOS's $14-$3F)

K_PRINT_CHAR    = $FF00
K_PRINT_NEWLINE = $FF06
K_GET_KEYSTROKE = $FF09         ; non-blocking: C set + A=char (uppercased)
K_CLEAR_SCREEN  = $FF0C
FS_OPEN         = $AF03         ; A/X=name ptr, Y=mode(0 read/1 write); C=err
FS_GETB         = $AF06         ; -> C clear + A=byte, or C set = EOF
FS_PUTB         = $AF09         ; A=byte; C set = error
FS_CLOSE        = $AF0C
FS_DIR_FIRST    = $AF0F         ; -> C clear + DOS_ENTRY filled, or C set = none
FS_DIR_NEXT     = $AF12
DOS_WARM        = $AF1E
DOS_ENTRY       = $0320         ; 32-byte current dir entry (name = first 11 bytes)

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

; ---- file I/O over the DOS FS (saves live on the mounted disk) -------------
; char dopen_read(char *name)  -- A/X = name ptr; returns 0 ok / 1 error
.proc _dopen_read
        ldy     #$00            ; mode 0 = read (A/X already = name ptr)
        jsr     FS_OPEN
        bcs     @err
        lda     #$00
        ldx     #$00
        rts
@err:   lda     #$01
        ldx     #$00
        rts
.endproc

; char dopen_write(char *name) -- A/X = name ptr; returns 0 ok / 1 error
.proc _dopen_write
        ldy     #$01            ; mode 1 = write (create/truncate)
        jsr     FS_OPEN
        bcs     @err
        lda     #$00
        ldx     #$00
        rts
@err:   lda     #$01
        ldx     #$00
        rts
.endproc

; int dgetb(void) -- returns next byte 0..255, or -1 at EOF
.proc _dgetb
        jsr     FS_GETB
        bcs     @eof
        ldx     #$00            ; A = byte, X = 0  -> 0..255
        rts
@eof:   lda     #$ff
        ldx     #$ff            ; -1
        rts
.endproc

; char dputb(char c) -- A = byte; returns 0 ok / 1 error
.proc _dputb
        jsr     FS_PUTB
        bcs     @err
        lda     #$00
        ldx     #$00
        rts
@err:   lda     #$01
        ldx     #$00
        rts
.endproc

; void dclose(void)
.proc _dclose
        jmp     FS_CLOSE        ; tail call (its RTS returns to the C caller)
.endproc

; ---- char dir_first(char *dst11) / char dir_next(char *dst11) --------------
; Enumerate root-dir entries: copy the 11-byte 8.3 name into dst; returns
; 0 = got an entry, 1 = no more. Run to completion before opening a file.
_dir_first:
        sta     ptr1
        stx     ptr1+1
        jsr     FS_DIR_FIRST
        jmp     dir_copy
_dir_next:
        sta     ptr1
        stx     ptr1+1
        jsr     FS_DIR_NEXT
dir_copy:
        bcs     dir_none
        ldy     #$0A            ; copy DOS_ENTRY[0..10] -> (ptr1)
@cp:    lda     DOS_ENTRY,y
        sta     (ptr1),y
        dey
        bpl     @cp
        lda     #$00
        ldx     #$00
        rts
dir_none:
        lda     #$01
        ldx     #$00
        rts

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
