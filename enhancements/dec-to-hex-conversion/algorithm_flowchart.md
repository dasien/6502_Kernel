# Algorithm Flowcharts: Decimal-to-Hex Conversion

**Purpose:** Visual reference for implementation and debugging
**Date:** 2025-10-11
**Task ID:** task_1760171649_54510

---

## 1. Overall Command Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Types: D:1024                        │
└────────────────────────────────┬────────────────────────────────┘
                                 │
                                 ▼
                    ┌────────────────────────┐
                    │   PARSE_COMMAND        │
                    │   (command dispatcher) │
                    └────────────┬───────────┘
                                 │
                                 │ Looks up 'D' in CMD_INDEX_MAP
                                 │ Gets index 14
                                 │ Jumps via CMD_JUMP_COMPACT[14]
                                 ▼
                    ┌────────────────────────┐
                    │ PARSE_CMD_DECIMAL_CHECK│
                    │ (D: command entry)     │
                    └────────────┬───────────┘
                                 │
                                 │ JSR PARSE_COLON_COMMAND
                                 ▼
                    ┌────────────────────────┐
                    │  Validate "D:" syntax  │
                    └────────┬───────┬───────┘
                             │       │
                      Valid  │       │ Invalid
                             ▼       ▼
                    ┌──────────┐  ┌─────────┐
                    │ Continue │  │ Error   │
                    └────┬─────┘  └─────────┘
                         │
                         │ JSR CMD_DECIMAL_TO_HEX
                         ▼
        ┌────────────────────────────────────────┐
        │      CMD_DECIMAL_TO_HEX                │
        │  1. Initialize result = 0              │
        │  2. Parse decimal string               │
        │  3. Display hex result                 │
        └────────────┬───────────────────────────┘
                     │
                     │ JSR PARSE_DECIMAL_VALUE
                     ▼
        ┌────────────────────────────────────────┐
        │     PARSE_DECIMAL_VALUE                │
        │  Parse "1024" → $0400                  │
        └────────────┬───────────────────────────┘
                     │
                     │ Result in MON_CURRADDR
                     │ JSR PRINT_CURRENT_ADDRESS
                     ▼
        ┌────────────────────────────────────────┐
        │     Display "0400"                     │
        └────────────┬───────────────────────────┘
                     │
                     ▼
              Return to prompt
