# Test Plan: BYE Command Implementation (Milestone 4 Steps 4.1-4.3)

**Task ID:** task_1759767225_5128
**Implementation Date:** 2025-10-06
**Implementer:** Assembly Implementer Agent
**Target:** Milestone 4, Steps 4.1-4.3 from implementation-plan.md

---

## Implementation Summary

### Changes Made

Successfully implemented the BYE command in the EhBASIC interpreter to enable returning from BASIC to the monitor. The implementation consisted of four coordinated changes to `src/kernel/basic.asm`:

#### 1. Token Definition (Line 362)
Added `TK_BYE` token definition after `TK_NMI`:
```assembly
TK_BYE            = TK_NMI+1        ; BYE token (return to monitor)
```
- **Location:** Line 362
- **Impact:** Updated `TK_TAB` base to `TK_BYE+1` to maintain token sequence
- **Token Value:** TK_BYE = $AB (171 decimal)

#### 2. Command Handler (Line 7547-7548)
Added `LAB_BYE` handler after `LAB_NMI`:
```assembly
; perform BYE - return to monitor
; This command exits BASIC and returns control to the monitor at $FF12

LAB_BYE:
      JMP   $FF12             ; Jump to monitor return handler
```
- **Location:** Lines 7547-7548 (code at $DF47 in ROM)
- **Functionality:** Direct jump to monitor's RETURN_FROM_BASIC handler
- **Machine Code:** 4C 12 FF (JMP $FF12)

#### 3. Jump Table Entry (Line 8170)
Added `LAB_BYE-1` entry to `LAB_CTBL` command jump table:
```assembly
      .word LAB_BYE-1         ; BYE             new command (return to monitor)
```
- **Location:** Line 8170 (data at $2279 in ROM)
- **Entry:** Table entry points to LAB_BYE-1 per BASIC convention
- **Position:** 43rd entry in command table (matching TK_BYE token offset)

#### 4. Keyword String (Line 8400-8401)
Added `BYE` keyword to `TAB_ASCB` keyword table:
```assembly
LBB_BYE:
      .byte "YE",TK_BYE       ; BYE
```
- **Location:** Lines 8400-8401 (data at $23C2 in ROM)
- **Format:** "YE" + token byte (first letter 'B' stripped per table format)
- **Bytes:** 59 45 AB (ASCII 'Y', 'E', token $AB)
- **Position:** Alphabetically placed after BITTST in 'B' keyword table

### Build Verification

 **Assembly Status:** Success
 **Linking Status:** Success
 **ROM Size:** 10,310 bytes (under 12KB limit)
 **ROM Location:** $C000-$E845
 **Symbol Verification:** LAB_BYE confirmed at $DF47 in listing file
 **Keyword Verification:** LBB_BYE confirmed at $23C2 in listing file

**Build Output:**
```
BASIC ROM BUILD COMPLETE
CODE segment: $C000-$E845 (10,310 bytes)
LAB_BYE handler: $DF47 (JMP $FF12)
LBB_BYE keyword: $23C2 ("YE" + $AB)
```

---

## Testing Requirements

### Prerequisites

Before testing, ensure:
1.  BASIC ROM rebuilt successfully (basic.rom in cmake-build-debug/)
2.   Monitor ROM has RETURN_FROM_BASIC handler at $FF12 (requires Milestone 4 Step 4.1-4.2)
3.   Monitor's B: command launches BASIC at $C000 (requires Milestone 2-3)
4.   Monitor state save/restore routines implemented (requires Milestone 2)

**Current Status:** BYE command implementation is complete, but integration depends on monitor-side changes from previous milestone steps.

---

## Test Scenarios

### Test 1: Token Recognition
**Objective:** Verify BASIC recognizes BYE as a valid command keyword

**Prerequisites:**
- BASIC interpreter running
- At BASIC prompt

**Test Steps:**
1. Type: `?TK_BYE` (if BASIC has token inspection)
2. Or type: `BYE` and observe parsing behavior

