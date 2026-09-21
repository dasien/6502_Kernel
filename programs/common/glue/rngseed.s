; ============================================================================
; rngseed.s -- entropy for a pseudo-random generator
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked. The
; generator itself is rng.s; a program may seed a C-side generator from here
; and never touch it.
; ============================================================================

.export _rng_seed

.include "mfc.inc"

.PC02
.segment "CODE"

; unsigned int rng_seed(void) -- latch the RTC and fold seconds, minutes, hours
; and the packed FAT-time bits into a 16-bit seed (A=lo, X=hi). Varies per
; launch; the caller guards against a zero result.
;
; The two EOR constants are arbitrary salt, there only to keep the low and high
; bytes from moving together when the clock barely has.
.proc _rng_seed
        sta     RTC_LATCH               ; snapshot the live clock (value irrelevant)
        lda     RTC_SEC
        eor     RTC_FATTIME_LO
        eor     #$5B
        pha                             ; -> low byte
        lda     RTC_MIN
        eor     RTC_HOUR
        eor     RTC_FATTIME_HI
        eor     #$C4
        tax                             ; -> high byte
        pla                             ; low byte back into A
        rts
.endproc
