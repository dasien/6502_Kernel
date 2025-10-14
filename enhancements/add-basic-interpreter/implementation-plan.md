---
enhancement: add-basic-interpreter
task_id: task_1759348065_71462
agent: assembly-developer
created: 2025-10-01 21:55:42
---

# BASIC Interpreter Implementation Plan

## Document Purpose

This document provides a detailed, step-by-step implementation plan for integrating the EhBASIC interpreter into the 6502 kernel monitor system. It is designed for the implementer agent to follow systematically, with clear milestones, testing checkpoints, and rollback procedures.

---

## Prerequisites

### Required Tools
- ✓ ca65 assembler (CC65 toolchain)
- ✓ ld65 linker (CC65 toolchain)
- ✓ CMake build system
- ✓ Ninja or Make
- ✓ 6502 emulator for testing (existing C++ tool)

### Required Knowledge
- ✓ 6502 assembly language
- ✓ Zero page addressing and memory banking
- ✓ Monitor program architecture (from existing codebase)
- ✓ EhBASIC architecture (from basic.asm comments)

### Source Files
- ✓ `src/kernel/kernel.asm` - Monitor program (will be modified)
- ✓ `src/kernel/basic.asm` - EhBASIC source (will be modified)
- ✓ `docs/kernel_memory_map.md` - Memory layout documentation
- ✓ `config/memory.cfg` - Monitor linker configuration

---

## Implementation Overview

### Major Milestones

```
┌─────────────────────────────────────────────────────────────────┐
│                  IMPLEMENTATION ROADMAP                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Milestone 1: Zero Page Relocation (CRITICAL PATH)             │
│  ├─ Update constant definitions                                │
│  ├─ Relocate HEX_LOOKUP_TABLE to ROM                           │
│  ├─ Update all variable references                             │
│  └─ Test each monitor command                                  │
│                                                                │
│  Milestone 2: B: Command Framework                             │
│  ├─ Implement state save/restore routines                      │
│  ├─ Implement I/O vector initialization                        │
│  ├─ Add B: command parser entry                                │
│  └─ Test command dispatch (stub target)                        │
│                                                                │
│  Milestone 3: BASIC ROM Build System                           │
│  ├─ Configure BASIC memory layout                              │
│  ├─ Create linker configuration                                │
│  ├─ Add CMake build target                                     │
│  └─ Build and verify BASIC ROM                                 │
│                                                                │
│  Milestone 4: BASIC Integration                                │
│  ├─ Add BYE command to BASIC                                   │
│  ├─ Connect B: command to BASIC cold start                     │
│  ├─ Implement return handler                                   │
│  └─ Test complete launch/return cycle                          │
│                                                                │
│  Milestone 5: Testing and Validation                           │
│  ├─ Execute integration test suite                             │
│  ├─ Manual test scenarios                                      │
│  ├─ Memory corruption verification                             │
│  └─ Stress testing                                             │
│                                                                │
│  Milestone 6: Documentation                                    │
│  ├─ Create basic_command.md                                    │
│  ├─ Update existing documentation                              │
│  ├─ Update help system                                         │
│  └─ Update README                                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Risk Management

**Critical Path:** Milestone 1 (Zero Page Relocation)
- Affects ALL monitor commands
- Errors here will break existing functionality
- **Strategy:** Test each command immediately after relocation

**Integration Points:** Milestones 2 and 4
- State management between monitor and BASIC
- **Strategy:** Implement comprehensive save/restore with verification

**Testing Coverage:** Milestone 5
- Must detect any memory corruption or state corruption
- **Strategy:** Pattern-fill tests, repeated cycles, boundary conditions

---

## Milestone 1: Zero Page Relocation

**Estimated Time:** 4-6 hours
**Risk Level:** HIGH (affects all monitor commands)
**Dependencies:** None

### Step 1.1: Backup Current Kernel

**Purpose:** Ensure rollback capability

```bash
cp src/kernel/kernel.asm src/kernel/kernel.asm.backup
git checkout -b feature/basic-interpreter
git commit -m "Checkpoint: Before zero page relocation"
```

### Step 1.2: Update Zero Page Constants

**File:** `src/kernel/kernel.asm`

**Location:** Lines ~76-94 (zero page variable definitions)

**Changes Required:**

```assembly
; OLD DEFINITIONS (remove or comment out):
; MON_CURRADDR_LO    = $00
; MON_CURRADDR_HI    = $01
; MON_MSG_PTR_LO     = $02
; MON_MSG_PTR_HI     = $03
; JUMP_VECTOR        = $04
; SCREEN_PTR_LO      = $06
; SCREEN_PTR_HI      = $07
; SCRL_SRC_ADDR_LO   = $08
; SCRL_SRC_ADDR_HI   = $09
; SCRL_DEST_ADDR_LO  = $0A
; SCRL_DEST_ADDR_HI  = $0B
; SCRL_BYTE_CNT      = $0C
; CMD_LINE_COUNT     = $0D
; PAGE_ABORT_FLAG    = $0E
; RNG_SEED           = $0F
; RNG_MAX            = $10
; HEX_LOOKUP_TABLE   = $F0

; NEW DEFINITIONS (relocated to $14-$3F):
MON_CURRADDR_LO    = $14           ; Current address low byte (was $00)
MON_CURRADDR_HI    = $15           ; Current address high byte (was $01)
MON_MSG_PTR_LO     = $16           ; Message pointer low byte (was $02)
MON_MSG_PTR_HI     = $17           ; Message pointer high byte (was $03)
JUMP_VECTOR        = $18           ; Indirect jump vector (2 bytes: $18-$19) (was $04-$05)
SCREEN_PTR_LO      = $1A           ; Current screen memory pointer low (was $06)
SCREEN_PTR_HI      = $1B           ; Current screen memory pointer high (was $07)
SCRL_SRC_ADDR_LO   = $1C           ; Scroll source address low byte (was $08)
SCRL_SRC_ADDR_HI   = $1D           ; Scroll source address high byte (was $09)
SCRL_DEST_ADDR_LO  = $1E           ; Scroll destination address low byte (was $0A)
SCRL_DEST_ADDR_HI  = $1F           ; Scroll destination address high byte (was $0B)
SCRL_BYTE_CNT      = $20           ; Scroll byte counter (was $0C)
CMD_LINE_COUNT     = $21           ; Lines printed by current command (was $0D)
PAGE_ABORT_FLAG    = $22           ; Set to 1 if user pressed ESC (was $0E)
RNG_SEED           = $23           ; Random number generator seed (was $0F)
RNG_MAX            = $24           ; Maximum value for random number (was $10)
HEX_LOOKUP_TABLE   = $25           ; Hex lookup table (16 bytes: $25-$34) (was $F0-$FF)
```

**Verification:**
```bash
# Compile to check for syntax errors
ca65 src/kernel/kernel.asm -o build/kernel.o
# Should compile without errors
```

### Step 1.3: Update Kernel Header Comments

**File:** `src/kernel/kernel.asm`

**Location:** Lines ~22-26 (memory usage summary)

**Update:**
```assembly
; OLD:
; Zero Page:    $00-$10 (17 bytes used)
;   Monitor:    $00-$0F (16 bytes)

; NEW:
; Zero Page:    $14-$34 (33 bytes used)
;   Monitor:    $14-$24 (17 bytes) - core variables
;   HEX_LOOKUP: $25-$34 (16 bytes) - lookup table
```

### Step 1.4: Initialize Relocated Variables

**File:** `src/kernel/kernel.asm`

**Location:** RESET handler initialization code

**No Changes Required:**
- Zero page variables are initialized by the monitor's initialization code
- Constants are compile-time, not run-time
- Monitor already initializes all its variables correctly

**Verify:** Check RESET handler still initializes:
- MON_CURRADDR_LO/HI (now at $14-$15)
- SCREEN_PTR_LO/HI (now at $1A-$1B)
- Command buffer pointers

### Step 1.5: Relocate HEX_LOOKUP_TABLE Initialization

**File:** `src/kernel/kernel.asm`

**Search for:** HEX_LOOKUP_TABLE initialization code

**Current location:** Likely in DATA section or initialization routine

**Example current code:**
```assembly
; Initialize hex lookup table at $F0-$FF
INIT_HEX_TABLE:
    LDX #$00
INIT_HEX_LOOP:
    TXA
    AND #$0F
    CMP #$0A
    BCC INIT_HEX_DIGIT
    ADC #$06        ; Carry already set, adds 7 (A-F)
INIT_HEX_DIGIT:
    ADC #$30        ; Add ASCII '0'
    STA HEX_LOOKUP_TABLE,X
    INX
    CPX #$10
    BNE INIT_HEX_LOOP
    RTS
```

**No changes needed** - HEX_LOOKUP_TABLE constant was updated in Step 1.2, so this code will automatically initialize at new location ($25-$34).

**Verification:**
```assembly
; Add debug routine to dump hex table
DUMP_HEX_TABLE:
    LDX #$00
DUMP_HEX_LOOP:
    LDA HEX_LOOKUP_TABLE,X
    JSR PRINT_HEX_BYTE
    INX
    CPX #$10
    BNE DUMP_HEX_LOOP
    RTS
```

### Step 1.6: Test Compilation

```bash
# Full clean build
cd build
cmake -G Ninja -DBUILD_TESTS=ON ..
ninja

