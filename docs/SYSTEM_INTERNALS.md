# MFC System Internals

Deep-dive developer reference for how MFC works inside — kernel execution
flow and call tree, the monitor command dispatch, the DOS/filesystem design,
the MFC BASIC label map, the host GUI, and external references. (User-facing
manuals are the `UPPERCASE.md` docs; the memory map / ABI / architecture
overview live in `ARCHITECTURE.md`.)

**Contents:** Part 1 Kernel flow · Part 2 Call tree · Part 3 Monitor command
infrastructure · Part 4 DOS/filesystem design · Part 5 BASIC label glossary ·
Part 6 Host GUI · Part 7 External references

---

## Part 1 — Kernel execution flow


This document describes the complete logic flow of the kernel.asm file, showing all loops, branches, wait states, and decision points.

### System Boot Flow

#### 1. RESET (Entry Point - $F000)
**Path**: Hardware Reset Vector → RESET
- **Sequential execution**: Processor initialization
  - `CLD` - Clear decimal mode
  - `SEI` - Disable interrupts  
  - `LDX #STACK_TOP; TXS` - Initialize stack pointer to $FF
  - `LDA #$3F; STA PROC_DDR` - Configure processor port direction
  - `LDA #$37; STA PROC_PORT` - Set memory banking configuration
- **Flow continues to**: Zero Page Initialization

#### 2. Zero Page Clear Loop
**Path**: RESET → ZP_CLEAR_LOOP
- **LOOP**: `ZP_CLEAR_LOOP`
  - **Entry condition**: X = $06 (start after processor port registers)
  - **Loop body**: 
    - `LDA #$00; STA $00,X` - Clear zero page location
    - `INX` - Increment address
  - **Exit condition**: `BNE ZP_CLEAR_LOOP` (when X wraps to $00)
  - **Iterations**: 250 iterations (clears $06-$FF)
- **Flow continues to**: Screen Memory Initialization

#### 3. Screen Clear Loop  
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

#### 4. Monitor Buffer Clear Loop
**Path**: Screen Clear → MON_CLEAR_CMDBUF
- **LOOP**: `MON_CLEAR_CMDBUF`
  - **Entry condition**: X = $00, A = $00
  - **Loop body**:
    - `STA MON_CMDBUF,X` - Clear buffer location
    - `INX` - Increment position
  - **Exit condition**: `CPX #MON_CMDBUF_LEN; BNE MON_CLEAR_CMDBUF`
  - **Iterations**: 80 iterations (clears command buffer)
- **Flow continues to**: System Ready

#### 5. System Startup Complete
**Path**: Buffer Clear → PRINT_WELCOME → MONITOR_MAIN
- **Sequential execution**:
  - `CLI` - Enable interrupts
  - `JSR PRINT_WELCOME` - Display boot message
  - `JMP MONITOR_MAIN` - Enter main program loop

---

### Main Program Flow

#### 6. Monitor Main Loop (Infinite)
**Path**: Startup → MONITOR_MAIN → MONITOR_LOOP
- **INFINITE LOOP**: `MONITOR_LOOP`
  - **Action**: `JSR PRINT_MONITOR_PROMPT` - Display prompt based on mode
  - **BLOCKING CALL**: `JSR READ_COMMAND_LINE` - Wait for user input
  - **Conditional check**: `LDA MON_CMDLEN; BEQ MONITOR_LOOP` - Empty command loops back
  - **Action**: `JSR PARSE_COMMAND` - Process the command
  - **Flow**: `JMP MONITOR_LOOP` - Always returns to start

---

### Input Handling Flow

#### 7. Command Input Handler (Blocking)
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

##### 7a. Backspace Handling Branch
**Path**: READ_CMD_LOOP → READ_CMD_BACKSPACE
- **Condition check**: `CPX #$00; BEQ READ_CMD_LOOP` - Empty buffer → ignore
- **Actions**: Remove character, update length, echo backspace sequence
- **Flow**: `JMP READ_CMD_LOOP` - Return to input loop

##### 7b. Escape/Cancel Handling Branch  
**Path**: READ_CMD_LOOP → READ_CMD_CANCEL → READ_CMD_CLEAR_LOOP
- **LOOP**: `READ_CMD_CLEAR_LOOP`
  - **Purpose**: Clear entire command buffer
  - **Exit condition**: `CPX #MON_CMDBUF_LEN; BNE READ_CMD_CLEAR_LOOP`
- **Actions**: Print newline, reset variables
- **Flow**: `JMP READ_CMD_LOOP` - Start over

#### 8. Keystroke Polling (Blocking)
**Path**: READ_CMD_LOOP → GET_KEYSTROKE → GET_KEYSTROKE_WAIT
- **INFINITE POLLING LOOP**: `GET_KEYSTROKE_WAIT`
  - **Hardware polling**: `LDA PIA_CONTROL` - Read PIA status
  - **WAIT CONDITION**: `AND #PIA_DATA_AVAIL; BEQ GET_KEYSTROKE_WAIT`
  - **Exit condition**: Data available bit set in PIA
  - **Result**: `LDA PIA_DATA` - Read character and return

---

### Command Parsing Flow

#### 9. Command Parser Decision Tree
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

#### 10. Colon Command Address Parsing
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

### Interactive Mode Flows

#### 11. Write Mode Flow
**Path**: CMD_WRITE_MODE → WRITE_MODE_LOOP → WRITE_MODE_INPUT

##### Main Write Loop (Interactive)
- **INFINITE LOOP**: `WRITE_MODE_INPUT`
  - **BLOCKING CALL**: `JSR READ_COMMAND_LINE` - Get hex input
  - **Exit checks**:
    - `LDA MON_CMDLEN; BEQ WRITE_MODE_DONE` - Empty → exit
    - Check for "X:" command → exit
  - **Flow to**: Hex parsing loop

##### Hex Parsing Loop
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

#### 12. Read Mode Flow  
**Path**: CMD_READ_MODE → READ_MODE_LOOP → READ_MODE_INPUT

##### Main Read Loop (Interactive)
- **INFINITE LOOP**: `READ_MODE_INPUT`
  - **BLOCKING CALL**: `JSR READ_COMMAND_LINE` - Get command
  - **Exit checks**:
    - `LDA MON_CMDLEN; BEQ READ_MODE_INPUT` - Empty → continue
    - Check for "X:" command → exit
  - **Command parsing**: Support R:, W:, G:, H, T commands in read mode
  - **Flow**: Process command and return to input loop

---

### Memory Display Flow

#### 13. Memory Dump Range Display
**Path**: Various commands → DUMP_MEMORY_RANGE → DUMP_RANGE_LOOP

##### Memory Display Loop
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

### Utility Function Flows

#### 14. Hex Conversion Flow
**Path**: Various → HEX_CHAR_TO_NIBBLE

##### Character Validation Decision Tree
- **Digit check**: `CMP #ASCII_0; BCC HEX_CHAR_INVALID`
- **Digit range**: `CMP #ASCII_9+1; BCC HEX_CHAR_DIGIT` → Convert 0-9
- **Uppercase check**: `CMP #ASCII_A; BCC HEX_CHAR_INVALID`  
- **Uppercase range**: `CMP #ASCII_F+1; BCC HEX_CHAR_UPPER` → Convert A-F
- **Lowercase check**: `CMP #$61; BCC HEX_CHAR_INVALID`
- **Lowercase range**: `CMP #$67; BCS HEX_CHAR_INVALID` → Convert a-f
- **Error path**: `SEC; RTS` - Set carry flag for invalid character

#### 15. Message Printing Flow
**Path**: Various → PRINT_MESSAGE → PRINT_MSG_LOOP

##### String Printing Loop  
- **LOOP**: `PRINT_MSG_LOOP`
  - **Character load**: `LDA (MON_MSG_PTR_LO),Y` - Indirect indexed load
  - **Null check**: `BEQ PRINT_MSG_DONE` - Exit on null terminator
  - **Print**: `JSR PRINT_CHAR` - Output character
  - **Increment**: `INY` - Move to next character
  - **Continue**: `BNE PRINT_MSG_LOOP` - Loop if Y hasn't wrapped
- **Limitation**: Strings must be < 256 characters

---

### Critical Wait States and Loops

#### Summary of Blocking Operations:
1. **GET_KEYSTROKE_WAIT** - Hardware polling loop (infinite until key pressed)
2. **READ_CMD_LOOP** - Command input loop (infinite until Enter/Escape)
3. **MONITOR_LOOP** - Main program loop (infinite)
4. **WRITE_MODE_INPUT** - Write mode input loop (infinite until X:)
5. **READ_MODE_INPUT** - Read mode input loop (infinite until X:)

#### Summary of Finite Loops:
1. **ZP_CLEAR_LOOP** - 250 iterations (zero page clear)
2. **SCREEN_CLEAR_LOOP** - 256 iterations (screen clear)  
3. **MON_CLEAR_CMDBUF** - 80 iterations (buffer clear)
4. **DUMP_PRINT_BYTES** - Up to 8 iterations per line (memory display)
5. **WRITE_MODE_PARSE_LOOP** - Variable iterations (hex input parsing)
6. **PRINT_MSG_LOOP** - Variable iterations (string printing)

#### Program Termination Points:
- **CMD_GO_MODE**: `JMP (MON_CURRADDR_LO)` - Transfers control to user program
- **IRQ_HANDLER/NMI_HANDLER**: `RTI` - Return from interrupt
- **No normal exit**: System runs indefinitely in monitor loop

This flow analysis shows that the kernel is designed as a persistent monitor system with interactive command processing, where the main execution flow is an infinite loop waiting for user commands, with various sub-modes providing specialized interactive environments for memory examination and modification.
---

## Part 2 — Kernel call tree


This document traces all JSR (Jump to Subroutine) calls for each monitor command from the main monitor loop through to completion.
It is intended as an aid to developers using the monitor, so that the call tree is easy to follow.

### Main Monitor Loop

```
MONITOR_MAIN
├── JSR PRINT_NEWLINE
└── MONITOR_LOOP
    ├── JSR PRINT_MONITOR_PROMPT
    │   ├── JSR PRINT_CURRENT_ADDRESS
    │   │   ├── JSR BYTE_TO_HEX_PAIR (for high byte)
    │   │   │   └── (uses HEX_LOOKUP_TABLE)
    │   │   ├── JSR PRINT_CHAR (4 times for address digits)
    │   │   └── JSR BYTE_TO_HEX_PAIR (for low byte)
    │   └── JSR PRINT_CHAR (2 times for "> ")
    ├── JSR READ_COMMAND_LINE
    │   ├── JSR GET_KEYSTROKE (multiple times)
    │   ├── JSR PRINT_CHAR (echo each character)
    │   └── JSR PRINT_NEWLINE
    ├── JSR PARSE_COMMAND
    │   └── [Command-specific path follows]
    └── JSR SAVE_COMMAND (if successful)
```

### Commands (Alphabetical Order)

#### C: (Clear Screen) Command
```
PARSE_COMMAND
└── PARSE_CMD_CLEAR
    ├── JSR PARSE_COLON_COMMAND
    │   └── JSR HEX_QUAD_TO_ADDR (if address provided)
    │       └── JSR HEX_PAIR_TO_BYTE (twice)
    │           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
    └── JSR CMD_CLEAR_SCREEN
        └── JSR CLEAR_SCREEN
```

#### F: (Fill Memory) Command
```
PARSE_COMMAND
└── PARSE_CMD_FILL_CHECK
    ├── JSR PARSE_COLON_COMMAND
    │   ├── JSR HEX_QUAD_TO_ADDR (start address)
    │   └── JSR HEX_QUAD_TO_ADDR (end address, if range)
    ├── JSR PARSE_FILL_VALUE
    │   └── JSR HEX_PAIR_TO_BYTE
    │       └── JSR HEX_CHAR_TO_NIBBLE (twice)
    └── JSR CMD_FILL_MEMORY
        ├── JSR VALIDATE_ADDRESS_RANGE
        └── JSR PRINT_MESSAGE (success message)
            └── JSR PRINT_CHAR (for each character)
```

#### G: (Go/Run) Command
```
PARSE_COMMAND
└── PARSE_CMD_GO_CHECK
    ├── JSR PARSE_COLON_COMMAND
    │   └── JSR HEX_QUAD_TO_ADDR
    │       └── JSR HEX_PAIR_TO_BYTE (twice)
    │           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
    └── JSR CMD_RUN_PROGRAM
        └── JSR RUN_USER_PROGRAM
            └── JMP (MON_CURRADDR_LO) [transfers control to user program]
```

