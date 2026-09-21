; ============================================================================
; GOPHER (network document browser) -- cc65 runtime glue
; ============================================================================
; Bridges the C client to the kernel + the VIC video port + the 6551 ACIA.
;   INCH/INCH_NB/QUITDOS  -- key input + return to DOS (as in EDIT).
;   vaddr/vputc/vattr      -- drive the VIC port: set the cell index, write a
;                             glyph (auto-increments), set the color latch.
;   vcursor                -- position the displayed hardware cursor.
;   vfill/vcmd             -- chip-side block ops (clear / scroll / fill-row).
;   acia_init/acia_get/acia_put -- polled 6551 driver (the serial line).
;   acia_carrier           -- /DCD: nonzero while a call is up.
; ============================================================================

.export _dl_chunk

.include "mfc.inc"

.PC02                           ; WDC 65C02, as the kernel, monitor and DOS declare.
                                ; Stated here as well as on the ca65 command line so
                                ; the file is right however it is assembled.

.segment "CODE"

; int dl_chunk(void) -- move up to 256 bytes straight from the ACIA into the
; open file, stopping early if the receiver runs dry.
;   returns  0..256 = bytes moved
;            -1     = the write failed (disk full)
;
; The download loop used to cross the C boundary twice per byte, and cc65's
; 32-bit counter arithmetic on top of that cost several hundred cycles against
; FS_PUTB's sixty -- the transfer was spending almost all its time in the
; bookkeeping rather than the work. Moving the inner loop here leaves C to run
; once per chunk, which is still often enough to poll the keyboard and repaint.
dl_n:   .byte 0                 ; chunk counter (CODE is rw in the .cfg)

.proc _dl_chunk
        lda     #$00
        sta     dl_n            ; count moved
@loop:  lda     ACIA_STATUS
        and     #$08            ; receiver full?
        beq     @done           ; dry: hand back what we have
        lda     ACIA_DATA
        jsr     FS_PUTB
        bcs     @err
        inc     dl_n            ; INC sets Z when it wraps 255 -> 0
        beq     @full           ; a full chunk of 256 moved
        jmp     @loop
@full:  lda     #$00
        ldx     #$01            ; 256
        rts
@done:  lda     dl_n
        ldx     #$00
        rts
@err:   lda     #$ff
        ldx     #$ff            ; -1
        rts
.endproc
