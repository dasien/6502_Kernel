; ============================================================================
; copper.s -- a copper list, run by the VIC's raster interrupt
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; A copper list is a table of register writes, each tied to a raster line, that
; the raster interrupt makes on those lines, frame after frame, with the main
; program free to do anything else meanwhile. The name is the Amiga's, whose
; copper coprocessor ran exactly such a table; here it is an interrupt handler.
;
; It exists because a raster interrupt's handler cannot be C: cc65's runtime keeps
; state in zero page that an interrupt would trample. So the handler is this, and a
; C program describes its splits as data instead:
;
;     static unsigned char list[] = {
;         0,  0,   0xCB, 0,          /* line 0:   palette index = slot 0      */
;         0,  0,   0xCC, 0x00,       /*           red, green, blue = black    */
;         ...
;         200, 0,  0xCB, 0,          /* line 200: slot 0 again ...            */
;         ...
;         0,  0xFF                   /* end: a line whose high byte is $FF    */
;     };
;     copper_start(list);
;
; Each entry is four bytes: line low, line high, register, value -- the register as
; the low byte of its I/O-page address ($FExx), so $CB is VREG_PAL_IDX. Entries
; must be in rising line order; several on one line are made together, in order.
; After the last, the list starts again from the first line of the next frame.
;
; The list may be changed while it runs -- a value rewritten during the blanking
; lines (400-499) is in place for the next frame. copper_stop() ends it; call it
; before leaving the program, since the handler is in the program's memory.
; ============================================================================

.export _copper_start, _copper_stop

.include "mfc.inc"

.PC02
.segment "CODE"

; void copper_start(const unsigned char *list)
.proc _copper_start
        sta     base
        stx     base+1
        jsr     rewind                  ; the compare is the first entry's line
        lda     #<copper_irq
        ldx     #>copper_irq
        jmp     K_RASTER_IRQ            ; install, enable; it returns for us
.endproc

; void copper_stop(void)
.proc _copper_stop
        lda     #$00
        tax
        jmp     K_RASTER_IRQ            ; 0: disable, restore the default handler
.endproc

; Back to the first entry, and wait for its line.
.proc rewind
        lda     base
        sta     entry+1
        lda     base+1
        sta     entry+2
        jmp     arm
.endproc

; Compare = the current entry's line.
.proc arm
        ldy     #0
        jsr     entry
        sta     VREG_RASTER_LO
        iny
        jsr     entry
        sta     VREG_RASTER_HI
        rts
.endproc

; Byte Y of the current entry. The address is patched as the list is walked:
; self-modifying, because the handler may not use cc65's zero page and a pointer
; kept anywhere else would need zero page to use.
entry:  lda     $FFFF,y
        rts

; The raster interrupt: the kernel has acknowledged it and saved A, X and Y.
.proc copper_irq
        ldy     #0                      ; the line that fired
        jsr     entry
        sta     line
        iny
        jsr     entry
        sta     line+1

@write: ldy     #2
        jsr     entry
        tax                             ; the register, as $FExx's low byte
        iny
        jsr     entry
        sta     $FE00,x                 ; the write

        clc                             ; on to the next entry
        lda     entry+1
        adc     #4
        sta     entry+1
        bcc     :+
        inc     entry+2
:       ldy     #1
        jsr     entry
        cmp     #$FF
        beq     @end                    ; the end: start over next frame
        cmp     line+1
        bne     @next
        dey
        jsr     entry
        cmp     line
        beq     @write                  ; same line: make it now, too
@next:  jmp     arm                     ; wait for the next entry's line
@end:   jmp     rewind
.endproc

.segment "BSS"
base:   .res 2                          ; the list's first entry
line:   .res 2                          ; the line being handled
