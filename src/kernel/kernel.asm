; ================================================================
; MFC 6502 KERNEL MONITOR
; ================================================================
; Filename:     kernel.asm
; Author:       Brian Gentry
; Date:         2025-08-05
; Version:      1.0
; Assembler:    ca65
;
; Description:  Machine language monitor for MFC 6502 system
;               Provides memory examination, modification, and
;               program execution capabilities via serial terminal
;
; ================================================================
; MEMORY USAGE SUMMARY
; ================================================================
; ROM Size (Reserved): ($F000-$FFFF) - 4096 bytes
; ROM Size (Used): 3413 bytes
;   CODE segment:   ($F000-$FD3D) - 3389 bytes
;   JUMPS segment:  ($FF00-$FF11) - 18 bytes
;   VECS segment:   ($FFFA-$FFFF) - 6 bytes
;
; Zero Page:    $00-$10 (17 bytes used)
;   Monitor:    $00-$0F (16 bytes)
;   Scroll:     $08-$0C (5 bytes, overlapped)
;   Paging:     $0D-$0E (2 bytes, overlapped)
;   RNG:        $0F-$10 (2 bytes, overlapped)
;
; RAM Usage:    $0200-$0284 (133 bytes)
;   Cmd Buffer: $0200-$024F (80 bytes)
;   Variables:  $0250-$025F (16 bytes)
;
; Stack:        $0100-$01FF (256 bytes)
; Screen RAM:   $0400-$07FF (1024 bytes)
;
; ================================================================
; FEATURES
; ================================================================
; - Interactive command processor
; - Memory read/write/dump operations
; - Program execution (GO command)
; - Hexadecimal conversion routines
; - Screen scrolling with paging
; - Built-in help system
; - Kernel API at $FF00 for user programs
;
; Commands:     R:(read) W:(write) G:(go) L:(load) C:(clear)
;               T:(stack) Z:(zero page) H:(help) F:(fill)
;
; ================================================================
; BUILD INFORMATION
; ================================================================
; Entry Point:  RESET ($F000)
; Vectors:      NMI ($FFFA), RESET ($FFFC), IRQ ($FFFE)
; API Entry:    $FF00 (jump table)
;
; Assembly:     ca65 kernel.asm -o kernel.o && ld65 -C memory.cfg kernel.o -o kernel.rom
; Checksum:     50f82437174779e391d3ac3b5b445357882251a5 (SHA-1)
;
; ================================================================
; REVISION HISTORY
; ================================================================
; 2025-06-01  v1.0  Initial release
;
; ================================================================

.PC02                               ; ca65 directive to enable 65C02 instructions.

.org $F000                          ; ROM start address.

; ================================================================
; SYSTEM CONSTANTS SECTION
; ================================================================
STACK_TOP          = $FF           ; Top of stack page

; ================================================================
; ZERO PAGE VARIABLES
; ================================================================
MON_CURRADDR_LO    = $00           ; Current address low byte (MUST be zero page)
MON_CURRADDR_HI    = $01           ; Current address high byte (MUST be zero page)
MON_MSG_PTR_LO     = $02           ; Message pointer low byte (MUST be zero page)
MON_MSG_PTR_HI     = $03           ; Message pointer high byte (MUST be zero page)
JUMP_VECTOR        = $04           ; Indirect jump vector (2 bytes: $04-$05)
SCREEN_PTR_LO      = $06           ; Current screen memory pointer (low byte)
SCREEN_PTR_HI      = $07           ; Current screen memory pointer (high byte)
SCRL_SRC_ADDR_LO   = $08           ; Scroll source address low byte
SCRL_SRC_ADDR_HI   = $09           ; Scroll source address high byte
SCRL_DEST_ADDR_LO  = $0A           ; Scroll destination address low byte
SCRL_DEST_ADDR_HI  = $0B           ; Scroll destination address high byte
SCRL_BYTE_CNT      = $0C           ; Scroll byte counter
CMD_LINE_COUNT     = $0D           ; Lines printed by current command
PAGE_ABORT_FLAG    = $0E           ; Set to 1 if user pressed ESC
RNG_SEED           = $0F           ; Random number generator seed
RNG_MAX            = $10           ; Maximum value for random number (1-255)
HEX_LOOKUP_TABLE   = $F0           ; Hex lookup table location (16 bytes: $F0-$FF)

; ================================================================
; MONITOR VARIABLES
; ================================================================

; Monitor Command Buffer and State Variables
MON_CMDBUF         = $0200         ; Command input buffer (80 bytes: $0200-$024F)
MON_CMDBUF_LEN     = 80            ; Maximum command buffer length
MON_CMDPTR         = $0250         ; Pointer to current position in command buffer
MON_CMDLEN         = $0251         ; Current length of command in buffer
MON_MODE           = $0252         ; Current monitor mode (0=Command, 1=Write, 2=Read, 3=Run)
MON_STARTADDR_LO   = $0253         ; Start address for range operations (low)
MON_STARTADDR_HI   = $0254         ; Start address for range operations (high)
MON_ENDADDR_LO     = $0255         ; End address for range operations (low)
MON_ENDADDR_HI     = $0256         ; End address for range operations (high)
MON_PARSE_PTR      = $0257         ; Parser position pointer
MON_PARSE_LEN      = $0258         ; Remaining characters to parse
MON_HEX_TEMP       = $0259         ; Temporary storage for hex conversion
MON_BYTE_COUNT     = $025A         ; Count of bytes for write operations
MON_LINE_COUNT     = $025B         ; Line counter for display formatting
MON_ERROR_FLAG     = $025C         ; Error flag for invalid operations
CURSOR_X           = $025D         ; Current cursor X position (0-39)
CURSOR_Y           = $025E         ; Current cursor Y position (0-24)
MON_MSG_TMP_POS    = $025F         ; Temp pointer to current position in message
MON_FILL_VALUE     = $0284         ; Fill command byte value
MON_DEST_ADDR_LO   = $0285         ; Move/Copy destination address low byte
MON_DEST_ADDR_HI   = $0286         ; Move/Copy destination address high byte
MON_COPY_MODE      = $0287         ; Move/Copy mode (0=copy, 1=move)
MON_SEARCH_PATTERN = $0288         ; Search pattern buffer (16 bytes: $0288-$0297)
MON_PATTERN_LEN    = $0298         ; Search pattern length (1-16 bytes)
MON_LAST_CMD_BUF   = $0299         ; Last command buffer (80 bytes: $0299-$02E8)
MON_LAST_CMD_LEN   = $02E9         ; Last command length (1 byte)

; Monitor Mode Constants
MON_MODE_CMD       = 0             ; Command mode
MON_MODE_WRITE     = 1             ; Write mode

; Monitor Display Constants
MON_BYTES_PER_LINE = 8             ; Number of bytes displayed per line
MON_HEX_DIGITS     = 2             ; Hex digits per byte

; Screen and I/O Constants for Monitor
SCREEN_START       = $0400         ; Screen memory start
SCREEN_WIDTH       = 40            ; Characters per line
SCREEN_HEIGHT      = 25            ; Lines on screen
LINES_PER_PAGE     = 24            ; Full screen height
CURSOR_CHAR        = $5F           ; Underscore cursor character

