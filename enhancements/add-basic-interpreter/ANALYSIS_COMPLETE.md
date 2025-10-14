---
enhancement: add-basic-interpreter
task_id: task_1759348065_71462
agent: assembly-developer
created: 2025-10-01 22:05:18
status: READY_FOR_IMPLEMENTATION
---

# BASIC Interpreter Integration - Analysis Complete

## Executive Summary

The assembly-developer agent has completed a comprehensive technical analysis and architecture design for integrating the EhBASIC 2.22p5 interpreter into the 6502 kernel monitor system. All memory conflicts have been identified and resolved, integration architecture has been designed, and a detailed implementation plan has been created.

**Status:** READY_FOR_IMPLEMENTATION

---

## Deliverables

### 1. Memory Conflict Analysis Report
**File:** `memory-conflict-analysis.md`

**Key Findings:**
- **26 zero page conflicts** identified between Monitor and BASIC
- **Resolution strategy:** Relocate monitor variables from $00-$10 and $F0-$FF to unused gap at $14-$3F
- **Extended RAM overlap** at $0200-$024F resolved through mutual exclusivity (monitor command buffer cleared on BASIC exit)
- **Complete address mapping** provided with before/after layouts

**Critical Conflicts Resolved:**
- Monitor $00-$02 conflicts with BASIC warm start vectors → Relocated to $14-$16
- Monitor $F0-$FF (HEX_LOOKUP_TABLE) conflicts with BASIC decimal string buffer ($EF-$FF) → Relocated to $25-$34
- All 26 conflicts have defined relocation targets with verification that no secondary conflicts are created

### 2. Integration Architecture Document
**File:** `integration-architecture.md`

**Key Components:**
- **System architecture diagrams** showing monitor/BASIC interaction
- **Control flow diagrams** for B: command entry and BYE command exit
- **Memory management strategy** with detailed zero page and extended RAM allocation
- **State management** routines (SAVE_MONITOR_STATE, RESTORE_MONITOR_STATE)
- **I/O integration** design (VEC_IN, VEC_OUT vectors point to monitor routines)
- **Build system integration** with CMake configuration
- **Testing strategy** with unit, integration, and manual test scenarios

**Integration Points:**
- B: command handler launches BASIC at $C000
- BASIC BYE command returns to monitor via $FF12
- BASIC uses monitor PRINT_CHAR ($FF00) and GET_KEYSTROKE ($FF09)
- Command buffer cleanup ensures clean state transitions

### 3. Implementation Plan
**File:** `implementation-plan.md`

**6 Milestones with Step-by-Step Instructions:**

1. **Milestone 1: Zero Page Relocation** (4-6 hours)
   - Update 17 monitor zero page constants
   - Relocate HEX_LOOKUP_TABLE from $F0-$FF to $25-$34
   - Test each monitor command individually
   - Document changes in kernel_memory_map.md

2. **Milestone 2: B: Command Framework** (2-3 hours)
   - Implement SAVE_MONITOR_STATE routine
   - Implement RESTORE_MONITOR_STATE with buffer cleanup
   - Implement INIT_BASIC_IO for vector setup
   - Add B: command parser and stub handler

3. **Milestone 3: BASIC ROM Build System** (3-4 hours)
   - Configure BASIC memory layout (Ram_base=$0300, Ram_top=$C000)
   - Add BYE command to BASIC (new token, keyword, handler)
   - Create memory_basic.cfg linker configuration
   - Update CMakeLists.txt with basic_rom target

4. **Milestone 4: BASIC Integration** (3-4 hours)
   - Add RETURN_FROM_BASIC handler at $FF12
   - Connect B: command to BASIC cold start
   - Test complete launch/return cycle
   - Verify I/O integration

5. **Milestone 5: Testing and Validation** (3-4 hours)
   - Create basic_integration_tests suite
   - Execute 17 manual test scenarios
   - Verify zero page integrity
   - Stress test repeated cycles

