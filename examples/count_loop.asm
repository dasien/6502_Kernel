; Count 1 to 10 -- MFC 6502
;
; The simplest possible loop: count a variable from 1 to 10 and print each value
; as a decimal number. Teaches a counter loop (INC/CMP/BNE) and decimal output.
;
; K_PRINT_DEC ($FF27) prints a 32-bit little-endian value: point A/X at a 4-byte
; buffer and put the field width in Y (here 4, so the numbers line up in columns).
;
; Enter via the monitor:  W:0800  then the byte block below,  G:0800
    .org $0800

K_PRINT_NEWLINE = $FF06
K_PRINT_DEC     = $FF27

START:
    LDA #1                  ; start the counter at 1
    STA CNT
    LDA #0                  ; the value is one byte, so clear the upper 3 buffer bytes
    STA NUM+1
    STA NUM+2
    STA NUM+3
LOOP:
    LDA CNT                 ; copy the counter into the print buffer (low byte)
    STA NUM
    LDA #<NUM               ; A/X = pointer to the 4-byte value
    LDX #>NUM
    LDY #4                  ; field width 4 -> right-aligned columns
    JSR K_PRINT_DEC
    INC CNT
    LDA CNT
    CMP #11                 ; stop after printing 10
    BNE LOOP
    JSR K_PRINT_NEWLINE
    RTS

NUM: .byte 0, 0, 0, 0       ; scratch value handed to K_PRINT_DEC
CNT: .byte 0                ; the loop counter
    .end

; Enter these bytes.
; A9 01 8D 31 08 A9 00 8D 2E 08 8D 2F 08 8D 30 08
; AD 31 08 8D 2D 08 A9 2D A2 08 A0 04 20 27 FF EE
; 31 08 AD 31 08 C9 0B D0 E7 20 06 FF 60 00 00 00
; 00 00