; ASCII Character Constants
ASCII_0            = $30           ; ASCII '0'
ASCII_9            = $39           ; ASCII '9'
ASCII_A            = $41           ; ASCII 'A'
ASCII_F            = $46           ; ASCII 'F'
ASCII_CR           = $0D           ; Carriage return
ASCII_LF           = $0A           ; Line feed
ASCII_SPACE        = $20           ; Space
ASCII_COLON        = $3A           ; Colon ':'
ASCII_DASH         = $2D           ; Dash '-'
ASCII_BACKSPACE    = $08           ; Backspace character
ASCII_DELETE       = $7F           ; Delete character
ASCII_ESC          = $1B           ; Escape character
ASCII_DOT          = $2E           ; Dot '.' character

; Hardware I/O addresses for keyboard input
PIA_DATA           = $DC00         ; PIA data register for keyboard
PIA_CONTROL        = $DC02         ; PIA control register for keyboard
PIA_DATA_AVAIL     = $01           ; Bit mask for data available flag

; File I/O interface addresses
FILE_COMMAND       = $DC10         ; File operation command
FILE_STATUS        = $DC11         ; File operation status
FILE_ADDR_LO       = $DC12         ; Target address low byte
FILE_ADDR_HI       = $DC13         ; Target address high byte
FILE_NAME_BUF      = $DC14         ; Filename buffer start ($DC14-$DC1F)

; Additional file I/O registers for save operations
FILE_END_ADDR_LO   = $DC20         ; End address low byte for save range
FILE_END_ADDR_HI   = $DC21         ; End address high byte for save range

; File command codes
FILE_LOAD_CMD      = $01           ; Load file command
FILE_SAVE_CMD      = $02           ; Save file command

; File status codes
FILE_IDLE          = $00           ; No operation
FILE_IN_PROGRESS   = $01           ; Operation in progress
FILE_SUCCESS       = $02           ; Operation completed successfully
FILE_ERROR         = $FF           ; Operation failed

; ================================================================
; KERNEL PROGRAM START
; ================================================================

; Reset vector entry point.
RESET:
    CLD                         ; Clear decimal mode flag
    SEI                         ; Set interrupt disable flag
    LDX #STACK_TOP              ; Initialize stack pointer to top of stack page
    TXS                         ; Transfer X to stack pointer

; ================================================================
; ZERO PAGE INITIALIZATION
; ================================================================

    LDX #$00                    ; Start at beginning of zero page
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

    JSR CLEAR_SCREEN            ; Clear screen memory ($0400-$07FF)

; ================================================================
; MONITOR INITIALIZATION
; ================================================================

    ; Initialize RNG seed
    LDA #$01                    ; Non-zero seed (LFSR can't use 0)
    STA RNG_SEED                ; Initialize RNG

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
    STA MON_LAST_CMD_LEN        ; Clear last command length

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

    CLI                         ; Enable interrupts

; ================================================================
; MONITOR START/WELCOME MESSAAGE
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
; RANDOM NUMBER GENERATOR ROUTINES
; ================================================================

; Get random number from 1 to RNG_MAX
; Input: RNG_MAX = maximum value
; Output: A = random number from 1 to RNG_MAX
; Modifies: A
GET_RANDOM_NUMBER:
    PHX
    PHY

RANDOM_RETRY:
    JSR GET_RANDOM          ; Get 0-255
    CMP RNG_MAX             ; Compare to max
    BCS RANDOM_RETRY        ; If >= max, try again

    CLC
    ADC #$01                ; Make it 1-based (1 to RNG_MAX)

    PLY
    PLX
    RTS

; Basic random number generator using 8-bit LFSR
; Uses polynomial: x^8 + x^6 + x^5 + x^4 + 1
; Output: A = pseudo-random byte (1-255, never 0)
; Modifies: A
; Preserves: X, Y
GET_RANDOM:
    LDA RNG_SEED            ; Get current seed
    ASL                     ; Shift left
    BCC NO_XOR              ; Branch if no carry

    ; If carry set, XOR with polynomial
    EOR #$1D                ; $1D = %00011101 (taps at bits 0,2,3,4)

NO_XOR:
    STA RNG_SEED            ; Store new value
    RTS                     ; Return with random value in A

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
    PHX                         ; Push X to stack
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
    PLX                         ; Restore X
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
SCROLL_SCREEN:
    ; Save registers
    TXA
    PHA
    TYA
    PHA

    ; Initialize source pointer to line 1 ($0428)
    LDA #$28
    STA SCRL_SRC_ADDR_LO
    LDA #$04
    STA SCRL_SRC_ADDR_HI

    ; Initialize destination pointer to line 0 ($0400)
    LDA #$00
    STA SCRL_DEST_ADDR_LO
    LDA #$04
    STA SCRL_DEST_ADDR_HI

    ; Copy 960 bytes using 16-bit counter
    LDX #$03                    ; High byte of counter (960 = $03C0)
    LDA #$C0                    ; Low byte of counter
    STA SCRL_BYTE_CNT           ; Use dedicated scroll counter

SCROLL_LOOP:
    LDY #$00                    ; Y must be 0 for indirect addressing
    LDA (SCRL_SRC_ADDR_LO),Y    ; Read from source
    STA (SCRL_DEST_ADDR_LO),Y   ; Write to destination

    ; Increment both pointers
    INC SCRL_SRC_ADDR_LO
    BNE SCROLL_NO_SRC_CARRY
    INC SCRL_SRC_ADDR_HI
SCROLL_NO_SRC_CARRY:

    INC SCRL_DEST_ADDR_LO
    BNE SCROLL_NO_DEST_CARRY
    INC SCRL_DEST_ADDR_HI
SCROLL_NO_DEST_CARRY:

    ; Decrement 16-bit counter
    DEC SCRL_BYTE_CNT
    LDA SCRL_BYTE_CNT
    CMP #$FF
    BNE SCROLL_LOOP
    DEX                         ; Decrement high byte
    BPL SCROLL_LOOP             ; Continue if not negative

    ; Clear line 24
    LDY #SCREEN_WIDTH-1
    LDA #ASCII_SPACE

CLEAR_BOTTOM:
    STA $07C0,Y
    DEY
    BPL CLEAR_BOTTOM

    ; Restore registers
    PLA
    TAY
    PLA
    TAX
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
    LDA #ASCII_CR
    JSR PRINT_CHAR

    ; Increment command line counter
    INC CMD_LINE_COUNT

    ; Check if we've printed a full screen in this command
    LDA CMD_LINE_COUNT
    CMP #LINES_PER_PAGE
    BNE PRINT_NEWLINE_DONE     ; Not at page boundary yet

    ; We're about to scroll a full page - pause
    JSR HANDLE_PAGE_BREAK

    ; Reset counter for next page
    LDA #0
    STA CMD_LINE_COUNT

PRINT_NEWLINE_DONE:
    RTS

