---
enhancement: add-basic-interpreter
task_id: task_1759432458_82784
agent: assembly-implementer
created: 2025-10-02 14:30:00
---

# Assembly Implementer - Analysis and Execution Plan

## Role and Mission

I am the **6502 Assembly Implementer** agent. My role is **EXECUTION, NOT ANALYSIS**. I execute pre-designed technical solutions by making actual code changes to files.

I DO NOT create new plans or analyze requirements. I implement the existing implementation plan.

## Current Status Assessment

### Analysis Phase Status: ✅ COMPLETE

The requirements analyst and assembly-developer agents have completed comprehensive analysis:

1. **requirements_analysis.md** - Business requirements defined
2. **basic-memory-analysis.md** - Memory conflicts identified and resolved
3. **basic-integration-architecture.md** - Technical architecture designed
4. **implementation-plan.md** - Step-by-step implementation procedures
5. **assembly-developer-summary.md** - Executive summary of decisions

### Code Implementation Status: ❌ NOT STARTED

Verification of current codebase:
- `kernel.asm` - Monitor variables still at OLD addresses ($00-$10, $F0-$FF)
- No B: command implementation exists
- No BASIC build system in place
- No BYE command in basic.asm
- No config/ directory exists

**Conclusion**: Implementation plan is complete and ready to execute, but NO CODE CHANGES have been made yet.

## Implementation Plan Review

I have reviewed the comprehensive implementation-plan.md document. It provides:

### 6 Clear Milestones

1. **Milestone 1**: Zero Page Relocation (4-6 hours, HIGH RISK)
2. **Milestone 2**: B: Command Framework (2-3 hours, MEDIUM RISK)
3. **Milestone 3**: BASIC ROM Build System (3-4 hours, LOW RISK)
4. **Milestone 4**: BASIC Integration (3-4 hours, HIGH RISK)
5. **Milestone 5**: Testing and Validation (3-4 hours, LOW RISK)
6. **Milestone 6**: Documentation (2-3 hours, NO RISK)

**Total Estimated Time**: 17-24 hours

### Key Technical Details Identified

**Memory Relocation Required**:
- MON_CURRADDR_LO: $00 → $14
- MON_CURRADDR_HI: $01 → $15
- MON_MSG_PTR_LO: $02 → $16
- MON_MSG_PTR_HI: $03 → $17
- JUMP_VECTOR: $04-$05 → $18-$19
- SCREEN_PTR_LO/HI: $06-$07 → $1A-$1B
- SCRL_SRC_ADDR_LO/HI: $08-$09 → $1C-$1D
- SCRL_DEST_ADDR_LO/HI: $0A-$0B → $1E-$1F
- SCRL_BYTE_CNT: $0C → $20
- CMD_LINE_COUNT: $0D → $21
- PAGE_ABORT_FLAG: $0E → $22
- RNG_SEED: $0F → $23
- RNG_MAX: $10 → $24
- HEX_LOOKUP_TABLE: $F0-$FF → $25-$34

**New Files to Create**:
- `src/kernel/basic_memory.cfg` - BASIC linker configuration

**Files to Modify**:
- `src/kernel/kernel.asm` - Zero page relocation, B: command, state management
- `src/kernel/basic.asm` - Add BYE command
- `CMakeLists.txt` - Add basic_rom build target
- `docs/kernel_memory_map.md` - Update memory layout documentation

## Critical Implementation Notes

### From Enhancement Document

The enhancement document "Notes for assembly-implementer Subagent" section provides these critical instructions:

**BUILD SYSTEM REQUIREMENTS** (lines 46-52):
- BASIC should be built as a separate basic.rom file (NOT combined with kernel.rom)
- Create basic_memory.cfg in src/kernel/ directory (same location as existing kernel build files)
- Follow the exact same build pattern as kernel.rom (parallel process, not combined)
- C++ loader code will handle loading both kernel.rom and basic.rom separately into computer memory
- DO NOT create a new /config directory or system.rom - use existing build patterns

**CRITICAL**: The implementation-plan.md suggests creating `config/memory_basic.cfg` but the enhancement document explicitly says to create `src/kernel/basic_memory.cfg` instead. I will follow the enhancement document guidance.

### Memory Safety Requirements

