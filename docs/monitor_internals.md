# MFC Monitor Internals

How to add a command to the machine-language monitor. This covers character
assignment, the jump table, the parsing helpers, where monitor variables may
live, the help and message systems, and the house patterns. The monitor's user
manual is [MONITOR.md](MONITOR.md).

---

### Overview

This document is a guide to implementing new commands in the monitor. It covers
the components, patterns and integration points a new command has to touch. The
monitor is module bank 4, so everything here lives in `src/kernel/monitor.asm`
and the assembler it includes, `src/kernel/assembler/assembler.inc`.

### Table of Contents

1. [Command Character Assignment](#1-command-character-assignment)
2. [Jump Table Integration](#2-jump-table-integration)
3. [Parsing Infrastructure](#3-parsing-infrastructure)
4. [Memory Allocation](#4-memory-allocation)
5. [Help System Integration](#5-help-system-integration)
6. [Message System](#6-message-system)
7. [Implementation Patterns](#7-implementation-patterns)
8. [Testing and Validation](#8-testing-and-validation)
9. [Optional Components](#9-optional-components)

---

### 1. Command Character Assignment

#### Available Command Letters

`PARSE_COMMAND` handles a handful of characters directly and then falls through
to a table lookup for the letters `A` to `Z` (ASCII `$41` to `$5A`). Anything
outside that range is a syntax error.

Handled before the table:

| Character | Meaning |
|---|---|
| `?` | help |
| `Q` | quit the monitor back to the DOS prompt |
| ESC | a clean no-op at the command prompt |
| `#:` | decimal to hexadecimal |
| `$:` | hexadecimal to decimal |

Dispatched through `CMD_INDEX_MAP`:

| Letter | Command |
|---|---|
| `A` | line assembler |
| `B` | build the loaded source |
| `C` | clear screen |
| `D` | disassemble |
| `F` | fill memory |
| `G` | go, run a program |
| `L` | load a source file |
| `M` | move or copy memory |
| `R` | read memory |
| `T` | stack dump |
| `W` | write to memory |
| `X` | search memory |
| `Z` | zero-page dump |

The free letters are `E`, `H`, `I`, `J`, `K`, `N`, `O`, `P`, `S`, `U`, `V` and
`Y`.

#### Command Types

Commands fall into two categories. Simple commands are a single character, as
`?` and `Q` are. Parameterised commands take colon syntax, as `W:8000` and
`R:8000-8FFF` do. Most letters in the table take the colon form even when they
have no parameters, because they run `PARSE_COLON_COMMAND` first and it insists
on the colon.

---

### 2. Jump Table Integration

#### Required Table Updates

Adding a command means updating three tables, and the index a letter maps to has
to match the slot the handler occupies.

##### A. `CMD_JUMP_COMPACT_LO`

```assembly
CMD_JUMP_COMPACT_LO:
    .BYTE <PARSE_CMD_BUILD      ; 0 - 'B' (build the loaded source)
    .BYTE <PARSE_CMD_CLEAR      ; 1 - 'C'
    .BYTE <PARSE_CMD_FILL_CHECK ; 2 - 'F'
    .BYTE <PARSE_CMD_GO_CHECK   ; 3 - 'G'
    .BYTE <PARSE_CMD_HELP       ; 4 - unused (help is the '?' command)
    .BYTE <PARSE_CMD_LOADSRC    ; 5 - 'L' (load source for B:)
    .BYTE <PARSE_CMD_MOVE_CHECK ; 6 - 'M'
    .BYTE <PARSE_CMD_READ_CHECK ; 7 - 'R'
    .BYTE <PARSE_CMD_DONE       ; 8 - unused ('S' retired)
    .BYTE <PARSE_CMD_STACK      ; 9 - 'T'
    .BYTE <PARSE_CMD_WRITE_CHECK; 10 - 'W'
    .BYTE <PARSE_CMD_EXIT       ; 11 - unused (ESC handled earlier)
    .BYTE <PARSE_CMD_ZERO       ; 12 - 'Z'
    .BYTE <PARSE_CMD_SEARCH_CHECK; 13 - 'X' (search)
    .BYTE <PARSE_CMD_DECIMAL_CHECK; 14 - '#:' (decimal to hex)
    .BYTE <PARSE_CMD_HEX_TO_DEC ; 15 - '$:' (hex to decimal)
    .BYTE <PARSE_CMD_DISASM     ; 16 - 'D' (disassemble)
    .BYTE <PARSE_CMD_LINEASM    ; 17 - 'A' (line assembler)
```

Slots 4, 8 and 11 are dead. Their commands are handled before the table is
consulted, or have been retired, and the entries stay only so the numbering of
everything below them does not shift.

##### B. `CMD_JUMP_COMPACT_HI`

The same list again with `>` instead of `<`. The two tables are read as a pair,
so an entry added to one and forgotten in the other produces a jump to a wild
address rather than an assembly error.

##### C. `CMD_INDEX_MAP`

One byte per letter from `A` to `Z`, holding the slot number in the jump tables
or `$FF` for a letter that is not a command.

```assembly
CMD_INDEX_MAP:
    .BYTE 17    ; A -> 17 (line assembler)
    .BYTE 0     ; B -> 0 (build the loaded source)
    .BYTE 1     ; C -> 1 (clear)
    .BYTE 16    ; D -> 16 (disassemble)
    .BYTE $FF   ; E -> invalid
    .BYTE 2     ; F -> 2 (fill)
    ; ... rest of the table
```

The index in `CMD_INDEX_MAP` must match the handler's position in the jump
tables, counting from zero.

---

### 3. Parsing Infrastructure

#### Command Parser Entry Points

Create a parser entry point following the naming convention
`PARSE_CMD_[NAME]`. A handler that needs no parsing simply jumps to its
implementation.

```assembly
PARSE_CMD_CLEAR:
    JSR PARSE_COLON_COMMAND     ; Parse C: format
    BCS PARSE_CMD_ERROR_JMP2    ; If error, jump to local error handler
    JMP CMD_CLEAR_SCREEN        ; Execute clear screen command
```

A parameterised command runs the shared address parser first and then its own
parameter parser.

```assembly
PARSE_CMD_FILL_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse F:xxxx-yyyy format
    BCS PARSE_CMD_RANGE_ERROR   ; Address error, show RANGE?
    JSR PARSE_FILL_VALUE        ; Parse comma and fill value
    BCS PARSE_CMD_VALUE_ERROR   ; Value error, show VALUE?
    JMP CMD_FILL_MEMORY         ; Execute fill memory command
```

Note that both examples end in `JMP`, not `JSR`. Command implementations return
to the main loop themselves, so the parser tail-jumps into them.

#### Using PARSE_COLON_COMMAND

`PARSE_COLON_COMMAND` handles the standard address parsing. It takes the command
in `MON_CMDBUF` starting with the command character, and it saves and restores
`MON_CURRADDR` if the parse fails, so a bad command leaves the prompt address
alone.

On success it leaves a single address in `MON_CURRADDR_LO/HI`, and for a range
it leaves the start in `MON_CURRADDR_LO/HI` and the end in
`MON_ENDADDR_LO/HI`. It clears `MON_ENDADDR` on entry, so a single address is
recognisable afterwards by both end bytes being zero. The carry flag is clear on
success and set on error. X is left pointing at the first character it did not
consume, which is where a custom parameter parser picks up.

Three input forms are accepted.

| Form | Result |
|---|---|
| `F:` | no address given, so the current address is used |
| `F:8000` | single address in `MON_CURRADDR_LO/HI` |
| `F:8000-8FFF` | range, start in `MON_CURRADDR_LO/HI` and end in `MON_ENDADDR_LO/HI` |

A comma after the address ends the parse successfully and leaves the rest of the
line to the caller, which is how `F:`, `M:` and `X:` get their extra fields.

#### Custom Parameter Parsing

For a command that needs more than an address, write a parser that runs after
`PARSE_COLON_COMMAND` and follows the same carry convention. `PARSE_FILL_VALUE`,
`PARSE_MOVE_PARAMS` and `PARSE_SEARCH_PARAMS` are the three existing examples.

---

### 4. Memory Allocation

#### Current Memory Layout

The monitor's page-2 variables are defined once in `src/kernel/kernel_vars.inc`,
not in `monitor.asm`, so the kernel BIOS and the monitor module cannot drift
apart on where they live.

```
$0200-$024F: Command input buffer, 80 bytes (MON_CMDBUF)
$026A-$027C: Core monitor state (length, mode, addresses, cursor, fill, move)
$027D-$028D: Search pattern buffer and length
$028E-$02DE: Last-command buffer and length, for the '.' recall
$02DF:       MON_DUMP_SNAP flag
$0400:       MON_SNAP_BUF, the 256-byte page snapshot used by T: and Z:
```

The gap below `$026A` is deliberate. BASIC uses `$0200-$0268`, and the monitor's
command buffer overlaps it only because the two are never active at the same
time. Monitor variables that must survive a trip through BASIC start above it.

#### Available Memory Ranges

`$02E0-$02FF` is free, 32 bytes. Beyond that, `$0500-$07FF` is already taken by
the assembler's identifier buffers and symbol table.

#### Variable Allocation Guidelines

1. Add the definition to `kernel_vars.inc`, not to `monitor.asm`.
2. Place it after the existing allocations, starting at `$02E0`.
3. Use the naming convention `MON_[COMMAND]_[PURPOSE]`.
4. Give it a comment saying what it holds and how wide it is.
5. Keep a command's variables together.

#### Zero Page Usage

The monitor's zero-page slots live in `$14-$3F`. BASIC uses `$00-$10` and
`$F0-$FF`, so nothing of the monitor's may go there.

| Address | Contents |
|---|---|
| `$14-$15` | `MON_CURRADDR_LO/HI`, the current address |
| `$16-$17` | `MON_MSG_PTR_LO/HI`, the message pointer |
| `$18-$19` | `JUMP_VECTOR`, the indirect jump vector |
| `$1A-$1D` | video cell-index scratch |
| `$1E-$22` | pager flags, line count and abort flag |
| `$23-$2B` | RNG state, move scratch, sound flags |
| `$31-$32` | the jiffy counter |
| `$35-$39` | decimal conversion workspace |

There is no processor port at `$00-$01`. That is a Commodore 64 detail and the
MFC has nothing like it, so zero page is cleared wholesale at reset. Banking is
the `MODULE_BANK` register at `$FE23`.

---

### 5. Help System Integration

#### Required Updates

Adding a command requires updating three help-related things.

##### A. Add the help message

Each help line is the syntax, a TAB that pads out to `HELP_DESC_COL`, then the
description.

```assembly
MSG_HELP_CLEAR:      .BYTE "C:", $09, "CLEAR SCREEN", 0
MSG_HELP_FILL:       .BYTE "F:XXXX-YYYY,ZZ", $09, "FILL MEMORY", 0
```

##### B. Add it to `HELP_MSG_TABLE`

The table is ordered alphabetically by command letter, with the non-letter
commands after it and ESC last.

```assembly
HELP_MSG_TABLE:
    .WORD MSG_HELP_LINEASM      ; A
    .WORD MSG_HELP_BUILD        ; B
    .WORD MSG_HELP_CLEAR        ; C
    ; ...
    .WORD MSG_HELP_EXIT         ; ESC
    .WORD MSG_HELP_HELP         ; ?
    .WORD MSG_HELP_RECALL       ; .
    .WORD MSG_HELP_DECIMAL      ; #
    .WORD MSG_HELP_HEX_TO_DEC   ; $
    .WORD MSG_HELP_QUIT         ; Q
```

##### C. Update the count

```assembly
HELP_MSG_COUNT = 19              ; Number of help messages
```

`PRINT_HELP_BODY` walks the table for `HELP_MSG_COUNT * 2` bytes, so a message
added to the table without bumping the count is simply never printed.

#### Help Message Format Guidelines

Keep the syntax field short enough that the TAB still lands on
`HELP_DESC_COL`, and write the description in the same terse uppercase style as
the rest. The existing lines show the range of forms.

```
C:                      CLEAR SCREEN
R:XXXX(-YYYY)           READ MEMORY
M:XXXX-YYYY,ZZZZ,B      COPY/MOVE (B 0=COPY 1=MOVE)
```

---

### 6. Message System

#### Message Storage

All messages are null-terminated strings in the message data section near the
end of `monitor.asm`, just before the assembler include.

```assembly
MSG_HELP_HEADER:     .BYTE "MONITOR COMMANDS", 0
MSG_SYNTAX_ERROR:    .BYTE "ERROR?", $0D, $0A, 0
MSG_SUCCESS:         .BYTE "OK", $0D, $0A, 0
```

#### Standard Message Types

There are four standard replies, and a new command should reuse them rather than
invent its own wording. `MSG_SUCCESS` prints `OK`. `MSG_SYNTAX_ERROR` prints
`ERROR?` for a malformed command, `MSG_RANGE_ERROR` prints `RANGE?` for an
address range that does not make sense, and `MSG_VALUE_ERROR` prints `VALUE?`
for a parameter that does not parse.

#### Message Printing System

Point `MON_MSG_PTR` at the string and call `PRINT_MESSAGE`.

```assembly
LDA #<MSG_FILL_SUCCESS          ; Load low byte
STA MON_MSG_PTR_LO              ; Store in pointer
LDA #>MSG_FILL_SUCCESS          ; Load high byte
STA MON_MSG_PTR_HI              ; Store in pointer
JSR PRINT_MESSAGE               ; Print the message
```

#### Message Optimization Pattern

For a message printed from more than one place, wrap the sequence in a
subroutine so the eleven bytes of setup appear once.

```assembly
PRINT_FILL_SUCCESS:
    LDA #<MSG_FILL_SUCCESS      ; 2 bytes
    STA MON_MSG_PTR_LO          ; 2 bytes (zero page)
    LDA #>MSG_FILL_SUCCESS      ; 2 bytes
    STA MON_MSG_PTR_HI          ; 2 bytes (zero page)
    JMP PRINT_MESSAGE           ; 3 bytes, tail call
```

---

### 7. Implementation Patterns

#### Command Implementation Structure

Follow this pattern for a command implementation. Validate the parameters,
perform the operation, report the result, and return.

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

Call `VALIDATE_ADDRESS_RANGE` rather than writing this out again. It is the
shared check that the start address is not above the end address, and it follows
the carry convention.

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
    SEC                         ; Set carry for error
    RTS

RANGE_VALID:
    CLC                         ; Clear carry for success
    ; Continue with operation
```

##### Memory Operations Loop

The end address is inclusive, so the loop has to do the byte at the end address
before it stops.

```assembly
OPERATION_LOOP:
    ; Perform operation on byte at (MON_CURRADDR_LO),Y

    ; Check if we've reached end address
    LDA MON_CURRADDR_HI
    CMP MON_ENDADDR_HI
    BCC CONTINUE_OPERATION       ; Current < end (high), continue
    BNE OPERATION_DONE           ; Current > end (high), done
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

##### Parameter Parsing

A custom parameter parser reads from `MON_CMDBUF` starting wherever
`PARSE_COLON_COMMAND` left X, and returns the carry flag set on error.

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

The monitor has a test of its own, `monitor_integration`, which drives real 6502
code on the emulated machine. Run it with
`ctest --test-dir cmake-build-debug -R monitor_integration`. A new command
belongs in that test rather than only in a manual checklist.

#### Integration Testing Checklist

When implementing a new command, verify the parser recognises the command
character without printing `ERROR?`, that parameters parse correctly, that
invalid syntax does print `ERROR?`, and that the command runs without crashing.
Check that the jump table calls the right handler, that all three tables agree,
and that the index mapping is correct. Check that the help message displays,
that `HELP_MSG_COUNT` was bumped, and that the formatting matches the other
lines. Check that any new variables sit in the free range, do not overlap
anything, and are documented in `kernel_vars.inc`.

#### Manual Test Cases

Cover valid operations across normal parameter ranges and the edge cases of a
single byte and the maximum range. Cover the error conditions of invalid syntax,
invalid parameters and boundary violations. Then check the command in sequence
with others, so that memory state is preserved and nothing interferes with the
existing commands.

#### Example Test Script

```
?               # Show help (verify new command listed)
F:8000-8010,FF  # Fill range with valid parameters
F:8000          # Single address fill
F:8000-7FFF,00  # Invalid range (start > end) - should error
F:8000-8010     # Missing parameter - should error
```

---

### 9. Optional Components

#### Success Messages

Success messages are optional but worth adding for a command that performs a
significant operation such as filling a large range, has a completion status
that is not obvious, or takes long enough that the user wonders whether it
worked. A simple command like clear screen needs nothing.

#### Progress Indication

For a long-running operation, consider periodic progress dots, an ESC abort
check, and a status counter.

```assembly
FILL_LOOP:
    ; Check for ESC key periodically
    LDA BYTE_COUNT
    AND #$FF                    ; Check every 256 bytes
    BNE SKIP_ESC_CHECK
    JSR CHECK_KEYBOARD          ; Check for ESC key
    BEQ FILL_ABORTED            ; ESC pressed, abort

SKIP_ESC_CHECK:
    ; Perform fill operation
    ; ... fill code here ...
    JMP FILL_LOOP

FILL_ABORTED:
    ; Handle abort condition
    RTS
```

#### Parameter Validation

Validate in proportion to the command's complexity. The basics are range
checking that start is not above end, verifying the parameter count, and
validating the syntax. Beyond that a command may want to protect ROM and I/O
regions, limit a value's range, or detect overlap in the way the move and copy
command does.

#### Error Message Specificity

Reuse the four existing messages rather than adding new ones. `ERROR?` covers a
general syntax or parameter error, `RANGE?` an address range error, `VALUE?` a
parameter value error, and `OK` a success. Each new string costs ROM in a bank
that also holds the assembler, and four well-known replies are easier to
recognise than a dozen bespoke ones.

---

### Implementation Workflow

1. Choose a command letter from the free ones listed in section 1.
2. Design the command syntax, including its parameter format and validation.
3. Allocate any variables in `kernel_vars.inc`, starting at `$02E0`.
4. Add entries to all three jump and mapping tables.
5. Implement the parser as `PARSE_CMD_[NAME]`.
6. Implement the command as `CMD_[NAME]`.
7. Add the help text and any custom messages.
8. Add the help message to the table and bump `HELP_MSG_COUNT`.
9. Extend `tests/test_monitor_integration.cpp` to drive the new command.
10. Update the memory usage comments and the monitor manual.

#### Memory Impact Summary

A typical command costs two bytes in the jump tables and nothing in the index
map, since that replaces a `$FF` with an index. The help entry runs to roughly
25 to 35 bytes for the message and its table slot. Variables take 1 to 10 bytes
depending on the command, and the implementation itself 50 to 200 bytes
depending on what it does. Altogether that is roughly 80 to 250 bytes per
command, in a 16 KB bank shared with the assembler.

---

### Common Pitfalls

1. Mismatched table indices. The jump table order must match the index mapping.
2. Forgetting the help count. `HELP_MSG_COUNT` gates the whole table walk.
3. Memory conflicts. Verify that new variable allocations do not overlap.
4. Error handling. Always set or clear the carry flag on the way out.
5. Address arithmetic. The 6502 has no 16-bit arithmetic instructions.
6. Parser state. Preserve `MON_CURRADDR` when another command needs it.
7. Message formatting. Match the TAB column the other help lines use.

---

## Monitor execution flows

The sections above describe how to *add* a command. The rest of this document traces
how the monitor actually runs, as execution paths and call trees taken from the source.

They are written as records rather than prose, with a labelled field per line, because
they are meant to be scanned against the source rather than read through.

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
    - `CMP #ASCII_CR; BEQ READ_CMD_DONE_CR` - Enter pressed → exit loop
    - `CMP #ASCII_BACKSPACE; BEQ READ_CMD_BACKSPACE` - Handle backspace
    - `CMP #ASCII_DELETE; BEQ READ_CMD_BACKSPACE` - Handle delete  
    - `CMP #ASCII_ESC; BEQ READ_CMD_ESCAPE` - Escape → see 7b below
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

##### 7b. Escape and cancel branch
**Path**: `READ_CMD_LOOP` → `READ_CMD_ESCAPE` → `READ_CMD_CANCEL`
- **Decision**: `READ_CMD_ESCAPE` checks whether anything has been typed
  - buffer empty: ESC is returned as a one-character command, so the caller sees it
  - buffer not empty: fall through to `READ_CMD_CANCEL`
- **LOOP**: `READ_CMD_CANCEL_ERASE`
  - **Purpose**: abandon the line in place by destructively backspacing over the X
    characters typed, so the line collapses back to the prompt
  - **Exit condition**: `CPX #$00; BEQ READ_CMD_CANCEL_DONE`
- **Flow**: reading continues on the same line, with the caller's prompt left intact

This branch does not clear a buffer with a length compare, and there is no
`READ_CMD_CLEAR_LOOP`. ESC erases what you typed and leaves you at a fresh prompt.

#### 8. Keystroke polling
**Path**: `READ_CMD_LOOP` → `GET_KEYSTROKE`
- **POLLING LOOP**: `READ_CMD_LOOP`
  - **Body**: `JSR GET_KEYSTROKE`, then dispatch on the character returned
  - **Flow**: every path that does not complete the line ends in `JMP READ_CMD_LOOP`
- **`GET_KEYSTROKE` itself does not block**
  - `LDA PIA_CONTROL; AND #PIA_DATA_AVAIL` tests the data-available bit
  - `BEQ GET_NO_KEY` returns immediately when no key is waiting
  - otherwise `LDA PIA_DATA` returns the character as typed, with case preserved

The waiting therefore happens in the caller's loop, not in the kernel entry point.
That is what makes `K_GET_KEYSTROKE` usable by real-time programs, which must poll
without stalling.

---

### Command Parsing Flow

#### 9. Command Parser Decision Tree
**Path**: MONITOR_LOOP → PARSE_COMMAND
- **Initial check**: `LDA MON_CMDLEN; BEQ PARSE_CMD_DONE` - Empty command → done
- **Characters matched before the table**, each on its own compare:
  - `CMP #'?'` → help
  - `CMP #'Q'` → quit to DOS through `RETURN_FROM_MODULE`
  - `CMP #ASCII_ESC` → clean no-op at the prompt
  - `CMP #'#'` → decimal to hex
  - `CMP #'$'` → hex to decimal
- **Range check**: `CMP #$41; BCC error` / `CMP #$5B; BCS error` - must be 'A'..'Z'
- **Table dispatch**:
  - `SEC; SBC #$41; TAX` - letter becomes an offset into `CMD_INDEX_MAP`
  - `LDA CMD_INDEX_MAP,X; CMP #$FF; BEQ error` - `$FF` means the letter is unused
  - `TAX`, then load `CMD_JUMP_COMPACT_LO/HI,X` into `JUMP_VECTOR`
  - `JMP (JUMP_VECTOR)` - dispatch to the handler
- **Unknown command**: `JMP PARSE_CMD_ERROR` → Show error

There is no chain of per-command compares any more. Every letter goes through
the one table lookup, which is why adding a command is three table edits rather
than a new branch in the parser.

#### 10. Colon Command Address Parsing
**Path**: PARSE_CMD_*_CHECK → PARSE_COLON_COMMAND
- **Syntax validation**:
  - `LDX #$01; LDA MON_CMDBUF,X` - Check second character
  - `CMP #ASCII_COLON; BNE PARSE_COLON_ERROR` - Must be colon
- **Address parsing**: `JSR HEX_QUAD_TO_ADDR` - Parse 4-hex-digit address
- **Range check** (any command that accepts a range):
  - `CPX MON_CMDLEN; BEQ PARSE_COLON_SUCCESS` - End of command → single address
  - `LDA MON_CMDBUF,X; CMP #ASCII_DASH; BEQ PARSE_RANGE` - dash → parse a range
  - `CMP #ASCII_COMMA; BEQ PARSE_COLON_SUCCESS` - comma → single address
  - anything else → `JMP PARSE_COLON_ERROR`
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
    - `LDA MON_CMDLEN; BEQ WRITE_MODE_DONE` - empty line → exit
    - `CMP #$01` then `LDA MON_CMDBUF; CMP #ASCII_ESC` - a lone ESC → exit
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

#### 12. `R:` memory read
**Path**: `CMD_READ_MEMORY`
- **One-shot, not a mode.** `R:` reads a single address or a range and returns to the
  main loop. There is no interactive read mode and no `READ_MODE_INPUT` loop
- **Sequential execution**:
  - `STZ CMD_LINE_COUNT` and `STZ PAGE_ABORT_FLAG` reset the pager for this command
  - if `MON_ENDADDR_LO`/`HI` are both zero it is a single address, so branch to
    `CMD_READ_SINGLE`
  - otherwise it is a range, and `VALIDATE_ADDRESS_RANGE` checks it first
- **Note**: an earlier version of this document described `R:` as an interactive mode
  supporting nested `R:`, `W:`, `G:`, `H` and `T` commands. That design is gone. `W:`
  is the only remaining interactive mode

---

### Memory Display Flow

#### 13. Memory Dump Range Display
**Path**: Various commands → DUMP_MEMORY_RANGE → DUMP_RANGE_LOOP

##### Memory Display Loop
- **LOOP**: `DUMP_RANGE_LOOP`
  - **Address display**: Print current address in hex
  - **Inner loop**: `DUMP_PRINT_BYTES` (up to `MON_BYTES_PER_LINE` = 16 per line)
    - **Address comparison**: Compare current vs end address
    - **BRANCH CONDITIONS**:
      - `BCC DUMP_PRINT_BYTE` - Current < end → print byte
      - `BNE DUMP_RANGE_DONE` - Current > end → done
      - `BEQ DUMP_PRINT_BYTE` - Current = end → last byte
    - **Byte printing**: Convert to hex, print with space
    - **Address increment**: Increment with carry handling
    - **Line limit**: `INC MON_BYTE_COUNT; CMP #MON_BYTES_PER_LINE; BNE DUMP_PRINT_BYTES`
  - **Line completion**: Print newline, start next line
  - **Exit condition**: Address comparison indicates end reached

---

### Utility Function Flows

#### 14. Hex Conversion Flow
**Path**: Various → HEX_CHAR_TO_NIBBLE

##### Character Validation Decision Tree
- **Rebase**: `SEC; SBC #$30` - subtract '0' once, then test the remainder
- **Digit range**: `CMP #$0A; BCC HEX_CHAR_VALID` → 0-9, done
- **Below 'A'**: `CMP #$11; BCC HEX_CHAR_INVALID` - `'A'-'0'` = `$11`
- **Above 'F'**: `CMP #$17; BCS HEX_CHAR_INVALID` - `'F'-'0'+1` = `$17`
- **Letter fold**: `SEC; SBC #$07` → A-F become 10-15
- **Exit paths**: `HEX_CHAR_VALID` clears carry, `HEX_CHAR_INVALID` sets it

There is no lowercase branch. `READ_COMMAND_LINE` folds case as it reads, so the
routine only ever sees uppercase.

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
1. `READ_CMD_LOOP` - command input loop, running until Enter or Escape
2. `MONITOR_LOOP` - the monitor's main loop, infinite
3. `WRITE_MODE_INPUT` - write-mode input loop, running until ESC or an empty line

`GET_KEYSTROKE` itself does not block. It reads `PIA_CONTROL`, and if the
data-available bit is clear it branches straight to `GET_NO_KEY` and returns. Callers
that want to wait do their own looping. An earlier version of this document listed a
`GET_KEYSTROKE_WAIT` polling loop, which does not exist in the kernel and never did in
this form.

#### Summary of Finite Loops:
1. `ZP_CLEAR_LOOP` - 240 iterations, clearing `$00-$EF`
2. Module-window clear - 64 pages, `$B0` through `$EF`
3. `DUMP_PRINT_BYTES` - up to 16 iterations per line, for memory display
4. `WRITE_MODE_PARSE_LOOP` - variable iterations, parsing hex input
5. `PRINT_MSG_LOOP` - variable iterations, printing a string

The screen clear is not a loop at all. `CLEAR_SCREEN` writes one `VCMD_CLEAR` command
to the VIC and the chip clears its own planes, so there is nothing for the CPU to
iterate over.

#### Program Termination Points:
- `JMP (MON_CURRADDR_LO)` in the monitor's `G:` handler transfers control to a user
  program.
- `IRQ_HANDLER` and `NMI_HANDLER` both end in `RTI`.
- **No normal exit**: System runs indefinitely in monitor loop

This flow analysis shows that the kernel is designed as a persistent monitor system with interactive command processing, where the main execution flow is an infinite loop waiting for user commands, with various sub-modes providing specialized interactive environments for memory examination and modification.
---

## Monitor call tree


This document traces all JSR (Jump to Subroutine) calls for each monitor command from the main monitor loop through to completion.
It is intended as an aid to developers using the monitor, so that the call tree is easy to follow.

### Main Monitor Loop

```
MONITOR_MAIN
├── JSR PRINT_NEWLINE
└── MONITOR_LOOP
    ├── JSR PRINT_MONITOR_PROMPT
    │   ├── JSR PRINT_CURRENT_ADDRESS
    │   │   ├── JSR PRINT_HEX_BYTE (for high byte)
    │   │   │   └── (uses NIBBLE_TO_ASCII)
    │   │   ├── JSR PRINT_CHAR (4 times for address digits)
    │   │   └── JSR PRINT_HEX_BYTE (for low byte)
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

#### A: (Line Assembler)
```
PARSE_COMMAND
└── PARSE_CMD_LINEASM
    └── JMP CMD_ASM            [assembler.inc]
```

#### B: (Build the Loaded Source)
```
PARSE_COMMAND
└── PARSE_CMD_BUILD
    └── JMP CMD_BUILD          [assembler.inc]
```

#### C: (Clear Screen)
```
PARSE_COMMAND
└── PARSE_CMD_CLEAR
    ├── JSR PARSE_COLON_COMMAND
    │   └── JSR HEX_QUAD_TO_ADDR (if an address was given)
    │       └── JSR HEX_PAIR_TO_BYTE (twice)
    │           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
    └── JMP CMD_CLEAR_SCREEN
        └── JSR CLEAR_SCREEN   (writes VCMD_CLEAR to the VIC)
```

#### D: (Disassemble)
```
PARSE_COMMAND
└── PARSE_CMD_DISASM
    └── JMP CMD_DISASM         [assembler.inc]
```

#### F: (Fill Memory)
```
PARSE_COMMAND
└── PARSE_CMD_FILL_CHECK
    ├── JSR PARSE_COLON_COMMAND
    │   ├── JSR HEX_QUAD_TO_ADDR (start address)
    │   └── JSR HEX_QUAD_TO_ADDR (end address, if range)
    ├── JSR PARSE_FILL_VALUE
    │   └── JSR HEX_PAIR_TO_BYTE
    │       └── JSR HEX_CHAR_TO_NIBBLE (twice)
    └── JMP CMD_FILL_MEMORY
        ├── JSR VALIDATE_ADDRESS_RANGE
        └── JSR PRINT_MESSAGE (success message)
            └── JSR PRINT_CHAR (for each character)
```

#### G: (Go/Run)
```
PARSE_COMMAND
└── PARSE_CMD_GO_CHECK
    ├── JSR PARSE_COLON_COMMAND
    │   └── JSR HEX_QUAD_TO_ADDR
    │       └── JSR HEX_PAIR_TO_BYTE (twice)
    │           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
    └── JMP CMD_RUN_PROGRAM
        └── JMP (MON_CURRADDR_LO) [transfers control to the user program]
```

`CMD_RUN_PROGRAM` is entered by a `JMP`, not a `JSR`, so it has no return
address of its own on the stack and the user program's `RTS` lands back in the
monitor's main loop.

#### L: (Load a Source File)
```
PARSE_COMMAND
└── PARSE_CMD_LOADSRC
    └── JMP CMD_LOAD           [assembler.inc]
        └── (FIO_OPEN_RD through the host file port into SRC_BUF, for B:)
```

`L:` loads assembler source text for `B:` to build. The monitor does not load or
save binaries, and the DOS `LOAD`, `SAVE`, `IMPORT` and `EXPORT` commands do
that job instead.

#### M: (Move/Copy Memory)
```
PARSE_COMMAND
└── PARSE_CMD_MOVE_CHECK
    ├── JSR PARSE_COLON_COMMAND (address range)
    ├── JSR PARSE_MOVE_PARAMS
    │   └── JSR HEX_QUAD_TO_ADDR (destination address)
    └── JMP CMD_MOVE_MEMORY
        ├── JSR VALIDATE_ADDRESS_RANGE
        └── JSR PRINT_MESSAGE (success message)
            └── JSR PRINT_CHAR (for each character)
```

#### R: (Read Memory)
```
PARSE_COMMAND
└── PARSE_CMD_READ_CHECK
    ├── JSR PARSE_COLON_COMMAND
    │   ├── JSR HEX_QUAD_TO_ADDR (start address)
    │   └── JSR HEX_QUAD_TO_ADDR (end address, if range)
    └── JMP CMD_READ_MEMORY
        ├── range: JSR VALIDATE_ADDRESS_RANGE
        │          JMP DUMP_MEMORY_RANGE
        │          ├── JSR PRINT_CHAR (address and data)
        │          └── JSR PRINT_NEWLINE_PAGED
        └── single: JMP SHOW_WRITE_ADDRESS
            ├── JSR PRINT_CURRENT_ADDRESS
            ├── JSR PRINT_HEX_BYTE
            └── JSR PRINT_CHAR (multiple times)
```

A single address and a range are told apart by `MON_ENDADDR`, which
`PARSE_COLON_COMMAND` zeroes on entry.

#### T: (Stack Dump)
```
PARSE_COMMAND
└── PARSE_CMD_STACK
    ├── JSR PARSE_COLON_COMMAND
    └── JMP CMD_DUMP_STACK
        └── JSR DUMP_MEMORY_RANGE
            ├── JSR PRINT_CHAR (address and data)
            └── JSR PRINT_NEWLINE_PAGED
```

#### W: (Write Mode)
```
PARSE_COMMAND
└── PARSE_CMD_WRITE_CHECK
    ├── JSR PARSE_COLON_COMMAND
    │   └── JSR HEX_QUAD_TO_ADDR
    │       └── JSR HEX_PAIR_TO_BYTE (twice)
    │           └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
    └── JMP CMD_WRITE_MODE
        ├── JSR SHOW_WRITE_ADDRESS
        │   ├── JSR PRINT_CURRENT_ADDRESS
        │   ├── JSR PRINT_HEX_BYTE
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

#### X: (Search Memory)
```
PARSE_COMMAND
└── PARSE_CMD_SEARCH_CHECK
    ├── JSR PARSE_COLON_COMMAND (address range)
    ├── JSR PARSE_SEARCH_PARAMS
    │   └── JSR HEX_PAIR_TO_BYTE (for each pattern byte)
    │       └── JSR HEX_CHAR_TO_NIBBLE (twice per byte)
    └── JMP CMD_SEARCH_MEMORY
        ├── JSR VALIDATE_ADDRESS_RANGE
        ├── JSR PRINT_CURRENT_ADDRESS (for each match found)
        │   └── JSR PRINT_HEX_BYTE
        └── JSR PRINT_NEWLINE_PAGED
```

#### Z: (Zero Page Dump)
```
PARSE_COMMAND
└── PARSE_CMD_ZERO
    ├── JSR PARSE_COLON_COMMAND
    └── JMP CMD_DUMP_ZERO_PAGE
        └── JSR DUMP_MEMORY_RANGE
            ├── JSR PRINT_CHAR (address and data)
            └── JSR PRINT_NEWLINE_PAGED
```

`T:` and `Z:` both snapshot their page into `MON_SNAP_BUF` at `$0400` first and
set `MON_DUMP_SNAP`, so the dump reports the page as it was rather than as the
dump routine's own stack and zero-page use leaves it.

#### ? (Help)
```
PARSE_COMMAND
└── PARSE_CMD_HELP_DIRECT           ('?' is matched before the table lookup)
    └── JMP CMD_SHOW_HELP
        ├── JSR PRINT_HELP_HEADER
        │   └── JMP PRINT_MSG_AY
        │       └── JSR PRINT_CHAR (for each character)
        ├── JSR PRINT_NEWLINE_PAGED
        ├── JSR PRINT_HELP_BODY
        │   ├── JSR PRINT_HELP_LINE (for each entry in HELP_MSG_TABLE)
        │   └── JSR PRINT_NEWLINE_PAGED (after each help line)
        └── JMP PRINT_NEWLINE_PAGED
```

#### Q (Quit to DOS)
```
PARSE_COMMAND
└── PARSE_CMD_QUIT_DIRECT
    └── JMP RETURN_FROM_MODULE  ($FF12)
        └── (unmaps bank 4, then re-enters the DOS shell at DOS_WARM)
```

`Q` must go through `RETURN_FROM_MODULE` rather than jumping straight to
`DOS_WARM`. Leaving the monitor mapped would hide 12 KB of the DOS's scratch RAM
behind this ROM.

#### `#:` and `$:` (Base Conversion)
```
PARSE_COMMAND
├── PARSE_CMD_DEC_DIRECT → JMP PARSE_CMD_DECIMAL_CHECK  ('#:nnnnn' -> hex)
└── PARSE_CMD_HEX_DIRECT → JMP PARSE_CMD_HEX_TO_DEC     ('$:xxxx'  -> decimal)
```

#### ESC (Exit Mode)
```
READ_COMMAND_LINE
└── (ESC is handled directly in input processing)
    └── JMP CMD_EXIT_MODE
        └── (sets MON_MODE back to command mode, no JSR calls)
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
PRINT_HEX_BYTE
└── (uses NIBBLE_TO_ASCII, no JSR calls)

PRINT_MESSAGE
└── JSR PRINT_CHAR (for each character until null terminator)

PRINT_CHAR
├── JSR SET_VREG_ADDR, then STA VREG_CHAR (the screen is behind the VIC port)
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
These commands execute once and return to the command prompt.

| Command | Action |
|---|---|
| `B:` | build the loaded source |
| `C:` | clear screen |
| `D:` | disassemble |
| `F:` | fill memory |
| `G:` | go, run a program |
| `L:` | load a source file |
| `M:` | move or copy memory |
| `R:` | read memory |
| `T:` | stack dump |
| `X:` | search memory |
| `Z:` | zero-page dump |
| `?` | help |
| `Q` | quit to DOS |
| `#:` / `$:` | base conversion |

#### Interactive Mode Commands

Two commands hold the terminal until you leave them. `W:` enters
`WRITE_MODE_LOOP` and stays there until ESC, and `A:` runs the line assembler
the same way.

### Error Handling

All parsing functions report through the carry flag, clear for success and set
for error. Error paths call one of the three message printers.
```
PRINT_ERROR_MSG, PRINT_VALUE_ERROR, or PRINT_RANGE_ERROR
├── JSR PRINT_MESSAGE
│   └── JSR PRINT_CHAR (multiple times)
└── JSR PRINT_NEWLINE
```

### Notes

- All commands return to `MONITOR_LOOP` after completion.
- `W:` and `A:` are the two persistent interactive modes.
- `G:` transfers control to user code and may not return.
- Paging keeps memory dumps from overflowing the screen.
- `.` recalls the last successful command from history.
- ESC exits any interactive mode and returns to command mode.
