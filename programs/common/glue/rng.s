; ============================================================================
; rng.s -- the pseudo-random generator
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; Seeding is in rngseed.s, not here. Most programs run their own generator in
; C and want only the entropy to start it; pulling this module for them would
; link a generator they never call, plus its seed word.
; ============================================================================

.export _RND

.include "mfc.inc"

.PC02

.segment "DATA"
rndseed:        .word   $ACE1   ; nonzero LFSR seed (DATA = loaded into RAM)

.segment "CODE"

; int RND(void) -- 16-bit Galois LFSR (poly $B400), returns A/X.
.proc _RND
        lsr     rndseed+1
        ror     rndseed
        bcc     @nofb
        lda     rndseed+1
        eor     #$B4
        sta     rndseed+1
@nofb:  lda     rndseed
        ldx     rndseed+1
        rts
.endproc