# Check for errors
# If successful, proceed to testing
```

### Step 1.7: Individual Command Testing

**Test Strategy:** Test each monitor command that uses relocated variables.

**Test Commands:**

1. **R: (Read) Command**
   - Uses: MON_CURRADDR_LO/HI, HEX_LOOKUP_TABLE
   - Test: `R:F000-F00F`
   - Expected: Display 16 bytes from $F000 in hex
   - Verify: Hex digits are correct (not garbage)

2. **W: (Write) Command**
   - Uses: MON_CURRADDR_LO/HI, HEX_LOOKUP_TABLE
   - Test: `W:8000 A9 20 4C`
   - Expected: Write 3 bytes to $8000
   - Verify: Read back with R:8000-8002 shows A9 20 4C

3. **Z: (Zero Page) Command**
   - Uses: MON_CURRADDR_LO/HI, CMD_LINE_COUNT, PAGE_ABORT_FLAG, HEX_LOOKUP_TABLE
   - Test: `Z:`
   - Expected: Display zero page with paging
   - Verify: Can see relocated monitor variables at $14-$34

4. **T: (Stack) Command**
   - Uses: MON_CURRADDR_LO/HI, CMD_LINE_COUNT, PAGE_ABORT_FLAG, HEX_LOOKUP_TABLE
   - Test: `T:`
   - Expected: Display stack page with paging
   - Verify: Paging works, ESC aborts

5. **H: (Help) Command**
   - Uses: MON_MSG_PTR_LO/HI, SCREEN_PTR_LO/HI
   - Test: `H:`
   - Expected: Display help text
   - Verify: Text displays correctly, no corruption

6. **C: (Clear) Command**
   - Uses: SCREEN_PTR_LO/HI, SCRL_* variables
   - Test: `C:`
   - Expected: Clear screen
   - Verify: Screen clears, cursor at top-left

7. **F: (Fill) Command**
   - Uses: MON_STARTADDR_LO/HI, MON_ENDADDR_LO/HI, MON_CURRADDR_LO/HI
   - Test: `F:8000-80FF,AA`
   - Expected: Fill 256 bytes with $AA
   - Verify: Read back with R:8000-80FF shows all AA

8. **M: (Move) Command**
   - Uses: MON_STARTADDR_LO/HI, MON_ENDADDR_LO/HI, MON_DEST_ADDR_LO/HI
   - Test: `M:8000-800F,9000,0`
   - Expected: Copy 16 bytes from $8000 to $9000
   - Verify: Both ranges contain same data

9. **X: (Search) Command**
   - Uses: MON_STARTADDR_LO/HI, MON_ENDADDR_LO/HI, MON_SEARCH_PATTERN
   - Test: `X:F000-FFFF,4C`
   - Expected: Find all JMP instructions in ROM
   - Verify: Multiple addresses displayed

**Test Script:**
```bash
#!/bin/bash
# test_relocation.sh

echo "Testing Zero Page Relocation..."

# Run emulator with test script
./build/6502_Kernel << EOF
R:F000-F00F
W:8000 A9 20 4C
R:8000-8002
Z:
T:
H:
C:
F:8000-80FF,AA
R:8000-8003
M:8000-800F,9000,0
R:9000-900F
X:F000-F010,4C
EOF

echo "Manual verification required:"
echo "1. Hex display shows correct digits"
echo "2. Memory write/read matches"
echo "3. Zero page shows variables at \$14-\$34"
echo "4. All commands execute without crash"
```

### Step 1.8: Regression Test Suite

**Run existing tests:**
```bash
cd build
ctest -R kernel_unit_tests --verbose
ctest -R monitor_integration --verbose
```

**Expected:** All tests pass
**If failures:** Debug relocated variable usage in failing tests

### Step 1.9: Document Changes

**File:** `docs/kernel_memory_map.md`

**Section to Update:** "Current Kernel Memory Allocations" → "Zero Page Usage"

**Changes:**
```markdown
### Zero Page Usage (Actual Allocations)

**RELOCATED FOR BASIC COMPATIBILITY:**

| Address | Symbol | Purpose |
|---------|--------|---------|
| $14-$15 | MON_CURRADDR_LO/HI | Monitor current address pointer (was $00-$01) |
| $16-$17 | MON_MSG_PTR_LO/HI | Message display pointer (was $02-$03) |
| $18-$19 | JUMP_VECTOR | Indirect jump vector (was $04-$05) |
| $1A-$1B | SCREEN_PTR_LO/HI | Current screen memory pointer (was $06-$07) |
| $1C-$1D | SCRL_SRC_ADDR_LO/HI | Scroll source address (was $08-$09) |
| $1E-$1F | SCRL_DEST_ADDR_LO/HI | Scroll destination address (was $0A-$0B) |
| $20 | SCRL_BYTE_CNT | Scroll byte counter (was $0C) |
| $21 | CMD_LINE_COUNT | Command line counter for paging (was $0D) |
| $22 | PAGE_ABORT_FLAG | Page abort flag (ESC handling) (was $0E) |
| $23 | RNG_SEED | Random number generator seed (was $0F) |
| $24 | RNG_MAX | Random number generator maximum (was $10) |
| $25-$34 | HEX_LOOKUP_TABLE | Hex digit lookup table (was $F0-$FF) |

**Reason for Relocation:** Zero page addresses $00-$02, $0A-$10, and $F0-$FF
conflict with BASIC interpreter zero page usage. Variables relocated to unused
gap at $14-$3F to enable BASIC integration.
```

### Step 1.10: Commit Milestone 1

```bash
git add src/kernel/kernel.asm docs/kernel_memory_map.md
git commit -m "Milestone 1: Zero page relocation for BASIC integration

- Relocated monitor zero page variables from $00-$10 and $F0-$FF to $14-$34
- Updated HEX_LOOKUP_TABLE location to avoid conflict with BASIC Decss buffer
- All monitor commands tested and working
- Documentation updated

Resolves 26 zero page conflicts with BASIC interpreter."
```

**Checkpoint:** Before proceeding, ensure:
- ✓ All monitor commands work correctly
- ✓ Test suite passes
- ✓ No regressions observed
- ✓ Documentation updated

---

## Milestone 2: B: Command Framework

**Estimated Time:** 2-3 hours
**Risk Level:** MEDIUM (new code, but isolated)
**Dependencies:** Milestone 1 complete

### Step 2.1: Add State Management Routines

**File:** `src/kernel/kernel.asm`

**Location:** Add new routines before command handlers section

**Code to Add:**

```assembly
; ================================================================
; BASIC INTEGRATION - STATE MANAGEMENT
; ================================================================

; Monitor state save area (in ROM, stores state during BASIC execution)
; Note: These are storage locations, not code
.segment "DATA"
MONITOR_STATE_SAVE:
MONITOR_SP_SAVE:        .RES 1     ; Stack pointer
MONITOR_SCREEN_X_SAVE:  .RES 1     ; Cursor X position
MONITOR_SCREEN_Y_SAVE:  .RES 1     ; Cursor Y position
MONITOR_MODE_SAVE:      .RES 1     ; Monitor mode

.segment "CODE"

; ----------------------------------------------------------------
; SAVE_MONITOR_STATE
; Save critical monitor state before entering BASIC
; Input: None
; Output: None
; Preserves: A, X, Y
; ----------------------------------------------------------------
SAVE_MONITOR_STATE:
    PHA                     ; Preserve A
    TXA
    PHA                     ; Preserve X
    TYA
    PHA                     ; Preserve Y

    ; Save stack pointer
    TSX
    STX MONITOR_SP_SAVE

    ; Save cursor position
    LDA CURSOR_X
    STA MONITOR_SCREEN_X_SAVE
    LDA CURSOR_Y
    STA MONITOR_SCREEN_Y_SAVE

    ; Save monitor mode
    LDA MON_MODE
    STA MONITOR_MODE_SAVE

    ; Restore registers
    PLA
    TAY
    PLA
    TAX
    PLA
    RTS

; ----------------------------------------------------------------
; RESTORE_MONITOR_STATE
; Restore monitor state after exiting BASIC
; Input: None
; Output: None
; Preserves: None (state restoration may modify registers)
; ----------------------------------------------------------------
RESTORE_MONITOR_STATE:
    ; Restore stack pointer
    LDX MONITOR_SP_SAVE
    TXS

    ; Clear monitor command buffer (CRITICAL!)
    LDA #$00
    STA MON_CMDPTR
    STA MON_CMDLEN

    LDX #MON_CMDBUF_LEN-1
CLEAR_CMD_BUF_LOOP:
    STA MON_CMDBUF,X
    DEX
    BPL CLEAR_CMD_BUF_LOOP

    ; Restore cursor position
    LDA MONITOR_SCREEN_X_SAVE
    STA CURSOR_X
    LDA MONITOR_SCREEN_Y_SAVE
    STA CURSOR_Y

    ; Restore monitor mode to command mode
    LDA #MON_MODE_CMD
    STA MON_MODE

    RTS

