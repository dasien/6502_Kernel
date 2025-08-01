; MFC 6502 Kernel Boot Initialization

; ROM start address.
.org $F000

; ================================================================
; SYSTEM CONSTANTS SECTION
; ================================================================
STACK_TOP       = $FF           ; Top of stack page
RAM_START       = $0200         ; Start of general purpose RAM
RAM_END         = $7FFF         ; End of available RAM

; ================================================================
; ZERO PAGE VALUES
; ================================================================
PROC_DDR        = $00           ; Data direction register for processor port
PROC_PORT       = $01           ; Processor port for memory banking
MON_CURRADDR_LO = $02           ; Current address low byte (MUST be zero page)
MON_CURRADDR_HI = $03           ; Current address high byte (MUST be zero page)
MON_MSG_PTR_LO  = $04           ; Message pointer low byte (MUST be zero page)
MON_MSG_PTR_HI  = $05           ; Message pointer high byte (MUST be zero page)
JUMP_VECTOR     = $06           ; Indirect jump vector (2 bytes: $06-$07)
SCREEN_PTR_LO   = $08           ; Current screen memory pointer (low byte)
SCREEN_PTR_HI   = $09           ; Current screen memory pointer (high byte)
HEX_LOOKUP_TABLE = $F0          ; Hex lookup table location

; ================================================================
; MONITOR VARIABLES
; ================================================================

; Monitor Command Buffer and State Variables
MON_CMDBUF      = $0200         ; Command input buffer (80 bytes: $0200-$024F)
MON_CMDBUF_LEN  = 80            ; Maximum command buffer length
MON_CMDPTR      = $0250         ; Pointer to current position in command buffer
MON_CMDLEN      = $0251         ; Current length of command in buffer
MON_MODE        = $0252         ; Current monitor mode (0=Command, 1=Write, 2=Read, 3=Run)
MON_STARTADDR_LO = $0253         ; Start address for range operations (low)
MON_STARTADDR_HI = $0254         ; Start address for range operations (high)
MON_ENDADDR_LO  = $0255         ; End address for range operations (low)
MON_ENDADDR_HI  = $0256         ; End address for range operations (high)

; Monitor Parser Variables
MON_PARSE_PTR   = $0257         ; Parser position pointer
MON_PARSE_LEN   = $0258         ; Remaining characters to parse
MON_HEX_TEMP    = $0259         ; Temporary storage for hex conversion
MON_BYTE_COUNT  = $025A         ; Count of bytes for write operations
MON_LINE_COUNT  = $025B         ; Line counter for display formatting
MON_ERROR_FLAG  = $025C         ; Error flag for invalid operations
CURSOR_X        = $025D         ; Current cursor X position (0-39)
CURSOR_Y        = $025E         ; Current cursor Y position (0-24)
MON_MSG_TMP_POS = $025F         ; Temp pointer to current position in message

; Monitor Mode Constants
MON_MODE_CMD    = 0             ; Command mode
MON_MODE_WRITE  = 1             ; Write mode
MON_MODE_READ   = 2             ; Read mode
MON_MODE_RUN    = 3             ; Run mode
MON_MODE_LOAD   = 4             ; Load binary file mode

; Monitor Display Constants
MON_BYTES_PER_LINE = 8          ; Number of bytes displayed per line
MON_HEX_DIGITS  = 2             ; Hex digits per byte

; Screen and I/O Constants for Monitor
SCREEN_START    = $0400         ; Screen memory start
SCREEN_WIDTH    = 40            ; Characters per line
SCREEN_HEIGHT   = 25            ; Lines on screen
CURSOR_CHAR     = $5F           ; Underscore cursor character

; ASCII Character Constants
ASCII_0         = $30           ; ASCII '0'
ASCII_9         = $39           ; ASCII '9'
ASCII_A         = $41           ; ASCII 'A'
ASCII_F         = $46           ; ASCII 'F'
ASCII_CR        = $0D           ; Carriage return
ASCII_LF        = $0A           ; Line feed
ASCII_SPACE     = $20           ; Space
ASCII_COLON     = $3A           ; Colon ':'
ASCII_DASH      = $2D           ; Dash '-'
ASCII_BACKSPACE = $08           ; Backspace character
ASCII_DELETE    = $7F           ; Delete character
ASCII_ESC       = $1B           ; Escape character

; Monitor Command Characters
CMD_WRITE       = $57           ; 'W' - Write mode
CMD_READ        = $52           ; 'R' - Read mode
CMD_GO          = $47           ; 'G' - Go/Run mode
CMD_LOAD        = $4C           ; 'L' - Load binary file mode
CMD_KLEAR       = $4B           ; 'K' - Clear screen (Klear)
CMD_STACK       = $53           ; 'S' - Stack dump
CMD_ZERO        = $5A           ; 'Z' - Zero page dump
CMD_TARGET      = $54           ; 'T' - Show target address
CMD_HELP        = $48           ; 'H' - Help
CMD_EXIT        = $58           ; 'X' - Exit mode

; Hardware I/O addresses for keyboard input
PIA_DATA        = $DC00         ; PIA data register for keyboard
PIA_CONTROL     = $DC02         ; PIA control register for keyboard
PIA_DATA_AVAIL  = $01           ; Bit mask for data available flag

; ================================================================
; KERNEL PROGRAM LOOP
; ================================================================

; Reset vector entry point - this is where the 6502 jumps on reset
RESET:
    ; === PROCESSOR INITIALIZATION ===

    ; Clear decimal mode - 6502 powers on in undefined state
    ; This is critical as some operations behave differently in decimal mode
    CLD                         ; Clear decimal mode flag

    ; Disable interrupts during initialization
    ; We don't want to be interrupted while setting up the system
    SEI                         ; Set interrupt disable flag

    ; Initialize stack pointer to top of stack page
    ; Stack grows downward from $01FF to $0100
    LDX #STACK_TOP              ; Load stack pointer with $FF
    TXS                         ; Transfer X to stack pointer

    ; Set up processor port for memory banking control
    ; This controls what appears in the $A000-$FFFF range
    LDA #$3F                    ; Set bits 0-5 as outputs
    STA PROC_DDR                ; Configure processor port direction

    ; Set default memory configuration
    ; Bit pattern xx111xxx = BASIC ROM, Kernal ROM, I/O visible
    LDA #$37                    ; Default banking configuration
    STA PROC_PORT               ; Set memory banking

; ================================================================
; HARDWARE INITIALIZATION
; ================================================================

; ================================================================
; ZERO PAGE INITIALIZATION
; ================================================================

    ; Clear critical zero page locations used by kernel
    LDX #$06                    ; Start after processor port registers and critical pointers
    LDA #$00                    ; Load zero

ZP_CLEAR_LOOP:
    STA $00,X               ; Clear zero page location
    INX                     ; Increment address
    CPX #$F0                ; Stop before hex table (and prevent wrap)
    BNE ZP_CLEAR_LOOP       ; Loop until done

; Initialize hex lookup table with "0123456789ABCDEF" at $F0-$FF
; Modifies: A, X
INIT_HEX_LOOKUP:
    LDX #$00                    ; Start with 0

INIT_HEX_DIGIT_LOOP:
    TXA                         ; Get current index (0-15)
    CMP #$0A                    ; Is it 0-9?
    BCS INIT_HEX_LETTER         ; No, it's A-F

    ; Handle 0-9
    CLC
    ADC #'0'                    ; Add ASCII '0' ($30)
    JMP STORE_HEX_CHAR

INIT_HEX_LETTER:
    ; Handle A-F (index 10-15)
    SEC
    SBC #$0A                    ; Subtract 10 (gives 0-5)
    CLC
    ADC #'A'                    ; Add ASCII 'A' ($41)

STORE_HEX_CHAR:
    STA HEX_LOOKUP_TABLE,X      ; Store at $F0+X
    INX
    CPX #$10                    ; Done all 16?
    BNE INIT_HEX_DIGIT_LOOP

; ================================================================
; RAM INITIALIZATION
; ================================================================

    ; Clear screen memory (typically at $0400-$07FF)
    JSR CLEAR_SCREEN