HANDLE_PAGE_BREAK:
    ; Save cursor state
    LDA CURSOR_X
    PHA
    LDA CURSOR_Y
    PHA
    LDA SCREEN_PTR_LO
    PHA
    LDA SCREEN_PTR_HI
    PHA

    ; Print prompt on current line (before it scrolls away)
    LDA #<MSG_PAGE_PROMPT
    STA MON_MSG_PTR_LO
    LDA #>MSG_PAGE_PROMPT
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE

PAGE_WAIT_KEY:
    JSR GET_KEYSTROKE

    CMP #ASCII_CR             ; Enter to continue
    BEQ PAGE_CONTINUE

    CMP #ASCII_ESC            ; ESC to abort?
    BEQ PAGE_ABORT

    JMP PAGE_WAIT_KEY         ; Wait for valid input

PAGE_ABORT:
    ; Set abort flag
    LDA #1
    STA PAGE_ABORT_FLAG
    ; Fall through to restore

PAGE_CONTINUE:
    ; Restore cursor state
    PLA
    STA SCREEN_PTR_HI
    PLA
    STA SCREEN_PTR_LO
    PLA
    STA CURSOR_Y
    PLA
    STA CURSOR_X
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
    INC RNG_SEED            ; Add entropy while waiting!
    BNE CHECK_KEY           ; Skip if no wrap
    INC RNG_SEED            ; Keep it non-zero
CHECK_KEY:
    LDA PIA_CONTROL         ; Read PIA control register
    AND #PIA_DATA_AVAIL     ; Check data available bit
    BEQ GET_KEYSTROKE_WAIT  ; Loop until data available

    ; Read the keystroke
    LDA PIA_DATA            ; Read character from PIA
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
    BEQ READ_CMD_DONE_CR_JMP    ; If so, command is complete (use local jump)

    CMP #ASCII_BACKSPACE        ; Is it backspace?
    BEQ READ_CMD_BACKSPACE      ; Handle backspace

    CMP #ASCII_DELETE           ; Is it delete?
    BEQ READ_CMD_BACKSPACE      ; Handle delete same as backspace

    CMP #ASCII_ESC              ; Is it escape?
    BEQ READ_CMD_ESCAPE         ; Handle escape (cancel command)

    ; Check for dot command (instant recall of last command)
    CMP #ASCII_DOT              ; Is it a dot?
    BNE CHECK_BUFFER_FULL       ; If not, continue normal processing

    ; Only process dot if it's the first character AND we're in command mode
    CPX #$00                    ; Is buffer empty?
    BNE CHECK_BUFFER_FULL       ; If not, treat as normal character

    ; Check if we're in command mode - only recall in command mode
    LDA MON_MODE                ; Load current mode
    BNE CHECK_BUFFER_FULL       ; If not command mode (0), treat dot as normal character

    ; It's a dot as first character in command mode - recall last command
    JSR RECALL_LAST_COMMAND     ; Recall and display last command
    JMP READ_CMD_LOOP           ; Continue normal input processing

; Local jump helper for CR done (within branch range)
READ_CMD_DONE_CR_JMP:
    JMP READ_CMD_DONE_CR        ; Jump to actual CR handler

CHECK_BUFFER_FULL:
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

READ_CMD_ESCAPE:
    ; Check if buffer is empty
    CPX #$00
    BNE READ_CMD_CANCEL      ; If not empty, clear buffer (existing behavior)

    ; Buffer is empty - treat like we got a special "ESC command"
    LDA #ASCII_ESC
    STA MON_CMDBUF           ; Put ESC as the only character
    LDA #$01
    STA MON_CMDLEN           ; Length = 1
    JSR PRINT_NEWLINE
    RTS

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

READ_CMD_DONE_CR:
    ; Command is complete - null terminate it
    LDA #$00                    ; Load null terminator
    STA MON_CMDBUF,X            ; Null terminate the command
    JSR PRINT_NEWLINE           ; Move to next line on screen
    RTS

; ================================================================
; DOT COMMAND (LAST COMMAND RECALL) IMPLEMENTATION
; ================================================================

; Recall last command and display it in the current command buffer
; Called when '.' is entered as first character in command line
; Modifies: A, X, Y
RECALL_LAST_COMMAND:
    ; Check if we have a last command to recall
    LDA MON_LAST_CMD_LEN        ; Load last command length
    BEQ RECALL_NOTHING          ; If zero, nothing to recall

    ; Clear current command buffer first
    LDX #$00
    LDA #$00
RECALL_CLEAR_LOOP:
    STA MON_CMDBUF,X            ; Clear buffer position
    INX
    CPX #MON_CMDBUF_LEN         ; Check if we've cleared enough
    BNE RECALL_CLEAR_LOOP

    ; Copy last command to current command buffer
    LDX #$00                    ; Initialize copy index
RECALL_COPY_LOOP:
    CPX MON_LAST_CMD_LEN        ; Have we copied all characters?
    BCS RECALL_COPY_DONE        ; If so, we're done copying

    LDA MON_LAST_CMD_BUF,X      ; Load character from last command buffer
    STA MON_CMDBUF,X            ; Store in current command buffer
    JSR PRINT_CHAR              ; Echo character to screen
    INX                         ; Move to next character
    JMP RECALL_COPY_LOOP        ; Continue copying

RECALL_COPY_DONE:
    ; Update current command length
    STX MON_CMDLEN              ; Set current command length
    ; X now contains the number of characters copied
    RTS

RECALL_NOTHING:
    ; No previous command to recall - just continue input normally
    RTS

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
    JMP SAVE_CMD_COPY_LOOP      ; Continue copying

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

    ; Quick range check - is it between 'C' and 'Z'?
    CMP #$43                    ; 'C'
    BCC PARSE_CMD_ERROR_JMP     ; Less than 'C' - jump to local error handler
    CMP #$5B                    ; 'Z'+1
    BCS PARSE_CMD_ERROR_JMP     ; Greater than 'Z' - jump to local error handler

    ; Get index from mapping table
    SEC
    SBC #$43                    ; Subtract 'C' to get offset
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

; Local error handler for range check jumps (within branch range)
PARSE_CMD_ERROR_JMP:
    JMP PARSE_CMD_ERROR         ; Jump to main error handler

; Single character commands (no parameters)
PARSE_CMD_CLEAR:
    JSR CMD_CLEAR_SCREEN        ; Execute clear screen command
    JMP PARSE_CMD_DONE

PARSE_CMD_STACK:
    JSR CMD_DUMP_STACK          ; Execute stack dump command
    JMP PARSE_CMD_DONE

PARSE_CMD_ZERO:
    JSR CMD_DUMP_ZERO_PAGE      ; Execute zero page dump command
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
    JSR PARSE_FILENAME          ; Parse comma and filename
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_LOAD_MODE           ; Execute load mode command
    JMP PARSE_CMD_DONE

PARSE_CMD_SAVE_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse S:xxxx-yyyy format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR PARSE_FILENAME          ; Parse comma and filename
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_SAVE_MODE           ; Execute save mode command
    JMP PARSE_CMD_DONE