```

---

## 2. PARSE_DECIMAL_VALUE Algorithm

```
┌──────────────────────────────────────────────────────────────┐
│ PARSE_DECIMAL_VALUE                                          │
│ Input: MON_CMDBUF = "D:1024"                                 │
│        MON_PARSE_PTR = 2 (position after "D:")               │
│ Output: MON_CURRADDR = $0400                                 │
└────────────────────────────┬─────────────────────────────────┘
                             │
                             │ Initialize
                             ▼
                ┌────────────────────────┐
                │ result = 0             │
                │ digit_count = 0        │
                │ position = 2           │
                └────────┬───────────────┘
                         │
         ┌───────────────┴───────────────┐
         │  PARSE LOOP                   │
         │  For each character:          │
         └───────────────┬───────────────┘
                         │
                         ▼
           ┌─────────────────────────┐
           │ Load char at position   │
           │ char = buffer[position] │
           └──────────┬──────────────┘
                      │
                      ▼
           ┌──────────────────────┐
           │ End of buffer?       │
           │ position >= length   │
           └──────┬─────────┬─────┘
                  │         │
             Yes  │         │ No
                  │         │
                  │         ▼
                  │    ┌──────────────────┐
                  │    │ Is digit 0-9?    │
                  │    │ '0' <= char <= '9'│
                  │    └────┬──────────┬───┘
                  │         │          │
                  │    Yes  │          │ No
                  │         │          │
                  │         ▼          ▼
                  │    ┌────────┐  ┌─────────────┐
                  │    │Process │  │ digit_count │
                  │    │ digit  │  │    > 0?     │
                  │    └───┬────┘  └──────┬──────┘
                  │        │              │
                  │        │         Yes  │  No
                  │        │              │
                  │        │              ▼
                  │        │         DONE   VALUE_ERROR
                  │        │
                  │        ▼
                  │   ┌─────────────────────────┐
                  │   │ Convert char to digit   │
                  │   │ digit = char - '0'      │
                  │   │ (gives 0-9)             │
                  │   └──────────┬──────────────┘
                  │              │
                  │              │ Save digit on stack (PHA)
                  │              ▼
                  │   ┌─────────────────────────┐
                  │   │  MULTIPLY_BY_10         │
                  │   │  result = result × 10   │
                  │   └──────────┬──────────────┘
                  │              │
                  │              ▼
                  │   ┌─────────────────────────┐
                  │   │  Overflow?              │
                  │   │  (carry set)            │
                  │   └──────┬──────────┬───────┘
                  │          │          │
                  │     No   │          │ Yes
                  │          │          │
                  │          │          ▼
                  │          │     RANGE_ERROR
                  │          │     (pop stack first)
                  │          │
                  │          ▼
                  │   ┌─────────────────────────┐
                  │   │ Add digit to result     │
                  │   │ result = result + digit │
                  │   │ (pop digit from stack)  │
                  │   └──────────┬──────────────┘
                  │              │
                  │              ▼
                  │   ┌─────────────────────────┐
                  │   │  Overflow?              │
                  │   │  (high byte = 0)        │
                  │   └──────┬──────────┬───────┘
                  │          │          │
                  │     No   │          │ Yes
                  │          │          │
                  │          │          ▼
                  │          │     RANGE_ERROR
                  │          │
                  │          ▼
                  │   ┌─────────────────────────┐
                  │   │ position++              │
                  │   │ digit_count++           │
                  │   └──────────┬──────────────┘
                  │              │
                  │              └──────┐
                  │                     │
                  ▼                     │
           ┌────────────┐               │
           │    DONE    │◄──────────────┘
           │ Success    │
           └────────────┘
```

**Example Trace: "1024" → $0400**
```
Step 1: char='1', digit=1
        result = 0 × 10 + 1 = 1

Step 2: char='0', digit=0
        result = 1 × 10 + 0 = 10 ($000A)

Step 3: char='2', digit=2
        result = 10 × 10 + 2 = 102 ($0066)

Step 4: char='4', digit=4
        result = 102 × 10 + 4 = 1024 ($0400)
```

---

## 3. MULTIPLY_BY_10 Algorithm

```
┌─────────────────────────────────────────────────────────────┐
│ MULTIPLY_BY_10                                              │
│ Input: MON_CURRADDR = 16-bit value                          │
│ Output: MON_CURRADDR = value × 10                           │
│ Formula: value × 10 = (value × 8) + (value × 2)            │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
              ┌──────────────────────────┐
              │ Save original value      │
              │ DEC_TEMP = MON_CURRADDR  │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ Step 1: Multiply by 2    │
              │ Shift left once          │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ ASL MON_CURRADDR_LO      │
              │ ROL MON_CURRADDR_HI      │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ Check overflow           │
              │ BCS OVERFLOW             │
              └──────┬───────────────────┘
                     │
                No   │
                     ▼
              ┌──────────────────────────┐
              │ Save × 2 on stack        │
              │ PHA (high byte)          │
              │ PHA (low byte)           │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ Step 2: Multiply by 4    │
              │ Shift left again         │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ ASL MON_CURRADDR_LO      │
              │ ROL MON_CURRADDR_HI      │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ Check overflow           │
              │ BCS OVERFLOW_CLEAN       │
              └──────┬───────────────────┘
                     │
                No   │
                     ▼
              ┌──────────────────────────┐
              │ Step 3: Multiply by 8    │
              │ Shift left again         │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ ASL MON_CURRADDR_LO      │
              │ ROL MON_CURRADDR_HI      │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ Check overflow           │
              │ BCS OVERFLOW_CLEAN       │
              └──────┬───────────────────┘
                     │
                No   │
                     ▼
              ┌──────────────────────────┐
              │ Step 4: Add (×8) + (×2)  │
              │ Pop × 2 from stack       │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ PLA → DEC_TEMP_LO        │
              │ PLA → DEC_TEMP_HI        │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ Add × 2 to × 8           │
              │ CLC                      │
              │ ADC DEC_TEMP_LO          │
              │ STA MON_CURRADDR_LO      │
              │ LDA MON_CURRADDR_HI      │
              │ ADC DEC_TEMP_HI          │
              │ STA MON_CURRADDR_HI      │
              └──────────┬───────────────┘
                         │
                         ▼
              ┌──────────────────────────┐
              │ Check overflow           │
              │ BCS OVERFLOW             │
              └──────┬───────────────────┘
                     │
                No   │
                     ▼
              ┌──────────────────────────┐
              │ SUCCESS                  │
              │ CLC (carry clear)        │
              │ RTS                      │
              └──────────────────────────┘
