---
enhancement: add-basic-interpreter
milestone: 4
task_id: task_1759677697_4009
agent: assembly-implementer
created: 2025-10-05
status: READY_FOR_TESTING
---

# BASIC Integration Milestone 4 - Test Plan

## Implementation Summary

### What Was Implemented

**Milestone 4 Objective**: Complete the BASIC launch/return cycle integration

This implementation completes the integration between the 6502 kernel monitor and the EhBASIC interpreter, enabling users to seamlessly transition between monitor mode and BASIC mode.

### Code Changes Made

#### 1. Added RETURN_FROM_BASIC Jump Vector ($FF12)
**File**: `src/kernel/kernel.asm`
**Location**: JUMPS segment (line 3051)

```assembly
K_RETURN_BASIC:  JMP RETURN_FROM_BASIC  ; $FF12 - BASIC exit point
```

- Extended the kernel API jump table to include the BASIC return handler
- Located at fixed address $FF12 for BASIC to call when user types BYE
- JUMPS segment now 21 bytes (was 18 bytes): $FF00-$FF14

#### 2. Implemented RETURN_FROM_BASIC Handler
**File**: `src/kernel/kernel.asm`
**Location**: After BASIC_SIG_FAIL handler (lines 1859-1888)

```assembly
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
    LDA #$00
    STA MON_CMDPTR
    STA MON_CMDLEN

    ; Return to monitor loop
    JMP MONITOR_LOOP
```

**Functionality**:
- Clears screen for clean visual transition
- Displays "RETURNING TO MONITOR..." message
- Calls RESTORE_MONITOR_STATE to restore stack, cursor, and monitor mode
- Double-checks command buffer is cleared (prevents command echoing)
- Returns to MONITOR_LOOP to display prompt and accept commands

#### 3. Updated CMD_LAUNCH_BASIC
**File**: `src/kernel/kernel.asm`
**Location**: CMD_LAUNCH_BASIC routine (lines 1800-1838)

**Changes**:
- Fixed missing `SAVE_MONITOR_STATE` call (line 1831 was incomplete `JSR`)
- Changed from stub return to real BASIC jump: `JMP $C000` (line 1838)
- Updated comments to reflect Milestone 4 completion
- Removed test stub code that was returning immediately

**Complete Flow**:
1. Verify BASIC ROM signature at $C000 (JMP opcode check)
2. Validate jump target is in range $C000-$EFFF
3. Clear screen for transition
4. Display "ENTERING BASIC..." message
5. Save monitor state (stack pointer, cursor, mode)
6. Initialize BASIC I/O vectors (VEC_IN, VEC_OUT)
7. Jump to BASIC cold start at $C000

**Critical Fix**: The `JSR SAVE_MONITOR_STATE` line was incomplete, causing build failures. This is now properly implemented.

---

## How It Works

### Complete Launch/Return Cycle

```
┌─────────────────────────────────────────────────────────────┐
│                    MONITOR → BASIC → MONITOR                │
└─────────────────────────────────────────────────────────────┘

1. User types "B:" at monitor prompt
   ↓
2. CMD_LAUNCH_BASIC executes
   ├─ Verifies BASIC ROM present at $C000
   ├─ Saves monitor state (stack, cursor, mode)
   ├─ Initializes I/O vectors
   │  • VEC_OUT ($0207) → PRINT_CHAR ($FF00)
   │  • VEC_IN  ($0205) → GET_KEYSTROKE ($FF09)
   └─ JMP $C000 (BASIC cold start)
   ↓
3. BASIC takes control
   ├─ Initializes BASIC variables ($0200-$0268)
   ├─ Displays "READY" prompt
   └─ Accepts BASIC commands
   ↓
4. User types "BYE" at BASIC prompt
   ↓
5. BASIC CMD_BYE executes
   └─ JMP $FF12 (RETURN_FROM_BASIC)
   ↓
6. RETURN_FROM_BASIC executes
   ├─ Clears screen
   ├─ Displays "RETURNING TO MONITOR..."
   ├─ Restores monitor state
   ├─ Clears command buffer
   └─ JMP MONITOR_LOOP
   ↓
7. Monitor ready for next command
```

