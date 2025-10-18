# Test Plan: Hex-to-Decimal Conversion Command (H:xxxx)

**Document Type:** Test Plan & Handoff Document
**Version:** 1.0
**Date:** 2025-10-17
**Status:** READY_FOR_TESTING
**Enhancement ID:** hex-to-decimal-conversion
**Implementation Agent:** Assembly Implementer
**Target Testing Agent:** Testing Agent

---

## Executive Summary

This document describes the implementation of the H:xxxx (hex-to-decimal) conversion command and provides comprehensive testing procedures for validation. The implementation is complete and ready for thorough testing.

### Implementation Status: ✅ COMPLETE

All phases of implementation have been completed:
- ✅ Command parser integration
- ✅ Hex parsing using existing HEX_QUAD_TO_ADDR routine
- ✅ Binary-to-decimal conversion algorithm
- ✅ Decimal display with leading zero suppression
- ✅ Error handling for invalid input
- ✅ Help system integration

---

## What Was Implemented

### Core Functionality

The H:xxxx command provides hex-to-decimal conversion for 16-bit values:
- **Input:** 4-digit hexadecimal value (0000-FFFF)
- **Output:** Decimal representation (0-65535)
- **Format:** `H:xxxx` where xxxx is the hex value

### Implementation Details

#### 1. Command Parser (PARSE_CMD_HEX_TO_DEC)
- **Location:** kernel.asm:1202-1227
- **Function:** Validates H:xxxx syntax and delegates to conversion routine
- **Validation:**
  - Checks for colon at position 1
  - Validates command length (exactly 6 characters)
  - Uses HEX_QUAD_TO_ADDR for hex parsing
  - Handles errors with VALUE? message

#### 2. Conversion Engine (CMD_HEX_TO_DECIMAL)
- **Location:** kernel.asm:2231-2282
- **Function:** Converts 16-bit binary to decimal ASCII string
- **Algorithm:** Repeated division by 10 with digit collection
- **Special Cases:**
  - Zero input → outputs "0"
  - Leading zeros suppressed automatically
- **Buffer:** Uses DEC_DIGIT_BUFFER ($027D-$0281, 5 bytes)

#### 3. Division Routine (DIVIDE_BY_10)
- **Location:** kernel.asm:2294-2331
- **Function:** Divides 16-bit value by 10
- **Method:** Iterative subtraction
- **Output:** Quotient in DEC_RESULT_LO/HI, remainder in A
- **Performance:** ~10-20 cycles per subtraction

#### 4. Integration Points
- **Jump Tables:** Added index 15 entries (kernel.asm:3328, 3346)
- **Index Map:** H → 15 (kernel.asm:3357)
- **Help System:**
  - Message: MSG_HELP_HEX_TO_DEC (kernel.asm:3413)
  - Table entry: HELP_MSG_TABLE (kernel.asm:3392)
  - Count updated: 14 → 15 messages (kernel.asm:3404)
  - Loop count: CPX #30 (kernel.asm:2377)

---

## Code Changes Summary

### Files Modified
- **src/kernel/kernel.asm** - All changes in single file

### Lines Added/Modified
- **Parser routine:** ~26 lines (1202-1227)
- **Conversion routines:** ~107 lines (2220-2331)
- **Jump table entries:** 2 lines (3328, 3346)
- **Index map:** 1 line (3357)
- **Help message:** 1 line (3413)
- **Help table:** 1 line (3392)
- **Help count:** 2 lines (3404, 2377)

### Total Implementation Size
- **ROM Usage:** ~140 bytes (well within 150-byte budget)
- **RAM Usage:** 0 new bytes (reuses existing variables)
- **Build Status:** ✅ SUCCESS (8192 bytes total ROM)

---

## Test Plan

### Test Environment Setup

#### Prerequisites
1. Build completed successfully (verified)
2. Emulator or hardware platform ready
3. Monitor prompt accessible
4. Keyboard input functional

#### Test Data Preparation
Create test input list with expected outputs (see test cases below)

