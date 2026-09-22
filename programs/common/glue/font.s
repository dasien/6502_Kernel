; ============================================================================
; font.s -- cc65 glue for the VIC soft-font port
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; The glyph shapes are RAM inside the chip, reached through this index/data
; pair, so redefining a character costs nothing in the 6502's address space.
; What it does cost is a port write per byte: sixteen per glyph.
;
; The index is glyph*16 + scanline within the live set (16 bytes a glyph, 256
; glyphs a set, 16 sets). Both data calls advance the index, so a glyph is a
; seek followed by sixteen writes.
; ============================================================================

.export _vfseek, _vfread, _vfwrite

.include "mfc.inc"

.PC02
.segment "CODE"

; void vfseek(unsigned int index) -- point the font data port at a byte.
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