### Memory Integration

**Zero Page Allocation**:
- Monitor: $14-$34 (relocated in Milestone 1)
- BASIC: $00-$13, $5B-$BB, $BC-$E1, $EF-$FF
- No conflicts!

**Extended RAM Allocation**:
- $0200-$0268: BASIC variables (105 bytes)
- $0269-$02DE: Monitor variables (118 bytes)
- **Overlap**: Monitor command buffer ($0200-$024F) overlaps BASIC input buffer
  - This is safe because only one is active at a time
  - RESTORE_MONITOR_STATE clears the command buffer

**I/O Vectors** ($0205-$020C):
- VEC_IN  ($0205): Keyboard input vector → GET_KEYSTROKE ($FF09)
- VEC_OUT ($0207): Character output vector → PRINT_CHAR ($FF00)
- VEC_LD  ($0209): Load stub (future enhancement)
- VEC_SV  ($020B): Save stub (future enhancement)

**ROM Allocation**:
- $C000-$EFFF: BASIC ROM (12KB) - built separately
- $F000-$FFFF: Monitor ROM (4KB) - includes all integration code
- Current utilization: 3773 bytes CODE + 21 bytes JUMPS + 6 bytes VECS = 3800/4096 (92.8%)

---

## Test Plan

### Prerequisites

1. **Build Environment**:
   - Monitor ROM built successfully: `cmake-build-debug/kernel.rom`
   - BASIC ROM required: `cmake-build-debug/basic.rom` (from Milestone 3)
   - 6502 emulator with both ROMs loaded

2. **Verification**:
   ```bash
   # Check kernel.rom size
   ls -l cmake-build-debug/kernel.rom
   # Should be 4096 bytes

   # Check JUMPS segment size in kernel.map
   grep "JUMPS" cmake-build-debug/kernel.map
   # Should show: JUMPS  00FF00  00FF14  000015  00001
   ```

### Test Categories

#### 1. ROM Verification Tests
**Objective**: Verify ROM built correctly and contains expected code

**Test 1.1: Kernel ROM Size**
```bash
ls -l cmake-build-debug/kernel.rom
```
**Expected**: 4096 bytes (0x1000)

**Test 1.2: JUMPS Segment Verification**
```bash
hexdump -C cmake-build-debug/kernel.rom | grep "ff00:"
```
**Expected**: See 7 JMP instructions ($4C) at $FF00, $FF03, $FF06, $FF09, $FF0C, $FF0F, $FF12

**Test 1.3: Memory Map Check**
```bash
cat cmake-build-debug/kernel.map | grep -A 5 "Segment list"
```
**Expected**:
```
CODE                  00F000  00FEBC  000EBD  00001
JUMPS                 00FF00  00FF14  000015  00001
VECS                  00FFFA  00FFFF  000006  00001
```

#### 2. Integration Tests (Requires Emulator)

**Test 2.1: BASIC ROM Detection - Success Case**

**Setup**: Load both kernel.rom and basic.rom into emulator

**Steps**:
1. Start emulator
2. Verify monitor "READY." prompt appears
3. Type: `B:`
4. Press ENTER

**Expected Output**:
```
READY.
> B:
ENTERING BASIC...

READY
_
```

**Pass Criteria**:
- Screen clears
- "ENTERING BASIC..." message displays
- BASIC "READY" prompt appears with cursor
- No crash or hang

**Test 2.2: BASIC ROM Detection - Failure Case**

**Setup**: Load only kernel.rom (no basic.rom)

**Steps**:
1. Start emulator
2. Type: `B:`
3. Press ENTER

**Expected Output**:
```
READY.
> B:
ERROR: BASIC ROM NOT FOUND
READY.
>
```

**Pass Criteria**:
- Error message displays
- Returns to monitor prompt
- Monitor still functional (test with `H:`)

