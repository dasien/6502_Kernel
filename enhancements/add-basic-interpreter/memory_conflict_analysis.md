---
enhancement: add-basic-interpreter
task_id: task_1759420271_78842
agent: assembly-developer
created: 2025-10-02 14:25:00
---

# BASIC Interpreter Memory Conflict Analysis

## Executive Summary

This document provides a comprehensive analysis of memory conflicts between the existing Monitor program and the EhBASIC interpreter. The analysis identifies **CRITICAL CONFLICTS** in zero page memory that must be resolved before integration.

**Key Finding**: Monitor variables have already been relocated to $0269-$02DE to avoid conflicts with BASIC's $0200-$0268 usage. The command buffer overlap ($0200-$024F) is acceptable since it's not needed during BASIC execution.

## Methodology

This analysis follows the mandatory systematic approach:
1. Complete extraction of BASIC zero page variable definitions
2. Tracing of all calculated address dependencies (VAR+1, VAR+2 patterns)
3. Cross-reference with Monitor memory map
4. Conflict identification and resolution planning

## BASIC Zero Page Memory Map (Complete)

### Direct Assignments (Base Addresses)

| Address | Variable | Purpose | Calculated Addresses |
|---------|----------|---------|---------------------|
| $00 | LAB_WARM | BASIC warm start entry | +1: Wrmjpl, +2: Wrmjph |
| $0A | Usrjmp | USR function JMP | +1: Usrjpl, +2: Usrjph |
| $0D | Nullct | Nulls output after line | - |
| $0E | TPos | Terminal position byte | - |
| $0F | TWidth | Terminal width byte | - |
| $10 | Iclim | Input column limit | - |
| $11 | Itempl | Temp integer low byte | +1: Itemph ($12), +2: nums_3 ($13) |
| $5B | Srchc/Temp3 | Search char/temp byte | - |
| $5C | Scnquo/Asrch | Scan quotes flag | Also: XOAw_h |
| $5D | Ibptr/Dimcnt/Tindx | Input buffer pointer | - |
| $5E | Defdim | Default DIM flag | - |
| $5F | Dtypef | Data type flag | - |
| $60 | Oquote/Gclctd | Open quote/GC flag | - |
| $61 | Sufnxf | Subscript/FNX flag | - |
| $62 | Imode | Input mode flag | - |
| $63 | Cflag | Comparison eval flag | - |
| $64 | TabSiz | TAB step size | - |
| $65 | next_s | Next descriptor stack addr | - |
| $66 | last_sl | Last descriptor stack low | +1: last_sh ($67) |
| $68 | des_sk | Descriptor stack start | Through $70 (9 bytes) |
| $71 | ut1_pl | Utility pointer 1 low | +1: ut1_ph ($72) |
| $73 | ut2_pl | Utility pointer 2 low | +1: ut2_ph ($74) |
| $75 | FACt_1 | FAC temp mantissa1 | +1: FACt_2 ($76), +2: FACt_3 ($77) |
| $78 | TempB | Temp page 0 byte | - |
| $79 | Smeml | Start of mem low | +1: Smemh ($7A) |
| $7B | Svarl | Start of vars low | +1: Svarh ($7C) |
| $7D | Sarryl | Var mem end low | +1: Sarryh ($7E) |
| $7F | Earryl | Array mem end low | +1: Earryh ($80) |
| $81 | Sstorl | String storage low | +1: Sstorh ($82) |
| $83 | Sutill | String utility ptr low | +1: Sutilh ($84) |
| $85 | Ememl | End of mem low | +1: Ememh ($86) |
| $87 | Clinel | Current line low | +1: Clineh ($88) |
| $89 | Blinel | Break line low | +1: Blineh ($8A) |
| $8B | Cpntrl | Continue pointer low | +1: Cpntrh ($8C) |
| $8D | Dlinel | Current DATA line low | +1: Dlineh ($8E) |
| $8F | Dptrl | DATA pointer low | +1: Dptrh ($90) |
| $91 | Rdptrl | Read pointer low | +1: Rdptrh ($92) |
| $93 | Varnm1 | Current var name 1st | +1: Varnm2 ($94) |
| $95 | Cvaral | Current var addr low | +1: Cvarah ($96) |
| $97 | Frnxtl | FOR/NEXT ptr low | +1: Frnxth ($98) |
| $99 | prstk | Precedence stacked flag | Through $9A (2 bytes) |
| $9B | comp_f | Compare function flag | - |
| $9C | func_l | Function pointer low | +1: func_h ($9D) |
| $9E | des_2l | String desc 2 ptr low | +1: des_2h ($9F) |
| $A0 | g_step | Garbage collect step size | - |
| $A1 | Fnxjmp | Jump vector for functions | +1: Fnxjpl ($A2), +2: Fnxjph ($A3) |
| $A3 | FAC2_r | FAC2 rounding byte | - |
| $A4 | Adatal | Array data ptr low | +1: Adatah ($A5) |
| $A6 | Obendl | Old block end ptr low | +1: Obendh ($A7) |
| $A8 | numexp/numbit | Number exponent count | - |
| $A9 | expcnt | Exponent count | - |
| $AA | numdpf (multi-use) | Decimal point flag | Many aliases |
| $AB | expneg (multi-use) | Exponent -ve flag | Many aliases |
| $AC | FAC1_e | FAC1 exponent | +1: FAC1_1 ($AD), +2: FAC1_2 ($AE), +3: FAC1_3 ($AF), +4: FAC1_s ($B0) |
| $B1 | negnum/numcon | -ve flag/const count | - |
| $B2 | FAC1_o | FAC1 overflow byte | - |
| $B3 | FAC2_e | FAC2 exponent | +1: FAC2_1 ($B4), +2: FAC2_2 ($B5), +3: FAC2_3 ($B6), +4: FAC2_s ($B7) |
| $B8 | FAC_sc/ssptr_l | FAC sign compare | - |
| $B9 | FAC1_r/ssptr_h | FAC1 rounding | - |
| $BA | csidx/Asptl | Line crunch save index | +1: Aspth ($BB) |
| $BC | LAB_IGBY | Get next BASIC byte sub | Through $C1 (6 bytes) |
| $C2 | LAB_GBYT | Get current BASIC byte | - |
| $C3 | Bpntrl | BASIC execute ptr low | +1: Bpntrh ($C4) |
| $D8 | Rbyte4 | Extra PRNG byte | +1: Rbyte1 ($D9), +2: Rbyte2 ($DA), +3: Rbyte3 ($DB) |
| $DC | NmiBase | NMI handler flags | Through $DE (3 bytes) |
| $DF | IrqBase | IRQ handler flags | Through $E1 (3 bytes) |
| $EF | Decss | Decimal string start | +1: Decssp1 ($F0), extends to $FF |

