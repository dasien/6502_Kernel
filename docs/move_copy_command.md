# M: Move/Copy Memory Command

## Purpose
Copy a block of memory from one location to another.

## Activation
- Command letter: `M`
- Format: `M:start-end,dest,bit`

## Input Requirements

### Source Range
- `start`: 4-digit hexadecimal start address (0000-FFFF)
- `end`: 4-digit hexadecimal end address (0000-FFFF)
- End address must be greater than or equal to start address

### Destination
- `dest`: 4-digit hexadecimal destination start address (0000-FFFF)
- Must be preceded by comma (`,`)

### Copy/Move Mode
- `bit`: Copy/move mode selector (single digit)
- Must be preceded by comma (`,`)
- `0` = Copy mode (source memory remains intact)
- `1` = Move mode (source memory is cleared/zeroed after copy)

## Examples
- `M:8000-8FFF,9000,0` - Copy $8000-$8FFF to $9000-$9FFF (source preserved)
- `M:8000-8FFF,9000,1` - Move $8000-$8FFF to $9000-$9FFF (source cleared)
- `M:E000-EFFF,2000,0` - Copy ROM to RAM (source preserved)
- `M:1000-10FF,1001,0` - Copy memory up by one byte
- `M:2000-2FFF,1000,1` - Move 4K block down by 4K, clear original

## Output Format
No output during operation

## Success Messages
- For copy mode (bit=0): `COPIED XXXX BYTES`
- For move mode (bit=1): `MOVED XXXX BYTES`

Where XXXX is the hexadecimal count of bytes processed

## Error Messages
- `?ERROR` - Invalid syntax or parameters
- `?RANGE` - Invalid address range
- `?OVERLAP` - Source and destination ranges overlap destructively
- `?VALUE` - Invalid hex characters or copy/move bit (must be 0 or 1)

## Special Behavior
- Handles overlapping operations correctly:
    - If dest > source: copies from end to beginning
    - If dest < source: copies from beginning to end
- Includes both start and end addresses in operation
- Move operation sequence:
    1. Perform copy operation (same as copy mode)
    2. Clear source range (fill with $00)
- Overlap detection applies to both copy and move modes
- Range validation remains identical for both modes