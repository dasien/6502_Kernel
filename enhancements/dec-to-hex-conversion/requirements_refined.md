 # Decimal to Hex Conversion - Refined Requirements

**Feature:** D:nnnnn Monitor Command
**Priority:** Medium
**Status:** Requirements Complete → Ready for Architecture

---

## User Story

**As a** 6502 assembly programmer
**I want** to convert decimal values to hexadecimal
**So that** I can calculate memory offsets, translate specifications, and plan memory layouts without external tools

---

## Functional Requirements (Prioritized)

### Must Have (MVP)

| ID | Requirement | Acceptance Criteria |
|----|-------------|---------------------|
| F-1 | Accept D:nnnnn syntax | Command parser recognizes D: with decimal argument |
| F-2 | Parse decimal 0-65535 | Only digits 0-9 accepted, 1-5 digit length |
| F-3 | Convert to hex | Accurate decimal-to-hex conversion for all valid inputs |
| F-4 | Display 4-digit hex | Output format: 0000-FFFF (uppercase hex) |
| F-5 | Error on invalid digit | Non-decimal characters → "VALUE?" error |
| F-6 | Error on out-of-range | Values > 65535 → "RANGE?" error |
| F-7 | Return to prompt | Command completes and returns to monitor prompt |
| F-8 | Help integration | Add "D:NNNNN DECIMAL TO HEX" to help display |

### Should Have (If Budget Permits)

| ID | Requirement | Notes |
|----|-------------|-------|
| F-9 | Accept leading zeros | D:00256 same as D:256 (parsing flexibility) |
| F-10 | Skip leading spaces | D: 256 works (whitespace tolerance) |
| F-11 | Show both formats | "256 = $0100" (clarity for user) |

### Won't Have (Out of Scope)

- Negative number support (monitor uses unsigned addresses)
- Binary output format (separate feature)
- Multiple values per line
- Comma separators (32,768)

---

## Non-Functional Requirements

### Performance

- **Conversion Time:** < 150ms @ 1MHz (imperceptible to user)
- **Worst Case:** D:65535 must complete within budget
- **Measurement:** Cycle counting on actual 6502 hardware

### Memory

- **ROM Budget:** ≤ 200 bytes total (parsing + conversion + display + integration)
- **Zero Page:** 2-4 bytes for temporary workspace (documented location)
- **System RAM:** 5-10 bytes if needed for scratch variables
- **No Conflicts:** Must not interfere with existing monitor variables

### Reliability

- **Input Validation:** 100% invalid inputs detected and rejected
- **Edge Cases:** 0, 1, 65535, 65536 handled correctly
- **State Cleanup:** No residual effects after command completion
- **Reentrant:** Multiple sequential D: commands work identically

### Compatibility

- **No Regressions:** All existing commands continue working
- **Pattern Compliance:** Follow colon-command syntax (LETTER:arg)
- **Error System:** Reuse MSG_VALUE_ERROR and MSG_RANGE_ERROR
- **Display System:** Use existing PRINT_CHAR or PRINT_CURRENT_ADDRESS

---

## Acceptance Tests (Critical Subset)

### Valid Input Tests

```
D:0       → 0000    # Minimum value
D:256     → 0100    # Common case (screen page)
D:1024    → 0400    # Common buffer size
D:32768   → 8000    # Signed/unsigned boundary
D:65535   → FFFF    # Maximum value
```

### Error Tests

```
D:65536   → RANGE?  # Out of range
D:ABC     → VALUE?  # Invalid characters
D:12A4    → VALUE?  # Mixed hex/decimal
D:        → SYNTAX? # No argument (TBD)
```

### Integration Tests

```
D:1024 then R:0400              # Use result in next command
D:256 then H:0100               # Cross-validate with H: command
Sequential: D:100, D:200, D:300 # Stability test
```

---

## Technical Constraints

### Memory Allocation

**Zero Page (Fast Access)**
- $0000-$0010: Reserved (monitor core variables)
- $00F0-$00FF: Reserved (hex lookup table)
- **Available:** $0011-$00EF (223 bytes)
- **Recommended:** $0011-$0014 (4 bytes for D: command workspace)

