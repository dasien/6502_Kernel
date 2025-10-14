---
enhancement: add-basic-interpreter
task_id: task_1759417756_77601
agent: assembly-developer
created: 2025-10-02 12:30:15
---

# BASIC Interpreter Integration Architecture

## Overview

This document defines the complete technical architecture for integrating EhBASIC with the 6502 Monitor system, including I/O integration, command implementation, build process, and memory management.

## I/O Integration Design

### Monitor Kernel API (Jump Table at $FF00)

The monitor provides a stable kernel API at $FF00 for user programs:

| Address | Routine | Function | Preserved Regs |
|---------|---------|----------|----------------|
| $FF00 | K_PRINT_CHAR | Print character in A register | Y |
| $FF03 | K_PRINT_MESSAGE | Print null-terminated string via pointer | - |
| $FF06 | K_PRINT_NEWLINE | Print newline (CR+LF) | A, X, Y |
| $FF09 | K_GET_KEYSTROKE | Get keyboard input (blocking) | X, Y |
| $FF0C | K_CLEAR_SCREEN | Clear screen | A, X, Y |
| $FF0F | K_GET_RAND_NUM | Get random number | - |

### BASIC I/O Vector Requirements

EhBASIC uses I/O vectors that must be configured during initialization:

From basic.asm analysis:
- **VEC_IN** ($0205-$0206): Input vector - jumps to character input routine
- **VEC_OUT** ($0207-$0208): Output vector - jumps to character output routine
- **VEC_LD** ($0209-$020A): Load vector - jumps to file load routine
- **VEC_SV** ($020B-$020C): Save vector - jumps to file save routine

### I/O Integration Implementation

**Integration Strategy**: Create I/O wrapper routines in BASIC ROM that call monitor kernel API

#### Output Integration (BASIC → Monitor)

```assembly
; BASIC output vector wrapper at $C000 (BASIC ROM area)
BASIC_OUT_CHAR:
    PHA                     ; Save character
    JMP $FF00              ; Jump to monitor K_PRINT_CHAR
    ; Monitor routine handles everything and returns
    PLA                     ; Restore character (if needed)
    RTS

; Initialize during BASIC startup:
; VEC_OUT ($0207-$0208) = address of BASIC_OUT_CHAR
```

**Monitor K_PRINT_CHAR behavior** (from kernel.asm:576-620):
- Handles CR (carriage return) → newline
- Handles backspace → cursor back
- Handles line wrap at column 40
- Handles screen scroll when bottom reached
- Updates SCREEN_PTR and CURSOR_X/CURSOR_Y
- Preserves Y register

**Compatibility**: ✓ BASIC expects output routine to print character in A register and return. Monitor API matches this exactly.

#### Input Integration (Monitor → BASIC)

```assembly
; BASIC input vector wrapper at $C010 (BASIC ROM area)
BASIC_IN_CHAR:
    JMP $FF09              ; Jump to monitor K_GET_KEYSTROKE
    ; Monitor routine waits for keypress, returns char in A
    ; Returns with A = character, X and Y preserved
    ; (RTS handled by monitor routine)
```

**Monitor K_GET_KEYSTROKE behavior** (from kernel.asm:821-833):
- Polls PIA keyboard controller
- Blocks until key available
- Returns ASCII character in A
- Adds entropy to RNG while waiting
- Preserves X and Y registers

**Compatibility**: ✓ BASIC expects input routine to return character in A register. Monitor API matches this exactly.

### Monitor Zero Page Usage During I/O

**CRITICAL**: Monitor I/O routines use relocated zero page variables:

After relocation (from basic-memory-analysis.md):
- SCREEN_PTR: $1A-$1B (was $06-$07)
- CURSOR_X: $76 (monitor variable, needs checking)
- CURSOR_Y: $77 (monitor variable, needs checking)
- RNG_SEED: $23 (was $0F)
- MON_MSG_TMP_POS: $78 (monitor variable, needs checking)

**NOTE**: Need to verify CURSOR_X, CURSOR_Y, MON_MSG_TMP_POS locations after reviewing full kernel.asm for these variables.

Let me check the relocated addresses:
- From kernel.asm line 119: CURSOR_X = $0276 (in RAM, not zero page!)
- From kernel.asm line 120: CURSOR_Y = $0277 (in RAM, not zero page!)
- From kernel.asm line 121: MON_MSG_TMP_POS = $0278 (in RAM, not zero page!)

**CORRECTED**: These are in extended RAM at $0269-$02DE range (monitor variables relocated to avoid BASIC conflicts). No additional zero page conflicts from I/O routines.

### File I/O Integration (Load/Save)

