# MFC 6502 Kernel Call Tree

### This document traces all JSR (Jump to Subroutine) calls for each monitor command from the main monitor loop through to completion.

### This is the main command loop:
```
MONITOR_LOOP
├── JSR PRINT_MONITOR_PROMPT
│   ├── JSR PRINT_CURRENT_ADDRESS
│   │   ├── JSR BYTE_TO_HEX_PAIR (for high byte)
│   │   └── JSR PRINT_CHAR (4 times for address digits)
│   └── JSR PRINT_CHAR (2 times for "> ")
├── JSR READ_COMMAND_LINE
│   ├── JSR GET_KEYSTROKE (multiple times)
│   ├── JSR PRINT_CHAR (echo each character)
│   └── JSR PRINT_NEWLINE
└── JSR PARSE_COMMAND
└── [Command-specific path follows]
```

## K: (Clear Screen) Command
```
PARSE_COMMAND
└── JMP PARSE_CMD_KLEAR
└── JSR CMD_CLEAR_SCREEN
└── JSR CLEAR_SCREEN
```

## S: (Stack Dump) Command
```
PARSE_COMMAND
└── JMP PARSE_CMD_STACK
└── JSR CMD_DUMP_STACK
└── JSR DUMP_MEMORY_RANGE
├── JSR PRINT_CHAR (multiple times for address/data)
└── JSR PRINT_NEWLINE (at end of each line)
```

## Z: (Zero Page Dump) Command
```
PARSE_COMMAND
└── JMP PARSE_CMD_ZERO
└── JSR CMD_DUMP_ZERO_PAGE
└── JSR DUMP_MEMORY_RANGE
├── JSR PRINT_CHAR (multiple times for address/data)
└── JSR PRINT_NEWLINE (at end of each line)
```

## T: (Target Address) Command
```
PARSE_COMMAND
└── JMP PARSE_CMD_TARGET
└── JSR CMD_SHOW_TARGET
├── JSR ADDR_TO_HEX_QUAD
├── JSR PRINT_CHAR (multiple times for address)
├── JSR BYTE_TO_HEX_PAIR
├── JSR PRINT_CHAR (multiple times for data)
└── JSR PRINT_NEWLINE
```

## H: (Help) Command
```
PARSE_COMMAND
└── JMP PARSE_CMD_HELP
└── JSR CMD_SHOW_HELP
├── JSR PRINT_HELP_HEADER
│   └── JSR PRINT_MESSAGE
│       └── JSR PRINT_CHAR (for each character)
├── JSR PRINT_NEWLINE
├── JSR PRINT_HELP_BODY
│   ├── JSR PRINT_MESSAGE (for each help line)
│   │   └── JSR PRINT_CHAR (for each character)
│   └── JSR PRINT_NEWLINE (after each help line)
└── JSR PRINT_NEWLINE
```

## X: (Exit) Command
```
PARSE_COMMAND
└── JMP PARSE_CMD_EXIT
└── JSR CMD_EXIT_MODE
└── (sets MON_MODE to command mode, no JSR calls)
```

## W: (Write Mode) Command
```
PARSE_COMMAND
└── JMP PARSE_CMD_WRITE_CHECK
├── JSR PARSE_COLON_COMMAND
│   └── JSR HEX_QUAD_TO_ADDR
│       └── JSR HEX_PAIR_TO_BYTE (twice)
│           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
└── JSR CMD_WRITE_MODE
├── JSR SHOW_WRITE_ADDRESS
│   ├── JSR ADDR_TO_HEX_QUAD
│   ├── JSR PRINT_CHAR (multiple times)
│   ├── JSR BYTE_TO_HEX_PAIR
│   └── JSR PRINT_NEWLINE
└── JSR WRITE_MODE_LOOP
└── [Write Mode Loop - see below]
```

