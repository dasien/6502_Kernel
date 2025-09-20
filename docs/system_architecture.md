# 6502 Computer System Architecture

## Overview

This document describes the virtual computer system architecture implemented in software, showing how the various virtual chips and circuits interconnect to create a complete 6502-based computer system. The design emulates a Commodore 64-like architecture with modern enhancements for development and debugging.

## System Block Diagram

```
                    6502 Computer System
    ┌─────────────────────────────────────────────────────────────┐
    │                                                             │
    │   ┌─────────────┐    ┌──────────────┐    ┌──────────────┐   │
    │   │ Reset       │    │ Timing       │    │ CPU 6502     │   │
    │   │ Circuit     │───▶│ Circuit      │───▶│              │   │
    │   │             │    │ (1MHz clock) │    │ • Registers  │   │
    │   └─────────────┘    └──────────────┘    │ • ALU        │   │
    │                                          │ • Decoder    │   │
    │                                          └──────┬───────┘   │
    │                                                 │           │
    │                          16-bit Address Bus     │           │
    │   ┌─────────────────────────────────────────────┼───────┐   │
    │   │                     │                       │       │   │
    │   │                     ▼                       ▼       │   │
    │   │           ┌──────────────┐         ┌──────────────┐ │   │
    │   │           │   Memory     │         │   Memory     │ │   │
    │   │           │ Mapped I/O   │◀────────│   System     │ │   │
    │   │           │   Select     │         │   (64KB)     │ │   │
    │   │           └──────┬───────┘         └──────────────┘ │   │
    │   │                  │                                  │   │
    │   │     ┌────────────┼────────────┐                     │   │
    │   │     │            │            │                     │   │
    │   │     ▼            ▼            ▼                     │   │
    │   │ ┌─────────┐ ┌─────────┐ ┌──────────┐                │   │
    │   │ │   VIC   │ │   PIA   │ │ Kernel   │                │   │
    │   │ │ Video   │ │Keyboard │ │  ROM     │                │   │
    │   │ │ Chip    │ │ & File  │ │          │                │   │
    │   │ │         │ │   I/O   │ │          │                │   │
    │   │ └─────────┘ └─────────┘ └──────────┘                │   │
    │   │     │           │                                   │   │
    │   └─────┼───────────┼───────────────────────────────────┘   │
    │         │           │                                       │
    │         ▼           ▼                                       │
    │   ┌─────────┐ ┌──────────┐                                  │
    │   │ Screen  │ │Keyboard/ │                                  │
    │   │Display  │ │File Ops  │                                  │
    │   │40x25    │ │          │                                  │
    │   └─────────┘ └──────────┘                                  │
    │                                                             │
    └─────────────────────────────────────────────────────────────┘
```

## Component Descriptions

### 1. CPU6502 - The Central Processing Unit

**Location**: `include/computer/CPU6502.h`

The heart of the system, implementing the complete MOS 6502 microprocessor:

- **8-bit Architecture**: Accumulator (A), Index registers (X, Y)
- **16-bit Address Bus**: 64KB addressable memory space
- **Stack Operations**: Hardware stack at $0100-$01FF
- **Status Flags**: N, V, B, D, I, Z, C flags in processor status register
- **Instruction Set**: Complete 6502 instruction set with all addressing modes
- **BCD Support**: Binary Coded Decimal arithmetic
- **Interrupt Handling**: IRQ, NMI, and BRK instruction support

**Key Interfaces**:
- Memory bus connection for read/write operations
- Reset signal from Reset Circuit
- Clock signal from Timing Circuit

### 2. Memory - 64KB System Memory with Memory-Mapped I/O

**Location**: `include/computer/Memory.h`

Central memory system with intelligent I/O routing:

**Memory Map**:
```
$0000-$03FF: System RAM (1KB)
  $0000-$00FF: Zero page (256 bytes)
  $0100-$01FF: Stack page (256 bytes)
  $0200-$03FF: System variables (512 bytes)

$0400-$07FF: Screen Memory (1KB)
  $0400-$07E7: 40x25 character screen (1000 bytes)

$0800-$CFFF: User RAM (51KB)
  Available for user programs and data

$D000-$DFFF: I/O Area (4KB)
  $DC00-$DC21: PIA registers (keyboard, file I/O)
  $D000-$D3FF: VIC registers (video)
  $D400-$D7FF: SID registers (future expansion)
  $D800-$DBFF: Color memory (future expansion)

$E000-$FFFF: ROM Area (8KB)
  $F000-$FDFF: Kernel ROM code
  $FF00-$FF11: Kernel API jump table
  $FFFA-$FFFF: Interrupt vectors
```

