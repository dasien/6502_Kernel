# Decision Log - Decimal to Hex Conversion

**Feature:** D:nnnnn Monitor Command
**Purpose:** Track architectural and design decisions during development
**Owner:** Architecture Team

---

## Decision Template

Each decision follows this format:
- **Decision ID:** Unique identifier
- **Question:** What needs to be decided?
- **Options:** Available choices with pros/cons
- **Recommendation:** Analysis-based recommendation
- **Decision:** Final decision (to be filled by architect)
- **Rationale:** Why this decision was made
- **Impact:** What this affects
- **Date:** When decision was made

---

## CRITICAL DECISIONS (Block Development)

### D-001: Output Format Selection

**Status:** 🔴 PENDING - Must decide before implementation

**Question:** What format should the output display use?

**Options:**

**Option A: Hex Only (Minimal)**
```
D:256
$0100
>
```
- Pros: Smallest code size, fastest display, clean output
- Cons: User must remember what they typed

**Option B: Both Formats (Verbose)**
```
D:256
256 = $0100
>
```
- Pros: Clear, self-documenting, easy to verify
- Cons: Larger code size (~30 bytes), more screen space

**Option C: Labeled Format**
```
D:256
HEX: 0100
>
```
- Pros: Clear label, moderate code size
- Cons: Extra screen line, label redundant given command

**Option D: Aligned Display**
```
D:256
DEC: 00256
HEX: 0100
>
```
- Pros: Structured, easy to read
- Cons: Largest code size, uses two screen lines

**Recommendation:** Option B (256 = $0100)
- Balances clarity with code efficiency
- Symmetric to H: command if it uses same format
- User can verify conversion visually
- Only ~30 bytes more than minimal

**Dependencies:**
- Must coordinate with H: command implementation
- H: command should use symmetric format (e.g., "$0100 = 256")

**Decision:** Option A

**Rationale:** The user doesn't need to remember what they typed, it is right above the hex version


---

### D-002: Input Length Handling Strategy

**Status:** 🔴 PENDING - Must decide before implementation

**Question:** How should we handle input longer than 5 digits (e.g., D:123456)?

**Options:**

**Option A: Hard Limit 5 Characters**
```assembly
; Parse up to 5 digits only, reject if 6th character exists
PARSE_DECIMAL_INPUT:
    LDY #0
PARSE_LOOP:
    CPY #5          ; Max 5 digits
    BEQ CHECK_END   ; If 5 digits, check for terminator
    ; ... parse digit
    INY
    JMP PARSE_LOOP
CHECK_END:
    LDA (CMDBUF),Y  ; Check next character
    CMP #CR         ; Should be carriage return
    BNE SYNTAX_ERROR ; If not, syntax error
```
- Pros: Clear limit, prevents long input processing
- Cons: More complex parsing logic, extra code for length check

**Option B: Parse All Digits, Range Check After**
```assembly
; Parse all digits until non-digit found, then check range
PARSE_DECIMAL_INPUT:
    LDA #0
    STA ACCUMULATOR_LO
    STA ACCUMULATOR_HI
PARSE_LOOP:
    LDA (CMDBUF),Y  ; Get next character
    CMP #'0'
    BCC PARSE_DONE  ; Not a digit, done parsing
    CMP #'9'+1
    BCS PARSE_DONE  ; Not a digit, done parsing
    ; ... multiply by 10 and add digit
    ; Check overflow
    BCS RANGE_ERROR ; Overflow = out of range
    INY
    JMP PARSE_LOOP
PARSE_DONE:
    ; ACCUMULATOR now has value, already range-checked
```
- Pros: Simpler logic, natural overflow detection, less code
- Cons: Accepts long input before rejecting (cosmetic only)

**Recommendation:** Option B (Parse All, Range Check After)
- Simpler implementation (~20 bytes less code)
- Natural overflow detection during multiply-by-10
- More intuitive error message (RANGE? vs SYNTAX?)
- User typing "123456" gets "RANGE?" which is accurate

**Decision:** Option B

**Rationale:** The clear error messages make it understood to the user whether they have entered an invalid value (not numeric) or a value greater than 65,535


---

## IMPORTANT DECISIONS (Affect UX)

### D-003: Empty Argument Behavior

**Status:** 🟡 IMPORTANT - Affects UX but doesn't block core implementation

**Question:** What should D: (with no argument) do?

**Options:**

**Option A: Syntax Error**
```
D:
ERROR?
>
```
- Pros: Consistent with other commands (R:, W: require arguments)
- Cons: Less convenient for edge case users

