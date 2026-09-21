; ============================================================================
; vic_write.s -- cc65 glue for the VIC register port
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; The 80x25 CP437 character and colour planes live inside the chip, not in the
; 64K map, which is why these are assembly rather than an array in C.
;
; Setting the cell index and streaming to the data ports: the write half of
; the port, and what almost every program needs.
; ============================================================================

.export _vaddr, _vputc, _vattr, _vputcolor

.include "mfc.inc"

.PC02
.segment "CODE"

; void vaddr(unsigned int cell) -- point the data ports at a cell (A=lo, X=hi).
.proc _vaddr
        sta     VREG_ADDR_LO
        stx     VREG_ADDR_HI
        rts
.endproc

; void vputc(unsigned char ch) -- write a glyph at the current cell (auto-inc).
.proc _vputc
        sta     VREG_CHAR
        rts
.endproc

; void vattr(unsigned char a) -- set the colour/attribute latch for next writes.
.proc _vattr
        sta     VREG_ATTR
        rts
.endproc

; void vputcolor(unsigned char a) -- write the attribute at the current cell.
.proc _vputcolor
        sta     VREG_COLOR
        rts
.endproc
