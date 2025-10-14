---
enhancement: add-basic-interpreter
task_id: task_1759348065_71462
agent: assembly-developer
created: 2025-10-01 21:52:15
---

# BASIC Interpreter Integration Architecture

## Document Purpose

This document defines the technical architecture for integrating the EhBASIC interpreter into the 6502 kernel monitor system. It specifies how BASIC will be loaded, executed, and integrated with monitor I/O routines, and how control will be transferred between the monitor and BASIC systems.

---

## System Overview

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                    6502 KERNEL SYSTEM                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────┐                    ┌──────────────────┐   │
│  │  MONITOR ROM    │                    │   BASIC ROM      │   │
│  │  $F000-$FFFF    │                    │   $C000-$EFFF    │   │
│  │                 │                    │                  │   │
│  │  Commands:      │                    │  EhBASIC 2.22p5  │   │
│  │  R: W: G: L:    │◄──── B: CMD ─────► │                  │   │
│  │  F: M: X: Z:    │                    │  Interpreter     │   │
│  │  T: H: C:       │                    │  Runtime         │   │
│  │  + B: (NEW)     │                    │                  │   │
│  └────────┬────────┘                    └────────┬─────────┘   │
│           │                                      │             │
│           │ Uses                                 │ Uses        │
│           ▼                                      ▼             │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │           SHARED I/O ROUTINES ($FF00-$FF11)             │   │
│  │                                                          │   │
│  │  PRINT_CHAR ($FF00)  - Character output to screen       │   │
│  │  GET_KEYSTROKE ($FF09) - Keyboard input with cursor     │   │
│  │                                                          │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│                    MEMORY LAYOUT                                │
├─────────────────────────────────────────────────────────────────┤
│  $0000-$00FF: Zero Page (Relocated monitor vars at $14-$3F)    │
│  $0100-$01FF: Stack (Shared between monitor and BASIC)         │
│  $0200-$0268: BASIC variables and input buffer                 │
│  $0269-$02DE: Monitor variables                                │
│  $0300-$03FF: BASIC user RAM start                             │
│  $0400-$07FF: Screen memory (40x25 characters)                 │
│  $0800-$BFFF: User program space                               │
│  $C000-$EFFF: BASIC ROM (12KB)                                 │
│  $F000-$FFFF: Monitor ROM (4KB)                                │
└─────────────────────────────────────────────────────────────────┘
```

### Control Flow

```
Power On / Reset
      │
      ▼
┌──────────────┐
│   Monitor    │
│   Startup    │
│   $F000      │
└──────┬───────┘
       │
       ▼
┌──────────────────┐
│  Monitor Prompt  │◄──────────────┐
│  "READY."        │               │
└──────┬───────────┘               │
       │                           │
       │ User types "B:"           │
       ▼                           │
┌──────────────────┐               │
│  HANDLE_B_CMD    │               │
│  (kernel.asm)    │               │
└──────┬───────────┘               │
       │                           │
       │ 1. Save monitor state     │
       │ 2. Init BASIC I/O vectors │
       │ 3. Clear BASIC RAM        │
       ▼                           │
┌──────────────────┐               │
│   LAB_COLD       │               │
│   BASIC Start    │               │
│   $C000          │               │
└──────┬───────────┘               │
       │                           │
       ▼                           │
┌──────────────────┐               │
│  BASIC Prompt    │               │
│  "READY"         │               │
└──────┬───────────┘               │
       │                           │
       │ User types BASIC code     │
       │ (PRINT, FOR, etc.)        │
       ▼                           │
┌──────────────────┐               │
│  BASIC Runtime   │               │
│  Executing       │               │
└──────┬───────────┘               │
       │                           │
       │ User types "BYE" or       │
       │ calls monitor return      │
       ▼                           │