; ----------------------------------------------------------------
; INIT_BASIC_IO
; Initialize BASIC I/O vectors to use monitor routines
; Input: None
; Output: None
; Preserves: None
; ----------------------------------------------------------------
INIT_BASIC_IO:
    ; Set output vector to monitor PRINT_CHAR
    LDA #<PRINT_CHAR
    STA $0207               ; VEC_OUT low byte
    LDA #>PRINT_CHAR
    STA $0208               ; VEC_OUT high byte

    ; Set input vector to monitor GET_KEYSTROKE
    LDA #<GET_KEYSTROKE
    STA $0205               ; VEC_IN low byte
    LDA #>GET_KEYSTROKE
    STA $0206               ; VEC_IN high byte

    ; Set load/save vectors to stub routines (future enhancement)
    ; For now, point to RTS instructions
    LDA #<IO_STUB
    STA $0209               ; VEC_LD low byte
    STA $020B               ; VEC_SV low byte
    LDA #>IO_STUB
    STA $020A               ; VEC_LD high byte
    STA $020C               ; VEC_SV high byte

    RTS

; Stub routine for unimplemented load/save
IO_STUB:
    RTS
```

### Step 2.2: Add B: Command Messages

**File:** `src/kernel/kernel.asm`

**Location:** Message strings section (near other MSG_ definitions)

**Code to Add:**

```assembly
; BASIC integration messages
MSG_ENTERING_BASIC:
    .BYTE "ENTERING BASIC...", $0D, $0A, 0

MSG_LEAVING_BASIC:
    .BYTE "RETURNING TO MONITOR...", $0D, $0A, 0

MSG_NO_BASIC:
    .BYTE "?ERROR: BASIC ROM NOT FOUND", $0D, $0A, 0

MSG_BASIC_SIG_FAIL:
    .BYTE "?ERROR: BASIC ROM SIGNATURE INVALID", $0D, $0A, 0
```

### Step 2.3: Add B: Command Handler (Stub Version)

**File:** `src/kernel/kernel.asm`

**Location:** Command parser section (after other command checks)

**Find:** Existing command parser code, looks like:
```assembly
PARSE_COMMAND:
    ; ... existing code
    CMP #'R'
    BEQ HANDLE_R_CMD
    CMP #'W'
    BEQ HANDLE_W_CMD
    ; ... more commands
```

**Add after last command check:**

```assembly
    CMP #'B'
    BEQ CHECK_B_COLON
    ; ... continue with error handling

; Check for colon after B
CHECK_B_COLON:
    INY
    LDA MON_CMDBUF,Y
    CMP #':'
    BNE CMD_ERROR           ; Error if not 'B:'
    JMP HANDLE_B_CMD

; ... later in file, add handler:

; ----------------------------------------------------------------
; HANDLE_B_CMD
; Launch BASIC interpreter
; Input: None
; Output: Transfers control to BASIC (may not return)
; ----------------------------------------------------------------
HANDLE_B_CMD:
    ; Check for BASIC ROM signature at $C000
    ; Expecting JMP opcode ($4C) at start of BASIC
    LDA $C000
    CMP #$4C                ; JMP opcode
    BNE BASIC_NOT_FOUND

    ; Verify jump target is reasonable ($C000-$EFFF range)
    LDA $C002               ; High byte of jump target
    CMP #$C0                ; Should be >= $C0
    BCC BASIC_SIG_FAIL
    CMP #$F0                ; Should be < $F0 (monitor ROM)
    BCS BASIC_SIG_FAIL

    ; Clear screen for clean transition
    JSR CLEAR_SCREEN

    ; Print transition message
    LDA #<MSG_ENTERING_BASIC
    STA MON_MSG_PTR_LO
    LDA #>MSG_ENTERING_BASIC
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE

    ; Save monitor state
    JSR SAVE_MONITOR_STATE

    ; Initialize BASIC I/O vectors
    JSR INIT_BASIC_IO

    ; FOR TESTING: Just return to monitor prompt
    ; TODO: In Milestone 4, replace with: JMP LAB_COLD
    JSR RESTORE_MONITOR_STATE
    JMP MONITOR_PROMPT

BASIC_NOT_FOUND:
    LDA #<MSG_NO_BASIC
    STA MON_MSG_PTR_LO
    LDA #>MSG_NO_BASIC
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE
    JMP MONITOR_PROMPT

BASIC_SIG_FAIL:
    LDA #<MSG_BASIC_SIG_FAIL
    STA MON_MSG_PTR_LO
    LDA #>MSG_BASIC_SIG_FAIL
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE
    JMP MONITOR_PROMPT
```

### Step 2.4: Test B: Command Stub

**Compile:**
```bash
cd build
ninja
```

**Test:**
```bash
# Run emulator
./build/6502_Kernel

# At monitor prompt, type:
B:

# Expected output:
# ENTERING BASIC...
# RETURNING TO MONITOR...
# READY.

# Verify:
# 1. Message displays correctly
# 2. Returns to monitor prompt
# 3. Monitor commands still work after B:
```

**Test without BASIC ROM:**
If BASIC ROM is not loaded at $C000, should see:
```
?ERROR: BASIC ROM NOT FOUND
READY.
```

### Step 2.5: Test State Save/Restore

**Create test program:**

```assembly
; test_state_save.asm
; Verify state save/restore works correctly

TEST_STATE_SAVE:
    ; Set up test values
    LDA #$42
    LDX #$43
    LDY #$44
    PHA                     ; Push test value to stack

    ; Call save
    JSR SAVE_MONITOR_STATE

    ; Corrupt stack by pushing more values
    LDA #$FF
    PHA
    PHA
    PHA

    ; Call restore
    JSR RESTORE_MONITOR_STATE

    ; Pop test value - should be $42
    PLA
    CMP #$42
    BNE TEST_FAIL

    ; Verify MON_CMDPTR cleared
    LDA MON_CMDPTR
    BNE TEST_FAIL

    ; Verify MON_CMDLEN cleared
    LDA MON_CMDLEN
    BNE TEST_FAIL

    ; Test passed
    RTS

TEST_FAIL:
    ; Indicate failure (hang in infinite loop for debugging)
    JMP TEST_FAIL
```

**Add to test suite or manually verify behavior.**

### Step 2.6: Commit Milestone 2

```bash
git add src/kernel/kernel.asm
git commit -m "Milestone 2: B: command framework

- Added SAVE_MONITOR_STATE routine
- Added RESTORE_MONITOR_STATE routine with command buffer cleanup
- Added INIT_BASIC_IO routine for I/O vector setup
- Added B: command parser entry and stub handler
- Added BASIC integration messages
- Added BASIC ROM signature verification

B: command currently returns immediately (stub for testing).
Full BASIC integration in Milestone 4."
```

---

## Milestone 3: BASIC ROM Build System

**Estimated Time:** 3-4 hours
**Risk Level:** LOW (build system only, no runtime changes)
**Dependencies:** None (can be done in parallel with Milestones 1-2)

### Step 3.1: Review BASIC Configuration

**File:** `src/kernel/basic.asm`

**Find configuration section:**
Look for lines around 469-470:
```assembly
Ram_base          = $0300     ; start of user RAM
Ram_top           = $8000     ; end of user RAM+1
```

**Verify Settings:**
- `Ram_base = $0300` ✓ (after BASIC variables at $0200-$0268)
- `Ram_top = $C000` ← **CHANGE THIS** (was $8000)

**Make Change:**
```assembly
Ram_base          = $0300     ; start of user RAM (after BASIC variables)
Ram_top           = $C000     ; end of user RAM+1 (before BASIC ROM)
```

**Rationale:** BASIC ROM will be at $C000-$EFFF, so user programs can use $0300-$BFFF.

### Step 3.2: Add BYE Command to BASIC (Preparation)

**File:** `src/kernel/basic.asm`

**Step 3.2.1: Find Token Definitions**

Search for: `TK_NMI` (last token in current implementation)

**Example location (~line 361):**
```assembly
TK_IRQ            = TK_BITCLR+1     ; IRQ token
TK_NMI            = TK_IRQ+1        ; NMI token
```

**Add new token:**
```assembly
TK_IRQ            = TK_BITCLR+1     ; IRQ token
TK_NMI            = TK_IRQ+1        ; NMI token
TK_BYE            = TK_NMI+1        ; BYE token (NEW - return to monitor)
```

**Step 3.2.2: Find Keyword Table**

Search for: Primary command token strings (around line 7000+)

Look for pattern like:
```assembly
LAB_KEYWL:
    .BYTE "END", 0
    .BYTE "FOR", 0
    ; ... many more
    .BYTE "NMI", 0
```

**Add after "NMI":**
```assembly
    .BYTE "NMI", 0
    .BYTE "BYE", 0     ; NEW keyword
```

**Step 3.2.3: Find Command Execution Table**

Search for: Command jump table (pointer table to command handlers)

Look for pattern like:
```assembly
LAB_CTBL:
    .WORD CMD_END       ; END command
    .WORD CMD_FOR       ; FOR command
    ; ... many more
    .WORD CMD_NMI       ; NMI command
```

**Add after CMD_NMI:**
```assembly
    .WORD CMD_NMI       ; NMI command
    .WORD CMD_BYE       ; BYE command (NEW)
```

**Step 3.2.4: Add BYE Command Handler**

**Location:** After other command handlers (near end of code section)

**Add:**
```assembly
; ----------------------------------------------------------------
; CMD_BYE
; Return to monitor from BASIC
; This command exits BASIC and returns control to the monitor.
; Syntax: BYE
; ----------------------------------------------------------------
CMD_BYE:
    ; Note: RETURN_FROM_BASIC must be defined in kernel.asm
    ; and exported as a global symbol, OR we use a known address
    ; For now, use fixed address $FF12 (define in kernel.asm)
    JMP $FF12           ; Jump to monitor return handler
