; ============================================================================
; EDIT (kilo spike) -- cc65 runtime glue
; ============================================================================
;   char INCH(void)     -- blocking key read; returns the key AS TYPED (the
;                          kernel preserves case as of v3.9). X=0.
;   void QUITDOS(void)  -- return to the DOS ] prompt.
; The editor writes the 40x25 screen RAM at $0400 directly, so no output glue
; is needed here.
; ============================================================================

.export _INCH, _INCH_NB, _QUITDOS
.export _dopen_read, _dopen_write, _dgetb, _dputb, _dclose

K_GET_KEYSTROKE = $FF09         ; non-blocking: C set + A=char
K_CLEAR_SCREEN  = $FF0C         ; clear + home (also resets the kernel cursor)
FS_OPEN         = $AF03         ; A/X=name ptr, Y=mode(0 read/1 write); C=err
FS_GETB         = $AF06         ; -> C clear + A=byte, or C set = EOF
FS_PUTB         = $AF09         ; A=byte; C set = error
FS_CLOSE        = $AF0C
DOS_WARM        = $AF1E

.segment "CODE"

.proc _INCH
@wait:  jsr     K_GET_KEYSTROKE
        bcc     @wait
        ldx     #$00
        rts
.endproc

; int INCH_NB(void) -- non-blocking: next key 0..255, or -1 if none ready.
; Used to peek the rest of an ESC sequence without blocking on a bare ESC.
.proc _INCH_NB
        jsr     K_GET_KEYSTROKE
        bcs     @got
        lda     #$ff
        ldx     #$ff            ; -1
        rts
@got:   ldx     #$00            ; 0..255
        rts
.endproc

.proc _QUITDOS
        jsr     K_CLEAR_SCREEN  ; wipe the editor's screen + re-home the kernel
        jmp     DOS_WARM        ; cursor, so DOS resumes clean at the top-left ]
.endproc

; ---- DOS file I/O (same contract as the ScottFree port) --------------------
; char dopen_read(char *name)  -- A/X = name ptr; 0 = ok, 1 = error
.proc _dopen_read
        ldy     #$00
        jsr     FS_OPEN
        bcs     @err
        lda     #$00
        ldx     #$00
        rts
@err:   lda     #$01
        ldx     #$00
        rts
.endproc

; char dopen_write(char *name) -- A/X = name ptr; 0 = ok, 1 = error
.proc _dopen_write
        ldy     #$01
        jsr     FS_OPEN
        bcs     @err
        lda     #$00
        ldx     #$00
        rts
@err:   lda     #$01
        ldx     #$00
        rts
.endproc

; int dgetb(void) -- next byte 0..255, or -1 at EOF
.proc _dgetb
        jsr     FS_GETB
        bcs     @eof
        ldx     #$00
        rts
@eof:   lda     #$ff
        ldx     #$ff
        rts
.endproc

; char dputb(char c) -- A = byte; 0 = ok, 1 = error
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
        jmp     FS_CLOSE
.endproc
