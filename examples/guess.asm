; Guess my number -- MFC 6502  (a little game)
;
; The computer picks a secret number 1-100; you type guesses and it tells you
; "TOO HIGH" / "TOO LOW" until you get it. Ties several ABIs together:
;   * K_GET_RAND_NUM ($FF0F) with RNG_MAX ($24) -> the secret 1..100
;   * K_READ_LINE ($FF15) -> read a typed line into MON_CMDBUF
;   * K_PARSE_DEC ($FF2A) -> parse it (X = start offset); result in MON_CURRADDR
;     ($14/$15), carry SET if the text wasn't a valid number
;   * CMP + BEQ/BCC to compare and branch
;
; Enter via the monitor:  W:0800  then the byte block below,  G:0800
    .org $0800

K_PRINT_MESSAGE = $FF03
K_CLEAR_SCREEN  = $FF0C
K_GET_RAND_NUM  = $FF0F
K_READ_LINE     = $FF15
K_PARSE_DEC     = $FF2A
RNG_MAX         = $24
MON_MSG_PTR     = $16
MON_CURRADDR_LO = $14            ; K_PARSE_DEC leaves the parsed value here

START:
    JSR K_CLEAR_SCREEN
    LDA #100                     ; secret = random 1..100
    STA RNG_MAX
    JSR K_GET_RAND_NUM
    STA SECRET
    LDA #<INTRO
    STA MON_MSG_PTR
    LDA #>INTRO
    STA MON_MSG_PTR+1
    JSR K_PRINT_MESSAGE
GLOOP:
    LDA #<PROMPT                 ; print "? "
    STA MON_MSG_PTR
    LDA #>PROMPT
    STA MON_MSG_PTR+1
    JSR K_PRINT_MESSAGE
    JSR K_READ_LINE              ; read the guess into MON_CMDBUF
    LDX #0                       ; parse from the start of the line
    JSR K_PARSE_DEC
    BCS GLOOP                    ; not a number -> ask again
    LDA MON_CURRADDR_LO          ; the guess (1..100 fits in the low byte)
    CMP SECRET
    BEQ WIN
    BCC LOW                      ; guess < secret
    LDA #<HImsg                  ; guess > secret
    STA MON_MSG_PTR
    LDA #>HImsg
    STA MON_MSG_PTR+1
    JSR K_PRINT_MESSAGE
    JMP GLOOP
LOW:
    LDA #<LOmsg
    STA MON_MSG_PTR
    LDA #>LOmsg
    STA MON_MSG_PTR+1
    JSR K_PRINT_MESSAGE
    JMP GLOOP
WIN:
    LDA #<WINmsg
    STA MON_MSG_PTR
    LDA #>WINmsg
    STA MON_MSG_PTR+1
    JSR K_PRINT_MESSAGE
    RTS

INTRO:  .byte "GUESS MY NUMBER 1-100!", $0D, 0
PROMPT: .byte "? ", 0
HImsg:  .byte "TOO HIGH", $0D, 0
LOmsg:  .byte "TOO LOW", $0D, 0
WINmsg: .byte "CORRECT!", $0D, 0
SECRET: .byte 0
    .end

; Enter these bytes.
; 20 0C FF A9 64 85 24 20 0F FF 8D 96 08 A9 5E 85
; 16 A9 08 85 17 20 03 FF A9 76 85 16 A9 08 85 17
; 20 03 FF 20 15 FF A2 00 20 2A FF B0 EB A5 14 CD
; 96 08 F0 1E 90 0E A9 79 85 16 A9 08 85 17 20 03
; FF 4C 18 08 A9 83 85 16 A9 08 85 17 20 03 FF 4C
; 18 08 A9 8C 85 16 A9 08 85 17 20 03 FF 60 47 55
; 45 53 53 20 4D 59 20 4E 55 4D 42 45 52 20 31 2D
; 31 30 30 21 0D 00 3F 20 00 54 4F 4F 20 48 49 47
; 48 0D 00 54 4F 4F 20 4C 4F 57 0D 00 43 4F 52 52
; 45 43 54 21 0D 00 00
