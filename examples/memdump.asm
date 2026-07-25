; Memory dump -- MFC 6502
;
; Prints the first 16 bytes of the kernel jump table ($FF00) as hex, so you can
; see the ABI itself: each entry is "4C xx FF" (JMP to a kernel routine). Teaches
; indexed addressing (LDA $FF00,X) and hex output via K_PRINT_HEX_BYTE ($FF1B).
;
; Change the $FF00 in "LDA $FF00,X" (bytes "BD 00 FF") to dump any other address.
;
; Enter via the monitor:  W:0800  then the byte block below,  G:0800
    .org $0800

K_PRINT_CHAR     = $FF00
K_PRINT_NEWLINE  = $FF06
K_PRINT_HEX_BYTE = $FF1B          ; print A as two hex digits

START:
    LDX #0
LOOP:
    LDA $FF00,X                   ; read byte X of the region being dumped
    PHX                           ; K_PRINT_* may use X, so save our index
    JSR K_PRINT_HEX_BYTE
    LDA #$20                      ; a space between bytes
    JSR K_PRINT_CHAR
    PLX
    INX
    CPX #16                       ; 16 bytes then stop
    BNE LOOP
    JSR K_PRINT_NEWLINE
    RTS
    .end

; Enter these bytes.
; A2 00 BD 00 FF DA 20 1B FF A9 20 20 00 FF FA E8
; E0 10 D0 EE 20 06 FF 60