```

**Example Trace: 102 × 10 = 1020**
```
Input: 102 = $0066

Step 1: × 2
        $0066 << 1 = $00CC (204)
        Save on stack: $00CC

Step 2: × 4
        $00CC << 1 = $0198 (408)

Step 3: × 8
        $0198 << 1 = $0330 (816)

Step 4: (× 8) + (× 2)
        $0330 + $00CC = $03FC (1020)

Result: 1020 = $03FC
```

---

## 4. Error Handling Flow

```
                    ┌──────────────┐
                    │   D:1024     │
                    └──────┬───────┘
                           │
          ┌────────────────┼────────────────┐
          │                │                │
          ▼                ▼                ▼
    ┌─────────┐     ┌──────────┐     ┌─────────┐
    │ Missing │     │ Invalid  │     │  Out of │
    │  colon  │     │  digit   │     │  range  │
    │  "D1024"│     │ "D:12A4" │     │"D:99999"│
    └────┬────┘     └────┬─────┘     └────┬────┘
         │               │                 │
         │               │                 │
         ▼               ▼                 ▼
    ┌─────────┐     ┌──────────┐     ┌─────────┐
    │ SYNTAX? │     │  VALUE?  │     │ RANGE?  │
    └────┬────┘     └────┬─────┘     └────┬────┘
         │               │                 │
         └───────────────┼─────────────────┘
                         │
                         ▼
              ┌──────────────────┐
              │ Return to prompt │
              └──────────────────┘
```

**Error Detection Points:**

1. **SYNTAX? Error:**
   - No colon after D
   - Empty argument (D:)
   - Detected by: PARSE_COLON_COMMAND or digit_count check

2. **VALUE? Error:**
   - Non-digit character: D:12A4
   - All non-digits: D:ABC
   - Detected by: PARSE_DECIMAL_VALUE character check

3. **RANGE? Error:**
   - Value > 65535: D:65536
   - Overflow during multiply: D:99999
   - Detected by: MULTIPLY_BY_10 overflow checks

---

## 5. State Transitions

```
┌─────────────────────────────────────────────────────────────┐
│                      MONITOR STATE                           │
│                                                              │
│  ┌─────────────┐                                            │
│  │  Command    │                                            │
│  │  Prompt (>) │                                            │
│  └──────┬──────┘                                            │
│         │                                                    │
│         │ User types: D:1024<ENTER>                         │
│         │                                                    │
│         ▼                                                    │
│  ┌──────────────┐                                           │
│  │  Parsing     │                                           │
│  │  D: command  │                                           │
│  └──────┬───────┘                                           │
│         │                                                    │
│         ▼                                                    │
│  ┌──────────────┐                                           │
│  │  Converting  │  ← result = 0                            │
│  │  1024 → $0400│  ← result = 1                            │
│  └──────┬───────┘  ← result = 10                           │
│         │          ← result = 102                           │
│         │          ← result = 1024                          │
│         ▼                                                    │
│  ┌──────────────┐                                           │
│  │  Displaying  │  Display: "0400"                         │
│  │  0400        │                                           │
│  └──────┬───────┘                                           │
│         │                                                    │
│         │ Print newline                                     │
│         │                                                    │
│         ▼                                                    │
│  ┌──────────────┐                                           │
│  │  Command     │  Ready for next command                  │
│  │  Prompt (>) │                                           │
│  └──────────────┘                                           │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