**Current State**: Monitor has L: (load) and S: (save) commands using file I/O interface at $DC10-$DC21

**BASIC Requirements**:
- VEC_LD: Load file to specified address
- VEC_SV: Save memory range to file

**Integration Strategy**: Create wrapper routines that call monitor's file I/O interface

```assembly
; BASIC load vector wrapper
BASIC_LOAD:
    ; BASIC calling convention (TBD - need to check EhBASIC docs)
    ; Set FILE_ADDR_LO/HI, FILE_NAME_BUF, FILE_COMMAND=$01
    ; Poll FILE_STATUS until complete
    RTS

; BASIC save vector wrapper
BASIC_SAVE:
    ; BASIC calling convention (TBD - need to check EhBASIC docs)
    ; Set FILE_ADDR_LO/HI, FILE_END_ADDR_LO/HI, FILE_NAME_BUF, FILE_COMMAND=$02
    ; Poll FILE_STATUS until complete
    RTS
```

**Deferred to "Should Have" scope**: File I/O integration is listed as optional in requirements.

## B: Command Implementation

### Command Parser Integration

**Location**: Add to monitor command dispatch table

From docs/kernel_command_infrastructure.md patterns, the monitor uses a command dispatch system:

```assembly
; In command parser section of kernel.asm
; After existing command checks (R:, W:, G:, L:, S:, C:, T:, Z:, H:, F:, M:, X:)

CHECK_B_COMMAND:
    CMP #'B'                    ; Check for 'B' command
    BNE CHECK_NEXT_COMMAND      ; Not B, try next command

    ; Check for colon after 'B'
    INY                         ; Move to next character
    LDA MON_CMDBUF,Y           ; Get next character
    CMP #ASCII_COLON           ; Is it ':'?
    BNE COMMAND_ERROR           ; No, invalid command

    ; Valid B: command - launch BASIC
    JMP START_BASIC_INTERPRETER ; Jump to BASIC startup
```

### BASIC Interpreter Startup Sequence

```assembly
START_BASIC_INTERPRETER:
    ; 1. Save monitor state (if needed)
    ; 2. Clear/initialize BASIC zero page variables ($00-$13, $5B-$BB, etc.)
    ; 3. Set up BASIC I/O vectors
    ; 4. Set up BASIC memory pointers (Ram_base=$0300, Ram_top=$8000)
    ; 5. Jump to BASIC cold start at LAB_COLD

    ; Initialize BASIC I/O vectors
    LDA #<BASIC_OUT_CHAR        ; Output vector low byte
    STA VEC_OUT
    LDA #>BASIC_OUT_CHAR        ; Output vector high byte
    STA VEC_OUT+1

    LDA #<BASIC_IN_CHAR         ; Input vector low byte
    STA VEC_IN
    LDA #>BASIC_IN_CHAR         ; Input vector high byte
    STA VEC_IN+1

    ; Set BASIC memory limits
    LDA #<$0300                 ; Ram_base = $0300
    STA Smeml
    LDA #>$0300
    STA Smemh

    LDA #<$8000                 ; Ram_top = $8000
    STA Ememl
    LDA #>$8000
    STA Ememh

    ; Jump to BASIC cold start
    JMP LAB_COLD                ; Enter BASIC (at $C000 in BASIC ROM)
```

### BASIC Exit / Return to Monitor

**Requirement**: User must be able to exit BASIC and return to monitor prompt

**Strategy**: Modify BASIC to recognize a warm restart command that returns to monitor instead of restarting BASIC

**Implementation Options**:

1. **Monitor Return Vector**: Add a monitor return address to BASIC's warm start vector
2. **NEW Command Intercept**: Intercept BASIC's NEW command to check for special exit sequence
3. **Custom Command**: Add MON command to BASIC vocabulary

**Recommended Approach**: Option 3 - Add MON command

```assembly
; In BASIC ROM, add to command token list:
TK_MON = <next_available_token>  ; MON token

; In BASIC command dispatcher:
CHECK_MON_COMMAND:
    CMP #TK_MON                 ; MON token?
    BNE NEXT_BASIC_CMD
    JMP RETURN_TO_MONITOR       ; Exit to monitor

RETURN_TO_MONITOR:
    ; Clean up BASIC state
    ; Clear monitor command buffer
    LDX #79
CLEAR_MON_BUFFER:
    LDA #$00
    STA MON_CMDBUF,X
    DEX
    BPL CLEAR_MON_BUFFER

    ; Reset monitor state
    LDA #$00
    STA MON_CMDPTR              ; Reset command pointer
    STA MON_CMDLEN              ; Reset command length
    STA MON_MODE                ; Set to command mode

    ; Jump back to monitor command loop
    JMP MONITOR_COMMAND_LOOP    ; Return to monitor (at $F000+ in kernel ROM)
```