### BASIC Zero Page Usage Summary

**Total Zero Page Usage**:
- $00-$13 (20 bytes)
- $5B-$68 (14 bytes)
- $68-$70 (descriptor stack, 9 bytes)
- $71-$BB (75 bytes)
- $BC-$C4 (9 bytes - subroutine code)
- $D8-$E1 (10 bytes)
- $EF-$FF (17 bytes - decimal string buffer)

**Critical Note**: The range $EF-$FF is used by BASIC for decimal-to-string conversion. Comments in basic.asm line 313 indicate "$FF decimal string end", confirming the entire 17-byte range is active.

## Monitor Zero Page Memory Map (Current)

| Address | Variable | Purpose |
|---------|----------|---------|
| $00-$01 | MON_CURRADDR_LO/HI | Current address pointer |
| $02-$03 | MON_MSG_PTR_LO/HI | Message display pointer |
| $04-$05 | JUMP_VECTOR | Indirect jump vector |
| $06-$07 | SCREEN_PTR_LO/HI | Screen memory pointer |
| $08-$09 | SCRL_SRC_ADDR_LO/HI | Scroll source address |
| $0A-$0B | SCRL_DEST_ADDR_LO/HI | Scroll dest address |
| $0C | SCRL_BYTE_CNT | Scroll byte counter |
| $0D | CMD_LINE_COUNT | Command line counter |
| $0E | PAGE_ABORT_FLAG | Page abort flag |
| $0F | RNG_SEED | Random number seed |
| $10 | RNG_MAX | Random number max |
| $F0-$FF | HEX_LOOKUP_TABLE | Hex digit lookup (16 bytes) |

**Total Monitor Zero Page Usage**: 17 bytes ($00-$10) + 16 bytes ($F0-$FF) = 33 bytes

## Zero Page Conflict Analysis

### Complete Conflict Table

