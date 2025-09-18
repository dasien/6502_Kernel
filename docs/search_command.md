# X: Search Memory Command

## Purpose
Search memory for a sequence of bytes.

## Activation
- Command letter: `X`
- Format: `X:start-end,b1 b2 b3...`

## Input Requirements

### Search Range
- `start`: 4-digit hexadecimal start address (0000-FFFF)
- `end`: 4-digit hexadecimal end address (0000-FFFF)
- End address must be greater than or equal to start address

### Search Pattern
- Comma-separated list of hex bytes after range
- Each byte: 2-digit hexadecimal (00-FF)
- Bytes separated by spaces
- Maximum 16 bytes in pattern

## Examples
- `X:8000-FFFF,A9 00` - Find LDA #$00 instruction
- `X:0400-7FFF,48 45 4C 4C 4F` - Find "HELLO" in memory
- `X:0000-FFFF,EA` - Find all NOP instructions

## Output Format
FOUND AT:
XXXX: b1 b2 b3 b4 b5 b6 b7 b8
XXXX: b1 b2 b3 b4 b5 b6 b7 b8
[ESC TO ABORT, ENTER FOR MORE]

- Shows 8 bytes starting at each match
- Pauses every 24 matches

## Success Message
SEARCH COMPLETE - XXXX MATCHES

Where XXXX is hexadecimal count

## Error Messages
- `?ERROR` - Invalid syntax or parameters
- `?RANGE` - Invalid address range
- `?PATTERN` - Invalid or empty search pattern

## Notes
- Search is case-sensitive for byte values
- ESC key aborts search
- Maximum 256 matches displayed