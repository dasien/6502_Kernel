; Random number demo for MFC 6502
; Prints ten random numbers in the range 1-100, space-aligned, on one line.
;
; Demonstrates the kernel RNG the correct way:
;   * set RNG_MAX ($24) to the (inclusive) upper bound, then call K_GET_RAND_NUM
;     ($FF0F) -- it returns an integer 1..RNG_MAX in A.
;   * print that integer as DECIMAL with K_PRINT_DEC ($FF27), which handles any
;     number of digits. (Do NOT "convert" a byte to a character with ADC #$30 --
;     that only works for single digits 0-9; 10 would print as ':'.)
;
; The RNG is RTC-seeded at boot, so the sequence differs from run to run; each
; call advances a 16-bit LFSR (period 65535), so numbers keep coming without a
; reboot -- run this repeatedly (G:0800) to see fresh values every time.
;
; Enter via the monitor:  W:0800  then type the byte block at the bottom,  G:0800
    .org $0800

; ---- kernel ABI (jump table at $FF00) ----
K_PRINT_MESSAGE = $FF03         ; print null-terminated string at (MON_MSG_PTR)
K_PRINT_NEWLINE = $FF06         ; print CR/LF
K_CLEAR_SCREEN  = $FF0C         ; clear + home
K_GET_RAND_NUM  = $FF0F         ; A = random 1..RNG_MAX
K_PRINT_DEC     = $FF27         ; print 32-bit value: A/X=ptr to 4 LE bytes, Y=field width
; ---- zero page ----
MON_MSG_PTR     = $16           ; K_PRINT_MESSAGE reads its string pointer here ($16/$17)
RNG_MAX         = $24           ; RNG upper bound (inclusive)

START:
    JSR K_CLEAR_SCREEN          ; 20 0C FF

    ; Print the header
    LDA #<HEADER                ; A9 37
    STA MON_MSG_PTR             ; 85 16
    LDA #>HEADER                ; A9 08
    STA MON_MSG_PTR+1           ; 85 17
    JSR K_PRINT_MESSAGE         ; 20 03 FF

    ; Roll in 1..100
    LDA #100                    ; A9 64
    STA RNG_MAX                 ; 85 24

    ; K_PRINT_DEC prints a 32-bit little-endian value; our roll is one byte, so
    ; clear the upper three bytes of the buffer once (the low byte is set per roll)
    LDA #0                      ; A9 00
    STA NUMBUF+1                ; 8D 47 08
    STA NUMBUF+2                ; 8D 48 08
    STA NUMBUF+3                ; 8D 49 08

    LDX #10                     ; A2 0A   (print ten numbers)
LOOP:
    JSR K_GET_RAND_NUM          ; 20 0F FF  -> A = 1..100
    STA NUMBUF                  ; 8D 46 08  (low byte of the value)

    PHX                         ; DA        (K_PRINT_DEC clobbers X; save the counter)
    LDA #<NUMBUF                ; A9 46
    LDX #>NUMBUF                ; A2 08
    LDY #4                      ; A0 04     (4-wide field -> space-aligned columns)
    JSR K_PRINT_DEC             ; 20 27 FF
    PLX                         ; FA

    DEX                         ; CA
    BNE LOOP                    ; D0 EC

    JSR K_PRINT_NEWLINE         ; 20 06 FF
    RTS                         ; 60

HEADER:
    .byte "RANDOM 1-100:", $0D, 0   ; 52 41 4E 44 4F 4D 20 31 2D 31 30 30 3A 0D 00
NUMBUF:
    .byte 0, 0, 0, 0                ; 00 00 00 00  (scratch: the value passed to K_PRINT_DEC)

; Enter these bytes.
; 20 0C FF A9 37 85 16 A9 08 85 17 20 03 FF A9 64
; 85 24 A9 00 8D 47 08 8D 48 08 8D 49 08 A2 0A 20
; 0F FF 8D 46 08 DA A9 46 A2 08 A0 04 20 27 FF FA
; CA D0 EC 20 06 FF 60 52 41 4E 44 4F 4D 20 31 2D
; 31 30 30 3A 0D 00 00 00 00 00
