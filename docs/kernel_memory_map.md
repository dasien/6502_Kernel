# 6502 Kernel System Memory Map

## Core System Memory Layout

| Address Range | Size       | Purpose | Usage in Kernel |
|---------------|------------|---------|-----------------|
| $0000-$00FF   | 256 bytes  | **Zero Page RAM** | Fast access variables, pointers, kernel workspace |
| $0100-$01FF   | 256 bytes  | **Stack** | Subroutine calls, interrupt handling, temporary storage |
| $0200-$03FF   | 512 bytes  | **System Variables** | Kernel data structures, I/O buffers |
| $0400-$07FF   | 1024 bytes | **Screen Memory** | Character display data (40x25 = 1000 bytes) |

## Critical Zero Page Locations

| Address | Size | Purpose | Kernel Usage |
|---------|------|---------|--------------|
| $0000-$0001 | 2 bytes | **Monitor Current Address** | MON_CURRADDR_LO/HI - current memory address |
| $0002-$0003 | 2 bytes | **Monitor Message Pointer** | MON_MSG_PTR_LO/HI - string display pointer |
| $0004-$0005 | 2 bytes | **Jump Vector** | JUMP_VECTOR - indirect jump target |
| $0006-$0007 | 2 bytes | **Screen Pointer** | SCREEN_PTR_LO/HI - current screen position |
| $0008-$0009 | 2 bytes | **Scroll Source Address** | SCRL_SRC_ADDR_LO/HI - scroll operations |
| $000A-$000B | 2 bytes | **Scroll Destination Address** | SCRL_DEST_ADDR_LO/HI - scroll operations |
| $000C | 1 byte | **Scroll Byte Counter** | SCRL_BYTE_CNT - scroll operations |
| $000D | 1 byte | **Command Line Counter** | CMD_LINE_COUNT - paging support |
| $000E | 1 byte | **Page Abort Flag** | PAGE_ABORT_FLAG - ESC key handling |
| $000F | 1 byte | **Random Number Seed** | RNG_SEED - random number generation |
| $0010 | 1 byte | **Random Number Max** | RNG_MAX - random number range |
| $0011-$00EF | 223 bytes | **Available Zero Page** | **FREE for user programs** |
| $00F0-$00FF | 16 bytes | **Hex Lookup Table** | HEX_LOOKUP_TABLE - hex digit conversion |

## Stack Area Detail

| Address Range | Purpose | Notes |
|---------------|---------|-------|
| $0100-$013F | **Deep Stack** | Nested subroutine calls, deep interrupt nesting |
| $0140-$019F | **Normal Stack** | Typical operation range |
| $01A0-$01FF | **Stack Top** | Initial stack pointer starts at $01FF |

## System Variables Area

| Address Range | Size | Purpose | Content |
|---------------|------|---------|---------|
| $0200-$024F | 80 bytes | **Monitor Command Buffer** | MON_CMDBUF - user input buffer |
| $0250-$0251 | 2 bytes | **Command Buffer Control** | MON_CMDPTR/CMDLEN - buffer position/length |
| $0252 | 1 byte | **Monitor Mode** | MON_MODE - current operating mode (0-3) |
| $0253-$0256 | 4 bytes | **Address Range Variables** | MON_STARTADDR/ENDADDR - operation addresses |
| $0257-$025F | 9 bytes | **Monitor Parser/Display** | Parser state, counters, cursor position |
| $0260-$0287 | 40 bytes | **Extended Monitor Variables** | Fill, move/copy, search command variables |
| $0288-$02E9 | 98 bytes | **Command History & Patterns** | Last command buffer and search patterns |
| $02EA-$03FF | 278 bytes | **Available System RAM** | **FREE for user programs** |

## Screen and Video Memory

| Address Range | Size | Purpose | Notes |
|---------------|------|---------|-------|
| $0400-$07E7 | 1000 bytes | **Screen Characters** | 40x25 character display |
| $07E8-$07FF | 24 bytes | **Screen Unused** | Padding to page boundary |

## Hardware I/O Mapping

| Address Range | Size | Component | Registers |
|---------------|------|-----------|-----------|
| $D000-$D3FF | 1024 bytes | **VIC-II Video** | Graphics, sprites, display control |
| $D400-$D7FF | 1024 bytes | **SID Sound** | Audio synthesis, filters |
| $D800-$DBFF | 1024 bytes | **Color Memory** | Screen color attributes |
| $DC00-$DCFF | 256 bytes | **CIA #1** | Keyboard, joystick, timers |
| $DD00-$DDFF | 256 bytes | **CIA #2** | Serial bus, RS-232, memory banking |
| $DE00-$DEFF | 256 bytes | **I/O Expansion #1** | Cartridge expansion |
| $DF00-$DFFF | 256 bytes | **I/O Expansion #2** | Additional expansion |

