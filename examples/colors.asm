; Colors -- MFC 6502
;
; Prints "COLOR 0".."COLOR 7", each line in a different color, to show the
; attribute byte and the K_SET_ATTR ($FF2D) ABI.
;
; Attribute byte format (what K_SET_ATTR latches for the following characters):
;   bit 7    reverse video
;   bit 6    bright
;   bits 5-3 background color (0-7)
;   bits 2-0 foreground color (0-7)
; Colors: 0 black, 1 red, 2 green, 3 yellow, 4 blue, 5 magenta, 6 cyan, 7 white.
; We OR in bit 6 (bright) so even color 0 (black) shows up as dark gray.
;
; Enter via the monitor:  W:0800  then the byte block below,  G:0800
    .org $0800

K_PRINT_CHAR    = $FF00
K_PRINT_MESSAGE = $FF03
K_PRINT_NEWLINE = $FF06
K_CLEAR_SCREEN  = $FF0C
K_SET_ATTR      = $FF2D          ; A = attribute byte -> color latch
MON_MSG_PTR     = $16            ; K_PRINT_MESSAGE reads its string pointer here

START:
    JSR K_CLEAR_SCREEN
    STZ CNT                      ; color 0..7
CLOOP:
    LDA CNT
    ORA #$40                     ; set the bright bit so color 0 is visible
    JSR K_SET_ATTR               ; following text prints in this color
    LDA #<CTXT                   ; print "COLOR "
    STA MON_MSG_PTR
    LDA #>CTXT
    STA MON_MSG_PTR+1
    JSR K_PRINT_MESSAGE
    LDA CNT
    ORA #$30                     ; color number -> ASCII digit '0'..'7'
    JSR K_PRINT_CHAR
    JSR K_PRINT_NEWLINE
    INC CNT
    LDA CNT
    CMP #8
    BNE CLOOP
    LDA #$02                     ; restore the default (green on black)
    JSR K_SET_ATTR
    RTS

CTXT: .byte "COLOR ", 0
CNT:  .byte 0
    .end

; Enter these bytes.
; 20 0C FF 9C 3B 08 AD 3B 08 09 40 20 2D FF A9 34
; 85 16 A9 08 85 17 20 03 FF AD 3B 08 09 30 20 00
; FF 20 06 FF EE 3B 08 AD 3B 08 C9 08 D0 D8 A9 02
; 20 2D FF 60 43 4F 4C 4F 52 20 00 00