#### H: (Help) Command
```
PARSE_COMMAND
└── PARSE_CMD_HELP
    ├── JSR PARSE_COLON_COMMAND
    └── JSR CMD_SHOW_HELP
        ├── JSR PRINT_HELP_HEADER
        │   └── JSR PRINT_MESSAGE
        │       └── JSR PRINT_CHAR (for each character)
        ├── JSR PRINT_NEWLINE_PAGED
        └── JSR PRINT_HELP_BODY
            ├── JSR PRINT_MESSAGE (for each help line)
            │   └── JSR PRINT_CHAR (for each character)
            └── JSR PRINT_NEWLINE_PAGED (after each help line)
```

#### L: (Load File) Command
```
PARSE_COMMAND
└── PARSE_CMD_LOAD_CHECK
    ├── JSR PARSE_COLON_COMMAND
    │   └── JSR HEX_QUAD_TO_ADDR
    │       └── JSR HEX_PAIR_TO_BYTE (twice)
    │           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
    ├── JSR PARSE_FILENAME
    └── JSR CMD_LOAD_FILE
        └── JSR PRINT_MESSAGE (success/error message)
            └── JSR PRINT_CHAR (for each character)
```

#### M: (Move/Copy Memory) Command
```
PARSE_COMMAND
└── PARSE_CMD_MOVE_CHECK
    ├── JSR PARSE_COLON_COMMAND (address range)
    ├── JSR PARSE_MOVE_PARAMS
    │   └── JSR HEX_QUAD_TO_ADDR (destination address)
    └── JSR CMD_MOVE_MEMORY
        ├── JSR VALIDATE_ADDRESS_RANGE
        └── JSR PRINT_MESSAGE (success message)
            └── JSR PRINT_CHAR (for each character)
```

#### R: (Read Memory) Command
```
PARSE_COMMAND
└── PARSE_CMD_READ_CHECK
    ├── JSR PARSE_COLON_COMMAND
    │   ├── JSR HEX_QUAD_TO_ADDR (start address)
    │   └── JSR HEX_QUAD_TO_ADDR (end address, if range)
    └── JSR CMD_READ_MEMORY
        ├── JSR VALIDATE_ADDRESS_RANGE (if range)
        ├── JSR READ_MEMORY_RANGE (for range) OR
        │   └── JSR DUMP_MEMORY_RANGE
        │       ├── JSR PRINT_CHAR (multiple times for address/data)
        │       └── JSR PRINT_NEWLINE_PAGED
        └── JSR SHOW_READ_ADDRESS (for single address)
            └── JSR SHOW_WRITE_ADDRESS
                ├── JSR PRINT_CURRENT_ADDRESS
                ├── JSR BYTE_TO_HEX_PAIR
                └── JSR PRINT_CHAR (multiple times)
```

#### S: (Save File) Command
```
PARSE_COMMAND
└── PARSE_CMD_SAVE_CHECK
    ├── JSR PARSE_COLON_COMMAND (address range)
    ├── JSR PARSE_FILENAME
    └── JSR CMD_SAVE_FILE
        ├── JSR VALIDATE_ADDRESS_RANGE
        └── JSR PRINT_MESSAGE (success/error message)
            └── JSR PRINT_CHAR (for each character)
```

#### T: (Stack Dump) Command
```
PARSE_COMMAND
└── PARSE_CMD_STACK
    ├── JSR PARSE_COLON_COMMAND
    └── JSR CMD_DUMP_STACK
        └── JSR DUMP_MEMORY_RANGE
            ├── JSR PRINT_CHAR (multiple times for address/data)
            └── JSR PRINT_NEWLINE_PAGED
```

#### W: (Write Mode) Command
```
PARSE_COMMAND
└── PARSE_CMD_WRITE_CHECK
    ├── JSR PARSE_COLON_COMMAND
    │   └── JSR HEX_QUAD_TO_ADDR
    │       └── JSR HEX_PAIR_TO_BYTE (twice)
    │           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
    └── JSR CMD_WRITE_MODE
        ├── JSR SHOW_WRITE_ADDRESS
        │   ├── JSR PRINT_CURRENT_ADDRESS
        │   ├── JSR BYTE_TO_HEX_PAIR
        │   ├── JSR PRINT_CHAR (multiple times)
        │   └── JSR PRINT_NEWLINE_PAGED
        └── JSR WRITE_MODE_LOOP
            ├── JSR PRINT_MONITOR_PROMPT
            ├── JSR READ_COMMAND_LINE
            ├── JSR HEX_PAIR_TO_BYTE (for each hex pair entered)
            │   └── JSR HEX_CHAR_TO_NIBBLE (twice)
            ├── JSR DUMP_MEMORY_RANGE (to show modified memory)
            │   ├── JSR PRINT_CHAR (multiple times)
            │   └── JSR PRINT_NEWLINE_PAGED
            └── JSR PRINT_VALUE_ERROR (on error)
                ├── JSR PRINT_MESSAGE
                │   └── JSR PRINT_CHAR (multiple times)
                └── JSR PRINT_NEWLINE
```

#### X: (Search Memory) Command
```
PARSE_COMMAND
└── PARSE_CMD_SEARCH_CHECK
    ├── JSR PARSE_COLON_COMMAND (address range)
    ├── JSR PARSE_SEARCH_PARAMS
    │   └── JSR HEX_PAIR_TO_BYTE (for each pattern byte)
    │       └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
    └── JSR CMD_SEARCH_MEMORY
        ├── JSR VALIDATE_ADDRESS_RANGE
        ├── JSR PRINT_CURRENT_ADDRESS (for each match found)
        │   └── JSR BYTE_TO_HEX_PAIR
        └── JSR PRINT_NEWLINE_PAGED
```

#### Z: (Zero Page Dump) Command
```
PARSE_COMMAND
└── PARSE_CMD_ZERO
    ├── JSR PARSE_COLON_COMMAND
    └── JSR CMD_DUMP_ZERO_PAGE
        └── JSR DUMP_MEMORY_RANGE
            ├── JSR PRINT_CHAR (multiple times for address/data)
            └── JSR PRINT_NEWLINE_PAGED
```

#### ESC (Exit Mode) Command
```
READ_COMMAND_LINE
└── (ESC is handled directly in input processing)
    └── JSR CMD_EXIT_MODE
        └── (sets MON_MODE to command mode, no JSR calls)
```

### Utility Functions Call Trees

#### Core Parsing Functions
```
PARSE_COLON_COMMAND
├── JSR HEX_QUAD_TO_ADDR
│   └── JSR HEX_PAIR_TO_BYTE (twice)
│       └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
└── (validation logic, no additional JSR calls)

HEX_QUAD_TO_ADDR
└── JSR HEX_PAIR_TO_BYTE (twice)
    └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)

HEX_PAIR_TO_BYTE
└── JSR HEX_CHAR_TO_NIBBLE (twice)

HEX_CHAR_TO_NIBBLE
└── (arithmetic operations only, no JSR calls)
```

#### Display Functions
```
BYTE_TO_HEX_PAIR
└── (uses HEX_LOOKUP_TABLE, no JSR calls)

PRINT_MESSAGE
└── JSR PRINT_CHAR (for each character until null terminator)

PRINT_CHAR
├── (normal characters: direct screen memory writes)
├── PRINT_CHAR_NEWLINE (for ASCII_CR)
│   └── JSR SCROLL_SCREEN (if needed)
└── PRINT_CHAR_BACKSPACE (for ASCII_BACKSPACE)
    └── (cursor and screen pointer manipulation)

PRINT_NEWLINE_PAGED
├── JSR PRINT_CHAR
└── JSR HANDLE_PAGE_BREAK (if page full)
    ├── JSR PRINT_MESSAGE (page prompt)
    └── JSR GET_KEYSTROKE (wait for user input)
```

#### Input Functions
```
READ_COMMAND_LINE
├── JSR GET_KEYSTROKE (multiple times)
├── JSR PRINT_CHAR (echo each character)
├── JSR RECALL_LAST_COMMAND (for '.' command)
│   └── JSR PRINT_CHAR (for each recalled character)
└── JSR PRINT_NEWLINE

GET_KEYSTROKE
└── (polls hardware directly, no JSR calls)
```

### Command Mode vs Interactive Mode

#### One-Shot Commands
These commands execute once and return to the command prompt:
- **C:** Clear Screen
- **F:** Fill Memory
- **G:** Go/Run Program
- **H:** Help
- **L:** Load File
- **M:** Move/Copy Memory
- **R:** Read Memory
- **S:** Save File
- **T:** Stack Dump
- **X:** Search Memory
- **Z:** Zero Page Dump

#### Interactive Mode Commands
Only one command has persistent interactive mode:
- **W:** Write Mode - Enters `WRITE_MODE_LOOP` until ESC is pressed

### Error Handling

All parsing functions use carry flag for error indication:
- **Carry Clear (CLC)**: Success
- **Carry Set (SEC)**: Error

Error paths typically call:
```
PRINT_ERROR_MSG, PRINT_VALUE_ERROR, or PRINT_RANGE_ERROR
├── JSR PRINT_MESSAGE
│   └── JSR PRINT_CHAR (multiple times)
└── JSR PRINT_NEWLINE
```

### Notes

- All commands return to `MONITOR_LOOP` after completion
- Only W: command enters persistent interactive mode
- The G: command transfers control to user code and may not return
- Paging support prevents screen overflow in memory dump commands
- The '.' command recalls the last successful command from history
- ESC exits any interactive mode and returns to command mode
---

## Part 3 — Monitor command infrastructure


### Overview

This document provides a comprehensive guide for implementing new commands in the 6502 kernel monitor system. It covers all the components, patterns, and integration points required to successfully add a new command to the monitor.

### Table of Contents

