; ================================================================
; monitor.asm - the MFC monitor, as module bank 4
; ================================================================
; The interactive monitor: command loop, parser, and the R:/W:/G:/F:/M:/X:/T:/Z:/
; D:/H: commands. It used to live in kernel ROM; the kernel is now just the BIOS
; (screen, keyboard, conversion, pager, IRQ/NMI, sound, bank launching, $FF00).
;
; Why a bank and not a disk program: a .PRG loads at $0800, which is exactly the
; memory a monitor exists to inspect -- it would overwrite the program under test.
; A bank costs no user RAM, loads instantly, and is present with no disk at all.
; The price is that the monitor cannot see its own window: R:B000-DFFF shows this
; ROM, not bank-0 RAM, and sibling banks (BASIC, ASM, FORTH) are invisible for the
; same reason. Everything else in the map -- zero page, stack, page 2, all of user
; RAM, the DOS ROM and the kernel ROM -- reads normally.
;
; Reaching the BIOS: this is a separate link unit, so the only kernel addresses it
; can bind to are the published $FF00 jump table. The equates below give the BIOS
; routines their old names at their ABI addresses, which is what lets the command
; code below stay exactly as it was written when it lived in kernel.asm.
;
; Shared addresses come from kernel_vars.inc, the same file the kernel includes, so
; the two halves cannot disagree about where MON_CMDBUF and friends live -- which
; matters because the DOS shell parses its own arguments out of that same buffer.
; ================================================================

.PC02                               ; 65C02 instruction set

.include "kernel_vars.inc"

; ----------------------------------------------------------------
; Kernel BIOS entry points (the $FF00 ABI)
; ----------------------------------------------------------------
PRINT_CHAR          = $FF00
PRINT_MESSAGE       = $FF03
PRINT_NEWLINE       = $FF06
PRINT_NEWLINE_PAGED = $FF06         ; the kernel pages every newline; same entry
GET_KEYSTROKE       = $FF09
CLEAR_SCREEN        = $FF0C
GET_RANDOM_NUMBER   = $FF0F
RETURN_FROM_MODULE  = $FF12         ; unmaps this bank, then re-enters the DOS
READ_COMMAND_LINE   = $FF15
HEX_QUAD_TO_ADDR    = $FF18
PRINT_HEX_BYTE      = $FF1B
LAUNCH_BY_NAME      = $FF21
LIST_MODULES        = $FF24
PRINT_DEC           = $FF27
PARSE_DEC_ABI       = $FF2A
SET_ATTR            = $FF2D
PRINT_HELP_LINE     = $FF30
SOUND_TONE          = $FF33
SOUND_OFF           = $FF36
GET_JIFFIES         = $FF39
HEX_PAIR_TO_BYTE    = $FF3C
PARSE_DECIMAL_VALUE = $FF3F

; (DOS_WARM and the rest of the MFC-DOS ABI come from kernel_vars.inc.)

.org $B000

; ----------------------------------------------------------------
; Entry table - the first bytes of the bank, at fixed addresses
; ----------------------------------------------------------------
; MON_ENTRY_COLD/BREAK in kernel_vars.inc name these. The kernel checks that the
; first byte is not $00 before jumping, which is how an uninstalled bank is
; detected, so these must stay real instructions at the very base of the window.
    JMP MONITOR_COLD                ; $B000 - DOS 'MON' via K_MON_ENTRY
    JMP MONITOR_MAIN                ; $B003 - NMI STOP break-in, no banner

; ----------------------------------------------------------------
; PRINT_MSG_AY - set the message pointer from A/Y and print
; ----------------------------------------------------------------
; A private 4-byte copy rather than a 21st ABI slot: it is pure sugar over
; PRINT_MESSAGE ($FF03) and the monitor is its only remaining caller.
PRINT_MSG_AY:
    STA MON_MSG_PTR_LO
    STY MON_MSG_PTR_HI
    JMP PRINT_MESSAGE               ; tail call



; Save current command to last command buffer
; Should be called after successful command parsing (not during data entry)
; Modifies: A, X, Y
SAVE_COMMAND:
    ; Check if command is empty or too long
    LDA MON_CMDLEN              ; Load current command length
    BEQ SAVE_CMD_SKIP           ; If empty, don't save
    CMP #MON_CMDBUF_LEN         ; Check if too long
    BCS SAVE_CMD_SKIP           ; If too long, don't save

    ; Copy current command to last command buffer
    LDX #$00                    ; Initialize copy index

SAVE_CMD_COPY_LOOP:
    CPX MON_CMDLEN              ; Have we copied all characters?
    BCS SAVE_CMD_COPY_DONE      ; If so, we're done copying

    LDA MON_CMDBUF,X            ; Load character from current command buffer
    STA MON_LAST_CMD_BUF,X      ; Store in last command buffer
    INX                         ; Move to next character
    BRA SAVE_CMD_COPY_LOOP      ; Continue copying

SAVE_CMD_COPY_DONE:
    ; Update last command length
    LDA MON_CMDLEN              ; Get current command length
    STA MON_LAST_CMD_LEN        ; Store as last command length
    RTS

SAVE_CMD_SKIP:
    ; Don't save this command
    RTS

; Print the monitor prompt based on current mode
; Print current address from MON_CURRADDR_HI/LO as 4 hex digits
; Modifies: A, X
PRINT_CURRENT_ADDRESS:
    ; Print high byte
    LDA MON_CURRADDR_HI         ; Load high byte
    JSR PRINT_HEX_BYTE          ; Print byte as two hex digits

    ; Print low byte
    LDA MON_CURRADDR_LO         ; Load low byte
    JMP PRINT_HEX_BYTE  ; Print byte as two hex digits

; Unified monitor prompt printing routine
; Prints: [mode:]address> (e.g., "W:8000> " or "8000> ")
; Modifies: A, X, Y
PRINT_MONITOR_PROMPT:
    ; Ensure the prompt starts at the left margin. A program run via G: (or any
    ; output) may leave the cursor mid-line; emit a newline first, but only when
    ; not already at column 0 so ordinary commands don't get a blank line.
    LDA CURSOR_X
    BEQ PROMPT_AT_MARGIN
    JSR PRINT_NEWLINE
PROMPT_AT_MARGIN:
    LDA MON_MODE                ; Load current mode
    TAX                         ; Use as index
    LDA MODE_PREFIX_TABLE,X     ; Get prefix character
    BEQ PRINT_ADDRESS_ONLY      ; If null, skip prefix

    ; Print mode prefix and colon
    JSR PRINT_CHAR              ; Print mode character
    LDA #ASCII_COLON            ; ':' character
    JSR PRINT_CHAR

PRINT_ADDRESS_ONLY:
    JSR PRINT_CURRENT_ADDRESS   ; Print current address
    LDA #'>'                    ; Prompt character
    JSR PRINT_CHAR
    LDA #ASCII_SPACE            ; Space after prompt
    JMP PRINT_CHAR

; ================================================================
; MONITOR COMMAND PARSER
; ================================================================

; Parse and execute a command from command buffer
; Input: Command text in MON_CMDBUF, length in MON_CMDLEN
; Output: Command executed, results displayed to screen, errors handled
; Modifies: A, X, Y, and various command-specific variables
; Note: Uses jump table for command dispatch, validates command syntax and parameters
PARSE_COMMAND:
    ; Initialize parser state
    STZ MON_PARSE_PTR
    STZ MON_ERROR_FLAG
    LDA MON_CMDLEN              ; (also the empty-command test below)

    ; Check if command is empty
    BNE PARSE_CMD_START
    JMP PARSE_CMD_DONE

PARSE_CMD_START:
    ; Get first character
    LDX #$00
    LDA MON_CMDBUF,X

    ; Check for special character '?' (help)
    CMP #'?'                    ; ASCII $3F
    BEQ PARSE_CMD_HELP_DIRECT   ; Jump directly to help

    ; 'Q' quits the monitor back to the DOS prompt.
    CMP #'Q'
    BEQ PARSE_CMD_QUIT_DIRECT

    ; ESC at the command prompt is a clean exit/no-op, not a syntax error
    CMP #ASCII_ESC
    BEQ PARSE_CMD_EXIT_DIRECT

    ; Quick range check - is it between 'B' and 'Z'?
    CMP #$42                    ; 'B'
    BCC PARSE_CMD_ERROR_JMP     ; Less than 'B' - jump to local error handler
    CMP #$5B                    ; 'Z'+1
    BCS PARSE_CMD_ERROR_JMP     ; Greater than 'Z' - jump to local error handler

    ; Get index from mapping table
    SEC
    SBC #$42                    ; Subtract 'B' to get offset
    TAX
    LDA CMD_INDEX_MAP,X         ; Get command index
    CMP #$FF                    ; Is it invalid?
    BEQ PARSE_CMD_ERROR_JMP

    ; Valid command - use compact jump table
    TAX
    LDA CMD_JUMP_COMPACT_LO,X
    STA JUMP_VECTOR
    LDA CMD_JUMP_COMPACT_HI,X
    STA JUMP_VECTOR+1
    JMP (JUMP_VECTOR)

PARSE_CMD_HELP_DIRECT:
    ; Direct jump to help for '?' character
    JMP PARSE_CMD_HELP

PARSE_CMD_QUIT_DIRECT:
    ; Quit the monitor: unmap this bank and return to the MFC/OS DOS shell. Must go
    ; through RETURN_FROM_MODULE ($FF12), not straight to DOS_WARM -- leaving the
    ; monitor mapped would hide 12 KB of the DOS's scratch RAM behind this ROM.
    JMP RETURN_FROM_MODULE

PARSE_CMD_EXIT_DIRECT:
    ; Direct jump to the clean-exit handler for a bare ESC
    JMP PARSE_CMD_EXIT

; Local error handler for range check jumps (within branch range)
PARSE_CMD_ERROR_JMP:
    JMP PARSE_CMD_ERROR         ; Jump to main error handler

; ================================================================
; MONITOR COMMAND PARSING ROUTINES
; ================================================================

PARSE_CMD_CLEAR:
    JSR PARSE_COLON_COMMAND     ; Parse C: format
    BCS PARSE_CMD_ERROR_JMP2    ; If error, jump to local error handler
    JMP CMD_CLEAR_SCREEN  ; Execute clear screen command

PARSE_CMD_STACK:
    JSR PARSE_COLON_COMMAND     ; Parse T: format
    BCS PARSE_CMD_ERROR_JMP2    ; If error, jump to local error handler
    JMP CMD_DUMP_STACK  ; Execute stack dump command