| Address | Monitor Usage | BASIC Usage | Conflict? | Severity |
|---------|---------------|-------------|-----------|----------|
| $00 | MON_CURRADDR_LO | LAB_WARM | **YES** | CRITICAL |
| $01 | MON_CURRADDR_HI | Wrmjpl (LAB_WARM+1) | **YES** | CRITICAL |
| $02 | MON_MSG_PTR_LO | Wrmjph (LAB_WARM+2) | **YES** | CRITICAL |
| $03 | MON_MSG_PTR_HI | UNUSED | No | - |
| $04 | JUMP_VECTOR | UNUSED | No | - |
| $05 | JUMP_VECTOR+1 | UNUSED | No | - |
| $06 | SCREEN_PTR_LO | UNUSED | No | - |
| $07 | SCREEN_PTR_HI | UNUSED | No | - |
| $08 | SCRL_SRC_ADDR_LO | UNUSED | No | - |
| $09 | SCRL_SRC_ADDR_HI | UNUSED | No | - |
| $0A | SCRL_DEST_ADDR_LO | Usrjmp | **YES** | CRITICAL |
| $0B | SCRL_DEST_ADDR_HI | Usrjpl (Usrjmp+1) | **YES** | CRITICAL |
| $0C | SCRL_BYTE_CNT | Usrjph (Usrjmp+2) | **YES** | CRITICAL |
| $0D | CMD_LINE_COUNT | Nullct | **YES** | CRITICAL |
| $0E | PAGE_ABORT_FLAG | TPos | **YES** | CRITICAL |
| $0F | RNG_SEED | TWidth | **YES** | CRITICAL |
| $10 | RNG_MAX | Iclim | **YES** | CRITICAL |
| $11-$EE | UNUSED | BASIC vars (varies) | No | - |
| $EF | UNUSED | Decss | No | - |
| $F0 | HEX_LOOKUP[0] | Decssp1 (Decss+1) | **YES** | CRITICAL |
| $F1-$FF | HEX_LOOKUP[1-15] | BASIC decimal string | **YES** | CRITICAL |

### Conflict Summary

**Total Conflicts Identified**: 13 zero page addresses
- $00-$02: MON_CURRADDR and MON_MSG_PTR conflict with LAB_WARM
- $0A-$0C: SCRL addresses conflict with Usrjmp
- $0D-$10: Paging/RNG variables conflict with BASIC I/O control
- $F0-$FF: HEX_LOOKUP_TABLE conflicts with BASIC Decss decimal string buffer

**Resolution Required**: All 13 conflicting monitor variables must be relocated to unused zero page addresses.

## Available Zero Page Space

Scanning $00-$FF for gaps not used by BASIC:

| Range | Size | Status | Notes |
|-------|------|--------|-------|
| $03-$09 | 7 bytes | AVAILABLE | Between LAB_WARM+2 and Usrjmp |
| $14-$5A | 71 bytes | AVAILABLE | Large contiguous block |
| $69-$70 | 8 bytes | PARTIAL | BASIC uses $68 (des_sk) through $70 |
| $C5-$D7 | 19 bytes | AVAILABLE | Between Bpntrh and Rbyte4 |
| $E2-$EE | 13 bytes | AVAILABLE | Between IrqBase and Decss |

**Best Relocation Target**: $14-$5A (71 bytes) - Provides ample contiguous space for all monitor variables

## Recommended Relocation Plan

### Strategy: Relocate ALL Monitor Zero Page Variables to $14-$24

This approach keeps all monitor zero page variables in a single contiguous block for easier management.

| Old Address | Variable | New Address | Rationale |
|-------------|----------|-------------|-----------|
| $00 | MON_CURRADDR_LO | $14 | Start of contiguous block |
| $01 | MON_CURRADDR_HI | $15 | Sequential layout |
| $02 | MON_MSG_PTR_LO | $16 | Sequential layout |
| $03 | MON_MSG_PTR_HI | $17 | Sequential layout |
| $04 | JUMP_VECTOR | $18 | Sequential layout |
| $05 | JUMP_VECTOR+1 | $19 | Sequential layout |
| $06 | SCREEN_PTR_LO | $1A | Sequential layout |
| $07 | SCREEN_PTR_HI | $1B | Sequential layout |
| $08 | SCRL_SRC_ADDR_LO | $1C | Sequential layout |
| $09 | SCRL_SRC_ADDR_HI | $1D | Sequential layout |
| $0A | SCRL_DEST_ADDR_LO | $1E | Sequential layout |
| $0B | SCRL_DEST_ADDR_HI | $1F | Sequential layout |
| $0C | SCRL_BYTE_CNT | $20 | Sequential layout |
| $0D | CMD_LINE_COUNT | $21 | Sequential layout |
| $0E | PAGE_ABORT_FLAG | $22 | Sequential layout |
| $0F | RNG_SEED | $23 | Sequential layout |
| $10 | RNG_MAX | $24 | Sequential layout |
| $F0-$FF | HEX_LOOKUP_TABLE | $25-$34 | Moved from $F0-$FF to avoid Decss conflict |

**Total Space Used**: $14-$34 (33 bytes) - Fits comfortably in $14-$5A available block

### Validation: No Secondary Conflicts