; ================================================================
; MONITOR INITIALIZATION
; ================================================================

    ; Initialize monitor variables and state
    LDA #$00                    ; Clear accumulator
    STA MON_CMDPTR              ; Reset command buffer pointer
    STA MON_CMDLEN              ; Reset command buffer length
    STA MON_MODE                ; Set to command mode (0)
    STA MON_CURRADDR_LO         ; Clear current address low byte
    STA MON_CURRADDR_HI         ; Clear current address high byte
    STA MON_STARTADDR_LO        ; Clear start address low byte
    STA MON_STARTADDR_HI        ; Clear start address high byte
    STA MON_ENDADDR_LO          ; Clear end address low byte
    STA MON_ENDADDR_HI          ; Clear end address high byte
    STA MON_PARSE_PTR           ; Reset parser pointer
    STA MON_PARSE_LEN           ; Reset parser length
    STA MON_HEX_TEMP            ; Clear hex temporary storage
    STA MON_BYTE_COUNT          ; Clear byte count
    STA MON_LINE_COUNT          ; Clear line count
    STA MON_ERROR_FLAG          ; Clear error flag
    STA MON_MSG_PTR_LO          ; Clear message pointer low byte
    STA MON_MSG_PTR_HI          ; Clear message pointer high byte
    STA MON_MSG_TMP_POS         ; Clear temp message pointer

    ; Initialize cursor position to top-left of screen
    STA CURSOR_X                ; Set cursor X to 0
    STA CURSOR_Y                ; Set cursor Y to 0
    
    ; Initialize screen pointer to start of screen memory
    LDA #<SCREEN_START          ; Load low byte of screen start ($00)
    STA SCREEN_PTR_LO           ; Store in screen pointer
    LDA #>SCREEN_START          ; Load high byte of screen start ($04)
    STA SCREEN_PTR_HI           ; Store in screen pointer

    ; Clear command buffer
    LDX #$00                    ; Start at beginning of buffer

; Only clear up to the previous command length
MON_CLEAR_CMDBUF:
    LDX MON_CMDLEN              ; Get previous length
    BEQ CLEAR_DONE              ; If zero, nothing to clear
    LDA #$00

CLEAR_LOOP:
    DEX
    STA MON_CMDBUF,X
    BNE CLEAR_LOOP

CLEAR_DONE:
    STX MON_CMDLEN              ; X is now 0

    ; Enable interrupts now that system is initialized
    CLI

; ================================================================
; MAIN MONITOR LOOP
; ================================================================
    ; Display welcome message
    LDA #<MSG_WELCOME           ; Load low byte of message address
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_WELCOME           ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message
    JSR PRINT_NEWLINE           ; Add newline for proper formatting

    ; Jump to monitor main loop
    JMP MONITOR_MAIN

; ================================================================
; MONITOR HEX CONVERSION ROUTINES
; ================================================================

; Convert ASCII hex character to 4-bit binary value
; Input: A = ASCII character ('0'-'9', 'A'-'F', 'a'-'f')
; Output: A = 4-bit value (0-15), Carry clear if valid, set if invalid
; Preserves: X, Y
HEX_CHAR_TO_NIBBLE:
    ; Convert lowercase to uppercase if needed
    CMP #$61                    ; 'a'
    BCC NOT_LOWERCASE_HEX
    CMP #$67                    ; 'g' (one past 'f')
    BCS NOT_LOWERCASE_HEX
    AND #$DF                    ; Convert to uppercase (clear bit 5)
NOT_LOWERCASE_HEX:

    ; Subtract '0' to normalize
    SEC
    SBC #$30                    ; Subtract '0' (ASCII $30)

    ; Check if it was 0-9 (result will be 0-9)
    CMP #$0A                    ; Is it less than 10?
    BCC HEX_CHAR_VALID          ; Yes, it's 0-9, we're done

    ; Check if it's A-F (after subtracting '0', 'A' becomes $11)
    CMP #$11                    ; 'A' - '0' = $41 - $30 = $11
    BCC HEX_CHAR_INVALID        ; Less than 'A'
    CMP #$17                    ; 'F' - '0' + 1 = $46 - $30 + 1 = $17
    BCS HEX_CHAR_INVALID        ; Greater than 'F'

    ; It's A-F, subtract 7 more to get 10-15
    SEC
    SBC #$07                    ; 'A' - '0' - 7 = 10

HEX_CHAR_VALID:
    CLC                         ; Clear carry for success
    RTS

HEX_CHAR_INVALID:
    SEC                         ; Set carry for error
    RTS

; Convert 4-bit binary value to ASCII hex character using lookup table
; Input: A = 4-bit value (0-15)
; Output: A = ASCII character ('0'-'9', 'A'-'F')
; Preserves: Y
NIBBLE_TO_HEX_CHAR:
    AND #$0F                    ; Ensure only low 4 bits
    TAX                         ; Transfer to X for indexing
    LDA HEX_LOOKUP_TABLE,X      ; Load from lookup table ($F0+X)
    RTS                         ; Much faster than branching!

NIBBLE_DIGIT:
    ; Handle 0-9: add '0'
    CLC
    ADC #ASCII_0                ; Add '0' (gives '0'-'9')
    RTS

; Convert two ASCII hex characters to a byte
; Input: X = pointer to first hex character in MON_CMDBUF
; Output: A = byte value, Carry clear if valid, set if invalid
;         X = X + 2 (points to character after the hex pair)
; Uses: MON_HEX_TEMP
HEX_PAIR_TO_BYTE:
    ; Convert first character (high nibble)
    LDA MON_CMDBUF,X            ; Load first hex character
    JSR HEX_CHAR_TO_NIBBLE      ; Convert to nibble
    BCS HEX_PAIR_ERROR          ; If invalid, return error

    ASL A                       ; Shift left 4 times to make high nibble
    ASL A
    ASL A
    ASL A
    STA MON_HEX_TEMP            ; Store high nibble

    ; Convert second character (low nibble)
    INX                         ; Move to second character
    LDA MON_CMDBUF,X            ; Load second hex character
    JSR HEX_CHAR_TO_NIBBLE      ; Convert to nibble
    BCS HEX_PAIR_ERROR          ; If invalid, return error

    ; Combine nibbles
    ORA MON_HEX_TEMP            ; OR with high nibble
    INX                         ; Move past the hex pair
    CLC                         ; Clear carry for success
    RTS

HEX_PAIR_ERROR:
    SEC                         ; Set carry for error
    RTS

; Convert byte to two ASCII hex characters using lookup table
; Input: A = byte value
; Output: First char in MON_HEX_TEMP, second char in A
; Preserves: Y
BYTE_TO_HEX_PAIR:
    PHA                         ; Save original byte

    ; Convert high nibble
    LSR A                       ; Shift right 4 times
    LSR A
    LSR A
    LSR A
    TAX                         ; Use as index
    LDA HEX_LOOKUP_TABLE,X      ; Get hex char from lookup
    STA MON_HEX_TEMP            ; Store first character

    ; Convert low nibble
    PLA                         ; Restore original byte
    AND #$0F                    ; Keep only low nibble
    TAX                         ; Use as index
    LDA HEX_LOOKUP_TABLE,X      ; Get hex char from lookup
    RTS

; Convert four ASCII hex characters to 16-bit address
; Input: X = pointer to first hex character in MON_CMDBUF
; Output: MON_CURRADDR_HI/LO = 16-bit value, Carry clear if valid, set if invalid
;         X = X + 4 (points to character after the hex quartet)
HEX_QUAD_TO_ADDR:
    ; Convert first byte (high byte of address)
    JSR HEX_PAIR_TO_BYTE        ; Convert first two hex chars
    BCS HEX_QUAD_ERROR          ; If invalid, return error
    STA MON_CURRADDR_HI         ; Store high byte

    ; Convert second byte (low byte of address)
    JSR HEX_PAIR_TO_BYTE        ; Convert next two hex chars
    BCS HEX_QUAD_ERROR          ; If invalid, return error
    STA MON_CURRADDR_LO         ; Store low byte

    CLC                         ; Clear carry for success
    RTS

HEX_QUAD_ERROR:
    SEC                         ; Set carry for error
    RTS

; Convert 16-bit address to four ASCII hex characters
; Input: MON_CURRADDR_HI/LO = 16-bit address
; Output: Four characters stored starting at MON_CMDBUF,X
;         X = X + 4 (points to position after the four characters)
ADDR_TO_HEX_QUAD:
    ; Convert high byte
    LDA MON_CURRADDR_HI         ; Load high byte
    JSR BYTE_TO_HEX_PAIR        ; Convert to two hex chars
    PHA                         ; Save second character (in A)
    LDA MON_HEX_TEMP            ; Get first character
    STA MON_CMDBUF,X            ; Store in buffer
    INX                         ; Move to next position
    PLA                         ; Restore second character
    STA MON_CMDBUF,X            ; Store in buffer
    INX                         ; Move to next position

    ; Convert low byte
    LDA MON_CURRADDR_LO         ; Load low byte
    JSR BYTE_TO_HEX_PAIR        ; Convert to two hex chars
    PHA                         ; Save second character (in A)
    LDA MON_HEX_TEMP            ; Get first character
    STA MON_CMDBUF,X            ; Store in buffer
    INX                         ; Move to next position
    PLA                         ; Restore second character
    STA MON_CMDBUF,X            ; Store in buffer
    INX                         ; Move to next position
    RTS

