; ============================================================================
; raster.s -- cc65 glue for the VIC raster
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; The VIC reports which line of the frame the beam is on. A palette, fine-scroll
; or font change made on a visible line shows from that line down, so a split is:
;
;     wait_frame();                      /* line 0: the top band's settings    */
;     ...set the top...
;     wait_line(200);                    /* the beam reaches line 200          */
;     ...set the bottom...
;
; wait_line() busy-waits. A line is about 133 cycles, so the change should be made
; promptly after it returns, and any heavy work belongs after the last split.
; ============================================================================

.export _raster_line, _wait_line, _raster_compare

.include "mfc.inc"

.PC02
.segment "CODE"

; unsigned int raster_line(void) -- the current line, 0..499. LO first: reading it
; latches HI, so the pair cannot tear across line 255/256.
.proc _raster_line
        lda     VREG_RASTER_LO
        pha
        lda     VREG_RASTER_HI
        and     #$01
        tax
        pla
        rts
.endproc

; void wait_line(unsigned int line) -- return once the beam is on `line` or past it.
; Returns at once if it already is, so call it in rising order within a frame.
; A/X hold the line; the comparison is 16 bits, high byte first.
.proc _wait_line
        sta     target
        stx     target+1
@poll:  lda     VREG_RASTER_LO          ; latches HI
        tay
        lda     VREG_RASTER_HI
        and     #$01
        cmp     target+1
        bcc     @poll                   ; high byte below: keep waiting
        bne     @done                   ; high byte above: past it
        cpy     target
        bcc     @poll                   ; same high byte, low byte below
@done:  rts
.endproc

; void raster_compare(unsigned int line) -- the line the raster interrupt waits
; for. Writing the line registers sets the compare; reading them is the beam.
.proc _raster_compare
        sta     VREG_RASTER_LO
        stx     VREG_RASTER_HI
        rts
.endproc

.segment "BSS"
target: .res 2