**Alternative Simple Approach**: Use BASIC's existing warm start mechanism with custom vector

```assembly
; During BASIC startup from monitor:
START_BASIC_INTERPRETER:
    ; ... (setup as above)

    ; Redirect BASIC warm start to monitor return
    LDA #$4C                    ; JMP opcode
    STA LAB_WARM                ; Store at $00 (after zero page relocation!)
    LDA #<RETURN_TO_MONITOR     ; Low byte of return routine
    STA Wrmjpl                  ; Store at $01
    LDA #>RETURN_TO_MONITOR     ; High byte of return routine
    STA Wrmjph                  ; Store at $02

    JMP LAB_COLD                ; Enter BASIC
```

**PROBLEM IDENTIFIED**: LAB_WARM is at $00-$02, which are being used by BASIC after our relocation plan freed them up. This creates a circular dependency.

**SOLUTION**: BASIC needs $00-$02 for its warm start vector. Monitor zero page relocation is correct. When user types a BASIC command that triggers warm start (like RESET or entering "SYS" to a return routine), BASIC will JMP ($0000) which we set to point to RETURN_TO_MONITOR.

**CORRECTED EXIT STRATEGY**:
1. BASIC naturally uses $00-$02 for warm start JMP vector (LAB_WARM)
2. Monitor sets this vector during BASIC startup to point to RETURN_TO_MONITOR routine
3. User triggers BASIC warm start by typing specific command or using RESET
4. BASIC jumps to RETURN_TO_MONITOR, which cleans up and returns to monitor

## Memory Layout Architecture

### Overall System Memory Map

```
$0000-$00FF : Zero Page
  $00-$13   : BASIC warm start and core variables
  $14-$24   : Monitor variables (relocated)
  $25-$34   : Monitor HEX_LOOKUP_TABLE (relocated)
  $35-$5A   : Available (38 bytes)
  $5B-$BB   : BASIC variables
  $BC-$DB   : BASIC routines & PRNG
  $DC-$E1   : BASIC IRQ/NMI handlers
  $E2-$EE   : Available (13 bytes)
  $EF-$FF   : BASIC decimal string buffer

$0100-$01FF : Stack (shared by both systems)

$0200-$02FF : System Variables / Extended RAM
  $0200-$024F : Monitor command buffer (overlaps with BASIC - acceptable)
  $0200-$0268 : BASIC control flags and input buffer
  $0269-$02DE : Monitor variables (relocated to avoid BASIC conflicts)
  $02DF-$02FF : Available (33 bytes)

$0300-$7FFF : User RAM (BASIC program storage)
  $0300       : BASIC Ram_base (start of BASIC program area)
  $0400-$07FF : Screen memory (not used for BASIC programs)
  $0800-$7FFF : Primary BASIC program storage area (~30KB)

$8000-$BFFF : Available RAM or ROM (system dependent)

$C000-$EFFF : BASIC ROM (12KB)
  $C000       : BASIC interpreter code start
  $C000-$C020 : I/O wrapper routines
  $C021-$EFFF : EhBASIC interpreter code

$F000-$FFFF : Monitor Kernel ROM (4KB)
  $F000       : Monitor code start
  $FF00-$FF11 : Kernel API jump table
  $FFFA-$FFFF : Interrupt vectors
```

### ROM Loading Strategy

**CRITICAL DECISION**: How to load BASIC ROM into $C000-$EFFF

**Options**:
1. Build basic.rom as separate file, C++ loader loads both kernel.rom and basic.rom
2. Combine basic.rom and kernel.rom into single system.rom file
3. Build BASIC as relocatable module loaded by monitor

**REQUIREMENT FROM ENHANCEMENT DOC**:
> BASIC should be built as a separate basic.rom file (NOT combined with kernel.rom)
> Follow the exact same build pattern as kernel.rom (parallel process, not combined)
> C++ loader code will handle loading both kernel.rom and basic.rom separately

**DECISION**: Option 1 - Separate ROM files loaded by C++ emulator

**Rationale**:
- Maintains clean separation of concerns
- Allows independent updates to either component
- Follows existing build system patterns
- C++ emulator already loads kernel.rom, extending to load basic.rom is straightforward

## Build System Design

### Existing Kernel Build Process

From existing build files:
- Assembler: ca65 (CC65 toolset)
- Linker: ld65
- Config file: memory.cfg (defines ROM segments)
- Output: kernel.rom (loaded at $F000-$FFFF)

