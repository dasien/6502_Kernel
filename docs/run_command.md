# G: Go/Run Command

## Purpose
Execute user program starting at a specified address.

## Activation
- Command letter: `G`
- Format: `G:address`

## Input Requirements

### Execution Address
- `address`: 4-digit hexadecimal address (0000-FFFF)  
- Specifies entry point for user program
- Must be preceded by `G:`

## Execution Model
The G: command transfers control directly to user code:
1. Parse and validate the target address
2. Call user program via indirect jump: `JMP (address)`
3. User program executes with full system access
4. User program must RTS to return to monitor
5. Monitor resumes at command prompt

## User Program Requirements
- Must end with RTS instruction to return to monitor
- Can modify any memory (including monitor variables)
- Has access to kernel jump table at $FF00-$FF0F
- Can use monitor I/O routines via jump table

## Available Kernel Services
User programs can call these kernel routines via jump table:
- `$FF00` - PRINT_CHAR (print single character)
- `$FF03` - PRINT_MESSAGE (print null-terminated string)
- `$FF06` - PRINT_NEWLINE (print CR/LF)
- `$FF09` - GET_KEYSTROKE (wait for key press)
- `$FF0C` - CLEAR_SCREEN (clear display)
- `$FF0F` - GET_RANDOM_NUMBER (get random byte)

## Examples
- `G:8000` - Execute program starting at $8000
- `G:1000` - Run code loaded at $1000
- `G:0300` - Execute program in page 3

## Return to Monitor
User programs return control by:
```assembly
RTS    ; Return to monitor command prompt
```

## Error Messages
- `?ERROR` - Invalid syntax or hex address
- `?VALUE` - Invalid hexadecimal address format

## Notes
- One-shot command - does not enter a special run mode
- No stack manipulation - uses normal JSR/RTS mechanism  
- User program has full system privileges
- Monitor state is preserved across program execution
- If user program crashes, system may require reset
- No timeout or safety mechanisms - user program controls system