; ================================================================
; MONITOR SCREEN OUTPUT AND INPUT ROUTINES
; ================================================================

; Scroll screen up by one line (40 characters)
; Modifies: A, X, Y
SCROLL_SCREEN:
    ; Save zero page pointers we'll use
    LDA SCREEN_PTR_LO
    PHA
    LDA SCREEN_PTR_HI
    PHA

    ; Copy 24 lines up (line 1-24 to line 0-23)
    ; Source starts at line 1 ($0428)
    LDA #$28                    ; Low byte of second line
    STA SCREEN_PTR_LO
    LDA #$04                    ; High byte
    STA SCREEN_PTR_HI

    ; Copy 960 bytes (24 lines * 40 chars)
    LDY #$00                    ; Y stays at 0
    LDX #$00                    ; Line counter

SCROLL_LINE_LOOP:
    ; Copy one line (40 bytes)
    LDY #$00
SCROLL_CHAR_LOOP:
    LDA (SCREEN_PTR_LO),Y       ; Read from source
    STA $0400,Y                 ; Write to line above
    INY
    CPY #SCREEN_WIDTH           ; 40 characters per line
    BNE SCROLL_CHAR_LOOP

    ; Move to next line
    CLC
    LDA SCREEN_PTR_LO
    ADC #SCREEN_WIDTH
    STA SCREEN_PTR_LO
    BCC SCROLL_NO_CARRY
    INC SCREEN_PTR_HI
SCROLL_NO_CARRY:

    INX
    CPX #24                     ; Done 24 lines?
    BNE SCROLL_LINE_LOOP

    ; Clear the bottom line (line 24)
    LDY #$00
    LDA #ASCII_SPACE
CLEAR_BOTTOM_LINE:
    STA $07C0,Y                 ; Line 24 starts at $07C0
    INY
    CPY #SCREEN_WIDTH
    BNE CLEAR_BOTTOM_LINE

    ; Restore zero page pointers
    PLA
    STA SCREEN_PTR_HI
    PLA
    STA SCREEN_PTR_LO

    RTS

; Clears all 1024 bytes of screen memory (4 pages of 256 bytes each)
; Input: None
; Modifies: A, X
CLEAR_SCREEN:
    LDA #$20                    ; Load space character once
    LDX #$00                    ; Initialize index

CLEAR_SCREEN_LOOP:
    ; Unrolled loop - do two sets of writes per iteration
    ; This cuts loop overhead in half (128 iterations instead of 256)

    ; First set of writes
    STA $0400,X                 ; Clear screen memory page 1
    STA $0500,X                 ; Clear screen memory page 2
    STA $0600,X                 ; Clear screen memory page 3
    STA $0700,X                 ; Clear screen memory page 4

    ; Second set of writes (X+1)
    INX                         ; Increment to next position
    STA $0400,X                 ; Clear screen memory page 1
    STA $0500,X                 ; Clear screen memory page 2
    STA $0600,X                 ; Clear screen memory page 3
    STA $0700,X                 ; Clear screen memory page 4

    INX                         ; Increment for next iteration
    BNE CLEAR_SCREEN_LOOP       ; Loop until X wraps to 0

    RTS

; Print a single character to screen at current cursor position
; Input: A = character to print
; Modifies: A, X, Y
; Optimized version using direct screen pointer tracking
PRINT_CHAR:
    ; Handle special characters
    CMP #ASCII_CR               ; Is it carriage return?
    BEQ PRINT_CHAR_NEWLINE      ; Handle newline
    
    CMP #ASCII_BACKSPACE        ; Is it backspace?
    BEQ PRINT_CHAR_BACKSPACE    ; Handle backspace
    
    ; Save Y register to memory variable (preserves A with character)
    STY MON_MSG_TMP_POS         ; Save Y register to memory
    
    ; Store character directly to screen memory
    LDY #$00                    ; Y=0 for indirect addressing
    STA (SCREEN_PTR_LO),Y       ; Store character (A still has the character!)
    
    ; Restore Y register
    LDY MON_MSG_TMP_POS         ; Restore Y from memory
    
    ; Advance screen pointer
    INC SCREEN_PTR_LO           ; Increment low byte
    BNE PRINT_CHAR_NO_CARRY     ; If no carry, continue
    INC SCREEN_PTR_HI           ; Increment high byte if carry
    
PRINT_CHAR_NO_CARRY:
    ; Advance cursor X position
    INC CURSOR_X
    
    ; Check for line wrap (X >= 40)
    PHA                         ; Save character
    LDA CURSOR_X
    CMP #SCREEN_WIDTH           ; Check if X >= 40
    PLA                         ; Restore character
    BCC PRINT_CHAR_DONE         ; If not, we're done

    ; Handle line wrap: reset X to 0, increment Y
    LDA #$00
    STA CURSOR_X                ; Reset X to 0
    INC CURSOR_Y                ; Move to next line

    ; Check if we need to scroll screen
    LDA CURSOR_Y
    CMP #SCREEN_HEIGHT          ; Have we gone past line 24?
    BCC PRINT_CHAR_DONE         ; If not, we're done

    ; Need to scroll
    JSR SCROLL_SCREEN           ; Scroll everything up one line

    ; Stay on bottom line
    LDA #SCREEN_HEIGHT-1        ; Set Y to 24 (last line)
    STA CURSOR_Y

    ; Adjust screen pointer to start of bottom line
    LDA #<($0400 + 24 * 40)     ; $07C0
    STA SCREEN_PTR_LO
    LDA #>($0400 + 24 * 40)
    STA SCREEN_PTR_HI

PRINT_CHAR_DONE:
    RTS

PRINT_CHAR_NEWLINE:
    ; Handle carriage return - move to next line
    TYA                         ; Transfer Y to A
    PHA                         ; Push Y register onto stack
    
    ; Calculate remaining characters to next line
    LDA #SCREEN_WIDTH           ; Load screen width (40)
    SEC
    SBC CURSOR_X                ; Subtract current X position
    
    ; Advance screen pointer by remaining characters
    CLC
    ADC SCREEN_PTR_LO           ; Add to low byte
    STA SCREEN_PTR_LO           ; Store result
    LDA #$00                    ; Clear A
    ADC SCREEN_PTR_HI           ; Add any carry to high byte
    STA SCREEN_PTR_HI           ; Store result
    
    ; Update cursor position
    LDA #$00                    ; Reset X to beginning of line
    STA CURSOR_X
    INC CURSOR_Y                ; Move to next line
    
    ; Check if we need to scroll screen
    LDA CURSOR_Y
    CMP #SCREEN_HEIGHT          ; Have we gone past line 24?
    BCC PRINT_CHAR_NEWLINE_DONE ; If not, we're done

    ; Need to scroll
    JSR SCROLL_SCREEN           ; Scroll everything up one line

    ; Stay on bottom line
    LDA #SCREEN_HEIGHT-1        ; Set Y to 24 (last line)
    STA CURSOR_Y

    ; Adjust screen pointer to start of bottom line
    LDA #<($0400 + 24 * 40)     ; $07C0
    STA SCREEN_PTR_LO
    LDA #>($0400 + 24 * 40)
    STA SCREEN_PTR_HI

PRINT_CHAR_NEWLINE_DONE:
    PLA                         ; Pull Y register from stack
    TAY                         ; Transfer A to Y
    RTS

PRINT_CHAR_BACKSPACE:
    ; Handle backspace - move cursor back and clear character
    ; Check if we're at beginning of line
    LDA CURSOR_X
    BEQ PRINT_CHAR_DONE         ; If X=0, can't go back further
    
    ; Move cursor back one position
    DEC CURSOR_X                ; Decrement cursor X position
    
    ; Decrement screen pointer
    LDA SCREEN_PTR_LO           ; Get current screen pointer low
    BNE PRINT_BACKSPACE_NO_BORROW ; If not zero, no borrow needed
    DEC SCREEN_PTR_HI           ; Decrement high byte if borrow

