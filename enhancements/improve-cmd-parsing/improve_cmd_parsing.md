# Enhancement: Improve Command Parsing Architecture

## 📋 Overview

**Status**: Proposed  
**Priority**: Medium  
**Complexity**: Medium  
**Estimated Effort**: 4-6 hours  
**Target Version**: 1.1

## 🎯 Objective

Refactor the monitor command parsing system to use a modular, composable architecture that separates syntax validation from semantic parsing. This will improve maintainability, reduce code duplication, and make it easier to add new commands with different syntax requirements.

## 🔍 Problem Statement

### Current Issues

1. **`PARSE_COLON_COMMAND` does too much**
   - Validates colon syntax
   - Parses hex addresses
   - Handles address ranges (dash separator)
   - Handles parameter separators (comma)
   - Manages multiple address variables (CURRADDR, STARTADDR, ENDADDR)
   - Assumes all addresses are hexadecimal

2. **Premature state modification**
   - `MON_CURRADDR` gets modified during parsing before the entire command is validated
   - If parsing fails partway through, the address is already corrupted
   - Example bug: `R:99999` (5 hex digits) parses first 4 digits into `MON_CURRADDR`, then fails validation, leaving address set to `$9999`

3. **Difficult to extend**
   - Adding commands with non-hex syntax (like `D:` decimal-to-hex) requires special handling
   - Cannot easily add commands with different parameter formats
   - Tightly coupled parsing makes testing difficult

4. **Inconsistent error handling**
   - Some commands handle errors inline
   - Some commands print errors directly
   - Decimal command has different error handling than hex commands

### Example Bugs

```assembly
; User types: R:99999 (too many digits)
; Expected: ERROR? message, MON_CURRADDR unchanged
; Actual: ERROR? message, MON_CURRADDR set to $9999

; User types: D:65999 (overflow)
; Expected: RANGE? message, MON_CURRADDR unchanged
; Actual: RANGE? message followed by OK message, then BASIC launches
```

## 🏗️ Proposed Architecture

### Design Principles

1. **Single Responsibility**: Each parsing function does ONE thing well
2. **Composition**: Commands compose simple parsers to build complex syntax
3. **No Side Effects on Error**: Parsing functions only modify state on success
4. **Explicit Validation**: Commands explicitly check for unexpected input
5. **Consistent Error Handling**: All commands use the same error reporting mechanism

### New Parsing Functions

#### 1. PARSE_COLON_SYNTAX
**Purpose**: Validate basic "X:" command syntax  
**Input**: MON_CMDBUF contains command  
**Output**: X = position after colon, Carry = success/error  
**Side Effects**: NONE

```assembly
PARSE_COLON_SYNTAX:
    LDX #$01                    ; Position after command character
    LDA MON_CMDBUF,X            ; Load second character
    CMP #ASCII_COLON            ; Is it a colon?
    BNE PARSE_SYNTAX_ERROR      ; If not, error
    INX                         ; X now points after colon
    CLC                         ; Success
    RTS
PARSE_SYNTAX_ERROR:
    SEC                         ; Error
    RTS
```

#### 2. PARSE_HEX_ADDRESS
**Purpose**: Parse a single 4-digit hex address  
**Input**: X = position in buffer  
**Output**: MON_CURRADDR = parsed address, X = position after address, Carry = success/error  
**Side Effects**: ONLY modifies MON_CURRADDR on success

```assembly
PARSE_HEX_ADDRESS:
    ; Save current address to restore on error
    LDA MON_CURRADDR_LO
    PHA
    LDA MON_CURRADDR_HI
    PHA
    
    ; Check if at end (no address provided)
    CPX MON_CMDLEN
    BCS PARSE_HEX_NO_ADDR       ; At or past end, error
    
    ; Parse 4-digit hex address
    JSR HEX_QUAD_TO_ADDR        ; Parses 4 digits, advances X
    BCS PARSE_HEX_ERROR         ; Invalid hex
    
    ; Success - discard saved values
    PLA
    PLA
    CLC
    RTS

PARSE_HEX_NO_ADDR:
    ; No address provided - error
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    SEC
    RTS

PARSE_HEX_ERROR:
    ; Restore original address
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    SEC
    RTS
```