6. **Milestone 6: Documentation** (2-3 hours)
   - Create docs/basic_command.md
   - Update kernel_memory_map.md, kernel_flow.md
   - Update command_help.md, README.md
   - Update monitor help system (H: command)

**Total Estimated Time:** 17-24 hours

---

## Technical Decisions Made

### 1. Memory Layout Strategy

**Decision:** Relocate monitor zero page variables to $14-$3F gap

**Rationale:**
- BASIC uses $00-$13, $5B-$BB, $BC-$E1, $EF-$FF
- Gap at $14-$5A (71 bytes) is unused by BASIC
- Monitor needs 33 bytes (17 variables + 16-byte HEX_LOOKUP_TABLE)
- Single contiguous block simplifies implementation

**Alternative Considered:** Split relocation across multiple gaps
**Rejected Because:** Increases complexity and error potential

### 2. Command Buffer Overlap Strategy

**Decision:** Allow monitor command buffer ($0200-$024F) to overlap BASIC variables

**Rationale:**
- Monitor and BASIC never run simultaneously
- Clearing buffer on BASIC exit ensures clean state
- Avoids need to relocate large buffer area
- Simplifies memory management

**Critical Requirement:** RESTORE_MONITOR_STATE MUST clear command buffer

### 3. BASIC Entry Point

**Decision:** Use cold start only (LAB_COLD at $C000)

**Rationale:**
- Simpler implementation for MVP
- Complete initialization ensures clean state
- No risk of corrupted variables from previous session

**Future Enhancement:** Add warm start option for program persistence

### 4. BASIC Exit Mechanism

**Decision:** Add BYE command that jumps to $FF12

**Rationale:**
- Explicit and user-friendly
- Doesn't interfere with BASIC's normal operation
- Easy to document and understand

**Alternative Considered:** Hook warm start vector
**Rejected Because:** Less intuitive, harder to control

### 5. I/O Integration

**Decision:** Use existing monitor PRINT_CHAR and GET_KEYSTROKE

**Rationale:**
- No need to duplicate I/O code
- Consistent behavior between monitor and BASIC
- EhBASIC designed for external I/O vectors
- Minimal integration code required

**Verification:** BASIC I/O vectors ($0205-$020C) initialized in INIT_BASIC_IO

### 6. Build System

**Decision:** Separate ROM files (kernel.rom and basic.rom)

**Rationale:**
- Independent compilation of monitor and BASIC
- Easier to update either component separately
- Clear separation of responsibilities
- Standard linker configuration approach

**Configuration:**
- kernel.rom: $F000-$FFFF (4KB, monitor)
- basic.rom: $C000-$EFFF (12KB, BASIC)

---

## Memory Safety Verification

### Zero Page Safety
✓ All 26 conflicts identified and resolved
✓ Relocation target ($14-$3F) verified unused by BASIC
✓ No secondary conflicts created by relocation
✓ Monitor uses 44 bytes, 71 available in gap (27 bytes spare)
✓ HEX_LOOKUP_TABLE moved away from BASIC Decss range

### Extended RAM Safety
✓ Monitor command buffer overlap documented and acceptable
✓ Monitor variables at $0269-$02DE clear of BASIC ($0200-$0268)
✓ Buffer cleanup procedure defined and mandatory
✓ No stack conflicts ($0100-$01FF shared, standard practice)

### ROM Safety
✓ BASIC ROM at $C000-$EFFF (12KB)
✓ Monitor ROM at $F000-$FFFF (4KB)
✓ No overlap between ROM regions
✓ Monitor ROM has 683 bytes free (180 bytes needed for B: command)
✓ Screen memory ($0400-$07FF) shared correctly

---

## Implementation Readiness Assessment

### Prerequisites Met
✓ Complete memory conflict analysis
✓ Relocation strategy defined with exact addresses
✓ Integration architecture documented
✓ Build system design complete
✓ Testing strategy defined
✓ Step-by-step implementation plan created

### Risks Identified and Mitigated
- **HIGH:** Zero page relocation affects all monitor commands
  - **Mitigation:** Test each command individually after relocation
  - **Rollback:** Git checkpoints at each milestone

