; ============================================================================
; sound.s -- cc65 glue for the SID, through the kernel
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; Via the kernel ABI rather than the SID registers directly, so these honour
; SOUND_ENABLE: a muted machine plays nothing and the program needs no say in
; it. Voice 1 only, which is what a single sound effect wants.
; ============================================================================

.export _sound_tone, _sound_off

.include "mfc.inc"

.PC02
.segment "CODE"

; void sound_tone(unsigned int freq) -- start a tone on voice 1. cc65 passes a
; 16-bit argument in A=lo/X=hi, which is exactly what K_SOUND_TONE wants, so
; this is a straight tail call.
.proc _sound_tone
        jmp     K_SOUND_TONE
.endproc

; void sound_off(void) -- gate voice 1 off.
.proc _sound_off
        jmp     K_SOUND_OFF
.endproc