```

**Note:** We'll define RETURN_FROM_BASIC at $FF12 in kernel.asm (Milestone 4).

### Step 3.3: Create BASIC Linker Configuration

**File:** `config/memory_basic.cfg` (NEW FILE)

**Content:**
```
# EhBASIC Linker Configuration
# Loads BASIC ROM at $C000-$EFFF (12KB)

MEMORY {
    # BASIC ROM area
    BASIC: start = $C000, size = $3000, fill = yes, fillval = $00;
}

SEGMENTS {
    # Code segment (main BASIC interpreter)
    CODE: load = BASIC, type = ro, align = $100;

    # Data segment (lookup tables, string constants)
    DATA: load = BASIC, type = ro;
}
```

### Step 3.4: Update CMakeLists.txt

**File:** `CMakeLists.txt`

**Find:** Kernel build target section

**Add after kernel ROM build:**

```cmake
# ================================================================
# BASIC ROM Build Target
# ================================================================

# Assemble BASIC source
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/basic.o
    COMMAND ca65
        ${CMAKE_SOURCE_DIR}/src/kernel/basic.asm
        -o ${CMAKE_BINARY_DIR}/basic.o
        --listing ${CMAKE_BINARY_DIR}/basic.lst
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/basic.asm
    COMMENT "Assembling EhBASIC ROM..."
    VERBATIM
)

# Link BASIC ROM
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/basic.rom
    COMMAND ld65
        -C ${CMAKE_SOURCE_DIR}/config/memory_basic.cfg
        ${CMAKE_BINARY_DIR}/basic.o
        -o ${CMAKE_BINARY_DIR}/basic.rom
        -m ${CMAKE_BINARY_DIR}/basic.map
    DEPENDS
        ${CMAKE_BINARY_DIR}/basic.o
        ${CMAKE_SOURCE_DIR}/config/memory_basic.cfg
    COMMENT "Linking BASIC ROM..."
    VERBATIM
)

# Create BASIC ROM target
add_custom_target(basic_rom ALL
    DEPENDS ${CMAKE_BINARY_DIR}/basic.rom
    COMMENT "BASIC ROM build complete"
)

# Add BASIC ROM size check
add_custom_command(
    TARGET basic_rom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Checking BASIC ROM size..."
    COMMAND bash -c "
        SIZE=$$(stat -f%z ${CMAKE_BINARY_DIR}/basic.rom 2>/dev/null || stat -c%s ${CMAKE_BINARY_DIR}/basic.rom);
        if [ $$SIZE -gt 12288 ]; then
            echo 'ERROR: BASIC ROM exceeds 12288 bytes ($$SIZE bytes)';
            exit 1;
        else
            echo 'BASIC ROM size OK: '$$SIZE' bytes (12288 max)';
        fi
    "
    VERBATIM
)
```

### Step 3.5: Build BASIC ROM

```bash
cd build
cmake -G Ninja -DBUILD_TESTS=ON ..
ninja basic_rom

# Expected output:
# Assembling EhBASIC ROM...
# Linking BASIC ROM...
# Checking BASIC ROM size...
# BASIC ROM size OK: XXXX bytes (12288 max)
# BASIC ROM build complete
```

**Verify files created:**
```bash
ls -lh build/basic.rom build/basic.lst build/basic.map

# basic.rom should exist and be < 12KB
# basic.lst shows assembly listing
# basic.map shows memory map
```

### Step 3.6: Inspect BASIC ROM

**Check ROM header:**
```bash
# First 16 bytes should be executable code
hexdump -C build/basic.rom | head -n 2

# Should see something like:
# 00000000  4c XX XX ...  (JMP instruction at start)
```

**Check size:**
```bash
ls -l build/basic.rom
# Size should be ~8000-10000 bytes (EhBASIC is typically 8KB)
```

**Check memory map:**
```bash
cat build/basic.map | grep "LAB_COLD"
# Should show LAB_COLD at $C000 or nearby

cat build/basic.map | grep "LAB_WARM"
# Should show LAB_WARM reference
```

### Step 3.7: Configure Emulator to Load BASIC ROM

**File:** `main.cpp` or emulator configuration

**Find:** ROM loading code

**Add BASIC ROM loading:**
```cpp
// Load monitor ROM at $F000-$FFFF
loadROM("build/kernel.rom", 0xF000);

// Load BASIC ROM at $C000-$EFFF
loadROM("build/basic.rom", 0xC000);
```

**Or if using command-line:**
```bash
./build/6502_Kernel --kernel build/kernel.rom --basic build/basic.rom
```

### Step 3.8: Verify BASIC ROM in Memory

**Start emulator and use monitor to inspect:**
```bash
./build/6502_Kernel

# At monitor prompt:
R:C000-C00F

# Should see:
# C000: 4C XX XX ...  (JMP instruction)
# Not: 00 00 00 ... (would indicate ROM not loaded)
```

### Step 3.9: Commit Milestone 3

```bash
git add config/memory_basic.cfg CMakeLists.txt src/kernel/basic.asm
git commit -m "Milestone 3: BASIC ROM build system

- Created memory_basic.cfg linker configuration for BASIC ROM at $C000
- Updated CMakeLists.txt with basic_rom build target
- Added ROM size verification (max 12KB)
- Configured BASIC RAM range: $0300-$BFFF (user programs)
- Added BYE command keyword and token to BASIC
- Added BYE command handler (jumps to $FF12 monitor return)

BASIC ROM builds successfully and loads at $C000-$EFFF."
```

**Checkpoint:** Before proceeding, ensure:
- ✓ basic.rom builds without errors
- ✓ ROM size is within 12KB limit
- ✓ ROM loads at $C000 in emulator
- ✓ First bytes of ROM are executable code (JMP instruction)

---

## Milestone 4: BASIC Integration

**Estimated Time:** 3-4 hours
**Risk Level:** HIGH (full system integration)
**Dependencies:** Milestones 1, 2, and 3 complete

### Step 4.1: Add RETURN_FROM_BASIC to Monitor

**File:** `src/kernel/kernel.asm`

**Location:** JUMPS segment ($FF00-$FF11)

**Current jump table:**
```assembly
.segment "JUMPS"
    ; Entry point at $FF00
    JMP PRINT_CHAR          ; $FF00 - Character output
    NOP                     ; $FF03 - alignment
    NOP                     ; $FF04
    NOP                     ; $FF05
    NOP                     ; $FF06
    NOP                     ; $FF07
    NOP                     ; $FF08
    JMP GET_KEYSTROKE       ; $FF09 - Keyboard input
    ; ... up to $FF11
```

**Add RETURN_FROM_BASIC at $FF12:**

**Option 1:** If space in JUMPS segment:
```assembly
.segment "JUMPS"
    ; Entry point at $FF00
    JMP PRINT_CHAR          ; $FF00 - Character output
    NOP                     ; $FF03 - alignment
    NOP                     ; $FF04
    NOP                     ; $FF05
    NOP                     ; $FF06
    NOP                     ; $FF07
    NOP                     ; $FF08
    JMP GET_KEYSTROKE       ; $FF09 - Keyboard input
    NOP                     ; $FF0C - alignment
    NOP                     ; $FF0D
    NOP                     ; $FF0E
    NOP                     ; $FF0F
    NOP                     ; $FF10
    NOP                     ; $FF11
    JMP RETURN_FROM_BASIC   ; $FF12 - BASIC exit point (NEW)
```

**Option 2:** If JUMPS segment is full, put in CODE segment but at known address:
```assembly
; In CODE segment, use .org directive
.org $FF12
RETURN_FROM_BASIC_JUMP:
    JMP RETURN_FROM_BASIC_HANDLER

; Later in CODE segment:
RETURN_FROM_BASIC_HANDLER:
    ; Implementation below
```

**Choose Option 1** (simpler, if space available).

### Step 4.2: Implement RETURN_FROM_BASIC Handler

**File:** `src/kernel/kernel.asm`

**Location:** After B: command handler

**Code:**
```assembly
; ----------------------------------------------------------------
; RETURN_FROM_BASIC
; Return handler called when user exits BASIC with BYE command
; Entry point: $FF12 (called from BASIC CMD_BYE)
; Input: None (called via JMP from BASIC)
; Output: Returns to monitor prompt
; ----------------------------------------------------------------
RETURN_FROM_BASIC:
    ; Clear screen for clean transition
    JSR CLEAR_SCREEN

    ; Print transition message
    LDA #<MSG_LEAVING_BASIC
    STA MON_MSG_PTR_LO
    LDA #>MSG_LEAVING_BASIC
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE

    ; Restore monitor state
    JSR RESTORE_MONITOR_STATE

    ; Critical: Ensure command buffer is completely clear
    ; (RESTORE_MONITOR_STATE does this, but double-check)
    LDA #$00
    STA MON_CMDPTR
    STA MON_CMDLEN

    ; Return to monitor prompt
    JMP MONITOR_PROMPT
```

### Step 4.3: Update B: Command to Jump to BASIC

**File:** `src/kernel/kernel.asm`

**Find:** HANDLE_B_CMD routine (created in Milestone 2)

**Replace stub return with real BASIC jump:**

```assembly
HANDLE_B_CMD:
    ; ... (ROM signature checks, same as before)

    ; Clear screen for clean transition
    JSR CLEAR_SCREEN

    ; Print transition message
    LDA #<MSG_ENTERING_BASIC
    STA MON_MSG_PTR_LO
    LDA #>MSG_ENTERING_BASIC
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE

    ; Save monitor state
    JSR SAVE_MONITOR_STATE

    ; Initialize BASIC I/O vectors
    JSR INIT_BASIC_IO

    ; Jump to BASIC cold start
    ; LAB_COLD is at $C000 (first instruction in basic.rom)
    JMP $C000               ; ← CHANGED: Was stub return, now real jump

