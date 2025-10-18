# D: Decimal to Hex Command

## Purpose
Convert a decimal number (0-65535) to its hexadecimal representation.

## Activation
- Command letter: `D`
- Format: `D:nnnnn`

## Input Requirements

### Decimal Value
- Decimal digits only (0-9)
- Range: 0 to 65535 (16-bit unsigned integer)
- Leading zeros optional
- Maximum 5 digits

## Examples
- `D:0` - Displays `$0000`
- `D:255` - Displays `$00FF`
- `D:1234` - Displays `$04D2`
- `D:65535` - Displays `$FFFF`

## Output Format
Hexadecimal value with `$` prefix

Format: `$XXXX` (4 hex digits with leading zeros)

Example output:
```
>D:1234
$04D2
```

## Success Message
Result displayed on next line

## Error Messages
- `?VALUE` - Invalid digit (non-numeric character entered)
- `?RANGE` - Number exceeds 65535

## Command Behavior
- **Read-only operation**: Does not modify memory or system state
- **Preserves MON_CURR_ADDR**: Current address pointer unchanged
- **Immediate execution**: Result displayed immediately after ENTER

## Algorithm
1. Parse decimal input character by character
2. Validate each character is 0-9
3. Build 16-bit binary value: `value = value × 10 + digit`
4. Check for overflow (value > 65535) after each digit
5. Convert final binary value to hexadecimal string
6. Display with '$' prefix

## Use Cases
- Convert decimal addresses to hex for memory operations
- Calculate hex values for programming constants
- Verify address calculations
- Educational demonstrations of number systems

## Notes
- Input validation occurs during parsing (not after)
- Early exit on first invalid character
- Range checking prevents 16-bit overflow
- Complements H: (hex to decimal) command

## Related Commands
- `H:xxxx` - Hex to Decimal conversion (reverse operation)

## Memory Usage
Uses temporary variables at $027F-$0284:
- $027F-$0280: Conversion temporary (16-bit)
- $0281: Decimal digit counter
- $0282-$0283: Decimal accumulator (16-bit)
- $0284: Conversion flags

## Implementation References
- Command index: 14 in CMD_INDEX_MAP
- Jump table: CMD_JUMP_COMPACT_LO/HI
- Parser routine: PARSE_CMD_DECIMAL_CHECK
- Execution routine: CMD_DECIMAL_TO_HEX
- Decimal parser: PARSE_DECIMAL_VALUE