PARSE_CMD_ZERO:
    JSR PARSE_COLON_COMMAND     ; Parse Z: format
    BCS PARSE_CMD_ERROR_JMP2    ; If error, jump to local error handler
    JMP CMD_DUMP_ZERO_PAGE  ; Execute zero page dump command

; ================================================================
; DECIMAL TO HEX COMMAND (D:nnnnn)
; ================================================================
; Parse and execute D:nnnnn command
; Input: MON_CMDBUF contains "D:nnnnn" (decimal number 0-65535)
; Output: Displays 4-digit hex equivalent
; Modifies: A, X, Y, MON_CURRADDR_HI/LO, DEC_TEMP_LO/HI, DEC_DIGIT_IDX
; Errors: MSG_VALUE_ERROR (invalid decimal), MSG_RANGE_ERROR (>65535)
; ================================================================
PARSE_CMD_DECIMAL_CHECK:
    ; Validate colon at position 1
    LDA MON_CMDBUF+1
    CMP #ASCII_COLON
    BNE PARSE_CMD_ERROR_JMP2

    ; Check if we have digits after colon
    LDA MON_CMDLEN
    CMP #$03                     ; Need at least "D:n"
    BCC PARSE_CMD_ERROR_JMP2     ; Too short

    JMP CMD_DECIMAL_TO_HEX

; ================================================================
; HEX TO DECIMAL COMMAND (H:xxxx)
; ================================================================
; Parse and execute H:xxxx command
; Input: MON_CMDBUF contains "H:xxxx" (hex value 0000-FFFF)
; Output: Displays decimal equivalent (0-65535)
; Modifies: A, X, Y, MON_CURRADDR_HI/LO, DEC_TEMP_LO/HI, DEC_DIGIT_IDX
; Errors: MSG_VALUE_ERROR (invalid hex input)
; ================================================================
PARSE_CMD_HEX_TO_DEC:
    ; Validate colon at position 1
    LDA MON_CMDBUF+1
    CMP #ASCII_COLON
    BNE @error

    ; Check if we have 4 hex digits after colon
    LDA MON_CMDLEN
    CMP #$06                    ; Need exactly "H:xxxx" (6 chars)
    BNE @error                  ; Wrong length

    ; Save current address (this command should not modify it)
    LDA MON_CURRADDR_LO
    PHA
    LDA MON_CURRADDR_HI
    PHA

    ; Parse the hex address using existing routine
    LDX #$02                    ; Start at position 2 (after "H:")
    JSR HEX_QUAD_TO_ADDR        ; Parse hex into MON_CURRADDR
    BCS @error_restore          ; If error, restore and jump to error handler

    ; Execute conversion
    JSR CMD_HEX_TO_DECIMAL

    ; Restore current address
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    JMP PARSE_CMD_DONE

@error_restore:
    ; Restore current address before error exit
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO

@error:
    ; Display VALUE? error and return
    LDA #$01
    STA MON_ERROR_FLAG
    JMP PRINT_VALUE_ERROR

PARSE_CMD_HELP:
    ; Help can be invoked with just '?' (no colon required)
    JMP CMD_SHOW_HELP  ; Execute help command

; Second local error handler for new commands (within branch range)
PARSE_CMD_ERROR_JMP2:
    JMP PARSE_CMD_ERROR         ; Jump to main error handler

PARSE_CMD_EXIT:
    JMP CMD_EXIT_MODE  ; Execute exit mode command

; Commands with colon syntax (W:, R:, G:)
PARSE_CMD_WRITE_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse W:xxxx format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JMP CMD_WRITE_MODE  ; Execute write mode command

PARSE_CMD_READ_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse R:xxxx or R:xxxx-yyyy format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JMP CMD_READ_MEMORY  ; Execute read memory command

PARSE_CMD_GO_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse G:xxxx format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JMP CMD_RUN_PROGRAM  ; Execute run program command

PARSE_CMD_FILL_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse F:xxxx-yyyy format
    BCS PARSE_CMD_RANGE_ERROR   ; If address parsing error, show range error
    JSR PARSE_FILL_VALUE        ; Parse comma and fill value
    BCS PARSE_CMD_VALUE_ERROR   ; If value parsing error, show value error
    JMP CMD_FILL_MEMORY  ; Execute fill memory command

PARSE_CMD_MOVE_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse M:xxxx-yyyy format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR PARSE_MOVE_PARAMS       ; Parse comma, destination, and mode
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JMP CMD_MOVE_MEMORY  ; Execute move/copy memory command

PARSE_CMD_SEARCH_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse X:xxxx-yyyy format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR PARSE_SEARCH_PARAMS     ; Parse comma and hex pattern
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JMP CMD_SEARCH_MEMORY  ; Execute memory search command

PARSE_CMD_ERROR:
    ; Display error message for invalid command
    LDA #$01                    ; Set error flag
    STA MON_ERROR_FLAG
    JMP PRINT_ERROR_MSG  ; Print error message

PARSE_CMD_VALUE_ERROR:
    ; Display value error message for invalid hex values
    LDA #$01                    ; Set error flag
    STA MON_ERROR_FLAG
    JMP PRINT_VALUE_ERROR  ; Print value error message

PARSE_CMD_RANGE_ERROR:
    ; Display range error message for invalid address ranges
    LDA #$01                    ; Set error flag
    STA MON_ERROR_FLAG
    JMP PRINT_RANGE_ERROR  ; Print range error message

PARSE_CMD_DONE:
    RTS

; Parse colon command syntax for address specification
; Input: Command in MON_CMDBUF (e.g., "W:8000", "R:8000-8010", "L:8000,filename")
; Output: Address(es) in MON_CURRADDR_HI/LO and optionally MON_STARTADDR/ENDADDR, Carry clear if valid, set if error
; Modifies: A, X, Y, address variables
; Note: Handles single addresses, ranges (dash), and parameters (comma), validates hex syntax
PARSE_COLON_COMMAND:
    ; Save current address to restore on error
    LDA MON_CURRADDR_LO
    PHA
    LDA MON_CURRADDR_HI
    PHA

    ; Clear end address to ensure single address commands work correctly
    STZ MON_ENDADDR_LO
    STZ MON_ENDADDR_HI

    LDX #$01                    ; Position after command character
    LDA MON_CMDBUF,X            ; Load second character
    CMP #ASCII_COLON            ; Is it a colon?
    BNE PARSE_COLON_ERROR       ; If not, error

    INX                         ; X now points to first character after colon
    CPX MON_CMDLEN              ; Are we at end of command?
    BEQ PARSE_COLON_NO_ADDR     ; If so, use current address

    ; Parse the hex address
    JSR HEX_QUAD_TO_ADDR        ; Parse 4-hex-digit address
    BCS PARSE_COLON_ERROR       ; If error, return error

    ; Check if there's more (for range commands like R:8000-8010)
    CPX MON_CMDLEN              ; Are we at end of command?
    BEQ PARSE_COLON_SUCCESS     ; If so, single address is complete

    ; Check for dash (range separator) or comma (parameter separator)
    LDA MON_CMDBUF,X            ; Load next character
    CMP #ASCII_DASH             ; Is it a dash?
    BEQ PARSE_RANGE             ; If dash, parse range
    CMP #ASCII_COMMA                    ; Is it a comma?
    BEQ PARSE_COLON_SUCCESS     ; If comma, single address with parameters

    ; (The end-of-command test above already handled X == MON_CMDLEN; X has not
    ; changed since, so repeating it here would be unreachable.)
    JMP PARSE_COLON_ERROR       ; Otherwise invalid character after address

PARSE_RANGE:
    LDA MON_CURRADDR_LO         ; Copy current address to start address
    STA MON_STARTADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_STARTADDR_HI

    INX                         ; Move past dash
    JSR HEX_QUAD_TO_ADDR        ; Parse end address into MON_CURRADDR
    BCS PARSE_COLON_ERROR       ; If error, return error

    ; Copy current address to end address
    LDA MON_CURRADDR_LO         ; Copy to end address
    STA MON_ENDADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_ENDADDR_HI

    ; Restore start address to current address for processing
    LDA MON_STARTADDR_LO        ; Restore start address to current
    STA MON_CURRADDR_LO
    LDA MON_STARTADDR_HI
    STA MON_CURRADDR_HI
    ; Fall through to success

PARSE_COLON_NO_ADDR:
    ; Current address is already in MON_CURRADDR_HI/LO
    ; Discard saved values
    PLA
    PLA
    CLC                         ; Clear carry for success
    RTS

PARSE_COLON_SUCCESS:
    ; Discard saved address values (new address is valid)
    PLA
    PLA
    CLC                         ; Clear carry for success
    RTS

PARSE_COLON_ERROR:
    ; Restore original address
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    SEC                         ; Set carry for error
    RTS

; ----------------------------------------------------------------------------
; Shared command-line parser helpers (used by the F:, M:, X:, L:/S: parsers)
; ----------------------------------------------------------------------------
; Skip spaces in the command buffer.
; In:  X = current position, MON_CMDLEN = command length.
; Out: carry CLEAR -> X points at the first non-space char, A = that char.
;      carry SET   -> reached end of buffer (no non-space char found).
SKIP_SPACES:
    CPX MON_CMDLEN              ; At or past end of command?
    BCS SKIP_SPACES_END         ; Yes -> return carry set
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BEQ SKIP_SPACES_NEXT
    CLC                         ; Found a non-space char; report success
    RTS
SKIP_SPACES_NEXT:
    INX                         ; Consume the space
    JMP SKIP_SPACES
SKIP_SPACES_END:
    RTS                         ; carry already set by CPX

; Skip spaces, require a single comma separator, then skip trailing spaces,
; leaving X at the first character of the next field.
; In:  X = current position, MON_CMDLEN = length.
; Out: carry CLEAR on success (X at next field, A = that char);
;      carry SET on end-of-buffer or a missing comma.
EXPECT_COMMA:
    JSR SKIP_SPACES
    BCS EXPECT_COMMA_FAIL       ; ran off the end before a comma
    CMP #ASCII_COMMA                    ; comma separator?
    BNE EXPECT_COMMA_FAIL
    INX                         ; consume the comma
    JMP SKIP_SPACES             ; tail call: skip trailing spaces, return its carry
EXPECT_COMMA_FAIL:
    SEC
    RTS