**System RAM ($0200-$03FF)**
- $0200-$028F: Monitor variables in use
- **Available:** $02EA-$03FF (278 bytes)
- **Recommended:** Use if zero page insufficient

### Integration Requirements

**Command Parser:**
- Add entry to CMD_INDEX_MAP ('D' = $44, index calculation)
- Add entry to CMD_JUMP_COMPACT_LO/HI tables
- Follow PARSE_COLON_COMMAND pattern

**Error Handling:**
- JSR PRINT_MESSAGE with MSG_VALUE_ERROR for invalid decimal
- JSR PRINT_MESSAGE with MSG_RANGE_ERROR for > 65535
- Return to MONITOR_PROMPT after error display

**Output Display:**
- Store result in MON_CURRADDR_LO/HI ($0000-$0001)
- JSR PRINT_CURRENT_ADDRESS (reuses existing hex display logic)
- Alternative: Custom display routine if format needs differ

---

## Open Questions (For Architect)

### Critical (Must Decide Before Implementation)

**Q1: Output Format**
- Option A: `0100` (hex only, minimal)
- Option B: `256 = $0100` (both formats, verbose)
- Option C: Aligned display with labels
- **Decision Needed:** Coordinate with H: command format

**Q2: Input Length Handling**
- Option A: Limit to 5 characters, reject longer
- Option B: Parse all digits, range check after conversion
- **Recommendation:** Option B (simpler logic)

### Important (Affects UX)

**Q3: Empty Argument Behavior (D:)**
- Option A: Show "SYNTAX?" error
- Option B: Convert current address ($0000 if unset)
- **Recommendation:** Option A (consistency with other commands)

**Q4: Leading Zero Acceptance**
- Question: Should D:00256 work?
- **Recommendation:** Yes (minimal parsing impact)

---

## Design Recommendations for Architect

### Algorithm Selection

**Recommended: Multiply-by-10 Method**

Pseudocode:
```
result = 0
for each digit in input:
    if digit not in '0'-'9':
        error "VALUE?"
    result = result * 10 + digit
    if result > 65535:
        error "RANGE?"
output result as hex
```

**Rationale:**
- Compact code size (~60-80 bytes)
- Performance within budget (~100-120ms @ 1MHz)
- Well-proven in 6502 BASIC interpreters
- Easier to implement and maintain

**Multiply-by-10 Optimization:**
```assembly
; Multiply 16-bit value by 10 efficiently
; result = value * 10 = value * 8 + value * 2
; Use shifts (ASL) for multiplication by powers of 2
```

### Memory Layout

**Temporary Variables (Zero Page)**
```
$0011-$0012: DEC_ACCUMULATOR (16-bit working value)
$0013:       DEC_CHAR_PTR    (input buffer pointer)
$0014:       DEC_TEMP        (digit conversion temp)
```

**Alternative (System RAM if zero page scarce)**
```
$02EA-$02EB: DEC_ACCUMULATOR
$02EC:       DEC_CHAR_PTR
$02ED:       DEC_TEMP
```

### Integration Pattern

```assembly
; Command jump table entry
CMD_DECIMAL_TO_HEX:
    JSR PARSE_DECIMAL_INPUT    ; Parse D:nnnnn format
    BCS ERROR_HANDLER          ; Carry set = error
    JSR CONVERT_DEC_TO_HEX     ; Convert to hex (already in MON_CURRADDR)
    JSR PRINT_CURRENT_ADDRESS  ; Display 4-digit hex
    JMP MONITOR_PROMPT         ; Return to prompt

PARSE_DECIMAL_INPUT:
    ; Parse decimal digits from command buffer
    ; Store result in MON_CURRADDR_LO/HI
    ; Set carry flag on error
    RTS

CONVERT_DEC_TO_HEX:
    ; Conversion is implicit - value already in binary
    ; This routine can be empty or used for validation
    RTS
```

---

## Dependencies

### Existing Code Dependencies