PARSE_CMD_FILL_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse F:xxxx-yyyy format
    BCS PARSE_CMD_RANGE_ERROR   ; If address parsing error, show range error
    JSR PARSE_FILL_VALUE        ; Parse comma and fill value
    BCS PARSE_CMD_VALUE_ERROR   ; If value parsing error, show value error
    JSR CMD_FILL_MODE           ; Execute fill mode command
    JMP PARSE_CMD_DONE

PARSE_CMD_MOVE_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse M:xxxx-yyyy format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR PARSE_MOVE_PARAMS       ; Parse comma, destination, and mode
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_MOVE_MODE           ; Execute move/copy mode command
    JMP PARSE_CMD_DONE

PARSE_CMD_SEARCH_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse X:xxxx-yyyy format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR PARSE_SEARCH_PARAMS     ; Parse comma and hex pattern
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_SEARCH_MODE         ; Execute memory search command
    JMP PARSE_CMD_DONE

PARSE_CMD_ERROR:
    ; Display error message for invalid command
    LDA #$01                    ; Set error flag
    STA MON_ERROR_FLAG
    JSR PRINT_ERROR_MSG         ; Print error message
    JMP PARSE_CMD_DONE

PARSE_CMD_VALUE_ERROR:
    ; Display value error message for invalid hex values
    LDA #$01                    ; Set error flag
    STA MON_ERROR_FLAG
    JSR PRINT_VALUE_ERROR       ; Print value error message
    JMP PARSE_CMD_DONE

PARSE_CMD_RANGE_ERROR:
    ; Display range error message for invalid address ranges
    LDA #$01                    ; Set error flag
    STA MON_ERROR_FLAG
    JSR PRINT_RANGE_ERROR       ; Print range error message
    ; Fall through to done

PARSE_CMD_DONE:
    RTS

; Parse colon command syntax (e.g., "W:8000", "R:8000-8010")
; Input: Command in MON_CMDBUF starting with command character
; Output: Address(es) parsed into MON_CURRADDR_HI/LO and optionally MON_ENDADDR_HI/LO
;         Carry clear if successful, set if error
; Modifies: A, X, Y
PARSE_COLON_COMMAND:

    ; Clear end address to ensure single address commands work correctly
        LDA #$00
        STA MON_ENDADDR_LO
        STA MON_ENDADDR_HI

    ; Check for colon after command character
    LDX #$01                    ; Position after command character
    LDA MON_CMDBUF,X            ; Load second character
    CMP #ASCII_COLON            ; Is it a colon?
    BNE PARSE_COLON_ERROR       ; If not, error

    ; Move past the colon
    INX                         ; X now points to first character after colon

    ; Check if there's anything after the colon
    CPX MON_CMDLEN              ; Are we at end of command?
    BEQ PARSE_COLON_NO_ADDR     ; If so, use current address

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

PARSE_COLON_NO_ADDR:
    ; Current address is already in MON_CURRADDR_HI/LO
    CLC                         ; Clear carry for success
    RTS

PARSE_COLON_SUCCESS:
    CLC                         ; Clear carry for success
    RTS

PARSE_COLON_ERROR:
    SEC                         ; Set carry for error
    RTS

; Parse fill value parameter (expects comma followed by 2-digit hex byte)
; Input: Command buffer positioned after address range
; Output: Fill value in MON_FILL_VALUE, Carry clear if success
; Modifies: A, X, Y
PARSE_FILL_VALUE:
    ; X register already contains correct position from PARSE_COLON_COMMAND
    ; Skip any spaces
PARSE_FILL_SKIP_SPACES:
    CPX MON_CMDLEN              ; Check if we're at end of command
    BCS PARSE_FILL_ERROR        ; If at or past end, error
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_FILL_CHECK_COMMA  ; If not space, check for comma
    INX                         ; Skip space
    JMP PARSE_FILL_SKIP_SPACES

PARSE_FILL_CHECK_COMMA:
    CMP #$2C                    ; Is it a comma?
    BNE PARSE_FILL_ERROR        ; If not comma, error
    INX                         ; Skip comma

    ; Skip any spaces after comma
PARSE_FILL_SKIP_SPACES2:
    CPX MON_CMDLEN              ; Check if we're at end of command
    BCS PARSE_FILL_ERROR        ; If at or past end, error
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_FILL_GET_VALUE    ; If not space, get value
    INX                         ; Skip space
    JMP PARSE_FILL_SKIP_SPACES2

PARSE_FILL_GET_VALUE:
    ; Parse 2-digit hex value
    JSR HEX_PAIR_TO_BYTE        ; Convert 2 hex digits to byte
    BCS PARSE_FILL_ERROR        ; If error, exit
    STA MON_FILL_VALUE          ; Store fill value
    CLC                         ; Clear carry for success
    RTS

PARSE_FILL_ERROR:
    SEC                         ; Set carry for error
    RTS

; Parse move/copy parameters (expects comma, destination address, and mode)
; Input: Command buffer positioned after address range
; Output: Destination address in MON_DEST_ADDR_HI/LO, mode in MON_COPY_MODE, Carry clear if success
; Modifies: A, X, Y
PARSE_MOVE_PARAMS:
    ; X register already contains correct position from PARSE_COLON_COMMAND
    ; Skip any spaces
PARSE_MOVE_SKIP_SPACES:
    CPX MON_CMDLEN              ; Check if we're at end of command
    BCS PARSE_MOVE_ERROR_JMP    ; If at or past end, error
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_MOVE_CHECK_COMMA  ; If not space, check for comma
    INX                         ; Skip space
    JMP PARSE_MOVE_SKIP_SPACES

PARSE_MOVE_CHECK_COMMA:
    CMP #$2C                    ; Is it a comma?
    BNE PARSE_MOVE_ERROR_JMP    ; If not comma, error
    INX                         ; Skip comma

    ; Skip any spaces after comma
PARSE_MOVE_SKIP_SPACES2:
    CPX MON_CMDLEN              ; Check if we're at end of command
    BCS PARSE_MOVE_ERROR_JMP    ; If at or past end, error
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_MOVE_GET_DEST     ; If not space, get destination
    INX                         ; Skip space
    JMP PARSE_MOVE_SKIP_SPACES2

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

    ; Skip any spaces before comma
PARSE_MOVE_SKIP_SPACES3:
    CPX MON_CMDLEN              ; Check if we're at end of command
    BCS PARSE_MOVE_ERROR_JMP    ; If at or past end, error
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_MOVE_CHECK_COMMA2 ; If not space, check for comma
    INX                         ; Skip space
    JMP PARSE_MOVE_SKIP_SPACES3

PARSE_MOVE_CHECK_COMMA2:
    CMP #$2C                    ; Is it a comma?
    BNE PARSE_MOVE_ERROR_JMP    ; If not comma, error
    INX                         ; Skip comma

    ; Skip any spaces after second comma
