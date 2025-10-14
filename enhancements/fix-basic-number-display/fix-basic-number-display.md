# Fix BASIC Number Display Bug

## Priority
**HIGH** - Affects basic usability of BASIC interpreter

## Problem Description

Line numbers and free memory byte counts are displaying incorrectly with unwanted decimal points and wrong digit positions:

### Symptoms
- **Numbers < 16**: Display as "0.909" instead of "10", "15", etc.
- **Numbers 16-296**: Display as "09.09" instead of "100", "256", etc.
- **Numbers > 296**: Display as "090.9" instead of "300", "1000", etc.

### Affected Features
1. **LIST command** - Line numbers display incorrectly
2. **Startup message** - Free bytes display incorrectly
3. **Any unsigned 16-bit integer display** - Uses same broken routine

## Technical Analysis

### Entry Points
Both issues call the same conversion routine:

1. **Free bytes display** (kernel.asm:639):
   ```assembly
   JSR   LAB_295E          ; print XA as unsigned integer (bytes free)
   ```

2. **LIST command** (kernel.asm:1406):
   ```assembly
   JSR   LAB_295E          ; print XA as unsigned integer
   ```

### Problem Routine: LAB_295E

Located at kernel.asm:6587-6596, this routine converts a 16-bit unsigned integer in XA to a decimal string:

```assembly
LAB_295E:
      STA   FAC1_1            ; save high byte as FAC1 mantissa1
      STX   FAC1_2            ; save low byte as FAC1 mantissa2
      LDX   #$90              ; set exponent to 16d bits
      SEC                     ; set integer is +ve flag
      JSR   LAB_STFA          ; set exp=X, clearFAC1 mantissa3 and normalise
      LDY   #$00              ; clear index
      TYA                     ; clear A
      JSR   LAB_297B          ; convert FAC1 to string, skip sign character save
      JMP   LAB_18C3          ; print null terminated string from memory and return
```

### Conversion Algorithm

The routine:
1. Converts 16-bit integer to floating point format (FAC1)
2. Normalizes the floating point number
3. Calls LAB_297B to convert to decimal string
4. Uses a digit extraction loop (LAB_29FB) with subtraction table

### Key Subroutines

**LAB_297B** (line 6610): Main conversion routine
- Handles sign character
- Calls normalization routines
- Calculates `numexp` (digits before decimal point)
- Calls digit extraction loop

**LAB_29C3-LAB_29D9** (lines 6667-6687): Calculate decimal point position
- Computes `numexp` (number of digits before decimal point)
- For unsigned integers, this should equal the total number of significant digits
- **BUG LIKELY HERE**: Incorrect calculation causing decimal point insertion

**LAB_29FB** (lines 6709-6762): Digit extraction loop
- Uses subtraction table at LAB_2A9A
- Extracts digits by repeated subtraction
- Inserts decimal point when `numexp` reaches 0 (line 6747)
- Table has powers: 100000, 10000, 1000, 100, 10, 1

### Subtraction Table (LAB_2A9A)
```assembly
LAB_2A9A:
      .byte $FE,$79,$60       ; -100000
      .byte $00,$27,$10       ; 10000
      .byte $FF,$FC,$18       ; -1000
      .byte $00,$00,$64       ; 100
      .byte $FF,$FF,$F6       ; -10
      .byte $00,$00,$01       ; 1
```

## Bug Hypothesis

The `numexp` calculation (lines 6671-6687) appears to be incorrectly computing the number of digits before the decimal point for small integers. This causes:

1. Decimal point inserted too early in the digit sequence
2. Leading zeros not being properly suppressed before decimal point
3. Digits appearing in wrong positions relative to decimal point

The algorithm seems designed for floating point numbers with scientific notation support, but fails for simple unsigned integers where NO decimal point should appear.

## Expected Behavior

For unsigned integers:
- **15** → "15" (no decimal point)
- **100** → "100" (no decimal point)
- **300** → "300" (no decimal point)
- **65535** → "65535" (no decimal point)

## Required Fix

1. Analyze the `numexp` calculation logic for integer inputs
2. Determine why decimal point is being inserted
3. Fix the calculation or add special handling for unsigned integers
4. Ensure no decimal point appears for integer values
5. Test with range of values: 0-65535

## Agent Assignment

**Assembly Developer** - Expert in 6502 assembly, floating point algorithms, and text conversion routines

## Test Cases

After fix:
```
TEST 1: Free bytes at startup (e.g., 38911 bytes)
EXPECT: "38911 Bytes free" (no decimal point)

TEST 2: LIST with line 10
EXPECT: "10 PRINT ..." (not "0.909 PRINT...")

TEST 3: LIST with line 100
EXPECT: "100 PRINT ..." (not "09.09 PRINT...")

TEST 4: LIST with line 1000
EXPECT: "1000 PRINT ..." (not "090.9 PRINT...")
```

## Files to Modify

- `src/kernel/basic.asm` - Fix LAB_295E conversion routine or related subroutines

## References

- Enhanced BASIC source code (EhBASIC derivative)
- 6502 floating point routines
- Decimal conversion algorithms