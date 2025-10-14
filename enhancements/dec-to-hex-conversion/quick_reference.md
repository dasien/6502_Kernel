# D: Command - Quick Reference

**Feature:** Decimal to Hex Conversion Monitor Command
**Command Syntax:** `D:nnnnn`
**Status:** Requirements Complete → Ready for Architecture

---

## At a Glance

| Aspect | Detail |
|--------|--------|
| **Command** | D:nnnnn (decimal input) |
| **Input Range** | 0 to 65535 |
| **Output** | 4-digit hex (0000-FFFF) |
| **ROM Budget** | ≤ 200 bytes |
| **Performance** | < 150ms @ 1MHz |
| **Priority** | Medium |

---

## Test Cases (Quick Check)

### ✅ Valid Inputs
```
D:0      → 0000
D:256    → 0100
D:1024   → 0400
D:32768  → 8000
D:65535  → FFFF
```

### ❌ Error Cases
```
D:65536  → RANGE?
D:ABC    → VALUE?
D:12A4   → VALUE?
```

---

## Memory Budget

| Resource | Allocation |
|----------|-----------|
| ROM | ≤ 200 bytes |
| Zero Page | 2-4 bytes |
| System RAM | 5-10 bytes (if needed) |

---

## Key Integration Points

1. **Command Parser:** Add to CMD_INDEX_MAP ('D' entry)
2. **Error System:** Use MSG_VALUE_ERROR, MSG_RANGE_ERROR
3. **Display:** Use PRINT_CURRENT_ADDRESS or custom
4. **Help:** Add "D:NNNNN DECIMAL TO HEX" entry

---

## Algorithm Recommendation

**Multiply-by-10 Method:**
```
result = 0
for each digit:
    result = result * 10 + digit
    check overflow → RANGE? error
```

**Why:** Best code size (~60-80 bytes), adequate performance, proven approach

---

## Critical Decisions Needed

1. **Output Format:** "0100" vs "256 = $0100" vs labeled
2. **Input Length:** Limit 5 digits vs parse all and range check
3. **Algorithm:** Multiply-by-10 vs lookup table vs Horner's
4. **Memory:** Zero page vs System RAM allocation

---

## Acceptance Criteria (Summary)

- [ ] All valid inputs (0-65535) convert correctly
- [ ] Invalid characters → "VALUE?" error
- [ ] Out of range → "RANGE?" error
- [ ] Code size ≤ 200 bytes
- [ ] Performance < 150ms @ 1MHz
- [ ] No regressions in existing commands
- [ ] Help text updated
- [ ] Memory map documented

---

## Related Documents

- **Detailed Analysis:** `analysis_summary.md` (comprehensive requirements)
- **Refined Requirements:** `requirements_refined.md` (concise specs)
- **Decision Log:** `decision_log.md` (architectural decisions)
- **Source Enhancement:** `dec_to_hex_enhancement.md` (original request)

---

## Companion Feature

**H:xxxx (Hex to Decimal)** - parallel development
- Must coordinate output format for consistency
- Cross-validation test: D:256 → H:0100 → should return 256

---

## Implementation Phases

1. **Architecture** (2-4h): Make critical decisions, design algorithm
2. **Implementation** (4-8h): Write parsing + conversion code
3. **Testing** (2-4h): Verify all test cases, measure performance
4. **Documentation** (1-2h): Update memory map, help, comments

**Total:** 9-18 hours

---

**Status:** 🟢 READY FOR ARCHITECTURE PHASE

**Blocking Issues:** None

**Next Agent:** 6502 Assembly Developer (Architecture focus)