#### 3. PARSE_OPTIONAL_RANGE
**Purpose**: Parse optional "-YYYY" range suffix  
**Input**: X = position after first address, MON_CURRADDR = first address  
**Output**: MON_STARTADDR = first address, MON_ENDADDR = second address (or $0000 if no range), MON_CURRADDR = first address, X = position after range  
**Side Effects**: ONLY modifies STARTADDR/ENDADDR/CURRADDR on success

```assembly
PARSE_OPTIONAL_RANGE:
    ; Save first address as start address
    LDA MON_CURRADDR_LO
    STA MON_STARTADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_STARTADDR_HI
    
    ; Clear end address (default = no range)
    LDA #$00
    STA MON_ENDADDR_LO
    STA MON_ENDADDR_HI
    
    ; Check what's next
    CPX MON_CMDLEN
    BCS PARSE_RANGE_NO_DASH     ; At or past end, no range - OK
    
    LDA MON_CMDBUF,X
    CMP #ASCII_DASH
    BEQ PARSE_RANGE_HAS_DASH    ; It's a dash - parse range
    CMP #$2C                    ; Comma?
    BEQ PARSE_RANGE_NO_DASH     ; Comma means parameters follow - OK
    
    ; Not end, not dash, not comma = ERROR!
    JMP PARSE_RANGE_ERROR

PARSE_RANGE_HAS_DASH:
    INX                         ; Skip dash
    JSR PARSE_HEX_ADDRESS       ; Parse end address
    BCS PARSE_RANGE_ERROR       ; Invalid hex = error
    
    ; Save as end address
    LDA MON_CURRADDR_LO
    STA MON_ENDADDR_LO
    LDA MON_CURRADDR_HI
    STA MON_ENDADDR_HI

PARSE_RANGE_NO_DASH:
    ; Restore start address to current
    LDA MON_STARTADDR_LO
    STA MON_CURRADDR_LO
    LDA MON_STARTADDR_HI
    STA MON_CURRADDR_HI
    CLC                         ; Success
    RTS

PARSE_RANGE_ERROR:
    SEC                         ; Error
    RTS
```

### Command Implementation Examples

#### Simple Command (no address)
```assembly
PARSE_CMD_CLEAR:
    JSR PARSE_COLON_SYNTAX      ; Validate "C:"
    BCS PARSE_CMD_ERROR
    
    ; Verify at end of command (no extra junk)
    CPX MON_CMDLEN
    BNE PARSE_CMD_ERROR
    
    JSR CMD_CLEAR_SCREEN
    JMP PARSE_CMD_DONE
```

#### Single Address Command
```assembly
PARSE_CMD_WRITE_CHECK:
    JSR PARSE_COLON_SYNTAX      ; Validate "W:"
    BCS PARSE_CMD_ERROR
    
    JSR PARSE_HEX_ADDRESS       ; Parse address
    BCS PARSE_CMD_ERROR
    
    ; Verify at end of command
    CPX MON_CMDLEN
    BNE PARSE_CMD_ERROR
    
    JSR CMD_WRITE_MODE
    JMP PARSE_CMD_DONE
```

#### Address with Optional Range
```assembly
PARSE_CMD_READ_CHECK:
    JSR PARSE_COLON_SYNTAX      ; Validate "R:"
    BCS PARSE_CMD_ERROR
    
    JSR PARSE_HEX_ADDRESS       ; Parse address
    BCS PARSE_CMD_ERROR
    
    JSR PARSE_OPTIONAL_RANGE    ; Parse optional "-YYYY"
    BCS PARSE_CMD_ERROR
    
    ; Verify at end of command
    CPX MON_CMDLEN
    BNE PARSE_CMD_ERROR
    
    JSR CMD_READ_MEMORY
    JMP PARSE_CMD_DONE
```

#### Range with Parameters
```assembly
PARSE_CMD_FILL_CHECK:
    JSR PARSE_COLON_SYNTAX      ; Validate "F:"
    BCS PARSE_CMD_ERROR
    
    JSR PARSE_HEX_ADDRESS       ; Parse start address
    BCS PARSE_CMD_ERROR
    
    JSR PARSE_OPTIONAL_RANGE    ; Parse "-YYYY"
    BCS PARSE_CMD_ERROR
    
    ; Validate range was provided (required for fill)
    LDA MON_ENDADDR_LO
    ORA MON_ENDADDR_HI
    BEQ PARSE_CMD_RANGE_ERROR   ; No range = error
    
    ; X should now point to comma
    CPX MON_CMDLEN
    BCS PARSE_CMD_ERROR         ; At end, missing fill value
    
    LDA MON_CMDBUF,X
    CMP #$2C                    ; Comma?
    BNE PARSE_CMD_ERROR         ; Must have comma before value
    
    INX                         ; Skip comma
    JSR PARSE_FILL_VALUE        ; Parse fill byte
    BCS PARSE_CMD_VALUE_ERROR
    
    ; Verify at end of command
    CPX MON_CMDLEN
    BNE PARSE_CMD_ERROR
    
    JSR CMD_FILL_MEMORY
    JMP PARSE_CMD_DONE
```