**Memory State During Execution:**

```
Before D:1024:
  MON_CURRADDR:    undefined
  DEC_TEMP:        undefined
  DEC_DIGIT_IDX:   undefined

After initialization:
  MON_CURRADDR:    $0000
  DEC_TEMP:        $0000
  DEC_DIGIT_IDX:   $00

After parsing '1':
  MON_CURRADDR:    $0001
  DEC_DIGIT_IDX:   $01

After parsing '0':
  MON_CURRADDR:    $000A
  DEC_DIGIT_IDX:   $02

After parsing '2':
  MON_CURRADDR:    $0066
  DEC_DIGIT_IDX:   $03

After parsing '4':
  MON_CURRADDR:    $0400
  DEC_DIGIT_IDX:   $04

After display:
  MON_CURRADDR:    $0400 (preserved)
  Output:          "0400"
```

---

## 6. Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        DATA FLOW                             │
│                                                              │
│  User Input          Memory              Output             │
│  ───────────         ──────              ──────             │
│                                                              │
│  "D:1024"                                                    │
│     │                                                        │
│     │                                                        │
│     ▼                                                        │
│  ┌────────────┐                                             │
│  │ MON_CMDBUF │  Command buffer                             │
│  │ $0200-$024F│  "D:1024\0..."                             │
│  └─────┬──────┘                                             │
│        │                                                     │
│        │  Parse position 2 (after "D:")                     │
│        ▼                                                     │
│  ┌────────────┐                                             │
│  │ MON_PARSE  │  Position: 2 → 3 → 4 → 5 → 6              │
│  │    _PTR    │                                             │
│  │   $0270    │                                             │
│  └─────┬──────┘                                             │
│        │                                                     │
│        │  Extract each digit                                │
│        ▼                                                     │
│  ┌────────────┐                                             │
│  │ A register │  '1' → '0' → '2' → '4'                     │
│  │ (digit)    │  1  →  0  →  2  →  4                       │
│  └─────┬──────┘                                             │
│        │                                                     │
│        │  Multiply and add                                  │
│        ▼                                                     │
│  ┌────────────┐                                             │
│  │MON_CURRADDR│  0 → 1 → 10 → 102 → 1024                   │
│  │  $14/$15   │  $0000 → $0001 → $000A → $0066 → $0400    │
│  └─────┬──────┘                                             │
│        │                                                     │
│        │  Convert to hex string                             │
│        ▼                                                     │
│  ┌────────────┐                                             │
│  │   Display  │  "0400"                                     │
│  │   buffer   │                                             │
│  └─────┬──────┘                                             │
│        │                                                     │
│        │  Character by character                            │
│        ▼                                                     │
│  ┌────────────┐                                             │
│  │   Screen   │  '0' → '4' → '0' → '0'                     │
│  │   $0400+   │                                             │
│  └────────────┘                                             │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 7. Optimization Decision Tree

```
                 ┌───────────────────┐
                 │  Need to optimize?│
                 └─────────┬─────────┘
                           │
            ┌──────────────┼──────────────┐
            │              │              │
      Yes   │              │ No           │ Unknown
            │              │              │
            ▼              ▼              ▼
    ┌───────────┐  ┌──────────────┐  ┌────────────┐
    │ Which     │  │ Current code │  │ Measure    │
    │ metric?   │  │ is adequate  │  │ and decide │
    └─────┬─────┘  └──────────────┘  └────────────┘
          │
    ┌─────┴─────┐
    │           │
    ▼           ▼
┌────────┐  ┌────────┐
│  ROM   │  │ Speed  │
│  size  │  │        │
└───┬────┘  └───┬────┘
    │           │
    │           ▼
    │      ┌──────────────────────┐
    │      │ Unroll multiply loop │
    │      │ Lookup table digits  │
    │      │ +30 bytes, -40% time │
    │      └──────────────────────┘
    │
    ▼
┌──────────────────────────────┐
│ Option 1: Remove space skip  │
│           -10 bytes          │
├──────────────────────────────┤
│ Option 2: Simplify errors    │
│           -15 bytes          │
├──────────────────────────────┤
│ Option 3: Inline multiply    │
│           -8 bytes           │
└──────────────────────────────┘
```

