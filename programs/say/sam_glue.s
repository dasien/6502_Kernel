; ============================================================================
; sam_glue.s -- SAY's C front end calling S.A.M.
; ============================================================================
; SAM takes its input in its own 256-byte buffer, ended by $9B (the Atari's end
; of line, which SAM kept on every machine), and its four voice settings in four
; bytes of its own. These give them C names, and wrap the two machine-language
; entries so each returns the parser's error position: $FF when it could say it,
; else the index in the buffer where it stopped.
;
; Both wrappers end with say_uninit. SAM's own path reaches it after speaking,
; but RECITER's error path returns without it -- interrupts still off, zero page
; still SAM's, the clock still at 1 MHz -- and it is safe to run twice.
;
; cc65's zero page ($80-$9E) and C stack are untouched by SAM (its zero page is
; $A0-$D3, see sam.s), so nothing here needs saving.
; ============================================================================

.import L9a03_SAM_ML_entry, L9a09_Reciter_ML_entry, L9bbc_say_uninit
.import L9a15_input, L9a14_error_pos
.import L9a0e_speed, L9a0f_pitch, L97e0_throat, L97e1_mouth, S97e2_SetMouthThroat

.export _sam_input, _sam_speed, _sam_pitch, _sam_throat, _sam_mouth
.export _sam_say_text, _sam_say_phonemes, _sam_set_mouth_throat, _read_line

_sam_input  = L9a15_input
_sam_speed  = L9a0e_speed
_sam_pitch  = L9a0f_pitch
_sam_throat = L97e0_throat
_sam_mouth  = L97e1_mouth

K_READ_LINE = $FF15             ; edited line input into MON_CMDBUF ($0200)

.segment "CODE"

; unsigned char sam_say_text(void) -- English in sam_input, through RECITER
.proc _sam_say_text
        jsr     L9a09_Reciter_ML_entry
        jmp     finish
.endproc

; unsigned char sam_say_phonemes(void) -- SAM's phonemes in sam_input
.proc _sam_say_phonemes
        jsr     L9a03_SAM_ML_entry
        ; fall through
.endproc

.proc finish
        jsr     L9bbc_say_uninit
        lda     L9a14_error_pos
        ldx     #$00
        rts
.endproc

; void sam_set_mouth_throat(void) -- rebuild the formant tables from the knobs
_sam_set_mouth_throat = S97e2_SetMouthThroat

; unsigned char read_line(void) -- K_READ_LINE; the text is at $0200
.proc _read_line
        jsr     K_READ_LINE
        ldx     #$00
        rts
.endproc