**Option B: Convert Current Address**
```
D:
0 = $0000    # If MON_CURRADDR is $0000
>
```
- Pros: Convenient, reuses current address pointer
- Cons: May be surprising behavior, adds code for special case

**Option C: Convert $0000**
```
D:
0 = $0000    # Always convert zero
>
```
- Pros: Predictable, simple to implement
- Cons: Not very useful, why would user want this?

**Recommendation:** Option A (Syntax Error)
- Consistency with existing monitor commands (R:, W:, G: all require arguments)
- Simpler implementation (no special case code)
- Clear user feedback that argument is required
- If user wants to convert 0, they can type D:0

**Decision:** Option A

**Rationale:** It is not valid syntax to not enter a value

---

### D-004: Leading Zero Acceptance

**Status:** 🟡 IMPORTANT - Affects input flexibility

**Question:** Should D:00256 work the same as D:256?

**Options:**

**Option A: Accept Leading Zeros**
```
D:00256
256 = $0100    # Same result as D:256
>
```
- Pros: Flexible input, user-friendly, minimal code impact
- Cons: None (parsing naturally handles this)

**Option B: Reject Leading Zeros**
```
D:00256
VALUE?         # Treat as invalid
>
```
- Pros: Enforces canonical format
- Cons: Requires extra code to detect and reject, less user-friendly

**Recommendation:** Option A (Accept Leading Zeros)
- Natural result of digit-by-digit parsing (no extra code needed)
- User-friendly (typing D:0256 or D:00256 just works)
- Consistent with how numbers work generally
- No code penalty

**Decision:** Option A

**Rationale:** No cost to implement and it is an edge case anyway

---

### D-005: Leading Space Handling

**Status:** 🟢 NICE-TO-HAVE - Not critical, code size dependent

**Question:** Should spaces after the colon be skipped (D: 256)?

**Options:**

**Option A: Skip Leading Spaces**
```assembly
PARSE_DECIMAL_INPUT:
    ; Skip spaces before first digit
SKIP_SPACES:
    LDA (CMDBUF),Y
    CMP #' '
    BNE START_PARSE
    INY
    JMP SKIP_SPACES
START_PARSE:
    ; Begin digit parsing
```
- Pros: User-friendly, forgiving input
- Cons: ~10-15 bytes extra code

**Option B: Require Immediate Digit**
```
D: 256
VALUE?         # Space is not a digit
>
```
- Pros: Simpler, no extra code
- Cons: Stricter input requirements

**Recommendation:** Option B (No Space Skipping)
- Saves 10-15 bytes of ROM
- Monitor commands don't generally skip spaces
- Consistent with existing command parsing
- User can learn to type D:256 without space

**Decision:** Option B

**Rationale:** Consistent with other commands

---

## ALGORITHM DECISIONS

### D-006: Decimal-to-Binary Conversion Algorithm

**Status:** 🔴 CRITICAL - Core implementation decision

**Question:** Which algorithm should be used for decimal-to-binary conversion?

**Options:**

**Option 1: Multiply-by-10 Method (RECOMMENDED)**

Algorithm:
```
result = 0
for each digit:
    result = result * 10
    result = result + digit_value
    if overflow: error "RANGE?"
```

Implementation approach:
```assembly
; Multiply 16-bit value by 10 using shifts and adds
; val * 10 = val * 8 + val * 2 = (val << 3) + (val << 1)
MULTIPLY_BY_10:
    ; Input: ACCUMULATOR (16-bit)
    ; Output: ACCUMULATOR * 10
    LDA ACCUMULATOR_LO      ; Save original
    STA TEMP_LO
    LDA ACCUMULATOR_HI
    STA TEMP_HI

    ASL ACCUMULATOR_LO      ; * 2
    ROL ACCUMULATOR_HI
    BCS OVERFLOW            ; Check carry

    ASL ACCUMULATOR_LO      ; * 4
    ROL ACCUMULATOR_HI
    BCS OVERFLOW

    ASL ACCUMULATOR_LO      ; * 8
    ROL ACCUMULATOR_HI
    BCS OVERFLOW

    CLC
    LDA ACCUMULATOR_LO      ; + original * 2
    ADC TEMP_LO
    STA ACCUMULATOR_LO
    LDA ACCUMULATOR_HI
    ADC TEMP_HI
    STA ACCUMULATOR_HI
    BCS OVERFLOW

    ; ... actually need to add original * 2, not original
    ; Refine this in implementation
    RTS
```

