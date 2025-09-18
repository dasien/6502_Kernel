# W: Write Memory Command

## Purpose
Interactively write hex bytes to memory starting at a specified address.

## Activation
- Command letter: `W`
- Format: `W:address`

## Input Requirements

### Start Address
- `address`: 4-digit hexadecimal address (0000-FFFF)
- No range required - single starting address only

## Interactive Mode
After issuing the command, the monitor enters write mode:
1. Displays current address and existing byte value
2. Waits for hex byte input or control keys
3. Advances to next address after each write
4. Continues until ESC is pressed

## Input Format in Write Mode
- Enter 2-digit hex bytes (00-FF)
- Press ENTER to confirm and advance to next address
- Press ESC to exit write mode
- Press `.` (dot) to recall last command

## Display Format
```
XXXX: YY _
```
Where:
- `XXXX` = current address in hex
- `YY` = existing byte value at address
- `_` = cursor waiting for input

## Examples
- `W:8000` - Start writing at $8000
- Input: `A9` + ENTER - Write $A9 to current address, advance
- Input: `20` + ENTER - Write $20 to next address, advance
- Input: ESC - Exit write mode

## Success Behavior
- Each successful write advances to the next address
- No explicit success message - advancing indicates success
- Returns to command mode when ESC pressed

## Error Messages
- `?ERROR` - Invalid syntax or hex address
- `?VALUE` - Invalid hex byte entered during write mode

## Notes
- Write mode preserves command in history for dot recall
- Can write to any address in memory map
- No range checking - single bytes only
- Displays old value before overwriting