## Write Mode Loop
```
WRITE_MODE_LOOP
├── JSR READ_COMMAND_LINE
│   ├── JSR GET_KEYSTROKE (multiple times)
│   ├── JSR PRINT_CHAR (echo)
│   └── JSR PRINT_NEWLINE
├── JSR HEX_PAIR_TO_BYTE (for each hex pair entered)
│   └── JSR HEX_CHAR_TO_NIBBLE (twice)
├── JSR DUMP_MEMORY_RANGE (to show modified memory)
│   ├── JSR PRINT_CHAR (multiple times)
│   └── JSR PRINT_NEWLINE
└── JSR PRINT_ERROR_MSG (on error)
├── JSR PRINT_MESSAGE
│   └── JSR PRINT_CHAR (multiple times)
└── JSR PRINT_NEWLINE
```
## R: (Read Mode) Command
```
PARSE_COMMAND
└── JMP PARSE_CMD_READ_CHECK
├── JSR PARSE_COLON_COMMAND
│   └── JSR HEX_QUAD_TO_ADDR
│       └── JSR HEX_PAIR_TO_BYTE (twice, up to 4 times for range)
│           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
└── JSR CMD_READ_MODE
├── JSR READ_MEMORY_RANGE (for range) or JSR SHOW_READ_ADDRESS (single)
│   └── JSR DUMP_MEMORY_RANGE or JSR SHOW_WRITE_ADDRESS
│       ├── JSR PRINT_CHAR (multiple times)
│       └── JSR PRINT_NEWLINE
└── JSR READ_MODE_LOOP
└── [Read Mode Loop - see below]
```

## Read Mode Loop
```
READ_MODE_LOOP
├── JSR READ_COMMAND_LINE
│   ├── JSR GET_KEYSTROKE (multiple times)
│   ├── JSR PRINT_CHAR (echo)
│   └── JSR PRINT_NEWLINE
└── JSR PRINT_ERROR_MSG (on error)
```
## G: (Go/Run) Command
```
PARSE_COMMAND
└── JMP PARSE_CMD_GO_CHECK
├── JSR PARSE_COLON_COMMAND
│   └── JSR HEX_QUAD_TO_ADDR
│       └── JSR HEX_PAIR_TO_BYTE (twice)
│           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
└── JSR CMD_GO_MODE
├── JSR PRINT_RUN_MESSAGE
│   ├── JSR PRINT_MESSAGE
│   │   └── JSR PRINT_CHAR (multiple times)
│   ├── JSR ADDR_TO_HEX_QUAD
│   ├── JSR PRINT_CHAR (4 times for address)
│   └── JSR PRINT_NEWLINE
└── JMP (MON_CURRADDR_LO) [transfers control to user program]
```
## L: (Load) Command
```
PARSE_COMMAND
└── JMP PARSE_CMD_LOAD_CHECK
├── JSR PARSE_COLON_COMMAND
│   └── JSR HEX_QUAD_TO_ADDR
│       └── JSR HEX_PAIR_TO_BYTE (twice)
│           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
└── JSR CMD_LOAD_MODE
├── JSR SHOW_LOAD_ADDRESS
│   ├── JSR PRINT_MESSAGE
│   │   └── JSR PRINT_CHAR (multiple times)
│   ├── JSR ADDR_TO_HEX_QUAD
│   ├── JSR PRINT_CHAR (4 times for address)
│   └── JSR PRINT_NEWLINE
└── JSR LOAD_MODE_LOOP
└── [Load Mode Loop - see below]
```
## Load Mode Loop
```
LOAD_MODE_LOOP
├── JSR PRINT_MESSAGE (filename prompt)
│   └── JSR PRINT_CHAR (multiple times)
├── JSR READ_COMMAND_LINE
│   ├── JSR GET_KEYSTROKE (multiple times)
│   ├── JSR PRINT_CHAR (echo)
│   └── JSR PRINT_NEWLINE
├── JSR PRINT_MESSAGE (success message)
│   └── JSR PRINT_CHAR (multiple times)
├── JSR PRINT_NEWLINE
└── JSR PRINT_ERROR_MSG (on error)
├── JSR PRINT_MESSAGE
└── JSR PRINT_NEWLINE
```

## Utility Functions Call Trees
```
BYTE_TO_HEX_PAIR

BYTE_TO_HEX_PAIR
└── (uses HEX_LOOKUP_TABLE, no JSR calls)

HEX_CHAR_TO_NIBBLE

HEX_CHAR_TO_NIBBLE
└── (arithmetic operations only, no JSR calls)

PRINT_MESSAGE

PRINT_MESSAGE
└── JSR PRINT_CHAR (for each character until null terminator)

PRINT_CHAR

PRINT_CHAR
└── (direct screen memory writes, no JSR calls)
```

## Special handling for:
- ASCII_CR: Updates cursor position
- ASCII_BACKSPACE: Moves cursor back
```
GET_KEYSTROKE

GET_KEYSTROKE
└── (polls hardware directly, no JSR calls)
```

## Notes
#### All commands return to MONITOR_LOOP after completion
#### Mode commands (W:, R:, L:) enter interactive loops that continue until X: is entered
#### The G: command transfers control to user code and may not return
#### Error conditions typically result in JSR PRINT_ERROR_MSG followed by return to appropriate loop
#### Many routines use the zero-page pointers for indirect addressing but don't make JSR calls



