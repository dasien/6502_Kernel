# S: Save Memory Command

## Purpose
Save a range of memory to a binary file on the host.

## Activation
- Command letter: `S`
- Format: `S:start-end`

## Input Requirements

### Memory Range
- `start`: 4-digit hexadecimal start address (0000-FFFF)
- `end`: 4-digit hexadecimal end address (0000-FFFF)
- End address must be greater than or equal to start address
- Addresses separated by dash (`-`)

### File selection
- No filename is given on the command line. When the save is issued the host
  presents a file-save dialog and the user chooses the destination there.
- The host owns the filesystem path, so naming the file in the monitor would be
  meaningless - the kernel cannot choose where on the host the file is written.

## File I/O Interface
The save operation uses memory-mapped file I/O (PIA page at `$FE00`):
- `FILE_COMMAND` ($FE10) - Set to `FILE_SAVE_CMD` ($02)
- `FILE_ADDR` ($FE12-$FE13) - Start address
- `FILE_END_ADDR` ($FE20-$FE21) - End address
- `FILE_STATUS` ($FE11) - Operation status

## Examples
- `S:8000-8FFF` - Save a 4KB range to a host-selected file
- `S:0400-07FF` - Save screen memory to a host-selected file
- `S:1000-1000` - Save a single byte
- `S:2000-2FFF` - Save a 4KB data block

## Operation Sequence
1. Parse command syntax and extract the range
2. Validate address range (end >= start)
3. Set up file I/O registers
4. Initiate save operation (host shows the file-save dialog)
5. Wait for completion
6. Display success or error message

## Success Message
`OK`

## Error Messages
- `ERROR?` - Invalid syntax/address, or the save failed (file error, or the
  user cancelled the host dialog)
- `RANGE?` - End address less than start address
- `VALUE?` - Invalid hexadecimal address

## Status Monitoring
The command monitors the `FILE_STATUS` register:
- in-progress ($01) - save running
- success ($02) - save completed successfully
- any other value - save failed

## Notes
- One-shot command - returns to command mode after completion
- Includes both start and end addresses in the saved range
- File size = (end - start + 1) bytes
- Creates a new file or overwrites the host-selected file
- No progress indication during save
- Memory contents are not modified during save
