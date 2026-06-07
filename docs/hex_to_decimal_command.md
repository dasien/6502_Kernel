# H: Hex to Decimal Command

## Purpose
Convert a hexadecimal value (0000-FFFF) to its decimal representation.

## Activation
- Command letter: `H`
- Format: `H:xxxx`

## Input Requirements

### Hexadecimal Value
- Hexadecimal digits only (0-9, A-F, a-f)
- Range: 0000 to FFFF (16-bit value)
- Case-insensitive (A-F or a-f both valid)
- Exactly 4 hex digits required

## Examples
- `H:0000` - Displays `#0`
- `H:00FF` - Displays `#255`
- `H:04D2` - Displays `#1234`
- `H:FFFF` - Displays `#65535`

## Output Format
Decimal value with `#` prefix

Format: `#nnnnn` (up to 5 decimal digits, no leading zeros except for value 0)

Example output:
```
>H:04D2
#1234
```

## Success Message
Result displayed on next line

## Error Messages
- `VALUE?` - Invalid hex digit (non-hexadecimal character entered)

## Command Behavior
- **Read-only operation**: Does not modify memory or system state
- **Preserves MON_CURR_ADDR**: Current address pointer unchanged
- **No range errors**: All 4-digit hex values are valid (0000-FFFF)
- **Immediate execution**: Result displayed immediately after ENTER

## Algorithm
1. Parse hex input digit by digit
2. Validate each character is 0-9, A-F (case-insensitive)
3. Build 16-bit binary value: `value = (value << 4) | digit_value`
4. Convert final binary value to decimal string
5. Display with '#' prefix

## Use Cases
- Convert hex addresses to decimal for calculations
- Calculate memory sizes in decimal
- Verify hex-to-decimal conversions
- Educational demonstrations of number systems

## Notes
- Input validation occurs during parsing
- Leading zeros required (H:FF invalid, must use H:00FF)
- Handles zero value correctly (H:0000 → #0)
- Complements D: (decimal to hex) command

## Command History
- Originally mapped to help function
- Remapped to hex-to-decimal conversion
- Help function moved to `?` command

## Related Commands
- `D:nnnnn` - Decimal to Hex conversion (reverse operation)
- `?` - Help command (was previously H:)

## Memory Usage
Uses temporary variables at $027F-$0284:
- $027F-$0280: Conversion temporary (16-bit)
- Saves/restores MON_CURR_ADDR to preserve address pointer

## Implementation References
- Command index: 15 in CMD_INDEX_MAP
- Jump table: CMD_JUMP_COMPACT_LO/HI
- Parser routine: PARSE_CMD_HEX_TO_DEC
- Execution routine: CMD_HEX_TO_DECIMAL
- Hex parser: HEX_QUAD_TO_ADDR (with address save/restore)

## Design Decisions
- Uses `#` prefix to distinguish from hex output (`$` prefix)
- Does not modify MON_CURR_ADDR (unlike most address commands)
- Saves and restores address to prevent side effects
- Read-only nature makes it safe to use repeatedly