**Test 2.3: Return to Monitor**

**Setup**: BASIC launched successfully

**Steps**:
1. Launch BASIC (see Test 2.1)
2. At BASIC prompt, type: `BYE`
3. Press ENTER

**Expected Output**:
```
READY
BYE
RETURNING TO MONITOR...

READY.
>
```

**Pass Criteria**:
- Screen clears
- "RETURNING TO MONITOR..." message displays
- Monitor "READY." prompt appears
- Cursor positioned correctly

**Test 2.4: Monitor Functionality After BASIC**

**Setup**: After returning from BASIC (Test 2.3)

**Steps**:
1. Type: `H:`
2. Type: `R:F000-F00F`
3. Type: `Z:`
4. Type: `C:`

**Expected**: All commands work normally

**Pass Criteria**:
- Help displays correctly
- Memory read shows ROM contents
- Zero page displays (check $14-$34 for monitor variables)
- Screen clears properly

#### 3. BASIC Functionality Tests

**Test 3.1: Simple PRINT**
```basic
PRINT "HELLO WORLD"
```
**Expected**: `HELLO WORLD` displays, "READY" prompt returns

**Test 3.2: Variable Assignment**
```basic
A=42
PRINT A
```
**Expected**: `42` displays

**Test 3.3: FOR Loop**
```basic
FOR I=1 TO 5
PRINT I
NEXT I
```
**Expected**: Numbers 1-5 display, one per line

**Test 3.4: BASIC Program**
```basic
10 PRINT "COUNTING"
20 FOR I=1 TO 3
30 PRINT I
40 NEXT I
LIST
RUN
```
**Expected**:
- LIST shows program lines
- RUN executes program correctly

**Test 3.5: String Operations**
```basic
A$="HELLO"
B$="WORLD"
PRINT A$;" ";B$
```
**Expected**: `HELLO WORLD` displays

#### 4. State Management Tests

**Test 4.1: Stack Pointer Preservation**

**Setup**: After BASIC launch/return cycle

**Steps**:
1. Before B:, note stack pointer (examine $01FF area with T:)
2. Execute B: command
3. Run BASIC program
4. Execute BYE
5. Check stack with T:

**Expected**: Stack pointer restored to pre-BASIC value

**Test 4.2: Cursor Position Preservation**

**Setup**: Position cursor at specific location

**Steps**:
1. Type several commands to move cursor down screen
2. Note cursor Y position
3. Execute B:
4. Execute BYE
5. Note cursor position

**Expected**: Cursor returns to saved position (or reasonable default)

**Test 4.3: Command Buffer Cleanup**

**Setup**: After returning from BASIC

**Steps**:
1. Execute B: and BYE cycle
2. At monitor prompt, press UP arrow (command recall)

**Expected**: No stray BASIC commands in buffer

**Critical**: This tests the double-clear of MON_CMDPTR and MON_CMDLEN

**Test 4.4: Zero Page Integrity**

**Setup**: After BASIC launch/return cycle

**Steps**:
1. Before B:, examine monitor zero page: `Z:`
2. Note values at $14-$34
3. Execute B:
4. Run BASIC program that uses variables
5. Execute BYE
6. Examine zero page: `Z:`

**Expected**: Monitor variables at $14-$34 unchanged

#### 5. Repeated Cycle Tests

**Test 5.1: Rapid Entry/Exit**

**Steps**:
1. B: → BYE
2. B: → BYE
3. B: → BYE
4. B: → BYE
5. B: → BYE

**Expected**: No crashes, no memory leaks, smooth transitions

**Test 5.2: Interleaved Commands**

**Steps**:
1. Type: `R:F000-F00F` (monitor command)
2. Type: `B:` (enter BASIC)
3. Type: `PRINT 123` (BASIC command)
4. Type: `BYE` (exit BASIC)
5. Type: `W:8000 AA BB CC` (monitor command)
6. Type: `R:8000-8002` (verify write)
7. Type: `B:` (enter BASIC again)
8. Type: `A=5: PRINT A` (BASIC command)
9. Type: `BYE` (exit BASIC)
10. Type: `H:` (monitor command)