## Memory Banking Control

| Address | Bit Pattern | BASIC ROM | Kernal ROM | I/O | Character ROM |
|---------|-------------|-----------|------------|-----|---------------|
| $0001 | %xx110xxx | Out | Out | Out | In |
| $0001 | %xx111xxx | In | In | In | Out |

## ROM Areas

| Address Range | Size | Content | Bankable |
|---------------|------|---------|----------|
| $A000-$BFFF | 8192 bytes | **BASIC ROM** | Yes (can switch to RAM) |
| $D000-$DFFF | 4096 bytes | **Character ROM** | Yes (normally I/O visible) |
| $E000-$FFFF | 8192 bytes | **Kernal ROM** | Yes (can switch to RAM) |

## Interrupt Vectors

| Address | Vector | Handler | Purpose |
|---------|---------|---------|---------|
| $FFFA-$FFFB | **NMI** | NMI_HANDLER | Non-maskable interrupt |
| $FFFC-$FFFD | **RESET** | RESET | Power-on/reset entry point |
| $FFFE-$FFFF | **IRQ** | IRQ_HANDLER | Maskable interrupt |

## Current Kernel Memory Allocations

### Zero Page Usage (Actual Allocations)
| Address | Symbol | Purpose |
|---------|--------|---------|
| $0000-$0001 | MON_CURRADDR_LO/HI | Monitor current address pointer |
| $0002-$0003 | MON_MSG_PTR_LO/HI | Message display pointer |
| $0004-$0005 | JUMP_VECTOR | Indirect jump vector (2 bytes) |
| $0006-$0007 | SCREEN_PTR_LO/HI | Current screen memory pointer |
| $0008-$0009 | SCRL_SRC_ADDR_LO/HI | Scroll source address |
| $000A-$000B | SCRL_DEST_ADDR_LO/HI | Scroll destination address |
| $000C | SCRL_BYTE_CNT | Scroll byte counter |
| $000D | CMD_LINE_COUNT | Command line counter for paging |
| $000E | PAGE_ABORT_FLAG | Page abort flag (ESC handling) |
| $000F | RNG_SEED | Random number generator seed |
| $0010 | RNG_MAX | Random number generator maximum |
| $00F0-$00FF | HEX_LOOKUP_TABLE | Hex digit lookup table |

### Monitor Variables in System RAM
| Address | Symbol | Purpose |
|---------|--------|---------|
| $0200-$024F | MON_CMDBUF | Command input buffer (80 bytes) |
| $0250 | MON_CMDPTR | Command buffer position pointer |
| $0251 | MON_CMDLEN | Current command length |
| $0252 | MON_MODE | Monitor mode (0=Cmd, 1=Write, 2=Read, 3=Run) |
| $0253-$0254 | MON_STARTADDR_LO/HI | Start address for operations |
| $0255-$0256 | MON_ENDADDR_LO/HI | End address for operations |
| $0257 | MON_PARSE_PTR | Parser position pointer |
| $0258 | MON_PARSE_LEN | Remaining parse length |
| $0259 | MON_HEX_TEMP | Temporary hex conversion storage |
| $025A | MON_BYTE_COUNT | Write operation byte counter |
| $025B | MON_LINE_COUNT | Display line counter |
| $025C | MON_ERROR_FLAG | Error flag for operations |
| $025D | CURSOR_X | Current cursor X position (0-39) |
| $025E | CURSOR_Y | Current cursor Y position (0-24) |
| $025F | MON_MSG_TMP_POS | Temporary message position |

## Available Memory for User Programs

### Zero Page Available Space
- **$0011-$00EF (223 bytes)** - Completely free zero page space
- Fast addressing modes available for user variables and pointers

### System RAM Available Space
- **$02EA-$03FF (278 bytes)** - Available system variable space
- **$0800-$7FFF (30,720 bytes)** - Main user program area
- Screen memory ($0400-$07FF) should not be used for program storage

## Critical Kernel Constants

| Symbol | Value | Purpose |
|--------|-------|---------|
| STACK_TOP | $FF | Initial stack pointer value |
| SCREEN_START | $0400 | Start of screen memory |
| SCREEN_WIDTH | 40 | Characters per line |
| SCREEN_HEIGHT | 25 | Lines on screen |

## Memory Usage Notes

- **Zero Page**: Most critical for kernel performance - 6502 has special fast addressing modes for $00-$FF
- **Stack**: Grows downward from $01FF, kernel must monitor for overflow
- **Screen Memory**: Fixed location for video display, must be initialized on boot
- **I/O Area**: Can be banked out for RAM/ROM access - kernel must manage switching
- **Banking**: Processor port bits control what appears in $A000-$FFFF range
- **Vectors**: Located at top of memory space, must point to valid handlers

This memory map reflects the initialization performed by the kernel code and shows how the 6502 address space is organized for system operation.