; Program start address.
    .org $0800

START:
    ; Clear the screen
    JSR $FF0C

    ; Print welcome message
    LDX #<WELCOME
    LDY #>WELCOME
    JSR $FF03

    ; Print "Press any key..."
    LDX #<PROMPT
    LDY #>PROMPT
    JSR $FF03

    ; Wait for keypress
    JSR $FF09

    ; Print "You pressed: "
    LDX #<PRESSED
    LDY #>PRESSED
    JSR $FF03

    ; Print the key that was pressed
    JSR $FF00

    ; New line
    JSR $FF06

    ; Done
    RTS

WELCOME:
    .BYTE "HELLO FROM USER PROGRAM!", $0D, 0

PROMPT:
    .BYTE "PRESS ANY KEY TO CONTINUE...", $0D, 0

PRESSED:
    .BYTE "YOU PRESSED: ", 0