PRINT_BACKSPACE_NO_BORROW:
    DEC SCREEN_PTR_LO           ; Decrement low byte
    
    ; Save Y register to memory variable (like normal character printing)
    STY MON_MSG_TMP_POS         ; Save Y register to memory
    
    ; Clear the character at new position by writing space
    LDY #$00                    ; Y=0 for indirect addressing
    LDA #ASCII_SPACE            ; Load space character
    STA (SCREEN_PTR_LO),Y       ; Store space at previous position
    
    ; Restore Y register
    LDY MON_MSG_TMP_POS         ; Restore Y from memory
    
    RTS                         ; Done with backspace

; Print a newline (carriage return)
; Modifies: A
PRINT_NEWLINE:
    LDA #ASCII_CR               ; Load carriage return
    JSR PRINT_CHAR              ; Print it
    RTS

; Print null-terminated string at address in MON_MSG_PTR_LO/MON_MSG_PTR_HI
; Input: MON_MSG_PTR_LO = low byte of string address, MON_MSG_PTR_HI = high byte
; Modifies: A, Y
PRINT_MESSAGE:
    LDY #$00                    ; Initialize string index

PRINT_MSG_LOOP:
    LDA (MON_MSG_PTR_LO),Y      ; Load character using indirect indexed
    BEQ PRINT_MSG_DONE          ; If null terminator, done
    JSR PRINT_CHAR              ; Print the character
    INY                         ; Move to next character
    BNE PRINT_MSG_LOOP          ; Continue if Y hasn't wrapped (strings < 256 chars)
    ; Note: For strings > 255 chars, we'd need to increment MON_MSG_PTR_HI

PRINT_MSG_DONE:
    RTS

; Wait for and read a single keystroke from keyboard
; Output: A = ASCII character received
; Modifies: A
GET_KEYSTROKE:
    ; Poll PIA for available data

GET_KEYSTROKE_WAIT:
    LDA PIA_CONTROL             ; Read PIA control register
    AND #PIA_DATA_AVAIL         ; Check data available bit
    BEQ GET_KEYSTROKE_WAIT      ; Loop until data available

    ; Read the keystroke
    LDA PIA_DATA                ; Read character from PIA
    RTS

; ================================================================
; MONITOR COMMAND LINE INPUT HANDLER
; ================================================================

; Read a complete command line from keyboard with editing support
; Output: Command stored in MON_CMDBUF, length in MON_CMDLEN
; Supports: backspace editing, 80 character limit
; Modifies: A, X, Y
READ_COMMAND_LINE:
    ; Initialize command buffer
    LDA #$00                    ; Clear accumulator
    STA MON_CMDLEN              ; Reset command length
    LDX #$00                    ; Reset buffer index

READ_CMD_LOOP:
    JSR GET_KEYSTROKE           ; Wait for keystroke

    ; Check for special keys
    CMP #ASCII_CR               ; Is it Enter/Return?
    BEQ READ_CMD_DONE           ; If so, command is complete

    CMP #ASCII_BACKSPACE        ; Is it backspace?
    BEQ READ_CMD_BACKSPACE      ; Handle backspace

    CMP #ASCII_DELETE           ; Is it delete?
    BEQ READ_CMD_BACKSPACE      ; Handle delete same as backspace

    CMP #ASCII_ESC              ; Is it escape?
    BEQ READ_CMD_CANCEL         ; Handle escape (cancel command)

    ; Check if buffer is full
    CPX #MON_CMDBUF_LEN-1       ; Check if at max length (leave room for null)
    BCS READ_CMD_LOOP           ; If full, ignore additional characters

    ; Check if character is printable (space to tilde)
    CMP #ASCII_SPACE            ; Is it less than space?
    BCC READ_CMD_LOOP           ; If so, ignore it
    CMP #$7F                    ; Is it greater than tilde?
    BCS READ_CMD_LOOP           ; If so, ignore it

    ; Convert to uppercase if it's a lowercase letter  
    CMP #$61                    ; Compare with 'a' ($61)
    BCC NOT_LOWERCASE_INPUT     ; If less than 'a', not lowercase
    CMP #$7B                    ; Compare with '{' (one after 'z')
    BCS NOT_LOWERCASE_INPUT     ; If greater than 'z', not lowercase
    AND #$5F                    ; Clear bit 5 to convert to uppercase

NOT_LOWERCASE_INPUT:
    ; Add character to buffer and echo to screen
    STA MON_CMDBUF,X            ; Store character in buffer (now uppercase if it was lowercase)
    JSR PRINT_CHAR              ; Echo character to screen
    INX                         ; Increment buffer position
    STX MON_CMDLEN              ; Update command length
    JMP READ_CMD_LOOP           ; Continue reading

READ_CMD_BACKSPACE:
    ; Handle backspace - remove last character
    CPX #$00                    ; Is buffer empty?
    BEQ READ_CMD_LOOP           ; If empty, ignore backspace

    DEX                         ; Move back one position
    STX MON_CMDLEN              ; Update command length
    LDA #$00                    ; Load null character
    STA MON_CMDBUF,X            ; Clear the character in buffer

    ; Echo backspace to screen (PRINT_CHAR will handle the backspace properly)
    LDA #ASCII_BACKSPACE        ; Print backspace
    JSR PRINT_CHAR              ; PRINT_CHAR now handles backspace correctly

    JMP READ_CMD_LOOP           ; Continue reading

READ_CMD_CANCEL:
    ; Handle escape - clear entire command
    LDA #$00                    ; Clear accumulator
    STA MON_CMDLEN              ; Reset command length
    LDX #$00                    ; Reset buffer index

    ; Clear the buffer
READ_CMD_CLEAR_LOOP:
    STA MON_CMDBUF,X            ; Clear buffer position
    INX                         ; Move to next position
    CPX #MON_CMDBUF_LEN         ; Have we cleared entire buffer?
    BNE READ_CMD_CLEAR_LOOP     ; Continue if not done

    ; Print new prompt
    JSR PRINT_NEWLINE           ; Start new line
    LDX #$00                    ; Reset buffer index
    JMP READ_CMD_LOOP           ; Start over

READ_CMD_DONE:
    ; Command is complete - null terminate it
    LDA #$00                    ; Load null terminator
    STA MON_CMDBUF,X            ; Null terminate the command
    JSR PRINT_NEWLINE           ; Move to next line on screen
    RTS

; Print the monitor prompt based on current mode
; Print current address from MON_CURRADDR_HI/LO as 4 hex digits
; Modifies: A, X
PRINT_CURRENT_ADDRESS:
    ; Print high byte
    LDA MON_CURRADDR_HI         ; Load high byte
    JSR BYTE_TO_HEX_PAIR        ; Convert to hex pair
    PHA                         ; Save second character
    LDA MON_HEX_TEMP            ; Get first hex character
    JSR PRINT_CHAR              ; Print it
    PLA                         ; Restore second character
    JSR PRINT_CHAR              ; Print it

    ; Print low byte
    LDA MON_CURRADDR_LO         ; Load low byte
    JSR BYTE_TO_HEX_PAIR        ; Convert to hex pair
    PHA                         ; Save second character
    LDA MON_HEX_TEMP            ; Get first hex character
    JSR PRINT_CHAR              ; Print it
    PLA                         ; Restore second character
    JSR PRINT_CHAR              ; Print it
    RTS

; Unified monitor prompt printing routine
; Prints: [mode:]address> (e.g., "W:8000> " or "8000> ")
; Modifies: A, X, Y
PRINT_MONITOR_PROMPT:
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
    LDA #$3E                    ; '>' character
    JSR PRINT_CHAR
    LDA #ASCII_SPACE            ; Space after prompt
    JSR PRINT_CHAR
    RTS

; ================================================================
; MONITOR COMMAND PARSER
; ================================================================

; Parse and execute a command from MON_CMDBUF
; Input: Command in MON_CMDBUF, length in MON_CMDLEN
; Modifies: A, X, Y
PARSE_COMMAND:
    ; Initialize parser state
    LDA #$00
    STA MON_PARSE_PTR
    STA MON_ERROR_FLAG
    LDA MON_CMDLEN
    STA MON_PARSE_LEN

    ; Check if command is empty
    BNE PARSE_CMD_START
    JMP PARSE_CMD_DONE

