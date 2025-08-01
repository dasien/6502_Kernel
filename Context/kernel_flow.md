# 6502 Kernel Logic Flow Analysis

This document describes the complete logic flow of the kernel.asm file, showing all loops, branches, wait states, and decision points.

## System Boot Flow

### 1. RESET (Entry Point - $F000)
**Path**: Hardware Reset Vector → RESET
- **Sequential execution**: Processor initialization
  - `CLD` - Clear decimal mode
  - `SEI` - Disable interrupts  
  - `LDX #STACK_TOP; TXS` - Initialize stack pointer to $FF
  - `LDA #$3F; STA PROC_DDR` - Configure processor port direction
  - `LDA #$37; STA PROC_PORT` - Set memory banking configuration
- **Flow continues to**: Zero Page Initialization

### 2. Zero Page Clear Loop
**Path**: RESET → ZP_CLEAR_LOOP
- **LOOP**: `ZP_CLEAR_LOOP`
  - **Entry condition**: X = $06 (start after processor port registers)
  - **Loop body**: 
    - `LDA #$00; STA $00,X` - Clear zero page location
    - `INX` - Increment address
  - **Exit condition**: `BNE ZP_CLEAR_LOOP` (when X wraps to $00)
  - **Iterations**: 250 iterations (clears $06-$FF)
- **Flow continues to**: Screen Memory Initialization

### 3. Screen Clear Loop  
**Path**: ZP_CLEAR_LOOP → SCREEN_CLEAR_LOOP
- **LOOP**: `SCREEN_CLEAR_LOOP`
  - **Entry condition**: X = $00, A = $20 (space character)
  - **Loop body**: 
    - Clear 4 screen memory pages simultaneously:
      - `STA $0400,X` - Screen page 1
      - `STA $0500,X` - Screen page 2  
      - `STA $0600,X` - Screen page 3
      - `STA $0700,X` - Screen page 4
    - `INX` - Increment position
  - **Exit condition**: `BNE SCREEN_CLEAR_LOOP` (when X wraps to $00)
  - **Iterations**: 256 iterations (clears 1024 bytes total)
- **Flow continues to**: Monitor Variable Initialization

### 4. Monitor Buffer Clear Loop
**Path**: Screen Clear → MON_CLEAR_CMDBUF
- **LOOP**: `MON_CLEAR_CMDBUF`
  - **Entry condition**: X = $00, A = $00
  - **Loop body**:
    - `STA MON_CMDBUF,X` - Clear buffer location
    - `INX` - Increment position
  - **Exit condition**: `CPX #MON_CMDBUF_LEN; BNE MON_CLEAR_CMDBUF`
  - **Iterations**: 80 iterations (clears command buffer)
- **Flow continues to**: System Ready

### 5. System Startup Complete
**Path**: Buffer Clear → PRINT_WELCOME → MONITOR_MAIN
- **Sequential execution**:
  - `CLI` - Enable interrupts
  - `JSR PRINT_WELCOME` - Display boot message
  - `JMP MONITOR_MAIN` - Enter main program loop

---

## Main Program Flow

### 6. Monitor Main Loop (Infinite)
**Path**: Startup → MONITOR_MAIN → MONITOR_LOOP
- **INFINITE LOOP**: `MONITOR_LOOP`
  - **Action**: `JSR PRINT_MONITOR_PROMPT` - Display prompt based on mode
  - **BLOCKING CALL**: `JSR READ_COMMAND_LINE` - Wait for user input
  - **Conditional check**: `LDA MON_CMDLEN; BEQ MONITOR_LOOP` - Empty command loops back
  - **Action**: `JSR PARSE_COMMAND` - Process the command
  - **Flow**: `JMP MONITOR_LOOP` - Always returns to start

---

## Input Handling Flow

### 7. Command Input Handler (Blocking)
**Path**: MONITOR_LOOP → READ_COMMAND_LINE → READ_CMD_LOOP
- **INFINITE LOOP**: `READ_CMD_LOOP` 
  - **BLOCKING CALL**: `JSR GET_KEYSTROKE` - Wait for keyboard input
  - **BRANCH CONDITIONS**:
    - `CMP #ASCII_CR; BEQ READ_CMD_DONE` - Enter pressed → exit loop
    - `CMP #ASCII_BACKSPACE; BEQ READ_CMD_BACKSPACE` - Handle backspace
    - `CMP #ASCII_DELETE; BEQ READ_CMD_BACKSPACE` - Handle delete  
    - `CMP #ASCII_ESC; BEQ READ_CMD_CANCEL` - Escape → clear and restart
    - `CPX #MON_CMDBUF_LEN-1; BCS READ_CMD_LOOP` - Buffer full → ignore char
    - `CMP #ASCII_SPACE; BCC READ_CMD_LOOP` - Non-printable → ignore
    - `CMP #$7F; BCS READ_CMD_LOOP` - Above tilde → ignore
  - **Normal path**: Add character to buffer, echo to screen, continue loop
  - **Exit conditions**: Enter key processing

