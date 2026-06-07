---
slug: fix-tz-curr-addr-update
status: NEW
created: 2025-10-19
author: bgentry
priority: medium
bug-id: BUG-001-TZ-CURR-ADDR
---

# Bug Fix: T: and Z: Commands Incorrectly Update Current Address

**Summary:** The T: (print stack) and Z: (print zero page) commands modify MON_CURR_ADDR to $01FF and $00FF respectively, when they should preserve the current address pointer.

**Impact:** Users see incorrect address in monitor prompt after using T: or Z: commands. The prompt displays the end address of the dumped range instead of preserving the user's working address.

## Bug Details

**Environment:**
- Platform: 6502 emulated machine
- Software Version: Current kernel development version
- Component: Monitor command system
- Commands affected: T: (CMD_DUMP_STACK), Z: (CMD_DUMP_ZERO_PAGE)

**Bug Classification:**
- **Severity:** Medium
- **Priority:** P2
- **Type:** Functional

## Reproduction

**Steps to Reproduce:**
1. Boot the 6502 kernel and reach monitor prompt
2. Set current address using any command (e.g., `R:8000` to set address to $8000)
3. Note the prompt shows `>` (or with address if implemented)
4. Execute `T:` command to display stack memory
5. Observe the prompt after T: completes
6. Note that MON_CURR_ADDR is now $01FF instead of $8000

**Alternative Reproduction (Z: command):**
1. Set current address to any value (e.g., `R:C000`)
2. Execute `Z:` command to display zero page
3. Note that MON_CURR_ADDR is now $00FF instead of $C000

**Reproduction Rate:** Always (100% reproducible)

**Test Data Needed:**
- None (bug occurs with any starting address)

## Expected vs Actual Behavior

**Expected Result:**
- T: and Z: commands should display their respective memory ranges (stack $0100-$01FF, zero page $0000-$00FF)
- MON_CURR_ADDR should remain unchanged before and after command execution
- Monitor prompt should continue showing the address that was set before T: or Z: was executed

**Actual Result:**
- T: command displays stack correctly, but updates MON_CURR_ADDR to $01FF
- Z: command displays zero page correctly, but updates MON_CURR_ADDR to $00FF
- Monitor prompt displays the end address of the dumped range instead of the user's working address

**Screenshots/Evidence:**
- [X] Issue reproduced and verified in kernel.asm source code
- [ ] Screenshot attached showing the bug
- [ ] Console output included (if applicable)

## Root Cause Analysis

**Suspected Cause:**
The issue is in the DUMP_MEMORY_RANGE routine (kernel.asm:2598-2726) which is called by both CMD_DUMP_STACK and CMD_DUMP_ZERO_PAGE:

1. **Lines 2604-2607**: DUMP_MEMORY_RANGE copies MON_STARTADDR to MON_CURRADDR
2. **Lines 2706-2708**: MON_CURRADDR is incremented throughout the dump loop
3. **Line 2726**: Function returns without restoring MON_CURRADDR to its original value

The T: and Z: commands are read-only display operations and should not have side effects on the monitor's current address pointer.

**Code Areas Affected:**
- `CMD_DUMP_STACK` (lines 2019-2035): Sets up stack range and calls DUMP_MEMORY_RANGE
- `CMD_DUMP_ZERO_PAGE` (lines 2042-2057): Sets up zero page range and calls DUMP_MEMORY_RANGE
- `DUMP_MEMORY_RANGE` (lines 2598-2726): The shared dump routine that modifies MON_CURRADDR
- Monitor main loop: Relies on MON_CURR_ADDR for prompt display and subsequent commands

**Related Code Pattern:**
Other commands that use DUMP_MEMORY_RANGE (like R: read command) may also exhibit this behavior, but for R: it's expected since the user explicitly specifies an address range to read.

## Fix Requirements

### Must Fix
- [X] Preserve MON_CURR_ADDR before calling DUMP_MEMORY_RANGE in CMD_DUMP_STACK
- [X] Restore MON_CURR_ADDR after DUMP_MEMORY_RANGE returns in CMD_DUMP_STACK
- [X] Preserve MON_CURR_ADDR before calling DUMP_MEMORY_RANGE in CMD_DUMP_ZERO_PAGE
- [X] Restore MON_CURR_ADDR after DUMP_MEMORY_RANGE returns in CMD_DUMP_ZERO_PAGE
- [ ] Ensure no regression in T: and Z: display functionality
- [ ] Verify R: command still works correctly (should update address as expected)

### Should Fix (if related)
- [ ] Review all other commands that might modify MON_CURR_ADDR unintentionally
- [ ] Document which commands modify current address vs. which preserve it
- [ ] Consider adding helper routine for save/restore of MON_CURR_ADDR

### Constraints
- **Memory Budget:** Minimal - only need 2 bytes on stack or in zero page for save/restore
- **Performance:** Negligible impact - just two store/load operations per command
- **Compatibility:** Must maintain exact same display output for T: and Z: commands