PARSE_CMD_START:
    ; Get first character
    LDX #$00
    LDA MON_CMDBUF,X

    ; Quick range check - is it between 'G' and 'Z'?
    CMP #$47                    ; 'G'
    BCC PARSE_CMD_ERROR         ; Less than 'G'
    CMP #$5B                    ; 'Z'+1
    BCS PARSE_CMD_ERROR         ; Greater than 'Z'

    ; Get index from mapping table
    SEC
    SBC #$47                    ; Subtract 'G' to get offset
    TAX
    LDA CMD_INDEX_MAP,X         ; Get command index
    CMP #$FF                    ; Is it invalid?
    BEQ PARSE_CMD_ERROR

    ; Valid command - use compact jump table
    TAX
    LDA CMD_JUMP_COMPACT_LO,X
    STA JUMP_VECTOR
    LDA CMD_JUMP_COMPACT_HI,X
    STA JUMP_VECTOR+1
    JMP (JUMP_VECTOR)

; Single character commands (no parameters)
PARSE_CMD_KLEAR:
    JSR CMD_CLEAR_SCREEN        ; Execute clear screen command
    JMP PARSE_CMD_DONE

PARSE_CMD_STACK:
    JSR CMD_DUMP_STACK          ; Execute stack dump command
    JMP PARSE_CMD_DONE

PARSE_CMD_ZERO:
    JSR CMD_DUMP_ZERO_PAGE      ; Execute zero page dump command
    JMP PARSE_CMD_DONE

PARSE_CMD_TARGET:
    JSR CMD_SHOW_TARGET         ; Execute show target command
    JMP PARSE_CMD_DONE

PARSE_CMD_HELP:
    JSR CMD_SHOW_HELP           ; Execute help command
    JMP PARSE_CMD_DONE

PARSE_CMD_EXIT:
    JSR CMD_EXIT_MODE           ; Execute exit mode command
    JMP PARSE_CMD_DONE

; Commands with colon syntax (W:, R:, G:)
PARSE_CMD_WRITE_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse W:xxxx format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_WRITE_MODE          ; Execute write mode command
    JMP PARSE_CMD_DONE

PARSE_CMD_READ_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse R:xxxx or R:xxxx-yyyy format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_READ_MODE           ; Execute read mode command
    JMP PARSE_CMD_DONE

PARSE_CMD_GO_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse G:xxxx format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_GO_MODE             ; Execute go mode command
    JMP PARSE_CMD_DONE

PARSE_CMD_LOAD_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse L:xxxx format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_LOAD_MODE           ; Execute load mode command
    JMP PARSE_CMD_DONE

PARSE_CMD_ERROR:
    ; Display error message for invalid command
    LDA #$01                    ; Set error flag
    STA MON_ERROR_FLAG
    JSR PRINT_ERROR_MSG         ; Print error message
    ; Fall through to done

PARSE_CMD_DONE:
    RTS

; Parse colon command syntax (e.g., "W:8000", "R:8000-8010")
; Input: Command in MON_CMDBUF starting with command character
; Output: Address(es) parsed into MON_CURRADDR_HI/LO and optionally MON_ENDADDR_HI/LO
;         Carry clear if successful, set if error
; Modifies: A, X, Y
PARSE_COLON_COMMAND:
    ; Check for colon after command character
    LDX #$01                    ; Position after command character
    LDA MON_CMDBUF,X            ; Load second character
    CMP #ASCII_COLON            ; Is it a colon?
    BNE PARSE_COLON_ERROR       ; If not, error

    ; Move past the colon
    INX                         ; X now points to first character after colon

    ; Parse the hex address
    JSR HEX_QUAD_TO_ADDR        ; Parse 4-hex-digit address
    BCS PARSE_COLON_ERROR       ; If error, return error

    ; Check if there's more (for range commands like R:8000-8010)
    CPX MON_CMDLEN              ; Are we at end of command?
    BEQ PARSE_COLON_SUCCESS     ; If so, single address is complete

    ; Check for dash (range separator)
    LDA MON_CMDBUF,X            ; Load next character
    CMP #ASCII_DASH             ; Is it a dash?
    BNE PARSE_COLON_SUCCESS     ; If not, assume single address

    ; Parse range - copy current address to start address
    LDA MON_CURRADDR_LO         ; Copy current address to start address
    STA MON_STARTADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_STARTADDR_HI

    ; Move past the dash and parse end address
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

PARSE_COLON_SUCCESS:
    CLC                         ; Clear carry for success
    RTS

PARSE_COLON_ERROR:
    SEC                         ; Set carry for error
    RTS

; Print error message for invalid commands
; Modifies: A, X, Y
PRINT_ERROR_MSG:
    ; Print syntax error message
    LDA #<MSG_SYNTAX_ERROR      ; Load low byte of message address
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_SYNTAX_ERROR      ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message
    JSR PRINT_NEWLINE
    RTS

; Checks to see if the command entered is 'X:' to exit the current mode.
; Modifies A
; Returns carry set if "X:" command detected
CHECK_EXIT_COMMAND:
    LDA MON_CMDLEN
    CMP #$02
    BNE NOT_EXIT_CMD
    LDA MON_CMDBUF
    CMP #CMD_EXIT
    BNE NOT_EXIT_CMD
    LDA MON_CMDBUF+1
    CMP #ASCII_COLON
    BNE NOT_EXIT_CMD
    SEC                     ; Set carry for exit command
    RTS
NOT_EXIT_CMD:
    CLC                     ; Clear carry for not exit
    RTS

; ================================================================
; MONITOR COMMAND IMPLEMENTATIONS
; ================================================================

; Clear screen command (K:)
; Modifies: A, X
CMD_CLEAR_SCREEN:
    JSR CLEAR_SCREEN            ; Call the unrolled clear screen routine
    
    ; Reset cursor position to top-left
    LDA #$00
    STA CURSOR_X                ; Reset cursor X to 0
    STA CURSOR_Y                ; Reset cursor Y to 0
    
    ; Reset screen pointer to start of screen memory
    LDA #<SCREEN_START          ; Load low byte of screen start ($00)
    STA SCREEN_PTR_LO           ; Store in screen pointer
    LDA #>SCREEN_START          ; Load high byte of screen start ($04)
    STA SCREEN_PTR_HI           ; Store in screen pointer
    RTS

; Stack dump command (S:) - Display stack memory ($0100-$01FF)
; Modifies: A, X, Y
CMD_DUMP_STACK:
    ; Set up for memory dump of stack area
    LDA #$00                    ; Start address low byte
    STA MON_STARTADDR_LO
    LDA #$01                    ; Start address high byte
    STA MON_STARTADDR_HI
    LDA #$FF                    ; End address low byte
    STA MON_ENDADDR_LO
    LDA #$01                    ; End address high byte
    STA MON_ENDADDR_HI
    JSR DUMP_MEMORY_RANGE       ; Use common memory dump routine
    RTS

; Zero page dump command (Z:) - Display zero page memory ($0000-$00FF)
; Modifies: A, X, Y
CMD_DUMP_ZERO_PAGE:
    ; Set up for memory dump of zero page
    LDA #$00                    ; Start address low byte
    STA MON_STARTADDR_LO
    LDA #$00                    ; Start address high byte
    STA MON_STARTADDR_HI
    LDA #$FF                    ; End address low byte
    STA MON_ENDADDR_LO
    LDA #$00                    ; End address high byte
    STA MON_ENDADDR_HI
    JSR DUMP_MEMORY_RANGE       ; Use common memory dump routine
    RTS

; Show target address command (T:) - Display current address and its value
; Modifies: A, X, Y
CMD_SHOW_TARGET:
    ; Display current address in MON_CURRADDR_HI/LO
    LDX #$00                    ; Start at beginning of buffer
    JSR ADDR_TO_HEX_QUAD        ; Convert address to hex string

    ; Print the address
    LDX #$00                    ; Start at beginning of buffer
    LDY #$04                    ; Print 4 characters
CMD_SHOW_TARGET_LOOP:
    LDA MON_CMDBUF,X            ; Load character
    JSR PRINT_CHAR              ; Print it
    INX                         ; Move to next character
    DEY                         ; Decrement count
    BNE CMD_SHOW_TARGET_LOOP    ; Continue until done

    ; Print colon and space
    LDA #ASCII_COLON            ; Print colon
    JSR PRINT_CHAR
    LDA #ASCII_SPACE            ; Print space
    JSR PRINT_CHAR

    ; Print the byte value at current address
    LDY #$00                    ; Use Y=0 for indirect addressing
    LDA (MON_CURRADDR_LO),Y     ; Load byte from current address
    JSR BYTE_TO_HEX_PAIR        ; Convert to hex pair
    LDA MON_HEX_TEMP            ; Print first hex character
    JSR PRINT_CHAR
    ; A still contains second character from BYTE_TO_HEX_PAIR
    JSR PRINT_CHAR              ; Print second hex character

    JSR PRINT_NEWLINE           ; End with newline
    RTS