#### 7a. Backspace Handling Branch
**Path**: READ_CMD_LOOP → READ_CMD_BACKSPACE
- **Condition check**: `CPX #$00; BEQ READ_CMD_LOOP` - Empty buffer → ignore
- **Actions**: Remove character, update length, echo backspace sequence
- **Flow**: `JMP READ_CMD_LOOP` - Return to input loop

#### 7b. Escape/Cancel Handling Branch  
**Path**: READ_CMD_LOOP → READ_CMD_CANCEL → READ_CMD_CLEAR_LOOP
- **LOOP**: `READ_CMD_CLEAR_LOOP`
  - **Purpose**: Clear entire command buffer
  - **Exit condition**: `CPX #MON_CMDBUF_LEN; BNE READ_CMD_CLEAR_LOOP`
- **Actions**: Print newline, reset variables
- **Flow**: `JMP READ_CMD_LOOP` - Start over

### 8. Keystroke Polling (Blocking)
**Path**: READ_CMD_LOOP → GET_KEYSTROKE → GET_KEYSTROKE_WAIT
- **INFINITE POLLING LOOP**: `GET_KEYSTROKE_WAIT`
  - **Hardware polling**: `LDA PIA_CONTROL` - Read PIA status
  - **WAIT CONDITION**: `AND #PIA_DATA_AVAIL; BEQ GET_KEYSTROKE_WAIT`
  - **Exit condition**: Data available bit set in PIA
  - **Result**: `LDA PIA_DATA` - Read character and return

---

## Command Parsing Flow

### 9. Command Parser Decision Tree
**Path**: MONITOR_LOOP → PARSE_COMMAND
- **Initial checks**:
  - `LDA MON_CMDLEN; BEQ PARSE_CMD_DONE` - Empty command → done
  - `LDA MON_CMDBUF,X` - Load first character
- **SINGLE CHARACTER COMMANDS** (immediate execution):
  - `CMP #CMD_KLEAR; BEQ PARSE_CMD_KLEAR` → Clear screen
  - `CMP #CMD_STACK; BEQ PARSE_CMD_STACK` → Stack dump  
  - `CMP #CMD_ZERO; BEQ PARSE_CMD_ZERO` → Zero page dump
  - `CMP #CMD_TARGET; BEQ PARSE_CMD_TARGET` → Show target address
  - `CMP #CMD_HELP; BEQ PARSE_CMD_HELP` → Show help
  - `CMP #CMD_EXIT; BEQ PARSE_CMD_EXIT` → Exit mode
- **COLON COMMANDS** (require address parsing):
  - `CMP #CMD_WRITE; BEQ PARSE_CMD_WRITE_CHECK` → Write mode
  - `CMP #CMD_READ; BEQ PARSE_CMD_READ_CHECK` → Read mode
  - `CMP #CMD_GO; BEQ PARSE_CMD_GO_CHECK` → Execute program
- **Unknown command**: `JMP PARSE_CMD_ERROR` → Show error

### 10. Colon Command Address Parsing
**Path**: PARSE_CMD_*_CHECK → PARSE_COLON_COMMAND
- **Syntax validation**:
  - `LDX #$01; LDA MON_CMDBUF,X` - Check second character
  - `CMP #ASCII_COLON; BNE PARSE_COLON_ERROR` - Must be colon
- **Address parsing**: `JSR HEX_QUAD_TO_ADDR` - Parse 4-hex-digit address
- **Range check** (for R: commands):
  - `CPX MON_CMDLEN; BEQ PARSE_COLON_SUCCESS` - End of command → single address
  - `LDA MON_CMDBUF,X; CMP #ASCII_DASH; BNE PARSE_COLON_SUCCESS` - No dash → single
  - **Range parsing**: Parse second address after dash
- **Result**: Address(es) stored in MON_CURRADDR_*, carry flag indicates success/error

---

## Interactive Mode Flows

### 11. Write Mode Flow
**Path**: CMD_WRITE_MODE → WRITE_MODE_LOOP → WRITE_MODE_INPUT

#### Main Write Loop (Interactive)
- **INFINITE LOOP**: `WRITE_MODE_INPUT`
  - **BLOCKING CALL**: `JSR READ_COMMAND_LINE` - Get hex input
  - **Exit checks**:
    - `LDA MON_CMDLEN; BEQ WRITE_MODE_DONE` - Empty → exit
    - Check for "X:" command → exit
  - **Flow to**: Hex parsing loop

#### Hex Parsing Loop
**Path**: WRITE_MODE_INPUT → WRITE_MODE_PARSE_LOOP
- **LOOP**: `WRITE_MODE_PARSE_LOOP`
  - **Termination check**: `CPX MON_CMDLEN; BCS WRITE_MODE_SHOW_RESULT`
  - **Space skipping**: `CMP #ASCII_SPACE; BNE WRITE_MODE_PARSE_BYTE`
  - **Hex parsing**: `JSR HEX_PAIR_TO_BYTE; BCS WRITE_MODE_ERROR`
  - **Memory write**: Store byte, increment address and count
  - **Continue**: `JMP WRITE_MODE_PARSE_LOOP`