**Current Status: No optimization needed**
- ROM: 197/200 bytes (within budget)
- Speed: 1.3ms << 150ms (within budget)

---

## 8. Testing Decision Tree

```
                    ┌─────────────┐
                    │  Test what? │
                    └──────┬──────┘
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
        ▼                  ▼                  ▼
┌───────────────┐  ┌───────────────┐  ┌───────────────┐
│  Algorithm    │  │  Integration  │  │  Regression   │
│  correctness  │  │  with system  │  │  other cmds   │
└───────┬───────┘  └───────┬───────┘  └───────┬───────┘
        │                  │                  │
        ▼                  ▼                  ▼
  ┌──────────┐      ┌──────────┐      ┌──────────┐
  │ Boundary │      │ Dispatch │      │ R: works │
  │ values   │      │ routing  │      │ W: works │
  │ 0,65535  │      │ correct  │      │ G: works │
  └────┬─────┘      └────┬─────┘      └────┬─────┘
       │                 │                 │
       ▼                 ▼                 ▼
  ┌──────────┐      ┌──────────┐      ┌──────────┐
  │ Powers   │      │ Error    │      │ All pass │
  │ of 2,10  │      │ messages │      │ = success│
  └────┬─────┘      └────┬─────┘      └──────────┘
       │                 │
       ▼                 ▼
  ┌──────────┐      ┌──────────┐
  │ Error    │      │ Display  │
  │ cases    │      │ format   │
  └────┬─────┘      └────┬─────┘
       │                 │
       └────────┬────────┘
                │
                ▼
          ┌──────────┐
          │ All pass │
          │ = success│
          └──────────┘
```

---

## 9. Debugging Guide

**If result is wrong:**

```
Check: Input parsing
│
├─ Is each character converted correctly?
│  └─ Add debug output after "Convert char to digit"
│
├─ Is multiply-by-10 working?
│  └─ Test with known values: 1×10=10, 10×10=100
│
└─ Is addition working?
   └─ Check carry flag handling
```

**If overflow not detected:**

```
Check: Overflow flags
│
├─ After each shift (ASL/ROL)
│  └─ BCS OVERFLOW immediately after
│
├─ After final addition
│  └─ Check for wrap to $0000
│
└─ Test with boundary: 6553×10, 6554×10
```

**If wrong error message:**

```
Check: Error routing
│
├─ VALUE? should show for non-digits
│  └─ Character range check: < '0' or > '9'
│
├─ RANGE? should show for overflow
│  └─ Carry flag set in multiply
│
└─ SYNTAX? should show for empty
   └─ digit_count == 0 check
```

---

## Quick Reference: Key Constants

```assembly
; Character codes
'0' = $30 (ASCII 48)
'9' = $39 (ASCII 57)
'A' = $41 (ASCII 65)

; Memory addresses
MON_CURRADDR_LO = $14
MON_CURRADDR_HI = $15
DEC_TEMP_LO     = $35
DEC_TEMP_HI     = $36
DEC_DIGIT_IDX   = $37

; Limits
MAX_DECIMAL = 65535 ($FFFF)
MIN_DECIMAL = 0 ($0000)
MAX_MULTIPLY_INPUT = 6553 (6553×10 = 65530)
```

---

**Document Version:** 1.0
**Created:** 2025-10-11
**Purpose:** Visual implementation guide
**Use:** Reference during coding and debugging