; Parse fill value parameter (expects comma followed by 2-digit hex byte)
; Input: Command buffer positioned after address range
; Output: Fill value in MON_FILL_VALUE, Carry clear if success
; Modifies: A, X, Y
PARSE_FILL_VALUE:
    JSR EXPECT_COMMA            ; skip spaces, require comma, skip spaces
    BCS PARSE_FILL_ERROR

PARSE_FILL_GET_VALUE:
    ; Parse 2-digit hex value
    JSR HEX_PAIR_TO_BYTE        ; Convert 2 hex digits to byte
    BCS PARSE_FILL_ERROR        ; If error, exit
    STA MON_FILL_VALUE          ; Store fill value
    CLC                         ; Clear carry for success
    RTS

PARSE_FILL_ERROR:
    RTS

; Parse move/copy parameters (expects comma, destination address, and mode)
; Input: Command buffer positioned after address range
; Output: Destination address in MON_DEST_ADDR_HI/LO, mode in MON_COPY_MODE, Carry clear if success
; Modifies: A, X, Y
PARSE_MOVE_PARAMS:
    JSR EXPECT_COMMA            ; skip spaces, require comma, skip spaces
    BCS PARSE_MOVE_ERROR_JMP

PARSE_MOVE_GET_DEST:
    ; Parse 4-digit hex destination address
    ; Save current address variables
    LDA MON_CURRADDR_LO
    PHA
    LDA MON_CURRADDR_HI
    PHA

    JSR HEX_QUAD_TO_ADDR        ; Convert 4 hex digits to address (into MON_CURRADDR)
    BCS PARSE_MOVE_RESTORE_ERROR ; If error, restore and exit

    ; Copy result to destination address
    LDA MON_CURRADDR_LO         ; Get parsed destination
    STA MON_DEST_ADDR_LO        ; Store destination address
    LDA MON_CURRADDR_HI
    STA MON_DEST_ADDR_HI

    ; Restore current address variables
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO

    ; Require the second comma (before the mode digit), skipping spaces
    JSR EXPECT_COMMA
    BCS PARSE_MOVE_ERROR_JMP

PARSE_MOVE_GET_MODE:
    ; Parse 1-digit mode (0=copy, 1=move)
    LDA MON_CMDBUF,X            ; Load mode character
    CMP #'0'                    ; Is it '0'?
    BEQ PARSE_MOVE_MODE_COPY    ; Yes, copy mode
    CMP #'1'                    ; Is it '1'?
    BEQ PARSE_MOVE_MODE_MOVE    ; Yes, move mode
    JMP PARSE_MOVE_ERROR_JMP    ; Neither, error

PARSE_MOVE_MODE_COPY:
    STZ MON_COPY_MODE           ; Copy mode (0)
    CLC                         ; Clear carry for success
    RTS

PARSE_MOVE_MODE_MOVE:
    LDA #$01                    ; Move mode
    STA MON_COPY_MODE
    CLC                         ; Clear carry for success
    RTS

; Local error handler for branch range jumps (within branch range)
PARSE_MOVE_ERROR_JMP:
    JMP PARSE_MOVE_ERROR        ; Jump to main error handler

PARSE_MOVE_RESTORE_ERROR:
    ; Restore address variables after error
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    ; Fall through to error

PARSE_MOVE_ERROR:
    SEC                         ; Set carry for error
    RTS

; Parse search parameters (expects comma followed by 1-16 hex bytes)
; Input: Command buffer positioned after address range
; Output: Pattern in MON_SEARCH_PATTERN, length in MON_PATTERN_LEN, Carry clear if success
; Modifies: A, X, Y
PARSE_SEARCH_PARAMS:
    JSR EXPECT_COMMA            ; skip spaces, require comma, skip spaces
    BCS PARSE_SEARCH_ERROR

PARSE_SEARCH_GET_PATTERN:
    ; Parse hex pattern bytes (1-16 bytes)
    STZ MON_PATTERN_LEN         ; Initialize pattern length
    LDY #$00                    ; Pattern buffer index

PARSE_SEARCH_PATTERN_LOOP:
    ; Check if we're at end of command
    CPX MON_CMDLEN              ; At end of command?
    BCS PARSE_SEARCH_PATTERN_DONE ; If so, we're done with pattern

    ; Check if we've reached maximum pattern length
    LDA MON_PATTERN_LEN
    CMP #$10                    ; 16 bytes maximum
    BCS PARSE_SEARCH_PATTERN_DONE ; If at max, we're done

    ; Parse two-character hex byte
    JSR HEX_PAIR_TO_BYTE        ; Parse hex pair (X points to first char)
    BCS PARSE_SEARCH_ERROR      ; If error, exit

    ; Store byte in pattern buffer
    STA MON_SEARCH_PATTERN,Y    ; Store pattern byte
    INC MON_PATTERN_LEN         ; Increment pattern length
    INY                         ; Move to next pattern position

    ; Skip any spaces before next hex pair (end of buffer = pattern complete)
    JSR SKIP_SPACES
    BCS PARSE_SEARCH_PATTERN_DONE
    JMP PARSE_SEARCH_PATTERN_LOOP

PARSE_SEARCH_PATTERN_DONE:
    ; Check if we have at least one pattern byte
    LDA MON_PATTERN_LEN
    BEQ PARSE_SEARCH_ERROR      ; If no pattern bytes, error
    CLC                         ; Clear carry for success
    RTS

PARSE_SEARCH_ERROR:
    RTS

; Print error message for invalid commands
; Modifies: A, X, Y
PRINT_ERROR_MSG:
    ; Print syntax error message
    LDA #<MSG_SYNTAX_ERROR
    LDY #>MSG_SYNTAX_ERROR
    JMP PRINT_MSG_AY            ; tail call: PRINT_MESSAGE's RTS returns to caller

; Print value error message for invalid hex values
; Modifies: A, X, Y
PRINT_VALUE_ERROR:
    ; Print value error message
    LDA #<MSG_VALUE_ERROR
    LDY #>MSG_VALUE_ERROR
    JMP PRINT_MSG_AY            ; tail call: PRINT_MESSAGE's RTS returns to caller

; Print range error message for invalid address ranges
; Modifies: A, X, Y
PRINT_RANGE_ERROR:
    ; Print range error message
    LDA #<MSG_RANGE_ERROR
    LDY #>MSG_RANGE_ERROR
    JMP PRINT_MSG_AY            ; tail call: PRINT_MESSAGE's RTS returns to caller

; Validate address range (start <= end)
; Input: MON_CURRADDR_HI/LO (start address), MON_ENDADDR_HI/LO (end address)
; Output: Carry clear if valid range, set if invalid
; Modifies: A
VALIDATE_ADDRESS_RANGE:
    ; Compare high bytes first
    LDA MON_CURRADDR_HI         ; Load start address high byte
    CMP MON_ENDADDR_HI          ; Compare with end address high byte
    BCC RANGE_VALID             ; start < end (high), valid
    BNE RANGE_INVALID           ; start > end (high), invalid

    ; High bytes equal, compare low bytes
    LDA MON_CURRADDR_LO         ; Load start address low byte
    CMP MON_ENDADDR_LO          ; Compare with end address low byte
    BCC RANGE_VALID             ; start < end (low), valid
    BEQ RANGE_VALID             ; start = end (low), valid (single byte)

RANGE_INVALID:
    SEC                         ; Set carry for invalid range
    RTS

; ----------------------------------------------------------------
; RANGE_HITS_STATE / DEST_HITS_STATE - refuse a bulk write over the monitor's own
; loop-control state
; ----------------------------------------------------------------
; F: and M: keep their live pointer, their loop bound and their fill byte in
; ordinary RAM (MON_CURRADDR $14/$15, MON_ENDADDR $026E/F, MON_FILL_VALUE $0279,
; MOVE_DEST/MOVE_DEND $25-$28), and re-read them every iteration. A range that
; covers those bytes therefore rewrites the loop as it runs:
;   F:0000-00FF,00  zeroed MON_CURRADDR mid-loop -> pointer reset -> ran forever,
;                   dead until reset
;   F:0200-02FF,AA  wrote $AA into MON_ENDADDR -> bound became $AAAA -> wiped all
;                   of user RAM ($0800-$87FF) and then printed OK
;   M:0800-08FF,0000,0  destination walked over MON_CURRADDR/JUMP_VECTOR and the
;                   copy ran away
; Rather than validate each variable, refuse any range overlapping the whole
; contiguous span the monitor lives in, MON_STATE_FIRST..MON_STATE_LAST. That also
; covers the command buffer holding the command being executed, which is equally
; unsurvivable. Individual bytes in the span are still reachable with W:.
; Out: carry set = the range overlaps monitor state (refuse). Modifies: A.
; ----------------------------------------------------------------
MON_STATE_FIRST    = MON_CURRADDR_LO    ; $0014 - first byte of monitor workspace
MON_STATE_LAST     = MON_COPY_MODE      ; $027C - last byte of monitor page-2 state

RANGE_HITS_STATE:
    ; safe if start > MON_STATE_LAST (range lies entirely above the workspace)
    LDA #<MON_STATE_LAST
    CMP MON_CURRADDR_LO
    LDA #>MON_STATE_LAST
    SBC MON_CURRADDR_HI
    BCC RHS_SAFE
    ; safe if end < MON_STATE_FIRST (range lies entirely below it)
    LDA MON_ENDADDR_LO
    CMP #<MON_STATE_FIRST
    LDA MON_ENDADDR_HI
    SBC #>MON_STATE_FIRST
    BCC RHS_SAFE
    SEC                         ; overlaps -> caller must refuse
    RTS
RHS_SAFE:
    CLC
    RTS

; Same test for M:'s destination range (MOVE_DEST..MOVE_DEND), which is computed
; after the source range has been validated.
DEST_HITS_STATE:
    LDA #<MON_STATE_LAST
    CMP MOVE_DEST_LO
    LDA #>MON_STATE_LAST
    SBC MOVE_DEST_HI
    BCC DHS_SAFE
    LDA MOVE_DEND_LO
    CMP #<MON_STATE_FIRST
    LDA MOVE_DEND_HI
    SBC #>MON_STATE_FIRST
    BCC DHS_SAFE
    SEC
    RTS
DHS_SAFE:
    CLC
    RTS

RANGE_VALID:
    CLC                         ; Clear carry for valid range
    RTS

