# 6502 Monitor Commands Reference

## Overview
The 6502 Monitor provides a comprehensive set of commands for memory manipulation, program execution, and system control. Commands are single letters followed by parameters.

## Command Categories

### Memory Operations
- **[R: Read Memory](read_command.md)** - Display memory contents at address or range
- **[W: Write Memory](write_command.md)** - Interactively write hex bytes to memory  
- **[F: Fill Memory](fill_command.md)** - Fill memory range with specified byte value
- **[M: Move/Copy Memory](move_copy_command.md)** - Copy or move memory blocks
- **[X: Search Memory](search_command.md)** - Search memory for byte patterns

### Program Operations  
- **[G: Go/Run](run_command.md)** - Execute user program at specified address
- **[L: Load File](load_command.md)** - Load binary file into memory
- **[S: Save File](save_command.md)** - Save memory range to binary file

### Display Commands

#### C: Clear Screen
- **Purpose**: Clear the display screen
- **Format**: `C:`
- **Example**: `C:`
- **Notes**: Clears entire screen, cursor returns to top-left

#### T: Print Stack  
- **Purpose**: Display stack memory contents ($0100-$01FF)
- **Format**: `T:`
- **Output**: Paged display of stack memory with ESC/ENTER controls
- **Notes**: Shows full stack page, useful for debugging

#### Z: Print Zero Page
- **Purpose**: Display zero page memory contents ($0000-$00FF) 
- **Format**: `Z:`
- **Output**: Paged display of zero page with ESC/ENTER controls
- **Notes**: Shows critical system variables and workspace

### System Commands

#### H: Help
- **Purpose**: Display available monitor commands
- **Format**: `H:`
- **Output**: List of all commands with brief descriptions
- **Notes**: Shows command syntax and usage

#### ESC: Exit Mode
- **Purpose**: Exit current command mode and return to command prompt
- **Format**: Press ESC key
- **Usage**: Exit write mode, abort paged displays, cancel operations
- **Notes**: Universal escape mechanism

#### .: Dot Command (Command Recall)
- **Purpose**: Recall and re-execute the last command
- **Format**: `.`
- **Usage**: Type dot at command prompt to recall previous command
- **Notes**: Recalled command can be edited before execution

## Command Syntax Rules

### General Format
- Commands start with a letter followed by colon: `X:`
- Addresses are 4-digit hexadecimal: `8000`, `FFFF`
- Ranges use dash separator: `8000-8FFF`
- Multiple parameters separated by commas: `8000,FF`

### Address Formats
- All addresses are hexadecimal (no $ prefix required)
- Leading zeros required for addresses < $1000: `0400`, not `400`
- Case insensitive: `8000` same as `8000`

### Error Messages
- `?ERROR` - Invalid command syntax or parameters
- `?RANGE` - Invalid address range (end < start)
- `?VALUE` - Invalid hexadecimal value or parameter

## Universal Commands
These commands work from any monitor mode:
- **ESC** - Exit current mode
- **H:** - Display help
- **R:** - Read memory (does not change mode)
- **.** - Recall last command

## Monitor Modes

### Command Mode (Default)
- Monitor prompt: `>`
- All commands available
- Default state after reset

### Write Mode  
- Monitor prompt: `W:XXXX>`
- Entered via W: command
- Interactive hex byte entry
- ESC returns to command mode

## Memory Map Considerations
- **$0000-$00FF**: Zero page (system variables)
- **$0100-$01FF**: Stack page  
- **$0200-$03FF**: Monitor variables and buffers
- **$0400-$07FF**: Screen memory
- **$F000-$FFFF**: Kernel ROM
- **$FF00-$FF0F**: Kernel jump table

## Tips and Best Practices
1. Use R: command to verify memory before writing
2. Save important code/data with S: command before experimenting
3. Use T: and Z: commands for debugging program state
4. ESC key aborts most operations safely
5. Dot command (.) saves typing for repeated operations