┌──────────────────┐               │
│ RETURN_FROM_     │               │
│ BASIC            │───────────────┘
│ (kernel.asm)     │
│ 1. Clear buffers │
│ 2. Restore state │
└──────────────────┘
```

---

## Component Specifications

### 1. Monitor ROM ($F000-$FFFF)

**Current Size:** 3413 bytes used / 4096 bytes available
**Remaining Space:** 683 bytes

**New Components Required:**
1. **B: Command Handler** (~80 bytes)
   - Save monitor state
   - Initialize BASIC I/O vectors
   - Transfer control to BASIC

2. **BASIC Return Handler** (~40 bytes)
   - Clear monitor command buffer
   - Restore monitor state
   - Return to monitor prompt

3. **State Save/Restore Routines** (~60 bytes)
   - Preserve critical monitor variables
   - Restore on BASIC exit

**Total Additional Code:** ~180 bytes
**Final ROM Usage:** 3593 / 4096 bytes (87.7% utilization)

### 2. BASIC ROM ($C000-$EFFF)

**Size:** 12,288 bytes (12KB) available
**EhBASIC 2.22p5 Size:** ~8KB (typical)
**Load Address:** $C000

**Entry Points:**
- `LAB_COLD` - Cold start (complete initialization)
- `LAB_WARM` - Warm start (preserve variables) at $00 (zero page vector)

**Exit Strategy:**
BASIC will need a custom "BYE" command or system call to return to monitor:
```assembly
CMD_BYE:
    JMP RETURN_FROM_BASIC    ; Jump to monitor return handler
```

**Configuration Required:**
1. Set `Ram_base = $0300` (after BASIC variables)
2. Set `Ram_top = $C000` (before BASIC ROM)
3. Configure I/O vectors to point to monitor routines

### 3. Shared I/O Routines ($FF00-$FF11)

**Existing Monitor API:**

```assembly
; Character Output
PRINT_CHAR:         ; $FF00
    ; Input: A = character to print
    ; Preserves: X, Y
    ; Modifies: A, SCREEN_PTR
    ; Handles: Cursor positioning, scrolling, special chars

; Keyboard Input
GET_KEYSTROKE:      ; $FF09
    ; Output: A = ASCII character
    ; Preserves: X, Y
    ; Features: Cursor display, blocking wait
```

**BASIC Vector Configuration:**

```assembly
; BASIC I/O vectors in RAM ($0205-$020A)
VEC_IN   = $0205    ; Input vector (2 bytes)
VEC_OUT  = $0207    ; Output vector (2 bytes)
VEC_LD   = $0209    ; Load vector (2 bytes) [optional]
VEC_SV   = $020B    ; Save vector (2 bytes) [optional]
```

**Integration Code:**

```assembly
INIT_BASIC_IO:
    ; Set output vector to monitor PRINT_CHAR
    LDA #<PRINT_CHAR
    STA VEC_OUT
    LDA #>PRINT_CHAR
    STA VEC_OUT+1

    ; Set input vector to monitor GET_KEYSTROKE
    LDA #<GET_KEYSTROKE
    STA VEC_IN
    LDA #>GET_KEYSTROKE
    STA VEC_IN+1

    ; Set load/save vectors to stub routines (future enhancement)
    LDA #<LOAD_STUB
    STA VEC_LD
    LDA #>LOAD_STUB
    STA VEC_LD+1

    LDA #<SAVE_STUB
    STA VEC_SV
    LDA #>SAVE_STUB
    STA VEC_SV+1

    RTS
```

---

## Memory Management

### Zero Page Allocation

**Post-Relocation Layout:**

```assembly
; BASIC Zero Page ($00-$13, $5B-$BB, $BC-$E1, $EF-$FF)
; Total BASIC usage: 165 bytes

; Monitor Zero Page ($14-$3F) - RELOCATED
$14: MON_CURRADDR_LO      ; Current address pointer
$15: MON_CURRADDR_HI
$16: MON_MSG_PTR_LO       ; Message display pointer
$17: MON_MSG_PTR_HI
$18: JUMP_VECTOR_LO       ; Indirect jump vector
$19: JUMP_VECTOR_HI
$1A: SCREEN_PTR_LO        ; Screen memory pointer
$1B: SCREEN_PTR_HI
$1C: SCRL_SRC_ADDR_LO     ; Scroll source
$1D: SCRL_SRC_ADDR_HI
$1E: SCRL_DEST_ADDR_LO    ; Scroll destination
$1F: SCRL_DEST_ADDR_HI
$20: SCRL_BYTE_CNT        ; Scroll byte counter
$21: CMD_LINE_COUNT       ; Command line counter
$22: PAGE_ABORT_FLAG      ; ESC key flag
$23: RNG_SEED             ; Random seed
$24: RNG_MAX              ; Random max
$25-$34: HEX_LOOKUP_TABLE ; 16-byte hex lookup (relocated from $F0-$FF)
$35-$3F: RESERVED         ; 11 bytes spare
```

**Critical Safety Rule:**
Monitor must NEVER access $00-$13, $5B-$BB, $BC-$E1, $EF-$FF while BASIC is running.
BASIC must NEVER access $14-$3F (monitor relocated area).

### Extended RAM Allocation

```assembly
; $0200-$02FF Extended RAM (256 bytes)