**Expected**: All commands execute correctly in their respective modes

#### 6. Stress Tests

**Test 6.1: Long BASIC Program**

```basic
10 FOR I=1 TO 100
20 PRINT "LINE ";I
30 NEXT I
```

**Expected**: Executes without crash, screen scrolls correctly

**Test 6.2: Screen Scroll Test**

```basic
FOR I=1 TO 50
PRINT I
NEXT I
```

**Expected**: Screen scrolls smoothly, no visual artifacts

**Test 6.3: Memory Usage**

```basic
DIM A(100)
FOR I=0 TO 100
A(I)=I
NEXT I
PRINT A(50)
```

**Expected**: BASIC manages memory correctly (may fail with "OUT OF MEMORY" on small systems - acceptable)

#### 7. Error Condition Tests

**Test 7.1: Invalid BASIC ROM**

**Setup**: Load corrupt ROM at $C000

**Expected**: "ERROR: BASIC ROM SIGNATURE INVALID" message, return to monitor

**Test 7.2: BASIC Syntax Error**

```basic
PRINT "UNTERMINATED STRING
```

**Expected**: BASIC displays syntax error, does not crash

**Test 7.3: Break to Monitor (ESC in BASIC)**

**Note**: This functionality may not be implemented yet (future enhancement)

**Steps**:
1. In BASIC, run infinite loop: `10 GOTO 10`
2. Press ESC

**Expected**: Either breaks to monitor or no effect (depends on implementation)

#### 8. Boundary Condition Tests

**Test 8.1: Zero Page Boundary ($13-$14)**

**Steps**:
1. Write value to $13: `W:13 FF`
2. Execute B: and BYE cycle
3. Read $13 and $14: `R:13-14`

**Expected**: $13 unchanged, $14 contains monitor variable

**Test 8.2: Command Buffer Boundary ($0268-$0269)**

**Steps**:
1. Write to $0268: `W:0268 AA`
2. Write to $0269: `W:0269 BB`
3. Execute B:
4. Run BASIC program
5. Execute BYE
6. Read $0268-$026A: `R:0268-026A`

**Expected**: $0269 cleared to $00 (MON_CMDPTR), $026A cleared to $00 (MON_CMDLEN)

---

## Known Issues and Limitations

### Current Limitations

1. **Cold Start Only**: BASIC always performs cold start, losing any program in memory
2. **No LOAD/SAVE**: File operations not yet implemented (I/O stubs in place)
3. **No Warm Start**: Cannot preserve BASIC programs between sessions
4. **No Breakpoints**: Cannot break from BASIC to monitor during program execution

### Expected Behaviors (Not Bugs)

1. **Command Buffer Overlap**: Monitor and BASIC share command buffer space - this is intentional and safe
2. **Screen Clear on Transition**: Both B: and BYE clear screen - intentional for clean UX
3. **ROM Utilization Warning**: Monitor ROM at 92.8% capacity - acceptable, leaves ~300 bytes for future enhancements

---

## Test Results Tracking

### Test Execution Checklist

- [ ] 1.1: Kernel ROM Size
- [ ] 1.2: JUMPS Segment Verification
- [ ] 1.3: Memory Map Check
- [ ] 2.1: BASIC ROM Detection - Success
- [ ] 2.2: BASIC ROM Detection - Failure
- [ ] 2.3: Return to Monitor
- [ ] 2.4: Monitor After BASIC
- [ ] 3.1: Simple PRINT
- [ ] 3.2: Variable Assignment
- [ ] 3.3: FOR Loop
- [ ] 3.4: BASIC Program
- [ ] 3.5: String Operations
- [ ] 4.1: Stack Pointer Preservation
- [ ] 4.2: Cursor Position Preservation
- [ ] 4.3: Command Buffer Cleanup
- [ ] 4.4: Zero Page Integrity
- [ ] 5.1: Rapid Entry/Exit
- [ ] 5.2: Interleaved Commands
- [ ] 6.1: Long BASIC Program
- [ ] 6.2: Screen Scroll Test
- [ ] 6.3: Memory Usage
- [ ] 7.1: Invalid BASIC ROM
- [ ] 7.2: BASIC Syntax Error
- [ ] 7.3: Break to Monitor (ESC)
- [ ] 8.1: Zero Page Boundary
- [ ] 8.2: Command Buffer Boundary