- **Exit paths**: 
  - End of input → Show results
  - Parse error → Show error, continue input

### 12. Read Mode Flow  
**Path**: CMD_READ_MODE → READ_MODE_LOOP → READ_MODE_INPUT

#### Main Read Loop (Interactive)
- **INFINITE LOOP**: `READ_MODE_INPUT`
  - **BLOCKING CALL**: `JSR READ_COMMAND_LINE` - Get command
  - **Exit checks**:
    - `LDA MON_CMDLEN; BEQ READ_MODE_INPUT` - Empty → continue
    - Check for "X:" command → exit
  - **Command parsing**: Support R:, W:, G:, H, T commands in read mode
  - **Flow**: Process command and return to input loop

---

## Memory Display Flow

### 13. Memory Dump Range Display
**Path**: Various commands → DUMP_MEMORY_RANGE → DUMP_RANGE_LOOP

#### Memory Display Loop
- **LOOP**: `DUMP_RANGE_LOOP`
  - **Address display**: Print current address in hex
  - **Inner loop**: `DUMP_PRINT_BYTES` (up to 8 bytes per line)
    - **Address comparison**: Compare current vs end address
    - **BRANCH CONDITIONS**:
      - `BCC DUMP_PRINT_BYTE` - Current < end → print byte
      - `BNE DUMP_RANGE_DONE` - Current > end → done
      - `BEQ DUMP_PRINT_LAST_BYTE` - Current = end → last byte
    - **Byte printing**: Convert to hex, print with space
    - **Address increment**: Increment with carry handling
    - **Line limit**: `CPY #MON_BYTES_PER_LINE; BNE DUMP_PRINT_BYTES`
  - **Line completion**: Print newline, start next line
  - **Exit condition**: Address comparison indicates end reached

---

## Utility Function Flows

### 14. Hex Conversion Flow
**Path**: Various → HEX_CHAR_TO_NIBBLE

#### Character Validation Decision Tree
- **Digit check**: `CMP #ASCII_0; BCC HEX_CHAR_INVALID`
- **Digit range**: `CMP #ASCII_9+1; BCC HEX_CHAR_DIGIT` → Convert 0-9
- **Uppercase check**: `CMP #ASCII_A; BCC HEX_CHAR_INVALID`  
- **Uppercase range**: `CMP #ASCII_F+1; BCC HEX_CHAR_UPPER` → Convert A-F
- **Lowercase check**: `CMP #$61; BCC HEX_CHAR_INVALID`
- **Lowercase range**: `CMP #$67; BCS HEX_CHAR_INVALID` → Convert a-f
- **Error path**: `SEC; RTS` - Set carry flag for invalid character

### 15. Message Printing Flow
**Path**: Various → PRINT_MESSAGE → PRINT_MSG_LOOP

#### String Printing Loop  
- **LOOP**: `PRINT_MSG_LOOP`
  - **Character load**: `LDA (MON_MSG_PTR_LO),Y` - Indirect indexed load
  - **Null check**: `BEQ PRINT_MSG_DONE` - Exit on null terminator
  - **Print**: `JSR PRINT_CHAR` - Output character
  - **Increment**: `INY` - Move to next character
  - **Continue**: `BNE PRINT_MSG_LOOP` - Loop if Y hasn't wrapped
- **Limitation**: Strings must be < 256 characters

---

## Critical Wait States and Loops

### Summary of Blocking Operations:
1. **GET_KEYSTROKE_WAIT** - Hardware polling loop (infinite until key pressed)
2. **READ_CMD_LOOP** - Command input loop (infinite until Enter/Escape)
3. **MONITOR_LOOP** - Main program loop (infinite)
4. **WRITE_MODE_INPUT** - Write mode input loop (infinite until X:)
5. **READ_MODE_INPUT** - Read mode input loop (infinite until X:)

### Summary of Finite Loops:
1. **ZP_CLEAR_LOOP** - 250 iterations (zero page clear)
2. **SCREEN_CLEAR_LOOP** - 256 iterations (screen clear)  
3. **MON_CLEAR_CMDBUF** - 80 iterations (buffer clear)
4. **DUMP_PRINT_BYTES** - Up to 8 iterations per line (memory display)
5. **WRITE_MODE_PARSE_LOOP** - Variable iterations (hex input parsing)
6. **PRINT_MSG_LOOP** - Variable iterations (string printing)

### Program Termination Points:
- **CMD_GO_MODE**: `JMP (MON_CURRADDR_LO)` - Transfers control to user program
- **IRQ_HANDLER/NMI_HANDLER**: `RTI` - Return from interrupt
- **No normal exit**: System runs indefinitely in monitor loop

This flow analysis shows that the kernel is designed as a persistent monitor system with interactive command processing, where the main execution flow is an infinite loop waiting for user commands, with various sub-modes providing specialized interactive environments for memory examination and modification.