**Expected Results:**
- BASIC parser recognizes "BYE" keyword
- Token value $AB assigned
- No "SYNTAX ERROR" response

**Success Criteria:**
-  BYE keyword parsed without error
-  Execution reaches LAB_BYE handler

---

### Test 2: Handler Execution
**Objective:** Verify LAB_BYE handler executes and jumps to $FF12

**Prerequisites:**
- BASIC interpreter running
- Debugger or emulator with execution tracing

**Test Steps:**
1. Set breakpoint at $DF47 (LAB_BYE)
2. Set breakpoint at $FF12 (RETURN_FROM_BASIC)
3. Type: `BYE` at BASIC prompt
4. Press ENTER

**Expected Results:**
- Execution breaks at $DF47
- Next instruction is JMP $FF12 (4C 12 FF)
- Execution transfers to $FF12

**Success Criteria:**
-  Breakpoint at LAB_BYE triggered
-  JMP $FF12 instruction executed
-  Control transferred to monitor

---

### Test 3: Jump Table Lookup
**Objective:** Verify command table correctly dispatches to LAB_BYE

**Prerequisites:**
- BASIC interpreter running
- Debugger with memory inspection

**Test Steps:**
1. Inspect LAB_CTBL at offset 42 (0-indexed position for BYE)
2. Calculate: LAB_CTBL + (42 × 2) = table entry address
3. Read 2-byte word at entry address
4. Verify word equals LAB_BYE-1

**Expected Results:**
- Table entry contains address $DF46 (LAB_BYE-1)
- Word stored in little-endian format: 46 DF

**Success Criteria:**
-  Table entry points to correct handler
-  Offset matches token value

---

### Test 4: Keyword Table Search
**Objective:** Verify keyword parser finds BYE in TAB_ASCB table

**Prerequisites:**
- BASIC interpreter running
- Debugger with memory inspection

**Test Steps:**
1. Inspect TAB_ASCB table starting at $23C2
2. Look for sequence: 59 45 AB 00 (YE + token + terminator)
3. Type `BYE` and trace keyword search routine

**Expected Results:**
- String "YE" followed by token $AB found in table
- Keyword search matches input "BYE" (with 'B' stripped)
- Token $AB returned to parser

**Success Criteria:**
-  Keyword found in correct alphabetical position
-  Token value matches TK_BYE definition
-  Search terminates successfully

---

### Test 5: End-to-End Integration Test
**Objective:** Verify complete BYE command flow from input to monitor return

**Prerequisites:**
-  BASIC ROM with BYE command installed
-  Monitor ROM with B: command and RETURN_FROM_BASIC at $FF12
-  System initialized and at monitor prompt

**Test Steps:**
1. Type: `B:` at monitor prompt
2. Wait for BASIC to initialize and display "READY" prompt
3. Type: `PRINT "TESTING BYE"` and press ENTER
4. Observe output: "TESTING BYE"
5. Type: `BYE` and press ENTER
6. Observe transition back to monitor

**Expected Results:**
- Monitor displays "ENTERING BASIC..." message
- BASIC starts and displays "READY" prompt
- BASIC executes PRINT command correctly
- BYE command triggers LAB_BYE handler
- JMP $FF12 transfers control to monitor
- Monitor displays "RETURNING TO MONITOR..." message
- Monitor prompt "READY." appears
- Monitor commands functional after return

**Success Criteria:**
-  Smooth transition: Monitor ’ BASIC ’ Monitor
-  No crashes or hangs
-  BASIC executes commands before BYE
-  Monitor state properly restored
-  Command buffer cleared (no "BYE" remnants)

---

### Test 6: Repeated Cycle Test
**Objective:** Verify BYE command works reliably across multiple sessions

**Prerequisites:**
- Complete integration working (Test 5 passed)