; ----------------------------------------------------------------
; Clear screen command - Clears all screen memory and resets cursor to origin
; Input: None (address in MON_CURRADDR_HI/LO is ignored for clear command)
; Output: Screen memory cleared to spaces, cursor at (0,0), screen pointer reset
; Modifies: A, X
; Note: This is a one-shot command that returns to command prompt
CMD_CLEAR_SCREEN:
    ; CLEAR_SCREEN already resets the cursor and screen pointer before its RTS,
    ; so just tail-call it (its RTS returns to the parser).
    JMP CLEAR_SCREEN

; Stack dump command - Display complete stack memory with paging support
; Input: None (dumps entire stack page $0100-$01FF regardless of current address)
; Output: Formatted hex dump of stack memory to screen with addresses and data
; Modifies: A, X, Y
; Note: Uses paging - user can press ESC to abort or ENTER to continue
; Note: Preserves MON_CURRADDR (DUMP_MEMORY_RANGE walks it across the range).
;       The stack page is the data being dumped here, so the saved address is
;       stashed in MON_DEST_ADDR (used only by the M: command) as scratch space
;       rather than on the stack, to avoid perturbing the displayed bytes.
CMD_DUMP_STACK:
    LDA #$01                    ; the stack page ($0100-$01FF)
    JMP DUMP_ONE_PAGE

; Zero page dump command - Display complete zero page memory with paging support
CMD_DUMP_ZERO_PAGE:
    LDA #$00                    ; zero page ($0000-$00FF)
    ; fall through

; ----------------------------------------------------------------
; DUMP_ONE_PAGE - dump the 256-byte page whose high address byte is in A
; ----------------------------------------------------------------
; Shared by T: and Z:, which were near-identical and differed only in the page and
; in how they preserved the current address: T: parked it in MON_DEST_ADDR (M:'s
; destination variable) while Z: used the stack. Both use the stack now.
; Input: A = page high byte.
; Output: the page is dumped, paged, ESC-abortable; MON_CURRADDR is preserved so
;         the prompt address is unaffected.
; ----------------------------------------------------------------
DUMP_ONE_PAGE:
    STA MON_STARTADDR_HI
    STA MON_ENDADDR_HI
    ; Snapshot the page BEFORE printing anything. PRINT_CHAR rewrites its own
    ; zero-page scratch ($16/$17 message pointer, $1A-$1D VIC cell/temp) between
    ; bytes, and the dump walks MON_CURRADDR ($14/$15) as its cursor -- so a live
    ; Z: reported the dump's own state rather than what was in memory: W:0014 AB CD
    ; followed by Z: showed "14 00", never AB CD. Read with absolute,X, since any
    ; zero-page pointer used for the copy would itself appear in its own dump.
    LDX #$00
    CMP #$01
    BEQ @snap1
@snap0:
    LDA $0000,X
    STA MON_SNAP_BUF,X
    INX
    BNE @snap0
    BRA @snapped
@snap1:
    LDA $0100,X
    STA MON_SNAP_BUF,X
    INX
    BNE @snap1
@snapped:
    INC MON_DUMP_SNAP           ; make the dump read the snapshot, not live memory
    LDA MON_CURRADDR_LO         ; preserve the prompt address
    PHA
    LDA MON_CURRADDR_HI
    PHA
    STZ CMD_LINE_COUNT          ; Reset command line counter
    STZ PAGE_ABORT_FLAG         ; Reset abort flag
    STZ MON_STARTADDR_LO
    LDA #$FF
    STA MON_ENDADDR_LO
    JSR DUMP_MEMORY_RANGE       ; Use common memory dump routine
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    RTS

; ================================================================
; CMD_DECIMAL_TO_HEX - Main decimal-to-hex conversion routine
; ================================================================
; Converts decimal string to hex and displays result
; Input: MON_CMDBUF contains decimal string starting at position 2 (after "D:")
;        MON_CMDLEN contains total command length
; Output: Displays 4-digit hex result, returns to prompt
; Errors: MSG_VALUE_ERROR (invalid digit), MSG_RANGE_ERROR (overflow)
; ================================================================
CMD_DECIMAL_TO_HEX:
    ; Initialize result to zero
    STZ DEC_RESULT_LO
    STZ DEC_RESULT_HI

    ; Initialize parser - start at position 2 (after "D:")
    LDA #$02
    STA MON_PARSE_PTR

    ; Initialize digit counter
    STZ DEC_DIGIT_IDX

    ; Call decimal parser (now a pure primitive: carry set = error, A = code)
    JSR PARSE_DECIMAL_VALUE
    BCS CMD_DEC_ERROR

    ; Success - print result
    LDA #'$'
    JSR PRINT_CHAR

    ; Display result in hex
    LDA DEC_RESULT_HI
    JSR PRINT_HEX_BYTE

    LDA DEC_RESULT_LO
    JSR PRINT_HEX_BYTE

    JMP PRINT_NEWLINE

CMD_DEC_ERROR:
    ; A = error code (1 = invalid digit -> VALUE?, 2 = overflow -> RANGE?). The
    ; parser no longer prints; the command maps the code to a monitor message.
    PHA
    LDA #$01
    STA MON_ERROR_FLAG
    PLA
    CMP #$02
    BEQ @range
    JMP PRINT_VALUE_ERROR       ; tail
@range:
    JMP PRINT_RANGE_ERROR       ; tail

; ================================================================
; HEX TO DECIMAL CONVERSION COMMAND
; ================================================================

