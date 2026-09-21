; ============================================================================
; rtc.s -- cc65 glue for reading the real-time clock
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
; Seeding an RNG from the clock lives in rng.s, not here.
; ============================================================================

.export _rtc_sec

.include "mfc.inc"

.PC02
.segment "CODE"

; unsigned char rtc_sec(void) -- latch the clock, return the BCD seconds byte.
; Only tested for change (a new second passed), so BCD is fine.
.proc _rtc_sec
        sta     RTC_LATCH
        lda     RTC_SEC
        ldx     #$00
        rts
.endproc