- Code size: ~60-80 bytes
- Performance: ~100-120ms @ 1MHz for 5 digits
- Memory: 4 bytes (2 for accumulator, 2 for temp)
- Pros: Compact, proven in BASIC interpreters, within performance budget
- Cons: Slightly slower than lookup table

**Option 2: Powers-of-10 Lookup Table**

Algorithm:
```
result = digit[0] * 10000 +
         digit[1] * 1000 +
         digit[2] * 100 +
         digit[3] * 10 +
         digit[4] * 1
```

Table:
```assembly
POW10_LO:  .BYTE $10, $E8, $64, $0A, $01  ; 10000, 1000, 100, 10, 1 (low bytes)
POW10_HI:  .BYTE $27, $03, $00, $00, $00  ; (high bytes)
```

- Code size: ~120-150 bytes (includes 10-byte table)
- Performance: ~40-60ms @ 1MHz
- Memory: 10 bytes ROM (table) + 6 bytes RAM (accumulator + index)
- Pros: Faster, straightforward
- Cons: Larger code size, may exceed ROM budget

**Option 3: Horner's Method**

Algorithm:
```
result = ((((d0 * 10) + d1) * 10 + d2) * 10 + d3) * 10 + d4
```

- Code size: ~70-90 bytes
- Performance: ~80-100ms @ 1MHz
- Memory: 4 bytes
- Pros: Balanced approach
- Cons: More complex to implement correctly

**Recommendation:** Option 1 (Multiply-by-10)
- Best code size within performance budget
- Well-proven algorithm in 6502 software
- Easier to implement and debug
- Performance adequate for interactive use

**Decision:** Option 1

**Rationale:** Proven method and no real performance overhead

---

## MEMORY ALLOCATION DECISIONS

### D-007: Zero Page Variable Allocation

**Status:** 🔴 CRITICAL - Must document memory usage

**Question:** Where should temporary variables be allocated?

**Available Locations:**

**Zero Page Options:**
- $0011-$00EF: Available (223 bytes)
- Recommended: $0011-$0014 (4 bytes)

**System RAM Options:**
- $02EA-$03FF: Available (278 bytes)
- Recommended: $02EA-$02ED (4 bytes)

**Required Variables:**
```
DEC_ACCUMULATOR_LO:  1 byte   # Working value (low byte)
DEC_ACCUMULATOR_HI:  1 byte   # Working value (high byte)
DEC_CHAR_PTR:        1 byte   # Input buffer pointer/offset
DEC_TEMP:            1 byte   # Temporary storage for multiply
```

**Option A: Use Zero Page ($0035-$0038)**
- Pros: Fastest access, optimal for multiply-by-10
- Cons: Uses precious zero page space

**Option B: Use System RAM ($02EA-$02ED)**
- Pros: Preserves zero page for critical uses
- Cons: Slower access (absolute addressing vs zero page)

**Option C: Use MON_CURRADDR for result, minimize temps**
- Store final result in MON_CURRADDR_LO/HI ($0000-$0001)
- Only allocate 2 bytes for temporary multiply workspace
- Pros: Reuses existing variable, smaller allocation
- Cons: Must preserve MON_CURRADDR during parsing if needed

**Recommendation:** Option C with Zero Page temps
- Use $0000-$0001 (MON_CURRADDR) for accumulator/result
- Use $0011-$0012 for multiply temporary storage (2 bytes)
- Minimal allocation, reuses existing infrastructure
- Fast access for critical multiply operation

**Decision:** Option A.  It is critical to use only those addresses. do not pick other addresses in zero page

**Rationale:** Basic and the kernel both make use of zero page and only specific addresses are safe

**Memory Map Update Required:** Yes - document in kernel_memory_map.md


---

## INTEGRATION DECISIONS

### D-008: Command Jump Table Integration

**Status:** 🟡 IMPORTANT - Standard integration pattern

**Question:** How should D: command be added to command jump table?

**Background:**
Current command structure uses:
- CMD_INDEX_MAP: Maps command letter to jump table index
- CMD_JUMP_COMPACT_LO/HI: Compact jump table (address - 1)

