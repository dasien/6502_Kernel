; Times table -- MFC 6502
;
; Prints the full 9x9 multiplication table. Teaches NESTED loops (an outer row
; counter and an inner column counter) and multiplication by repeated addition
; (I added J times), with the results lined up in 4-wide columns via K_PRINT_DEC.
;
; Enter via the monitor:  W:0800  then the byte block below,  G:0800
    .org $0800

K_PRINT_NEWLINE = $FF06
K_PRINT_DEC     = $FF27

START:
    LDA #1
    STA I                        ; outer: rows 1..9
ROW:
    LDA #1
    STA J                        ; inner: columns 1..9
COL:
    LDA #0                        ; product = I * J by repeated addition
    STA PROD
    LDX J                         ; add I, J times
ADDL:
    CLC
    LDA PROD
    ADC I
    STA PROD
    DEX
    BNE ADDL
    LDA PROD                      ; print the product in a 4-wide column
    STA NUM
    LDA #0
    STA NUM+1
    STA NUM+2
    STA NUM+3
    LDA #<NUM
    LDX #>NUM
    LDY #4
    JSR K_PRINT_DEC
    INC J
    LDA J
    CMP #10
    BNE COL                       ; next column
    JSR K_PRINT_NEWLINE           ; end of a row
    INC I
    LDA I
    CMP #10
    BNE ROW                       ; next row
    RTS

I:    .byte 0
J:    .byte 0
PROD: .byte 0
NUM:  .byte 0, 0, 0, 0
    .end

; Enter these bytes.
; A9 01 8D 51 08 A9 01 8D 52 08 A9 00 8D 53 08 AE
; 52 08 18 AD 53 08 6D 51 08 8D 53 08 CA D0 F3 AD
; 53 08 8D 54 08 A9 00 8D 55 08 8D 56 08 8D 57 08
; A9 54 A2 08 A0 04 20 27 FF EE 52 08 AD 52 08 C9
; 0A D0 C7 20 06 FF EE 51 08 AD 51 08 C9 0A D0 B5
; 60 00 00 00 00 00 00 00