; Main conversion routine (the monitor's H: command)
; Input: MON_CURRADDR_HI/LO = 16-bit value to convert
; Output: Decimal value printed to screen as #NNNNN
; Modifies: A, X, Y, the $35-$39 decimal workspace
; Now a thin wrapper over the shared kernel PRINT_DEC ($FF27): build a 32-bit
; value (16-bit zero-extended) and print it with no field padding.
CMD_HEX_TO_DECIMAL:
    LDA #'#'                    ; all results are prefixed with '#'
    JSR PRINT_CHAR
    LDA MON_CURRADDR_LO         ; 4-byte little-endian value in the workspace
    STA DEC32_VAL
    LDA MON_CURRADDR_HI
    STA DEC32_VAL+1
    STZ DEC32_VAL+2
    STZ DEC32_VAL+3
    LDA #<DEC32_VAL             ; pointer = the workspace itself
    LDX #>DEC32_VAL
    LDY #$00                    ; no field padding
    JSR PRINT_DEC
    JMP PRINT_NEWLINE           ; tail: newline after result

CMD_SHOW_HELP:
    ; Reset line counter for paging
    STZ CMD_LINE_COUNT          ; Reset command line counter
    STZ PAGE_ABORT_FLAG         ; Reset abort flag

    ; Print comprehensive help for all monitor commands
    JSR PRINT_HELP_HEADER       ; Print "6502 MONITOR COMMANDS"
    JSR PRINT_NEWLINE_PAGED

    ; Print each command with description
    JSR PRINT_HELP_BODY
    JMP PRINT_NEWLINE_PAGED

; Print help header text
PRINT_HELP_HEADER:
    LDA #<MSG_HELP_HEADER
    LDY #>MSG_HELP_HEADER
    JMP PRINT_MSG_AY            ; tail call: PRINT_MESSAGE's RTS returns to caller

; Print the help body text
; Modifies X
PRINT_HELP_BODY:
    LDX #0

HELP_LOOP:
    LDA HELP_MSG_TABLE,X
    STA MON_MSG_PTR_LO
    INX
    LDA HELP_MSG_TABLE,X
    STA MON_MSG_PTR_HI
    JSR PRINT_HELP_LINE         ; syntax, then pad to the description column, then text
    JSR PRINT_NEWLINE_PAGED
    INX
    CPX #(HELP_MSG_COUNT * 2)   ; loop over the whole table (2 bytes per entry)
    BNE HELP_LOOP
    RTS

; Exit mode command - Return from any interactive mode to command mode
; Input: None (can be called from any mode state)
; Output: MON_MODE set to command mode (0)
; Modifies: A
; Note: Used by ESC key handling to exit write mode or other interactive states
CMD_EXIT_MODE:
    LDA #MON_MODE_CMD           ; Set to command mode
    STA MON_MODE                ; Update mode
    RTS

; Write mode command - Enter interactive memory writing mode at specified address
; Input: Target address in MON_CURRADDR_HI/LO (parsed from W:xxxx command)
; Output: MON_MODE set to write mode, displays current memory value, enters write loop
; Modifies: A, X, Y
; Note: Enters persistent interactive mode - user can write hex bytes until ESC pressed
CMD_WRITE_MODE:
    LDA #MON_MODE_WRITE         ; Set to write mode
    STA MON_MODE                ; Update mode

    ; Display current byte at target address
    JSR SHOW_WRITE_ADDRESS      ; Show "XXXX: YY" format

    ; Enter write mode loop for sequential input
    JMP WRITE_MODE_LOOP  ; Handle sequential byte input

; Read memory command - Display memory contents at single address or address range
; Input: Start address in MON_CURRADDR_HI/LO, optional end address in MON_ENDADDR_HI/LO
; Output: Memory contents displayed to screen in hex format with addresses
; Modifies: A, X, Y
; Note: Single address shows "XXXX: YY" format, range shows 8-byte lines with paging
CMD_READ_MEMORY:

    STZ CMD_LINE_COUNT          ; Reset command line counter
    STZ PAGE_ABORT_FLAG         ; Reset abort flag

    ; Check if we have end address (range operation)
    LDA MON_ENDADDR_HI          ; Check if end address is set
    ORA MON_ENDADDR_LO          ; (non-zero means range)
    BEQ CMD_READ_SINGLE         ; If zero, single address

    ; Range operation - validate range first
    JSR VALIDATE_ADDRESS_RANGE  ; Use common range validation
    BCS CMD_READ_RANGE_ERROR    ; If invalid range, show error

CMD_READ_RANGE_VALID:
    ; Display memory range with 8-byte hex line formatting
    JMP DUMP_MEMORY_RANGE       ; tail call (RTS returns to the parser)

CMD_READ_RANGE_ERROR:
    JMP PRINT_RANGE_ERROR  ; Print RANGE? message

CMD_READ_SINGLE:
    ; Single address - show single byte in format "xxxx: bb"
    JMP SHOW_WRITE_ADDRESS      ; tail call: same address+byte display as write mode

; Run program command - Transfer control to user program at specified address
; Input: Execution address in MON_CURRADDR_HI/LO (parsed from G:xxxx command)
; Output: Transfers control to user program (may not return)
; Modifies: A, and all registers depending on user program
; Note: Uses JSR so user program can RTS to return to monitor
CMD_RUN_PROGRAM:
    ; Jump straight to the user program. CMD_RUN_PROGRAM is entered via the
    ; parser's JMP dispatch (it has no return address of its own on the stack),
    ; so the user program's RTS returns directly to the monitor — one fewer
    ; stack level than the old JSR-wrapper indirection.
    JMP (MON_CURRADDR_LO)       ; Jump to user program

; Dump memory range in formatted hex display with paging support
; Input: Start address in MON_STARTADDR_HI/LO, end address in MON_ENDADDR_HI/LO
; Output: Formatted memory dump to screen (8 bytes per line with addresses)
; Modifies: A, X, Y, MON_LINE_COUNT, MON_MSG_TMP_POS
; Note: Supports paging - user can ESC to abort, shows address: data format
DUMP_MEMORY_RANGE:
    ; Copy start address to current address
    LDA MON_STARTADDR_LO
    STA MON_CURRADDR_LO
    LDA MON_STARTADDR_HI
    STA MON_CURRADDR_HI

DUMP_RANGE_LOOP:
    ; Check if user aborted
    LDA PAGE_ABORT_FLAG
    BEQ CONTINUE_DUMP
    JMP DUMP_ABORTED
CONTINUE_DUMP:

    ; Print the address (four hex digits: high byte then low byte)
    LDA MON_CURRADDR_HI
    JSR PRINT_HEX_BYTE
    LDA MON_CURRADDR_LO
    JSR PRINT_HEX_BYTE

    ; Print colon and space
    LDA #ASCII_COLON
    JSR PRINT_CHAR
    LDA #ASCII_SPACE
    JSR PRINT_CHAR

    STZ MON_BYTE_COUNT          ; Initialize byte counter for this line

DUMP_PRINT_BYTES:
    ; Check if we've gone past end address
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BCC DUMP_PRINT_BYTE         ; Current < end, continue
    BNE DUMP_RANGE_DONE         ; Current > end, done
    LDA MON_CURRADDR_LO
    CMP MON_ENDADDR_LO
    BCC DUMP_PRINT_BYTE         ; Current < end, continue
    BEQ DUMP_PRINT_BYTE         ; Current = end, print last byte

    ; If we get here, current > end
    JMP DUMP_RANGE_DONE         ; We're past the end, done

DUMP_PRINT_BYTE:
    ; Load byte from memory and print as two hex digits (65C02 zp indirect), or from
    ; the page snapshot when T:/Z: armed it. For a page dump the address low byte IS
    ; the offset into the snapshot.
    LDA MON_DUMP_SNAP
    BEQ DUMP_BYTE_LIVE
    LDY MON_CURRADDR_LO
    LDA MON_SNAP_BUF,Y
    BRA DUMP_BYTE_SHOW
DUMP_BYTE_LIVE:
    LDA (MON_CURRADDR_LO)
DUMP_BYTE_SHOW:
    JSR PRINT_HEX_BYTE

    ; Check if this is the last byte
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BNE NOT_LAST_BYTE
    LDA MON_CURRADDR_LO
    CMP MON_ENDADDR_LO
    BEQ DUMP_RANGE_DONE         ; This was the last byte, we're done

NOT_LAST_BYTE:
    ; Print space separator
    LDA #ASCII_SPACE
    JSR PRINT_CHAR

    ; Increment current address
    INC MON_CURRADDR_LO
    BNE DUMP_NO_CARRY
    INC MON_CURRADDR_HI

DUMP_NO_CARRY:
    ; Increment and check byte counter
    INC MON_BYTE_COUNT
    LDA MON_BYTE_COUNT
    CMP #MON_BYTES_PER_LINE
    BNE DUMP_PRINT_BYTES

    ; End of line
    JSR PRINT_NEWLINE_PAGED
    JMP DUMP_RANGE_LOOP

DUMP_ABORTED:
    STZ MON_DUMP_SNAP           ; R:/W: always read live memory
    ; Just fall through to done

DUMP_RANGE_DONE:
    STZ MON_DUMP_SNAP
    JMP PRINT_NEWLINE_PAGED

; ================================================================
; MONITOR WRITE MODE IMPLEMENTATION
; ================================================================

; Display current address and value for write mode
; Input: Address in MON_CURRADDR_HI/LO
; Modifies: A, X, Y
SHOW_WRITE_ADDRESS:
    ; Print the address directly without using command buffer
    JSR PRINT_CURRENT_ADDRESS   ; This already prints the address correctly!

    ; Print colon and space
    LDA #ASCII_COLON            ; Print colon
    JSR PRINT_CHAR
    LDA #ASCII_SPACE            ; Print space
    JSR PRINT_CHAR

; Print the current byte value at address
    LDA (MON_CURRADDR_LO)       ; Load byte (65C02 zero-page indirect)
    JSR PRINT_HEX_BYTE          ; Print byte as two hex digits

    JMP PRINT_NEWLINE_PAGED  ; End with newline

; Write mode main loop - handles sequential byte input
; Implements the requirements for old/new value display and sequential writing
; Modifies: A, X, Y
WRITE_MODE_LOOP:
    ; Initialize byte count for this write operation
    STZ MON_BYTE_COUNT          ; Clear byte count

    ; Save starting address for old/new value display
    LDA MON_CURRADDR_LO         ; Save current address as start
    STA MON_STARTADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_STARTADDR_HI

WRITE_MODE_INPUT:
    JSR PRINT_MONITOR_PROMPT
    JSR READ_COMMAND_LINE

    ; Check if it's just ESC
    LDA MON_CMDLEN
    CMP #$01
    BNE WRITE_MODE_CHECK_EMPTY
    LDA MON_CMDBUF
    CMP #ASCII_ESC
    BEQ WRITE_MODE_DONE

WRITE_MODE_CHECK_EMPTY:
    ; Check if empty (Enter on empty line exits)
    LDA MON_CMDLEN
    BEQ WRITE_MODE_DONE

WRITE_MODE_PARSE_HEX:
    ; Parse space-separated hex bytes from input
    LDX #$00                    ; Start at beginning of input

WRITE_MODE_PARSE_LOOP:
    ; Skip spaces
    CPX MON_CMDLEN              ; At end of input?
    BCS WRITE_MODE_SHOW_RESULT  ; If so, show old/new values
    LDA MON_CMDBUF,X            ; Load character
    CMP #ASCII_SPACE            ; Is it a space?
    BNE WRITE_MODE_PARSE_BYTE   ; If not, try to parse byte
    INX                         ; Skip space
    BRA WRITE_MODE_PARSE_LOOP   ; Continue

WRITE_MODE_PARSE_BYTE:
    ; Parse two-character hex byte
    JSR HEX_PAIR_TO_BYTE        ; Parse hex pair (X points to first char)
    BCS WRITE_MODE_ERROR        ; If error, show error message

    ; Store the byte at current address
    STA (MON_CURRADDR_LO)       ; Store byte (65C02 zero-page indirect)

    ; Increment byte count and current address
    INC MON_BYTE_COUNT          ; Increment byte count
    INC MON_CURRADDR_LO         ; Increment current address low byte
    BNE WRITE_MODE_NO_CARRY     ; If no carry, continue
    INC MON_CURRADDR_HI         ; Increment high byte if carry

WRITE_MODE_NO_CARRY:
    ; Continue parsing more bytes in the same input line
    BRA WRITE_MODE_PARSE_LOOP

WRITE_MODE_SHOW_RESULT:
    ; Display old and new values if any bytes were written
    LDA MON_BYTE_COUNT          ; Check if any bytes were written
    BEQ WRITE_MODE_INPUT        ; If none, continue input

    ; Save the current address (where we'll continue writing)
    LDA MON_CURRADDR_LO
    PHA
    LDA MON_CURRADDR_HI
    PHA

    ; Set up end address for display (current address - 1)
    LDA MON_CURRADDR_LO         ; Get current address
    SEC                         ; Set carry for subtraction
    SBC #$01                    ; Subtract 1
    STA MON_ENDADDR_LO          ; Store as end address
    LDA MON_CURRADDR_HI         ; Get high byte
    SBC #$00                    ; Subtract carry
    STA MON_ENDADDR_HI          ; Store as end address high

    ; Display the range that was modified
    JSR DUMP_MEMORY_RANGE       ; Show new values

    ; Restore the current address for continued writing
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO

    ; Continue for more input
    JMP WRITE_MODE_INPUT

WRITE_MODE_ERROR:
    ; Display value error message for invalid hex input
    JSR PRINT_VALUE_ERROR       ; Print value error
    JMP WRITE_MODE_INPUT        ; Continue input

WRITE_MODE_DONE:
    ; Exit write mode and return to command mode
    LDA #MON_MODE_CMD           ; Set to command mode
    STA MON_MODE                ; Update mode
    RTS


; Fill memory command - Fill specified memory range with a single byte value
; Input: Start address in MON_STARTADDR_HI/LO, end address in MON_ENDADDR_HI/LO, fill value in MON_FILL_VALUE
; Output: Memory range filled with specified byte; prints OK on success
; Modifies: A, X, Y, and memory in specified range
; Note: Validates address range, uses forward-fill algorithm, displays progress
CMD_FILL_MEMORY:
    ; Validate address range (start <= end)
    JSR VALIDATE_ADDRESS_RANGE  ; Use common range validation
    BCS FILL_RANGE_ERROR        ; If invalid range, show error

    JSR RANGE_HITS_STATE        ; refuse a fill over the monitor's own loop state
    BCS FILL_RANGE_ERROR        ; (it would hang or run away -- see RANGE_HITS_STATE)

    JSR FILL_RANGE_CORE         ; Fill [MON_CURRADDR..MON_ENDADDR] with MON_FILL_VALUE

    ; Print success message
    LDA #<MSG_SUCCESS
    LDY #>MSG_SUCCESS
    JMP PRINT_MSG_AY            ; tail call: PRINT_MESSAGE's RTS returns to caller

FILL_RANGE_ERROR:
    JMP PRINT_RANGE_ERROR  ; Print range error message

; ----------------------------------------------------------------
; FILL_RANGE_CORE - Fill an inclusive address range with a byte (no output)
; Input: MON_CURRADDR_LO/HI = start, MON_ENDADDR_LO/HI = end, MON_FILL_VALUE = byte
; Output: Range filled; MON_CURRADDR advanced to the end. Assumes start <= end
;         (caller validates). Shared by the F: command and the RESET window clear.
; Modifies: A, MON_CURRADDR_LO/HI
; ----------------------------------------------------------------
FILL_RANGE_CORE:
    LDA MON_FILL_VALUE          ; Load fill value

FILL_LOOP:
    ; Fill byte at current address
    STA (MON_CURRADDR_LO)       ; Store fill value (65C02 zero-page indirect)

    ; Check if we've reached end address
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BCC FILL_CONTINUE           ; Current < end (high), continue
    BNE FILL_DONE              ; Current > end (high), done
    LDA MON_CURRADDR_LO
    CMP MON_ENDADDR_LO
    BCS FILL_DONE              ; Current >= end (low), done

FILL_CONTINUE:
    ; Increment current address
    INC MON_CURRADDR_LO
    BNE FILL_NO_CARRY          ; No carry, continue
    INC MON_CURRADDR_HI        ; Handle carry

FILL_NO_CARRY:
    LDA MON_FILL_VALUE         ; Reload fill value
    BRA FILL_LOOP              ; Continue filling

FILL_DONE:
    RTS

; Move/Copy memory command - Copy or move memory block between addresses
; Input: Start address in MON_STARTADDR_HI/LO, end address in MON_ENDADDR_HI/LO, destination in MON_DEST_ADDR_HI/LO, mode in MON_COPY_MODE (0=copy, 1=move)
; Output: Memory block copied/moved to destination, source cleared if move; prints OK on success
; Modifies: A, X, Y, and memory at destination and optionally source
; Note: Handles overlapping regions correctly, validates ranges, clears source if move mode
CMD_MOVE_MEMORY:
    ; Validate address range (start <= end)
    JSR VALIDATE_ADDRESS_RANGE  ; Use common range validation
    BCS MOVE_RANGE_ERROR        ; If invalid range, show error
    JSR RANGE_HITS_STATE        ; refuse a source range over the monitor's own state
    BCC MOVE_RANGE_VALID        ; Continue with valid range

MOVE_RANGE_ERROR:
    JMP PRINT_RANGE_ERROR  ; Print range error message

MOVE_RANGE_VALID:
    ; Save all original address variables on stack to preserve them for next command
    LDA MON_CURRADDR_LO
    PHA
    LDA MON_CURRADDR_HI
    PHA
    LDA MON_ENDADDR_LO
    PHA
    LDA MON_ENDADDR_HI
    PHA
    LDA MON_DEST_ADDR_LO
    PHA
    LDA MON_DEST_ADDR_HI
    PHA

    ; Store start and end addresses for later use within this function
    LDA MON_CURRADDR_LO
    STA MON_STARTADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_STARTADDR_HI

    ; Capture the original destination range now, before the copy loops mutate
    ; MON_DEST_ADDR. The move-mode source clear uses it to avoid zeroing bytes
    ; that overlap the destination. dest_end = dest + (end - start).
    LDA MON_DEST_ADDR_LO
    STA MOVE_DEST_LO
    LDA MON_DEST_ADDR_HI
    STA MOVE_DEST_HI
    SEC                         ; delta = end - start (into MOVE_DEND)
    LDA MON_ENDADDR_LO
    SBC MON_STARTADDR_LO
    STA MOVE_DEND_LO
    LDA MON_ENDADDR_HI
    SBC MON_STARTADDR_HI
    STA MOVE_DEND_HI
    CLC                         ; dest_end = dest + delta
    LDA MOVE_DEND_LO
    ADC MOVE_DEST_LO
    STA MOVE_DEND_LO
    LDA MOVE_DEND_HI
    ADC MOVE_DEST_HI
    STA MOVE_DEND_HI
    BCC MOVE_DEST_NOWRAP        ; dest+length carried past $FFFF: the copy would
    JMP MOVE_DEST_ERROR         ;   wrap into zero page and overwrite its own
MOVE_DEST_NOWRAP:               ;   pointers (the loops end on SOURCE == end only)

    JSR DEST_HITS_STATE         ; refuse a destination over the monitor's own state
    BCC MOVE_DEST_OK
    JMP MOVE_DEST_ERROR
MOVE_DEST_OK:

    ; Check for overlapping memory regions
    ; If destination is between source start and end, we need backward copy
    LDA MON_DEST_ADDR_HI        ; Compare dest with source start
    CMP MON_CURRADDR_HI
    BCC MOVE_FORWARD            ; dest < start, safe for forward copy
    BNE MOVE_CHECK_OVERLAP      ; dest > start, check if overlaps
    LDA MON_DEST_ADDR_LO
    CMP MON_CURRADDR_LO
    BCC MOVE_FORWARD            ; dest < start, safe for forward copy

MOVE_CHECK_OVERLAP:
    ; Check if destination is within source range
    LDA MON_DEST_ADDR_HI        ; Compare dest with source end
    CMP MON_ENDADDR_HI
    BCC MOVE_BACKWARD           ; dest < end, overlaps, need backward copy
    BNE MOVE_FORWARD            ; dest > end, no overlap
    LDA MON_DEST_ADDR_LO
    CMP MON_ENDADDR_LO
    BCC MOVE_BACKWARD           ; dest < end, overlaps, need backward copy
    BEQ MOVE_BACKWARD           ; dest == end, still overlaps (forward would clobber)

MOVE_FORWARD:
    ; Forward copy: copy from start to end
    LDY #$00                    ; Initialize Y index

MOVE_FORWARD_LOOP:
    ; Copy byte from source to destination
    LDA (MON_CURRADDR_LO),Y     ; Load byte from source
    ; We need to use zero page addressing since MON_DEST_ADDR is not in zero page
    ; Use JUMP_VECTOR ($06/$07) as temporary zero page pointer
    PHA                         ; Save the byte to copy
    LDA MON_DEST_ADDR_LO        ; Load dest address low
    STA JUMP_VECTOR             ; Store in zero page temp location
    LDA MON_DEST_ADDR_HI        ; Load dest address high
    STA JUMP_VECTOR+1           ; Store in zero page temp location+1
    PLA                         ; Restore the byte to copy
    STA (JUMP_VECTOR),Y         ; Store using zero page indirect

    ; Check if this was the last byte to copy (current == end)
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BNE MOVE_FORWARD_CONTINUE   ; Not equal, continue or check
    LDA MON_CURRADDR_LO
    CMP MON_ENDADDR_LO
    BEQ MOVE_FORWARD_DONE       ; Equal, we just copied the last byte, done

MOVE_FORWARD_CONTINUE:
    ; Not the last byte yet, increment addresses and continue
    INC MON_CURRADDR_LO
    BNE MOVE_FORWARD_NO_SRC_CARRY
    INC MON_CURRADDR_HI

MOVE_FORWARD_NO_SRC_CARRY:
    INC MON_DEST_ADDR_LO
    BNE MOVE_FORWARD_NO_DEST_CARRY
    INC MON_DEST_ADDR_HI

MOVE_FORWARD_NO_DEST_CARRY:
    JMP MOVE_FORWARD_LOOP       ; Continue copying

MOVE_FORWARD_DONE:
    JMP MOVE_CLEAR_CHECK        ; Check if we need to clear source

MOVE_BACKWARD:
    ; Backward copy: start from end and work backwards
    ; Set current address to end address
    LDA MON_ENDADDR_LO
    STA MON_CURRADDR_LO
    LDA MON_ENDADDR_HI
    STA MON_CURRADDR_HI

    ; Calculate destination end address
    ; dest_end = dest + (source_end - source_start)
    ; First calculate the offset (end - start)
    LDA MON_ENDADDR_LO
    SEC
    SBC MON_STARTADDR_LO        ; offset_lo = end_lo - start_lo
    TAX                         ; Save offset low
    LDA MON_ENDADDR_HI
    SBC MON_STARTADDR_HI        ; offset_hi = end_hi - start_hi with borrow
    PHA                         ; preserve offset_hi (A is about to be reused)

    ; Add offset to destination to get destination end
    STX MON_HEX_TEMP            ; Use temp storage for offset_lo
    CLC
    LDA MON_DEST_ADDR_LO
    ADC MON_HEX_TEMP            ; dest_end_lo = dest_lo + offset_lo
    STA MON_DEST_ADDR_LO
    PLA                         ; recover offset_hi (PLA preserves carry)
    ADC MON_DEST_ADDR_HI        ; dest_end_hi = offset_hi + dest_hi + carry
    STA MON_DEST_ADDR_HI

    LDY #$00                    ; Initialize Y index

MOVE_BACKWARD_LOOP:
    ; Copy byte from source to destination (both at end positions)
    LDA (MON_CURRADDR_LO),Y     ; Load byte from source
    ; We need to use zero page addressing since MON_DEST_ADDR is not in zero page
    ; Use JUMP_VECTOR ($06/$07) as temporary zero page pointer
    PHA                         ; Save the byte to copy
    LDA MON_DEST_ADDR_LO        ; Load dest address low
    STA JUMP_VECTOR             ; Store in zero page temp location
    LDA MON_DEST_ADDR_HI        ; Load dest address high
    STA JUMP_VECTOR+1           ; Store in zero page temp location+1
    PLA                         ; Restore the byte to copy
    STA (JUMP_VECTOR),Y         ; Store using zero page indirect

    ; Check if we've reached start address (going backwards)
    LDA MON_CURRADDR_HI
    CMP MON_STARTADDR_HI
    BCC MOVE_BACKWARD_DONE      ; Current < start (high), done
    BNE MOVE_BACKWARD_CONTINUE  ; Current > start (high), continue
    LDA MON_CURRADDR_LO
    CMP MON_STARTADDR_LO
    BCC MOVE_BACKWARD_DONE      ; Current < start (low), done
    BEQ MOVE_BACKWARD_DONE      ; Current = start (low), done after this copy

MOVE_BACKWARD_CONTINUE:
    ; Decrement both source and destination addresses
    LDA MON_CURRADDR_LO
    BNE MOVE_BACKWARD_NO_SRC_BORROW
    DEC MON_CURRADDR_HI

MOVE_BACKWARD_NO_SRC_BORROW:
    DEC MON_CURRADDR_LO

    LDA MON_DEST_ADDR_LO
    BNE MOVE_BACKWARD_NO_DEST_BORROW
    DEC MON_DEST_ADDR_HI

MOVE_BACKWARD_NO_DEST_BORROW:
    DEC MON_DEST_ADDR_LO
    JMP MOVE_BACKWARD_LOOP      ; Continue copying

MOVE_BACKWARD_DONE:
    ; Fall through to clear check

MOVE_CLEAR_CHECK:
    ; Check if this is a move operation (need to clear source)
    LDA MON_COPY_MODE
    BEQ MOVE_SUCCESS            ; Copy mode (0), skip clearing

    ; Move mode (1): clear only the VACATED source bytes. Bytes that now fall
    ; within the destination [MOVE_DEST, MOVE_DEND] hold the moved data and must
    ; not be zeroed (this is what makes an overlapping move preserve all bytes).
    LDA MON_STARTADDR_LO
    STA MON_CURRADDR_LO
    LDA MON_STARTADDR_HI
    STA MON_CURRADDR_HI

MOVE_CLEAR_LOOP:
    ; Clear the current byte unless it lies within [MOVE_DEST, MOVE_DEND].
    LDA MON_CURRADDR_HI         ; cur < dest -> vacated, clear
    CMP MOVE_DEST_HI
    BCC MOVE_CLEAR_DO
    BNE MOVE_CLEAR_CHK_END      ; cur_hi > dest_hi -> cur >= dest
    LDA MON_CURRADDR_LO
    CMP MOVE_DEST_LO
    BCC MOVE_CLEAR_DO           ; cur < dest -> clear

MOVE_CLEAR_CHK_END:
    ; cur >= dest: clear only if cur > dest_end
    LDA MOVE_DEND_HI
    CMP MON_CURRADDR_HI
    BCC MOVE_CLEAR_DO           ; dest_end < cur -> clear
    BNE MOVE_CLEAR_NEXT         ; dest_end_hi > cur_hi -> within -> keep
    LDA MOVE_DEND_LO
    CMP MON_CURRADDR_LO
    BCC MOVE_CLEAR_DO           ; dest_end < cur -> clear
    BCS MOVE_CLEAR_NEXT         ; dest_end >= cur -> within -> keep

MOVE_CLEAR_DO:
    LDA #$00
    STA (MON_CURRADDR_LO)       ; clear source byte (65C02 zero-page indirect)

MOVE_CLEAR_NEXT:
    ; Reached the end of the source range?
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BCC MOVE_CLEAR_CONTINUE     ; Current < end (high), continue
    BNE MOVE_CLEAR_DONE         ; Current > end (high), done
    LDA MON_CURRADDR_LO
    CMP MON_ENDADDR_LO
    BCS MOVE_CLEAR_DONE         ; Current >= end (low), done

MOVE_CLEAR_CONTINUE:
    INC MON_CURRADDR_LO
    BNE MOVE_CLEAR_LOOP
    INC MON_CURRADDR_HI
    BRA MOVE_CLEAR_LOOP

MOVE_CLEAR_DONE:
    JMP MOVE_SUCCESS            ; Show success message

; Destination rejected after CMD_MOVE_MEMORY pushed its saved state: restore it (so
; the prompt address stays consistent, as the rest of the monitor expects) and
; report RANGE?. The restore is inline in both exits on purpose -- it cannot be a
; subroutine, because the JSR return address would sit on top of the six saved bytes
; and the PLAs would pull that instead.
MOVE_DEST_ERROR:
    PLA
    STA MON_DEST_ADDR_HI
    PLA
    STA MON_DEST_ADDR_LO
    PLA
    STA MON_ENDADDR_HI
    PLA
    STA MON_ENDADDR_LO
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    JMP PRINT_RANGE_ERROR

MOVE_SUCCESS:
    ; Restore all original address variables from stack
    PLA
    STA MON_DEST_ADDR_HI
    PLA
    STA MON_DEST_ADDR_LO
    PLA
    STA MON_ENDADDR_HI
    PLA
    STA MON_ENDADDR_LO
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO

    ; Print the success message. (Copy and move share the same message; the
    ; distinct "COPIED/MOVED N BYTES" output was never implemented.)
    LDA #<MSG_SUCCESS
    LDY #>MSG_SUCCESS
    JMP PRINT_MSG_AY            ; tail call: PRINT_MESSAGE's RTS returns to caller

; Search memory command - Search for multi-byte hex pattern within memory range
; Input: Start address in MON_STARTADDR_HI/LO, end address in MON_ENDADDR_HI/LO, search pattern in MON_SEARCH_PATTERN, pattern length in MON_PATTERN_LEN
; Output: Address of each match displayed to screen with paging support
; Modifies: A, X, Y
; Note: Supports 1-16 byte patterns, uses paging - user can press ESC to abort
CMD_SEARCH_MEMORY:
    ; Save original current address to preserve it (search should not modify current address)
    LDA MON_CURRADDR_LO
    PHA
    LDA MON_CURRADDR_HI
    PHA

    ; Initialize line counter and abort flag for paging
    STZ CMD_LINE_COUNT          ; Reset command line counter
    STZ PAGE_ABORT_FLAG         ; Reset abort flag

    ; Validate address range (start <= end)
    JSR VALIDATE_ADDRESS_RANGE  ; Use common range validation
    BCS SEARCH_RANGE_ERROR      ; If invalid range, show error
    BRA SEARCH_RANGE_VALID      ; Continue with valid range

SEARCH_RANGE_ERROR:
    ; Restore original current address before error exit
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    JMP PRINT_RANGE_ERROR  ; Print range error message

SEARCH_RANGE_VALID:
    ; Perform search operation
    ; Copy start address to current address for searching
    LDA MON_CURRADDR_LO
    STA MON_STARTADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_STARTADDR_HI

SEARCH_LOOP:
    ; Check if user aborted
    LDA PAGE_ABORT_FLAG
    BEQ SEARCH_CONTINUE
    JMP SEARCH_DONE            ; User aborted, exit through normal cleanup

SEARCH_CONTINUE:
    ; Check if we've gone past end address
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BCC SEARCH_CHECK_PATTERN    ; Current < end, continue searching
    BNE SEARCH_DONE             ; Current > end, done
    LDA MON_CURRADDR_LO
    CMP MON_ENDADDR_LO
    BCC SEARCH_CHECK_PATTERN    ; Current < end, continue searching
    BEQ SEARCH_CHECK_PATTERN    ; Current = end, check last position

    ; If we get here, current > end
    JMP SEARCH_DONE             ; We're past the end, done

SEARCH_CHECK_PATTERN:
    ; The whole pattern must fit within the range: (current + len - 1) <= end.
    ; current only increases, so once it can't fit the search is finished.
    ; MON_STARTADDR is free here (unused during the search) so reuse it as scratch.
    LDA MON_PATTERN_LEN
    SEC
    SBC #$01                    ; len - 1 (parser guarantees len >= 1)
    CLC
    ADC MON_CURRADDR_LO         ; last_lo = current_lo + (len - 1)
    STA MON_STARTADDR_LO
    LDA MON_CURRADDR_HI
    ADC #$00                    ; propagate carry
    BCS SEARCH_DONE             ; address wrapped past $FFFF -> can't fit, done
    STA MON_STARTADDR_HI
    CMP MON_ENDADDR_HI
    BCC SEARCH_DO_MATCH         ; last_hi < end_hi -> fits
    BNE SEARCH_DONE             ; last_hi > end_hi -> past end, done
    LDA MON_STARTADDR_LO
    CMP MON_ENDADDR_LO
    BEQ SEARCH_DO_MATCH         ; last == end -> fits (pattern ends exactly at end)
    BCS SEARCH_DONE             ; last > end -> past end, done

SEARCH_DO_MATCH:
    ; Check if pattern matches at current address
    LDY #$00                    ; Pattern index

SEARCH_PATTERN_LOOP:
    ; Check if we have more pattern bytes to match
    CPY MON_PATTERN_LEN
    BCS SEARCH_FOUND_MATCH      ; If Y >= pattern length, we found a match

    ; Get memory byte at current address + Y offset
    LDA (MON_CURRADDR_LO),Y     ; Load byte from current address + Y

    ; Compare with pattern byte at pattern index Y
    CMP MON_SEARCH_PATTERN,Y    ; Compare with pattern byte at index Y
    BNE SEARCH_NO_MATCH         ; If not equal, no match at this position

    ; Bytes match, continue with next byte
    INY                         ; Move to next pattern byte
    JMP SEARCH_PATTERN_LOOP     ; Continue pattern matching

SEARCH_NO_MATCH:
    ; Pattern didn't match at this location, try next address
    INC MON_CURRADDR_LO         ; Increment current address
    BNE SEARCH_NO_CARRY
    INC MON_CURRADDR_HI         ; Handle carry

SEARCH_NO_CARRY:
    JMP SEARCH_LOOP             ; Continue searching

SEARCH_FOUND_MATCH:
    ; Found a match! Print the address where pattern was found
    JSR PRINT_CURRENT_ADDRESS   ; Print the address where match was found
    JSR PRINT_NEWLINE_PAGED     ; Add newline

    ; Move to next address to continue searching
    INC MON_CURRADDR_LO         ; Increment current address
    BNE SEARCH_FOUND_NO_CARRY
    INC MON_CURRADDR_HI         ; Handle carry

SEARCH_FOUND_NO_CARRY:
    JMP SEARCH_LOOP             ; Continue searching for more matches

SEARCH_DONE:
    ; Restore original current address before returning
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    ; Search completed - ensure clean line for next prompt
    JMP PRINT_NEWLINE_PAGED

; ================================================================
; MONITOR MAIN COMMAND LOOP
; ================================================================

; Main monitor command loop - reads and processes commands
; This is the heart of the monitor program
; MONITOR_COLD - cold entry from the DOS 'MON' command (K_MON_ENTRY). Prints the
; MFC sign-on banner once, then falls into MONITOR_MAIN. A BREAK/NMI return
; (NMI_HANDLER_BREAK) jumps straight to MONITOR_MAIN, so the banner appears once
; per session rather than on every break.
MONITOR_COLD:
    JSR CLEAR_SCREEN           ; start on a clean screen so the banner sits at the top
    LDA #<MSG_MON_BANNER
    LDY #>MSG_MON_BANNER
    JSR PRINT_MSG_AY            ; "MFC MONITOR   ?=HELP  Q=QUIT" + newline
    ; fall through to MONITOR_MAIN

MONITOR_MAIN:
    ; A freshly launched monitor (from DOS via MON) starts clean: command mode,
    ; current address 0. The DOS shell uses MON_CURRADDR as scratch, so reset it
    ; here rather than relying on boot-time zeroing.
    STZ MON_CURRADDR_LO
    STZ MON_CURRADDR_HI
    STZ MON_MODE
    JSR PRINT_NEWLINE           ; Start with a newline

MONITOR_LOOP:

    JSR PRINT_MONITOR_PROMPT    ; Print appropriate prompt
    JSR READ_COMMAND_LINE       ; Read command from user

    ; Check if command is empty
    LDA MON_CMDLEN              ; Load command length
    BEQ MONITOR_LOOP            ; If empty, just show prompt again

    ; Parse and execute the command
    JSR PARSE_COMMAND           ; Parse the command in MON_CMDBUF

    ; Save the command only if we're in command mode and it completed without error
    LDA MON_MODE                ; Check current mode
    BNE MONITOR_SKIP_SAVE       ; If not in command mode, don't save
    LDA MON_ERROR_FLAG          ; Check if command had error
    BNE MONITOR_SKIP_SAVE       ; If error, don't save
    JSR SAVE_COMMAND            ; Save command for dot recall

MONITOR_SKIP_SAVE:
    JMP MONITOR_LOOP            ; Continue command loop

; ================================================================
; COMMAND JUMP TABLES - For fast command dispatch
; ================================================================

; Compact jump table of command handler addresses, indexed by the slot number
; that CMD_INDEX_MAP assigns to each command letter. Slots 4 and 11 are not
; produced by CMD_INDEX_MAP (the '?' help and ESC commands are handled before
; this table is consulted), so those two entries are unused.
CMD_JUMP_COMPACT_LO:
    .BYTE <PARSE_CMD_DONE       ; 0 - unused ('B' bank menu retired)
    .BYTE <PARSE_CMD_CLEAR      ; 1 - 'C'
    .BYTE <PARSE_CMD_FILL_CHECK ; 2 - 'F'
    .BYTE <PARSE_CMD_GO_CHECK   ; 3 - 'G'
    .BYTE <PARSE_CMD_HELP       ; 4 - unused (help is the '?' command, handled earlier)
    .BYTE <PARSE_CMD_DONE       ; 5 - unused ('L' retired; host load is DOS IMPORT)
    .BYTE <PARSE_CMD_MOVE_CHECK ; 6 - 'M'
    .BYTE <PARSE_CMD_READ_CHECK ; 7 - 'R'
    .BYTE <PARSE_CMD_DONE       ; 8 - unused ('S' retired; host save is DOS EXPORT)
    .BYTE <PARSE_CMD_STACK      ; 9 - 'T'
    .BYTE <PARSE_CMD_WRITE_CHECK; 10 - 'W'
    .BYTE <PARSE_CMD_EXIT       ; 11 - unused (ESC handled earlier)
    .BYTE <PARSE_CMD_ZERO       ; 12 - 'Z'
    .BYTE <PARSE_CMD_SEARCH_CHECK; 13 - 'X' (search)
    .BYTE <PARSE_CMD_DECIMAL_CHECK; 14 - 'D' (decimal to hex)
    .BYTE <PARSE_CMD_HEX_TO_DEC ; 15 - 'H' (hex to decimal)

CMD_JUMP_COMPACT_HI:
    .BYTE >PARSE_CMD_DONE       ; 0 - unused ('B' bank menu retired)
    .BYTE >PARSE_CMD_CLEAR      ; 1 - 'C'
    .BYTE >PARSE_CMD_FILL_CHECK ; 2 - 'F'
    .BYTE >PARSE_CMD_GO_CHECK   ; 3 - 'G'
    .BYTE >PARSE_CMD_HELP       ; 4 - unused (help is the '?' command, handled earlier)
    .BYTE >PARSE_CMD_DONE       ; 5 - unused ('L' retired; host load is DOS IMPORT)
    .BYTE >PARSE_CMD_MOVE_CHECK ; 6 - 'M'
    .BYTE >PARSE_CMD_READ_CHECK ; 7 - 'R'
    .BYTE >PARSE_CMD_DONE       ; 8 - unused ('S' retired; host save is DOS EXPORT)
    .BYTE >PARSE_CMD_STACK      ; 9 - 'T'
    .BYTE >PARSE_CMD_WRITE_CHECK; 10 - 'W'
    .BYTE >PARSE_CMD_EXIT       ; 11 - unused (ESC handled earlier)
    .BYTE >PARSE_CMD_ZERO       ; 12 - 'Z'
    .BYTE >PARSE_CMD_SEARCH_CHECK; 13 - 'X' (search)
    .BYTE >PARSE_CMD_DECIMAL_CHECK; 14 - 'D' (decimal to hex)
    .BYTE >PARSE_CMD_HEX_TO_DEC ; 15 - 'H' (hex to decimal)

; Index mapping table - maps command character to table index
; For characters B-Z, subtract 'B' ($42) to get offset into this table
; Note: '?' character is handled as special case before table lookup (maps to help)
CMD_INDEX_MAP:
    .BYTE $FF   ; B -> invalid (bank menu retired; launch BASIC/ASM by name at DOS)
    .BYTE 1     ; C -> 1 (Clear)
    .BYTE 14    ; D -> 14 (Decimal to Hex)
    .BYTE $FF   ; E -> invalid
    .BYTE 2     ; F -> 2 (Fill)
    .BYTE 3     ; G -> 3 (Run)
    .BYTE 15    ; H -> 15 (Hex to Decimal) - Help is now via '?' character
    .BYTE $FF   ; I -> invalid
    .BYTE $FF   ; J -> invalid
    .BYTE $FF   ; K -> invalid
    .BYTE $FF   ; L -> invalid (host load retired; use DOS LOAD / IMPORT)
    .BYTE 6     ; M -> 6 (Move/Copy)
    .BYTE $FF   ; N -> invalid
    .BYTE $FF   ; O -> invalid
    .BYTE $FF   ; P -> invalid
    .BYTE $FF   ; Q -> invalid
    .BYTE 7     ; R -> 7 (Read Memory)
    .BYTE $FF   ; S -> invalid (host save retired; use DOS SAVE / EXPORT)
    .BYTE 9     ; T -> 9 (Print Stack)
    .BYTE $FF   ; U -> invalid
    .BYTE $FF   ; V -> invalid
    .BYTE 10    ; W -> 10 (Write to Memory)
    .BYTE 13    ; X -> 13 (Search)
    .BYTE $FF   ; Y -> invalid
    .BYTE 12    ; Z -> 12 (Print Zero Page)

; ================================================================
; MODE PREFIX TABLE - Characters for prompt prefixes
; ================================================================
MODE_PREFIX_TABLE:
    .BYTE 0         ; MON_MODE_CMD = 0: No prefix (just address>)
    .BYTE 'W'       ; MON_MODE_WRITE = 1: 'W' (W:address>)

; ================================================================
; HELP MESSAGE TABLE - Addresses of help messages for display
; ================================================================
; Commands listed alphabetically by command letter; ESC (a navigation key,
; not a colon command) is kept last.
HELP_MSG_TABLE:
    .WORD MSG_HELP_CLEAR        ; C
    .WORD MSG_HELP_DECIMAL      ; D
    .WORD MSG_HELP_FILL         ; F
    .WORD MSG_HELP_GO           ; G
    .WORD MSG_HELP_HEX_TO_DEC   ; H
    .WORD MSG_HELP_MOVE         ; M
    .WORD MSG_HELP_READ         ; R
    .WORD MSG_HELP_STACK        ; T
    .WORD MSG_HELP_WRITE        ; W
    .WORD MSG_HELP_SEARCH       ; X
    .WORD MSG_HELP_ZERO         ; Z
    .WORD MSG_HELP_EXIT         ; ESC
    .WORD MSG_HELP_HELP         ; ?
    .WORD MSG_HELP_RECALL       ; .
    .WORD MSG_HELP_QUIT         ; Q

HELP_MSG_COUNT = 15              ; Number of help messages

; ================================================================
; MESSAGE DATA SECTION - Null-terminated strings for monitor
; ================================================================
MSG_MON_BANNER:      .BYTE "MFC MONITOR   ?=HELP  Q=QUIT", $0D, 0
MSG_HELP_HEADER:     .BYTE "MONITOR COMMANDS", 0
; Each help line is "<syntax>", $09 (TAB -> pad to HELP_DESC_COL), "<description>".
MSG_HELP_CLEAR:      .BYTE "C:", $09, "CLEAR SCREEN", 0
MSG_HELP_DECIMAL:    .BYTE "D:NNNNN", $09, "DECIMAL TO HEX", 0
MSG_HELP_GO:         .BYTE "G:XXXX", $09, "RUN PROGRAM", 0
MSG_HELP_HEX_TO_DEC: .BYTE "H:XXXX", $09, "HEX TO DECIMAL", 0
MSG_HELP_READ:       .BYTE "R:XXXX(-YYYY)", $09, "READ MEMORY", 0
MSG_HELP_STACK:      .BYTE "T:", $09, "PRINT STACK", 0
MSG_HELP_WRITE:      .BYTE "W:XXXX", $09, "WRITE MEMORY", 0
MSG_HELP_ZERO:       .BYTE "Z:", $09, "PRINT ZERO PAGE", 0
MSG_HELP_FILL:       .BYTE "F:XXXX-YYYY,ZZ", $09, "FILL MEMORY", 0
MSG_HELP_MOVE:       .BYTE "M:XXXX-YYYY,ZZZZ,B", $09, "COPY/MOVE (B 0=COPY 1=MOVE)", 0
MSG_HELP_SEARCH:     .BYTE "X:XXXX-YYYY,PATTERN", $09, "SEARCH MEMORY", 0
MSG_HELP_EXIT:       .BYTE "ESC", $09, "EXIT CURRENT MODE", 0
MSG_HELP_HELP:       .BYTE "?", $09, "SHOW THIS HELP", 0
MSG_HELP_RECALL:     .BYTE ".", $09, "RECALL LAST COMMAND", 0
MSG_HELP_QUIT:       .BYTE "Q", $09, "QUIT TO DOS", 0
MSG_SYNTAX_ERROR:    .BYTE "ERROR?", $0D, $0A, 0
MSG_RANGE_ERROR:     .BYTE "RANGE?", $0D, $0A, 0
MSG_VALUE_ERROR:     .BYTE "VALUE?", $0D, $0A, 0
MSG_SUCCESS:         .BYTE "OK", $0D, $0A, 0
