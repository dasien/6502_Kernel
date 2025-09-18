# 6502 Kernel Monitor Command Infrastructure

## Overview

This document provides a comprehensive guide for implementing new commands in the 6502 kernel monitor system. It covers all the components, patterns, and integration points required to successfully add a new command to the monitor.

## Table of Contents

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

## 1. Command Character Assignment

### Available Command Letters

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

### Command Types

Commands fall into two categories:

1. **Simple Commands** - Single character (e.g., `H`, `C`, `Z`)
2. **Parameterized Commands** - Colon syntax (e.g., `W:8000`, `R:8000-8FFF`)

---

## 2. Jump Table Integration

### Required Table Updates

When adding a new command, you must update three tables in the exact order:

#### A. CMD_JUMP_COMPACT_LO (Line ~2498)
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

#### B. CMD_JUMP_COMPACT_HI (Line ~2511)
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

#### C. CMD_INDEX_MAP (Line ~2526)
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

## 3. Parsing Infrastructure

### Command Parser Entry Points

Create a parser entry point following the naming convention `PARSE_CMD_[NAME]`:

#### Simple Commands (No Parameters)
```assembly
PARSE_CMD_CLEAR:
    JSR CMD_CLEAR_SCREEN        ; Execute clear screen command
    JMP PARSE_CMD_DONE
```

#### Parameterized Commands (Colon Syntax)
```assembly
PARSE_CMD_FILL_CHECK:
    JSR PARSE_COLON_COMMAND     ; Parse F:xxxx-yyyy,zz format
    BCS PARSE_CMD_ERROR         ; If error, show error message
    JSR CMD_FILL_MODE           ; Execute fill mode command
    JMP PARSE_CMD_DONE
```

### Using PARSE_COLON_COMMAND

The `PARSE_COLON_COMMAND` function handles standard address parsing:

**Input:** Command in `MON_CMDBUF` starting with command character
**Output:** 
- Single address: `MON_CURRADDR_HI/LO`
- Range: `MON_CURRADDR_HI/LO` (start) and `MON_ENDADDR_HI/LO` (end)
- Carry flag: Clear if successful, Set if error

**Supported Formats:**
- `F:8000` → Single address in `MON_CURRADDR_HI/LO`
- `F:8000-8FFF` → Range with start in `MON_CURRADDR_HI/LO`, end in `MON_ENDADDR_HI/LO`

### Custom Parameter Parsing

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

## 4. Memory Allocation

### Current Memory Layout

The monitor uses system RAM `$0200-$0283` (132 bytes total):

```
$0200-$024F: Command input buffer (80 bytes)
$0250-$025F: Core monitor variables (16 bytes)
$0260-$0283: Phase 1 command variables (36 bytes)
```

### Available Memory Ranges

**Next Available:** `$0284-$02FF` (124 bytes remaining in page)

### Variable Allocation Guidelines

1. **Place variables after existing allocations** (starting at `$0284`)
2. **Use consistent naming:** `MON_[COMMAND]_[PURPOSE]`
3. **Document memory usage** in header comments
4. **Allocate in logical groups** by command

### Example Variable Allocation

```assembly
; ================================================================
; PHASE 2 COMMAND VARIABLES ($0284+)
; ================================================================
MON_FILL_VALUE    = $0284         ; Fill command byte value
MON_FILL_COUNTER  = $0285         ; Fill operation counter (2 bytes: $0285-$0286)
MON_SEARCH_MATCHES = $0287        ; Search command match counter (2 bytes)
MON_COPY_TEMP     = $0289         ; Copy command temporary storage
```

### Zero Page Usage

**Critical Zero Page Locations (DO NOT MODIFY):**
- `$00-$01`: Processor port (memory banking)
- `$02-$03`: `MON_CURRADDR_LO/HI` (current address)
- `$04-$05`: `MON_MSG_PTR_LO/HI` (message pointer)
- `$06-$07`: `JUMP_VECTOR` (indirect jump vector)

---

## 5. Help System Integration

### Required Updates

Adding a command requires updating three help-related components:

#### A. Add Help Message (Line ~2585)
```assembly
; MESSAGE DATA SECTION - Null-terminated strings for monitor
MSG_HELP_CLEAR:      .BYTE "C:     CLEAR SCREEN", 0
MSG_HELP_GO:         .BYTE "G:XXXX RUN", 0
; Add new help message:
MSG_HELP_FILL:       .BYTE "F:XXXX-YYYY,ZZ FILL MEMORY", 0
```

#### B. Add to Help Message Table (Line ~2568)
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

#### C. Update Help Count (Line ~2580)
```assembly
HELP_MSG_COUNT = 11              ; Number of help messages (was 10)
```

### Help Message Format Guidelines