; Show help command (H:) - Display available commands
; Modifies: A, X, Y
CMD_SHOW_HELP:
    ; Print comprehensive help for all monitor commands
    JSR PRINT_HELP_HEADER       ; Print "6502 MONITOR COMMANDS"
    JSR PRINT_NEWLINE

    ; Print each command with description
    JSR PRINT_HELP_BODY
    JSR PRINT_NEWLINE
    RTS

; Print help header text
PRINT_HELP_HEADER:
    LDA #<MSG_HELP_HEADER       ; Load low byte of message address
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_HELP_HEADER       ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message
    RTS

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
    JSR PRINT_MESSAGE
    JSR PRINT_NEWLINE
    INX
    CPX #18                 ; 9 messages * 2 bytes each
    BNE HELP_LOOP
    RTS

; Exit mode command (X:) - Return to command mode
; Modifies: A
CMD_EXIT_MODE:
    LDA #MON_MODE_CMD           ; Set to command mode
    STA MON_MODE                ; Update mode
    RTS

; Write mode command (W:xxxx) - Enter write mode at specified address
; Input: Address in MON_CURRADDR_HI/LO
; Modifies: A, X, Y
CMD_WRITE_MODE:
    LDA #MON_MODE_WRITE         ; Set to write mode
    STA MON_MODE                ; Update mode

    ; Display current byte at target address
    JSR SHOW_WRITE_ADDRESS      ; Show "XXXX: YY" format

    ; Enter write mode loop for sequential input
    JSR WRITE_MODE_LOOP         ; Handle sequential byte input
    RTS

; Read mode command (R:xxxx or R:xxxx-yyyy) - Enter read mode and display memory
; Input: Address(es) in MON_CURRADDR_HI/LO and optionally MON_ENDADDR_HI/LO
; Modifies: A, X, Y
CMD_READ_MODE:
    LDA #MON_MODE_READ          ; Set to read mode
    STA MON_MODE                ; Update mode

    ; Check if we have end address (range operation)
    LDA MON_ENDADDR_HI          ; Check if end address is set
    ORA MON_ENDADDR_LO          ; (non-zero means range)
    BEQ CMD_READ_SINGLE         ; If zero, single address

    ; Range operation - dump memory range with 8-byte formatting
    JSR READ_MEMORY_RANGE       ; Display the range with read-specific formatting
    JMP CMD_READ_DONE

CMD_READ_SINGLE:
    ; Single address - show single byte in format "xxxx: bb"
    JSR SHOW_READ_ADDRESS       ; Use read-specific display routine

CMD_READ_DONE:
    ; After read operation, remain in read mode for continued interaction
    JSR READ_MODE_LOOP          ; Enter interactive read mode
    RTS

; Go/Run mode command (G:xxxx) - Execute program at specified address
; Input: Address in MON_CURRADDR_HI/LO
; Modifies: A
CMD_GO_MODE:
    LDA #MON_MODE_RUN           ; Set to run mode
    STA MON_MODE                ; Update mode

    ; Display message indicating program execution
    JSR PRINT_RUN_MESSAGE       ; Print "RUNNING AT XXXX"

    ; Jump to the specified address (this gives control to user program)
    ; Note: The user program must somehow return control to the monitor
    ; (typically via software interrupt or jump back to MONITOR_MAIN)
    JMP (MON_CURRADDR_LO)       ; Jump to address (indirect)

; Load mode command (L:xxxx) - Enter load mode at specified address
; Input: Address in MON_CURRADDR_HI/LO
; Modifies: A, X, Y
CMD_LOAD_MODE:
    LDA #MON_MODE_LOAD          ; Set to load mode
    STA MON_MODE                ; Update mode

    ; Display current address for loading
    JSR SHOW_LOAD_ADDRESS       ; Show "LOAD AT XXXX:" format

    ; Enter load mode loop for filename input
    JSR LOAD_MODE_LOOP          ; Handle filename input and loading
    RTS

; Print run message showing execution address
; Modifies: A, X, Y
PRINT_RUN_MESSAGE:
    ; Print "RUNNING AT "
    LDA #<MSG_RUN_AT            ; Load low byte of message address
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_RUN_AT            ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message

    ; Print the execution address
    LDX #$00                    ; Start at beginning of buffer
    JSR ADDR_TO_HEX_QUAD        ; Convert address to hex string

    ; Print the address
    LDX #$00                    ; Start at beginning of buffer
    LDY #$04                    ; Print 4 characters
PRINT_RUN_ADDR_LOOP:
    LDA MON_CMDBUF,X            ; Load character
    JSR PRINT_CHAR              ; Print it
    INX                         ; Move to next character
    DEY                         ; Decrement count
    BNE PRINT_RUN_ADDR_LOOP     ; Continue until done

    JSR PRINT_NEWLINE           ; End with newline
    RTS

; Optimized memory dump routine with inlined hex conversion
; Input: Start address in MON_STARTADDR_HI/LO, end address in MON_ENDADDR_HI/LO
; Modifies: A, X, Y
; Uses: MON_MSG_TMP_POS as temporary storage
DUMP_MEMORY_RANGE:
    ; Initialize line counter
    LDA #$00
    STA MON_LINE_COUNT

    ; Copy start address to current address
    LDA MON_STARTADDR_LO
    STA MON_CURRADDR_LO
    LDA MON_STARTADDR_HI
    STA MON_CURRADDR_HI

DUMP_RANGE_LOOP:
    ; Print high byte of address (first two hex digits)
    LDA MON_CURRADDR_HI         ; Load high byte
    LSR A                       ; Shift right 4 times for high nibble
    LSR A
    LSR A
    LSR A
    TAX                         ; Use as index
    LDA HEX_LOOKUP_TABLE,X      ; Get hex character from lookup
    JSR PRINT_CHAR              ; Print first hex digit

    LDA MON_CURRADDR_HI         ; Reload high byte
    AND #$0F                    ; Keep only low nibble
    TAX                         ; Use as index
    LDA HEX_LOOKUP_TABLE,X      ; Get hex character from lookup
    JSR PRINT_CHAR              ; Print second hex digit

    ; Print low byte of address (last two hex digits)
    LDA MON_CURRADDR_LO         ; Load low byte
    LSR A                       ; Shift right 4 times for high nibble
    LSR A
    LSR A
    LSR A
    TAX                         ; Use as index
    LDA HEX_LOOKUP_TABLE,X      ; Get hex character from lookup
    JSR PRINT_CHAR              ; Print third hex digit

    LDA MON_CURRADDR_LO         ; Reload low byte
    AND #$0F                    ; Keep only low nibble
    TAX                         ; Use as index
    LDA HEX_LOOKUP_TABLE,X      ; Get hex character from lookup
    JSR PRINT_CHAR              ; Print fourth hex digit

    ; Print colon and space
    LDA #ASCII_COLON
    JSR PRINT_CHAR
    LDA #ASCII_SPACE
    JSR PRINT_CHAR

    LDY #$00                    ; Byte counter for this line
    TYA
    STA MON_BYTE_COUNT          ; Initialize byte counter for this line

DUMP_PRINT_BYTES:
    ; Check if we've reached end address
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BCC DUMP_PRINT_BYTE         ; Current < end, continue
    BNE DUMP_RANGE_DONE         ; Current > end, done
    LDA MON_CURRADDR_LO
    CMP MON_ENDADDR_LO
    BEQ DUMP_PRINT_LAST_BYTE    ; Equal means this is last byte
    BCS DUMP_RANGE_DONE         ; Current > end, done

DUMP_PRINT_BYTE:
    ; Load byte and convert to hex inline
    LDA (MON_CURRADDR_LO),Y     ; Load byte from memory (Y=0)

    ; Print high nibble
    LSR A                       ; Shift right 4 times
    LSR A
    LSR A
    LSR A
    TAX                         ; Use as index
    LDA HEX_LOOKUP_TABLE,X      ; Get hex character
    JSR PRINT_CHAR              ; Print high nibble

    ; Print low nibble
    LDA (MON_CURRADDR_LO),Y     ; Restore byte
    AND #$0F                    ; Keep only low nibble
    TAX                         ; Use as index
    LDA HEX_LOOKUP_TABLE,X      ; Get hex character
    JSR PRINT_CHAR              ; Print low nibble

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
    CMP #MON_BYTES_PER_LINE     ; Have we printed 8 bytes?
    BNE DUMP_PRINT_BYTES        ; If not, continue

    ; End of line
    JSR PRINT_NEWLINE
    JMP DUMP_RANGE_LOOP         ; Start next line