Checking relocated addresses against BASIC usage:
- $14-$34: ✅ CLEAR - Not used by BASIC (in $14-$5A unused range)
- No calculated BASIC addresses extend into this range
- All monitor variables remain contiguous for easy reference

## Extended Memory Analysis ($0200-$03FF)

### BASIC Usage in $0200-$02FF

From basic.asm lines 446-467:

| Address | Variable | Purpose | Size |
|---------|----------|---------|------|
| $0200 | ccflag | CTRL-C flag | 1 byte |
| $0201 | ccbyte | CTRL-C byte | 1 byte |
| $0202 | ccnull | CTRL-C timeout | 1 byte |
| $0203-$0204 | VEC_CC | CTRL-C check vector | 2 bytes |
| $0205-$0206 | VEC_IN | Input vector | 2 bytes |
| $0207-$0208 | VEC_OUT | Output vector | 2 bytes |
| $0209-$020A | VEC_LD | Load vector | 2 bytes |
| $020B-$020C | VEC_SV | Save vector | 2 bytes |
| $020D-$0268 | Ibuffs | Input buffer | 92 bytes |

**BASIC Total Usage**: $0200-$0268 (105 bytes)

### Monitor Usage in $0200-$02FF

From kernel.asm and kernel_memory_map.md:

| Address | Variable | Purpose | Size |
|---------|----------|---------|------|
| $0200-$024F | MON_CMDBUF | Command input buffer | 80 bytes |
| $0269-$02DE | Monitor variables | All monitor variables | 118 bytes |

**Monitor Total Usage**: $0200-$024F, $0269-$02DE (198 bytes)

### Extended Memory Conflict Analysis

| Address Range | Monitor | BASIC | Conflict? | Resolution |
|---------------|---------|-------|-----------|------------|
| $0200-$0268 | MON_CMDBUF ($0200-$024F) | BASIC I/O vectors & Ibuffs | **YES** | **ACCEPTABLE OVERLAP** |
| $0269-$02DE | Monitor variables (relocated) | UNUSED | No | Clean separation |

**Critical Understanding**: The $0200-$024F overlap is **ACCEPTABLE** because:
1. Monitor command buffer is only active when Monitor is running
2. BASIC input buffer (Ibuffs) is only active when BASIC is running
3. When transitioning from BASIC back to Monitor, the command buffer must be cleared/reinitialized

## Memory Transition Requirements

### Entering BASIC from Monitor

1. Monitor state preserved in $0269-$02DE
2. Monitor command buffer ($0200-$024F) becomes inactive
3. BASIC initializes its I/O vectors and input buffer ($0200-$0268)
4. Zero page $00-$FF owned by BASIC

### Exiting BASIC to Monitor

1. BASIC variables in $0200-$0268 become invalid
2. Monitor MUST clear/reinitialize command buffer at $0200-$024F
3. Monitor MUST reset MON_CMDPTR ($0269) and MON_CMDLEN ($026A) to zero
4. Zero page $14-$34 restored to Monitor control

## Build System Requirements

Per enhancement specification:

1. **Separate ROM Files**:
   - kernel.rom (existing, $F000-$FFFF)
   - basic.rom (new, target $C000)

2. **Configuration Files Needed**:
   - Create `basic_memory.cfg` in src/kernel/ directory
   - Parallel to existing kernel build configuration

3. **Assembly Process**:
   - Use CA65 toolset (same as kernel)
   - Build basic.rom separately from kernel.rom
   - C++ loader loads both ROMs independently

4. **Memory Layout**:
   - BASIC ROM starts at $C000
   - Kernel ROM remains at $F000-$FFFF
   - No ROM overlap or combination

## Integration Architecture

### I/O Integration Points

BASIC must integrate with Monitor's I/O routines:

| Monitor Routine | Address | BASIC Usage |
|-----------------|---------|-------------|
| PRINT_CHAR | $FF00 | VEC_OUT must point here |
| GET_KEYSTROKE | $FF09 | VEC_IN must point here |

### BASIC Launch Sequence (B: Command)

1. Monitor parses "B:" command
2. Monitor saves return address on stack
3. Monitor jumps to BASIC cold start (LAB_COLD in basic.asm)
4. BASIC initializes its zero page variables ($00-$FF)
5. BASIC sets VEC_OUT = $FF00, VEC_IN = $FF09
6. BASIC displays "READY" prompt and enters main loop

### BASIC Exit Sequence

BASIC can return to monitor via:
- Warm restart mechanism
- END command (if configured)
- Error condition

