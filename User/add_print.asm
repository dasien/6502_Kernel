; Simple addition test program for MFC 6502
; Adds 5 + 3 and displays "8" on screen

.org $0800

START:
    ; Load first number
    LDA #$05            ; A9 05

    ; Add second number
    CLC                 ; 18
    ADC #$03            ; 69 03

    ; Convert to ASCII (add $30)
    CLC                 ; 18
    ADC #$30            ; 69 30

    ; Print the result
    JSR $FF00           ; 20 00 FF  (K_PRINT_CHAR)

    ; Print newline
    LDA #$0D            ; A9 0D
    JSR $FF00           ; 20 00 FF  (K_PRINT_CHAR)

    ; Return to monitor
    RTS                 ; 60

; Enter these bytes.
; A9 05 18 69 03 18 69 30 20 00 FF A9 0D 20 00 FF 60
