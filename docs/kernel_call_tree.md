# MFC 6502 Kernel Call Tree

This document traces all JSR (Jump to Subroutine) calls for each monitor command from the main monitor loop through to completion.
It is intended as an aid to developers using the monitor, so that the call tree is easy to follow.

## Main Monitor Loop

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

## Commands (Alphabetical Order)

### C: (Clear Screen) Command
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

### F: (Fill Memory) Command
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

### G: (Go/Run) Command
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

### H: (Help) Command
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

### L: (Load File) Command
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

### M: (Move/Copy Memory) Command
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

### R: (Read Memory) Command
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

### S: (Save File) Command
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

### T: (Stack Dump) Command
```
PARSE_COMMAND
└── PARSE_CMD_STACK
    ├── JSR PARSE_COLON_COMMAND
    └── JSR CMD_DUMP_STACK
        └── JSR DUMP_MEMORY_RANGE
            ├── JSR PRINT_CHAR (multiple times for address/data)
            └── JSR PRINT_NEWLINE_PAGED
```

### W: (Write Mode) Command
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

### X: (Search Memory) Command
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

### Z: (Zero Page Dump) Command
```
PARSE_COMMAND
└── PARSE_CMD_ZERO
    ├── JSR PARSE_COLON_COMMAND
    └── JSR CMD_DUMP_ZERO_PAGE
        └── JSR DUMP_MEMORY_RANGE
            ├── JSR PRINT_CHAR (multiple times for address/data)
            └── JSR PRINT_NEWLINE_PAGED
```

### ESC (Exit Mode) Command
```
READ_COMMAND_LINE
└── (ESC is handled directly in input processing)
    └── JSR CMD_EXIT_MODE
        └── (sets MON_MODE to command mode, no JSR calls)
```

## Utility Functions Call Trees

### Core Parsing Functions
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

### Display Functions
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

### Input Functions
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

## Command Mode vs Interactive Mode

### One-Shot Commands
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

### Interactive Mode Commands
Only one command has persistent interactive mode:
- **W:** Write Mode - Enters `WRITE_MODE_LOOP` until ESC is pressed

## Error Handling

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

## Notes

- All commands return to `MONITOR_LOOP` after completion
- Only W: command enters persistent interactive mode
- The G: command transfers control to user code and may not return
- Paging support prevents screen overflow in memory dump commands
- The '.' command recalls the last successful command from history
- ESC exits any interactive mode and returns to command mode