PARSE_MOVE_SKIP_SPACES4:
    CPX MON_CMDLEN              ; Check if we're at end of command
    BCS PARSE_MOVE_ERROR_JMP    ; If at or past end, error
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_MOVE_GET_MODE     ; If not space, get mode
    INX                         ; Skip space
    JMP PARSE_MOVE_SKIP_SPACES4

PARSE_MOVE_GET_MODE:
    ; Parse 1-digit mode (0=copy, 1=move)
    LDA MON_CMDBUF,X            ; Load mode character
    CMP #'0'                    ; Is it '0'?
    BEQ PARSE_MOVE_MODE_COPY    ; Yes, copy mode
    CMP #'1'                    ; Is it '1'?
    BEQ PARSE_MOVE_MODE_MOVE    ; Yes, move mode
    JMP PARSE_MOVE_ERROR_JMP    ; Neither, error

PARSE_MOVE_MODE_COPY:
    LDA #$00                    ; Copy mode
    STA MON_COPY_MODE
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
    ; X register already contains correct position from PARSE_COLON_COMMAND
    ; Skip any spaces
PARSE_SEARCH_SKIP_SPACES:
    CPX MON_CMDLEN              ; Check if we're at end of command
    BCS PARSE_SEARCH_ERROR      ; If at or past end, error
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_SEARCH_CHECK_COMMA ; If not space, check for comma
    INX                         ; Skip space
    JMP PARSE_SEARCH_SKIP_SPACES

PARSE_SEARCH_CHECK_COMMA:
    CMP #$2C                    ; Is it a comma?
    BNE PARSE_SEARCH_ERROR      ; If not comma, error
    INX                         ; Skip comma

    ; Skip any spaces after comma
PARSE_SEARCH_SKIP_SPACES2:
    CPX MON_CMDLEN              ; Check if we're at end of command
    BCS PARSE_SEARCH_ERROR      ; If at or past end, error
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_SEARCH_GET_PATTERN ; If not space, get pattern
    INX                         ; Skip space
    JMP PARSE_SEARCH_SKIP_SPACES2

PARSE_SEARCH_GET_PATTERN:
    ; Parse hex pattern bytes (1-16 bytes)
    LDA #$00                    ; Initialize pattern length
    STA MON_PATTERN_LEN
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

    ; Skip any spaces before next hex pair
PARSE_SEARCH_SKIP_SPACES3:
    CPX MON_CMDLEN              ; At end of command?
    BCS PARSE_SEARCH_PATTERN_DONE ; If so, we're done
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_SEARCH_PATTERN_LOOP ; If not space, try next hex pair
    INX                         ; Skip space
    JMP PARSE_SEARCH_SKIP_SPACES3

PARSE_SEARCH_PATTERN_DONE:
    ; Check if we have at least one pattern byte
    LDA MON_PATTERN_LEN
    BEQ PARSE_SEARCH_ERROR      ; If no pattern bytes, error
    CLC                         ; Clear carry for success
    RTS

PARSE_SEARCH_ERROR:
    SEC                         ; Set carry for error
    RTS

; Parse filename parameter (expects comma followed by filename)
; Input: Command buffer positioned after address
; Output: Filename copied to FILE_NAME_BUF, Carry clear if success
; Modifies: A, X, Y
PARSE_FILENAME:
    ; X register already contains correct position from PARSE_COLON_COMMAND
    ; Skip any spaces
PARSE_FILENAME_SKIP_SPACES:
    CPX MON_CMDLEN              ; Check if we're at end of command
    BCS PARSE_FILENAME_ERROR    ; If at or past end, error
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_FILENAME_CHECK_COMMA ; If not space, check for comma
    INX                         ; Skip space
    JMP PARSE_FILENAME_SKIP_SPACES

PARSE_FILENAME_CHECK_COMMA:
    CMP #$2C                    ; Is it a comma?
    BNE PARSE_FILENAME_ERROR    ; If not comma, error
    INX                         ; Skip comma

    ; Skip any spaces after comma
PARSE_FILENAME_SKIP_SPACES2:
    CPX MON_CMDLEN              ; Check if we're at end of command
    BCS PARSE_FILENAME_ERROR    ; If at or past end, error
    LDA MON_CMDBUF,X            ; Load character
    CMP #$20                    ; Is it a space?
    BNE PARSE_FILENAME_GET_NAME ; If not space, get filename
    INX                         ; Skip space
    JMP PARSE_FILENAME_SKIP_SPACES2

PARSE_FILENAME_GET_NAME:
    ; Copy filename from command buffer to file interface (max 12 chars)
    LDY #$00                    ; Destination index (file name buffer)

PARSE_FILENAME_COPY_LOOP:
    CPX MON_CMDLEN              ; Check if we've reached end of command
    BCS PARSE_FILENAME_COPIED   ; If so, we're done
    CPY #$0C                    ; Check if file buffer is full (12 bytes max)
    BCS PARSE_FILENAME_COPIED   ; If so, we're done

    LDA MON_CMDBUF,X            ; Load character from command buffer
    ; Check for valid filename characters (no spaces allowed)
    CMP #$20                    ; Is it a space?
    BEQ PARSE_FILENAME_COPIED   ; If space, end of filename

    STA FILE_NAME_BUF,Y         ; Store in file name buffer
    INX                         ; Move to next source character
    INY                         ; Move to next destination character
    JMP PARSE_FILENAME_COPY_LOOP ; Continue copying

PARSE_FILENAME_COPIED:
    ; Check if we have at least one character
    CPY #$00                    ; Check if we copied any characters
    BEQ PARSE_FILENAME_ERROR    ; If no characters, error

    ; Null-terminate filename if there's room
    CPY #$0C                    ; Check if buffer is full
    BCS PARSE_FILENAME_FULL     ; If full, skip null termination
    LDA #$00                    ; Load null terminator
    STA FILE_NAME_BUF,Y         ; Store null terminator

PARSE_FILENAME_FULL:
    CLC                         ; Clear carry for success
    RTS

PARSE_FILENAME_ERROR:
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

; Print value error message for invalid hex values
; Modifies: A, X, Y
PRINT_VALUE_ERROR:
    ; Print value error message
    LDA #<MSG_VALUE_ERROR       ; Load low byte of message address
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_VALUE_ERROR       ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message
    JSR PRINT_NEWLINE
    RTS

; Print range error message for invalid address ranges
; Modifies: A, X, Y
PRINT_RANGE_ERROR:
    ; Print range error message
    LDA #<MSG_RANGE_ERROR       ; Load low byte of message address
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_RANGE_ERROR       ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message
    JSR PRINT_NEWLINE
    RTS

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

RANGE_VALID:
    CLC                         ; Clear carry for valid range
    RTS

; ================================================================
; MONITOR COMMAND IMPLEMENTATIONS
; ================================================================



; Clear screen command (C:)
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

; Stack dump command (T:) - Display stack memory ($0100-$01FF)
; Modifies: A, X, Y
CMD_DUMP_STACK:

    LDA #0
    STA CMD_LINE_COUNT          ; Reset command line counter
    STA PAGE_ABORT_FLAG         ; Reset abort flag

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
    LDA #0
    STA CMD_LINE_COUNT          ; Reset command line counter
    STA PAGE_ABORT_FLAG         ; Reset abort flag

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
    CPX #24                 ; 12 messages * 2 bytes each
    BNE HELP_LOOP
    RTS

