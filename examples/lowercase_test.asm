; Lowercase + underscore assembler test for MFC 6502
;
; Everything here is deliberately lowercase, with underscores in identifier
; names, to exercise the case-insensitive assembler (task #73). It prints a
; message and rings the bell, so a successful build AND run are both visible.
;
; Build it in the devtools assembler:
;   ]  ASM              ; launch the assembler
;   *  L                ; load this file (host open dialog)
;   *  B                ; build -- should assemble with NO "? LINE" error
;   *  <ESC>            ; back to DOS
; Run it:
;   ]  MON              ; enter the monitor
;   *  G:0800           ; run -- prints the message and beeps
;
; Note the .ascii string keeps its lowercase: only code is folded to uppercase,
; string literals are left alone.

.org $0800

k_print = $ff00         ; kernel PRINT_CHAR (underscore in the name)
bel_char = $07          ; ASCII BEL -> rings the SID beep

start:
    ldx #$00            ; lowercase mnemonic + lowercase hex
print_ch:
    lda message,x       ; absolute,x with a (forward) label
    beq do_beep         ; 0 terminator -> done printing
    jsr k_print
    inx
    bne print_ch
do_beep:
    lda #bel_char
    jsr k_print         ; BEL -> short beep
    rts

message:
    .ascii "lowercase + underscores ok!"
    .byte $0d, $00      ; newline, then 0 terminator

.end