; BASIC Variables ($0200-$0268) - 105 bytes
$0200: ccflag           ; CTRL-C flag
$0201: ccbyte           ; CTRL-C byte
$0202: ccnull           ; CTRL-C null timeout
$0203-$0204: VEC_CC     ; CTRL-C check vector
$0205-$0206: VEC_IN     ; Input vector
$0207-$0208: VEC_OUT    ; Output vector
$0209-$020A: VEC_LD     ; Load vector
$020B-$020C: VEC_SV     ; Save vector
$020D-$0220: (IRQ/NMI)  ; Interrupt handler space
$0221-$0268: Ibuffs     ; BASIC input buffer (71 bytes)

; Monitor Variables ($0269-$02DE) - 118 bytes
$0269: MON_CMDPTR       ; Command buffer pointer
$026A: MON_CMDLEN       ; Command buffer length
$026B: MON_MODE         ; Monitor mode
... (full list in memory-conflict-analysis.md)
$02DE: MON_LAST_CMD_LEN ; Last command length

; Available Space ($02DF-$02FF) - 33 bytes
```

**Buffer Overlap Strategy:**
- Monitor command buffer ($0200-$024F) overlaps BASIC variables
- This is ACCEPTABLE because they are mutually exclusive:
  - When monitor is active: Command buffer is used, BASIC variables inactive
  - When BASIC is active: Command buffer is unused, BASIC variables active
- **CRITICAL:** Monitor command buffer MUST be cleared when returning from BASIC

### Stack Management ($0100-$01FF)

**Shared Resource:**
Both monitor and BASIC use the 6502 hardware stack at $0100-$01FF.

**Stack Pointer Initialization:**
- Monitor initializes SP to $FF on cold start
- BASIC may use stack during execution
- On return from BASIC, monitor must verify stack integrity

**Safety Measures:**
```assembly
SAVE_MONITOR_STATE:
    TSX                 ; Transfer SP to X
    STX MONITOR_SP_SAVE ; Save current stack pointer
    ; ... other state saves
    RTS

RESTORE_MONITOR_STATE:
    LDX MONITOR_SP_SAVE ; Restore saved stack pointer
    TXS                 ; Transfer X to SP
    ; ... other state restores
    RTS
```

**Note:** Stack floor protection in BASIC (16 bytes) helps prevent stack overflow into monitor code during interrupt handling.

---

## State Management

### Monitor State Variables

**Variables to Preserve During BASIC Execution:**

```assembly
; State save area (new allocation in monitor ROM)
MONITOR_STATE_SAVE:
MONITOR_SP_SAVE:     .RES 1    ; Stack pointer
MONITOR_SCREEN_X:    .RES 1    ; Cursor X position (if needed)
MONITOR_SCREEN_Y:    .RES 1    ; Cursor Y position (if needed)
MONITOR_MODE_SAVE:   .RES 1    ; Monitor mode
; Add others as needed during implementation
```

**State Save Routine:**

```assembly
SAVE_MONITOR_STATE:
    ; Save stack pointer
    TSX
    STX MONITOR_SP_SAVE

    ; Save cursor position
    LDA CURSOR_X
    STA MONITOR_SCREEN_X
    LDA CURSOR_Y
    STA MONITOR_SCREEN_Y

    ; Save monitor mode
    LDA MON_MODE
    STA MONITOR_MODE_SAVE

    RTS