#### Custom Syntax Command
```assembly
PARSE_CMD_DECIMAL_CHECK:
    JSR PARSE_COLON_SYNTAX      ; Validate "D:"
    BCS PARSE_CMD_ERROR
    
    ; Check minimum length (D:n requires at least 3 chars)
    LDA MON_CMDLEN
    CMP #$03
    BCC PARSE_CMD_ERROR
    
    JSR CMD_DECIMAL_TO_HEX      ; Does its own decimal parsing
    ; Error flag already set if parsing failed
    JMP PARSE_CMD_DONE
```

## 📝 Implementation Plan

### Phase 1: Create New Parsing Functions (1-2 hours)

1. **Add new parsing functions**
   - Location: After existing hex conversion routines
   - Add `PARSE_COLON_SYNTAX`
   - Add `PARSE_HEX_ADDRESS` (with save/restore)
   - Add `PARSE_OPTIONAL_RANGE`

2. **Test new functions in isolation**
   - Create simple test cases
   - Verify no state modification on error
   - Verify proper X register advancement

### Phase 2: Refactor Simple Commands (1 hour)

Refactor commands that don't use ranges first:

1. `PARSE_CMD_CLEAR` (C:)
2. `PARSE_CMD_STACK` (T:)
3. `PARSE_CMD_ZERO` (Z:)
4. `PARSE_CMD_HELP` (H:)
5. `PARSE_CMD_WRITE_CHECK` (W:)
6. `PARSE_CMD_GO_CHECK` (G:)

### Phase 3: Refactor Range Commands (1-2 hours)

Refactor commands that use optional or required ranges:

1. `PARSE_CMD_READ_CHECK` (R:) - optional range
2. `PARSE_CMD_FILL_CHECK` (F:) - required range + parameter
3. `PARSE_CMD_SEARCH_CHECK` (X:) - required range + parameters
4. `PARSE_CMD_MOVE_CHECK` (M:) - required range + parameters

### Phase 4: Refactor File Commands (1 hour)

Refactor commands with filename parameters:

1. `PARSE_CMD_LOAD_CHECK` (L:)
2. `PARSE_CMD_SAVE_CHECK` (S:)

### Phase 5: Remove Old Code (30 minutes)

1. Remove or mark deprecated: `PARSE_COLON_COMMAND`
2. Clean up any unused helper functions
3. Update comments and documentation

### Phase 6: Testing (1 hour)

Test all commands with valid and invalid input:

**Valid Input Tests:**
- `R:8000` - single address
- `R:8000-8010` - address range
- `W:C000` - single address
- `F:8000-8100,FF` - range with parameter
- `D:65535` - decimal command
- `C:` - no address

**Invalid Input Tests:**
- `R:GGGG` - invalid hex
- `R:8000Q` - extra characters
- `R:99999` - too many digits
- `F:8000` - missing required range
- `F:8000-8100` - missing required parameter
- `D:` - missing decimal value
- `D:65999` - decimal overflow
- `X:` - missing colon

**Expected Behavior:**
- All invalid inputs show appropriate error message
- `MON_CURRADDR` unchanged on any error
- No spurious "OK" messages
- No unintended command execution

## ✅ Acceptance Criteria

### Functional Requirements

1. ✅ All existing commands work identically to current behavior (for valid input)
2. ✅ Invalid input produces clear error messages
3. ✅ `MON_CURRADDR` is never modified when parsing fails
4. ✅ Extra characters after valid commands produce errors
5. ✅ Decimal command (`D:`) works with custom parsing
6. ✅ All commands validate input completely before execution

### Non-Functional Requirements

1. ✅ Code is more maintainable and easier to understand
2. ✅ Adding new commands requires less code duplication
3. ✅ Parsing functions are independently testable
4. ✅ ROM usage does not increase significantly (< 100 bytes)
5. ✅ Performance is not degraded (similar cycle counts)

