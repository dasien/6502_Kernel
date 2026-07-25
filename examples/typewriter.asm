; Typewriter -- MFC 6502
;
; Echoes every key you press until you hit ESC. Teaches a non-blocking input
; loop: K_GET_KEYSTROKE ($FF09) returns with carry SET and the key in A only when
; one is waiting -- so loop on BCC to wait, then echo with K_PRINT_CHAR ($FF00).
;
; Enter via the monitor:  W:0800  then the byte block below,  G:0800
; (ESC returns you to the monitor prompt.)
    .org $0800

K_PRINT_CHAR    = $FF00
K_GET_KEYSTROKE = $FF09
K_CLEAR_SCREEN  = $FF0C

START:
    JSR K_CLEAR_SCREEN
LOOP:
    JSR K_GET_KEYSTROKE     ; C clear = no key yet; C set = A holds the key
    BCC LOOP                ; keep waiting until a key arrives
    CMP #$1B                ; ESC?
    BEQ DONE                ; yes -> quit
    JSR K_PRINT_CHAR        ; echo the key
    BRA LOOP
DONE:
    RTS
    .end

; Enter these bytes.
; 20 0C FF 20 09 FF 90 FB C9 1B F0 05 20 00 FF 80
; F2 60