---

## Test Cases

### Category 1: Boundary Values

#### TC-1.1: Zero Value
- **Input:** `H:0000`
- **Expected Output:** `0` + newline
- **Priority:** CRITICAL
- **Rationale:** Special case handling in code

#### TC-1.2: Maximum Value
- **Input:** `H:FFFF`
- **Expected Output:** `65535` + newline
- **Priority:** CRITICAL
- **Rationale:** 16-bit boundary, tests full range

#### TC-1.3: Signed/Unsigned Boundary
- **Input:** `H:8000`
- **Expected Output:** `32768` + newline
- **Priority:** HIGH
- **Rationale:** Midpoint, signed interpretation boundary

#### TC-1.4: Maximum Signed Positive
- **Input:** `H:7FFF`
- **Expected Output:** `32767` + newline
- **Priority:** HIGH
- **Rationale:** Tests near-boundary value

#### TC-1.5: Minimum Non-Zero
- **Input:** `H:0001`
- **Expected Output:** `1` + newline
- **Priority:** MEDIUM
- **Rationale:** Tests single digit output

---

### Category 2: Common Values

#### TC-2.1: Page Boundary (256)
- **Input:** `H:0100`
- **Expected Output:** `256` + newline
- **Priority:** HIGH
- **Rationale:** Common page size reference

#### TC-2.2: Kilobyte (1024)
- **Input:** `H:0400`
- **Expected Output:** `1024` + newline
- **Priority:** MEDIUM
- **Rationale:** Common memory size

#### TC-2.3: 4K Boundary
- **Input:** `H:1000`
- **Expected Output:** `4096` + newline
- **Priority:** MEDIUM
- **Rationale:** Common block size

#### TC-2.4: Example from Spec (33023)
- **Input:** `H:80FF`
- **Expected Output:** `33023` + newline
- **Priority:** CRITICAL
- **Rationale:** Reference value from requirements

---

### Category 3: Powers of Two

#### TC-3.1: Powers of Two Series
| Input | Expected Output | Notes |
|-------|-----------------|-------|
| H:0001 | 1 | 2^0 |
| H:0002 | 2 | 2^1 |
| H:0004 | 4 | 2^2 |
| H:0008 | 8 | 2^3 |
| H:0010 | 16 | 2^4 |
| H:0020 | 32 | 2^5 |
| H:0040 | 64 | 2^6 |
| H:0080 | 128 | 2^7 |
| H:0100 | 256 | 2^8 |
| H:0200 | 512 | 2^9 |
| H:0400 | 1024 | 2^10 |
| H:0800 | 2048 | 2^11 |
| H:1000 | 4096 | 2^12 |
| H:2000 | 8192 | 2^13 |
| H:4000 | 16384 | 2^14 |
| H:8000 | 32768 | 2^15 |

**Priority:** MEDIUM
**Rationale:** Tests algorithm accuracy across full range

---

### Category 4: Error Handling

#### TC-4.1: Invalid Hex Characters
- **Input:** `H:GGGG`
- **Expected Output:** `VALUE?` + newline
- **Priority:** CRITICAL
- **Rationale:** Tests input validation

#### TC-4.2: Mixed Invalid Characters
- **Input:** `H:12XY`
- **Expected Output:** `VALUE?` + newline
- **Priority:** HIGH
- **Rationale:** Tests partial validation

#### TC-4.3: No Address Provided
- **Input:** `H:`
- **Expected Output:** `VALUE?` + newline
- **Priority:** HIGH
- **Rationale:** Tests length validation

#### TC-4.4: Short Input
- **Input:** `H:1`
- **Expected Output:** `VALUE?` + newline
- **Priority:** HIGH
- **Rationale:** Tests minimum length requirement

#### TC-4.5: Long Input
- **Input:** `H:12345`
- **Expected Output:** `VALUE?` + newline
- **Priority:** HIGH
- **Rationale:** Tests maximum length enforcement