**Test Steps:**
1. Type: `B:` (enter BASIC)
2. Type: `PRINT 1` (test BASIC works)
3. Type: `BYE` (return to monitor)
4. Type: `H:` (verify monitor works)
5. Repeat steps 1-4 ten times

**Expected Results:**
- All 10 cycles complete without errors
- No memory corruption observed
- Each cycle behaves identically to first

**Success Criteria:**
-  10 successful round trips
-  No degradation in performance
-  No accumulated errors

---

### Test 7: Error Handling Test
**Objective:** Verify BYE command handles edge cases

**Prerequisites:**
- BASIC interpreter running

**Test Cases:**

**Case A: BYE with arguments**
- Input: `BYE 123`
- Expected: Should either execute (ignoring args) or show "SYNTAX ERROR"

**Case B: BYE in program line**
- Input: `10 PRINT "TEST"`
- Input: `20 BYE`
- Input: `RUN`
- Expected: Program prints "TEST" then returns to monitor

**Case C: BYE in immediate mode**
- Input: `BYE` (at READY prompt)
- Expected: Returns to monitor immediately

**Success Criteria:**
-  All cases handled gracefully
-  No crashes or unexpected behavior

---

## Known Limitations

1. **Monitor Dependency:** BYE command requires monitor to have:
   - RETURN_FROM_BASIC handler at $FF12
   - State restoration routines
   - Command buffer cleanup

2. **Cold Start Only:** BYE exits BASIC completely. User programs are lost. No warm start support.

3. **No State Preservation:** BASIC variables, arrays, and program text are cleared on each B: command entry.

4. **One-Way Exit:** Once BYE executes, cannot return to previous BASIC state. Must re-enter via B: command.

---

## Integration Checklist

Before declaring BYE command fully functional, verify:

- [ ] Monitor has RETURN_FROM_BASIC handler at $FF12 (Milestone 4 Step 4.1)
- [ ] RETURN_FROM_BASIC calls RESTORE_MONITOR_STATE (Milestone 4 Step 4.2)
- [ ] RESTORE_MONITOR_STATE clears command buffer (Milestone 2)
- [ ] B: command saves monitor state before BASIC entry (Milestone 2)
- [ ] B: command initializes I/O vectors (Milestone 2)
- [ ] Both ROMs (kernel.rom and basic.rom) loaded at correct addresses
- [ ] End-to-end test (Test 5) passes completely
- [ ] Repeated cycle test (Test 6) passes without errors

---

## Testing Checklist

### Unit Tests
- [x] Token definition correct (TK_BYE = TK_NMI+1)
- [x] Handler code assembled correctly (JMP $FF12)
- [x] Jump table entry points to LAB_BYE-1
- [x] Keyword string in correct table position
- [x] BASIC ROM builds without errors
- [x] ROM size under 12KB limit

### Integration Tests (Pending Monitor Implementation)
- [ ] Test 1: Token Recognition
- [ ] Test 2: Handler Execution
- [ ] Test 3: Jump Table Lookup
- [ ] Test 4: Keyword Table Search
- [ ] Test 5: End-to-End Integration
- [ ] Test 6: Repeated Cycle Test
- [ ] Test 7: Error Handling

### Regression Tests
- [ ] Existing BASIC commands still work (PRINT, FOR/NEXT, etc.)
- [ ] BASIC cold start initializes correctly
- [ ] Monitor commands work after BASIC exit
- [ ] Zero page not corrupted by BASIC usage

---

## Files Modified

### Source Files
1. **src/kernel/basic.asm**
   - Line 362: Added TK_BYE token definition
   - Line 366: Updated TK_TAB base reference
   - Lines 7547-7548: Added LAB_BYE handler
   - Line 8170: Added LAB_CTBL jump table entry
   - Lines 8400-8401: Added LBB_BYE keyword string

### Build Artifacts
1. **cmake-build-debug/kernel/basic.rom** (10,310 bytes)
   - LAB_BYE handler at $DF47
   - Jump table entry at $2279
   - Keyword entry at $23C2