; Exit mode command (ESC) - Return to command mode
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

; Read mode command (R:xxxx or R:xxxx-yyyy) - Display memory (one-shot)
; Input: Address(es) in MON_CURRADDR_HI/LO and optionally MON_ENDADDR_HI/LO
; Modifies: A, X, Y
CMD_READ_MODE:

    LDA #0
    STA CMD_LINE_COUNT          ; Reset command line counter
    STA PAGE_ABORT_FLAG         ; Reset abort flag

    ; Check if we have end address (range operation)
    LDA MON_ENDADDR_HI          ; Check if end address is set
    ORA MON_ENDADDR_LO          ; (non-zero means range)
    BEQ CMD_READ_SINGLE         ; If zero, single address

    ; Range operation - validate range first
    JSR VALIDATE_ADDRESS_RANGE  ; Use common range validation
    BCS CMD_READ_RANGE_ERROR    ; If invalid range, show error

CMD_READ_RANGE_VALID:
    ; Display memory range with 8-byte formatting
    JSR READ_MEMORY_RANGE       ; Display the range with read-specific formatting
    RTS

CMD_READ_RANGE_ERROR:
    JSR PRINT_RANGE_ERROR       ; Print ?RANGE message
    RTS

CMD_READ_SINGLE:
    ; Single address - show single byte in format "xxxx: bb"
    JSR SHOW_READ_ADDRESS       ; Use read-specific display routine
    RTS

; Go/Run mode command (G:xxxx) - Execute program at specified address
; Input: Address in MON_CURRADDR_HI/LO
; Modifies: A
CMD_GO_MODE:
    ; Call user program (this pushes return address)
    JSR RUN_USER_PROGRAM

    ; User program returns here
    RTS

RUN_USER_PROGRAM:
    JMP (MON_CURRADDR_LO)       ; Jump to user program

; Load command (L:xxxx,filename) - Load file at specified address (one-shot)
; Input: Address in MON_CURRADDR_HI/LO, filename in FILE_NAME_BUF
; Modifies: A, X, Y
CMD_LOAD_MODE:
    ; Set up file I/O communication with C++ host
    ; Store target address in file I/O interface
    LDA MON_CURRADDR_LO         ; Get load address low byte
    STA FILE_ADDR_LO            ; Store in file interface
    LDA MON_CURRADDR_HI         ; Get load address high byte
    STA FILE_ADDR_HI            ; Store in file interface

    ; Filename is already in FILE_NAME_BUF from PARSE_FILENAME

    ; Issue load command to C++ host
    LDA #FILE_LOAD_CMD          ; Load command code
    STA FILE_COMMAND            ; Store in command register

    ; Wait for operation to complete
LOAD_WAIT_COMPLETE:
    LDA FILE_STATUS             ; Read status register
    CMP #FILE_IN_PROGRESS       ; Still in progress?
    BEQ LOAD_WAIT_COMPLETE      ; If so, keep waiting

    ; Check if operation was successful
    CMP #FILE_SUCCESS           ; Was it successful?
    BEQ LOAD_CMD_SUCCESS        ; If so, show success message

    ; Operation failed - show error
    JSR PRINT_ERROR_MSG         ; Print error message
    RTS

LOAD_CMD_SUCCESS:
    ; Show success message
    LDA #<MSG_SUCCESS           ; Use OK message
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_SUCCESS           ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message
    JSR PRINT_NEWLINE           ; Add newline
    RTS

; Save mode command (S:xxxx-yyyy,filename) - Save memory range to file
; Input: Start address in MON_STARTADDR_HI/LO, end address in MON_ENDADDR_HI/LO
;        Filename already parsed in MON_PARSE_FILENAME buffer
; Modifies: A, X, Y
CMD_SAVE_MODE:
    ; Check if we have a valid address range (end address must be non-zero)
    LDA MON_ENDADDR_LO          ; Check end address low byte
    ORA MON_ENDADDR_HI          ; OR with high byte
    BEQ SAVE_RANGE_ERROR        ; If zero, no range specified

    ; Validate that start <= end
    JSR VALIDATE_ADDRESS_RANGE  ; Use common range validation
    BCC SAVE_MODE_VALID_RANGE   ; If valid range, continue

SAVE_RANGE_ERROR:
    JSR PRINT_RANGE_ERROR       ; Print range error message
    RTS

SAVE_MODE_VALID_RANGE:
    ; Set up file I/O communication with C++ host
    ; Store start address in file I/O interface
    LDA MON_STARTADDR_LO        ; Get start address low byte
    STA FILE_ADDR_LO            ; Store in file interface
    LDA MON_STARTADDR_HI        ; Get start address high byte
    STA FILE_ADDR_HI            ; Store in file interface

    ; Store end address in additional PIA registers
    LDA MON_ENDADDR_LO          ; Get end address low byte
    STA FILE_END_ADDR_LO        ; Store in file end address low
    LDA MON_ENDADDR_HI          ; Get end address high byte
    STA FILE_END_ADDR_HI        ; Store in file end address high

    ; Filename is already in FILE_NAME_BUF from PARSE_FILENAME

    ; Issue save command to C++ host
    LDA #FILE_SAVE_CMD          ; Save command code
    STA FILE_COMMAND            ; Store in command register

    ; Wait for operation to complete
SAVE_WAIT_COMPLETE:
    LDA FILE_STATUS             ; Read status register
    CMP #FILE_IN_PROGRESS       ; Still in progress?
    BEQ SAVE_WAIT_COMPLETE      ; If so, keep waiting

    ; Check if operation was successful
    CMP #FILE_SUCCESS           ; Was it successful?
    BEQ SAVE_CMD_SUCCESS        ; If so, show success message

    ; Operation failed - show error
    JSR PRINT_ERROR_MSG         ; Print error message
    RTS

SAVE_CMD_SUCCESS:
    ; Show success message
    LDA #<MSG_SUCCESS           ; Use OK message
    STA MON_MSG_PTR_LO          ; Store in message pointer
    LDA #>MSG_SUCCESS           ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message
    JSR PRINT_NEWLINE           ; Add newline
    RTS


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
    ; Check if user aborted
    LDA PAGE_ABORT_FLAG
    BEQ CONTINUE_DUMP
    JMP DUMP_ABORTED
CONTINUE_DUMP:

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
    ; Load byte and convert to hex inline
    LDA (MON_CURRADDR_LO),Y     ; Load byte from memory (Y=0)

    ; Print high nibble
    LSR A
    LSR A
    LSR A
    LSR A
    TAX
    LDA HEX_LOOKUP_TABLE,X
    JSR PRINT_CHAR

    ; Print low nibble
    LDA (MON_CURRADDR_LO),Y
    AND #$0F
    TAX
    LDA HEX_LOOKUP_TABLE,X
    JSR PRINT_CHAR

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
    JSR PRINT_NEWLINE
    JMP DUMP_RANGE_LOOP

