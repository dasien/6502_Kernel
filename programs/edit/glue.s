; ============================================================================
; EDIT (kilo spike) -- cc65 runtime glue
; ============================================================================
;   char INCH(void)     -- blocking key read; returns the key AS TYPED (the
;                          kernel preserves case as of v3.9). X=0.
;   void QUITDOS(void)  -- return to the DOS ] prompt.
; The 80x25 screen lives behind the VIC register port (not in the 64K map), so
; the editor renders through the video helpers below: set the cell index with
; vaddr(), then stream glyphs with vputc() (the port auto-increments). vgetc()
; reads a cell back (for the reverse-video read-modify-write), and vhidecur()
; hides the kernel's hardware cursor so only the editor's block cursor shows.
; ============================================================================

.export _INCH, _INCH_NB, _QUITDOS
.export _jiffies, _vfill, _vcmd
.export _dopen_read, _dopen_write, _dgetb, _dputb, _dclose
.export _vaddr, _vputc, _vgetc, _vhidecur
.export _vattr, _vgetcolor, _vputcolor

K_GET_KEYSTROKE = $FF09         ; non-blocking: C set + A=char
K_CLEAR_SCREEN  = $FF0C         ; clear + home (also resets the kernel cursor)
FS_OPEN         = $AF03         ; A/X=name ptr, Y=mode(0 read/1 write); C=err
FS_GETB         = $AF06         ; -> C clear + A=byte, or C set = EOF
FS_PUTB         = $AF09         ; A=byte; C set = error
FS_CLOSE        = $AF0C
DOS_WARM        = $AF1E

; VIC video register port (chars/colors live behind these, not in the map).
VREG_ADDR_LO    = $FE2D         ; cell index low (0..1999)
VREG_ADDR_HI    = $FE2E         ; cell index high
VREG_CHAR       = $FE2F         ; char data port; full 8-bit glyph; auto-increments
VREG_COLOR      = $FE30         ; color/attribute data port; auto-increments
VREG_ATTR       = $FE31         ; attribute latch [R][BR][bg:3][fg:3]
VREG_CMD        = $FE32         ; command engine (1 = clear screen)
VREG_CMD_PARAM  = $FE36         ; fill char for the clear
VREG_CURSOR_HI  = $FE35         ; cursor cell high; bit7 = hidden
K_GET_JIFFIES   = $FF39         ; 60 Hz monotonic counter -> A=lo, X=hi

.segment "CODE"

; void vaddr(unsigned int cell) -- point the data port at a cell (A=lo, X=hi).
.proc _vaddr
        sta     VREG_ADDR_LO
        stx     VREG_ADDR_HI
        rts
.endproc

; unsigned int jiffies(void) -- the 60 Hz tick counter, for the title card's timeout.
.proc _jiffies
        jmp     K_GET_JIFFIES
.endproc

; void vfill(unsigned char ch) -- the fill character a chip-side clear will use.
.proc _vfill
        sta     VREG_CMD_PARAM
        rts
.endproc

; void vcmd(unsigned char cmd) -- run a chip-side block operation.
.proc _vcmd
        sta     VREG_CMD
        rts
.endproc

; void vattr(unsigned char a) -- set the color/attribute latch for next writes.
.proc _vattr
        sta     VREG_ATTR
        rts
.endproc

; unsigned char vgetcolor(void) -- read the attribute at the current cell (X=0).
.proc _vgetcolor
        lda     VREG_COLOR
        ldx     #$00
        rts
.endproc

; void vputcolor(unsigned char a) -- write the attribute at the current cell.
.proc _vputcolor
        sta     VREG_COLOR
        rts
.endproc

; void vputc(unsigned char ch) -- write a glyph at the current cell (A=ch). bit7
; set means reverse video; the port stores the 7-bit glyph + reverse attribute
; and auto-advances the cell index, so a run can be streamed after one vaddr().
.proc _vputc
        sta     VREG_CHAR
        rts
.endproc

; unsigned char vgetc(void) -- read the glyph at the current cell (-> A, X=0);
; advances the cell index. Used for the reverse-video read-modify-write.
.proc _vgetc
        lda     VREG_CHAR
        ldx     #$00
        rts
.endproc

; void vhidecur(void) -- hide the kernel hardware cursor (the editor draws its
; own reverse-video block cursor). Returning to DOS re-shows it on the next
; PRINT_CHAR (UPDATE_CURSOR clears the hidden bit).
.proc _vhidecur
        lda     #$80
        sta     VREG_CURSOR_HI
        rts
.endproc

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

; char dclose(void) -- 0 = ok, 1 = error
; FS_CLOSE returns carry set if the flush or the directory-entry finalize failed
; (e.g. the volume filled up). Discarding that let a truncated save report success.
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