### Test Cases

| Input | Expected Output | Expected CURRADDR |
|-------|----------------|-------------------|
| `R:8000` | Memory display at $8000 | $8000 |
| `R:8000-8010` | Memory range display | $8000 |
| `R:99999` | ERROR? | Unchanged |
| `R:8000X` | ERROR? | Unchanged |
| `W:C000` | Enter write mode | $C000 |
| `D:65535` | $FFFF | Unchanged |
| `D:65999` | RANGE? | Unchanged |
| `D:` | VALUE? or ERROR? | Unchanged |
| `F:8000-8100,FF` | Fill memory | $8000 |
| `F:8000,FF` | ERROR? (missing range) | Unchanged |
| `C:` | Clear screen | Unchanged |
| `C:X` | ERROR? | Unchanged |

## 🔄 Migration Strategy

### Backward Compatibility

- All existing command syntax remains unchanged
- All existing commands continue to work identically
- No changes to command semantics or behavior
- Only internal parsing implementation changes

### Rollback Plan

- Keep old `PARSE_COLON_COMMAND` code commented out
- Can revert by uncommenting old code and removing new functions
- Test thoroughly before removing old code permanently

## 📊 Benefits

### Short Term

1. **Fixes existing bugs**
   - `MON_CURRADDR` corruption on parse errors
   - Spurious error messages
   - Unintended command execution after errors

2. **Improved error messages**
   - More specific error reporting
   - Consistent error handling across all commands

### Long Term

1. **Easier maintenance**
   - Clear separation of concerns
   - Each function has single responsibility
   - Easier to debug parsing issues

2. **Easier to extend**
   - Adding new commands requires less code
   - Can mix and match parsing functions
   - Custom syntax commands are straightforward

3. **Better testing**
   - Can test parsing functions independently
   - Can validate error handling systematically
   - Easier to create comprehensive test suites

## 🚧 Risks and Mitigation

### Risk: Regression in existing commands

**Mitigation**: Comprehensive testing of all commands with valid and invalid input before deployment

### Risk: Increased ROM usage

**Mitigation**: New functions are small and targeted. Expected increase < 100 bytes. Monitor during implementation.

### Risk: Performance degradation

**Mitigation**: New architecture uses similar number of function calls. Profile if concerned. Likely negligible impact.

### Risk: Introduction of new bugs

**Mitigation**: Incremental refactoring with testing after each phase. Keep old code until new code proven stable.

## 📚 References

### Related Code

- `kernel.asm` lines 400-650: Hex conversion routines
- `kernel.asm` lines 1200-1400: Current `PARSE_COLON_COMMAND`
- `kernel.asm` lines 1450-1850: Command parsing dispatch

### Related Issues

- Bug: `R:99999` corrupts MON_CURRADDR
- Bug: `D:65999` prints RANGE? then launches BASIC
- Enhancement: Need better command syntax validation

### Design Discussions

- Single Responsibility Principle discussion (2025-10-14)
- Composition vs Monolithic parsing debate (2025-10-14)
- Error handling consistency requirements (2025-10-14)

## 👥 Implementation Notes

### For Requirements Analyst

- Focus on WHAT commands need to do
- Validate that all command behaviors are preserved
- Ensure error messages are appropriate and clear
- Create comprehensive test cases

### For Assembly Developer

- Focus on HOW to implement parsing functions
- Make technical decisions about register usage
- Optimize for both code size and performance
- Ensure proper state management (save/restore on error)
- Follow existing code style and conventions

### For Testing Agent

- Create test suite for each new parsing function
- Test all commands with valid and invalid input
- Verify no state corruption on errors
- Check for memory leaks or stack issues
- Performance testing if needed

### For Documentation Agent

- Document new parsing functions with examples
- Update command reference with clearer syntax
- Create troubleshooting guide for parsing errors
- Document the refactoring rationale

## 🎉 Success Metrics

- ✅ All existing commands work correctly
- ✅ Zero parse errors leave state corrupted
- ✅ Code review shows improved maintainability
- ✅ New command can be added in < 30 minutes
- ✅ ROM usage increase < 100 bytes
- ✅ All acceptance tests pass

---

**Created**: 2025-10-14  
**Author**: Brian Gentry  
**Status**: Ready for Implementation