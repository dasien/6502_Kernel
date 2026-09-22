; ============================================================================
; vic_cmd.s -- cc65 glue for the VIC register port
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; The 80x25 CP437 character and colour planes live inside the chip, not in the
; 64K map, which is why these are assembly rather than an array in C.
;
; The chip-side block operations: clear, scroll, fill a row. These do in one
; register write what would otherwise be two thousand.
; ============================================================================

.export _vfill, _vcmd, _vscrolltop, _vscrollbot

.include "mfc.inc"

.PC02
.segment "CODE"

; void vfill(unsigned char ch) -- set the parameter for the next chip command.
.proc _vfill
        sta     VREG_CMD_PARAM
        rts
.endproc

; void vcmd(unsigned char cmd) -- run a chip-side block op (clear/scroll/fill).
.proc _vcmd
        sta     VREG_CMD
        rts
.endproc

; void vscrolltop(unsigned char row) -- scroll-region top row. The port block
; ends at $FE37 with no room for a register of its own, so it rides the
; command engine with the row in the parameter.
.proc _vscrolltop
        sta     VREG_CMD_PARAM
        lda     #VCMD_SCROLL_TOP
        sta     VREG_CMD
        rts
.endproc

; void vscrollbot(unsigned char row) -- scroll-region bottom row. Scroll
; commands then affect only rows top..row; anything outside stays put.
; NOTE: a clear command resets the region to the full screen, so set this
; AFTER clearing, never before.
.proc _vscrollbot
        sta     VREG_SCROLL_BOT
        rts
.endproc
