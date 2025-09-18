# MFC 6502 Kernel

A comprehensive 6502 microprocessor kernel implementation with an interactive monitor program for debugging, programming, and system control.

## Project Overview

This project implements a complete 6502-based computer system kernel for emulated environments. The kernel provides low-level system initialization, hardware control, and most importantly, a powerful interactive monitor program for direct system interaction.

**Key Features:**
- Complete 6502 assembly language kernel optimized for emulated environments
- Interactive monitor with comprehensive debugging tools  
- Memory manipulation and program execution capabilities
- Streamlined architecture with universal commands and simplified modes
- File I/O operations for loading and saving programs
- Comprehensive search, fill, move, and copy operations

## Monitor Program

The heart of this system is the **6502 Monitor** - a complete interactive debugging and programming environment that provides direct control over the computer's memory and execution. The monitor offers a command-line interface with powerful tools for memory operations, program execution, and system inspection.

### Architecture Overview

The monitor features a streamlined architecture with:
- **Two primary modes**: Command mode (default) and Write mode for interactive editing  
- **Universal commands** that work from any mode: ESC, H:, R:, and . (dot)
- **One-shot operations** for G:, L:, and S: commands that execute immediately
- **Simplified command processing** with consistent syntax and error handling

### Getting Started

When the system boots, you'll see:
```
-=MFC 6502 OPERATIONAL=-
>
```

The `>` prompt indicates you're in command mode. You can now enter any monitor command.

## Monitor Commands

For complete command documentation including syntax, examples, and detailed usage information, see:

### **📖 [Complete Command Reference](docs/command_help.md)**

The command reference provides comprehensive documentation for all monitor commands, organized by category:

### Quick Command Summary

| Category | Commands | Description |
|----------|----------|-------------|
| **Memory Operations** | [R:](docs/read_command.md), [W:](docs/write_command.md), [F:](docs/fill_command.md), [M:](docs/move_copy_command.md), [X:](docs/search_command.md) | Read, write, fill, move/copy, and search memory |
| **Program Operations** | [G:](docs/run_command.md), [L:](docs/load_command.md), [S:](docs/save_command.md) | Execute, load, and save programs |
| **Display Commands** | C:, T:, Z: | Clear screen, show stack, show zero page |
| **System Commands** | H:, ESC, . | Help, exit mode, command recall |

### Key Command Features

- **R: Read Memory** - Universal command that works from any mode, supports single addresses or ranges
- **W: Write Memory** - Interactive hex editing with address advancement  
- **G: Go/Run** - Direct program execution with return to monitor
- **L: Load File** - Immediate file loading: `L:8000,FILENAME`
- **S: Save File** - Immediate file saving: `S:8000-8FFF,FILENAME`
- **F: Fill Memory** - High-performance memory filling with progress feedback
- **M: Move/Copy** - Smart memory operations with overlap detection  
- **X: Search Memory** - Multi-byte pattern search with paged output

### Universal Commands

These commands work from any monitor mode:
- **ESC** - Exit current mode, abort operations
- **H:** - Display help information
- **R:** - Read memory (non-modal)
- **.** - Recall and edit last command

### Error Handling

The monitor provides clear, consistent error messages:
- **`?ERROR`** - Invalid command syntax or parameters
- **`?RANGE`** - Invalid or out-of-bounds address range  
- **`?VALUE`** - Invalid hexadecimal characters in input

## System Architecture

### Memory Layout

- **$0000-$00FF**: Zero Page (system workspace and variables)
- **$0100-$01FF**: Stack memory  
- **$0200-$03FF**: Monitor variables and command buffers
- **$0400-$07FF**: Screen memory (display buffer)
- **$F000-$FFFF**: Kernel ROM (3,834 bytes optimized)

### Kernel Services

User programs can access kernel services via the jump table at $FF00:

| Address | Service | Description |
|---------|---------|-------------|
| $FF00 | PRINT_CHAR | Print single character |
| $FF03 | PRINT_MESSAGE | Print null-terminated string |
| $FF06 | PRINT_NEWLINE | Print carriage return/line feed |
| $FF09 | GET_KEYSTROKE | Wait for key press |
| $FF0C | CLEAR_SCREEN | Clear display |
| $FF0F | GET_RANDOM_NUMBER | Generate random byte |

### File I/O Interface

The kernel provides memory-mapped file I/O at:
- **$DC10**: File command register
- **$DC11**: File status register  
- **$DC12-$DC13**: Address registers
- **$DC14-$DC1F**: Filename buffer
- **$DC20-$DC21**: End address (for save operations)

## Recent Optimizations

The kernel has been extensively optimized for size and performance:

- **ROM Size**: 3,413 bytes (83.3% of 4KB limit)
- **Mode Simplification**: Reduced from 6 modes to 2 modes
- **Universal Commands**: R:, H:, ESC, and . work from any context
- **One-Shot Operations**: G:, L:, and S: execute immediately without mode changes
- **Code Cleanup**: Removed unused functions and variables
- **Memory Banking**: Eliminated C64-specific banking for pure emulated environment

## Building and Development

### Prerequisites
- CMake 3.20 or later
- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 2019+)
- cc65 toolchain (ca65 assembler and ld65 linker)
- Qt6 or Qt5 (optional, for GUI support)

### Build Instructions

```bash
# Option 1: Use the convenient build script (recommended)
./build.sh

# Option 2: Manual out-of-source build
cd cmake-build-debug
cmake -G Ninja ..
ninja

# Build outputs:
# - Executable: cmake-build-debug/bin/6502-kernel
# - Kernel ROM: cmake-build-debug/kernel.rom
# - Memory map: cmake-build-debug/kernel.map
```

### Project Structure
```
6502-kernel/
├── src/                    # C++ source files
├── include/                # C++ header files  
├── kernel/                 # 6502 assembly kernel
│   ├── asm/               # Assembly source files
│   └── config/            # Build configuration
├── docs/                  # Documentation
├── examples/              # Example 6502 programs
├── tools/cmake/           # CMake modules
└── tests/                 # Unit tests
```

For detailed development information and project context, see:
- **[CLAUDE.md](CLAUDE.md)** - Development guidelines and architecture documentation  
- **[docs/](docs/)** - Detailed command documentation and requirements

## Tips for Effective Use

1. **Start with Help**: Use `H:` to see all available commands
2. **Use Command Recall**: The `.` command saves time when refining commands
3. **Leverage Universal Commands**: R: works from any mode for quick memory checks
4. **File Operations**: Use immediate syntax `L:8000,FILE` and `S:8000-8FFF,FILE`
5. **Search Effectively**: Use X: with multiple byte patterns for precise matching
6. **Program Development**: Load programs with L:, test with G:, save modifications with S:

The monitor is designed for both interactive exploration and efficient program development workflows.