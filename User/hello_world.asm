; Program start address.
    .org $0800

START:
    ; Clear the screen
    JSR $FF0C               ; 20 0C FF

    ; Print welcome message
    LDA #<WELCOME           ; A9 29
    STA $04                 ; 85 04
    LDA #>WELCOME           ; A9 08
    STA $05                 ; 85 05
    JSR $FF03               ; 20 03 FF

    ; Print "Press any key..."
    LDA #<PROMPT            ; A9 43
    STA $04                 ; 85 04
    LDA #>PROMPT            ; A9 08
    STA $05                 ; 85 05
    JSR $FF03               ; 20 03 FF

    ; Wait for keypress
    JSR $FF09               ; 20 09 FF

    ; Print "You pressed: "
    LDA #<PRESSED           ; A9 62
    STA $04                 ; 85 04
    LDA #>PRESSED           ; A9 08
    STA $05                 ; 85 05
    JSR $FF03               ; 20 03 FF

    ; Print the key that was pressed
    JSR $FF00               ; 20 00 FF

    ; New line
    JSR $FF06               ; 20 06 FF

    ; Done
    RTS                     ; 60

WELCOME:
    .BYTE "HELLO FROM USER PROGRAM!", $0D, 0

PROMPT:
    .BYTE "PRESS ANY KEY TO CONTINUE...", $0D, 0

PRESSED:
    .BYTE "YOU PRESSED: ", 0

; Enter these bytes.
; 20 0C FF A9 29 85 04 A9 08 85 05 20 03 FF A9 43
; 85 04 A9 08 85 05 20 03 FF 20 09 FF A9 62 85 04
; A9 08 85 05 20 03 FF 20 00 FF 20 06 FF 60 48 45
; 4C 4C 4F 20 46 52 4F 4D 20 55 53 45 52 20 50 52
; 4F 47 52 41 4D 21 0D 00 50 52 45 53 53 20 41 4E
; 59 20 4B 45 59 20 54 4F 20 43 4F 4E 54 49 4E 55
; 45 2E 2E 2E 0D 00 59 4F 55 20 50 52 45 53 53 45
; 44 3A 20 00