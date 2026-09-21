; ============================================================================
; VENTURE -- cc65 runtime glue
; ============================================================================
; The 80x25 screen lives behind the VIC register port (not in the 64K map), so
; all drawing goes through the video helpers: set the cell index with vaddr(),
; then stream glyphs with vputc() (the port auto-increments).
;
; Lifted from KERNEL PANIC's glue, minus vscrollbot(): Venture never scrolls, it
; redraws the cells that changed. What it does share is the pair that make
; real-time play possible on this machine --
;   jiffies()  -- the kernel's 60 Hz monotonic tick counter (K_GET_JIFFIES),
;                 which paces the fixed-tick loop.
;   keystate() -- the live held-key bitmask at $FE0F. Eight-way movement while
;                 firing is impossible from the keystroke FIFO: it carries no
;                 key-up, and the host only auto-repeats the most recent key.
; ============================================================================

.export _vfontaddr, _vfontput
.export _sound_tone, _sound_off

.include "mfc.inc"

.PC02                           ; WDC 65C02, as the kernel, monitor and DOS declare.
                                ; Stated here as well as on the ca65 command line so
                                ; the file is right however it is assembled.

.segment "CODE"

; void vfontaddr(unsigned int idx) -- point the font data port at a byte. The index
; is glyph*16 + scanline within the live set (16 bytes a glyph, 256 glyphs a set).
.proc _vfontaddr
        sta     VREG_FONT_LO
        stx     VREG_FONT_HI
        rts
.endproc

; void vfontput(unsigned char bits) -- write one scanline and advance.
.proc _vfontput
        sta     VREG_FONT_DATA
        rts
.endproc

; void sound_tone(unsigned int freq) -- start a tone on voice 1. cc65 already
; passes a 16-bit argument in A=lo/X=hi, which is what K_SOUND_TONE wants, so this
; is a straight tail call. The kernel honours SOUND_ENABLE, so this is silent
; rather than wrong on a muted machine.
.proc _sound_tone
        jmp     K_SOUND_TONE
.endproc

; void sound_off(void) -- gate voice 1 off.
.proc _sound_off
        jmp     K_SOUND_OFF
.endproc
