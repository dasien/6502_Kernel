# Multi-Agent Workflow Step Template

This document provides the standard command sequence for processing enhancements through our multi-agent workflow system.

## Prerequisites

1. Enhancement file created following `enhancement_template_lg.md`
2. Enhancement file located at `enhancements/{enhancement_name}.md`
3. Queue system operational (`.claude/queues/queue_manager.sh` executable)

## Standard Workflow Commands

### Phase 1: Requirements Analysis

```bash
# 1. Check initial system status
.claude/queues/queue_manager.sh status

# 2. Add requirements analysis task
.claude/queues/queue_manager.sh add "Analyze {enhancement_name} enhancement" "requirements-analyst" "high" "Process {enhancement_name}.md through requirements analysis phase"

# 3. Start requirements analysis
.claude/queues/queue_manager.sh start requirements-analyst

# 4. Complete requirements analysis (with auto-chain)
.claude/queues/queue_manager.sh complete {task_id} "READY_FOR_DEVELOPMENT - Requirements analysis complete, implementation plan created, next: {suggested_agent} (architect role)" --auto-chain

# 5. If auto-chain suggests task, approve it (y/N prompt will appear)
```

### Phase 2: Architecture Design

```bash
# 6. Review auto-created architecture task
.claude/queues/queue_manager.sh status

# 7. Start architecture design
.claude/queues/queue_manager.sh start {architect_agent}

# 8. Complete architecture design (with auto-chain)
.claude/queues/queue_manager.sh complete {task_id} "READY_FOR_IMPLEMENTATION - Architecture design complete, file structure planned, implementation guidance provided" --auto-chain

# 9. If auto-chain suggests task, approve it (y/N prompt will appear)
```

### Phase 3: Implementation

```bash
# 10. Review auto-created implementation task
.claude/queues/queue_manager.sh status

# 11. Start implementation
.claude/queues/queue_manager.sh start {implementer_agent}

# 12. Complete implementation (with auto-chain)
.claude/queues/queue_manager.sh complete {task_id} "READY_FOR_INTEGRATION - Implementation complete, all components created and integrated" --auto-chain

# 13. If auto-chain suggests task, approve it (y/N prompt will appear)
```

### Phase 4: Testing & Validation

```bash
# 14. Review auto-created testing task
.claude/queues/queue_manager.sh status

# 15. Start testing phase
.claude/queues/queue_manager.sh start testing-agent

# 16. Complete testing phase (final step - no auto-chain needed)
.claude/queues/queue_manager.sh complete {task_id} "TESTING_COMPLETE - Multi-agent workflow system validated, enhancement ready for production"
```

### Final Verification

```bash
# 17. Check final system status
.claude/queues/queue_manager.sh status

# 18. Review completed workflow
ls enhancements/{enhancement_name}*/
```

## Agent Assignment Guide

Our smart auto-chain system will typically assign agents based on enhancement content:

### Assembly-Focused Enhancements
- **Architecture**: `assembly-developer`
- **Implementation**: `assembly-developer`
- **Keywords**: `6502`, `assembly`, `kernel.asm`, `monitor`, `hardware`

### C++-Focused Enhancements
- **Architecture**: `cpp-developer`
- **Implementation**: `cpp-developer`
- **Keywords**: `C++`, `main.cpp`, `.cpp`, `.h`, `simulator`, `emulator`

### Mixed or Unclear Enhancements
- **System prompts for choice**: Manual agent selection required

## Expected File Structure After Completion

```
enhancements/
├── {enhancement_name}.md                           # Original enhancement file
├── {enhancement_name}_requirements_analysis.md     # Phase 1 output
├── {enhancement_name}_architecture.md              # Phase 2 output
└── {enhancement_name}/                             # Phase 3 output directory
    ├── implementation_notes.md
    ├── {various_implementation_files}
    └── {enhancement_name}_testing_validation.md    # Phase 4 output
```

## Human Review Points (HITL - Human in the Loop)

At each phase transition, humans should:

1. **Review agent output** before approving auto-chain suggestions
2. **Work directly with agents** for refinements if needed (outside queue system)
3. **Only approve transitions** when satisfied with quality
4. **Override agent assignments** if the auto-suggestion is incorrect

## Iterative Refinement Pattern

If an agent's output needs refinement:

```bash
# Complete the task normally (no auto-chain)
.claude/queues/queue_manager.sh complete {task_id} "Initial {phase} complete, needs refinement"

# Work directly with agent for improvements (normal Claude Code conversation)
# Agent makes refinements based on human feedback

# When satisfied, manually create next phase task
.claude/queues/queue_manager.sh add "{next_phase_title}" "{next_agent}" "high" "{next_phase_description}"
```

## Troubleshooting

### Auto-Chain Not Working
- Verify enhancement name can be extracted from task title
- Check that enhancement file exists at expected location
- Ensure status message contains recognized keywords

### Agent Assignment Issues
- Review enhancement content for technology indicators
- Add explicit agent guidance in enhancement file
- Use manual task creation if auto-assignment fails

### Workflow Stuck
- Check queue status: `.claude/queues/queue_manager.sh status`
- Review logs: `tail .claude/logs/queue_operations.log`
- Complete stuck tasks manually if needed

## Example: Complete Workflow Execution

```bash
# Example for "add-websocket-support" enhancement

# Phase 1
.claude/queues/queue_manager.sh add "Analyze add-websocket-support enhancement" "requirements-analyst" "high" "Process add-websocket-support.md through requirements analysis phase"
.claude/queues/queue_manager.sh start requirements-analyst
.claude/queues/queue_manager.sh complete task_123 "READY_FOR_DEVELOPMENT - Requirements complete" --auto-chain
# (Answer 'y' to auto-chain suggestion)

# Phase 2
.claude/queues/queue_manager.sh start cpp-developer
.claude/queues/queue_manager.sh complete task_456 "READY_FOR_IMPLEMENTATION - Architecture complete" --auto-chain
# (Answer 'y' to auto-chain suggestion)

# Phase 3
.claude/queues/queue_manager.sh start cpp-developer
.claude/queues/queue_manager.sh complete task_789 "READY_FOR_INTEGRATION - Implementation complete" --auto-chain
# (Answer 'y' to auto-chain suggestion)

# Phase 4
.claude/queues/queue_manager.sh start testing-agent
.claude/queues/queue_manager.sh complete task_101 "TESTING_COMPLETE - Enhancement validated and ready"

# Verification
.claude/queues/queue_manager.sh status
```

This template provides a complete reference for executing enhancements through our multi-agent workflow system while maintaining human oversight and quality control.