```

**State Restore Routine:**

```assembly
RESTORE_MONITOR_STATE:
    ; Restore stack pointer
    LDX MONITOR_SP_SAVE
    TXS

    ; Clear monitor command buffer
    LDX #$00
    STX MON_CMDPTR
    STX MON_CMDLEN

    ; Restore cursor position (optional, may just reset)
    LDA MONITOR_SCREEN_X
    STA CURSOR_X
    LDA MONITOR_SCREEN_Y
    STA CURSOR_Y

    ; Restore monitor mode to command mode
    LDA #MON_MODE_CMD
    STA MON_MODE

    RTS
```

### BASIC State Persistence

**Between BASIC Sessions:**
- BASIC variables are preserved in RAM unless explicitly cleared
- User can return to monitor and back to BASIC without losing programs
- This enables debugging workflow: Edit in BASIC → Test → Return to monitor → Examine memory

**BASIC Cold Start vs Warm Start:**
- **Cold Start (LAB_COLD):** Complete initialization, clear all variables
- **Warm Start (LAB_WARM):** Resume with existing program/variables

**Decision:** Initial implementation uses **COLD START** (simpler, safer)
- Future enhancement: Add flag to support warm start for program persistence

---

## Command Implementation

### B: Command Handler

**Command Syntax:** `B:`

**Implementation Location:** `src/kernel/kernel.asm`

**Integration with Command Parser:**

```assembly
; In PARSE_COMMAND routine, add new case:
CHECK_B_CMD:
    CMP #'B'
    BNE CHECK_OTHER_CMDS
    ; Check for colon
    INY
    LDA MON_CMDBUF,Y
    CMP #':'
    BNE CMD_ERROR
    JMP HANDLE_B_CMD

