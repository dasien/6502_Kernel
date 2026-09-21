; ============================================================================
; file.s -- cc65 glue for the DOS FAT16 byte-stream calls
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; The DOS reports failure in carry. These translate that to cc65's convention:
; 0 for success and 1 for failure, or -1 for end of file where a byte is
; expected. One file at a time -- the DOS has a single open-file state.
; ============================================================================

.export _dopen_read, _dopen_write, _dgetb, _dputb, _dclose

.include "mfc.inc"

.PC02
.segment "CODE"

; char dopen_read(char *name) -- A/X = name ptr; 0 = ok, 1 = error
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

; char dclose(void) -- 0 = ok, 1 = error
;
; FS_CLOSE sets carry if the final flush or the directory-entry finalize
; failed, which is how a full disk surfaces on the last sector of a transfer.
; Callers declaring this void simply ignore A.
.proc _dclose
        jsr     FS_CLOSE
        bcs     @err
        lda     #$00
        ldx     #$00
        rts
@err:   lda     #$01
        ldx     #$00
        rts
.endproc