DUMP_ABORTED:
    ; Just fall through to done

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





; Fill memory command - Fill memory range with specified byte value
; Input: MON_CURRADDR_HI/LO (start), MON_ENDADDR_HI/LO (end), MON_FILL_VALUE (fill byte)
; Modifies: A, X, Y
CMD_FILL_MODE:
    ; Validate address range (start <= end)
    JSR VALIDATE_ADDRESS_RANGE  ; Use common range validation
    BCS FILL_RANGE_ERROR        ; If invalid range, show error
    JMP FILL_RANGE_VALID        ; Continue with valid range

FILL_RANGE_ERROR:
    JSR PRINT_RANGE_ERROR       ; Print range error message
    RTS

FILL_RANGE_VALID:
    ; Perform fill operation
    LDY #$00                    ; Initialize Y index
    LDA MON_FILL_VALUE          ; Load fill value

FILL_LOOP:
    ; Fill byte at current address
    STA (MON_CURRADDR_LO),Y     ; Store fill value at current address

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
    JMP FILL_LOOP              ; Continue filling

FILL_DONE:
    ; Print success message
    LDA #<MSG_SUCCESS
    STA MON_MSG_PTR_LO
    LDA #>MSG_SUCCESS
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE
    JSR PRINT_NEWLINE
    RTS

; Move/Copy memory command - Copy or move memory block with optional source clearing
; Input: MON_CURRADDR_HI/LO (start), MON_ENDADDR_HI/LO (end), MON_DEST_ADDR_HI/LO (destination), MON_COPY_MODE (0=copy, 1=move)
; Modifies: A, X, Y
CMD_MOVE_MODE:
    ; Validate address range (start <= end)
    JSR VALIDATE_ADDRESS_RANGE  ; Use common range validation
    BCS MOVE_RANGE_ERROR        ; If invalid range, show error

MOVE_RANGE_ERROR:
    JSR PRINT_RANGE_ERROR       ; Print range error message
    RTS

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
    BCC MOVE_BACKWARD           ; dest <= end, overlaps, need backward copy

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

    ; Add offset to destination to get destination end
    STX MON_HEX_TEMP            ; Use temp storage for offset_lo
    CLC
    LDA MON_DEST_ADDR_LO
    ADC MON_HEX_TEMP            ; dest_end_lo = dest_lo + offset_lo
    STA MON_DEST_ADDR_LO
    LDA MON_DEST_ADDR_HI
    ADC $00                     ; dest_end_hi = dest_hi + offset_hi + carry
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

    ; Move mode (1): clear source memory
    ; Restore original source range
    LDA MON_STARTADDR_LO
    STA MON_CURRADDR_LO
    LDA MON_STARTADDR_HI
    STA MON_CURRADDR_HI

    LDY #$00                    ; Initialize Y index
    LDA #$00                    ; Clear value

MOVE_CLEAR_LOOP:
    ; Clear byte at current source address
    STA (MON_CURRADDR_LO),Y     ; Store zero at source address

    ; Check if we've reached end address
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BCC MOVE_CLEAR_CONTINUE     ; Current < end (high), continue
    BNE MOVE_CLEAR_DONE         ; Current > end (high), done
    LDA MON_CURRADDR_LO
    CMP MON_ENDADDR_LO
    BCS MOVE_CLEAR_DONE         ; Current >= end (low), done

MOVE_CLEAR_CONTINUE:
    ; Increment current address
    INC MON_CURRADDR_LO
    BNE MOVE_CLEAR_NO_CARRY
    INC MON_CURRADDR_HI

MOVE_CLEAR_NO_CARRY:
    LDA #$00                    ; Reload clear value
    JMP MOVE_CLEAR_LOOP         ; Continue clearing

MOVE_CLEAR_DONE:
    JMP MOVE_SUCCESS            ; Show success message

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

    ; Print success message based on mode
    LDA MON_COPY_MODE
    BEQ MOVE_SHOW_COPY_MSG      ; Copy mode

    ; Move mode - different message could be shown here
    LDA #<MSG_SUCCESS
    STA MON_MSG_PTR_LO
    LDA #>MSG_SUCCESS
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE
    JSR PRINT_NEWLINE
    RTS

MOVE_SHOW_COPY_MSG:
    ; Copy mode
    LDA #<MSG_SUCCESS
    STA MON_MSG_PTR_LO
    LDA #>MSG_SUCCESS
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE
    JSR PRINT_NEWLINE
    RTS

; Search memory command - Search for hex pattern in memory range
; Input: MON_CURRADDR_HI/LO (start), MON_ENDADDR_HI/LO (end), pattern in MON_SEARCH_PATTERN, length in MON_PATTERN_LEN
; Modifies: A, X, Y
CMD_SEARCH_MODE:
    ; Save original current address to preserve it (search should not modify current address)
    LDA MON_CURRADDR_LO
    PHA
    LDA MON_CURRADDR_HI
    PHA

    ; Initialize line counter and abort flag for paging
    LDA #0
    STA CMD_LINE_COUNT          ; Reset command line counter
    STA PAGE_ABORT_FLAG         ; Reset abort flag

    ; Validate address range (start <= end)
    JSR VALIDATE_ADDRESS_RANGE  ; Use common range validation
    BCS SEARCH_RANGE_ERROR      ; If invalid range, show error

SEARCH_RANGE_ERROR:
    ; Restore original current address before error exit
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    JSR PRINT_RANGE_ERROR       ; Print range error message
    RTS

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
    JSR PRINT_NEWLINE           ; Add newline

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
    JSR PRINT_NEWLINE
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

    ; Save the command only if we're in command mode and it completed without error
    LDA MON_MODE                ; Check current mode
    BNE MONITOR_SKIP_SAVE       ; If not in command mode, don't save
    LDA MON_ERROR_FLAG          ; Check if command had error
    BNE MONITOR_SKIP_SAVE       ; If error, don't save
    JSR SAVE_COMMAND            ; Save command for dot recall

MONITOR_SKIP_SAVE:
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
; Maps B,C,G,H,L,R,T,W,X,Z to indices 0-10
CMD_JUMP_COMPACT_LO:
    .BYTE <PARSE_CMD_GO_CHECK   ; 0 - 'G'
    .BYTE <PARSE_CMD_HELP       ; 1 - 'H'
    .BYTE <PARSE_CMD_CLEAR      ; 2 - 'C'
    .BYTE <PARSE_CMD_LOAD_CHECK ; 3 - 'L'
    .BYTE <PARSE_CMD_READ_CHECK ; 4 - 'R'
    .BYTE <PARSE_CMD_SAVE_CHECK ; 5 - 'S'
    .BYTE <PARSE_CMD_STACK      ; 6 - 'T'
    .BYTE <PARSE_CMD_WRITE_CHECK; 7 - 'W'
    .BYTE <PARSE_CMD_EXIT       ; 8 - 'X'
    .BYTE <PARSE_CMD_ZERO       ; 9 - 'Z'
    .BYTE <PARSE_CMD_FILL_CHECK ; 10 - 'F'
    .BYTE <PARSE_CMD_MOVE_CHECK ; 11 - 'M'
    .BYTE <PARSE_CMD_SEARCH_CHECK; 12 - 'X'