; ... (error handlers remain the same)
```

### Step 4.4: Verify Entry Point Address

**Check BASIC entry point:**

```bash
# Look at basic.map
cat build/basic.map | grep "LAB_COLD"

# Should show something like:
# LAB_COLD = $C000 or $C0XX
```

**If LAB_COLD is not at $C000:**

Option A: Update jump target in HANDLE_B_CMD:
```assembly
    JMP LAB_COLD_ADDRESS    ; Use actual address from map file
```

Option B: Add linker directive to force LAB_COLD at $C000:
```assembly
; In basic.asm, at start of CODE segment:
.segment "CODE"
.org $C000
LAB_COLD:
    ; ... (existing BASIC cold start code)
```

### Step 4.5: Build Complete System

```bash
cd build
ninja

# Should build both:
# - kernel.rom (monitor with B: command)
# - basic.rom (BASIC with BYE command)
```

**Verify no errors:**
```bash
echo $?
# Should output: 0 (success)
```

### Step 4.6: Test Basic Launch

**Start emulator:**
```bash
./build/6502_Kernel
```

**Test sequence:**
```
READY.
> B:
ENTERING BASIC...

READY
_
```

**Expected behavior:**
1. Monitor "READY." prompt appears
2. Type "B:" and press ENTER
3. "ENTERING BASIC..." message appears
4. BASIC "READY" prompt appears with cursor

**If BASIC doesn't start:**
- Check monitor output for error messages
- Verify $C000 contains JMP instruction (use R:C000-C002)
- Check basic.map for LAB_COLD address
- Verify I/O vectors initialized (use R:0205-020C)

### Step 4.7: Test BASIC Functionality

**Test 1: Simple PRINT**
```
READY
PRINT "HELLO WORLD"
HELLO WORLD
READY
_
```

**Test 2: Variable Assignment**
```
READY
A=10
PRINT A
10
READY
_
```

**Test 3: FOR Loop**
```
READY
FOR I=1 TO 5
PRINT I
NEXT I
1
2
3
4
5
READY
_
```

**Test 4: BASIC Program**
```
READY
10 PRINT "HELLO"
20 PRINT "WORLD"
LIST
10 PRINT "HELLO"
20 PRINT "WORLD"
READY
RUN
HELLO
WORLD
READY
_
```

### Step 4.8: Test Return to Monitor

**At BASIC prompt:**
```
READY
BYE
RETURNING TO MONITOR...

READY.
>
```

**Expected behavior:**
1. Type "BYE" at BASIC prompt
2. "RETURNING TO MONITOR..." message appears
3. Monitor "READY." prompt appears

**Verify monitor still works:**
```
READY.
> H:
(Help text should appear)

READY.
> R:F000-F00F
(Should display ROM contents)
```

### Step 4.9: Test Repeated Cycles

**Test rapid entry/exit:**
```bash
# Manually or via script:
B:
BYE
B:
PRINT 123
BYE
B:
BYE
```

**Expected:** No crashes, no memory corruption, smooth transitions.

### Step 4.10: Memory Corruption Test

**Pattern-fill zero page test:**
```assembly
; Before entering BASIC:
F:5B-EE,55    ; Fill BASIC zero page area with $55

; Enter BASIC:
B:

; In BASIC, run a program:
PRINT "TEST"

; Exit BASIC:
BYE

; Check monitor zero page not corrupted:
R:14-3F

; Should show monitor variables intact, not $55
```

**Expected:** Monitor zero page ($14-$3F) unchanged after BASIC use.

### Step 4.11: Commit Milestone 4

```bash
git add src/kernel/kernel.asm
git commit -m "Milestone 4: Complete BASIC integration

- Added RETURN_FROM_BASIC handler at $FF12
- Updated B: command to jump to BASIC cold start ($C000)
- Verified BASIC I/O integration with monitor routines
- Tested complete launch/return cycle
- Verified monitor functionality after BASIC usage

BASIC interpreter now fully functional:
- B: command launches BASIC
- User can write and run BASIC programs
- BYE command returns to monitor
- No memory corruption observed"
```

**Checkpoint:** Before proceeding, ensure:
- ✓ B: command successfully launches BASIC
- ✓ BASIC programs execute correctly
- ✓ BYE command returns to monitor
- ✓ Monitor commands work after BASIC
- ✓ No crashes in repeated entry/exit cycles

---

## Milestone 5: Testing and Validation

**Estimated Time:** 3-4 hours
**Risk Level:** LOW (testing only, no new code)
**Dependencies:** Milestone 4 complete

### Step 5.1: Run Unit Test Suite

```bash
cd build
ctest -R kernel_unit_tests --verbose
```

**Expected:** All existing tests pass (no regressions).

### Step 5.2: Create BASIC Integration Tests

**File:** `tests/test_basic_integration.cpp` (NEW)

**Content:**
```cpp
#include <gtest/gtest.h>
#include "CPU6502.h"
#include "Memory.h"

class BasicIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        cpu = new CPU6502();
        memory = new Memory();

        // Load kernel ROM
        memory->loadROM("kernel.rom", 0xF000);

        // Load BASIC ROM
        memory->loadROM("basic.rom", 0xC000);

        // Initialize CPU
        cpu->reset();
    }

    void TearDown() override {
        delete cpu;
        delete memory;
    }

    CPU6502* cpu;
    Memory* memory;
};

TEST_F(BasicIntegrationTest, BCommandLaunchesBASIC) {
    // Simulate typing "B:" at monitor prompt
    // ... test implementation

    // Verify BASIC started
    // Check program counter is in BASIC ROM range ($C000-$EFFF)
    EXPECT_GE(cpu->getPC(), 0xC000);
    EXPECT_LT(cpu->getPC(), 0xF000);
}

TEST_F(BasicIntegrationTest, BYECommandReturnsToMonitor) {
    // Launch BASIC
    // ... implementation

    // Execute BYE command
    // ... implementation

    // Verify returned to monitor
    // Check program counter is in monitor ROM range ($F000-$FFFF)
    EXPECT_GE(cpu->getPC(), 0xF000);
}

TEST_F(BasicIntegrationTest, MonitorZeroPageIntact) {
    // Fill monitor zero page with test pattern
    for (int i = 0x14; i <= 0x34; i++) {
        memory->write(i, 0xAA);
    }

    // Launch BASIC
    // ... implementation

    // Run BASIC program
    // ... implementation

    // Exit BASIC
    // ... implementation

    // Verify monitor zero page unchanged
    for (int i = 0x14; i <= 0x34; i++) {
        EXPECT_EQ(memory->read(i), 0xAA)
            << "Memory corruption at $" << std::hex << i;
    }
}

TEST_F(BasicIntegrationTest, CommandBufferClearedOnReturn) {
    // Fill monitor command buffer
    for (int i = 0x0200; i < 0x0250; i++) {
        memory->write(i, 0xFF);
    }

    // Launch BASIC, run program, exit
    // ... implementation

    // Verify command buffer cleared
    EXPECT_EQ(memory->read(0x0269), 0x00) << "MON_CMDPTR not cleared";
    EXPECT_EQ(memory->read(0x026A), 0x00) << "MON_CMDLEN not cleared";
}

TEST_F(BasicIntegrationTest, IOVectorsInitialized) {
    // Launch BASIC
    // ... implementation

    // Verify VEC_OUT points to PRINT_CHAR ($FF00)
    uint16_t vec_out = memory->read(0x0207) | (memory->read(0x0208) << 8);
    EXPECT_EQ(vec_out, 0xFF00) << "VEC_OUT not initialized correctly";

    // Verify VEC_IN points to GET_KEYSTROKE ($FF09)
    uint16_t vec_in = memory->read(0x0205) | (memory->read(0x0206) << 8);
    EXPECT_EQ(vec_in, 0xFF09) << "VEC_IN not initialized correctly";
}