DUMP_PRINT_LAST_BYTE:
    ; Print the final byte (inline conversion)
    LDA (MON_CURRADDR_LO),Y     ; Load final byte
    STA MON_MSG_TMP_POS         ; Save byte

    ; Print high nibble
    LSR A
    LSR A
    LSR A
    LSR A
    TAX
    LDA HEX_LOOKUP_TABLE,X
    JSR PRINT_CHAR

    ; Print low nibble
    LDA MON_MSG_TMP_POS
    AND #$0F
    TAX

    LDA HEX_LOOKUP_TABLE,X
    JSR PRINT_CHAR

DUMP_RANGE_DONE:
    JSR PRINT_NEWLINE
    RTS

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
    LDY #$00                    ; Use Y=0 for indirect addressing
    LDA (MON_CURRADDR_LO),Y     ; Load byte from current address
    JSR BYTE_TO_HEX_PAIR        ; Convert to hex pair
    PHA                         ; Save second character (in A)
    LDA MON_HEX_TEMP            ; Get first hex character
    JSR PRINT_CHAR              ; Print it
    PLA                         ; Restore second character
    JSR PRINT_CHAR              ; Print it

    JSR PRINT_NEWLINE           ; End with newline
    RTS

; Write mode main loop - handles sequential byte input
; Implements the requirements for old/new value display and sequential writing
; Modifies: A, X, Y
WRITE_MODE_LOOP:
    ; Initialize byte count for this write operation
    LDA #$00                    ; Clear byte count
    STA MON_BYTE_COUNT

    ; Save starting address for old/new value display
    LDA MON_CURRADDR_LO         ; Save current address as start
    STA MON_STARTADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_STARTADDR_HI

WRITE_MODE_INPUT:
    JSR PRINT_MONITOR_PROMPT    ; Print the prompt
    JSR READ_COMMAND_LINE       ; Read input from user

    ; Check for empty input (special case for write mode)
    LDA MON_CMDLEN
    BEQ WRITE_MODE_DONE         ; If empty, finish write operation

    ; Check if user wants to exit (X:)
    JSR CHECK_EXIT_COMMAND      ; Check for "X:" command
    BCS WRITE_MODE_DONE         ; If carry set, exit command found

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
    JMP WRITE_MODE_PARSE_LOOP   ; Continue

WRITE_MODE_PARSE_BYTE:
    ; Parse two-character hex byte
    JSR HEX_PAIR_TO_BYTE        ; Parse hex pair (X points to first char)
    BCS WRITE_MODE_ERROR        ; If error, show error message

    ; Store the byte at current address
    LDY #$00                    ; Use Y=0 for indirect addressing
    STA (MON_CURRADDR_LO),Y     ; Store byte at current address

    ; Increment byte count and current address
    INC MON_BYTE_COUNT          ; Increment byte count
    INC MON_CURRADDR_LO         ; Increment current address low byte
    BNE WRITE_MODE_NO_CARRY     ; If no carry, continue
    INC MON_CURRADDR_HI         ; Increment high byte if carry

WRITE_MODE_NO_CARRY:
    ; Continue parsing more bytes in the same input line
    JMP WRITE_MODE_PARSE_LOOP

WRITE_MODE_SHOW_RESULT:
    ; Display old and new values if any bytes were written
    LDA MON_BYTE_COUNT          ; Check if any bytes were written
    BEQ WRITE_MODE_INPUT        ; If none, continue input

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

    ; Continue for more input
    JMP WRITE_MODE_INPUT

WRITE_MODE_ERROR:
    ; Display error message for invalid hex input
    JSR PRINT_ERROR_MSG         ; Print error
    JMP WRITE_MODE_INPUT        ; Continue input

WRITE_MODE_DONE:
    ; Exit write mode and return to command mode
    LDA #MON_MODE_CMD           ; Set to command mode
    STA MON_MODE                ; Update mode
    RTS

; ================================================================
; MONITOR READ MODE IMPLEMENTATION
; ================================================================

; Display single address and value for read mode
; Input: Address in MON_CURRADDR_HI/LO
; Modifies: A, X, Y
SHOW_READ_ADDRESS:
    ; Display current address in XXXX: YY format (same as write mode)
    JSR SHOW_WRITE_ADDRESS      ; Reuse write mode display logic
    RTS

; Read mode memory range display with 8-byte line formatting
; Input: Start address in MON_STARTADDR_HI/LO, end address in MON_ENDADDR_HI/LO
; Modifies: A, X, Y
READ_MEMORY_RANGE:
    ; This is essentially the same as DUMP_MEMORY_RANGE but could be
    ; customized for read-specific formatting in the future
    JSR DUMP_MEMORY_RANGE       ; Use common memory dump with 8-byte formatting
    RTS

; Read mode interactive loop - allows continued read operations
; Modifies: A, X, Y
READ_MODE_LOOP:
    ; Stay in read mode and accept additional commands
READ_MODE_INPUT:
    JSR PRINT_MONITOR_PROMPT    ; Print the prompt
    JSR READ_COMMAND_LINE       ; Read input from user

    ; Check if user wants to exit (X:)
    JSR CHECK_EXIT_COMMAND      ; Check for "X:" command
    BCS WRITE_MODE_DONE         ; If carry set, exit command found

READ_MODE_PARSE_CMD:
    ; In read mode, only X: is valid to exit
    ; Any other input is an error
    JSR PRINT_ERROR_MSG         ; Print error
    JMP READ_MODE_INPUT         ; Continue input

READ_MODE_ERROR:
    ; Display error message for invalid commands
    JSR PRINT_ERROR_MSG         ; Print error
    JMP READ_MODE_INPUT         ; Continue input

READ_MODE_DONE:
    ; Exit read mode and return to command mode
    LDA #MON_MODE_CMD           ; Set to command mode
    STA MON_MODE                ; Update mode
    RTS

; ================================================================
; MONITOR LOAD MODE IMPLEMENTATION
; ================================================================

; Display current address for load mode
; Input: Address in MON_CURRADDR_HI/LO
; Modifies: A, X, Y
SHOW_LOAD_ADDRESS:
    ; Display current address in "LOAD AT XXXX:" format
    LDA #<MSG_LOAD_AT           ; Load low byte of message address
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_LOAD_AT           ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message

    ; Print the address
    LDX #$00                    ; Start at beginning of buffer
    JSR ADDR_TO_HEX_QUAD        ; Convert address to hex string

    ; Print the address
    LDX #$00                    ; Start at beginning of buffer
    LDY #$04                    ; Print 4 characters
SHOW_LOAD_ADDR_LOOP:
    LDA MON_CMDBUF,X            ; Load character
    JSR PRINT_CHAR              ; Print it
    INX                         ; Move to next character
    DEY                         ; Decrement count
    BNE SHOW_LOAD_ADDR_LOOP     ; Continue until done

    ; Print colon
    LDA #ASCII_COLON            ; Print colon
    JSR PRINT_CHAR
    JSR PRINT_NEWLINE           ; End with newline
    RTS

; Load mode main loop - handles filename input and loading
; Modifies: A, X, Y
LOAD_MODE_LOOP:
    ; Initialize for load operation
    LDA #$00                    ; Clear accumulator
    STA MON_ERROR_FLAG          ; Clear error flag

LOAD_MODE_INPUT:
    ; Prompt for filename
    LDA #<MSG_FILENAME_PROMPT   ; Load low byte of message address
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_FILENAME_PROMPT   ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message

    ; Read filename from user
    JSR READ_COMMAND_LINE       ; Read input from user

    ; Check for empty input (special case for load mode)
    LDA MON_CMDLEN
    BEQ WRITE_MODE_DONE         ; If empty, finish write operation

    ; Check if user wants to exit (X:)
    JSR CHECK_EXIT_COMMAND      ; Check for "X:" command
    BCS WRITE_MODE_DONE         ; If carry set, exit command found

LOAD_MODE_PROCESS_FILE:
    ; Validate filename (basic check for non-empty)
    LDA MON_CMDLEN              ; Check command length
    BEQ LOAD_MODE_ERROR         ; If empty, error

    ; Store filename in command buffer (already there)
    ; Null-terminate it (already done by READ_COMMAND_LINE)

    ; Request file load from host via PIA
    ; This will be expanded in later phases to communicate with C++ host
    ; For now, just show success message
    LDA #<MSG_LOAD_SUCCESS      ; Load success message
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_LOAD_SUCCESS      ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message
    JSR PRINT_NEWLINE           ; Add newline

    ; Exit load mode after successful operation
    JMP LOAD_MODE_DONE

