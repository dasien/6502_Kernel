# L: Load File Command

## Purpose
Load a binary file from the host into memory at a specified address.

## Activation
- Command letter: `L`
- Format: `L:address`

## Input Requirements

### Load Address
- `address`: 4-digit hexadecimal address (0000-FFFF)
- Specifies where in memory the file will be loaded
- Must be preceded by `L:`

### File selection
- No filename is given on the command line. When the load is issued the host
  presents a file-open dialog and the user picks the file there.
- The host owns the filesystem path, so naming the file in the monitor would be
  meaningless - the kernel cannot know where on the host the file lives.

## File I/O Interface
The load operation uses memory-mapped file I/O (PIA page at `$FE00`):
- `FILE_COMMAND` ($FE10) - Set to `FILE_LOAD_CMD` ($01)
- `FILE_ADDR` ($FE12-$FE13) - Target load address
- `FILE_STATUS` ($FE11) - Operation status

## Examples
- `L:8000` - Load a host-selected file to $8000
- `L:0400` - Load a host-selected file into screen memory
- `L:1000` - Load a host-selected file to $1000

## Operation Sequence
1. Parse command syntax and extract the load address
2. Set up file I/O registers
3. Initiate load operation (host shows the file-open dialog)
4. Wait for completion
5. Display success or error message

## Success Message
`OK`

## Error Messages
- `ERROR?` - Invalid syntax/address, or the load failed (file error, or the
  user cancelled the host dialog)
- `VALUE?` - Invalid hexadecimal address

## Status Monitoring
The command monitors the `FILE_STATUS` register:
- in-progress ($01) - load running
- success ($02) - load completed successfully
- any other value - load failed

## Notes
- One-shot command - returns to command mode after completion
- File size is determined by the host file
- Load address must have sufficient free memory
- No progress indication during load
- Overwrites existing memory content at the target address