### Test Results Summary

**Date**: _____________
**Tester**: _____________
**Environment**: _____________

**Tests Passed**: _____ / 24
**Tests Failed**: _____
**Tests Skipped**: _____

**Critical Issues Found**: _____

**Notes**:
```
[Space for tester notes]
```

---

## Testing Agent Instructions

### Prerequisites Check

1. Verify kernel.rom built successfully (4096 bytes)
2. Verify BASIC ROM available (from Milestone 3)
3. Set up emulator with both ROMs loaded
4. Ensure emulator has keyboard input and screen output

### Recommended Test Order

**Phase 1: Static Verification** (Tests 1.1-1.3)
- Can be done without running emulator
- Verifies build correctness

**Phase 2: Integration Tests** (Tests 2.1-2.4)
- Core functionality verification
- If these fail, stop and report to implementer

**Phase 3: BASIC Functionality** (Tests 3.1-3.5)
- Verify BASIC works through monitor I/O
- Validates I/O vector integration

**Phase 4: State Management** (Tests 4.1-4.4)
- Critical for data integrity
- Tests save/restore mechanisms

**Phase 5: Robustness** (Tests 5.1-6.3)
- Repeated cycles and stress testing
- Validates stability

**Phase 6: Edge Cases** (Tests 7.1-8.2)
- Error conditions and boundaries
- Optional but recommended

### Failure Handling

**If Test 2.1 Fails**:
- Check BASIC ROM loaded at correct address ($C000)
- Verify ROM signature (first byte should be $4C)
- Check emulator memory map

**If Test 2.3 Fails**:
- Verify BYE command added to BASIC (Milestone 3)
- Check $FF12 entry point exists in kernel.rom
- Examine crash address if system hangs

**If Test 4.3 Fails** (Command Buffer):
- This is a CRITICAL bug - report immediately
- Can cause command echoing and erratic behavior
- Check RESTORE_MONITOR_STATE implementation

**If Test 4.4 Fails** (Zero Page Integrity):
- This indicates memory corruption - report immediately
- Check zero page allocation boundaries
- Verify BASIC not overwriting monitor variables

### Test Environment Setup

**Emulator Command Example**:
```bash
./build/6502_Kernel \
  --kernel cmake-build-debug/kernel.rom \
  --basic cmake-build-debug/basic.rom \
  --memory 64K
```

**Alternative: Manual Load**:
```bash
./build/6502_Kernel
# Then use monitor commands:
# L:F000,kernel.rom    (if supported)
# L:C000,basic.rom     (if supported)
```

---

## Files Modified

### Source Files
- `src/kernel/kernel.asm` (lines 1800-1888, 3051)
  - Added K_RETURN_BASIC jump vector
  - Implemented RETURN_FROM_BASIC handler
  - Fixed CMD_LAUNCH_BASIC (added SAVE_MONITOR_STATE, changed to JMP $C000)

### Build Artifacts
- `cmake-build-debug/kernel.rom` (4096 bytes)
- `cmake-build-debug/kernel.map` (updated segment sizes)
- `cmake-build-debug/kernel/kernel.o` (updated object file)

### Memory Map Changes
- CODE segment: 3773 bytes (was ~3600 bytes)
- JUMPS segment: 21 bytes (was 18 bytes)
- Total ROM usage: 3800 / 4096 bytes (92.8%)

---

## Integration Points

### External Dependencies