; New command handler
HANDLE_B_CMD:
    ; Clear screen (user experience - show we're switching modes)
    JSR CLEAR_SCREEN

    ; Print transition message
    LDA #<MSG_ENTERING_BASIC
    STA MON_MSG_PTR_LO
    LDA #>MSG_ENTERING_BASIC
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE

    ; Save monitor state
    JSR SAVE_MONITOR_STATE

    ; Initialize BASIC I/O vectors
    JSR INIT_BASIC_IO

    ; Jump to BASIC cold start
    JMP LAB_COLD

; Message strings
MSG_ENTERING_BASIC:
    .BYTE "ENTERING BASIC...", $0D, $0A, 0
```

### BASIC Return Mechanism

**Option 1: BYE Command (Recommended)**

Modify basic.asm to add a BYE command that returns to monitor:

```assembly
; In BASIC keyword table, add:
TK_BYE = TK_NMI+1    ; New token after last existing token

; In BASIC keyword string table, add:
    .BYTE "BYE", 0

; In BASIC command handler, add:
CMD_BYE:
    JMP RETURN_FROM_BASIC    ; Jump to monitor return handler
```

**Option 2: Warm Start Hook (Alternative)**

Hook BASIC's warm start vector to return to monitor:

```assembly
; During BASIC initialization, replace warm start vector
    LDA #<RETURN_FROM_BASIC
    STA LAB_WARM+1    ; Wrmjpl at $01
    LDA #>RETURN_FROM_BASIC
    STA LAB_WARM+2    ; Wrmjph at $02
```

**Chosen Approach:** Option 1 (BYE command)
- More explicit and user-friendly
- Doesn't interfere with BASIC's normal operation
- Easier to document

**Return Handler:**

```assembly
RETURN_FROM_BASIC:
    ; Clear screen (user experience)
    JSR CLEAR_SCREEN

    ; Print transition message
    LDA #<MSG_LEAVING_BASIC
    STA MON_MSG_PTR_LO
    LDA #>MSG_LEAVING_BASIC
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE

    ; Restore monitor state
    JSR RESTORE_MONITOR_STATE

    ; Clear monitor command buffer (CRITICAL!)
    LDA #$00
    LDX #MON_CMDBUF_LEN
CLEAR_CMD_BUF_LOOP:
    STA MON_CMDBUF,X
    DEX
    BPL CLEAR_CMD_BUF_LOOP

    ; Reset command buffer pointers
    LDA #$00
    STA MON_CMDPTR
    STA MON_CMDLEN

    ; Return to monitor prompt
    JMP MONITOR_PROMPT

MSG_LEAVING_BASIC:
    .BYTE "RETURNING TO MONITOR...", $0D, $0A, 0
```

---

## I/O Integration Details

### Character Output Integration

**BASIC Output Flow:**

```
BASIC PRINT Statement
       │
       ▼
BASIC Internal Formatting
       │
       ▼
Call (VEC_OUT)  [$0207-$0208]
       │
       ▼
Monitor PRINT_CHAR ($FF00)
       │
       ▼
Screen Memory Update
```

**Special Character Handling:**

| Character | ASCII | BASIC Meaning | Monitor Handling |
|-----------|-------|---------------|------------------|
| CR        | $0D   | Carriage return | Move cursor to start of line |
| LF        | $0A   | Line feed | Move cursor to next line |
| BELL      | $07   | Beep/bell | (Optional: implement sound via SID) |
| BS        | $08   | Backspace | Move cursor left, erase char |
| TAB       | $09   | Tab | Move cursor to next tab stop |

**Current PRINT_CHAR Capabilities:**
- ✓ Character display at cursor position
- ✓ Cursor advancement
- ✓ Automatic scroll at bottom of screen
- ✓ Basic special character handling (CR, LF)

**Enhancements Needed:** None critical for MVP. BASIC output will work with existing implementation.

### Character Input Integration

**BASIC Input Flow:**

```
BASIC INPUT Statement
       │
       ▼
Call (VEC_IN)  [$0205-$0206]
       │
       ▼
Monitor GET_KEYSTROKE ($FF09)
       │
       ▼
Wait for Keyboard
       │
       ▼
Return Character in A
```

**Current GET_KEYSTROKE Capabilities:**
- ✓ Blocking wait for keyboard input
- ✓ Cursor display during wait
- ✓ Returns ASCII character in A register
- ✓ Echo character to screen (standard terminal behavior)

**BASIC Requirements:**
- Input terminates on RETURN key ($0D)
- Backspace support for input editing
- Line length limit enforcement

**Assessment:** Existing GET_KEYSTROKE should work correctly with BASIC input routines.

**Potential Issue:** BASIC may have its own line editing
- Monitor GET_KEYSTROKE echoes characters
- BASIC may also try to echo
- **Solution:** BASIC's input routine should handle this correctly (EhBASIC designed for external I/O)

---

## Build System Integration

### File Structure

```
6502 Kernel/
├── src/
│   └── kernel/
│       ├── kernel.asm          (Monitor, modified for B: command)
│       ├── basic.asm           (EhBASIC 2.22p5, BYE command added)
│       └── basic_config.inc    (NEW: BASIC configuration)
├── build/
│   ├── kernel.o
│   ├── kernel.rom              (Monitor ROM at $F000-$FFFF)
│   ├── basic.o
│   └── basic.rom               (BASIC ROM at $C000-$EFFF)
├── config/
│   ├── memory.cfg              (Monitor linker config)
│   └── memory_basic.cfg        (NEW: BASIC linker config)
└── CMakeLists.txt              (Modified to build both ROMs)
```

### Build Configuration

**Monitor ROM Configuration (memory.cfg):**

```
# Existing configuration, no changes needed
MEMORY {
    ROM: start = $F000, size = $1000, fill = yes, fillval = $EA;
    JUMPS: start = $FF00, size = $12, fill = no;
    VECS: start = $FFFA, size = $6, fill = no;
}

SEGMENTS {
    CODE: load = ROM, type = ro;
    JUMPS: load = JUMPS, type = ro;
    VECS: load = VECS, type = ro;
}
```

**BASIC ROM Configuration (memory_basic.cfg) - NEW:**

```
MEMORY {
    BASIC: start = $C000, size = $3000, fill = yes, fillval = $00;
}

SEGMENTS {
    CODE: load = BASIC, type = ro;
}
```

### Assembly Commands

**Monitor Assembly:**
```bash
ca65 src/kernel/kernel.asm -o build/kernel.o
ld65 -C config/memory.cfg build/kernel.o -o build/kernel.rom
```

**BASIC Assembly:**
```bash
ca65 src/kernel/basic.asm -o build/basic.o
ld65 -C config/memory_basic.cfg build/basic.o -o build/basic.rom
```

**Combined System:**
The emulator/hardware will load both ROM files:
- kernel.rom at $F000-$FFFF (monitor)
- basic.rom at $C000-$EFFF (BASIC)

### CMakeLists.txt Integration

**Add BASIC build target:**

```cmake
# BASIC ROM build
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/basic.o
    COMMAND ca65 ${CMAKE_SOURCE_DIR}/src/kernel/basic.asm
            -o ${CMAKE_BINARY_DIR}/basic.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/basic.asm
    COMMENT "Assembling BASIC ROM..."
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/basic.rom
    COMMAND ld65 -C ${CMAKE_SOURCE_DIR}/config/memory_basic.cfg
            ${CMAKE_BINARY_DIR}/basic.o
            -o ${CMAKE_BINARY_DIR}/basic.rom
    DEPENDS ${CMAKE_BINARY_DIR}/basic.o
            ${CMAKE_SOURCE_DIR}/config/memory_basic.cfg
    COMMENT "Linking BASIC ROM..."
)