**Key Features**:
- Automatic I/O routing to VIC and PIA chips
- Memory-mapped hardware register access
- ROM/RAM banking simulation

### 3. VIC - Video Interface Chip

**Location**: `include/computer/VIC.h`

Text-mode video controller similar to VIC-II:

**Features**:
- **40x25 Character Display**: 1000 characters total
- **Memory-Mapped Screen**: $0400-$07E7 screen buffer
- **Text Operations**: Screen clearing, scrolling, cursor management
- **Direct Character Access**: Read/write individual screen positions

**Interfaces**:
- Memory bus connection for screen buffer access
- Screen buffer for display system integration
- Character positioning and cursor control

### 4. PIA - Peripheral Interface Adapter

**Location**: `include/computer/PIA.h`

Enhanced PIA with traditional I/O and modern file operations:

**Traditional PIA Registers** ($DC00-$DC05):
- Port A Data/DDR: Keyboard input
- Port B Data/DDR: Future expansion
- Control Registers: Port configuration

**Extended File I/O Interface** ($DC10-$DC21):
- File command/status registers
- Target address registers
- Filename buffer (12 characters)
- End address for save operations

**Features**:
- **Keyboard Buffer**: 32-character circular buffer
- **File Operations**: Load/save for monitor L: and S: commands
- **Status Management**: Data available flags and operation status

### 5. Reset Circuit - System Reset Management

**Location**: `include/computer/ResetCircuit.h`

Manages system initialization and reset operations:

**Functions**:
- **Power-On Reset**: Complete system initialization sequence
- **Manual Reset**: User-triggered reset (warm boot)
- **Vector Loading**: Reset vector from $FFFC-$FFFD
- **CPU State Initialization**: Proper 6502 startup state

**Reset Sequence**:
1. Initialize CPU registers to known state
2. Load program counter from reset vector
3. Set interrupt disable flag
4. Clear decimal mode
5. Initialize stack pointer

### 6. Timing Circuit - Clock Generation

**Location**: `include/computer/TimingCircuit.h`

Provides accurate timing for realistic emulation:

**Features**:
- **Target Frequency**: 1MHz (classic 6502 speed)
- **Cycle Timing**: Accurate delay between instructions
- **Performance Monitoring**: Actual vs. target frequency measurement
- **Speed Control**: Prevents emulation from running too fast

## Data Flow and Interconnections

### CPU to Memory
1. CPU generates 16-bit address and control signals
2. Memory system decodes address range
3. Regular memory: Direct RAM/ROM access
4. I/O ranges: Routed to appropriate chip (VIC/PIA)

### Memory-Mapped I/O
1. CPU writes to $0400-$07E7 → VIC screen buffer
2. CPU reads from $DC00 → PIA keyboard data
3. CPU writes to $DC10-$DC21 → PIA file operations

### System Initialization
1. Reset Circuit triggers power-on sequence
2. MapFileParser loads kernel ROM segments
3. Memory system maps ROM to $F000-$FFFF
4. CPU loads reset vector and begins execution
5. Timing Circuit begins clock generation

### Keyboard Input Flow
1. Host system → PIA keyboard buffer
2. PIA sets data available flag at $DC02
3. Kernel polls PIA for keystrokes
4. Characters processed by monitor program

### Display Output Flow
1. Kernel writes characters to screen memory
2. Memory routes writes to VIC chip
3. VIC updates internal screen buffer
4. Display system reads VIC buffer for rendering

### File Operations Flow
1. Monitor L:/S: commands → Parameters to PIA registers
2. PIA coordinates with host file system
3. File data transferred through memory system
4. Status flags updated in PIA registers

## System Integration

The `Computer6502` class serves as the system integrator, instantiating and connecting all components:

```cpp
class Computer6502 {
    VIC video_chip;           // Video subsystem
    PIA pia;                  // I/O subsystem
    Memory memory;            // Memory subsystem
    CPU6502 cpu;              // Processing subsystem
    ResetCircuit reset_circuit; // Reset subsystem
    TimingCircuit timing_circuit; // Timing subsystem
};
```

**Initialization Sequence**:
1. Construct all virtual chips
2. Connect memory to VIC and PIA for I/O routing
3. Connect CPU to memory for bus access
4. Connect Reset Circuit to CPU for control
5. Load kernel ROM using MapFileParser
6. Trigger power-on reset
7. Begin instruction execution loop

This architecture provides a realistic 6502 computing environment while adding modern conveniences like file I/O and development tools.