**Command Buffer Overlap** (from basic-memory-analysis.md):
- Monitor command buffer ($0200-$024F) overlaps with BASIC variables
- This is ACCEPTABLE because systems are mutually exclusive
- CRITICAL: RESTORE_MONITOR_STATE must clear command buffer completely

**Zero Page Conflicts**:
- 33 conflicts identified between BASIC and Monitor
- All resolved by relocating Monitor to $14-$34 gap
- Post-relocation validation confirms zero conflicts

### Build System Pattern

From examining existing kernel build:
- Kernel assembly uses ca65 assembler
- Kernel linking uses ld65 with memory configuration
- Must follow same pattern for BASIC ROM

## Execution Strategy

### Phase 1: Verification and Preparation

Before making any changes:
1. ✅ Read implementation-plan.md completely
2. ✅ Understand all 6 milestones
3. ✅ Verify current code state (nothing implemented yet)
4. ✅ Note critical requirements from enhancement document
5. ✅ Identify file path corrections needed

### Phase 2: Milestone Execution

Execute milestones in order, testing after each:

**Milestone 1: Zero Page Relocation**
- Update constant definitions in kernel.asm
- Verify compilation succeeds
- Test each monitor command individually
- Update documentation
- Commit before proceeding

**Milestone 2: B: Command Framework**
- Add state management routines
- Add B: command parser entry
- Add stub handler
- Test command dispatch
- Commit before proceeding

**Milestone 3: BASIC ROM Build System**
- Configure BASIC RAM range in basic.asm
- Create src/kernel/basic_memory.cfg (NOTE: src/kernel/, not config/)
- Add BYE command to basic.asm
- Update CMakeLists.txt
- Build and verify BASIC ROM
- Commit before proceeding

**Milestone 4: BASIC Integration**
- Add RETURN_FROM_BASIC handler
- Update B: command to jump to BASIC
- Test complete launch/return cycle
- Commit before proceeding

**Milestone 5: Testing and Validation**
- Create integration test suite
- Execute manual test scenarios
- Document results
- Commit when tests pass

**Milestone 6: Documentation**
- Create basic_command.md
- Update all affected documentation
- Update in-monitor help
- Commit when complete

### Phase 3: Validation and Completion

Final checks:
- All 6 milestones complete
- All tests passing
- Documentation complete
- No regressions observed

## File Path Corrections

The implementation-plan.md contains a file path inconsistency that needs correction:

**Implementation Plan Says**:
```
File: config/memory_basic.cfg (NEW FILE)
```

**Enhancement Document Says** (line 48):
```
Create basic_memory.cfg in src/kernel/ directory (same location as existing kernel build files)
```

**My Decision**: Follow the enhancement document. The correct path is:
```
File: src/kernel/basic_memory.cfg (NEW FILE)
```

**Rationale**:
1. Enhancement document is the authoritative source
2. It explicitly says "DO NOT create a new /config directory"
3. It says to use "existing build patterns"
4. Placing in src/kernel/ keeps build files together

## Risk Assessment

### HIGH RISK: Milestone 1 (Zero Page Relocation)

**Risk**: Updating 18 variable addresses could break ALL monitor commands

**Mitigation**:
- Create backup before changes
- Test each command immediately after relocation
- Commit before proceeding to Milestone 2
- Keep rollback procedure ready

### MEDIUM RISK: Milestone 2 & 4 (State Management)

**Risk**: Improper state save/restore could cause crashes

**Mitigation**:
- Implement comprehensive save/restore routines
- Test state management independently
- Use stub version in Milestone 2 before full integration

### LOW RISK: Milestone 3 (Build System)

**Risk**: Build configuration errors

**Mitigation**:
- Build system changes only affect build-time, not runtime
- Test build before integration
- Easy to fix if errors occur

## Success Criteria

I will mark this task complete when:

### Code Implementation
- ✅ All variable relocations completed in kernel.asm
- ✅ B: command implemented and functional
- ✅ BASIC ROM build system working
- ✅ BYE command added to BASIC
- ✅ Complete launch/return cycle working
- ✅ All code compiles without errors

### Testing
- ✅ All existing tests pass (no regressions)
- ✅ All monitor commands work after relocation
- ✅ B: command launches BASIC
- ✅ BASIC programs execute correctly
- ✅ BYE command returns to monitor
- ✅ Repeated cycles work without crashes

