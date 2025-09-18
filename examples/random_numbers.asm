; Random number test program
; Generates 3 random numbers from 1-10 and displays them
.org $0800

START:
    ; Clear screen
    JSR $FF0C               ; 20 0C FF

    ; Print header
    LDA #<HEADER            ; A9 20
    STA $04                 ; 85 04
    LDA #>HEADER            ; A9 08
    STA $05                 ; 85 05
    JSR $FF03               ; 20 03 FF

    ; Set maximum to 10
    LDA #$0A                ; A9 0A
    STA $12                 ; 85 12 (RNG_MAX)

    ; Generate and print 3 random numbers
    LDX #$03                ; A2 03 (counter)

LOOP:
    ; Get random number 1-10
    JSR $FF0F               ; 20 0F FF

    ; Convert to ASCII (works for 1-9)
    CLC                     ; 18
    ADC #$30                ; 69 30
    JSR $FF00               ; 20 00 FF (print digit)

    ; Print space
    LDA #$20                ; A9 20
    JSR $FF00               ; 20 00 FF

    ; Decrement counter
    DEX                     ; CA
    BNE LOOP                ; D0 EF

    ; Print newline
    JSR $FF06               ; 20 06 FF

    ; Done
    RTS                     ; 60

HEADER:
    .BYTE "RANDOM NUMBERS (1-10):", $0D, 0

; Enter these bytes.
; 20 0C FF A9 20 85 04 A9 08 85 05 20 03 FF A9 0A
; 85 12 A2 03 20 0F FF 18 69 30 20 00 FF A9 20 20
; 00 FF CA D0 EF 20 06 FF 60 52 41 4E 44 4F 4D 20
; 4E 55 4D 42 45 52 53 20 28 31 2D 31 30 29 3A 0D
; 00