- `PARSE_COMMAND` - Command parser entry point
- `PRINT_CURRENT_ADDRESS` - 4-digit hex display routine
- `PRINT_MESSAGE` - Error message display
- `PRINT_CHAR` - Character output
- `MSG_VALUE_ERROR` - Invalid input error message
- `MSG_RANGE_ERROR` - Out of range error message

### Companion Feature

- **H:xxxx (hex-to-decimal)** - Parallel development
- **Coordination Required:** Output format consistency
- **Testing:** Cross-validation between D: and H: commands

### No External Dependencies

This is a pure kernel feature with no external libraries or tools required.

---

## Test Strategy

### Unit Testing (Component Level)

**Parse Function Tests:**
- Valid single digit: "0", "1", "9"
- Valid multiple digits: "256", "1024", "65535"
- Invalid characters: "ABC", "12A", "1G5"
- Leading zeros: "00256", "00000"
- Empty input: ""

**Conversion Tests:**
- Boundary values: 0, 1, 255, 256, 32768, 65535
- Powers of 2: 256, 512, 1024, 2048, 4096, 8192, 16384
- Powers of 10: 10, 100, 1000, 10000
- Out of range: 65536, 99999, 100000

### Integration Testing (System Level)

**Command Flow:**
1. D:256 → verify hex output → use in R:0100
2. D:1024 → verify hex output → use in W:0400
3. D:65535 → verify FFFF → H:FFFF → verify 65535

**Error Recovery:**
1. D:ABC → VALUE? → R:8000 (should work)
2. D:99999 → RANGE? → D:256 (should work)

**Stability:**
1. Execute D: command 100 times with various inputs
2. Verify no memory corruption or state leakage

### Manual Testing (Hardware)

**Real Hardware Verification:**
- Test on actual 6502/65C02 system
- Measure timing with stopwatch (should be imperceptible)
- Verify display output on real screen
- Test keyboard input handling

---

## Success Metrics

### Code Quality

- [ ] ROM size ≤ 200 bytes (measured by assembler)
- [ ] All code commented and documented
- [ ] No compiler/assembler warnings
- [ ] Follows existing code style and patterns

### Functional Completeness

- [ ] All 8 Must Have requirements implemented (F-1 through F-8)
- [ ] All 10 valid input test cases pass
- [ ] All 8 error test cases pass
- [ ] All 3 integration test scenarios pass

### Documentation

- [ ] Help text updated (D:NNNNN DECIMAL TO HEX)
- [ ] Memory map updated with variable allocations
- [ ] Implementation notes document created
- [ ] Code comments explain algorithm and optimizations

### Performance

- [ ] Conversion time < 150ms @ 1MHz (all test cases)
- [ ] Worst case (D:65535) within performance budget
- [ ] No perceptible delay to user

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| ROM budget exceeded | Use multiply-by-10 algorithm, inline critical code, reuse existing routines |
| Algorithm too slow | Optimize multiply-by-10 with shifts, measure early and adjust |
| Zero page conflicts | Document allocations clearly, use designated scratch area |
| Inconsistent with H: | Coordinate output format early, test cross-validation |
| Hardware timing differs | Test on real hardware, not just emulator |

---

## Implementation Checklist

### Architecture Phase
- [ ] Answer critical open questions (Q1-Q4)
- [ ] Select algorithm (recommend multiply-by-10)
- [ ] Define memory allocations
- [ ] Design integration points
- [ ] Create detailed test plan

### Implementation Phase
- [ ] Write PARSE_DECIMAL_INPUT routine
- [ ] Write multiply-by-10 conversion logic
- [ ] Integrate with command parser
- [ ] Add error handling
- [ ] Update help text

### Testing Phase
- [ ] Run all unit tests
- [ ] Run all integration tests
- [ ] Measure code size
- [ ] Measure performance
- [ ] Test on real hardware

### Documentation Phase
- [ ] Update kernel_memory_map.md
- [ ] Write implementation notes
- [ ] Add code comments
- [ ] Update user documentation

---

**Status:** READY_FOR_DEVELOPMENT

**Next Phase:** Architecture & Technical Design

**Next Agent:** 6502 Assembly Developer (Architecture)

**Blocking Issues:** None

**Estimated Timeline:** 9-18 hours total (architecture + implementation + testing + documentation)
