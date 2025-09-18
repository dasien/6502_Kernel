# F: Fill Memory Command

## Purpose
Fill a range of memory addresses with a specified byte value.

## Activation
- Command letter: `F`
- Format: `F:start-end,value`

## Input Requirements

### Address Range
- `start`: 4-digit hexadecimal start address (0000-FFFF)
- `end`: 4-digit hexadecimal end address (0000-FFFF)
- End address must be greater than or equal to start address
- Addresses separated by dash (`-`)

### Fill Value
- Single byte value in hexadecimal (00-FF)
- Must be preceded by comma (`,`)
- Leading zeros required (e.g., `F` is not valid, but `0F` is valid)

## Examples
- `F:8000-8FFF,00` - Fill $8000-$8FFF with $00
- `F:0400-07FF,20` - Fill screen memory with spaces
- `F:1000-1000,FF` - Fill single byte at $1000 with $FF

## Output Format
No output during operation (for performance)

## Success Message
FILLED

## Error Messages
- `?ERROR` - Invalid syntax or parameters
- `?RANGE` - End address less than start address
- `?PROTECTED` - Attempt to fill ROM or I/O space (optional)

## Notes
- Fill operation includes both start and end addresses
- Large fills may take noticeable time
- No progress indication during fill