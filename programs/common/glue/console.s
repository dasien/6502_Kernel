; ============================================================================
; console.s -- cc65 glue for teletype-style character output
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; Separate from kernel.s because these two are a different idiom. A program
; ported from a host terminal writes a character at a time and lets the kernel
; track the cursor; everything written for this machine paints cells through
; the VIC port instead. Keeping the pair here means a program that only wants
; jiffies() or INCH_NB() does not link output routines it never calls.
; ============================================================================

.export _OUTCH, _CLS

.include "mfc.inc"

.PC02
.segment "CODE"

; void OUTCH(char c) -- print c, mapping LF to the kernel's newline.
.proc _OUTCH
        cmp     #10
        bne     @ch
        jmp     K_PRINT_NEWLINE ; tail call: its RTS returns to the C caller
@ch:    jmp     K_PRINT_CHAR
.endproc

; void CLS(void) -- clear the screen and home the cursor.
.proc _CLS
        jmp     K_CLEAR_SCREEN  ; tail call: its RTS returns to the C caller
.endproc
