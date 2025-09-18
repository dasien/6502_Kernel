# L: Load File Command

## Purpose
Load a binary file from storage into memory at a specified address.

## Activation
- Command letter: `L`
- Format: `L:address,filename`

## Input Requirements

### Load Address
- `address`: 4-digit hexadecimal address (0000-FFFF)
- Specifies where in memory the file will be loaded
- Must be preceded by `L:`

### Filename
- Must be preceded by comma (`,`)
- Filename specification depends on host system
- No spaces allowed in filename
- Case-sensitive on most systems

## File I/O Interface
The load operation uses memory-mapped file I/O:
- `FILE_COMMAND` ($DC10) - Set to `FILE_LOAD_CMD` ($01)
- `FILE_ADDR` ($DC12-$DC13) - Target load address
- `FILE_NAME_BUF` ($DC14-$DC1F) - Filename buffer
- `FILE_STATUS` ($DC11) - Operation status

## Examples
- `L:8000,PROGRAM.BIN` - Load PROGRAM.BIN to $8000
- `L:0400,SCREEN.DAT` - Load screen data to screen memory
- `L:1000,CODE` - Load file named CODE to $1000

## Operation Sequence
1. Parse command syntax and extract address/filename
2. Set up file I/O registers
3. Initiate load operation
4. Wait for completion
5. Display success or error message

## Success Message
`LOADED`

## Error Messages
- `?ERROR` - Invalid syntax, missing comma, or invalid address
- `?FILE` - File not found or read error
- `?VALUE` - Invalid hexadecimal address

## Status Monitoring
The command monitors `FILE_STATUS` register:
- `FILE_IDLE` ($00) - No operation
- `FILE_IN_PROGRESS` ($01) - Load in progress  
- `FILE_SUCCESS` ($02) - Load completed successfully
- `FILE_ERROR` ($FF) - Load failed

## Notes
- One-shot command - returns to command mode after completion
- File size is determined by the host file system
- Load address must have sufficient free memory
- No progress indication during load
- Overwrites existing memory content at target address