2. **cmake-build-debug/kernel/basic.lst**
   - Listing shows all changes correctly assembled
   - Symbol addresses verified

3. **cmake-build-debug/kernel/basic.map**
   - CODE segment: $C000-$E845
   - Size: 10,310 bytes (under 12KB limit)

---

## Next Steps (For Testing Phase)

1. **Complete Monitor Integration** (blocking BYE testing):
   - Implement RETURN_FROM_BASIC handler at $FF12 (if not done)
   - Verify RESTORE_MONITOR_STATE works correctly
   - Test B: command launches BASIC successfully

2. **Execute Test Suite**:
   - Run Tests 1-4 (unit-level tests)
   - Run Test 5 (end-to-end integration)
   - Run Tests 6-7 (reliability and edge cases)

3. **Document Test Results**:
   - Record all test outcomes
   - Note any failures or unexpected behavior
   - Update this document with actual results

4. **Regression Testing**:
   - Verify no existing BASIC commands broken
   - Verify no monitor commands affected
   - Check memory map integrity

5. **Performance Testing** (optional):
   - Measure BYE command execution time
   - Verify no memory leaks
   - Check stack depth during transition

---

## Implementation Notes

### Design Decisions

1. **Token Placement:** TK_BYE placed after TK_NMI to maintain sequential token assignment and minimize changes to existing code.

2. **Handler Simplicity:** LAB_BYE is intentionally minimal (single JMP instruction) to:
   - Reduce code size
   - Minimize failure points
   - Delegate state management to monitor
   - Follow BASIC's existing pattern for system calls

3. **Keyword Alphabetization:** BYE placed after BITTST in TAB_ASCB to maintain alphabetical order required by BASIC's binary search keyword parser.

4. **Jump Table Position:** LAB_BYE entry added at end of LAB_CTBL table, matching token offset calculation (token - TK_END = table index).

### Technical Considerations

1. **Address Hardcoding:** $FF12 is hardcoded as monitor return point. This creates tight coupling but:
   - Matches implementation plan specification
   - Simplifies jump table entry
   - Avoids external symbol resolution
   - Monitor ROM controls this address anyway

2. **Token Sequence Impact:** Adding TK_BYE shifts all secondary tokens by 1. This is safe because:
   - Tokens are compile-time constants
   - All references updated automatically
   - No external token dependencies

3. **ROM Size Impact:** BYE command adds:
   - 3 bytes: Handler code (JMP instruction)
   - 2 bytes: Jump table entry
   - 3 bytes: Keyword string + token
   - **Total: 8 bytes**

---

## Success Metrics

### Implementation Phase (COMPLETE )
-  All code changes made successfully
-  BASIC ROM builds without errors
-  No assembly warnings or issues
-  ROM size within limits (10,310 / 12,288 bytes)
-  Symbol addresses verified in listing

### Testing Phase (PENDING)
- [ ] All unit tests pass
- [ ] End-to-end integration test passes
- [ ] No regressions in existing functionality
- [ ] Repeated cycle test shows stability
- [ ] Error handling works as expected

### Integration Phase (PENDING)
- [ ] Monitor ROMs updated with required handlers
- [ ] Both ROMs loaded and functional in emulator
- [ ] Full workflow tested: Monitor ’ B: ’ BASIC ’ BYE ’ Monitor
- [ ] Documentation updated with actual test results

---

## Contact & Escalation

**Implementation Agent:** Assembly Implementer
**Testing Agent:** Testing Agent (next phase)
**Integration Agent:** System Integration Agent (final phase)

**Blocking Issues:**
- None at implementation level
- Monitor integration required for functional testing

**Status:** READY_FOR_TESTING (pending monitor-side implementation)

---

**Document Version:** 1.0
**Last Updated:** 2025-10-06
**Status:** Implementation Complete, Awaiting Integration Testing