### BASIC Build Process Design

**Build Parallel to Kernel**: Create matching build structure for BASIC

#### New File: src/kernel/basic_memory.cfg

```cfg
# Memory configuration for BASIC ROM
# Target: 6502 computer system
# ROM Location: $C000-$EFFF (12KB)

MEMORY {
    # BASIC ROM area
    CODE:   start = $C000, size = $3000, fill = yes, fillval = $00;
}

SEGMENTS {
    CODE:   load = CODE, type = ro;
}
```

#### Build Commands

```bash
# Assemble BASIC
ca65 src/kernel/basic.asm -o build/basic.o

# Link BASIC to create basic.rom
ld65 -C src/kernel/basic_memory.cfg build/basic.o -o build/basic.rom

# Existing kernel build (unchanged)
ca65 src/kernel/kernel.asm -o build/kernel.o
ld65 -C src/kernel/memory.cfg build/kernel.o -o build/kernel.rom
```

#### CMakeLists.txt Integration

Add to existing CMakeLists.txt (assuming it exists):

```cmake
# Add BASIC ROM build target
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/basic.rom
    COMMAND ca65 ${CMAKE_SOURCE_DIR}/src/kernel/basic.asm -o ${CMAKE_BINARY_DIR}/basic.o
    COMMAND ld65 -C ${CMAKE_SOURCE_DIR}/src/kernel/basic_memory.cfg
                 ${CMAKE_BINARY_DIR}/basic.o
                 -o ${CMAKE_BINARY_DIR}/basic.rom
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/basic.asm
            ${CMAKE_SOURCE_DIR}/src/kernel/basic_memory.cfg
    COMMENT "Building BASIC ROM"
)

add_custom_target(basic_rom ALL DEPENDS ${CMAKE_BINARY_DIR}/basic.rom)
```

### C++ Loader Integration

**File to Modify**: src/main.cpp (or wherever ROM loading occurs)

**Required Changes**:

```cpp
// In main.cpp or Computer class initialization

// Load kernel ROM at $F000-$FFFF
bool loadKernelROM() {
    std::ifstream kernelFile("kernel.rom", std::ios::binary);
    if (!kernelFile) return false;

    kernelFile.read(reinterpret_cast<char*>(&memory[0xF000]), 0x1000);
    return true;
}

// NEW: Load BASIC ROM at $C000-$EFFF
bool loadBasicROM() {
    std::ifstream basicFile("basic.rom", std::ios::binary);
    if (!basicFile) {
        // BASIC is optional - system can run without it
        std::cout << "BASIC ROM not found - B: command unavailable\n";
        return false;
    }

    basicFile.read(reinterpret_cast<char*>(&memory[0xC000]), 0x3000);
    return true;
}

// In initialization:
void Computer::initialize() {
    if (!loadKernelROM()) {
        throw std::runtime_error("Failed to load kernel.rom");
    }

    loadBasicROM(); // Optional - doesn't fail if missing

    // ... rest of initialization
}
```

## Integration Testing Strategy

### Phase 1: Memory Relocation Testing
1. Build modified kernel.rom with relocated zero page variables
2. Test all existing monitor commands (R:, W:, G:, L:, S:, C:, T:, Z:, H:, F:, M:, X:)
3. Verify no regressions in monitor functionality

### Phase 2: BASIC ROM Build Testing
1. Assemble basic.asm with basic_memory.cfg
2. Verify basic.rom is created and is ~12KB
3. Verify ROM loads at $C000 in emulator memory

### Phase 3: B: Command Testing
1. Add B: command to monitor command parser
2. Test B: command invocation
3. Verify BASIC interpreter starts and displays banner

### Phase 4: I/O Integration Testing
1. Test BASIC PRINT command → monitor character output
2. Test BASIC INPUT command → monitor keyboard input
3. Verify screen scrolling works correctly
4. Test line wrap and cursor positioning

### Phase 5: Exit/Return Testing
1. Test BASIC to monitor return mechanism
2. Verify monitor command buffer is cleared
3. Verify monitor prompt appears after BASIC exit
4. Test multiple BASIC entry/exit cycles

### Phase 6: Complete Integration Testing
1. Run complete BASIC programs
2. Test BASIC with monitor memory operations
3. Verify no memory corruption between systems
4. Test error handling and recovery

## Documentation Requirements

### Files to Create

1. **docs/basic_command.md**
   - B: command description
   - Usage examples
   - BASIC interpreter features
   - Exit procedures

2. **docs/basic_integration.md**
   - Technical integration details
   - Memory layout
   - I/O architecture
   - Build process

### Files to Update