CMD_JUMP_COMPACT_HI:
    .BYTE >PARSE_CMD_GO_CHECK   ; 0 - 'G'
    .BYTE >PARSE_CMD_HELP       ; 1 - 'H'
    .BYTE >PARSE_CMD_CLEAR      ; 2 - 'C'
    .BYTE >PARSE_CMD_LOAD_CHECK ; 3 - 'L'
    .BYTE >PARSE_CMD_READ_CHECK ; 4 - 'R'
    .BYTE >PARSE_CMD_SAVE_CHECK ; 5 - 'S'
    .BYTE >PARSE_CMD_STACK      ; 6 - 'T'
    .BYTE >PARSE_CMD_WRITE_CHECK; 7 - 'W'
    .BYTE >PARSE_CMD_EXIT       ; 8 - 'ESC' (keep existing)
    .BYTE >PARSE_CMD_ZERO       ; 9 - 'Z'
    .BYTE >PARSE_CMD_FILL_CHECK ; 10 - 'F'
    .BYTE >PARSE_CMD_MOVE_CHECK ; 11 - 'M'
    .BYTE >PARSE_CMD_SEARCH_CHECK; 12 - 'X'

; Index mapping table - maps command character to table index
; For characters C-Z, subtract 'C' ($43) to get offset into this table
CMD_INDEX_MAP:
    .BYTE 2     ; C -> 2 (Clear)
    .BYTE $FF   ; D -> invalid
    .BYTE $FF   ; E -> invalid
    .BYTE 10    ; F -> 10 (Fill)
    .BYTE 0     ; G -> 0 (Run)
    .BYTE 1     ; H -> 1 (Help)
    .BYTE $FF   ; I -> invalid
    .BYTE $FF   ; J -> invalid
    .BYTE $FF   ; K -> invalid (was 2)
    .BYTE 3     ; L -> 3 (Load)
    .BYTE 11    ; M -> 11 (Move/Copy)
    .BYTE $FF   ; N -> invalid
    .BYTE $FF   ; O -> invalid
    .BYTE $FF   ; P -> invalid
    .BYTE $FF   ; Q -> invalid
    .BYTE 4     ; R -> 4 (Read Memory)
    .BYTE 5     ; S -> 5 (Save)
    .BYTE 6     ; T -> 6 (Print Stack)
    .BYTE $FF   ; U -> invalid
    .BYTE $FF   ; V -> invalid
    .BYTE 7     ; W -> 7 (Write to Memory)
    .BYTE 12    ; X -> 12 (Search)
    .BYTE $FF   ; Y -> invalid
    .BYTE 9     ; Z -> 9 (Print Zero Page)

; ================================================================
; MODE PREFIX TABLE - Characters for prompt prefixes
; ================================================================
; Indexed by MON_MODE value: CMD(0), WRITE(1), READ(2), RUN(3), LOAD(4), SAVE(5)
MODE_PREFIX_TABLE:
    .BYTE 0         ; MON_MODE_CMD = 0: No prefix (just address>)
    .BYTE $57       ; MON_MODE_WRITE = 1: 'W' (W:address>)

; ================================================================
; HELP MESSAGE TABLE - Addresses of help messages for display
; ================================================================
; Word addresses of help messages in display order
HELP_MSG_TABLE:
    .WORD MSG_HELP_CLEAR
    .WORD MSG_HELP_GO
    .WORD MSG_HELP_LOAD
    .WORD MSG_HELP_READ
    .WORD MSG_HELP_SAVE
    .WORD MSG_HELP_STACK
    .WORD MSG_HELP_WRITE
    .WORD MSG_HELP_ZERO
    .WORD MSG_HELP_EXIT
    .WORD MSG_HELP_FILL
    .WORD MSG_HELP_MOVE
    .WORD MSG_HELP_SEARCH

HELP_MSG_COUNT = 12              ; Number of help messages

; ================================================================
; MESSAGE DATA SECTION - Null-terminated strings for monitor
; ================================================================
MSG_HELP_HEADER:     .BYTE "MONITOR COMMANDS", 0
MSG_HELP_CLEAR:      .BYTE "C:     CLEAR SCREEN", 0
MSG_HELP_GO:         .BYTE "G:XXXX RUN", 0
MSG_HELP_LOAD:       .BYTE "L:XXXX,FILENAME LOAD FILE", 0
MSG_HELP_READ:       .BYTE "R:XXXX(-YYYY) READ FROM MEMORY", 0
MSG_HELP_SAVE:       .BYTE "S:XXXX-YYYY   SAVE MEMORY RANGE", 0
MSG_HELP_STACK:      .BYTE "T:     PRINT STACK", 0
MSG_HELP_WRITE:      .BYTE "W:XXXX WRITE TO MEMORY", 0
MSG_HELP_ZERO:       .BYTE "Z:     PRINT ZERO PAGE", 0
MSG_HELP_FILL:       .BYTE "F:XXXX-YYYY,ZZ FILL MEMORY", 0
MSG_HELP_MOVE:       .BYTE "M:XXXX-YYYY,ZZZZ,B MOVE/COPY", 0
MSG_HELP_SEARCH:     .BYTE "X:XXXX-YYYY,PATTERN SEARCH MEMORY", 0
MSG_HELP_EXIT:       .BYTE "ESC    EXIT CURRENT MODE", 0
MSG_SYNTAX_ERROR:    .BYTE "ERROR?", 0
MSG_RANGE_ERROR:     .BYTE "RANGE?", 0
MSG_VALUE_ERROR:     .BYTE "VALUE?", 0
MSG_SUCCESS:         .BYTE "OK", 0
MSG_WELCOME:         .BYTE "-=MFC 6502 OPERATIONAL=-", 0
MSG_PAGE_PROMPT:     .BYTE "--MORE-- (ENTER)", 0

; ================================================================
; KERNEL API JUMP TABLE
; ================================================================
.segment "JUMPS"
.org $FF00

; These are indirect jumps to the actual routines
K_PRINT_CHAR:    JMP PRINT_CHAR         ; $FF00
K_PRINT_MESSAGE: JMP PRINT_MESSAGE      ; $FF03
K_PRINT_NEWLINE: JMP PRINT_NEWLINE      ; $FF06
K_GET_KEYSTROKE: JMP GET_KEYSTROKE      ; $FF09
K_CLEAR_SCREEN:  JMP CLEAR_SCREEN       ; $FF0C
K_GET_RAND_NUM:  JMP GET_RANDOM_NUMBER  ; $FF0F
; ================================================================
; RESET VECTORS
; ================================================================
.segment "VECS"
.org $FFFA

    .WORD NMI_HANDLER           ; NMI vector ($FFFA-$FFFB)
    .WORD RESET                 ; Reset vector ($FFFC-$FFFD)
    .WORD IRQ_HANDLER           ; IRQ vector ($FFFE-$FFFF)