### Documentation
- ✅ basic_command.md created
- ✅ kernel_memory_map.md updated
- ✅ kernel_flow.md updated
- ✅ kernel_command_infrastructure.md updated
- ✅ command_help.md updated
- ✅ README.md updated
- ✅ In-monitor help (H: command) updated

## Quality Standards

### Code Quality
- Follow existing code style and formatting
- Maintain proper indentation and commenting
- Use consistent naming conventions
- Ensure all addresses and constants are correct

### Testing Thoroughness
- Test each milestone before proceeding
- Test each monitor command after relocation
- Test repeated entry/exit cycles
- Test boundary conditions

### Documentation Accuracy
- Ensure all documentation matches implementation
- Update all affected files consistently
- Provide clear examples and usage instructions

## Execution Decision

**Status**: READY_FOR_IMPLEMENTATION

I have thoroughly reviewed:
1. ✅ Implementation plan (60KB, comprehensive)
2. ✅ Memory analysis (26KB, complete)
3. ✅ Integration architecture (35KB, detailed)
4. ✅ Enhancement requirements (original spec)
5. ✅ Current code state (verified unmodified)

**Decision**: Proceed with implementation following the 6-milestone plan

**First Step**: Begin Milestone 1 - Zero Page Relocation

**File Path Correction**: Use `src/kernel/basic_memory.cfg` instead of `config/memory_basic.cfg`

**Confidence Level**: HIGH - Plan is comprehensive, detailed, and ready to execute

## Notes for Implementation

### Critical Reminders

1. **Test after EVERY change** - Don't accumulate untested changes
2. **Commit after each milestone** - Enable easy rollback if needed
3. **Follow the plan exactly** - It's comprehensive and tested
4. **Use correct file paths** - src/kernel/basic_memory.cfg, not config/
5. **Clear command buffer** - RESTORE_MONITOR_STATE must clear completely

### Expected Challenges

1. **Finding all variable references** - Use grep to search thoroughly
2. **HEX_LOOKUP_TABLE relocation** - Ensure initialization code updated
3. **Testing all commands** - Takes time but critical for verification
4. **Build system integration** - Follow existing patterns carefully
5. **BASIC entry point** - Verify LAB_COLD address matches JMP target

### Tools and Commands

**Search for variable references**:
```bash
grep -n '\$00[0-9A-Fa-f]' kernel.asm  # Find ZP references
grep -n '\$0F[0-9A-Fa-f]' kernel.asm  # Find hex table refs
```

**Build commands**:
```bash
cd build
cmake -G Ninja -DBUILD_TESTS=ON ..
ninja
ctest --verbose
```

**Test individual commands**:
```bash
./build/6502_Kernel
# At prompt: R:, W:, Z:, T:, H:, C:, F:, M:, X:
```

## Implementation Workflow

### Step-by-Step Process

For each milestone:
1. Read the milestone section in implementation-plan.md
2. Execute each step exactly as specified
3. Test thoroughly before proceeding
4. Document any deviations or issues
5. Commit when milestone complete
6. Verify no regressions before next milestone

### If Issues Arise

**If tests fail**:
1. Revert to last good commit
2. Review the specific step that failed
3. Check for typos or incorrect addresses
4. Verify against architecture documents
5. Fix and test again

**If build fails**:
1. Check CMakeLists.txt syntax
2. Verify file paths are correct
3. Ensure memory.cfg format is valid
4. Check for assembler errors in output

**If runtime errors occur**:
1. Use monitor R: command to inspect memory
2. Verify variable addresses are correct
3. Check state save/restore routines
4. Test individual components in isolation

## Conclusion

I am ready to begin implementation. I will:

1. Start with Milestone 1 (Zero Page Relocation)
2. Follow the implementation-plan.md step-by-step
3. Test thoroughly at each checkpoint
4. Use correct file paths (src/kernel/basic_memory.cfg)
5. Document progress and any deviations
6. Complete all 6 milestones systematically

**My task is to EXECUTE, not to create new plans.** The plan is excellent and comprehensive. I will implement it faithfully.

---

**Status**: READY_FOR_IMPLEMENTATION
**Next Action**: Begin Milestone 1 - Zero Page Relocation
**Confidence**: HIGH
**Estimated Time**: 17-24 hours total

End of Analysis
