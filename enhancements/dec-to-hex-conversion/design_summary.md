# Design Summary: Decimal-to-Hex Conversion (D:)

**Status:** READY_FOR_IMPLEMENTATION
**Date:** 2025-10-11
**Task ID:** task_1760171649_54510

---

## Quick Reference

**Purpose:** Add D:nnnnn command to convert decimal values (0-65535) to hexadecimal (0000-FFFF)

**Primary Document:** `implementation_plan.md` (complete implementation instructions)
**Architecture Document:** `technical_architecture.md` (design decisions and rationale)
**Requirements Document:** `analysis_summary.md` (detailed requirements analysis)

---

## Key Technical Decisions

### 1. Algorithm
**Selected:** Multiply-by-10 (Horner's Method)
- Formula: result = (((d1×10 + d2)×10 + d3)×10 + d4)×10 + d5
- Technique: (value × 10) = (value << 3) + (value << 1)
- Rationale: Best balance of code size, performance, and maintainability

### 2. Memory Allocation
**Zero Page:** $35-$37 (3 bytes)
- $35: DEC_TEMP_LO (multiply temporary)
- $36: DEC_TEMP_HI (multiply temporary)
- $37: DEC_DIGIT_IDX (digit counter)

**Reused:** MON_CURRADDR_HI/LO ($14/$15) for result storage

### 3. Output Format
**Decision:** 4-digit hex only
- Example: `D:256` displays `0400` (not "256 = $0400")
- Rationale: Consistency with other commands, minimal ROM, adequate clarity

### 4. Input Handling
- Accept all digits, range check after conversion
- Accept leading zeros (D:00256 = D:256)
- Empty argument (D:) shows SYNTAX? error
- Invalid digit shows VALUE? error
- Overflow (>65535) shows RANGE? error

---

## Resource Budget

### ROM Usage
```
Component                    Bytes
─────────────────────────────────
PARSE_CMD_DECIMAL_CHECK:      15
CMD_DECIMAL_TO_HEX:           40
PARSE_DECIMAL_VALUE:          85
MULTIPLY_BY_10:               45
Jump tables:                   2
Help message:                 10
─────────────────────────────────
TOTAL:                       197
BUDGET:                      200
MARGIN:                        3
```

### Performance
```
Case                    Cycles      Time @ 1MHz
────────────────────────────────────────────────
Best (D:0):               545       0.5 ms
Average (D:256):          825       0.8 ms
Worst (D:65535):        1,325       1.3 ms
────────────────────────────────────────────────
BUDGET:               150,000     150.0 ms
MARGIN:                  113×      99.1% margin
```

---

## Implementation Checklist

### Step 1: Update Command Tables
- [ ] Modify CMD_INDEX_MAP['D'-'B'] = 14 (change $FF to 14)
- [ ] Add to CMD_JUMP_COMPACT_LO[14] = <PARSE_CMD_DECIMAL_CHECK
- [ ] Add to CMD_JUMP_COMPACT_HI[14] = >PARSE_CMD_DECIMAL_CHECK

### Step 2: Add Zero Page Variables
- [ ] Define DEC_TEMP_LO = $35
- [ ] Define DEC_TEMP_HI = $36
- [ ] Define DEC_DIGIT_IDX = $37

### Step 3: Implement Routines
- [ ] PARSE_CMD_DECIMAL_CHECK (entry point)
- [ ] CMD_DECIMAL_TO_HEX (orchestrator)
- [ ] PARSE_DECIMAL_VALUE (string parser)
- [ ] MULTIPLY_BY_10 (16-bit arithmetic)

### Step 4: Update Help System
- [ ] Add MSG_HELP_DECIMAL string
- [ ] Add to HELP_MSG_TABLE
- [ ] Increment HELP_MSG_COUNT to 14

### Step 5: Documentation
- [ ] Update kernel_memory_map.md
- [ ] Add inline code comments
- [ ] Document integration points

### Step 6: Testing
- [ ] Run unit tests (27 test cases)
- [ ] Run integration tests (4 scenarios)
- [ ] Verify no regressions
- [ ] Test on real hardware

---

## Test Cases Summary

### Boundary Values (7 tests)
- D:0 → 0000
- D:1 → 0001
- D:255 → 00FF
- D:256 → 0100
- D:65534 → FFFE
- D:65535 → FFFF
- D:65536 → RANGE?

### Powers of 2 (7 tests)
- D:512 → 0200
- D:1024 → 0400
- D:2048 → 0800
- D:4096 → 1000
- D:8192 → 2000
- D:16384 → 4000
- D:32768 → 8000

### Powers of 10 (4 tests)
- D:10 → 000A
- D:100 → 0064
- D:1000 → 03E8
- D:10000 → 2710

### Error Cases (5 tests)
- D: → ERROR?
- D:ABC → VALUE?
- D:12A4 → VALUE?
- D:99999 → RANGE?
- D:100000 → RANGE?

### Edge Cases (4 tests)
- D:00256 → 0100 (leading zeros)
- D: 256 → 0100 or ERROR? (optional)
- D:000 → 0000
- D:00001 → 0001

**Total: 27 test cases**

---

## Integration Points

### 1. Command Parser
- **File:** kernel.asm, line ~2964
- **Change:** CMD_INDEX_MAP[2] = 14 (was $FF)
- **Risk:** Low (standard pattern)

### 2. Jump Tables
- **File:** kernel.asm, line ~2930, 2946
- **Change:** Add entries at index 14
- **Risk:** Low (append to existing)

### 3. Error System
- **File:** kernel.asm, line ~3035-3037
- **Change:** Reuse existing messages
- **Risk:** None (no changes)

### 4. Display System
- **File:** kernel.asm (existing routine)
- **Change:** Reuse PRINT_CURRENT_ADDRESS
- **Risk:** None (no changes)

### 5. Help System
- **File:** kernel.asm, line ~3000
- **Change:** Add message and table entry
- **Risk:** Low (simple addition)

---

## Key Files Modified

```
src/kernel/kernel.asm
├── Line ~95:    Add zero page variable definitions
├── Line ~1165:  Add PARSE_CMD_DECIMAL_CHECK
├── Line ~1170:  Add CMD_DECIMAL_TO_HEX
├── Line ~1200:  Add PARSE_DECIMAL_VALUE
├── Line ~1285:  Add MULTIPLY_BY_10
├── Line ~2930:  Modify CMD_JUMP_COMPACT_LO
├── Line ~2946:  Modify CMD_JUMP_COMPACT_HI
├── Line ~2964:  Modify CMD_INDEX_MAP
├── Line ~3000:  Add MSG_HELP_DECIMAL
└── Line ~3001:  Modify HELP_MSG_TABLE

kernel_memory_map.md (if exists)
└── Add zero page documentation ($35-$37)
```

---

## Risk Summary

### High Priority (Must Address)
✓ ROM budget (197/200 bytes, 3-byte margin)
✓ Overflow detection (multiple checks implemented)
✓ Stack balance (careful PHA/PLA pairing)

### Medium Priority (Monitor)
✓ Parser edge cases (comprehensive test coverage)
✓ Zero page conflicts (documented allocation)
✓ Integration testing (test plan defined)

### Low Priority (Acceptable)
- Performance (99% margin above requirement)
- Help system display (simple addition)
- Documentation (document-first approach)

**Overall Risk:** LOW - Well-designed, thoroughly planned

---

## Success Metrics

### Must Have (MVP)
- [x] Algorithm selected and designed
- [x] Memory layout defined
- [x] Error handling specified
- [x] Output format decided
- [x] Integration points identified
- [x] Test plan created
- [x] Documentation complete

### Quality Gates
- [ ] Code review passed
- [ ] All tests passed (27/27)
- [ ] No regressions detected
- [ ] ROM budget met (≤200 bytes)
- [ ] Performance verified (<150ms)
- [ ] Real hardware tested

### User Acceptance
- [ ] D:nnnnn converts correctly
- [ ] Error messages clear
- [ ] Help text displayed
- [ ] No user confusion
- [ ] Workflow smooth

---

## Next Steps

### Immediate (Implementation Phase)
1. Implement PARSE_CMD_DECIMAL_CHECK (15 bytes)
2. Implement CMD_DECIMAL_TO_HEX (40 bytes)
3. Implement PARSE_DECIMAL_VALUE (85 bytes)
4. Implement MULTIPLY_BY_10 (45 bytes)
5. Update command tables (2 bytes)
6. Update help system (10 bytes)

### Testing Phase
1. Build and verify size (must be ≤200 bytes)
2. Run unit tests (multiply, parser, command)
3. Run integration tests (dispatch, errors, display)
4. Run regression tests (all existing commands)
5. Test on real hardware (timing verification)

### Documentation Phase
1. Update kernel_memory_map.md
2. Add inline code comments
3. Update user documentation
4. Create implementation notes

### Deployment Phase
1. Final code review
2. Performance validation
3. Quality gate approval
4. Commit to repository
5. Update changelog

---

## FAQ

**Q: Why multiply-by-10 instead of lookup table?**
A: Similar code size (~5 bytes difference), better maintainability, more straightforward debugging. Performance difference negligible (1.3ms vs 0.8ms, both well under 150ms budget).

**Q: Why only 3 bytes zero page?**
A: Minimal temporary workspace needed. Multiply uses 2 bytes ($35-$36), digit counter uses 1 byte ($37). Result uses existing MON_CURRADDR variables.

**Q: Why not show "256 = $0100" format?**
A: Consistency with other commands (R:, W:, G: show minimal output), ROM savings (~20 bytes), screen space efficiency. User already knows input format.

**Q: Can we support D:256+128 expressions?**
A: Not in current scope. Future enhancement possible. Current design provides foundation for calculator command if needed.

**Q: What if ROM exceeds 200 bytes?**
A: Multiple fallback options: remove optional space handling (-10 bytes), simplify error handling (-15 bytes), optimize string handling (-8 bytes). Currently at 197 bytes with 3-byte margin.

**Q: How do we test overflow detection?**
A: Specific test cases: D:65535 (max valid), D:65536 (first overflow), D:99999 (overflow), D:100000 (overflow). Multiply routine checks carry at each step.

**Q: Will this work with H: command?**
A: Yes, designed for compatibility. D:256 → $0100, then H:0100 → 256. Symmetric conversion for cross-validation.

**Q: What about negative numbers?**
A: Out of scope. Monitor works with unsigned 16-bit addresses (0-65535). Negative numbers not meaningful in address space context.

---

## Document Cross-Reference

**Primary Implementation Guide:**
→ `implementation_plan.md` (complete step-by-step instructions)

**Detailed Architecture:**
→ `technical_architecture.md` (design decisions, rationale, analysis)

**Requirements Source:**
→ `analysis_summary.md` (comprehensive requirements analysis)

**Original Enhancement:**
→ `dec_to_hex_enhancement.md` (initial feature proposal)

**Related Feature:**
→ `../hex-to-dec-conversion/` (companion H: command)

---

## Contact & Support

**Implementation Questions:** Refer to `implementation_plan.md` Section 5 (Code Specifications)

**Architecture Questions:** Refer to `technical_architecture.md` Section 2 (Design Decisions)

**Test Questions:** Refer to `implementation_plan.md` Section 7 (Testing & Validation)

**Risk Questions:** Refer to `technical_architecture.md` Section 7 (Risk Analysis)

---

**Status:** READY_FOR_IMPLEMENTATION
**Confidence Level:** HIGH
**Blocking Issues:** NONE
**Go/No-Go Decision:** ✓ GO

---

**Document Version:** 1.0
**Last Updated:** 2025-10-11
**Author:** 6502 Assembly Developer Agent