## Fix Strategy

**Approach 1: Save/Restore in Command Handlers (Recommended)**
```assembly
CMD_DUMP_STACK:
    ; Save current address
    LDA MON_CURRADDR_LO
    PHA
    LDA MON_CURRADDR_HI
    PHA

    ; Reset counters and set up dump range
    LDA #0
    STA CMD_LINE_COUNT
    STA PAGE_ABORT_FLAG
    LDA #$00
    STA MON_STARTADDR_LO
    LDA #$01
    STA MON_STARTADDR_HI
    LDA #$FF
    STA MON_ENDADDR_LO
    LDA #$01
    STA MON_ENDADDR_HI

    ; Perform dump
    JSR DUMP_MEMORY_RANGE

    ; Restore current address
    PLA
    STA MON_CURRADDR_HI
    PLA
    STA MON_CURRADDR_LO
    RTS
```

**Approach 2: Use Temporary Variables**
Could use temporary zero page locations if stack usage is a concern, but stack is cleaner and more efficient for this use case.

## Testing Strategy

**Unit Tests Required:**
- [ ] Test T: command preserves MON_CURR_ADDR at various starting addresses
- [ ] Test Z: command preserves MON_CURR_ADDR at various starting addresses
- [ ] Verify T: output is identical before and after fix
- [ ] Verify Z: output is identical before and after fix

**Integration Tests:**
- [ ] Execute sequence: R:8000, T:, verify address still $8000
- [ ] Execute sequence: R:C000, Z:, verify address still $C000
- [ ] Execute sequence: R:1000, T:, R:, verify R: shows data at $1000
- [ ] Execute sequence: W:2000, Z:, verify write mode starts at $2000

**Manual Test Cases:**
1. **Verify Fix for T:**
   - Enter `R:8000` to set address
   - Enter `T:` to display stack
   - Verify prompt shows original address (not $01FF)
   - Enter `R:` to confirm current address still $8000

2. **Verify Fix for Z:**
   - Enter `R:C000` to set address
   - Enter `Z:` to display zero page
   - Verify prompt shows original address (not $00FF)
   - Enter `R:` to confirm current address still $C000

3. **Regression Test for R:**
   - Enter `R:5000-50FF` to read range
   - Verify R: still updates current address (expected behavior)

**Test Data:**
- Test with various starting addresses: $0000, $1000, $8000, $C000, $FFFF
- Test both T: and Z: commands at each address

## Success Criteria

**Definition of Done:**
- [X] Bug documented with root cause analysis
- [ ] Fix implemented in both CMD_DUMP_STACK and CMD_DUMP_ZERO_PAGE
- [ ] All unit tests pass
- [ ] No regressions detected in display output
- [ ] Manual testing verification complete
- [ ] Code review completed

**Acceptance Criteria:**
- Given monitor is at address $8000, when user executes `T:` command, then current address remains $8000 after command completes
- Given monitor is at address $C000, when user executes `Z:` command, then current address remains $C000 after command completes
- Given any starting address, when T: or Z: executes, then display output is identical to pre-fix behavior

## Notes for Implementer Subagent

- **Investigation Priority:** Root cause already identified - DUMP_MEMORY_RANGE modifies MON_CURRADDR
- **Code Areas:** Focus on CMD_DUMP_STACK (lines 2019-2035) and CMD_DUMP_ZERO_PAGE (lines 2042-2057)
- **Implementation:** Use stack-based save/restore (PHA/PLA) as shown in Approach 1
- **Testing Approach:** Verify address preservation while maintaining identical display output
- **Memory Considerations:** Stack usage is minimal (4 bytes peak) and temporary
- **Side Effects:** None expected - purely fixing unintended side effect

**Implementation Notes:**
- Save MON_CURRADDR_LO first, then MON_CURRADDR_HI (reverse order for restore)
- Place save immediately at function entry, restore just before RTS
- Do NOT modify DUMP_MEMORY_RANGE itself (R: command needs current behavior)
- Consider similar pattern may be needed for other display-only commands

## Notes for Tester Subagent

- **Verification Steps:** Test both address preservation AND display output correctness
- **Test Environments:** Emulated 6502 environment with monitor system
- **Critical Tests:**
  1. Address preservation (main bug fix)
  2. Display output regression (ensure no changes)
  3. Subsequent command behavior (verify monitor state is clean)
- **Sign-off Requirements:** Manual testing + automated tests (when test framework available)

## Related Documentation

- See `/Users/bgentry/Source/repos/6502 Kernel/docs/command_help.md` for T: and Z: command documentation
- Monitor architecture: `/Users/bgentry/Source/repos/6502 Kernel/CLAUDE.md` section on Monitor Development Guidelines
- Memory map: `/Users/bgentry/Source/repos/6502 Kernel/docs/system_architecture.md`