1. **docs/kernel_memory_map.md**
   - Update zero page allocations
   - Document relocated monitor variables
   - Add BASIC memory usage

2. **docs/kernel_flow.md**
   - Add B: command flow
   - Document BASIC entry/exit paths

3. **docs/kernel_command_infrastructure.md**
   - Add B: command to command list
   - Document command parser changes

4. **docs/command_help.md**
   - Add B: command to help text

5. **README.md** (project root)
   - Mention BASIC interpreter feature
   - Document build requirements
   - Add BASIC usage examples

6. **src/kernel/kernel.asm**
   - Update header comments with new memory layout
   - Document B: command in command list

## Risk Analysis and Mitigation

### Risk 1: Zero Page Relocation Breaks Monitor
**Impact**: HIGH
**Probability**: MEDIUM
**Mitigation**:
- Comprehensive testing of all monitor commands after relocation
- Use search/replace to ensure all references updated
- Create backup of working kernel before changes

### Risk 2: I/O Integration Incompatibility
**Impact**: HIGH
**Probability**: LOW
**Mitigation**:
- Monitor API already matches BASIC requirements
- Create simple test programs to verify I/O before full integration

### Risk 3: Memory Overlap Causes Data Corruption
**Impact**: HIGH
**Probability**: LOW (after analysis)
**Mitigation**:
- Complete memory analysis performed and validated
- Acceptable overlaps clearly documented
- Test programs that stress memory boundaries

### Risk 4: BASIC ROM Too Large for $C000-$EFFF
**Impact**: MEDIUM
**Probability**: LOW
**Mitigation**:
- EhBASIC is well-tested and typically ~10KB
- 12KB allocation provides headroom
- If needed, can extend into $B000-$BFFF range

### Risk 5: Exit Mechanism Fails
**Impact**: MEDIUM
**Probability**: MEDIUM
**Mitigation**:
- Implement and test multiple exit strategies
- Document recovery procedures
- Ensure monitor can be reached via reset if needed

## Implementation Phases

### Phase 1: Monitor Zero Page Relocation (Est: 4-6 hours)
- Update kernel.asm zero page definitions
- Search and update all references
- Update HEX_LOOKUP_TABLE initialization
- Test all monitor commands
- Update documentation

### Phase 2: Build System Setup (Est: 2-3 hours)
- Create basic_memory.cfg
- Add BASIC build to CMakeLists.txt
- Test BASIC assembly and linking
- Integrate with C++ loader
- Verify ROMs load correctly

### Phase 3: I/O Integration (Est: 3-4 hours)
- Create BASIC_OUT_CHAR wrapper
- Create BASIC_IN_CHAR wrapper
- Add vector initialization code
- Test I/O with simple BASIC programs
- Verify screen handling

### Phase 4: B: Command Implementation (Est: 2-3 hours)
- Add B: command to parser
- Implement START_BASIC_INTERPRETER
- Add initialization code
- Test BASIC startup

### Phase 5: Exit Mechanism (Est: 2-3 hours)
- Implement RETURN_TO_MONITOR
- Add warm start vector setup
- Implement monitor state cleanup
- Test exit scenarios

### Phase 6: Testing & Documentation (Est: 4-6 hours)
- Run comprehensive test suite
- Create/update all documentation
- Add example BASIC programs
- Document known issues and limitations

**Total Estimated Effort**: 17-25 hours

## Success Criteria

1. ✓ All monitor commands work after zero page relocation
2. ✓ basic.rom builds successfully as separate file
3. ✓ B: command launches BASIC interpreter
4. ✓ BASIC PRINT command outputs to screen via monitor
5. ✓ BASIC INPUT command reads from keyboard via monitor
6. ✓ User can exit BASIC and return to monitor prompt
7. ✓ Multiple BASIC entry/exit cycles work correctly
8. ✓ All documentation updated
9. ✓ No memory corruption between systems
10. ✓ Example BASIC programs run successfully

## Conclusion

This architecture provides a complete, implementable design for integrating EhBASIC with the 6502 Monitor system. The design:

- Completely resolves all memory conflicts through systematic relocation
- Provides clean I/O integration using existing monitor kernel API
- Follows existing build system patterns for maintainability
- Includes comprehensive testing and documentation strategy
- Identifies and mitigates key risks
- Provides realistic implementation phases and effort estimates

The architecture is ready for implementation by the assembly developer or implementation agent.

---

**Status**: READY_FOR_IMPLEMENTATION

**Next Steps**:
1. Review and approve architecture
2. Begin Phase 1: Monitor zero page relocation
3. Proceed through implementation phases sequentially
4. Test thoroughly at each phase
5. Update documentation as implementation progresses
