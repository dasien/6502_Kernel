# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a 6502 kernel development project that implements low-level system initialization and hardware control for a 6502-based computer system (specifically targeting Commodore 64 architecture). The project combines assembly language kernel code with C++ development tools.

This is not a c64 emulator and does not use PETSCII.

## Build System

The project uses CMake as the build system:

```bash
# Or using ninja (if available)
cmake -G Ninja ..
ninja
```

The main executable target is `6502_Kernel` which builds from `main.cpp`.

## Architecture and Code Organization

### Core Components

1. **kernel.asm** - The main 6502 assembly kernel code that handles:
   - System initialization and reset vector handling
   - Hardware setup (VIC-II, SID, CIA chips)
   - Memory banking configuration
   - Interrupt service routines
   - Zero page and stack initialization
   - **6502 Monitor Program** - Complete interactive debugging/programming environment

2. **main.cpp** - C++ development tool/simulator (currently a basic template)

3. **kernel_memory_map.md** - Comprehensive documentation of the 6502 memory layout including:
   - Zero page allocation ($0000-$00FF)
   - Stack organization ($0100-$01FF)
   - System variables ($0200-$03FF)
   - Screen memory ($0400-$07FF)
   - Hardware I/O mapping ($D000-$DFFF)
   - ROM areas and banking control

### Memory Architecture

The kernel follows standard Commodore 64 memory organization:
- **Zero Page ($00-$FF)**: Critical for kernel workspace and fast addressing
- **Stack ($0100-$01FF)**: System stack growing downward from $01FF
- **System Variables ($0200-$03FF)**: Kernel data structures and I/O buffers
- **Screen Memory ($0400-$07FF)**: 40x25 character display
- **Hardware I/O ($D000-$DFFF)**: VIC-II, SID, CIA, and expansion slots

## 6502 Monitor Program

The kernel includes a complete interactive monitor program for debugging and programming. The monitor provides a command-line interface for memory manipulation, program execution, and system inspection.

### Monitor Memory Layout ($0200-$0261)

The monitor uses system RAM starting at $0200 for variables and buffers:

- **$0200-$024F**: Command input buffer (80 bytes)
- **$0250-$0251**: Command buffer pointer and length
- **$0252-$0258**: Monitor state variables (mode, addresses)
- **$0259-$025E**: Parser variables and temporary storage
- **$0260-$0261**: Message pointer for optimized string printing

### Monitor Commands

The monitor supports 4 primary modes plus utility commands:

#### Core Modes
- **W:xxxx [data]** - Write mode: Write bytes to memory at address xxxx
- **R:xxxx[-yyyy]** - Read mode: Read and display memory contents
- **G:xxxx** - Go/Run mode: Execute program starting at address xxxx
- **X:** - Exit current mode and return to command mode

#### Utility Commands
- **K:** - Clear screen (Klear)
- **S:** - Display stack memory ($0100-$01FF)
- **Z:** - Display zero page memory ($0000-$00FF)
- **T:** - Show target address and current byte value
- **H:** - Display help with all available commands

### Monitor Features

#### Command Line Interface
- 80-character input buffer with full editing support
- Backspace/delete character support
- Command history and error handling
- Syntax validation and error reporting

#### Memory Operations
- Hex address parsing (supports both uppercase and lowercase)
- Range operations (R:8000-80FF displays memory range)
- Sequential write operations with old/new value display
- 8-byte-per-line formatted output for readability

#### String Optimization System
The monitor uses an advanced null-terminated string system for efficient message display:

```assembly
; Message data stored as null-terminated strings
MSG_HELP_HEADER:     .BYTE "6502 MONITOR COMMANDS", 0
MSG_HELP_WRITE:      .BYTE "W:XXXX WRITE", 0

; Generic print routine using indirect indexed addressing
PRINT_MESSAGE:
    LDY #$00                    ; Initialize string index
PRINT_MSG_LOOP:
    LDA (MON_MSG_PTR),Y         ; Load character using indirect indexed
    BEQ PRINT_MSG_DONE          ; If null terminator, done
    JSR PRINT_CHAR              ; Print the character
    INY                         ; Move to next character
    BNE PRINT_MSG_LOOP          ; Continue (strings < 256 chars)
PRINT_MSG_DONE:
    RTS

; Optimized function calls (8 bytes each)
PRINT_HELP_HEADER:
    LDA #<MSG_HELP_HEADER       ; Load low byte of message address
    STA MON_MSG_PTR             ; Store in message pointer
    LDA #>MSG_HELP_HEADER       ; Load high byte of message address
    STA MON_MSG_PTR_HI          ; Store in message pointer high
    JSR PRINT_MESSAGE           ; Print the message
    RTS
```

This optimization achieves ~80-88% code size reduction compared to individual character loading.

### Key Assembly Patterns

The kernel code follows these patterns:
- Hardware initialization loops for clearing chip registers
- Memory banking through processor port ($00/$01) control
- Interrupt vector setup at $FFFA-$FFFF
- Zero page clearing preserving processor port registers
- Screen and color memory initialization
- You can use https://www.masswerk.at/6502/6502_instruction_set.html and http://www.6502.org/documents as resources for the processor and assembly language
- 
## Development Guidelines

### Assembly Code Standards
- Use meaningful labels and constants (defined at top of file)
- Include detailed comments explaining hardware interactions
- Follow the existing memory map allocation strictly
- Preserve critical zero page locations during initialization

### Memory Banking Considerations
- The processor port at $00/$01 controls memory banking
- Default configuration ($37 at $01) enables BASIC ROM, Kernal ROM, and I/O
- Banking changes affect what appears in $A000-$FFFF range
- Always restore banking state after temporary changes

### Hardware Initialization Sequence
1. Clear decimal mode and disable interrupts
2. Set up stack pointer
3. Configure memory banking
4. Initialize VIC-II video chip
5. Initialize SID sound chip
6. Initialize CIA chips for keyboard/timers
7. Clear zero page (preserving $00/$01)
8. Clear screen and color memory
9. Start monitor program

### Monitor Development Guidelines
- Always use the null-terminated string system for new messages
- Allocate monitor variables in the $0200-$03FF range per the memory map
- Implement proper hex parsing for addresses and data input
- Provide clear error messages and syntax validation
- Follow the 8-byte function pattern for message printing optimization
- Use the existing PRINT_CHAR, PRINT_HEX_BYTE, and I/O routines
- Test all monitor commands thoroughly in both emulation and hardware

## Critical Memory Locations

Refer to `kernel_memory_map.md` for complete details, but key locations include:
- **$00/$01**: Processor port for memory banking
- **$01FF**: Initial stack pointer location
- **$0400-$07E7**: Screen character memory
- **$D800-$DBFF**: Color memory
- **$FFFA-$FFFF**: Interrupt vectors (NMI, RESET, IRQ)

## Requirements and Context Documents 
- Located in the Context/ subdirectory