- **Keep consistent width** (approximately 30 characters max)
- **Use format:** `COMMAND:PARAMS DESCRIPTION`
- **Examples:**
  - Simple: `"C:     CLEAR SCREEN"`
  - Range: `"R:XXXX(-YYYY) READ FROM MEMORY"`
  - Complex: `"F:XXXX-YYYY,ZZ FILL MEMORY"`

---

## 6. Message System

### Message Storage

All messages are stored as null-terminated strings in the message data section (starting around line 2585):

```assembly
; MESSAGE DATA SECTION - Null-terminated strings for monitor
MSG_HELP_HEADER:     .BYTE "6502 MONITOR COMMANDS", 0
MSG_SYNTAX_ERROR:    .BYTE "?ERROR", 0
MSG_SUCCESS:         .BYTE "OK", 0
```

### Standard Message Types

#### Success Messages
- `MSG_SUCCESS` - Generic "OK" message
- Custom success messages for specific operations

#### Error Messages  
- `MSG_SYNTAX_ERROR` - "?ERROR" for invalid syntax
- Custom error messages for specific conditions

### Message Printing System

Use the optimized message printing system:

```assembly
; Set up message pointer
LDA #<MSG_FILL_SUCCESS          ; Load low byte
STA MON_MSG_PTR_LO              ; Store in pointer
LDA #>MSG_FILL_SUCCESS          ; Load high byte  
STA MON_MSG_PTR_HI              ; Store in pointer
JSR PRINT_MESSAGE               ; Print the message
```

### Message Optimization Pattern

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

## 7. Implementation Patterns

### Command Implementation Structure

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

### Common Code Patterns

#### Address Validation
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

#### Memory Operations Loop
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

#### Parameter Parsing (Custom)
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

## 8. Testing and Validation

### Integration Testing Checklist

When implementing a new command, verify:

#### Parser Integration
- [ ] Command character recognized (no "?ERROR")
- [ ] Parameters parsed correctly
- [ ] Invalid syntax shows "?ERROR" 
- [ ] Command executes without crashing

#### Jump Table Validation
- [ ] Correct parser function called
- [ ] All three tables updated consistently
- [ ] Index mapping correct

#### Help System
- [ ] Help message displays correctly
- [ ] Help count updated
- [ ] Message formatting consistent

#### Memory Usage
- [ ] Variables allocated in correct range
- [ ] No conflicts with existing variables
- [ ] Memory usage documented

### Manual Test Cases

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

### Example Test Script

```
H:              # Show help (verify new command listed)
F:8000-8010,FF  # Fill range with valid parameters
F:8000          # Single address fill
F:8000-7FFF,00  # Invalid range (start > end) - should error
F:8000-8010     # Missing parameter - should error  
```

---

## 9. Optional Components

### Success Messages

Success messages are **optional** but recommended for commands that:
- Perform significant operations (filling large ranges)
- Have non-obvious completion status
- Take noticeable time to execute

**Simple commands** (like clear screen) typically don't need success messages.

### Progress Indication

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

### Parameter Validation

Implement parameter validation appropriate to command complexity:

#### Basic Validation
- Range checking (start <= end)
- Parameter count verification
- Syntax validation

#### Advanced Validation
- Memory protection (ROM/I/O areas)
- Value range limits
- Overlap detection (for move/copy operations)

### Error Message Specificity

Balance error message detail with memory usage:

#### Generic Errors (Recommended)
- `?ERROR` - General syntax/parameter error
- `?RANGE` - Address range error  

#### Specific Errors (Optional)
- `?SYNTAX` - Syntax error
- `?VALUE` - Parameter value error
- `?PROTECTED` - Attempting to modify protected memory

---

## Implementation Workflow

### Step-by-Step Process

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

### Memory Impact Summary

Adding a typical command requires:
- **Jump tables:** +2 bytes (low/high byte entries)
- **Index mapping:** 0 bytes (replace $FF with index)
- **Help system:** ~25-35 bytes (message + table entry)
- **Command variables:** 1-10 bytes (depends on command complexity)
- **Implementation code:** 50-200 bytes (depends on functionality)

**Total overhead:** ~80-250 bytes per command

---

## Common Pitfalls

1. **Mismatched table indices** - Ensure jump table order matches index mapping
2. **Forgetting help count update** - Must increment `HELP_MSG_COUNT`
3. **Memory conflicts** - Verify variable allocations don't overlap
4. **Error handling** - Always set/clear carry flag appropriately
5. **Address arithmetic** - Remember 6502 has no 16-bit arithmetic instructions
6. **Parser state** - Preserve `MON_CURRADDR` for other commands when needed
7. **Message formatting** - Keep consistent with existing help messages

Following this infrastructure guide ensures new commands integrate cleanly with the existing monitor system while maintaining consistency and reliability.