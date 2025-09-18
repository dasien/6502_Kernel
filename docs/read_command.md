# R: Read Memory Command

## Purpose
Display memory contents at a specified address or address range.

## Activation
- Command letter: `R`  
- Format: `R:address` or `R:start-end`

## Input Requirements

### Single Address
- `address`: 4-digit hexadecimal address (0000-FFFF)
- Displays single byte at specified address

### Address Range
- `start`: 4-digit hexadecimal start address (0000-FFFF)
- `end`: 4-digit hexadecimal end address (0000-FFFF)
- End address must be greater than or equal to start address
- Addresses separated by dash (`-`)

## Output Format

### Single Address
```
XXXX: YY
```

### Address Range
```
XXXX: YY YY YY YY YY YY YY YY
XXXX: YY YY YY YY YY YY YY YY
[--MORE-- (ENTER/ESC)]
```

Where:
- `XXXX` = address in hex
- `YY` = byte value in hex
- 8 bytes displayed per line
- Automatic paging for ranges > 24 lines

## Examples
- `R:8000` - Read single byte at $8000
- `R:8000-800F` - Read 16 bytes from $8000-$800F  
- `R:0400-07FF` - Read entire screen memory (1KB)
- `R:FF00-FFFF` - Read jump table and vectors

## Paging Controls
- ENTER - Continue to next page
- ESC - Abort display and return to command mode

## Universal Command
- R: commands work from any monitor mode (command, write)
- Does not change current monitor mode
- Always returns to previous mode after display

## Error Messages
- `?ERROR` - Invalid syntax or hex address
- `?RANGE` - End address less than start address

## Notes
- Always displays in 8-byte-per-line format for ranges
- Includes both start and end addresses in range display
- No modification of memory - read-only operation
- Addresses wrap at 64KB boundary ($FFFF + 1 = $0000)