1. [Command Character Assignment](#command-character-assignment)
2. [Jump Table Integration](#jump-table-integration)
3. [Parsing Infrastructure](#parsing-infrastructure)
4. [Memory Allocation](#memory-allocation)
5. [Help System Integration](#help-system-integration)
6. [Message System](#message-system)
7. [Implementation Patterns](#implementation-patterns)
8. [Testing and Validation](#testing-and-validation)
9. [Optional Components](#optional-components)

---

### 1. Command Character Assignment

#### Available Command Letters

The monitor accepts commands in the range 'C' to 'Z' (ASCII $43-$5B). Review the current `CMD_INDEX_MAP` to find available slots:

**Currently Used Commands:**
- `C` → Clear screen
- `G` → Go/Run program  
- `H` → Help
- `L` → Load from file
- `P` → Processor status
- `R` → Read memory
- `S` → Save to file
- `T` → Stack dump (print sTack)
- `W` → Write to memory
- `Z` → Zero page dump

**Available Commands:**
- `D`, `E`, `F`, `I`, `J`, `K`, `M`, `N`, `O`, `Q`, `U`, `V`, `X`, `Y`

#### Command Types

Commands fall into two categories:

1. **Simple Commands** - Single character (e.g., `H`, `C`, `Z`)
2. **Parameterized Commands** - Colon syntax (e.g., `W:8000`, `R:8000-8FFF`)

---

### 2. Jump Table Integration

#### Required Table Updates

When adding a new command, you must update three tables in the exact order:

##### A. CMD_JUMP_COMPACT_LO (Line ~2498)
```assembly
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
    .BYTE <PARSE_CMD_PROCESSOR  ; 10 - 'P'
    ; Add new command here:
    .BYTE <PARSE_CMD_NEW_COMMAND ; 11 - 'F' (example)
```

##### B. CMD_JUMP_COMPACT_HI (Line ~2511)
```assembly
CMD_JUMP_COMPACT_HI:
    .BYTE >PARSE_CMD_GO_CHECK   ; 0 - 'G'
    .BYTE >PARSE_CMD_HELP       ; 1 - 'H'
    .BYTE >PARSE_CMD_CLEAR      ; 2 - 'C'
    .BYTE >PARSE_CMD_LOAD_CHECK ; 3 - 'L'
    .BYTE >PARSE_CMD_READ_CHECK ; 4 - 'R'
    .BYTE >PARSE_CMD_SAVE_CHECK ; 5 - 'S'
    .BYTE >PARSE_CMD_STACK      ; 6 - 'T'
    .BYTE >PARSE_CMD_WRITE_CHECK; 7 - 'W'
    .BYTE >PARSE_CMD_EXIT       ; 8 - 'X'
    .BYTE >PARSE_CMD_ZERO       ; 9 - 'Z'
    .BYTE >PARSE_CMD_PROCESSOR  ; 10 - 'P'
    ; Add new command here:
    .BYTE >PARSE_CMD_NEW_COMMAND ; 11 - 'F' (example)
```

##### C. CMD_INDEX_MAP (Line ~2526)
```assembly
CMD_INDEX_MAP:
    .BYTE 2     ; C -> 2 (Clear)
    .BYTE $FF   ; D -> invalid
    .BYTE $FF   ; E -> invalid
    .BYTE 11    ; F -> 11 (New command) - CHANGE FROM $FF
    .BYTE 0     ; G -> 0 (Run)
    ; ... rest of table
```

**Critical:** The index in `CMD_INDEX_MAP` must match the position in the jump tables (0-based).

---

### 3. Parsing Infrastructure

#### Command Parser Entry Points

Create a parser entry point following the naming convention `PARSE_CMD_[NAME]`:

##### Simple Commands (No Parameters)
```assembly
PARSE_CMD_CLEAR:
    JSR CMD_CLEAR_SCREEN        ; Execute clear screen command
    JMP PARSE_CMD_DONE
```

##### Parameterized Commands (Colon Syntax)
```assembly
PARSE_CMD_FILL_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse F:xxxx-yyyy,zz format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_FILL_MODE           ; Execute fill mode command
    JMP PARSE_CMD_DONE
```

#### Using PARSE_COLON_COMMAND

The `PARSE_COLON_COMMAND` function handles standard address parsing:

**Input:** Command in `MON_CMDBUF` starting with command character
**Output:** 
- Single address: `MON_CURRADDR_HI/LO`
- Range: `MON_CURRADDR_HI/LO` (start) and `MON_ENDADDR_HI/LO` (end)
- Carry flag: Clear if successful, Set if error

**Supported Formats:**
- `F:8000` → Single address in `MON_CURRADDR_HI/LO`
- `F:8000-8FFF` → Range with start in `MON_CURRADDR_HI/LO`, end in `MON_ENDADDR_HI/LO`

#### Custom Parameter Parsing

For commands requiring additional parameters (like Fill's comma-separated value), implement custom parsing after `PARSE_COLON_COMMAND`:

```assembly
PARSE_CMD_FILL_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse address/range
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR PARSE_FILL_VALUE        ; Custom parsing for fill value
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_FILL_MODE           ; Execute fill command
    JMP PARSE_CMD_DONE
```

---

### 4. Memory Allocation

#### Current Memory Layout

The monitor uses system RAM `$0200-$0283` (132 bytes total):

```
$0200-$024F: Command input buffer (80 bytes)
$0250-$025F: Core monitor variables (16 bytes)
$0260-$0283: Phase 1 command variables (36 bytes)
```

#### Available Memory Ranges

**Next Available:** `$0284-$02FF` (124 bytes remaining in page)

#### Variable Allocation Guidelines

1. **Place variables after existing allocations** (starting at `$0284`)
2. **Use consistent naming:** `MON_[COMMAND]_[PURPOSE]`
3. **Document memory usage** in header comments
4. **Allocate in logical groups** by command

#### Example Variable Allocation

```assembly
; ================================================================
; PHASE 2 COMMAND VARIABLES ($0284+)
; ================================================================
MON_FILL_VALUE    = $0284         ; Fill command byte value
MON_FILL_COUNTER  = $0285         ; Fill operation counter (2 bytes: $0285-$0286)
MON_SEARCH_MATCHES = $0287        ; Search command match counter (2 bytes)
MON_COPY_TEMP     = $0289         ; Copy command temporary storage
```

#### Zero Page Usage

**Critical Zero Page Locations (DO NOT MODIFY):**
- `$00-$01`: Processor port (memory banking)
- `$02-$03`: `MON_CURRADDR_LO/HI` (current address)
- `$04-$05`: `MON_MSG_PTR_LO/HI` (message pointer)
- `$06-$07`: `JUMP_VECTOR` (indirect jump vector)

---

### 5. Help System Integration

#### Required Updates

Adding a command requires updating three help-related components:

##### A. Add Help Message (Line ~2585)
```assembly
; MESSAGE DATA SECTION - Null-terminated strings for monitor
MSG_HELP_CLEAR:      .BYTE "C:     CLEAR SCREEN", 0
MSG_HELP_GO:         .BYTE "G:XXXX RUN", 0
; Add new help message:
MSG_HELP_FILL:       .BYTE "F:XXXX-YYYY,ZZ FILL MEMORY", 0
```

##### B. Add to Help Message Table (Line ~2568)
```assembly
HELP_MSG_TABLE:
    .WORD MSG_HELP_CLEAR
    .WORD MSG_HELP_GO
    .WORD MSG_HELP_LOAD
    .WORD MSG_HELP_PROCESSOR
    .WORD MSG_HELP_READ
    .WORD MSG_HELP_SAVE
    .WORD MSG_HELP_STACK
    .WORD MSG_HELP_WRITE
    .WORD MSG_HELP_ZERO
    .WORD MSG_HELP_EXIT
    ; Add new help message:
    .WORD MSG_HELP_FILL
```

##### C. Update Help Count (Line ~2580)
```assembly
HELP_MSG_COUNT = 11              ; Number of help messages (was 10)
```

#### Help Message Format Guidelines

- **Keep consistent width** (approximately 30 characters max)
- **Use format:** `COMMAND:PARAMS DESCRIPTION`
- **Examples:**
  - Simple: `"C:     CLEAR SCREEN"`
  - Range: `"R:XXXX(-YYYY) READ FROM MEMORY"`
  - Complex: `"F:XXXX-YYYY,ZZ FILL MEMORY"`

---

### 6. Message System

#### Message Storage

All messages are stored as null-terminated strings in the message data section (starting around line 2585):

```assembly
; MESSAGE DATA SECTION - Null-terminated strings for monitor
MSG_HELP_HEADER:     .BYTE "6502 MONITOR COMMANDS", 0
MSG_SYNTAX_ERROR:    .BYTE "ERROR?", 0
MSG_SUCCESS:         .BYTE "OK", 0
```

#### Standard Message Types

##### Success Messages
- `MSG_SUCCESS` - Generic "OK" message
- Custom success messages for specific operations

##### Error Messages  
- `MSG_SYNTAX_ERROR` - "ERROR?" for invalid syntax
- Custom error messages for specific conditions

#### Message Printing System

Use the optimized message printing system:

```assembly
; Set up message pointer
LDA #<MSG_FILL_SUCCESS          ; Load low byte
STA MON_MSG_PTR_LO              ; Store in pointer
LDA #>MSG_FILL_SUCCESS          ; Load high byte  
STA MON_MSG_PTR_HI              ; Store in pointer
JSR PRINT_MESSAGE               ; Print the message
```

#### Message Optimization Pattern

For frequently used messages, create dedicated print functions:

```assembly
PRINT_FILL_SUCCESS:
    LDA #<MSG_FILL_SUCCESS      ; 2 bytes
    STA MON_MSG_PTR_LO          ; 3 bytes
    LDA #>MSG_FILL_SUCCESS      ; 2 bytes
    STA MON_MSG_PTR_HI          ; 3 bytes
    JSR PRINT_MESSAGE           ; 3 bytes
    RTS                         ; 1 byte
    ; Total: 14 bytes vs ~3 bytes per inline usage
```

---

### 7. Implementation Patterns

#### Command Implementation Structure

Follow this consistent pattern for command implementations:

```assembly
; Command Name - Brief description
; Input: Description of expected input (addresses, parameters)
; Modifies: A, X, Y (list registers modified)
CMD_COMMAND_NAME:
    ; 1. Validate parameters (if needed)
    ; 2. Perform operation
    ; 3. Display results/success message
    ; 4. Return

    RTS
```

#### Common Code Patterns

##### Address Validation
```assembly
; Validate address range (start <= end)
LDA MON_CURRADDR_HI             ; Compare high bytes first
CMP MON_ENDADDR_HI
BCC RANGE_VALID                 ; start < end (high), valid
BNE RANGE_ERROR                 ; start > end (high), error
LDA MON_CURRADDR_LO             ; High bytes equal, compare low bytes
CMP MON_ENDADDR_LO
BCC RANGE_VALID                 ; start < end (low), valid
BEQ RANGE_VALID                 ; start = end (low), valid
; start > end, error falls through

RANGE_ERROR:
    ; Handle error
    SEC                         ; Set carry for error
    RTS

RANGE_VALID:
    CLC                         ; Clear carry for success
    ; Continue with operation
```

##### Memory Operations Loop
```assembly
OPERATION_LOOP:
    ; Perform operation on byte at (MON_CURRADDR_LO),Y

    ; Check if we've reached end address
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BCC CONTINUE_OPERATION       ; Current < end (high), continue
    BNE OPERATION_DONE          ; Current > end (high), done
    LDA MON_CURRADDR_LO
    CMP MON_ENDADDR_LO
    BCC CONTINUE_OPERATION       ; Current < end (low), continue  
    BEQ CONTINUE_OPERATION       ; Current = end (low), do this byte too

OPERATION_DONE:
    RTS

CONTINUE_OPERATION:
    ; Increment address
    INC MON_CURRADDR_LO
    BNE OPERATION_LOOP           ; No carry, continue
    INC MON_CURRADDR_HI          ; Handle carry
    JMP OPERATION_LOOP
```

##### Parameter Parsing (Custom)
```assembly
; Parse custom parameter from MON_CMDBUF
; Input: X = position in command buffer
; Output: Parsed value, Carry = error flag
PARSE_CUSTOM_PARAM:
    ; Skip to parameter (after comma, space, etc.)
    ; Parse value
    ; Validate value
    ; Store result
    ; Set/clear carry flag
    RTS
```

---

### 8. Testing and Validation

#### Integration Testing Checklist

When implementing a new command, verify:

##### Parser Integration
- [ ] Command character recognized (no "ERROR?")
- [ ] Parameters parsed correctly
- [ ] Invalid syntax shows "ERROR?" 
- [ ] Command executes without crashing

##### Jump Table Validation
- [ ] Correct parser function called
- [ ] All three tables updated consistently
- [ ] Index mapping correct

##### Help System
- [ ] Help message displays correctly
- [ ] Help count updated
- [ ] Message formatting consistent

##### Memory Usage
- [ ] Variables allocated in correct range
- [ ] No conflicts with existing variables
- [ ] Memory usage documented

#### Manual Test Cases

Create test cases covering:

1. **Valid Operations**
   - Normal parameter ranges
   - Edge cases (single byte, max range)
   - Different parameter formats

2. **Error Conditions**
   - Invalid syntax
   - Invalid parameters
   - Boundary violations

3. **Integration**
   - Command works in sequence with others
   - Memory state preserved correctly
   - No interference with existing commands

#### Example Test Script

```
H:              # Show help (verify new command listed)
F:8000-8010,FF  # Fill range with valid parameters
F:8000          # Single address fill
F:8000-7FFF,00  # Invalid range (start > end) - should error
F:8000-8010     # Missing parameter - should error  
```

---

### 9. Optional Components

#### Success Messages

Success messages are **optional** but recommended for commands that:
- Perform significant operations (filling large ranges)
- Have non-obvious completion status
- Take noticeable time to execute

**Simple commands** (like clear screen) typically don't need success messages.

#### Progress Indication

For long-running operations, consider:
- Periodic progress dots
- Abort checking (ESC key)
- Status counters

Example progress pattern:
```assembly
FILL_LOOP:
    ; Check for ESC key periodically
    LDA BYTE_COUNT
    AND #$FF                    ; Check every 256 bytes
    BNE SKIP_ESC_CHECK
    JSR CHECK_KEYBOARD          ; Check for ESC key
    BEQ FILL_ABORTED           ; ESC pressed, abort

SKIP_ESC_CHECK:
    ; Perform fill operation
    ; ... fill code here ...
    JMP FILL_LOOP

FILL_ABORTED:
    ; Handle abort condition
    RTS
```

#### Parameter Validation

Implement parameter validation appropriate to command complexity:

##### Basic Validation
- Range checking (start <= end)
- Parameter count verification
- Syntax validation

##### Advanced Validation
- Memory protection (ROM/I/O areas)
- Value range limits
- Overlap detection (for move/copy operations)

#### Error Message Specificity

Balance error message detail with memory usage:

##### Generic Errors (Recommended)
- `ERROR?` - General syntax/parameter error
- `RANGE?` - Address range error  

##### Specific Errors (Optional)
- `?SYNTAX` - Syntax error
- `VALUE?` - Parameter value error
- `?PROTECTED` - Attempting to modify protected memory

---

### Implementation Workflow

#### Step-by-Step Process

1. **Choose Command Letter** - Select unused letter from available range
2. **Design Command Syntax** - Define parameter format and validation rules
3. **Allocate Memory** - Reserve variables in `$0284+` range
4. **Update Tables** - Add entries to all three jump/mapping tables
5. **Implement Parser** - Create `PARSE_CMD_[NAME]` function
6. **Implement Command** - Create `CMD_[NAME]` function
7. **Add Messages** - Create help text and any custom messages
8. **Update Help System** - Add to help table and increment count
9. **Test Integration** - Verify all components work together
10. **Document** - Update memory usage comments and any documentation

#### Memory Impact Summary

Adding a typical command requires:
- **Jump tables:** +2 bytes (low/high byte entries)
- **Index mapping:** 0 bytes (replace $FF with index)
- **Help system:** ~25-35 bytes (message + table entry)
- **Command variables:** 1-10 bytes (depends on command complexity)
- **Implementation code:** 50-200 bytes (depends on functionality)

**Total overhead:** ~80-250 bytes per command

---

### Common Pitfalls

1. **Mismatched table indices** - Ensure jump table order matches index mapping
2. **Forgetting help count update** - Must increment `HELP_MSG_COUNT`
3. **Memory conflicts** - Verify variable allocations don't overlap
4. **Error handling** - Always set/clear carry flag appropriately
5. **Address arithmetic** - Remember 6502 has no 16-bit arithmetic instructions
6. **Parser state** - Preserve `MON_CURRADDR` for other commands when needed
7. **Message formatting** - Keep consistent with existing help messages

Following this infrastructure guide ensures new commands integrate cleanly with the existing monitor system while maintaining consistency and reliability.
---

## Part 4 — DOS / filesystem design


**Status:** in progress (the major arc after v3.2). Phase 1 (block device) done;
**Phase 2 (FAT16 read) COMPLETE** — 2.1 memory-map shift, 2.2 block primitives +
`$AF00` DOS ABI table, 2.3 FAT16 read (mount + directory + cluster-chain file read),
2.4 the `mkfat16` tool + a temporary `@` catalog/type monitor command (kernel v3.3).
**Phase 3 (FAT16 write) COMPLETE** (3a write engine + 3b FS_DELETE/`@` save-erase).
**Phase 4 (DOS shell) underway** — 4.1 boot pivot + 4.2 file verbs done: the machine
boots into the **MFC/OS** shell (`]` prompt) with `CATALOG`/`TYPE`/`SAVE`/`LOAD`/`ERASE`/
`RENAME`/`IMPORT`/`EXPORT`/`MON`/`HELP`; the monitor is launched by `MON`, exited with
`Q`, and is a pure debugger (the `@` preview and the host `L:`/`S:` are retired - host
transfer is now DOS `IMPORT`/`EXPORT`). **Phase 4 COMPLETE:** launch-by-name runs
`BASIC`/`ASM` and disk `.PRG` programs (`&` forces the disk version), each returning to
`]`; the `B:` bank menu is retired. Kernel v3.8. **The assemble → SAVE → run loop is
closed.** Identity: OS = **MFC/OS**, `]` prompt.

**Utility commands added** (post-phase-4): `COPY SRC,DST` (via a RAM buffer — the
filesystem is single-open, so COPY reads the source fully into `$0800` then writes it
out; files > ~32 KB report `FILE TOO BIG`), `DISKFREE` (free space in decimal bytes +
KB; scans each FAT sector once), `MEMMAP` (the full memory map with region sizes),
`VERSION`, `MORE NAME`, and **wildcards** in `CATALOG`/`CAT` (`*` and `?`, 8.3).
`CATALOG` now prints a `NAME / BYTES` header with decimal sizes in aligned columns.

**System-wide pager (kernel v3.18 / DOS 1.9).** Paging moved out of DOS's own
`MORE` into the kernel's single `PRINT_CHAR` path (`PAGE_ADVANCE`): it counts
newlines and pauses every `LINES_PER_PAGE` (24) with a `--MORE-- (SPACE, ESC=STOP)`
prompt, gated by `PAGE_ENABLE` (default on) and reset per command in
`GET_KEYSTROKE` (on the submitting CR). Because every text program prints through
`K_PRINT_CHAR`, the DOS shell, MON, BASIC, ASM, and FORTH are **all** paged with no
code of their own — long `TYPE`/`CATALOG`/`LIST`/`WORDS`/dumps pause each screenful;
SPACE (or any key) advances, ESC stops. As a result `MORE` is now identical to
`TYPE` (it dispatches to it); the old per-command DOS pager was removed. A future
settings facility will expose `PAGE_ENABLE` so paging can be turned off. `DATE` shows
the date and time from the host-time RTC (`$FE55-$FE5C`, added in the RTC phase-1
commit); the same clock timestamps files, so `CATALOG` shows a date/time per entry.
`BANKS` lists the ROM modules. Decimal conversion was
promoted to the BIOS ABI (kernel v3.12): `K_PRINT_DEC` ($FF27, 32-bit → decimal,
right-justifiable) and `K_PARSE_DEC` ($FF2A, decimal → 16-bit). The monitor's `H:`/`D:`
and the DOS (`CATALOG` sizes, `DISKFREE`) all share these — one implementation each.

**Drawers — one level of subdirectories (DOS v1.2, Phase 1).** `NEWDRAWER name`,
`OPEN name`, `CLOSE`, `DROPDRAWER name` (empty only). A current-directory pointer
(`DOS_CWD_CLUS`, 0 = root) plus a unified directory iterator that walks either the
fixed root region or a subdirectory's **cluster chain** (growable: a full drawer
directory chains another cluster). Path resolution (`FILE` = current dir,
`DRAWER/FILE` = a named root drawer, `/FILE` = root) is wired into `_FS_OPEN`/
`_FS_DELETE`/`_FS_RENAME`, so the file verbs act in the current drawer (and bare
names keep the `FS_OPEN` ABI working for launched programs). `CATALOG` tags drawers
`<D>` and hides `.`/`..`; the prompt shows the open drawer (`UTILS]`). Drawers are
created at runtime (the `mkfat16`/test image builder is root-only). One level only:
drawers can't nest. Note: this surfaced and fixed a latent overlap where
`DOS_TMP2` aliased `DOS_ENTRY+0` (harmless until subdir enumeration interleaved a
FAT read with entry inspection).

**Drawers Phase 2 (DOS v1.3).** Cross-drawer file motion: `COPY` takes qualified
paths on either side (e.g. `COPY GAMES/CHESS.PRG,/CHESS.PRG`) — it gets this for
free since path resolution lives in `_FS_OPEN`. New `MOVE SRC,DST` shares COPY's
RAM-buffer path then deletes the source (same dir → effectively a rename; across
drawers → a move), guarded against `MOVE A,A`. Path resolution was given its own
scratch byte (`DOS_RES_SLASH`) so it no longer clobbers the SRC offset COPY/MOVE
hold in `DOS_SH_NAMEIDX`. Also added a FAT **allocation rover** (`DOS_ALLOC_HINT`):
cluster allocation resumes where the last one stopped instead of rescanning from
cluster 2, turning a multi-cluster write from O(file x used-clusters) FAT reads
into ~O(file) — copying a several-KB program is now snappy instead of seconds.
80-column display is the remaining DOS-arc item after drawers.

The pivot: the machine **boots into a DOS** — a command shell with a filesystem,
like an Apple II / TRS-80 / Kaypro (CP/M). BASIC, the assembler/disassembler, the
monitor, and an editor become **programs you launch by name** from the DOS, not the
front door. This turns the project from "a monitor with ROM modules" into a small
**6502 disk operating system**.

References that shape this: cpm65 (a 6502 OS with BIOS/BDOS/CCP layering, disk-as-a-
host-file, relocatable programs), the X16 emulator's `sdcard.c` (host `.img` file,
512-byte blocks), and mike42's 6502 SD reader (the block device). See
Part 7 — External references (below).

---

### The model: a resident OS + apps

```
Apps        ── BASIC, monitor, assembler/disassembler, editor, games
              (ROM banks in the $B000-$EFFF window, or program files on disk)
─────────────────────────────────────────────────────────────────────────
Resident OS ── DOS shell  (prompt, commands, launch-by-name)        [DOS ROM]
            ── Filesystem (FAT16 over the block device)             [DOS ROM]
            ── BIOS       (boot/init, I/O, the $FF00 ABI, banking)  [kernel ROM]
            ── Block device driver (512-byte sectors)               [BIOS]
─────────────────────────────────────────────────────────────────────────
Hardware    ── host disk.img (a real FAT16 volume the Mac can mount too)
```

This is the CP/M BIOS / BDOS / CCP shape: **BIOS** = machine + I/O, **BDOS** = the
filesystem, **CCP** = the DOS command shell.

### Kernel refactor: BIOS vs. monitor

Today's `kernel.asm` is really two things glued together. We separate them:

**BIOS — the resident foundation (stays in the kernel ROM, never banked):**
- `RESET` + init (decimal/interrupt/stack, ZP clear, screen clear, window clear,
  `MODULE_BANK` init), IRQ/NMI handlers + timer.
- Screen/keyboard I/O: `PRINT_CHAR`, `PRINT_MESSAGE`, `PRINT_NEWLINE`,
  `SCROLL_SCREEN`, `CLEAR_SCREEN`, cursor; `GET_KEYSTROKE`, `READ_COMMAND_LINE`,
  hex parse, `PRINT_HEX_BYTE`.
- The **`$FF00` jump-table ABI** and the **bank mechanism** (`MODULE_DIR`, bank
  launch, `$FF12` return).
- **New:** block-device driver; **FS ABI** entries (the FAT16 code lives in the DOS
  ROM, reached through these).
- Vectors, RNG.

**Monitor — a debugger *tool* (no longer the front door):**
- `MONITOR_LOOP`, prompt, dispatch, and the single-letter commands `R`/`W`/`F`/`M`/
  `X`/`G`/`T`/`Z`/`C`, `D`/`H`, `?`, `.`.
- Launched from the DOS by `MON`; exits back to the DOS. (`L`/`S` and the old `B:`
  bank menu move to the DOS.)
- Stays in the kernel ROM for now; can be relocated to a bank later (see
  [Memory](#memory-budget--creating-space)).

### Boot flow

`RESET` → BIOS init (as today) → **`JMP` to the DOS shell entry** (instead of
`MONITOR_MAIN`). The user lands at the DOS prompt.

### Storage backing + block device (emulator)

A single host file, `disk.img` (a real FAT16 volume), opened `r+b`. The 6502 talks
to it through a **simple memory-mapped block-device register interface** — not SPI/SD
protocol emulation (the X16 emulates SPI only because it's real hardware with a
physical SD port; we're software-defined, so SPI ceremony buys nothing).

Registers in the I/O page, after `MODULE_BANK` (`$FE23`):

| Addr | Name | Purpose |
|------|------|---------|
| `$FE24-$FE25` | `BLK_LBA` | 16-bit sector number (→ 32 MB image; widen to 3 bytes later if needed) |
| `$FE26` | `BLK_CMD` | write `1` = read sector→buffer, `2` = write buffer→sector |
| `$FE27` | `BLK_STATUS` | `0` = ready, non-zero = busy/error/no-disk |
| `$FE28` | `BLK_DATA` | 512-byte data port, auto-incrementing index |

Read = set `BLK_LBA`, `BLK_CMD=1`, read `BLK_DATA` ×512. Write = write `BLK_DATA`
×512, set `BLK_LBA`, `BLK_CMD=2`. Emulator side: a small block-device class (open
`disk.img`, a 512-byte buffer, four registers wired through `Memory`). ~50-80 lines.

### Filesystem (resident FAT16, in the DOS ROM)

**FAT16**, chosen for **host interoperability**: `disk.img` is a normal FAT16 volume,
so the Mac can mount it and exchange the same files the machine sees. Simpler than
FAT32; a few-MB image is plenty. (FAT32 / CP/M / custom formats were considered and
set aside — FAT16 is the sweet spot for size and Mac-mountability.)

Driver scope: mount (boot sector/BPB → FAT, root dir, data region); read (find by
8.3 name, follow the cluster chain); write (allocate clusters, update FAT + dir
entry); directory enumeration. **Starting simplifications:** root directory only,
one open file at a time, 8.3 names, FAT16 only — enough for the whole workflow.

**FS ABI** (via the `$FF00` table, byte-stream like today's LOAD/SAVE):
`FS_OPEN(name, mode)`, `FS_GETB` (carry=EOF), `FS_PUTB(byte)`, `FS_CLOSE`,
`FS_DIR_FIRST`/`FS_DIR_NEXT`. Filenames are supplied by the 6502 (the FS finds the
file, no host dialog).

### The DOS shell (CCP)

Boots to a prompt, reads a line (reuses the BIOS `READ_COMMAND_LINE`), parses a
**verb + args**, and dispatches. Verbs (our vocabulary — full words, optional short
aliases):

| Command | Does |
|---------|------|
| `CATALOG` (`CAT`) | list files (name, size) + free space |
| `LOAD name[,addr]` | load a file into memory (addr from the file's header if omitted) |
| `SAVE name,start-end` | save a memory range to a file (writes the load-address header) |
| `ERASE name` | delete a file |
| `RENAME old,new` | rename a file |
| `TYPE name` | display a text file |
| `IMPORT name[,host]` | copy a host file into a FAT16 file (`host` names it; omitted = file picker) |
| `EXPORT name[,host]` | copy a FAT16 file out to a host file (`host` names it; omitted = save dialog) |

#### Launch by name (unified — ROMs *and* programs)

There is **no `BANKS` menu and no `RUN` verb.** You type a **name** at the DOS prompt
and the DOS resolves it, in order:

1. **Built-in DOS command** (`CATALOG`, `ERASE`, …; includes `MON`).
2. **Built-in ROM program** — `BASIC`, `ASM` (from the `MODULE_DIR` registry) → map
   the bank, jump to its entry; returns to the DOS on exit.
3. **Program file on disk** → load it into RAM and execute.

**Settled (2026-06):** the assembler's launch name is **`ASM`** (its `MODULE_DIR`
name). **ROM-module-first** resolution (a disk file of the same name is shadowed),
with an **override prefix `&`**: typing `&NAME` skips the module check and runs the
disk file `NAME` (so a disk program can intentionally replace a built-in). A launched
program returns to the DOS by a plain **`RTS`** — the DOS runs it as a subroutine
(pushes a `DOS_WARM` return), so a normal `RTS` lands back at the `]` prompt; a
program that takes over the machine just needs a reset.

So "ROMs" vs "programs" is just an implementation detail (ROM = fast, always present,
not editable; file = on disk, can be assembled/saved). The old `B:` bank menu is
retired; `MODULE_DIR` remains the registry the DOS consults (via a kernel ABI). A
`HELP`/built-ins listing surfaces the resident programs; `CATALOG` lists disk files.

#### Program file format

A runnable program file begins with a **2-byte little-endian load address** (exactly
like a C64 `.PRG`). To launch a file program: read the 2-byte header, load the body
there, `JMP` to the load address (entry = start). This dovetails with the assembler:
`.ORG $0800` → the saved program carries `$0800` as its header → launching it puts it
back at `$0800`. Closes the loop: **assemble → SAVE (with header) → run by name.**
(Relocatable, cpm65-style position-independent loading is a possible future upgrade.)

#### `RUN` vs the monitor's `G:`

Different layers, both kept: the monitor's **`G:addr`** *jumps to an address* where
code already sits (low-level debug "go"); **launch-by-name** *loads a program then
executes* (≈ load + go). `RUN` is dropped — typing the name is the launch.

### Memory budget — creating space

The resident OS (BIOS + monitor + FS + DOS) won't fit the 8 KB kernel ROM, and the
kernel can't grow in place (the 12 KB BASIC window sits below `$E000`; vectors pin the
top). So we **add a second always-mapped ROM** — sanctioned ("don't be afraid to
create space"):

- **`DOS ROM` at `$9000-$AFFF` (8 KB), always mapped**, holding the **FAT16 FS + DOS
  shell**. Routed by the emulator like the kernel/BASIC ROMs (but always mapped, not
  banked). Boot: BIOS init → `JMP` DOS entry; FS ABI entries in `$FF00` jump into it.
- **User RAM becomes `$0800-$8FFF` (~34 KB)** — still ample; the assembler's source
  buffer / symbol table relocate below `$9000`.

> **Later change (2026-07):** the DOS base moved down again, to `$8800-$AFFF` (10 KB),
> because the DOS had 145 bytes left below its `$AF00` ABI table while the kernel sat
> on 3.7 KB spare. User RAM is now `$0800-$87FF` (32 KB) and the assembler's buffers
> moved with it (`$7800` source, `$7600` symbols). The addresses in this section and in
> the phase log below record the state at the time and are left as written.

This keeps the **kernel ROM = BIOS + monitor** untouched, and gives FS+DOS a roomy
home. (An alternative is the bigger coordinated memory-map overhaul; the second-ROM
approach is lower-risk and is the plan.)

Later optional tidy-up: relocate the **monitor to a bank** (porting it to the `$FF00`
ABI), leaving the kernel ROM as a lean pure-BIOS — not required for function.

### Identity

**Settled (2026-06-14):** the OS is **MFC/OS**; boot shows a sign-on banner and a
an **`]`** prompt. The monitor is launched by `MON` and exited with **`Q`** (back to
the DOS prompt). (The dos.rom signature string stays "MFC-DOS" as an internal marker.)

### Phased build plan

1. **Block device** — emulator `disk.img` + the `$FE24` registers + `Memory` routing;
   a 6502 sector read/write smoke test. (Small, foundational.) **— DONE.** `BlockDevice`
   (`include/computer/BlockDevice.h`, `src/computer/BlockDevice.cpp`) backs a host
   `disk.img` (lazily opened, auto-created, grows on write, reads zeros past EOF);
   `Memory` routes `$FE24-$FE28` to it; `Computer6502` owns it (default image
   `../disk.img`). Covered by `tests/test_block_device.cpp` (`block_device_unit_tests`).
2. **FAT16 mount + read** — mount, `CATALOG`, read a file by name; the FS ABI. (At
   this point, mount-on-Mac authoring already works.) Sub-steps:
   - **2.1 Memory-map shift — DONE.** The always-mapped **DOS ROM at `$9000-$AFFF`**
     was pulled forward to here (rather than phase 4) so the FS has its permanent
     home from the start. Emulator routes `$9000-$AFFF` to a `dos.rom` image (writes
     ignored; falls through to RAM if absent); user RAM is now `$0800-$8FFF`; BASIC
     `Ram_top → $9000`; the assembler's buffers moved to `$8000` (source) / `$7E00`
     (symbols). Stub `src/kernel/dos/dos.asm` (signature only) builds `dos.rom`.
     Covered by DOS-ROM cases in `tests/test_memory_banking.cpp`.
   - **2.2 — DONE.** Block-device equates + 512-byte sector read/write primitives
     (`BLK_READ_SECTOR`/`BLK_WRITE_SECTOR`, the 6502 side of `$FE24-$FE28`) + FS ABI
     stubs, all in `src/kernel/dos/dos.asm`. Stable entry points live in a **DOS ABI
     jump table at `$AF00`** (mirrors the kernel `$FF00` table): `DOS_COLD`, `FS_OPEN`/
     `FS_GETB`/`FS_PUTB`/`FS_CLOSE`/`FS_DIR_FIRST`/`FS_DIR_NEXT` (stubs, carry=error),
     `BLK_READ_SECTOR` (`$AF15`), `BLK_WRITE_SECTOR` (`$AF18`).

     **Register contract for the `$AF00` FS entries:** results come back in **A and
     the carry** (carry set = error / EOF). `FS_GETB` preserves **X and Y** — it used
     to destroy X only on the path that crosses a 512-byte sector boundary, which is
     the worst kind of bug: a read loop indexing with X passed every small-file test
     and corrupted itself on the 513th byte, so it is now explicitly preserved and
     pinned by `FsGetbPreservesXAcrossSectorBoundaries`. Every **other** FS entry
     leaves **X undefined** (`FS_PUTB` clobbers it at entry testing `DOS_W_MODE`, and
     the open/close/delete/rename paths run cluster and directory arithmetic through
     X); assume only A and carry survive those. The cc65 glue in `programs/*/glue.s`
     reloads X on return, which is why this went unnoticed for so long. DOS zero page uses the
     free `$3A-$5A` gap (`BLK_BUF_PTR=$3A`). Covered by `tests/test_dos_blockio.cpp`
     (`dos_blockio_tests`) which runs the real `dos.rom` routines.
   - **2.3 — DONE.** FAT16 read driver in `src/kernel/dos/dos.asm`, validated by
     `tests/test_dos_fat16.cpp` against host-built images (`tests/support/fat16_image.h`).
     - *2.3a:* auto-mount (parse boot-sector BPB → sectors/cluster, FAT/root/data
       start, root entry count, cached in the `$0300` DOS state block) +
       `FS_DIR_FIRST`/`FS_DIR_NEXT` (root-dir walk, skipping deleted/LFN/volume
       entries, leaving the 32-byte entry in `DOS_ENTRY`).
     - *2.3b:* `FS_OPEN` (parse 8.3 name, scan dir, arm the open-file cursor),
       `FS_GETB` (stream bytes across sector boundaries, following the FAT16
       cluster chain at cluster boundaries; carry=EOF), `FS_CLOSE`. Reads stream
       through the block device's sector buffer (no 512B RAM buffer); FAT lookups
       and dir scans use bounded skip-reads. `FS_PUTB` remains a stub (phase 3).
   - **2.4 — DONE.** Interactive surface + tooling:
     - `tools/mkfat16` creates a FAT16 `disk.img` (sample files by default, or host
       files added under derived 8.3 names), reusing the shared image builder. It
       sizes the volume as a genuine FAT16 (>= 4085 clusters), so macOS mounts it
       read/write and exchanges files with the machine. A `sample_disk` CMake
       target writes `<build>/disk.img`.
     - Temporary monitor command **`@`** (kernel.asm, v3.3): `@` catalogs the disk
       (names + sizes), `@NAME` types a file. It calls the DOS ABI at `$AF..`
       directly (the DOS ROM is always mapped), so no kernel `$FF00` change was
       needed; phase 4 replaces `@` with the real DOS shell.
     - Covered by `monitor_integration` (catalog, type, missing-file) against a
       mounted FAT16 image.
3. **FAT16 write** — create / `ERASE` / `SAVE`; full round-trip on the machine.
   - **3a — DONE.** The write engine in `dos.asm`: cluster allocation (scan the FAT
     for a free entry, mark EOC), FAT-entry **read-modify-write** (read the sector,
     skip to the entry, overwrite 2 bytes in the buffered sector, flush — no RAM
     sector buffer), free-chain, directory-slot find (reuse same-name + free old
     chain, else append), and `FS_OPEN(write)` / `FS_PUTB` / `FS_CLOSE` (stream
     bytes, allocate + chain clusters across boundaries, pad + flush the final
     sector, finalize the dir entry). Single + multi-cluster; truncate-on-reopen.
     `FS_OPEN` mode is passed in Y (0 = read, 1 = write). A C++ FAT16 parser
     (`Fat16ImageReader`) independently validates the on-disk format; covered by
     write/round-trip cases in `tests/test_dos_fat16.cpp`. (Single FAT copy;
     deleted-slot reclaim deferred.)
   - **3b — DONE.** `FS_DELETE` (scan, free the cluster chain, mark the directory
     entry `$E5`) at DOS ABI `$AF1B`. The temporary `@` preview gains write
     commands (kernel v3.4): `@-NAME` erases, and `@SSSS-EEEE=NAME` saves a memory
     range to a file (`FS_OPEN`-write + `FS_PUTB` loop + `FS_CLOSE`). Covered by
     erase/free-and-reuse cases in `dos_fat16_tests` and save/erase round-trip in
     `monitor_integration`. (Full machine round-trip: poke memory -> `@..=F` save
     -> `@` catalog -> `@F` type back.)
4. **DOS shell as boot target** — the pivot: fill the (already-present) `$9000-$AFFF`
   DOS ROM with the command shell, boot into the DOS prompt, the command set above,
   launch-by-name (command → ROM module → file), program-file loader. `MON`/`BASIC`/
   `ASM` launch their banks; the monitor is entered as a tool and returns to DOS.
   - **4.1 — DONE.** The boot pivot. RESET now `JMP DOS_COLD`; the machine boots into
     the **MFC/OS** shell (banner + `]` prompt) in `dos.asm`: read a line (BIOS
     `READ_COMMAND_LINE`), match a verb, dispatch. Verbs: `HELP`, `MON` (launches the
     monitor via the new `K_MON_ENTRY` `$FF1E` BIOS entry), `CATALOG`/`CAT`, `TYPE
     NAME`. The monitor gains `Q` (quit → `DOS_WARM` `$AF1E`). Kernel v3.5. The `@`
     preview + `B:` menu remain reachable through `MON` (retired in 4.2/4.3).
   - **4.2a — DONE.** DOS file verbs in the shell: `SAVE name,SSSS-EEEE` (writes the
     `.PRG` 2-byte load-address header then the range), `LOAD name[,AAAA]` (loads to
     the header's address, or an override), `ERASE name`, `RENAME old,new`. New
     `FS_RENAME` (DOS ABI `$AF21`) + a shared `_DOS_DIR_FIND_EXISTING` helper.
     Kernel v3.5.1 (MONITOR_MAIN resets its display state on launch). Covered by
     DOS round-trip cases in `monitor_integration` and `FS_RENAME` cases in
     `dos_fat16_tests`.
   - **4.2c — DONE.** Host bridge moved into the DOS as `IMPORT name` / `EXPORT name`
     (host file picker <-> a FAT16 file, reusing the PIA byte-stream that `L:`/`S:`
     used, now bridged to the FS via `FS_PUTB`/`FS_GETB`). The monitor's `L:`/`S:`
     host load/save are retired: removed from `CMD_INDEX_MAP` + help, and their
     handlers (`PARSE_CMD_LOAD`/`SAVE_CHECK`, `CMD_LOAD_FILE`, `CMD_SAVE_FILE`)
     excised - freeing ~180 bytes of kernel ROM (the two vacated jump-table slots
     map to a no-op). Kernel v3.7.
   - **4.2b — DONE.** Retired the temporary `@` preview: removed its dispatch,
     `CMD_CATALOG`/`CMD_TYPE`/`CMD_SAVE`/`CMD_ERASE` routines, the `FS_*`/`DOS_DIR_ENTRY`
     equates, and the `MSG_DOS_*` strings from `kernel.asm` (~550 bytes freed). The
     monitor is a pure debugger again; the DOS shell owns the file verbs. Kernel v3.6.
     The obsolete monitor `@` tests were removed (coverage is the DOS-level tests).
   - **4.3** — launch-by-name. Decisions: `ASM` launch name, ROM-module-first with
     `&NAME` override to force a disk program, programs return via `RTS`.
     - *4.3a — DONE.* `RETURN_FROM_MODULE` (`$FF12`) now re-enters `DOS_WARM` (monitor-
       state save/restore dropped); new `K_LAUNCH_BY_NAME` ABI (`$FF21`) scans
       `MODULE_DIR` and `BANK_LAUNCH`es a match (assembler's name is `ASM`); the DOS
       resolves an unmatched verb to a module. `BASIC`/`ASM` run from `]` and return
       to `]`. The monitor `B:` bank menu is excised (`CMD_BANK_MENU`/`PARSE_CMD_BASIC`
       removed). Kernel v3.8.
     - *4.3b — DONE.* Disk `.PRG` launch (`_DOS_RUN_FILE`): `FS_OPEN` the name, read
       the 2-byte load-address header, load the body there, then run it as a
       subroutine — clean stack with a `DOS_WARM` return pushed, so the program's
       `RTS` returns to `]`. A leading `&` forces this disk path over a same-named
       module. Unknown name → `COMMAND NOT FOUND`. **Closes the loop:** assemble in
       `ASM` → `SAVE NAME,start-end` → type `NAME` to run it.
5. **Editor** (module, bank) — full-screen, generic; edit/save FS files → full
   in-machine self-hosting (edit → assemble → run, all at the DOS).
6. *(Later/optional)* relocate the monitor to a bank; kernel ROM becomes a lean BIOS.

The FS phases (1–3) are foundational and unchanged regardless of the DOS framing; the
pivot mainly reshapes phase 4 (a DOS shell, not file-verbs bolted onto the monitor)
and flips the boot target.

### Settled decisions
- Boot into the **DOS**; monitor becomes a launchable tool (`MON`).
- **BIOS / DOS / monitor** three-way split; BIOS always resident, never banked.
- Storage = host `disk.img`, 512-byte sectors, simple block-device registers (not SPI).
- Filesystem = **FAT16** (host-mountable).
- DOS commands: **`CATALOG`, `LOAD`, `SAVE`, `ERASE`, `RENAME`, `TYPE`**.
- **Unified launch-by-name** (command → ROM module → disk file); no `BANKS` menu, no
  `RUN` verb.
- Program file format = **2-byte load-address header** (`.PRG`-style).
- Space = add an always-mapped **DOS ROM at `$9000-$AFFF`**; user RAM → `$0800-$8FFF`.
- Editor = full-screen, generic, a bank.

### Open questions
- OS name + boot prompt (Identity above).
- Image creation/format — the `tools/mkfat16` host tool creates a genuine FAT16
  image (>= 4085 clusters; ~2 MB), confirmed mountable read/write by macOS
  (`fsck_msdos` clean, `hdiutil attach` exchanges files both ways). An in-machine
  `FORMAT` is still a possible later addition.
- `BLK_LBA` width (16-bit/32 MB vs wider).
- Nested subdirectories, multiple open files, long names — deferred. (One level of
  drawer *is* implemented, and a drawer may span several clusters.)
- ~~Editor cursor addressing~~ — **settled**: `EDIT` draws straight to screen RAM and
  paints its own reverse-video block cursor, hiding the kernel's hardware cursor
  while editing and restoring it on exit. No `K_SET_CURSOR` ABI entry was added.
- Whether/when to do the monitor-to-bank relocation.

---

## Part 5 — MFC BASIC label glossary


A reference for the cryptic `LAB_<hex>` labels in `src/kernel/basic.asm`. The
interpreter — MFC BASIC — is derived from EhBASIC (see `NOTICE`). Rather than
rename ~780 code labels in place (a large, error-prone change), the labels are
left untouched and this file maps them to meaning.

**Descriptions are taken verbatim from the inline comments in `basic.asm`.** A
`LAB_<hex>` label's hex suffix is its assembled address (e.g. `LAB_1274` lives at
`$1274`). Most zero-page variables already have mnemonic names (`Bpntr`,
`Baslnl`, `FAC1`, …) and are documented in the source, so they are not repeated
here.

### Zero-page entry vectors / equates

| Label | Value | Meaning |
|-------|-------|---------|
| `LAB_WARM` | `$00` | BASIC warm start entry point |
| `LAB_IGBY` | `$BC` | "get next BASIC byte" subroutine (in zero page) |
| `LAB_GBYT` | `$C2` | "get current BASIC byte" at the text pointer |
| `LAB_STAK` | `$0100` | stack bottom (page 1), no offset |

### Named routine labels

| Label | Meaning |
|-------|---------|
| `LAB_A2HX` | convert A to ASCII hex byte and output .. note set decimal mode before calling |
| `LAB_ABS` | perform ABS() |
| `LAB_ADD` | add FAC2 to FAC1 |
| `LAB_AND` | perform AND |
| `LAB_ASC` | perform ASC() |
| `LAB_ATN` | perform ATN() |
| `LAB_AYFC` | save and convert integer AY to FAC1 |
| `LAB_BHSS` | process numeric expression(s) for BIN$ or HEX$ |
| `LAB_BINS` | perform BIN$() |
| `LAB_BITCLR` | perform BITCLR |
| `LAB_BITSET` | perform BITSET |
| `LAB_BTST` | perform BITTST() |
| `LAB_BYE` | This command exits BASIC and returns control to the monitor at $FF12 |
| `LAB_CALL` | perform CALL |
| `LAB_CASC` | check byte, return C=0 if<"A" or >"Z" or "a" to "z" |
| `LAB_CBIN` | get binary number |
| `LAB_CHEX` | get hex number |
| `LAB_CHRS` | perform CHR$() |
| `LAB_CKIN` | check whichever interrupt is indexed by X |
| `LAB_CKRN` | check not Direct (used by DEF and INPUT) |
| `LAB_CKTM` | type match check, set C for string, clear C for numeric |
| `LAB_CLEAR` | perform CLEAR |
| `LAB_COLD` | new page 2 initialisation, copy block to ccflag on |
| `LAB_CONT` | perform CONT |
| `LAB_COS` | perform COS() |
| `LAB_CRLF` | print CR/LF |
| `LAB_CTNM` | check if source is numeric, else do type mismatch |
| `LAB_CTST` | check if source is string, else do type mismatch |
| `LAB_DATA` | perform DATA |
| `LAB_DEC` | perform DEC |
| `LAB_DEEK` | perform DEEK() |
| `LAB_DEF` | perform DEF |
| `LAB_DIM` | perform DIM |
| `LAB_DO` | perform DO |
| `LAB_DOKE` | perform DOKE |
| `LAB_EOR` | pointers and offsets afterwards! |
| `LAB_EQUAL` | do = compare |
| `LAB_ESGL` | evaluate string, get length in Y |
| `LAB_EVBY` | evaluate byte expression, result in X |
| `LAB_EVEX` | evaluate expression |
| `LAB_EVIN` | evaluate integer expression |
| `LAB_EVIR` | evaluate integer expression (no sign check) |
| `LAB_EVNM` | evaluate expression and check is numeric, else do type mismatch |
| `LAB_EVPI` | evaluate integer expression (no check) |
| `LAB_EVST` | evaluate string |
| `LAB_EXP` | perform EXP()   (x^e) |
| `LAB_F2FX` | save unsigned 16 bit integer part of FAC1 in temporary integer |
| `LAB_FCER` | do function call error |
| `LAB_FOR` | perform FOR |
| `LAB_FRE` | perform FRE() |
| `LAB_FTBL` | action addresses for functions |
| `LAB_FTPL` | function pre process routine table |
| `LAB_GADB` | get two parameters for POKE or WAIT |
| `LAB_GARB` | garbage collection routine |
| `LAB_GET` | perform GET |
| `LAB_GFPN` | get fixed-point number into temp integer |
| `LAB_GMEM` | copy block from StrTab to $0000 - $0012 |
| `LAB_go_search` | search for line # in temp (Itempl/Itemph) from (AX) |
| `LAB_GOSUB` | perform GOSUB |
| `LAB_GOTO` | perform GOTO |
| `LAB_GTBY` | get byte parameter |
| `LAB_GTHAN` | do - FAC1 |
| `LAB_GVAL` | get value from line |
| `LAB_GVAR` | return pointer to variable in Cvaral/Cvarah |
| `LAB_HEXS` | perform HEX$() |
| `LAB_IF` | perform IF |
| `LAB_INC` | perform INC |
| `LAB_INLN` | print "? " and get BASIC input |
| `LAB_INPUT` | perform INPUT |
| `LAB_INT` | perform INT() |
| `LAB_IRQ` | perform IRQ {ON|OFF|CLEAR} |
| `LAB_KEYT` | note if length is 1 then the pointer is ignored |
| `LAB_LCASE` | perform LCASE$() |
| `LAB_LEFT` | perform LEFT$() |
| `LAB_LENS` | perform LEN() |
| `LAB_LET` | perform LET |
| `LAB_LIST` | bigger, faster version (a _lot_ faster) |
| `LAB_LOG` | perform LOG() |
| `LAB_LOOP` | perform LOOP |
| `LAB_LRMS` | process string for LEFT$, RIGHT$ or MID$ |
| `LAB_LSHIFT` | perform << (left shift) |
| `LAB_LTHAN` | do < compare |
| `LAB_MAX` | perform MAX() |
| `LAB_MIDS` | perform MID$() |
| `LAB_MIN` | perform MIN() |
| `LAB_MMEC` | check for correct exit, else so syntax error |
| `LAB_MSSP` | A=length, X=Sutill=ptr low byte, Y=Sutilh=ptr high byte |
| `LAB_NEW` | perform NEW |
| `LAB_NEXT` | perform NEXT |
| `LAB_NMI` | perform NMI {ON|OFF|CLEAR} |
| `LAB_no_ELSE` | following ELSE will, correctly, cause a syntax error |
| `LAB_NULL` | perform NULL |
| `LAB_OMER` | do "Out of memory" error then warm start |
| `LAB_ON` | perform ON |
| `LAB_OPPT` | hierarchy and action addresses for operator |
| `LAB_OR` | perform OR |
| `LAB_PEEK` | perform PEEK() |
| `LAB_PFAC` | pack FAC1 into (Lvarpl) |
| `LAB_PHFA` | this is the routine that does most of the work |
| `LAB_PI` | perform PI |
| `LAB_POKE` | perform POKE |
| `LAB_POS` | perform POS() |
| `LAB_POWER` | perform power function |
| `LAB_PPBI` | set numeric data type and increment BASIC execute pointer |
| `LAB_PPFN` | process numeric expression in parenthesis |
| `LAB_PPFS` | process string expression in parenthesis |
| `LAB_PRNA` | note! some routines expect this one to exit with Zb=0 |
| `LAB_READ` | perform READ |
| `LAB_REM` | perform REM, skip (rest of) line |
| `LAB_reset_search` | search for line # in temp (Itempl/Itemph) from start of mem pointer (Smeml) |
| `LAB_RESTORE` | perform RESTORE |
| `LAB_RETIRQ` | perform RETIRQ |
| `LAB_RETNMI` | perform RETNMI |
| `LAB_RETURN` | perform RETURN |
| `LAB_RIGHT` | perform RIGHT$() |
| `LAB_RND` | Serial correlation coefficient is -0.000370, totally uncorrelated would be 0.0 |
| `LAB_RSHIFT` | perform >> (right shift) |
| `LAB_RTST` | put string address and length on descriptor stack and update stack pointers |
| `LAB_RUN` | perform RUN |
| `LAB_SADD` | perform SADD() |
| `LAB_SCCA` | scan for CHR$(A) , else do syntax error then warm start |
| `LAB_SCGB` | scan for "," and get byte, else do Syntax error then warm start |
| `LAB_SGBY` | scan and get byte parameter |
| `LAB_SGN` | perform SGN() |
| `LAB_SHLN` | old 541 new 507 |
| `LAB_SIN` | perform SIN() |
| `LAB_SIRQ` | perform ON IRQ |
| `LAB_SNBL` | returns Y as index to [EOL] |
| `LAB_SNBS` | returns Y as index to [:] or [EOL] |
| `LAB_SNER` | syntax error then warm start |
| `LAB_SNMI` | perform ON NMI |
| `LAB_SQR` | perform SQR() |
| `LAB_SSLN` | search Basic for temp integer line number from start of mem |
| `LAB_STFA` | set exp=X, clearFAC1 mantissa3 and normalise |
| `LAB_STOP` | perform STOP |
| `LAB_STRS` | perform STR$() |
| `LAB_SUBTRACT` | perform subtraction, FAC1 from FAC2 |
| `LAB_SWAP` | perform SWAP |
| `LAB_TAN` | perform TAN() |
| `LAB_TWOPI` | perform TWOPI |
| `LAB_UCASE` | perform UCASE$() |
| `LAB_UFAC` | unpack memory (AY) into FAC1 |
| `LAB_USR` | perform USR() |
| `LAB_VAL` | perform VAL() |
| `LAB_VARPTR` | perform VARPTR() |
| `LAB_WAIT` | perform WAIT |
| `LAB_WDTH` | perform WIDTH |
| `LAB_XERR` | do error #X, then warm start |

### Numeric labels (`LAB_<hex>`, sorted by address)

| Label | Address | Meaning |
|-------|---------|---------|
| `LAB_11A1` | `$11A1` | exit with z=1 if FOR else exit with z=0 |
| `LAB_1212` | `$1212` | stack too deep? do OM error |
| `LAB_121F` | `$121F` | addr to check is in AY (low/high) |
| `LAB_1274` | `$1274` | wait for Basic command |
| `LAB_127D` | `$127D` | wait for Basic command (no "Ready") |
| `LAB_1295` | `$1295` | handle new BASIC line |
| `LAB_1357` | `$1357` | call for BASIC input (main entry point) |
| `LAB_138E` | `$138E` | announce buffer full |
| `LAB_13A6` | `$13A6` | faster, dictionary search version .... |
| `LAB_13D1` | `$13D1` | have matched first character of some keyword |
| `LAB_1477` | `$1477` | reset execution to start, clear vars and flush stack |
| `LAB_147A` | `$147A` | "CLEAR" command gets here |
| `LAB_1491` | `$1491` | flush stack and clear continue flag |
| `LAB_15C2` | `$15C2` | interpreter inner loop |
| `LAB_15FF` | `$15FF` | interpret BASIC code from (Bpntrl) |
| `LAB_1629` | `$1629` | key press is detected. |
| `LAB_1636` | `$1636` | if there was a key press it gets back here .. |
| `LAB_1696` | `$1696` | does RUN n |
| `LAB_16D0` | `$16D0` | search for line # in temp (Itempl/Itemph) from start of mem pointer (Smeml) |
| `LAB_16D4` | `$16D4` | search for line # in temp (Itempl/Itemph) from (AX) |
| `LAB_16F4` | `$16F4` | do the return without gosub error |
| `LAB_174E` | `$174E` | perform ELSE after IF |
| `LAB_1753` | `$1753` | found the matching ELSE, now do <{n|statement}> |
| `LAB_176B` | `$176B` | next character was GOTO or GOSUB |
| `LAB_17D5` | `$17D5` | string LET |
| `LAB_1829` | `$1829` | perform PRINT |
| `LAB_18C3` | `$18C3` | print null terminated string from memory |
| `LAB_18C6` | `$18C6` | print string from Sutill/Sutilh |
| `LAB_18E0` | `$18E0` | print " " |
| `LAB_18E3` | `$18E3` | print "?" character |
| `LAB_1904` | `$1904` | handle bad input data |
| `LAB_1B5B` | `$1B5B` | push sign, round FAC1 and put on stack |
| `LAB_1B78` | `$1B78` | do functions |
| `LAB_1BC1` | `$1BC1` | print "..." string to string util area |
| `LAB_1BD0` | `$1BD0` | do tokens |
| `LAB_1BFB` | `$1BFB` | scan for ")" , else do syntax error then warm start |
| `LAB_1BFE` | `$1BFE` | scan for "(" , else do syntax error then warm start |
| `LAB_1C01` | `$1C01` | scan for "," , else do syntax error then warm start |
| `LAB_1C11` | `$1C11` | set-up for functions |
| `LAB_1C18` | `$1C18` | get (var), return value in FAC_1 and $ flag |
| `LAB_1C27` | `$1C27` | for functions that returned strings |
| `LAB_1D82` | `$1D82` | check byte, return C=0 if<"A" or >"Z" |
| `LAB_1DE6` | `$1DE6` | set Adatal,Adatah to Astrtl,Astrth+2*Dimcnt+#$05 |
| `LAB_1E17` | `$1E17` | find or make array |
| `LAB_1E1F` | `$1E1F` | now get the array dimension(s) and stack it (them) before the data type and DIM flag |
| `LAB_1E5C` | `$1E5C` | no arrays). |
| `LAB_1E85` | `$1E85` | do array bounds error |
| `LAB_1F28` | `$1F28` | we have found, or built, the array. now we need to find the element |
| `LAB_1F7C` | `$1F7C` | does XY = (Astrtl),Y * (Asptl) |
| `LAB_1FD0` | `$1FD0` | convert Y to byte in FAC1 |
| `LAB_200B` | `$200B` | check FNx syntax |
| `LAB_2074` | `$2074` | restore Bpntrl,Bpntrh and function variable from stack |
| `LAB_207A` | `$207A` | put execute pointer and variable pointer into function |
| `LAB_209C` | `$209C` | copy des_pl/h to des_2l/h and make string space A bytes long |
| `LAB_20AE` | `$20AE` | print " terminated string to Sutill/Sutilh |
| `LAB_20B4` | `$20B4` | source is AY |
| `LAB_20F8` | `$20F8` | put string address and length on descriptor stack and update stack pointers |
| `LAB_2115` | `$2115` | return X=Sutill=ptr low byte, Y=Sutill=ptr high byte |
| `LAB_214B` | `$214B` | re-run routine from last ending |
| `LAB_21D1` | `$21D1` | return with XA = next variable pointer |
| `LAB_2216` | `$2216` | search complete, now either exit or set-up and move string |
| `LAB_224D` | `$224D` | add strings, string 1 is in descriptor des_pl, string 2 is in line |
| `LAB_228A` | `$228A` | copy string from descriptor (sdescr) to (Sutill) |
| `LAB_2298` | `$2298` | store string A bytes long from YX to (Sutill) |
| `LAB_229C` | `$229C` | store string A bytes long from (ut1_pl) to (Sutill) |
| `LAB_22B6` | `$22B6` | returns with A = length, X=pointer low byte, Y=pointer high byte |
| `LAB_22BA` | `$22BA` | returns with A = length, X=ut1_pl=pointer low byte, Y=ut1_ph=pointer high byte |
| `LAB_22EB` | `$22EB` | checks if AY is on the descriptor stack, if so does a stack discard |
| `LAB_236F` | `$236F` | return pointer in des_2l/h, byte in A (and X), Y=0 |
| `LAB_23A8` | `$23A8` | do function call error then warm start |
| `LAB_23F3` | `$23F3` | restore BASIC execute pointer from temp (Btmpl/Btmph) |
| `LAB_244E` | `$244E` | add 0.5 to FAC1 |
| `LAB_2455` | `$2455` | perform subtraction, FAC1 from (AY) |
| `LAB_2467` | `$2467` | perform addition |
| `LAB_246C` | `$246C` | add (AY) to FAC1 |
| `LAB_24D0` | `$24D0` | do ABS and normalise FAC1 |
| `LAB_24D5` | `$24D5` | normalise FAC1 |
| `LAB_24F1` | `$24F1` | clear FAC1 exponent and sign |
| `LAB_24F5` | `$24F5` | save FAC1 sign |
| `LAB_24F8` | `$24F8` | add FAC2 mantissa to FAC1 mantissa |
| `LAB_251B` | `$251B` | normalise FAC1 |
| `LAB_2528` | `$2528` | test and normalise FAC1 for C=0/1 |
| `LAB_252A` | `$252A` | normalise FAC1 for C=1 |
| `LAB_2537` | `$2537` | negate FAC1 |
| `LAB_253D` | `$253D` | twos complement FAC1 mantissa |
| `LAB_2559` | `$2559` | increment FAC1 mantissa |
| `LAB_2564` | `$2564` | do overflow error (overflow exit) |
| `LAB_2569` | `$2569` | shift FCAtemp << A+8 times |
| `LAB_257B` | `$257B` | shift FACX -A times right (> 8 shifts) |
| `LAB_2592` | `$2592` | shift FACX Y times right |
| `LAB_25FB` | `$25FB` | do convert AY, FCA1*(AY) |
| `LAB_264D` | `$264D` | unpack memory (AY) into FAC2 |
| `LAB_2673` | `$2673` | test and adjust accumulators |
| `LAB_2690` | `$2690` | handle overflow and underflow |
| `LAB_269E` | `$269E` | multiply by 10 |
| `LAB_26B9` | `$26B9` | divide by 10 |
| `LAB_26C2` | `$26C2` | divide by (AY) (X=sign) |
| `LAB_26CA` | `$26CA` | convert AY and do (AY)/FAC1 |
| `LAB_272B` | `$272B` | do A<<6, save as FAC1 rounding byte, normalise and return |
| `LAB_2737` | `$2737` | do "Divide by zero" error |
| `LAB_273C` | `$273C` | copy temp to FAC1 and normalise |
| `LAB_276E` | `$276E` | pack FAC1 into Adatal |
| `LAB_2778` | `$2778` | pack FAC1 into (XY) |
| `LAB_279B` | `$279B` | copy FAC2 to FAC1 |
| `LAB_279D` | `$279D` | save FAC1 sign and copy ABS(FAC2) to FAC1 |
| `LAB_27AB` | `$27AB` | round and copy FAC1 to FAC2 |
| `LAB_27AE` | `$27AE` | copy FAC1 to FAC2 |
| `LAB_27BA` | `$27BA` | round FAC1 |
| `LAB_27C2` | `$27C2` | round FAC1 (no check) |
| `LAB_27CA` | `$27CA` | return A=FF,C=1/-ve A=01,C=0/+ve |
| `LAB_27CE` | `$27CE` | no = 0 check |
| `LAB_27D0` | `$27D0` | no = 0 check, sign in A |
| `LAB_27DB` | `$27DB` | save A as integer byte |
| `LAB_27E3` | `$27E3` | set exp=X, clearFAC1 mantissa3 and normalise |
| `LAB_27F8` | `$27F8` | returns A=$FF if FAC1 < (AY) |
| `LAB_2828` | `$2828` | gets here if number <> FAC1 |
| `LAB_2831` | `$2831` | convert FAC1 floating-to-fixed |
| `LAB_2851` | `$2851` | shift FAC1 A times right |
| `LAB_287F` | `$287F` | clear FAC1 and return |
| `LAB_2887` | `$2887` | starting with "$" and "%" respectively |
| `LAB_289A` | `$289A` | get FAC1 from string .. first character wasn't numeric or - |
| `LAB_289C` | `$289C` | was "+" or "-" to start, so get next character |
| `LAB_289D` | `$289D` | code here for hex and binary numbers |
| `LAB_28A3` | `$28A3` | get FAC1 from string .. character wasn't numeric, -, +, hex or binary |
| `LAB_28FB` | `$28FB` | do - FAC1 and return |
| `LAB_28FE` | `$28FE` | do unsigned FAC1*10+number |
| `LAB_2912` | `$2912` | evaluate new ASCII digit |
| `LAB_2925` | `$2925` | evaluate next character of exponential part of number |
| `LAB_2953` | `$2953` | print " in line [LINE #]" |
| `LAB_295E` | `$295E` | print XA as unsigned integer |
| `LAB_296E` | `$296E` | not any more, moved scratchpad to page 0 |
| `LAB_29C0` | `$29C0` | now we have just the digits to do |
| `LAB_2A9A` | `$2A9A` | This table is used in converting numbers to ASCII. |
| `LAB_2B6E` | `$2B6E` | ^2 then series evaluation |
| `LAB_2B84` | `$2B84` | series evaluation |
| `LAB_2CEE` | `$2CEE` | increment and scan memory |
| `LAB_2CF4` | `$2CF4` | scan memory |

---

## Part 6 — Host GUI

 # 6502 Monitor UI Layout - Status Sidebar (IMPLEMENTED)

### Current printStatus() Console Output
```
  Current Byte: 0xXX
CPU Status:
  A: 0xXX
  X: 0xXX  
  Y: 0xXX
  PC: 0xXXXX
  SP: 0xXX
  P: 0xXX  <- Replace with individual flag bits
  Cycles: XXXXX  <- Already in status bar, remove
```

### Current UI Layout (52x25 Characters - Wider Window)

```
+----------------------------------------+----------+
|                                        |   CPU    | Row 0
|                                        |   0x4C   | Row 1 - Current Byte
|                                        +----------+
|                                        | A: 0x00  | Row 2
|                                        | X: 0x00  | Row 3  
|         6502 Monitor Display           | Y: 0x00  | Row 4
|         (Full 40 characters)           |PC: 8000  | Row 5
|       *** DIRECT INPUT ENABLED ***     |SP: 0xFF  | Row 6
|     Click display to focus, type       +----------+
|     commands directly on screen        |NV-BDIZC  | Row 7 - Flag names
|                                        |01010001  | Row 8 - Flag values
|                                        |          | Row 9
|                                        |          | Row 10
|                                        |          | Rows 11-22
|                                        |          | (Reserved)
+----------------------------------------+----------+
```

### Status Sidebar Layout (Columns 41-51, 10 chars wide)

#### Section 1: Current State (Rows 0-1)
```
|   CPU    |  - Header
|   0x4C   |  - Current Byte value (mem_.read(reg.PC))
```

#### Section 2: Registers (Rows 2-6)  
```
| A: 0x00  |  - Accumulator
| X: 0x00  |  - X Register
| Y: 0x00  |  - Y Register  
|PC: 8000  |  - Program Counter (4 hex digits)
|SP: 0xFF  |  - Stack Pointer
```

#### Section 3: Processor Flags (Rows 7-8)
```
|NV-BDIZC  |  - Flag bit names (N=Negative, V=oVerflow, B=Break, D=Decimal, I=Interrupt, Z=Zero, C=Carry)
|01010001  |  - Current flag values (0=clear, 1=set)
```

### 6502 Processor Status Flag Mapping
```
Bit 7: N (Negative)
Bit 6: V (Overflow) 
Bit 5: - (Unused, always 1)
Bit 4: B (Break)
Bit 3: D (Decimal)
Bit 2: I (Interrupt)
Bit 1: Z (Zero)
Bit 0: C (Carry)
```

### Implementation Status - COMPLETE

#### Window/UI Framework Changes ✓
- Window width expanded to 52 characters (40 + 12 for sidebar and separator)
- All existing 6502 screen memory at 40x25 preserved unchanged
- Status sidebar renders outside the 6502 screen memory area

#### Direct Input Implementation ✓
- **DisplayWidget Enhanced**: Now accepts keyboard input directly
- **QLineEdit Removed**: No separate input field needed
- **Focus-based Input**: Click display to focus, type commands directly
- **Cursor Display**: Blinking cursor appears when display has focus
- **Complete Key Support**: Letters, numbers, Enter, Backspace, special keys

#### Console Output Replacement ✓
- `printStatus()` updates sidebar labels instead of console output
- Status sidebar rendered by UI framework, not 6502 assembly
- Fixed position labels that don't scroll
- Individual flag bits displayed instead of hex P register value
- All console debug output removed for performance

#### Direct Input System (Implemented)
```cpp
// DisplayWidget enhanced with keyboard input
class DisplayWidget : public QWidget {
    Q_OBJECT
signals:
    void keyPressed(uint8_t ascii_code);  // Emits on any key press
    
protected:
    void keyPressEvent(QKeyEvent* event) override;      // Handles Qt key events
    void focusInEvent(QFocusEvent* event) override;     // Shows cursor
    void focusOutEvent(QFocusEvent* event) override;    // Hides cursor
    
private:
    uint8_t qtKeyToAscii(QKeyEvent* event) const;       // Qt key → ASCII conversion
    void drawCursor(QPainter& painter);                 // Draws blinking cursor
    void blinkCursor();                                 // Timer-based cursor blink
};

// MainWindow connection
void onDisplayKeyPressed(uint8_t ascii_code) {
    computer_->getPia()->addKeypress(ascii_code);       // Direct to PIA system
}
```

#### Status Update Functions (Implemented)
```cpp
void updateCpuStatusSidebar();    // Single consolidated function updates all:
                                  // - CPU header, current byte, registers
                                  // - Individual processor flag bits
                                  // - Real-time updates via QTimer
```

#### Display Area Implementation ✓
- **6502 monitor display**: Full 40 characters (columns 0-39) preserved
- **Status sidebar**: Columns 41-51 (10 chars wide) implemented
- **Vertical separator**: Column 40 visual separation
- **Assembly code**: No changes to existing PRINT_CHAR or screen positioning
- **Direct input flow**: DisplayWidget → PIA → kernel.rom → screen display
- **Focus management**: Click display to enable keyboard input
- **Visual feedback**: Blinking cursor indicates input focus

### Benefits - ALL ACHIEVED ✓
- **Real-time CPU state visibility**: Status sidebar updates 10x/second
- **Individual flag bit status**: NV-BDIZC format with 0/1 values
- **No console output**: All debug output removed for performance
- **No assembly changes**: All existing 6502 code preserved exactly
- **Direct input experience**: Type commands directly on authentic 6502 screen
- **Focus-based UI**: Click display to input, visual cursor feedback
- **Complete key support**: Monitor commands (W:, R:, G:, etc.) work perfectly
- **Authentic terminal feel**: True 6502 computer experience
- **Performance optimized**: Console I/O bottleneck eliminated

### Current User Experience
1. **Power On**: Click "Power On" button
2. **Focus Display**: Click the 6502 screen area
3. **Direct Input**: Type monitor commands directly (W:8000 FF, R:8000, etc.)
4. **Real-time Feedback**: CPU status updates live in sidebar
5. **Authentic Feel**: Genuine 6502 terminal experience
---

## Part 7 — External references


Working list of repos, datasheets, docs, and other sources informing the design
of upcoming features — primarily the **in-machine text editor** and **resident
filesystem** (post-v3.2). Paste links and short notes under the relevant heading.
For each entry, a one-line note on *why it's relevant* and (if known) its
**license** is helpful — license fit gates whether we can port/borrow code.

Format suggestion per entry:
```
- <url>
  - what: one line on what it is
  - relevant: why it matters for our design
  - license: e.g. MIT / GPL / BSD / unknown / docs-only
```

---

### Resident filesystem — format & 6502 implementations
(FAT12/16/32, CBM-DOS-style, or a custom format; existing 6502 FS code to study or port.)

- https://github.com/commanderx16/x16-rom/tree/master/dos/fat32
  - what: Commander X16 ROM's FAT32 implementation (6502)
  - relevant: candidate to port/adapt; reference for FAT directory walking, etc.
  - license: (to confirm)



### Storage backing — how the host presents "disk" to the emulated machine
(Single disk-image file? A host directory? An SD-card image? Block-device API the
emulator exposes vs. the current per-op host file dialogs.)
https://mike42.me/blog/2021-12-adding-an-sd-card-reader-to-my-6502-computer
https://mike42.me/blog/2021-12-implementing-the-xmodem-protocol-for-file-transfer
https://6502.org/forum/viewtopic.php?f=2&t=5824
https://github.com/x16community/x16-emulator

### Assemblers and language implementations
Examples of assembler/disassembler programs or other langage interpreters/compilers
https://github.com/Museum-of-Art-and-Digital-Entertainment/macross
https://archive.org/stream/6502MacroAssemblerAndTextEditorForPETAPPLESYM/6502%20Macro%20Assembler%20and%20Text%20Editor%20for%20PET%2C%20APPLE%2C%20SYM_djvu.txt
https://github.com/jefftranter/6502/blob/master/asm/jmon/miniasm.s
https://github.com/mike42/6502-computer/blob/main/rom/basic/basic.s
https://github.com/Klaus2m5/6502_EhBASIC_V2.22/blob/master/basic.asm
https://github.com/davervw/vwas6502
https://mike42.me/blog/2021-09-porting-basic-to-my-6502-computer


### Text editor — implementations & the "feel" we want
(Full-screen vs modal vs line-numbered; text-buffer + screen-redraw approaches.)

- https://turbo.style64.org/docs/turbo-macro-pro-editor
  - what: Turbo Macro Pro editor command reference
  - relevant: full-screen integrated editor model (the gold-standard feel)
  - license: docs-only (TMP itself: to confirm)

- https://sourceforge.net/p/vi65/code/HEAD/tree/trunk/
  - what: vi65 — a vi editor for 6502 systems
  - relevant: a standalone editor port option (modal)
  - license: (to confirm)



### Screen / cursor / terminal handling
(Our kernel I/O is a PRINT_CHAR byte stream with no gotoxy in the ABI; an editor
needs cursor addressing, insert/delete-with-reflow, and scrolling. References on
how editors drive the screen.)




### CPU / hardware datasheets & references
(WDC 65C02, timing, anything relevant to new features.)




### Misc / inspiration
(Other 6502 systems, ROM projects, blog posts, forum threads — anything loosely
relevant, e.g. the broader X16 ROM project.)
https://github.com/haldean/x6502/blob/master/cpu.h
https://github.com/mist64/c64rom/blob/master/kernal/kernal.s
https://github.com/Klaus2m5/6502_65C02_functional_tests/blob/master/6502_interrupt_test.a65
https://github.com/iScsc/6502-assembly/tree/main/src
https://c64os.com/post/c64kernalrom#scr_setmsg

### Unsorted - Please sort these into relevant sections above or create new ones.
https://github.com/davidgiven/cpm65

### Sound — MOS 6581/8580 SID
(Datasheets and reverse-engineering notes behind the software SID. Docs/behavior
only — we wrote the synth from scratch and deliberately did NOT port reSID (GPL).)
- SID 6581/8580 datasheet (register map, waveforms, ADSR rate tables, filter)
  - what: the chip our `Sid` model targets
  - relevant: register layout relocated to `$FE38`; envelope/filter behavior
  - license: docs-only
- reSID (Dag Lem) — studied for *behavior*, NOT copied
  - relevant: reference for combined-waveform / filter nonlinearity notes
  - license: GPL — **do not port code**; behavior reference only

### Games / Assembler Programs to port
https://github.com/jefftranter/6502/tree/master/asm/KIM-1/TheFirstBookOfKIM/Games
https://www.linusakesson.net/software/zeugma/index.php
https://6502.org/source/?product=87