**Implementation Pattern:**
```assembly
; Add to CMD_INDEX_MAP
; 'D' = $44, 'C' = $43, so offset is $44 - $43 = $01
; Find position for 'D' in alphabetical command order

CMD_INDEX_MAP:
    .BYTE $00  ; 'C' = Clear screen
    .BYTE $01  ; 'D' = Decimal to hex  ← ADD THIS
    .BYTE $02  ; 'F' = Fill memory
    .BYTE $03  ; 'G' = Go/Run
    ; ... etc

; Add to jump table
CMD_JUMP_COMPACT_LO:
    .BYTE <(CMD_CLEAR_SCREEN - 1)
    .BYTE <(CMD_DECIMAL_TO_HEX - 1)     ← ADD THIS
    .BYTE <(CMD_FILL_MEMORY - 1)
    ; ... etc

CMD_JUMP_COMPACT_HI:
    .BYTE >(CMD_CLEAR_SCREEN - 1)
    .BYTE >(CMD_DECIMAL_TO_HEX - 1)     ← ADD THIS
    .BYTE >(CMD_FILL_MEMORY - 1)
    ; ... etc
```

**Decision:** Follow existing pattern (standard integration)

**Rationale:** Consistency with existing command structure

---

## DISPLAY DECISIONS

### D-009: Hex Output Routine Selection

**Status:** 🟡 IMPORTANT - Affects output formatting

**Question:** Which routine should display the hex result?

**Options:**

**Option A: Reuse PRINT_CURRENT_ADDRESS**
```assembly
CMD_DECIMAL_TO_HEX:
    JSR PARSE_DECIMAL_INPUT      ; Parse D:nnnnn
    BCS ERROR_HANDLER            ; Handle errors
    ; Result already in MON_CURRADDR_LO/HI
    JSR PRINT_CURRENT_ADDRESS    ; Display as 4-digit hex
    JMP MONITOR_PROMPT
```
- Pros: Smallest code (reuses existing), guaranteed 4-digit format
- Cons: Limited formatting options, tied to existing output format

**Option B: Custom Hex Display**
```assembly
CMD_DECIMAL_TO_HEX:
    JSR PARSE_DECIMAL_INPUT
    BCS ERROR_HANDLER
    ; Custom formatting to show "256 = $0100"
    JSR PRINT_DECIMAL_VALUE      ; Print original decimal
    JSR PRINT_EQUALS_HEX         ; Print " = $"
    JSR PRINT_HEX_VALUE          ; Print 4-digit hex
    JMP MONITOR_PROMPT
```
- Pros: Full control over format, can show both values
- Cons: ~40-60 bytes extra code

**Recommendation:** Depends on D-001 (Output Format Decision)
- If Option A (hex only) chosen in D-001: Use Option A (reuse existing)
- If Option B (both formats) chosen in D-001: Use Option B (custom display)

**Decision:** Option B

**Rationale:** We can't use MON_CURR_ADDR as that is used for other things.  Use the variables we created in zero page for this.

---

## SUMMARY: Decision Dependencies

```
Critical Path:
    D-001 (Output Format) → D-009 (Display Routine)
    D-002 (Input Length) → Implementation approach
    D-006 (Algorithm) → D-007 (Memory allocation)
    D-007 (Memory) → Update kernel_memory_map.md

Can Proceed in Parallel:
    D-003 (Empty arg)
    D-004 (Leading zeros)
    D-005 (Leading spaces)
    D-008 (Jump table) - standard pattern

Blocking Implementation:
    🔴 D-001: Output Format
    🔴 D-002: Input Length Handling
    🔴 D-006: Algorithm Selection
    🔴 D-007: Memory Allocation

Can Decide During Implementation:
    🟡 D-003, D-004, D-005 (input handling details)
    🟡 D-008 (standard pattern)
    🟡 D-009 (depends on D-001)
```

---

## DECISION TRACKING CHECKLIST

**Before Architecture Phase Complete:**
- [ ] D-001: Output Format (CRITICAL)
- [ ] D-002: Input Length (CRITICAL)
- [ ] D-006: Algorithm (CRITICAL)
- [ ] D-007: Memory Allocation (CRITICAL)

**Before Implementation Phase:**
- [ ] D-003: Empty Argument (IMPORTANT)
- [ ] D-004: Leading Zeros (IMPORTANT)
- [ ] D-005: Leading Spaces (NICE-TO-HAVE)
- [ ] D-008: Jump Table Integration (STANDARD)

**After D-001 Decision:**
- [ ] D-009: Display Routine (DEPENDENT)

---

**Status:** Ready for Architect Review

**Next Action:** Architect reviews and comments on decisions for 🔴 CRITICAL items

**Timeline:** Architecture decisions should be made before implementation begins