TEST_F(BasicIntegrationTest, RepeatedEntryCycles) {
    // Test 10 rapid entry/exit cycles
    for (int cycle = 0; cycle < 10; cycle++) {
        // Launch BASIC
        // ... implementation

        // Exit BASIC
        // ... implementation

        // Verify system stable
        EXPECT_TRUE(cpu->isStable()) << "Crash on cycle " << cycle;
    }
}
```

**Add to CMakeLists.txt:**
```cmake
add_executable(basic_integration_tests
    tests/test_basic_integration.cpp
    # ... other sources
)
target_link_libraries(basic_integration_tests gtest_main)
add_test(NAME basic_integration COMMAND basic_integration_tests)
```

### Step 5.3: Run Integration Tests

```bash
cd build
ninja basic_integration_tests
ctest -R basic_integration --verbose
```

**Expected:** All tests pass.

### Step 5.4: Manual Test Scenarios

**Execute each test from Milestone 4, Step 4.7 again:**

1. ✓ Simple PRINT
2. ✓ Variable assignment
3. ✓ FOR loop
4. ✓ BASIC program (LIST, RUN)
5. ✓ BYE command
6. ✓ Monitor commands after BASIC
7. ✓ Repeated entry/exit cycles

**Additional tests:**

**Test 8: Long BASIC Program**
```
READY
10 FOR I=1 TO 100
20 PRINT "LINE ";I
30 NEXT I
RUN
(Should print 100 lines without crash or corruption)
```

**Test 9: Nested Loops**
```
READY
10 FOR I=1 TO 5
20 FOR J=1 TO 3
30 PRINT I;",";J
40 NEXT J
50 NEXT I
RUN
```

**Test 10: String Operations**
```
READY
A$="HELLO"
B$="WORLD"
PRINT A$+" "+B$
HELLO WORLD
```

### Step 5.5: Stress Testing

**Test 11: Maximum Line Length**
```
READY
PRINT "AAAAAAAAAA..." (78 characters)
(Should wrap correctly)
```

**Test 12: Out of Memory**
```
READY
10 DIM A(1000)
(May fail with "OUT OF MEMORY" - this is correct BASIC behavior)
```

**Test 13: Rapid Input**
```
READY
INPUT X
(Type rapidly: 123456789 <ENTER>)
PRINT X
123456789
```

### Step 5.6: Boundary Condition Tests

**Test 14: Zero Page Boundaries**
```bash
# Write to $13 (just before monitor area)
W:13 FF

# Enter BASIC, run program
B:
PRINT "TEST"
BYE

# Verify $13 unchanged, $14 intact
R:13-15
```

**Test 15: Command Buffer Boundary**
```bash
# Write to $0268 (end of BASIC Ibuffs)
W:0268 FF

# Write to $0269 (start of monitor vars)
W:0269 EE

# Enter BASIC, run program
B:
PRINT "TEST"
BYE

# Verify $0269 restored to 0 (cleared)
R:0268-026A
```

### Step 5.7: Performance Testing

**Test 16: Benchmark BASIC Execution**
```
READY
10 FOR I=1 TO 1000
20 NEXT I
(Time execution - should complete in reasonable time)
```

**Test 17: Screen Scroll Performance**
```
READY
FOR I=1 TO 100
PRINT I
NEXT I
(Should scroll smoothly without artifacts)
```

### Step 5.8: Document Test Results

**Create:** `tests/basic_integration_results.md`

**Content:**
```markdown
# BASIC Integration Test Results

Date: YYYY-MM-DD
Tester: [Name]

## Unit Tests
- kernel_unit_tests: ✓ PASS (XX tests)
- basic_integration_tests: ✓ PASS (XX tests)

## Manual Tests
1. Simple PRINT: ✓ PASS
2. Variable assignment: ✓ PASS
3. FOR loop: ✓ PASS
... (all 17 tests)

## Issues Found
(List any issues discovered during testing)

## Performance Metrics
- BASIC launch time: ~Xms
- BASIC return time: ~Xms
- Screen scroll rate: ~X lines/sec

## Conclusion
BASIC integration is [STABLE | NEEDS WORK | FAILED].
```

### Step 5.9: Commit Milestone 5

```bash
git add tests/test_basic_integration.cpp tests/basic_integration_results.md
git commit -m "Milestone 5: Testing and validation

- Created basic_integration_tests suite
- Executed all 17 manual test scenarios
- Verified zero page integrity
- Verified command buffer cleanup
- Verified I/O vector initialization
- Stress tested with long programs and rapid cycles
- Documented test results

All tests pass. BASIC integration verified stable."
```

---

## Milestone 6: Documentation

**Estimated Time:** 2-3 hours
**Risk Level:** NONE (documentation only)
**Dependencies:** All previous milestones complete

### Step 6.1: Create basic_command.md

**File:** `docs/basic_command.md` (NEW)

**Content:** (Use template from integration-architecture.md, Section "User Documentation")

**Structure:**
```markdown
# B: Command - BASIC Interpreter

## Overview
The B: command launches the EhBASIC 2.22p5 interpreter...

## Syntax
`B:`

## Description
[Full description of command]

## Usage Examples
[Multiple examples]

## BASIC Commands Reference
[Quick reference to BASIC commands]

## Technical Details
- Memory Usage: $C000-$EFFF (BASIC ROM)
- RAM Usage: $0300-$BFFF (user programs)
- Zero Page: $00-$13, $5B-$BB, $BC-$E1, $EF-$FF
- I/O Vectors: VEC_IN ($0205), VEC_OUT ($0207)

## Returning to Monitor
Type `BYE` at BASIC prompt.

## Limitations
- Cold start only (no program persistence between sessions)
- No LOAD/SAVE support (future enhancement)
- Screen memory shared with monitor ($0400-$07FF)