add_custom_target(basic_rom ALL
    DEPENDS ${CMAKE_BINARY_DIR}/basic.rom
)

# Add to main build target
add_dependencies(6502_Kernel basic_rom)
```

### Size Verification

**Build script should verify:**
```bash
# Check kernel ROM size
KERNEL_SIZE=$(stat -f%z build/kernel.rom 2>/dev/null || stat -c%s build/kernel.rom)
if [ $KERNEL_SIZE -gt 4096 ]; then
    echo "ERROR: Kernel ROM exceeds 4096 bytes ($KERNEL_SIZE)"
    exit 1
fi

# Check BASIC ROM size
BASIC_SIZE=$(stat -f%z build/basic.rom 2>/dev/null || stat -c%s build/basic.rom)
if [ $BASIC_SIZE -gt 12288 ]; then
    echo "ERROR: BASIC ROM exceeds 12288 bytes ($BASIC_SIZE)"
    exit 1
fi

echo "ROM sizes OK: Kernel=$KERNEL_SIZE, BASIC=$BASIC_SIZE"
```

---

## Testing Strategy

### Unit Tests

**1. Zero Page Relocation Tests**
```cpp
TEST(MonitorTests, ZeroPageRelocation) {
    // Verify new addresses are correct
    EXPECT_EQ(MON_CURRADDR_LO, 0x14);
    EXPECT_EQ(MON_CURRADDR_HI, 0x15);
    // ... test all relocated variables

    // Verify no conflicts with BASIC
    // Write BASIC pattern to BASIC zero page ranges
    // Execute monitor commands
    // Verify monitor variables unchanged
}
```

**2. Command Buffer Cleanup Tests**
```cpp
TEST(BasicIntegration, CommandBufferCleanup) {
    // Fill monitor command buffer with test pattern
    // Enter BASIC mode
    // Verify BASIC can use $0200-$024F
    // Exit BASIC mode
    // Verify monitor command buffer is cleared
    // Verify MON_CMDPTR and MON_CMDLEN are reset to 0
}
```

**3. I/O Vector Tests**
```cpp
TEST(BasicIntegration, IOVectorInit) {
    // Call INIT_BASIC_IO
    // Verify VEC_OUT points to PRINT_CHAR
    // Verify VEC_IN points to GET_KEYSTROKE
}
```

### Integration Tests

**1. BASIC Launch Test**
```cpp
TEST(BasicIntegration, LaunchAndReturn) {
    // Start monitor
    // Issue "B:" command
    // Verify BASIC prompt appears
    // Issue "BYE" command
    // Verify monitor prompt appears
}
```

**2. BASIC Program Execution Test**
```cpp
TEST(BasicIntegration, SimplePrint) {
    // Enter BASIC
    // Type: PRINT "HELLO"
    // Verify output appears on screen
    // Verify characters at correct screen memory locations
}
```

**3. Memory Corruption Test**
```cpp
TEST(BasicIntegration, NoMemoryCorruption) {
    // Fill monitor zero page with test pattern
    // Enter BASIC
    // Run BASIC program
    // Exit BASIC
    // Verify monitor zero page unchanged (only $14-$3F should have values)
}
```

### Manual Test Scenarios

**Test 1: Basic Launch**
1. Start system
2. See "READY." prompt
3. Type "B:" and press ENTER
4. Observe "ENTERING BASIC..." message
5. Observe BASIC "READY" prompt

**Test 2: Simple BASIC Program**
1. After launching BASIC (Test 1)
2. Type: `10 PRINT "HELLO WORLD"`
3. Type: `RUN`
4. Verify "HELLO WORLD" appears on screen

**Test 3: Return to Monitor**
1. After running BASIC program (Test 2)
2. Type: `BYE`
3. Observe "RETURNING TO MONITOR..." message
4. Observe "READY." monitor prompt
5. Type "H:" to verify monitor still works

**Test 4: Monitor After BASIC**
1. Complete Test 3
2. Type "R:0200-020F" to read memory
3. Verify command executes correctly
4. Type "W:8000 A9 20 4C 00 FF" to write memory
5. Verify write operation works

**Test 5: Repeated Entry/Exit**
1. Type "B:" to enter BASIC
2. Type "PRINT 123"
3. Type "BYE" to exit
4. Repeat steps 1-3 five times
5. Verify no memory corruption or crashes

---

## Error Handling

### Error Scenarios

**1. BASIC ROM Not Present**
- **Detection:** Check for BASIC signature at $C000
- **Handling:** Display "BASIC ROM NOT FOUND" and return to monitor
- **Implementation:**
```assembly
HANDLE_B_CMD:
    ; Check for BASIC ROM signature
    LDA $C000
    CMP #$4C        ; JMP opcode expected at start of BASIC
    BNE BASIC_NOT_FOUND
    ; ... continue with normal B: command