1. **BASIC ROM** (from Milestone 3):
   - Must be loaded at $C000-$EFFF
   - Must start with JMP instruction ($4C)
   - Must include BYE command that jumps to $FF12

2. **Monitor ROM** (this implementation):
   - Provides PRINT_CHAR at $FF00
   - Provides GET_KEYSTROKE at $FF09
   - Provides RETURN_FROM_BASIC at $FF12

### Data Flow

**Monitor → BASIC**:
1. User command: "B:"
2. CMD_LAUNCH_BASIC validates and saves state
3. I/O vectors set to monitor routines
4. JMP $C000 transfers control

**BASIC → Monitor**:
1. User command: "BYE"
2. BASIC CMD_BYE executes JMP $FF12
3. RETURN_FROM_BASIC restores state
4. JMP MONITOR_LOOP returns control

**BASIC I/O**:
- BASIC PRINT → VEC_OUT ($0207) → PRINT_CHAR ($FF00) → Screen
- BASIC INPUT → VEC_IN ($0205) → GET_KEYSTROKE ($FF09) → Keyboard

---

## Success Criteria

### Minimum Acceptance Criteria

- [ ] Kernel ROM builds without errors
- [ ] B: command launches BASIC successfully
- [ ] BASIC displays "READY" prompt
- [ ] BASIC commands execute correctly
- [ ] BYE command returns to monitor
- [ ] Monitor commands work after BASIC
- [ ] No crashes during normal operation

### Full Acceptance Criteria

- [ ] All 24 test cases pass
- [ ] No memory corruption observed
- [ ] No command buffer issues
- [ ] Smooth transitions between modes
- [ ] Zero page integrity maintained
- [ ] Repeated cycles work correctly

### Performance Criteria

- BASIC launch time: < 1 second
- BASIC return time: < 1 second
- No observable lag in I/O operations

---

## Next Steps After Testing

### If Tests Pass

1. Mark Milestone 4 as COMPLETED
2. Proceed to Milestone 5 (Comprehensive Testing)
3. Update documentation with test results
4. Commit changes with test report

### If Tests Fail

1. Document all failures in detail:
   - Test case number
   - Expected vs actual behavior
   - Steps to reproduce
   - Any error messages or crash addresses

2. Categorize issues:
   - Critical (system crash, data corruption)
   - Major (feature doesn't work)
   - Minor (cosmetic or edge case)

3. Return to implementer with:
   - This test plan
   - Test results summary
   - Detailed failure descriptions
   - Suggested fixes (if obvious)

---

## Appendix: Quick Reference

### Memory Addresses

| Address | Symbol | Purpose |
|---------|--------|---------|
| $C000 | LAB_COLD | BASIC cold start entry point |
| $FF00 | K_PRINT_CHAR | Monitor character output |
| $FF09 | K_GET_KEYSTROKE | Monitor keyboard input |
| $FF12 | K_RETURN_BASIC | BASIC exit point |
| $0205 | VEC_IN | BASIC input vector |
| $0207 | VEC_OUT | BASIC output vector |

### Monitor Variables (Zero Page)

| Address | Symbol | Size |
|---------|--------|------|
| $14-$15 | MON_CURRADDR | 2 bytes |
| $16-$17 | MON_MSG_PTR | 2 bytes |
| $25-$34 | HEX_LOOKUP_TABLE | 16 bytes |

### Monitor Variables (Extended RAM)

| Address | Symbol | Size |
|---------|--------|------|
| $0200-$024F | MON_CMDBUF | 80 bytes |
| $0269 | MON_CMDPTR | 1 byte |
| $026A | MON_CMDLEN | 1 byte |

### Build Commands

```bash
# Full build
cd cmake-build-debug
cmake -G Ninja -DBUILD_TESTS=ON ..
ninja kernel_rom

# Check ROM size
ls -lh kernel.rom

# View memory map
cat kernel.map | grep -A 5 "Segment list"
```

---

**Document Version**: 1.0
**Created**: 2025-10-05
**Implementation Status**: READY_FOR_TESTING
**Test Status**: PENDING
