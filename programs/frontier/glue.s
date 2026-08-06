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

.export _INCH, _INCH_NB, _QUITDOS
.export _vaddr, _vputc, _vattr
.export _vfill, _vcmd, _vhidecur, _vshowcur
.export _rng_seed
.export _dopen_read, _dopen_write, _dgetb, _dputb, _dclose

K_GET_KEYSTROKE = $FF09         ; non-blocking: C set + A=char
K_CLEAR_SCREEN  = $FF0C         ; clear + home (also resets the kernel cursor)

; MFC-DOS byte-stream file I/O -- the same contract the ScottFree port and VAULT
; use for saves. Files land in whatever drawer is current, so a score table
; written from GAMES/ stays beside the game.
FS_OPEN         = $AF03         ; A/X=name ptr, Y=mode(0 read/1 write); C=err
FS_GETB         = $AF06         ; -> C clear + A=byte, or C set = EOF
FS_PUTB         = $AF09         ; A=byte; C set = error
FS_CLOSE        = $AF0C
DOS_WARM        = $AF1E

; VIC video register port (chars/colors live behind these, not in the map).
VREG_ADDR_LO    = $FE2D         ; cell index low (0..1999)
VREG_ADDR_HI    = $FE2E         ; cell index high
VREG_CHAR       = $FE2F         ; char data port; full 8-bit glyph; auto-increments
VREG_ATTR       = $FE31         ; attribute latch [R][BR][bg:3][fg:3]
VREG_CMD        = $FE32         ; 1=clear 2=scroll-up 3=scroll-down 4=fill-row
VREG_CURSOR_LO  = $FE34         ; hardware cursor cell low
VREG_CURSOR_HI  = $FE35         ; cursor cell high; bit7 = hidden
VREG_CMD_PARAM  = $FE36         ; fill char for clear / fill-row

; RTC -- entropy for the price rolls.
RTC_LATCH       = $FE55
RTC_SEC         = $FE56
RTC_MIN         = $FE57
RTC_HOUR        = $FE58
RTC_FATTIME_LO  = $FE5D
RTC_FATTIME_HI  = $FE5E

.segment "CODE"

; void vaddr(unsigned int cell) -- point the data port at a cell (A=lo, X=hi).
.proc _vaddr
        sta     VREG_ADDR_LO
        stx     VREG_ADDR_HI
        rts
.endproc

; void vattr(unsigned char a) -- set the colour/attribute latch for next writes.
.proc _vattr
        sta     VREG_ATTR
        rts
.endproc

; void vputc(unsigned char ch) -- write a glyph at the current cell and advance.
.proc _vputc
        sta     VREG_CHAR
        rts
.endproc

; void vfill(unsigned char ch) -- set the fill char used by clear / fill-row.
.proc _vfill
        sta     VREG_CMD_PARAM
        rts
.endproc

; void vcmd(unsigned char cmd) -- run a chip-side block op (clear/scroll/fill-row).
.proc _vcmd
        sta     VREG_CMD
        rts
.endproc

; void vhidecur(void) -- hide the kernel hardware cursor while a screen is up.
.proc _vhidecur
        lda     #$80
        sta     VREG_CURSOR_HI
        rts
.endproc

; void vshowcur(unsigned int cell) -- park the hardware cursor on a cell and show
; it (A=lo, X=hi). Used while typing an amount, so there is a real caret to aim at
; instead of the game faking one.
.proc _vshowcur
        sta     VREG_CURSOR_LO
        stx     VREG_CURSOR_HI          ; bit7 clear -> visible
        rts
.endproc

; unsigned char INCH(void) -- blocking key read; returns the key as typed.
.proc _INCH
@wait:  jsr     K_GET_KEYSTROKE
        bcc     @wait
        ldx     #$00
        rts
.endproc

; int INCH_NB(void) -- non-blocking: next key 0..255, or -1 if none ready.
.proc _INCH_NB
        jsr     K_GET_KEYSTROKE
        bcs     @got
        lda     #$ff
        ldx     #$ff                    ; -1
        rts
@got:   ldx     #$00
        rts
.endproc

; unsigned int rng_seed(void) -- latch the RTC and fold seconds/minutes/hours plus
; the packed FAT-time bits into a 16-bit seed (A=lo, X=hi). Varies per launch; the
; caller guards against a zero result.
.proc _rng_seed
        sta     RTC_LATCH               ; snapshot the live clock (value irrelevant)
        lda     RTC_SEC
        eor     RTC_FATTIME_LO
        eor     #$5B
        pha                             ; -> low byte
        lda     RTC_MIN
        eor     RTC_HOUR
        eor     RTC_FATTIME_HI
        eor     #$C4
        tax                             ; -> high byte
        pla                             ; low byte back into A
        rts
.endproc

; ---- DOS file I/O (same contract as the ScottFree port and VAULT) ----------
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

.proc _QUITDOS
        jsr     K_CLEAR_SCREEN          ; wipe our screen + re-home the kernel cursor
        jmp     DOS_WARM
.endproc
