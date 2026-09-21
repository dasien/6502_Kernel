; ============================================================================
; KERNEL PANIC -- cc65 runtime glue
; ============================================================================
; The 80x25 screen lives behind the VIC register port (not in the 64K map), so
; all drawing goes through the video helpers: set the cell index with vaddr(),
; then stream glyphs with vputc() (the port auto-increments).
;
; Game-specific additions over the VAULT glue:
;   jiffies()    -- the kernel's 60 Hz monotonic tick counter (K_GET_JIFFIES,
;                   kernel v3.23). This is what paces the fixed-tick loop.
;   vscrollbot() -- set the scroll-region bottom row, so chip-side scrolls move
;                   the playfield and leave the HUD rows below it pinned.
; ============================================================================

.export _spr_sel, _spr_x, _spr_x_px, _spr_y, _spr_y_px, _spr_glyph, _spr_attr, _spr_on
.export _vfseek, _vfread, _vfwrite

.include "mfc.inc"

.PC02                           ; WDC 65C02, as the kernel, monitor and DOS declare.
                                ; Stated here as well as on the ca65 command line so
                                ; the file is right however it is assembled.

.segment "CODE"

; void vfseek(unsigned int index) -- point the font data port at a byte (A=lo, X=hi).
.proc _vfseek
        sta     VREG_FONT_LO
        stx     VREG_FONT_HI
        rts
.endproc

; unsigned char vfread(void) -- read a font byte; the port then advances.
.proc _vfread
        lda     VREG_FONT_DATA
        ldx     #$00
        rts
.endproc

; void vfwrite(unsigned char b) -- write a font byte; the port then advances.
.proc _vfwrite
        sta     VREG_FONT_DATA
        rts
.endproc

; void vscrollbot(unsigned char row) -- bound the scroll region to rows 0..row.
; NOTE: a clear command resets this to the full screen, so always set it AFTER
; clearing, never before.
; ---- sprites ------------------------------------------------------------
; A sprite is pixel-positioned and does NOT ride the scroll region, which is the whole
; reason these exist: anything in the cell plane rides the fine-scroll offset, so a
; screen-fixed object sawtooths by a cell on every scroll.
;
; Select-then-set, so every setter takes a SINGLE argument and therefore arrives in A
; with no C-stack handling at all. spr_sel() converts a sprite index into a byte offset
; once; the setters then index the register block with it. Positions are given in CELLS
; and converted here -- the chip wants nominal pixels on the 8x16 grid.
SPR_X_LO        = SPR0+0
SPR_X_HI        = SPR0+1        ; bits 1-0
SPR_Y_LO        = SPR0+2
SPR_Y_HI        = SPR0+3        ; bits 1-0 = y high, bit 7 = enable
SPR_GLYPH       = SPR0+4
SPR_ATTR        = SPR0+5

; void spr_sel(unsigned char index) -- offset = index * 6, so index*4 + index*2.
; 24 sprites max here, so the product always fits a byte.
.proc _spr_sel
        asl     a               ; index*2
        sta     spr_tmp
        asl     a               ; index*4
        clc
        adc     spr_tmp         ; index*6
        sta     spr_off
        rts
.endproc

; void spr_x(unsigned char cell_col) -- x = col * 8, ten bits wide
.proc _spr_x
        ldx     spr_off
        pha
        lda     #$00
        sta     spr_tmp         ; high bits accumulate here
        pla
        asl     a
        rol     spr_tmp
        asl     a
        rol     spr_tmp
        asl     a
        rol     spr_tmp
        sta     SPR_X_LO,x
        lda     spr_tmp
        and     #$03
        sta     spr_tmp
        ; Read-modify-write: bits 4-2 of this byte are the sprite's WIDTH and bit 5 is
        ; its X magnify. Storing the position alone would zero them. They read as 1x1
        ; unmagnified when zero, so KPANIC happens not to care -- but clobbering another
        ; program's size on a position write is the kind of thing that is very hard to
        ; find later.
        lda     SPR_X_HI,x
        and     #$FC
        ora     spr_tmp
        sta     SPR_X_HI,x
        rts
.endproc

; void spr_x_px(unsigned int px) -- X straight in nominal pixels, for an object that
; sits BETWEEN columns. cc65 passes a lone unsigned int in A/X, so Y is the register
; index here because X carries the argument's high byte. Same read-modify-write on the
; high byte as _spr_x: bits 4-2 are WIDTH and bit 5 is X magnify.
.proc _spr_x_px
        ldy     spr_off
        sta     SPR_X_LO,y
        txa
        and     #$03
        sta     spr_tmp
        lda     SPR_X_HI,y
        and     #$FC            ; keep WIDTH and X magnify
        ora     spr_tmp
        sta     SPR_X_HI,y
        rts
.endproc

; void spr_y(unsigned char cell_row) -- y = row * 16, nine bits wide. Preserves the
; enable bit so a caller can reposition without re-enabling.
.proc _spr_y
        ldx     spr_off
        pha
        lda     #$00
        sta     spr_tmp
        pla
        asl     a
        rol     spr_tmp
        asl     a
        rol     spr_tmp
        asl     a
        rol     spr_tmp
        asl     a
        rol     spr_tmp
        sta     SPR_Y_LO,x
        lda     spr_tmp
        and     #$03
        sta     spr_tmp
        lda     SPR_Y_HI,x
        and     #$FC            ; keep enable, Y magnify and HEIGHT
        ora     spr_tmp
        sta     SPR_Y_HI,x
        rts
.endproc

; void spr_y_px(unsigned int py) -- Y straight in nominal pixels, for an object that
; sits BETWEEN rows. cc65 passes a lone unsigned int in A/X. Uses Y as the register
; index because X carries the argument's high byte.
.proc _spr_y_px
        ldy     spr_off
        sta     SPR_Y_LO,y
        txa
        and     #$03
        sta     spr_tmp
        lda     SPR_Y_HI,y
        and     #$FC            ; keep enable, Y magnify and HEIGHT
        ora     spr_tmp
        sta     SPR_Y_HI,y
        rts
.endproc

.proc _spr_glyph
        ldx     spr_off
        sta     SPR_GLYPH,x
        rts
.endproc

.proc _spr_attr
        ldx     spr_off
        sta     SPR_ATTR,x
        rts
.endproc

; void spr_on(unsigned char enable) -- leaves the position alone
.proc _spr_on
        ldx     spr_off
        cmp     #$00
        beq     @off
        lda     SPR_Y_HI,x
        ora     #$80
        sta     SPR_Y_HI,x
        rts
@off:   lda     SPR_Y_HI,x
        and     #$7F
        sta     SPR_Y_HI,x
        rts
.endproc

.segment "BSS"
spr_off:        .res 1          ; selected sprite's byte offset into the block
spr_tmp:        .res 1
.segment "CODE"