LOAD_MODE_ERROR:
    ; Display error message for invalid filename
    JSR PRINT_ERROR_MSG         ; Print error
    JMP LOAD_MODE_INPUT         ; Continue input

LOAD_MODE_DONE:
    ; Exit load mode and return to command mode
    LDA #MON_MODE_CMD           ; Set to command mode
    STA MON_MODE                ; Update mode
    RTS

; ================================================================
; MONITOR MAIN COMMAND LOOP
; ================================================================

; Main monitor command loop - reads and processes commands
; This is the heart of the monitor program
MONITOR_MAIN:
    ; Print welcome message (simplified)
    JSR PRINT_NEWLINE           ; Start with a newline

MONITOR_LOOP:
    JSR PRINT_MONITOR_PROMPT    ; Print appropriate prompt
    JSR READ_COMMAND_LINE       ; Read command from user

    ; Check if command is empty
    LDA MON_CMDLEN              ; Load command length
    BEQ MONITOR_LOOP            ; If empty, just show prompt again

    ; Parse and execute the command
    JSR PARSE_COMMAND           ; Parse the command in MON_CMDBUF
    JMP MONITOR_LOOP            ; Continue command loop

; Interrupt service routines (minimal implementations)
IRQ_HANDLER:
    ; Handle IRQ interrupts here
    RTI                         ; Return from interrupt

NMI_HANDLER:
    ; Handle NMI interrupts here
    RTI                         ; Return from interrupt

; ================================================================
; COMMAND JUMP TABLES - For fast command dispatch
; ================================================================

; Compact jump table - only valid commands
; Maps G,H,K,L,R,S,T,W,X,Z to indices 0-9
CMD_JUMP_COMPACT_LO:
    .BYTE <PARSE_CMD_GO_CHECK   ; 0 - 'G'
    .BYTE <PARSE_CMD_HELP       ; 1 - 'H'
    .BYTE <PARSE_CMD_KLEAR      ; 2 - 'K'
    .BYTE <PARSE_CMD_LOAD_CHECK ; 3 - 'L'
    .BYTE <PARSE_CMD_READ_CHECK ; 4 - 'R'
    .BYTE <PARSE_CMD_STACK      ; 5 - 'S'
    .BYTE <PARSE_CMD_TARGET     ; 6 - 'T'
    .BYTE <PARSE_CMD_WRITE_CHECK; 7 - 'W'
    .BYTE <PARSE_CMD_EXIT       ; 8 - 'X'
    .BYTE <PARSE_CMD_ZERO       ; 9 - 'Z'

CMD_JUMP_COMPACT_HI:
    .BYTE >PARSE_CMD_GO_CHECK   ; 0 - 'G'
    .BYTE >PARSE_CMD_HELP       ; 1 - 'H'
    .BYTE >PARSE_CMD_KLEAR      ; 2 - 'K'
    .BYTE >PARSE_CMD_LOAD_CHECK ; 3 - 'L'
    .BYTE >PARSE_CMD_READ_CHECK ; 4 - 'R'
    .BYTE >PARSE_CMD_STACK      ; 5 - 'S'
    .BYTE >PARSE_CMD_TARGET     ; 6 - 'T'
    .BYTE >PARSE_CMD_WRITE_CHECK; 7 - 'W'
    .BYTE >PARSE_CMD_EXIT       ; 8 - 'X'
    .BYTE >PARSE_CMD_ZERO       ; 9 - 'Z'

; Index mapping table - maps command character to table index
; For characters G-Z, subtract 'G' ($47) to get offset into this table
CMD_INDEX_MAP:
    .BYTE 0     ; G -> 0
    .BYTE 1     ; H -> 1
    .BYTE $FF   ; I -> invalid
    .BYTE $FF   ; J -> invalid
    .BYTE 2     ; K -> 2
    .BYTE 3     ; L -> 3
    .BYTE $FF   ; M -> invalid
    .BYTE $FF   ; N -> invalid
    .BYTE $FF   ; O -> invalid
    .BYTE $FF   ; P -> invalid
    .BYTE $FF   ; Q -> invalid
    .BYTE 4     ; R -> 4
    .BYTE 5     ; S -> 5
    .BYTE 6     ; T -> 6
    .BYTE $FF   ; U -> invalid
    .BYTE $FF   ; V -> invalid
    .BYTE 7     ; W -> 7
    .BYTE 8     ; X -> 8
    .BYTE $FF   ; Y -> invalid
    .BYTE 9     ; Z -> 9

; ================================================================
; MODE PREFIX TABLE - Characters for prompt prefixes
; ================================================================
; Indexed by MON_MODE value: CMD(0), WRITE(1), READ(2), RUN(3), LOAD(4)
MODE_PREFIX_TABLE:
    .BYTE 0         ; MON_MODE_CMD = 0: No prefix (just address>)
    .BYTE $57       ; MON_MODE_WRITE = 1: 'W' (W:address>)
    .BYTE $52       ; MON_MODE_READ = 2: 'R' (R:address>)
    .BYTE 0         ; MON_MODE_RUN = 3: No prefix (not used for prompts)
    .BYTE $4C       ; MON_MODE_LOAD = 4: 'L' (L:address>)

; ================================================================
; HELP MESSAGE TABLE - Addresses of help messages for display
; ================================================================
; Word addresses of help messages in display order
HELP_MSG_TABLE:
    .WORD MSG_HELP_WRITE
    .WORD MSG_HELP_READ
    .WORD MSG_HELP_GO
    .WORD MSG_HELP_LOAD
    .WORD MSG_HELP_KLEAR
    .WORD MSG_HELP_STACK
    .WORD MSG_HELP_ZERO
    .WORD MSG_HELP_TARGET
    .WORD MSG_HELP_EXIT

HELP_MSG_COUNT = 9              ; Number of help messages

; ================================================================
; MESSAGE DATA SECTION - Null-terminated strings for monitor
; ================================================================
MSG_HELP_HEADER:     .BYTE "6502 MONITOR COMMANDS", 0
MSG_HELP_WRITE:      .BYTE "W:XXXX WRITE", 0
MSG_HELP_READ:       .BYTE "R:XXXX READ", 0
MSG_HELP_GO:         .BYTE "G:XXXX RUN", 0
MSG_HELP_LOAD:       .BYTE "L:XXXX LOAD", 0
MSG_HELP_KLEAR:      .BYTE "K:     CLEAR SCREEN", 0
MSG_HELP_STACK:      .BYTE "S:     PRINT STACK", 0
MSG_HELP_ZERO:       .BYTE "Z:     PRINT ZERO PAGE", 0
MSG_HELP_TARGET:     .BYTE "T:     PRINT TARGET ADDRESS", 0
MSG_HELP_EXIT:       .BYTE "X:     EXIT TO COMMAND MODE", 0
MSG_RUN_AT:          .BYTE "RUNNING AT ", 0
MSG_LOAD_AT:         .BYTE "LOAD AT ", 0
MSG_FILENAME_PROMPT: .BYTE "FILENAME: ", 0
MSG_LOAD_SUCCESS:    .BYTE "LOADED", 0
MSG_SYNTAX_ERROR:    .BYTE "?", 0
MSG_SUCCESS:         .BYTE "OK", 0
MSG_WELCOME:         .BYTE "-=MFC 6502 OPERATIONAL=-", 0

; ================================================================
; KERNEL API JUMP TABLE
; ================================================================
.org $FF00

; These are indirect jumps to the actual routines
K_PRINT_CHAR:    JMP PRINT_CHAR         ; $FF00
K_PRINT_MESSAGE: JMP PRINT_MESSAGE      ; $FF03
K_PRINT_NEWLINE: JMP PRINT_NEWLINE      ; $FF06
K_GET_KEYSTROKE: JMP GET_KEYSTROKE      ; $FF09
K_CLEAR_SCREEN:  JMP CLEAR_SCREEN       ; $FF0C

; ================================================================
; RESET VECTORS
; ================================================================
.org $FFFA

    .WORD NMI_HANDLER           ; NMI vector ($FFFA-$FFFB)
    .WORD RESET                 ; Reset vector ($FFFC-$FFFD)
    .WORD IRQ_HANDLER           ; IRQ vector ($FFFE-$FFFF)