BASIC_NOT_FOUND:
    LDA #<MSG_NO_BASIC
    STA MON_MSG_PTR_LO
    LDA #>MSG_NO_BASIC
    STA MON_MSG_PTR_HI
    JSR PRINT_MESSAGE
    JMP MONITOR_PROMPT

MSG_NO_BASIC:
    .BYTE "?ERROR: BASIC ROM NOT FOUND", $0D, $0A, 0
```

**2. Stack Overflow During BASIC**
- **Detection:** BASIC includes stack floor protection
- **Handling:** BASIC error message "OUT OF MEMORY" or crash
- **Mitigation:** Document minimum required free RAM (BASIC will handle)

**3. BASIC Memory Conflict**
- **Prevention:** Relocation strategy resolves all conflicts
- **Detection:** Memory corruption would manifest as crashes
- **Testing:** Extensive memory pattern tests during development

---

## Documentation Requirements

### User Documentation

**New Command: B:**

```markdown
### B: - Enter BASIC Interpreter

**Syntax:** `B:`

**Description:**
Launches the EhBASIC 2.22p5 interpreter, allowing you to write and execute
BASIC programs. BASIC provides a higher-level programming environment compared
to raw assembly language.

**Usage:**
1. Type `B:` at the monitor prompt
2. Press ENTER
3. You will see "ENTERING BASIC..." followed by the BASIC "READY" prompt
4. Type BASIC commands or programs
5. Type `BYE` to return to the monitor

**Examples:**
```
READY.
> B:
ENTERING BASIC...

READY
10 PRINT "HELLO WORLD"
20 FOR I=1 TO 10
30 PRINT I
40 NEXT I
RUN
HELLO WORLD
1
2
...
10
READY
BYE
RETURNING TO MONITOR...

READY.
>
```

**BASIC Commands:**
- Standard BASIC commands: PRINT, INPUT, FOR, NEXT, IF, THEN, GOTO, GOSUB, RETURN
- Variable assignment: A=10, B$="HELLO"
- Functions: INT, ABS, SGN, SIN, COS, TAN, etc.
- Full EhBASIC 2.22p5 command set

**Returning to Monitor:**
- Type `BYE` at the BASIC prompt to return to the monitor
- Your BASIC program will be lost (cold start only in MVP)