- **MEDIUM:** State management between monitor and BASIC
  - **Mitigation:** Comprehensive save/restore with verification
  - **Rollback:** Separate commits for each component

- **LOW:** Build system integration
  - **Mitigation:** Standard CMake patterns, separate ROM files
  - **Rollback:** Build system changes easily reversible

### Critical Success Factors
1. **Accurate variable relocation** - Must update ALL references to relocated variables
2. **Complete buffer cleanup** - Command buffer MUST be cleared on BASIC exit
3. **Correct I/O vectors** - VEC_IN and VEC_OUT must point to monitor routines
4. **Thorough testing** - Every monitor command must be tested after relocation

---

## Next Steps for Implementer

### Phase 1: Setup
1. Create feature branch: `git checkout -b feature/basic-interpreter`
2. Review all three analysis documents thoroughly
3. Backup current kernel.asm: `cp src/kernel/kernel.asm src/kernel/kernel.asm.backup`

### Phase 2: Implementation
Follow implementation-plan.md milestones sequentially:
1. Milestone 1: Zero page relocation (CRITICAL - test thoroughly)
2. Milestone 2: B: command framework (test stub behavior)
3. Milestone 3: BASIC ROM build (verify ROM builds and loads)
4. Milestone 4: BASIC integration (full system test)
5. Milestone 5: Testing and validation (comprehensive)
6. Milestone 6: Documentation (complete and consistent)

### Phase 3: Validation
1. Run full test suite: `ctest --verbose`
2. Execute all 17 manual test scenarios
3. Verify no regressions in existing functionality
4. Test repeated entry/exit cycles (memory leak check)
5. Document any issues found

### Phase 4: Integration
1. Create pull request with complete description
2. Code review focusing on memory safety
3. Final acceptance testing
4. Merge to main branch
5. Tag release: v1.1.0-basic

---

## Documentation Provided

### Technical Architecture
1. **memory-conflict-analysis.md** - Complete memory conflict analysis
2. **integration-architecture.md** - System integration design
3. **implementation-plan.md** - Step-by-step implementation guide

### Sections in Implementation Plan
- 6 detailed milestones with time estimates
- Step-by-step instructions for each task
- Code examples for all new routines
- Test procedures for each milestone
- Rollback procedures for each phase
- Commit messages for each milestone
- Success criteria checklists

### Reference Information
- Complete zero page address mapping (before/after)
- Extended RAM layout with overlaps documented
- Build system commands and configuration
- Testing strategy with 17 test scenarios
- Error handling and edge cases
- Future enhancement suggestions

---

## Key Implementation Notes

### CRITICAL: Zero Page Relocation
The most important and error-prone task is relocating monitor zero page variables from $00-$10 and $F0-$FF to $14-$3F. **Every single reference** to these variables in kernel.asm must be updated.

**Verification Strategy:**
1. Update constant definitions first
2. Compile to find any missed references (assembler errors)
3. Test EACH command individually after relocation
4. Run full regression test suite
5. Visual inspection of zero page memory during operation

### CRITICAL: Command Buffer Cleanup
When returning from BASIC to monitor, the command buffer at $0200-$024F MUST be completely cleared. This is handled in RESTORE_MONITOR_STATE, but must be verified in testing.

**Verification Strategy:**
1. Fill buffer with test pattern (0xFF) before entering BASIC
2. Run BASIC program
3. Exit with BYE command
4. Verify MON_CMDPTR = 0, MON_CMDLEN = 0
5. Verify buffer contains zeros
6. Test monitor command execution (should work normally)

### CRITICAL: I/O Vector Initialization
BASIC must use monitor I/O routines via vectors at $0205-$020C. These are initialized in INIT_BASIC_IO.

**Verification Strategy:**
1. After calling INIT_BASIC_IO, inspect memory:
   - $0207-$0208 should contain $00 $FF (VEC_OUT = $FF00)
   - $0205-$0206 should contain $09 $FF (VEC_IN = $FF09)
