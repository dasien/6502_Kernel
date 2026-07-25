; 8x8 multiply -- MFC 6502
;
; Computes 123 x 45 = 5535 with the classic shift-and-add algorithm and prints
; the result. Teaches how multiplication is built from shifts and adds (the 6502
; has no multiply instruction) and how to print a 16-bit result as decimal.
;
; The algorithm: for each of the 8 multiplier bits, shift it out (LSR); if it was
; set, add the multiplicand to the running high byte; then rotate the 16-bit
; product right. After 8 rounds A = product high, RESLO = product low.
;
; Enter via the monitor:  W:0800  then the byte block below,  G:0800
    .org $0800

K_PRINT_MESSAGE = $FF03
K_PRINT_NEWLINE = $FF06
K_PRINT_DEC     = $FF27
MON_MSG_PTR     = $16

START:
    LDA #<LBL                    ; print "123 x 45 = "
    STA MON_MSG_PTR
    LDA #>LBL
    STA MON_MSG_PTR+1
    JSR K_PRINT_MESSAGE

    LDA #45                      ; multiplier
    STA MPLIER
    LDA #0
    STA RESLO                    ; product low = 0; A (= product high) = 0
    LDX #8                       ; 8 bits
MUL:
    LSR MPLIER                   ; next multiplier bit -> carry
    BCC NOADD
    CLC
    ADC #123                     ; add the multiplicand (123) to the high byte
NOADD:
    ROR A                        ; rotate the 16-bit product right
    ROR RESLO
    DEX
    BNE MUL
    STA NUM+1                    ; A = high byte of the product
    LDA RESLO
    STA NUM                      ; RESLO = low byte
    LDA #0
    STA NUM+2
    STA NUM+3
    LDA #<NUM                    ; print the 16-bit result as decimal
    LDX #>NUM
    LDY #0                       ; width 0 = no padding
    JSR K_PRINT_DEC
    JSR K_PRINT_NEWLINE
    RTS

LBL:    .byte "123 x 45 = ", 0
MPLIER: .byte 0
RESLO:  .byte 0
NUM:    .byte 0, 0, 0, 0
    .end

; Enter these bytes.
; A9 44 85 16 A9 08 85 17 20 03 FF A9 2D 8D 50 08
; A9 00 8D 51 08 A2 08 4E 50 08 90 03 18 69 7B 6A
; 6E 51 08 CA D0 F1 8D 53 08 AD 51 08 8D 52 08 A9
; 00 8D 54 08 8D 55 08 A9 52 A2 08 A0 00 20 27 FF
; 20 06 FF 60 31 32 33 20 78 20 34 35 20 3D 20 00
; 00 00 00 00 00 00