**See Also:**
- EhBASIC documentation: http://www.6502.org/users/mycorner/6502/ehbasic/index.html
```

### Technical Documentation Updates

**Files to Update:**
1. `docs/kernel_memory_map.md` - Add relocated zero page layout
2. `docs/kernel_flow.md` - Add B: command flow
3. `docs/kernel_command_infrastructure.md` - Add B: command details
4. `docs/command_help.md` - Add B: command reference
5. `README.md` - Mention BASIC interpreter feature

**New Document:**
6. `docs/basic_command.md` - Complete B: command and BASIC usage guide

---

## Implementation Timeline

### Phase 1: Monitor Relocation (Estimated: 4-6 hours)
- [ ] Update zero page constant definitions in kernel.asm
- [ ] Update all references to relocated variables
- [ ] Update HEX_LOOKUP_TABLE initialization
- [ ] Compile and test each monitor command individually
- [ ] Run full monitor test suite
- [ ] Update kernel_memory_map.md

### Phase 2: B: Command Implementation (Estimated: 2-3 hours)
- [ ] Implement HANDLE_B_CMD routine
- [ ] Implement SAVE_MONITOR_STATE routine
- [ ] Implement RESTORE_MONITOR_STATE routine
- [ ] Implement INIT_BASIC_IO routine
- [ ] Add B: to command parser
- [ ] Add messages (MSG_ENTERING_BASIC, etc.)
- [ ] Test B: command (stub jump to test address first)

### Phase 3: BASIC Integration (Estimated: 3-4 hours)
- [ ] Review basic.asm configuration section
- [ ] Set Ram_base = $0300
- [ ] Set Ram_top = $C000
- [ ] Add BYE command to BASIC keyword table
- [ ] Implement CMD_BYE handler
- [ ] Create memory_basic.cfg linker configuration
- [ ] Add BASIC build target to CMakeLists.txt
- [ ] Build and test BASIC ROM

### Phase 4: Testing and Validation (Estimated: 3-4 hours)
- [ ] Unit tests for zero page relocation
- [ ] Unit tests for command buffer cleanup
- [ ] Integration test for B: command
- [ ] Manual testing of BASIC programs
- [ ] Stress testing (repeated entry/exit)
- [ ] Memory corruption verification

### Phase 5: Documentation (Estimated: 2-3 hours)
- [ ] Create docs/basic_command.md
- [ ] Update kernel_memory_map.md
- [ ] Update kernel_flow.md
- [ ] Update kernel_command_infrastructure.md
- [ ] Update command_help.md
- [ ] Update README.md
- [ ] Update kernel.asm header comments

**Total Estimated Time:** 14-20 hours

---

## Success Criteria

### Functional Requirements
✓ User can enter BASIC from monitor with "B:" command
✓ BASIC interpreter runs and accepts programs
✓ User can execute BASIC programs (PRINT, FOR, etc.)
✓ User can return to monitor with "BYE" command
✓ Monitor functions correctly after BASIC usage

### Technical Requirements
✓ Zero page conflicts completely resolved (26 addresses relocated)
✓ Extended RAM overlap handled with buffer cleanup
✓ I/O integration functional (PRINT_CHAR, GET_KEYSTROKE)
✓ Build system produces both kernel.rom and basic.rom
✓ ROM sizes within limits (kernel ≤ 4KB, BASIC ≤ 12KB)

### Quality Requirements
✓ No memory corruption during BASIC operation
✓ No regressions in existing monitor commands
✓ Clean state after returning from BASIC
✓ Comprehensive test coverage (unit + integration)
✓ Complete documentation updates

---

## Future Enhancements (Out of Scope for MVP)

1. **BASIC Program Persistence**
   - Implement warm start to preserve programs between sessions
   - Add flag to choose cold vs warm start

2. **Load/Save Integration**
   - Implement VEC_LD and VEC_SV vectors
   - Allow BASIC to use monitor's L: and S: commands
   - Enable saving BASIC programs to storage

3. **Enhanced I/O**
   - Color support for BASIC PRINT statements
   - Graphics commands for sprite control
   - Sound commands for SID chip

4. **Debugging Integration**
   - Break to monitor from running BASIC program
   - Examine BASIC variables from monitor
   - Set breakpoints in BASIC code

5. **Extended BASIC Commands**
   - Monitor utility commands callable from BASIC
   - Memory dump from BASIC (PEEK, POKE already exist)
   - System information commands

---

**Document Status:** COMPLETE
**Ready for Implementation:** YES
**Estimated Development Time:** 14-20 hours
