# S: Save Memory Command

## Purpose
Save a range of memory to a binary file.

## Activation
- Command letter: `S`
- Format: `S:start-end,filename`

## Input Requirements

### Memory Range
- `start`: 4-digit hexadecimal start address (0000-FFFF)
- `end`: 4-digit hexadecimal end address (0000-FFFF)
- End address must be greater than or equal to start address
- Addresses separated by dash (`-`)

### Filename
- Must be preceded by comma (`,`)
- Filename specification depends on host system
- No spaces allowed in filename
- Case-sensitive on most systems

## File I/O Interface
The save operation uses memory-mapped file I/O:
- `FILE_COMMAND` ($DC10) - Set to `FILE_SAVE_CMD` ($02)
- `FILE_ADDR` ($DC12-$DC13) - Start address
- `FILE_END_ADDR` ($DC20-$DC21) - End address  
- `FILE_NAME_BUF` ($DC14-$DC1F) - Filename buffer
- `FILE_STATUS` ($DC11) - Operation status

## Examples
- `S:8000-8FFF,PROGRAM.BIN` - Save 4KB program to file
- `S:0400-07FF,SCREEN.DAT` - Save screen memory to file
- `S:1000-1000,BYTE.DAT` - Save single byte to file
- `S:2000-2FFF,DATA` - Save 4KB data block

## Operation Sequence
1. Parse command syntax and extract range/filename
2. Validate address range (end >= start)
3. Set up file I/O registers
4. Initiate save operation
5. Wait for completion
6. Display success or error message

## Success Message
`SAVED`

## Error Messages
- `?ERROR` - Invalid syntax, missing comma, or parse error
- `?RANGE` - End address less than start address
- `?FILE` - File write error or permission denied
- `?VALUE` - Invalid hexadecimal address

## Status Monitoring
The command monitors `FILE_STATUS` register:
- `FILE_IDLE` ($00) - No operation
- `FILE_IN_PROGRESS` ($01) - Save in progress
- `FILE_SUCCESS` ($02) - Save completed successfully  
- `FILE_ERROR` ($FF) - Save failed

## Notes
- One-shot command - returns to command mode after completion
- Includes both start and end addresses in saved range
- File size = (end - start + 1) bytes
- Creates new file or overwrites existing file
- No progress indication during save
- Memory contents are not modified during save