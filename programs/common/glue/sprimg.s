; ============================================================================
; sprimg.s -- cc65 glue for the VIC sprite pattern RAM
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; A bitmap sprite takes its picture from pattern RAM instead of the font: 256
; slots of 16x16 pixels at 4 bits a pixel, inside the chip and reached through an
; index/data pair exactly like the soft font. A slot is 128 bytes, so loading one
; is a seek and 128 writes -- do it once, at start-up, and animate by pointing a
; sprite at a different slot, which is a single write.
;
;     spr_img_seek(SLOT_HERO); spr_img_load(hero_art);    /* once */
;     spr_sel(0); spr_glyph(SLOT_HERO); spr_bitmap(1);    /* then as a sprite */
;
; Two pixels a byte, the LEFT one in the high nibble, eight bytes a row, rows top
; to bottom. 0 is transparent; 1-15 are palette slots.
; ============================================================================

.export _spr_img_seek, _spr_img_read, _spr_img_write, _spr_img_load, _spr_bitmap
.import spr_off                 ; sprite.s: the selected sprite's register offset
.importzp ptr1                  ; cc65 zero-page scratch

.include "mfc.inc"

.PC02
.segment "CODE"

; void spr_img_seek(unsigned char slot) -- point the data port at a slot's first
; byte. slot*128 is the slot shifted right one into the high byte, with its low
; bit becoming bit 7 of the low byte.
.proc _spr_img_seek
        lsr     a
        tax                     ; high byte = slot >> 1
        lda     #$00
        ror     a               ; low byte = (slot & 1) << 7
        sta     VREG_SPRPAT_LO
        stx     VREG_SPRPAT_HI
        rts
.endproc

; unsigned char spr_img_read(void) -- read a pattern byte; the port then advances.
.proc _spr_img_read
        lda     VREG_SPRPAT_DATA
        ldx     #$00
        rts
.endproc

; void spr_img_write(unsigned char b) -- write a pattern byte; the port then advances.
.proc _spr_img_write
        sta     VREG_SPRPAT_DATA
        rts
.endproc

; void spr_img_load(const unsigned char *src) -- stream one whole slot, 128 bytes,
; from src into the port at wherever it points. After a seek that is one slot; a
; second call carries on into the next, since the port keeps advancing.
.proc _spr_img_load
        sta     ptr1
        stx     ptr1+1
        ldy     #$00
@loop:  lda     (ptr1),y
        sta     VREG_SPRPAT_DATA
        iny
        bpl     @loop           ; 0..127
        rts
.endproc

; void spr_bitmap(unsigned char on) -- the selected sprite shows a pattern slot
; (on) or a glyph (off). Bit 6 of the Y high byte; the rest of it is kept.
.proc _spr_bitmap
        ldx     spr_off
        cmp     #$00
        beq     @off
        lda     SPR0+3,x
        ora     #SPR_BITMAP
        sta     SPR0+3,x
        rts
@off:   lda     SPR0+3,x
        and     #<~SPR_BITMAP
        sta     SPR0+3,x
        rts
.endproc
