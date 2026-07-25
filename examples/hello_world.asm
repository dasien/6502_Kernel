; Hello-world / keyboard echo for MFC 6502
;
; Prints a greeting, waits for a key, then echoes the key you pressed.
; Demonstrates the string- and key- ABIs the right way:
;   * K_PRINT_MESSAGE ($FF03) prints a null-terminated string whose address you
;     put in the MON_MSG_PTR zero-page pointer ($16/$17).
;   * K_GET_KEYSTROKE ($FF09) is NON-blocking: it sets carry (and returns the key
;     in A) only when a key is waiting -- so spin on it to actually wait.
;   * The key is in A, but printing the "YOU PRESSED:" string clobbers A, so we
;     save it (PHA) across that print and restore it (PLA) before echoing.
;
; Enter via the monitor:  W:0800  then type the byte block at the bottom,  G:0800
    .org $0800

K_PRINT_CHAR    = $FF00         ; print A as a character
K_PRINT_MESSAGE = $FF03         ; print null-terminated string at (MON_MSG_PTR)
K_PRINT_NEWLINE = $FF06         ; print CR/LF
K_GET_KEYSTROKE = $FF09         ; C set + A=key when one is waiting (non-blocking)
K_CLEAR_SCREEN  = $FF0C         ; clear + home
MON_MSG_PTR     = $16           ; K_PRINT_MESSAGE reads its string pointer here ($16/$17)

START:
    JSR K_CLEAR_SCREEN          ; 20 0C FF

    ; Print the greeting
    LDA #<WELCOME               ; A9 32
    STA MON_MSG_PTR             ; 85 16
    LDA #>WELCOME               ; A9 08
    STA MON_MSG_PTR+1           ; 85 17
    JSR K_PRINT_MESSAGE         ; 20 03 FF

    ; Print the prompt
    LDA #<PROMPT                ; A9 43
    STA MON_MSG_PTR             ; 85 16
    LDA #>PROMPT                ; A9 08
    STA MON_MSG_PTR+1           ; 85 17
    JSR K_PRINT_MESSAGE         ; 20 03 FF

    ; Spin until a key is actually available
WAIT:
    JSR K_GET_KEYSTROKE         ; 20 09 FF  -> C set + A=key when ready
    BCC WAIT                    ; 90 FB     (no key yet -> keep waiting)

    PHA                         ; 48        (save the key; the next print clobbers A)
    LDA #<PRESSED               ; A9 55
    STA MON_MSG_PTR             ; 85 16
    LDA #>PRESSED               ; A9 08
    STA MON_MSG_PTR+1           ; 85 17
    JSR K_PRINT_MESSAGE         ; 20 03 FF
    PLA                         ; 68        (restore the key)
    JSR K_PRINT_CHAR            ; 20 00 FF  (echo it)
    JSR K_PRINT_NEWLINE         ; 20 06 FF
    RTS                         ; 60

WELCOME: .byte "HELLO FROM MFC!", $0D, 0     ; 48 45 4C 4C 4F 20 46 52 4F 4D 20 4D 46 43 21 0D 00
PROMPT:  .byte "PRESS ANY KEY...", $0D, 0    ; 50 52 45 53 53 20 41 4E 59 20 4B 45 59 2E 2E 2E 0D 00
PRESSED: .byte "YOU PRESSED: ", 0            ; 59 4F 55 20 50 52 45 53 53 45 44 3A 20 00

; Enter these bytes.
; 20 0C FF A9 32 85 16 A9 08 85 17 20 03 FF A9 43
; 85 16 A9 08 85 17 20 03 FF 20 09 FF 90 FB 48 A9
; 55 85 16 A9 08 85 17 20 03 FF 68 20 00 FF 20 06
; FF 60 48 45 4C 4C 4F 20 46 52 4F 4D 20 4D 46 43
; 21 0D 00 50 52 45 53 53 20 41 4E 59 20 4B 45 59
; 2E 2E 2E 0D 00 59 4F 55 20 50 52 45 53 53 45 44
; 3A 20 00
