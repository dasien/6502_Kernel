; ============================================================================
; sprite.s -- cc65 glue for the VIC sprite block
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; A sprite is pixel-positioned and does NOT ride the scroll region, which is the whole
; reason these exist: anything in the cell plane rides the fine-scroll offset, so a
; screen-fixed object sawtooths by a cell on every scroll.
;
; Select-then-set, so every setter takes a SINGLE argument and therefore arrives in A
; with no C-stack handling at all. spr_sel() converts a sprite index into a byte offset
; once; the setters then index the register block with it. Positions are given in CELLS
; and converted here -- the chip wants nominal pixels on the 8x16 grid.
; ============================================================================

.export _spr_sel, _spr_x, _spr_x_px, _spr_y, _spr_y_px
.export _spr_glyph, _spr_attr, _spr_on
.export _spr_w, _spr_h, _spr_mag
.export spr_off                 ; for sprimg.s, which sets the bitmap bit

.include "mfc.inc"

.PC02
.segment "CODE"

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

; void spr_w(unsigned char cells) / spr_h(unsigned char cells) -- size, 1..8. Glyph
; sprites count 8x16 cells, bitmap sprites 16x16 slots; either way the chip draws
; consecutive codes or slots from the base, row-major. Bits 4-2 of each high byte,
; stored as size-1; the position, magnify and enable bits are kept.
.proc _spr_w
        jsr     size_bits
        ldx     spr_off
        lda     SPR_X_HI,x
        and     #$E3
        ora     spr_tmp
        sta     SPR_X_HI,x
        rts
.endproc

.proc _spr_h
        jsr     size_bits
        ldx     spr_off
        lda     SPR_Y_HI,x
        and     #$E3
        ora     spr_tmp
        sta     SPR_Y_HI,x
        rts
.endproc

; A = size 1..8 -> spr_tmp = (size-1) << 2, confined to bits 4-2.
.proc size_bits
        dec     a
        and     #$07
        asl     a
        asl     a
        sta     spr_tmp
        rts
.endproc

; void spr_mag(unsigned char axes) -- bit 0 doubles X, bit 1 doubles Y. Bit 5 of the
; matching high byte; everything else in both bytes is kept.
.proc _spr_mag
        ldx     spr_off
        sta     spr_tmp
        lda     SPR_X_HI,x
        and     #$DF
        lsr     spr_tmp         ; bit 0 -> carry
        bcc     :+
        ora     #$20
:       sta     SPR_X_HI,x
        lda     SPR_Y_HI,x
        and     #$DF
        lsr     spr_tmp         ; bit 1 -> carry
        bcc     :+
        ora     #$20
:       sta     SPR_Y_HI,x
        rts
.endproc

.segment "BSS"
spr_off:        .res 1          ; selected sprite's byte offset into the block
spr_tmp:        .res 1
.segment "CODE"