#### TC-4.6: Missing Colon
- **Input:** `H 1234`
- **Expected Output:** `VALUE?` + newline
- **Priority:** MEDIUM
- **Rationale:** Tests syntax validation

---

### Category 5: Case Sensitivity

#### TC-5.1: Uppercase Hex
- **Input:** `H:ABCD`
- **Expected Output:** `43981` + newline
- **Priority:** MEDIUM
- **Rationale:** Tests standard hex input

#### TC-5.2: Lowercase Hex
- **Input:** `H:abcd`
- **Expected Output:** `43981` + newline
- **Priority:** MEDIUM
- **Rationale:** Tests case insensitivity

#### TC-5.3: Mixed Case
- **Input:** `H:aBcD`
- **Expected Output:** `43981` + newline
- **Priority:** MEDIUM
- **Rationale:** Tests parser flexibility

---

### Category 6: Leading Zero Suppression

#### TC-6.1: Single Digit
- **Input:** `H:0001`
- **Expected Output:** `1` (NOT `00001`)
- **Priority:** HIGH
- **Rationale:** Confirms leading zero suppression

#### TC-6.2: Two Digits
- **Input:** `H:0010`
- **Expected Output:** `16` (NOT `00016`)
- **Priority:** MEDIUM

#### TC-6.3: Three Digits
- **Input:** `H:0100`
- **Expected Output:** `256` (NOT `00256`)
- **Priority:** MEDIUM

---

### Category 7: Integration Tests

#### TC-7.1: No Side Effects on Other Commands
**Procedure:**
1. Execute: `R:8000`
2. Note displayed value
3. Execute: `H:1234`
4. Verify output: `4660`
5. Execute: `R:8000` again
6. Verify same value as step 2

**Expected:** No interference between commands
**Priority:** HIGH

#### TC-7.2: Error Recovery
**Procedure:**
1. Execute: `H:ZZZZ` (error)
2. Verify: `VALUE?` displayed
3. Execute: `W:1000` (valid command)
4. Verify: Write mode enters normally

**Expected:** System recovers cleanly from errors
**Priority:** HIGH

#### TC-7.3: Help Display Updated
**Procedure:**
1. Execute help command (H: with colon or old help mechanism)
2. Search output for "H:XXXX HEX TO DECIMAL"
3. Verify it appears in alphabetical position (after G:, before L:)

**Expected:** Help includes new command
**Priority:** MEDIUM

#### TC-7.4: Sequential Conversions
**Procedure:**
1. Execute: `H:0001`
2. Execute: `H:0002`
3. Execute: `H:0004`
4. Execute: `H:0008`

**Expected:** All conversions work sequentially without errors
**Priority:** MEDIUM

---

### Category 8: Performance Tests

#### TC-8.1: Conversion Speed - Typical Value
- **Input:** `H:1000` (4096)
- **Measurement:** Time from ENTER to output display
- **Target:** < 50ms @ 1MHz
- **Priority:** LOW
- **Note:** Visual observation acceptable

#### TC-8.2: Conversion Speed - Maximum Value
- **Input:** `H:FFFF` (65535)
- **Measurement:** Time from ENTER to output display
- **Target:** < 100ms @ 1MHz (may be slower due to iteration count)
- **Priority:** LOW
- **Note:** May exceed target, acceptable if < 500ms

---

### Category 9: Stress Tests

#### TC-9.1: Rapid Sequential Input
**Procedure:**
1. Execute H: command 20 times in rapid succession
2. Vary input values randomly
3. Check for crashes, hangs, or memory corruption

**Expected:** System remains stable
**Priority:** MEDIUM

#### TC-9.2: Alternating Valid/Invalid Input
**Procedure:**
1. Execute valid H: command
2. Execute invalid H: command
3. Repeat 10 times

**Expected:** No state corruption, consistent error handling
**Priority:** MEDIUM

---

## Regression Testing

### Existing Commands to Verify

Ensure the following commands still work correctly after H: implementation:

| Command | Test Input | Expected Behavior |
|---------|-----------|-------------------|
| R: | R:8000 | Display memory at $8000 |
| W: | W:8000 | Enter write mode at $8000 |
| D: | D:12345 | Display hex $3039 |
| G: | G:8000 | (Don't execute unless safe) |
| F: | F:8000-80FF,00 | Fill range with zeros |
| M: | M:8000-80FF,9000,0 | Copy memory block |
| X: | X:8000-8FFF,4C | Search for $4C (JMP) |
| C: | C: | Clear screen |
| Z: | Z: | Display zero page |
| T: | T: | Display stack |

**Priority:** HIGH - All existing functionality must remain intact

---

## Test Execution Procedures

### Manual Testing Steps

1. **Build Verification**
   - Confirm build completed without errors ✅
   - Check ROM size is within limits ✅

2. **Emulator Launch**
   - Start emulator
   - Verify monitor prompt appears
   - Test keyboard input works

3. **Execute Test Cases**
   - Run each test case in order
   - Document results: PASS/FAIL
   - For failures, note exact behavior observed
   - Capture screenshots or output for failures

4. **Regression Testing**
   - Execute regression test suite
   - Verify no commands broken by changes

5. **Performance Observation**
   - Note any perceived delays in conversion
   - Flag if conversion takes > 1 second for any value

### Automated Testing (if available)

If automated testing infrastructure exists:
1. Create test scripts for all test cases
2. Run full suite with automated input/output verification
3. Generate test report with pass/fail statistics

---

## Known Issues and Limitations

### Identified During Implementation

1. **Performance with Large Values**
   - Division by repeated subtraction is slow for values > 30000
   - Worst case (65535) may take ~350ms @ 1MHz
   - **Mitigation:** Acceptable for MVP, can optimize later if needed
   - **Status:** DOCUMENTED, not a blocker

2. **Help Command Mapping**
   - 'H' was previously mapped to help (index 4)
   - Now 'H' maps to hex-to-decimal (index 15)
   - Old help handler remains in jump table for backward compatibility
   - **Impact:** If code relied on H: for help, it will now invoke hex-to-decimal
   - **Status:** Intentional design decision per requirements

3. **Buffer Space Sharing**
   - DEC_DIGIT_BUFFER ($027D) shares space with MON_SEARCH_PATTERN
   - **Safety:** Safe because H: and X: commands never run simultaneously
   - **Status:** Design optimization, properly documented

---

## Acceptance Criteria Verification

### Must-Have Criteria (from Requirements)

| ID | Criterion | Status | Verification Method |
|----|-----------|--------|-------------------|
| AC-1 | Correct Parsing | ✅ IMPLEMENTED | Test TC-4.x series |
| AC-2 | Accurate Conversion | ✅ IMPLEMENTED | Test TC-1.x, TC-2.x, TC-3.x |
| AC-3 | Proper Display | ✅ IMPLEMENTED | Test TC-6.x (leading zeros) |
| AC-4 | Error Handling | ✅ IMPLEMENTED | Test TC-4.x series |
| AC-5 | Memory Budget | ✅ VERIFIED | Build output: ~140 bytes used of 150 budget |
| AC-6 | No Regressions | ⏳ PENDING | Run regression suite |
| AC-7 | Help Integration | ✅ IMPLEMENTED | Test TC-7.3 |

---

## Test Deliverables

### Expected Outputs from Testing Phase

1. **Test Execution Report**
   - Test case results (PASS/FAIL) for all categories
   - Total pass rate percentage
   - List of failed test cases with details

2. **Defect Report** (if issues found)
   - Description of each defect
   - Steps to reproduce
   - Severity and priority
   - Suggested fix (if known)

3. **Regression Test Results**
   - Confirmation that all existing commands work
   - List of any broken functionality

4. **Performance Measurements**
   - Timing data for TC-8.1 and TC-8.2
   - Any observed delays or issues

5. **Final Test Summary**
   - Overall assessment: PASS/FAIL
   - Readiness recommendation: READY FOR INTEGRATION / NEEDS REWORK
   - Outstanding issues summary

---

## Success Criteria

The implementation is considered **READY FOR INTEGRATION** when:

- ✅ All Category 1-6 test cases pass (CRITICAL/HIGH priority)
- ✅ All regression tests pass (no broken commands)
- ✅ Performance acceptable (conversions complete in reasonable time)
- ✅ No critical or high-severity defects remain
- ✅ Medium/low defects documented for future iteration

---

## Testing Schedule

### Recommended Testing Timeline

- **Phase 1:** Boundary and common value tests (TC-1.x, TC-2.x) - 30 minutes
- **Phase 2:** Error handling tests (TC-4.x) - 20 minutes
- **Phase 3:** Integration tests (TC-7.x) - 20 minutes
- **Phase 4:** Regression testing - 30 minutes
- **Phase 5:** Optional stress/performance tests (TC-8.x, TC-9.x) - 20 minutes

**Total Estimated Time:** 2 hours for comprehensive testing

---

## Contact and Escalation

### Implementation Agent Information
- **Agent:** Assembly Implementer
- **Implementation Date:** 2025-10-17
- **Status:** Implementation Complete

### For Test Issues
- Document all failures in test report
- Include exact input, expected output, actual output
- Note any error messages or unexpected behavior

### For Critical Bugs
- Immediately document and report
- Include steps to reproduce
- Do not proceed with integration if critical bugs found

---

## Appendix A: Quick Reference

### Command Syntax
```
H:xxxx
```
- xxxx = 4-digit hex value (0000-FFFF)
- Output: Decimal value (0-65535) + newline

### Example Usage
```
> H:0000
0

> H:FFFF
65535

> H:80FF
33023

> H:GGGG
VALUE?
```

---

## Appendix B: Code Locations Reference

| Component | File | Lines | Description |
|-----------|------|-------|-------------|
| Parser | kernel.asm | 1202-1227 | PARSE_CMD_HEX_TO_DEC |
| Converter | kernel.asm | 2231-2282 | CMD_HEX_TO_DECIMAL |
| Division | kernel.asm | 2294-2331 | DIVIDE_BY_10 |
| Jump table LO | kernel.asm | 3328 | Index 15 entry |
| Jump table HI | kernel.asm | 3346 | Index 15 entry |
| Index map | kernel.asm | 3357 | H → 15 |
| Help message | kernel.asm | 3413 | MSG_HELP_HEX_TO_DEC |
| Help table | kernel.asm | 3392 | Table entry |

---

## Appendix C: Test Case Summary Table

| Category | Test Cases | Priority | Status |
|----------|-----------|----------|--------|
| Boundary Values | TC-1.1 - TC-1.5 | CRITICAL/HIGH | READY |
| Common Values | TC-2.1 - TC-2.4 | CRITICAL/MEDIUM | READY |
| Powers of Two | TC-3.1 (16 cases) | MEDIUM | READY |
| Error Handling | TC-4.1 - TC-4.6 | CRITICAL/HIGH | READY |
| Case Sensitivity | TC-5.1 - TC-5.3 | MEDIUM | READY |
| Leading Zeros | TC-6.1 - TC-6.3 | HIGH/MEDIUM | READY |
| Integration | TC-7.1 - TC-7.4 | HIGH/MEDIUM | READY |
| Performance | TC-8.1 - TC-8.2 | LOW | OPTIONAL |
| Stress | TC-9.1 - TC-9.2 | MEDIUM | OPTIONAL |

**Total:** 38+ test cases across 9 categories

---

## Document Status

**Status:** READY_FOR_TESTING

This test plan is complete and ready for use by the Testing Agent. All implementation work is finished, build verified, and comprehensive test procedures documented.

**Next Phase:** Testing → Integration → Deployment

---

**Document Version:** 1.0
**Created:** 2025-10-17
**Author:** Assembly Implementer Agent
**Approved For:** Testing Phase

**End of Test Plan**