Exit procedure:
1. BASIC returns control to Monitor entry point
2. Monitor clears command buffer ($0200-$024F)
3. Monitor resets MON_CMDPTR ($0269) = 0
4. Monitor resets MON_CMDLEN ($026A) = 0
5. Monitor displays command prompt

## Critical Implementation Notes

### Zero Page Relocation Impact

**ALL references to monitor zero page variables must be updated**:
- Assembly constant definitions in kernel.asm
- All LDA/STA/etc instructions using these addresses
- Documentation in kernel_memory_map.md

**Search patterns to find usages**:
```
MON_CURRADDR_LO, MON_CURRADDR_HI
MON_MSG_PTR_LO, MON_MSG_PTR_HI
JUMP_VECTOR
SCREEN_PTR_LO, SCREEN_PTR_HI
SCRL_SRC_ADDR_LO through SCRL_BYTE_CNT
CMD_LINE_COUNT, PAGE_ABORT_FLAG
RNG_SEED, RNG_MAX
HEX_LOOKUP_TABLE
```

### HEX_LOOKUP_TABLE Relocation

The hex lookup table is **data**, not just constants. Code that uses it:
- Must use new base address $25 instead of $F0
- Pattern: `LDA HEX_LOOKUP_TABLE,X`
- Will automatically update if constant is changed

### Command Buffer Cleanup

**CRITICAL**: When returning from BASIC to Monitor, must execute:
```assembly
    LDX #$4F                ; Clear 80 bytes ($00-$4F)
CLEAR_CMDBUF_LOOP:
    STZ MON_CMDBUF,X        ; Clear command buffer byte
    DEX
    BPL CLEAR_CMDBUF_LOOP

    STZ MON_CMDPTR          ; Reset command pointer ($0269)
    STZ MON_CMDLEN          ; Reset command length ($026A)
```

## Implementation Checklist

### Phase 1: Monitor Zero Page Relocation
- [ ] Update zero page constant definitions in kernel.asm
- [ ] Search and replace all references to old addresses
- [ ] Update HEX_LOOKUP_TABLE base address from $F0 to $25
- [ ] Rebuild kernel.rom and verify no breakage
- [ ] Test all monitor commands (R:, W:, F:, M:, X:, etc.)

### Phase 2: BASIC Build System
- [ ] Create basic_memory.cfg for CA65 linker
- [ ] Configure BASIC to load at $C000
- [ ] Set up build process for basic.rom
- [ ] Verify basic.rom assembles without errors

### Phase 3: Monitor B: Command
- [ ] Add B: command to monitor command parser
- [ ] Implement BASIC launch routine
- [ ] Set up BASIC entry point call
- [ ] Update help system with B: command

### Phase 4: I/O Integration
- [ ] Configure BASIC VEC_OUT to point to $FF00 (PRINT_CHAR)
- [ ] Configure BASIC VEC_IN to point to $FF09 (GET_KEYSTROKE)
- [ ] Test character output from BASIC
- [ ] Test keyboard input to BASIC

### Phase 5: Exit/Cleanup
- [ ] Implement BASIC exit mechanism
- [ ] Add command buffer cleanup routine
- [ ] Test transition back to monitor
- [ ] Verify monitor prompt displays correctly

### Phase 6: Documentation
- [ ] Update kernel_memory_map.md with new zero page layout
- [ ] Create basic_command.md documentation
- [ ] Update kernel_command_infrastructure.md
- [ ] Update kernel_flow.md
- [ ] Update README.md

## Risk Assessment

### High Risk Items

1. **Zero Page Relocation**: Any missed reference will cause crashes
   - Mitigation: Comprehensive grep search for all symbols
   - Mitigation: Thorough testing of all monitor functions

2. **Memory Overlap**: Command buffer and BASIC Ibuffs share space
   - Mitigation: Proper cleanup on BASIC exit
   - Mitigation: Clear documentation of state transitions

3. **Hex Lookup Table**: Data structure relocation
   - Mitigation: All indexed accesses use symbolic constant
   - Mitigation: Verify table contents after build

### Medium Risk Items

1. **I/O Integration**: BASIC must call monitor routines correctly
   - Mitigation: Test with simple PRINT and INPUT commands

2. **Build System**: Parallel ROM build process
   - Mitigation: Follow existing kernel build patterns

## Conclusion

This analysis has identified all zero page conflicts between Monitor and BASIC systems. The recommended relocation strategy moves all monitor zero page variables to a single contiguous block at $14-$34, completely avoiding BASIC's usage patterns.

The command buffer overlap at $0200-$024F is acceptable with proper state management during mode transitions.

**Next Steps**: Proceed to implementation phase with zero page relocation as the first priority.

**Status**: Analysis complete, ready for implementation planning.
