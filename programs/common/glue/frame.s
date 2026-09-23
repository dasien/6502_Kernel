; ============================================================================
; frame.s -- cc65 glue for the frame boundary
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; The two ends of one frame. wait_frame() returns as a frame begins; present()
; says it is finished. Between them is the program's to paint in, and only the
; frame it presents is ever shown whole:
;
;     for (;;) { wait_frame(); ...simulate and draw...; present(); }
;
; Pacing is a separate matter. wait_frame() says a frame began, not how many
; went by, so a game still keeps an accumulator against jiffies() to make up a
; backlog after the host stalls.
; ============================================================================

.export _wait_frame, _present

.include "mfc.inc"

.PC02
.segment "CODE"

; void wait_frame(void) -- block until the VIC starts a new frame.
.proc _wait_frame
        jmp     K_WAIT_FRAME
.endproc

; void present(void) -- this frame is finished; show it now.
;
; Without it the host can only show the plane at the next frame boundary, and a
; program that paints after waking at one is always a frame behind. Write
; nothing to the VIC after this until wait_frame() returns: the host reads the
; plane shortly after the present, not at the instant of it.
.proc _present
        sta     VREG_FRAME      ; any value presents; reading the register is the counter
        rts
.endproc