## See Also
- [EhBASIC Documentation](http://www.6502.org/users/mycorner/6502/ehbasic/index.html)
- [Kernel Memory Map](kernel_memory_map.md)
- [Kernel Command Infrastructure](kernel_command_infrastructure.md)
```

### Step 6.2: Update kernel_memory_map.md

**File:** `docs/kernel_memory_map.md`

**Section to add:** "BASIC Integration Memory Layout"

**Content:**
```markdown
## BASIC Integration Memory Layout

The kernel supports an integrated BASIC interpreter (EhBASIC 2.22p5) alongside
the monitor program. Memory is carefully partitioned to avoid conflicts.

### Zero Page Allocation Strategy

**BASIC Zero Page Usage:**
- $00-$02: Warm start vectors
- $0A-$13: USR function, terminal control
- $5B-$BB: Core interpreter variables
- $BC-$D7: Get byte subroutines
- $D8-$E1: PRNG and interrupt handlers
- $EF-$FF: Decimal string buffer

**Monitor Zero Page Usage (Relocated):**
- $14-$34: Monitor variables and HEX_LOOKUP_TABLE

**Available Gaps:**
- $03-$09: 7 bytes (unused)
- $35-$5A: 38 bytes (unused)
- $E2-$EE: 13 bytes (unused)

### Extended RAM Partitioning

**$0200-$02FF (256 bytes):**
- $0200-$0268: BASIC variables and input buffer (105 bytes)
- $0269-$02DE: Monitor variables (118 bytes)
- $02DF-$02FF: Unused (33 bytes)

**Overlap Strategy:**
Monitor command buffer ($0200-$024F) overlaps BASIC variables, but this is
safe because they are mutually exclusive (monitor active OR BASIC active,
never both simultaneously). The monitor clears its command buffer when
returning from BASIC.

### ROM Allocation

**$C000-$EFFF: BASIC ROM (12KB)**
- EhBASIC 2.22p5 interpreter
- Entry point: LAB_COLD at $C000
- Exit point: CMD_BYE jumps to $FF12

**$F000-$FFFF: Monitor ROM (4KB)**
- Monitor commands and infrastructure
- B: command launches BASIC
- RETURN_FROM_BASIC handler at $FF12
- I/O routines: PRINT_CHAR ($FF00), GET_KEYSTROKE ($FF09)

### State Management

**Monitor State Save Area:**
Located in monitor ROM, stores:
- Stack pointer
- Cursor position
- Monitor mode

Saved when entering BASIC, restored when exiting.

### I/O Vector Integration

**BASIC I/O Vectors ($0205-$020C):**
- VEC_IN ($0205): Points to GET_KEYSTROKE ($FF09)
- VEC_OUT ($0207): Points to PRINT_CHAR ($FF00)
- VEC_LD ($0209): Stub (future enhancement)
- VEC_SV ($020B): Stub (future enhancement)

Initialized by monitor when B: command is executed.
```

### Step 6.3: Update kernel_flow.md

**File:** `docs/kernel_flow.md`

**Section to add:** "B: Command and BASIC Integration Flow"

**Content:**
```markdown
## B: Command Flow

### Entry to BASIC

```
User types "B:" at monitor prompt
         │
         ▼
   PARSE_COMMAND
         │
         ├─ Verify "B:"
         ▼
  CHECK_B_COLON
         │
         ├─ Check for BASIC ROM signature at $C000
         ├─ Verify ROM is valid (JMP instruction)
         ▼
  HANDLE_B_CMD
         │
         ├─ Clear screen
         ├─ Display "ENTERING BASIC..." message
         ├─ Call SAVE_MONITOR_STATE
         ├─ Call INIT_BASIC_IO (set VEC_IN, VEC_OUT)
         ▼
   Jump to $C000 (LAB_COLD)
         │
         ▼
   BASIC Cold Start
         │
         ├─ Initialize BASIC variables
         ├─ Clear BASIC RAM ($0300-$BFFF)
         ├─ Display "READY" prompt
         ▼
   BASIC Command Loop
         │
         ├─ Wait for user input (via VEC_IN → GET_KEYSTROKE)
         ├─ Parse and execute BASIC commands
         ├─ Display output (via VEC_OUT → PRINT_CHAR)
         └─ Loop until BYE command
```

### Exit from BASIC

```
User types "BYE" at BASIC prompt
         │
         ▼
  BASIC Parser
         │
         ├─ Recognize TK_BYE token
         ▼
    CMD_BYE
         │
         ├─ Jump to $FF12
         ▼
 RETURN_FROM_BASIC
         │
         ├─ Clear screen
         ├─ Display "RETURNING TO MONITOR..." message
         ├─ Call RESTORE_MONITOR_STATE
         │   ├─ Restore stack pointer
         │   ├─ Clear MON_CMDBUF
         │   ├─ Reset MON_CMDPTR, MON_CMDLEN
         │   └─ Restore monitor mode
         ▼
   MONITOR_PROMPT
         │
         ▼
   Wait for next command
```

### State Transitions

```
Monitor Mode ─[B: command]─► BASIC Mode
      ▲                           │
      │                           │
      └────[BYE command]──────────┘
```
```

### Step 6.4: Update kernel_command_infrastructure.md

**File:** `docs/kernel_command_infrastructure.md`

**Add new section:** "B: Command (BASIC Interpreter)"

**Content:**
```markdown
## B: Command (BASIC Interpreter)

### Command Definition
- **Syntax:** `B:`
- **Description:** Launches the EhBASIC 2.22p5 interpreter
- **Category:** System Control
- **Privilege:** User

### Implementation Details

**Command Handler:** `HANDLE_B_CMD`
**Location:** kernel.asm
**Entry Point:** Called from PARSE_COMMAND

**Prerequisites:**
1. BASIC ROM must be present at $C000-$EFFF
2. ROM signature verified (JMP instruction at $C000)
3. Sufficient system RAM available

**Execution Steps:**
1. Verify BASIC ROM signature
2. Clear screen for clean transition
3. Display "ENTERING BASIC..." message
4. Save monitor state (SAVE_MONITOR_STATE)
5. Initialize BASIC I/O vectors (INIT_BASIC_IO)
6. Transfer control to LAB_COLD ($C000)

**Error Conditions:**
- "?ERROR: BASIC ROM NOT FOUND" - No ROM at $C000
- "?ERROR: BASIC ROM SIGNATURE INVALID" - Invalid ROM header

### Associated Routines

**SAVE_MONITOR_STATE:**
- Saves stack pointer to MONITOR_SP_SAVE
- Saves cursor position
- Saves monitor mode

**INIT_BASIC_IO:**
- Sets VEC_OUT ($0207) = PRINT_CHAR ($FF00)
- Sets VEC_IN ($0205) = GET_KEYSTROKE ($FF09)
- Sets VEC_LD ($0209) = IO_STUB (no-op)
- Sets VEC_SV ($020B) = IO_STUB (no-op)

**RESTORE_MONITOR_STATE:**
- Restores stack pointer
- Clears monitor command buffer (CRITICAL)
- Resets MON_CMDPTR and MON_CMDLEN
- Restores cursor position
- Sets monitor mode to MON_MODE_CMD

### Return Mechanism

**Exit Command:** `BYE` (BASIC command)
**Exit Handler:** CMD_BYE (in basic.asm)
**Jump Target:** $FF12 (RETURN_FROM_BASIC)

**Return Steps:**
1. User types BYE at BASIC prompt
2. BASIC parser calls CMD_BYE
3. CMD_BYE jumps to $FF12
4. RETURN_FROM_BASIC clears screen
5. Displays "RETURNING TO MONITOR..."
6. Calls RESTORE_MONITOR_STATE
7. Returns to MONITOR_PROMPT

### Memory Impact

**Zero Page:** Monitor variables relocated to $14-$34
**Extended RAM:** Command buffer overlap at $0200-$024F (cleared on return)
**ROM:** ~180 bytes additional code in monitor ROM
**BASIC ROM:** 8-10KB at $C000-$EFFF

### Integration Points

**I/O Integration:**
- BASIC PRINT uses PRINT_CHAR via VEC_OUT
- BASIC INPUT uses GET_KEYSTROKE via VEC_IN
- Screen memory ($0400-$07FF) shared

**Memory Safety:**
- Zero page conflicts resolved by relocation
- Command buffer conflicts resolved by cleanup
- Stack shared (standard 6502 practice)

### Testing Requirements

**Unit Tests:**
- test_basic_command_dispatch
- test_state_save_restore
- test_io_vector_init
- test_command_buffer_cleanup

**Integration Tests:**
- test_basic_launch_and_return
- test_basic_program_execution
- test_monitor_after_basic
- test_repeated_cycles

### Future Enhancements

1. Warm start support (preserve programs)
2. LOAD/SAVE integration (use monitor L:/S: commands)
3. Breakpoint support (break to monitor from BASIC)
4. Variable inspection (examine BASIC vars from monitor)
```

### Step 6.5: Update command_help.md

**File:** `docs/command_help.md`

**Add B: command entry (alphabetically):**

```markdown
### B: - Enter BASIC Interpreter

**Syntax:** `B:`

**Description:** Launches the EhBASIC 2.22p5 interpreter for high-level
programming. Allows writing and executing BASIC programs interactively.

**Examples:**
```
READY.
> B:
ENTERING BASIC...

READY
10 PRINT "HELLO"
20 PRINT "WORLD"
RUN
HELLO
WORLD
READY
BYE
RETURNING TO MONITOR...

READY.
>
```

**Notes:**
- Type `BYE` to return to monitor
- Program is lost on exit (cold start only)
- Full BASIC command set available

**See Also:** [B: Command Documentation](basic_command.md)
```

### Step 6.6: Update In-Monitor Help System

**File:** `src/kernel/kernel.asm`

**Find:** Help message strings

**Add B: command to help display:**

```assembly
MSG_HELP_B:
    .BYTE "B:     ENTER BASIC INTERPRETER", $0D, $0A, 0
```

**Update HANDLE_H_CMD routine to include:**

```assembly
HANDLE_H_CMD:
    ; ... existing help messages
    LDA #<MSG_HELP_B
    STA MON_MSG_PTR_LO
    LDA #>MSG_HELP_B
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE
    ; ... continue with other help messages
```

### Step 6.7: Update README.md

**File:** `README.md`

**Section to update:** "Features"

**Add:**
```markdown
## Features

### Monitor Commands
- **Memory Operations:** R: (read), W: (write), F: (fill), M: (move/copy), X: (search)
- **Program Execution:** G: (go/run)
- **System Display:** Z: (zero page), T: (stack), H: (help), C: (clear screen)
- **BASIC Interpreter:** B: (launch BASIC) - NEW!

### BASIC Interpreter
- Full EhBASIC 2.22p5 support
- Launch with `B:` command from monitor
- Write and execute BASIC programs interactively
- Return to monitor with `BYE` command
- Integrated I/O with monitor routines

### Examples

**Monitor mode:**
```bash
READY.
> R:F000-F00F
(Displays ROM contents)

> W:8000 A9 20 4C 00 FF
(Writes machine code)

> B:
(Launches BASIC)
```

**BASIC mode:**
```basic
READY
10 FOR I=1 TO 10
20 PRINT "LINE ";I
30 NEXT I
RUN
LINE 1
LINE 2
...
LINE 10
READY
BYE
(Returns to monitor)
```
```

**Section to add:** "Memory Layout"

```markdown
## Memory Layout

| Address Range | Size | Contents |
|---------------|------|----------|
| $0000-$00FF | 256 bytes | Zero Page (monitor: $14-$34, BASIC: various) |
| $0100-$01FF | 256 bytes | Stack (shared) |
| $0200-$02FF | 256 bytes | System variables (BASIC + monitor) |
| $0300-$BFFF | 48 KB | User program RAM |
| $C000-$EFFF | 12 KB | BASIC ROM (EhBASIC 2.22p5) |
| $F000-$FFFF | 4 KB | Monitor ROM |

**Note:** Zero page carefully partitioned to avoid conflicts between monitor and BASIC.
Monitor variables relocated to $14-$34 gap unused by BASIC.
```

### Step 6.8: Commit Milestone 6

```bash
git add docs/basic_command.md docs/kernel_memory_map.md docs/kernel_flow.md
git add docs/kernel_command_infrastructure.md docs/command_help.md README.md
git add src/kernel/kernel.asm
git commit -m "Milestone 6: Documentation

- Created docs/basic_command.md (comprehensive B: command guide)
- Updated docs/kernel_memory_map.md (BASIC integration layout)
- Updated docs/kernel_flow.md (B: command flow diagrams)
- Updated docs/kernel_command_infrastructure.md (B: command details)
- Updated docs/command_help.md (B: command reference)
- Updated README.md (features and memory layout)
- Updated monitor help system (added B: to H: command output)

All documentation complete and consistent."
```

---

## Final Integration

### Final Build and Test

```bash
# Clean build
cd build
rm -rf *
cmake -G Ninja -DBUILD_TESTS=ON ..
ninja

# Run all tests
ctest --verbose

# Manual smoke test
./build/6502_Kernel
# Type: B:
# Type: PRINT "SUCCESS"
# Type: BYE
# Type: H:
# Verify all works correctly
```

### Create Release Tag

```bash
git tag -a v1.1.0-basic -m "Version 1.1.0: BASIC Interpreter Integration

Features:
- B: command launches EhBASIC 2.22p5 interpreter
- BYE command returns to monitor
- Zero page conflicts resolved (26 addresses relocated)
- I/O integration complete (PRINT_CHAR, GET_KEYSTROKE)
- Full test coverage
- Complete documentation

Tested and verified stable on 6502 emulator."

git push origin feature/basic-interpreter
git push origin v1.1.0-basic
```

### Create Pull Request

**Title:** "Add BASIC Interpreter Integration (EhBASIC 2.22p5)"

**Description:**
```markdown
## Summary
This PR integrates the EhBASIC 2.22p5 interpreter into the 6502 kernel monitor,
enabling users to write and execute BASIC programs alongside monitor commands.

## Changes
- **Zero Page Relocation:** Relocated monitor variables from $00-$10 and $F0-$FF
  to $14-$34 to resolve 26 conflicts with BASIC
- **B: Command:** New command to launch BASIC interpreter from monitor
- **BYE Command:** New BASIC command to return to monitor
- **I/O Integration:** BASIC uses monitor PRINT_CHAR and GET_KEYSTROKE routines
- **Build System:** Added BASIC ROM build target and linker configuration
- **State Management:** Save/restore routines for clean mode transitions
- **Testing:** Comprehensive unit and integration tests
- **Documentation:** Complete user and technical documentation

## Testing
- ✓ All existing monitor tests pass (no regressions)
- ✓ BASIC integration tests pass (launch, execute, return)
- ✓ Memory corruption tests pass (zero page, command buffer)
- ✓ Stress tests pass (repeated cycles, long programs)
- ✓ Manual testing complete (17 test scenarios)

## Memory Impact
- Monitor ROM: +180 bytes (3593 / 4096 used, 87.7%)
- BASIC ROM: ~8KB (loaded at $C000-$EFFF)
- Zero page: Monitor relocated to $14-$34 (44 bytes)

## Documentation
- [B: Command Documentation](docs/basic_command.md)
- [Memory Conflict Analysis](enhancements/add-basic-interpreter/memory-conflict-analysis.md)
- [Integration Architecture](enhancements/add-basic-interpreter/integration-architecture.md)
- Updated: kernel_memory_map.md, kernel_flow.md, command_help.md, README.md

## Demo
[Optional: Video or screenshot of B: command in action]

## Breaking Changes
None. All existing monitor commands work identically.

## Future Enhancements
- LOAD/SAVE integration for BASIC programs
- Warm start support (preserve programs between sessions)
- Debugger integration (breakpoints, variable inspection)
```

---

## Rollback Procedures

### If Milestone 1 Fails (Zero Page Relocation)

**Symptom:** Monitor commands crash or produce incorrect output

**Rollback:**
```bash
git checkout kernel.asm.backup
# Or:
git reset --hard HEAD~1  # If committed
```

**Recovery:**
1. Review relocation addresses for typos
2. Check HEX_LOOKUP_TABLE initialization
3. Verify no missed variable references
4. Re-test each command individually

### If Milestone 2-3 Fails (B: Command or Build)

**Symptom:** B: command doesn't compile or BASIC ROM build fails

**Rollback:**
```bash
git revert HEAD  # Revert last commit
# Or:
git reset --hard <milestone-1-commit-hash>
```

**Recovery:**
1. Check basic.asm syntax for BYE command additions
2. Verify memory_basic.cfg path references
3. Review CMakeLists.txt for typos
4. Check linker map for address conflicts

### If Milestone 4 Fails (Integration)

**Symptom:** B: command crashes or BASIC doesn't start

**Rollback:**
```bash
git checkout src/kernel/kernel.asm HEAD~1
ninja  # Rebuild with previous version
```

**Recovery:**
1. Verify $C000 contains valid code (use R:C000-C010)
2. Check LAB_COLD address in basic.map
3. Verify I/O vectors initialized (R:0205-020C)
4. Add debug output in HANDLE_B_CMD
5. Test SAVE_MONITOR_STATE and RESTORE_MONITOR_STATE independently

---

## Success Criteria Checklist

### Functional Requirements
- [ ] User can launch BASIC with "B:" command
- [ ] BASIC interpreter runs and accepts input
- [ ] BASIC programs execute correctly
- [ ] User can return to monitor with "BYE" command
- [ ] Monitor functions correctly after BASIC usage
- [ ] No crashes during normal operation
- [ ] No memory corruption observed

### Technical Requirements
- [ ] Zero page conflicts resolved (all 26 addresses)
- [ ] Monitor variables relocated to $14-$34
- [ ] HEX_LOOKUP_TABLE relocated from $F0-$FF
- [ ] Command buffer cleanup on BASIC exit
- [ ] I/O vectors correctly initialized
- [ ] BASIC ROM builds without errors
- [ ] Monitor ROM within 4KB limit
- [ ] BASIC ROM within 12KB limit

### Quality Requirements
- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] Manual test scenarios complete (17 tests)
- [ ] No regressions in existing commands
- [ ] Code follows project style guidelines
- [ ] Assembly code well-commented
- [ ] Memory usage documented

### Documentation Requirements
- [ ] basic_command.md created
- [ ] kernel_memory_map.md updated
- [ ] kernel_flow.md updated
- [ ] kernel_command_infrastructure.md updated
- [ ] command_help.md updated
- [ ] README.md updated
- [ ] In-monitor help updated (H: command)
- [ ] Technical architecture documented

---

## Implementation Estimated Hours

| Milestone | Tasks | Estimated Hours | Risk Level |
|-----------|-------|----------------|------------|
| 1. Zero Page Relocation | Update constants, test commands | 4-6 hours | HIGH |
| 2. B: Command Framework | State management, I/O vectors | 2-3 hours | MEDIUM |
| 3. BASIC ROM Build | Build config, BYE command | 3-4 hours | LOW |
| 4. BASIC Integration | Connect components, test | 3-4 hours | HIGH |
| 5. Testing & Validation | Test suite, manual tests | 3-4 hours | LOW |
| 6. Documentation | Create/update docs | 2-3 hours | NONE |
| **Total** | | **17-24 hours** | |

**Recommended Schedule:**
- Day 1: Milestones 1-2 (6-9 hours)
- Day 2: Milestones 3-4 (6-8 hours)
- Day 3: Milestones 5-6 (5-7 hours)

---

## Post-Implementation Tasks

### Code Review Checklist
- [ ] Zero page addresses verified against BASIC usage
- [ ] All monitor commands tested individually
- [ ] State save/restore covers all necessary variables
- [ ] Command buffer cleanup is comprehensive
- [ ] I/O vector initialization is correct
- [ ] BASIC ROM signature check is robust
- [ ] Error messages are clear and helpful
- [ ] Code comments are accurate and complete

### Performance Verification
- [ ] Monitor commands respond quickly
- [ ] BASIC launch time acceptable (< 500ms)
- [ ] BASIC return time acceptable (< 500ms)
- [ ] Screen updates smooth
- [ ] No observable lag in I/O operations

### User Acceptance
- [ ] User guide clear and complete
- [ ] Examples work as documented
- [ ] Error messages helpful
- [ ] Workflow intuitive
- [ ] Help system comprehensive

---

## Appendices

### Appendix A: Variable Relocation Quick Reference

| Old Addr | New Addr | Symbol | Offset |
|----------|----------|--------|--------|
| $00 | $14 | MON_CURRADDR_LO | +$14 |
| $01 | $15 | MON_CURRADDR_HI | +$14 |
| $02 | $16 | MON_MSG_PTR_LO | +$14 |
| $03 | $17 | MON_MSG_PTR_HI | +$14 |
| $04 | $18 | JUMP_VECTOR | +$14 |
| $05 | $19 | JUMP_VECTOR+1 | +$14 |
| $06 | $1A | SCREEN_PTR_LO | +$14 |
| $07 | $1B | SCREEN_PTR_HI | +$14 |
| $08 | $1C | SCRL_SRC_ADDR_LO | +$14 |
| $09 | $1D | SCRL_SRC_ADDR_HI | +$14 |
| $0A | $1E | SCRL_DEST_ADDR_LO | +$14 |
| $0B | $1F | SCRL_DEST_ADDR_HI | +$14 |
| $0C | $20 | SCRL_BYTE_CNT | +$14 |
| $0D | $21 | CMD_LINE_COUNT | +$14 |
| $0E | $22 | PAGE_ABORT_FLAG | +$14 |
| $0F | $23 | RNG_SEED | +$14 |
| $10 | $24 | RNG_MAX | +$14 |
| $F0-$FF | $25-$34 | HEX_LOOKUP_TABLE | -$CB |

### Appendix B: Key Memory Addresses

| Address | Symbol/Purpose | Access |
|---------|---------------|--------|
| $C000 | LAB_COLD (BASIC cold start) | JMP target |
| $FF00 | PRINT_CHAR (monitor I/O) | JSR target |
| $FF09 | GET_KEYSTROKE (monitor I/O) | JSR target |
| $FF12 | RETURN_FROM_BASIC | JMP target |
| $0205 | VEC_IN | 2-byte pointer |
| $0207 | VEC_OUT | 2-byte pointer |
| $0269 | MON_CMDPTR | 1-byte variable |
| $026A | MON_CMDLEN | 1-byte variable |

### Appendix C: Build Commands Reference

```bash
# Full clean build
cd build
rm -rf *
cmake -G Ninja -DBUILD_TESTS=ON ..
ninja

# Build specific targets
ninja kernel_rom
ninja basic_rom

# Run tests
ctest --verbose
ctest -R kernel_unit_tests
ctest -R basic_integration

# Check ROM sizes
ls -lh build/kernel.rom build/basic.rom

# Disassemble ROMs (if needed)
ca65 -l build/kernel.lst src/kernel/kernel.asm

# View memory maps
cat build/kernel.map
cat build/basic.map
```

---

**Document Status:** COMPLETE
**Implementation Ready:** YES
**Estimated Total Time:** 17-24 hours
**Risk Assessment:** MEDIUM (careful testing required)
**Recommended Approach:** Follow milestones sequentially with testing checkpoints