2. In BASIC, execute PRINT "TEST"
3. Verify output appears on screen via PRINT_CHAR
4. In BASIC, execute INPUT X
5. Verify keyboard input works via GET_KEYSTROKE

---

## Success Metrics

### Functional Requirements
- [ ] B: command launches BASIC successfully
- [ ] BASIC programs execute correctly
- [ ] BYE command returns to monitor
- [ ] Monitor commands work after BASIC usage
- [ ] No crashes or hangs during normal operation

### Technical Requirements
- [ ] All 26 zero page conflicts resolved
- [ ] HEX_LOOKUP_TABLE relocated from $F0-$FF to $25-$34
- [ ] Command buffer cleanup verified
- [ ] I/O vectors correctly initialized
- [ ] Both ROM files build within size limits

### Quality Requirements
- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] 17 manual test scenarios complete successfully
- [ ] No regressions in existing functionality
- [ ] Memory corruption tests pass

### Documentation Requirements
- [ ] User documentation created (basic_command.md)
- [ ] Technical documentation updated (4 files)
- [ ] In-monitor help updated (H: command)
- [ ] README updated with new features

---

## Estimated Development Timeline

**Conservative Estimate:** 20-24 hours
**Aggressive Estimate:** 14-17 hours
**Recommended Schedule:** 3 days × 6-8 hours

**Day 1:** Milestones 1-2 (Zero page relocation + B: framework)
- Most critical and time-consuming work
- Thorough testing required
- Checkpoint: Monitor still works, B: stub executes

**Day 2:** Milestones 3-4 (BASIC build + Integration)
- Build system configuration
- Full integration and testing
- Checkpoint: B: launches BASIC, BYE returns to monitor

**Day 3:** Milestones 5-6 (Testing + Documentation)
- Comprehensive test execution
- Documentation completion
- Checkpoint: All tests pass, docs complete

---

## Final Recommendations

### For the Implementer

1. **Follow the plan sequentially** - Don't skip milestones or steps
2. **Test at every checkpoint** - Catch errors early
3. **Commit frequently** - One commit per milestone minimum
4. **Document issues** - Keep notes on any problems encountered
5. **Ask questions** - Refer back to architecture docs if unclear

### For Code Review

1. **Focus on memory safety** - Verify no conflicts remain
2. **Check ALL variable references** - Relocation must be complete
3. **Test thoroughly** - Execute all test scenarios
4. **Verify documentation** - Should be accurate and complete
5. **Performance check** - Monitor commands should not be slower

### For Testing

1. **Test monitor commands individually** - After zero page relocation
2. **Test BASIC programs** - Various types and complexity levels
3. **Test state transitions** - Enter/exit cycles repeatedly
4. **Test edge cases** - Long programs, rapid input, boundary conditions
5. **Test error conditions** - BASIC ROM missing, invalid commands

---

## Conclusion

The BASIC interpreter integration is **technically sound** and **ready for implementation**. All memory conflicts have been analyzed and resolved, the integration architecture is complete, and a detailed implementation plan with step-by-step instructions has been provided.

**Critical Path:** Zero page relocation (Milestone 1) is the most important and error-prone task. Extra care and thorough testing are essential here.

**Risk Level:** MEDIUM - Careful implementation and testing required, but design is solid.

**Success Probability:** HIGH - Comprehensive planning reduces implementation risk significantly.

**Recommendation:** Proceed with implementation following the provided plan.

---

**Agent Status:** READY_FOR_IMPLEMENTATION

**Next Agent:** implementer (or assembly-developer can implement if requested)

**Output Files:**
1. `memory-conflict-analysis.md` - Memory conflict analysis and resolution
2. `integration-architecture.md` - System architecture and integration design
3. `implementation-plan.md` - Step-by-step implementation guide
4. `ANALYSIS_COMPLETE.md` - This summary document

**Total Documentation:** ~28,000 words across 4 comprehensive technical documents

---

**Analysis Date:** 2025-10-01
**Completion Time:** ~2.5 hours
**Agent:** assembly-developer
**Task ID:** task_1759348065_71462
