# Queue System Command Reference - Test Workflow Demo

**Enhancement:** test-workflow-demo
**Purpose:** Document all queue management commands used during the workflow test
**Date:** 2024-09-29

## Overview

This document provides a complete command reference for operating the multi-agent workflow system, based on the actual commands executed during our test workflow demo validation.

## Initial System State Check

### Command 1: Check Queue Status
```bash

```

**Purpose**: Check initial state of queue system and agent statuses
**Parameters**: None (status is the default command)
**Output**:
- Agent statuses (all idle initially)
- Pending tasks (found one old test task)
- Active workflows (none)
- Recently completed tasks (none initially)

**Result**: Confirmed system operational, found existing test task from previous session

## Phase 1: Requirements Analysis

### Command 2: Add Requirements Analysis Task
```bash
.claude/queues/queue_manager.sh add "Analyze test workflow demo enhancement" "requirements-analyst" "high" "Process test-workflow-demo.md through requirements analysis phase"
```

**Purpose**: Create new task for requirements analysis phase
**Parameters**:
- `add`: Command to add new task
- `"Analyze test workflow demo enhancement"`: Task title (descriptive name)
- `"requirements-analyst"`: Agent assignment (must match agent name)
- `"high"`: Priority level (critical/high/normal/low)
- `"Process test-workflow-demo.md through requirements analysis phase"`: Detailed task description

**Output**: Returns new task ID: `task_1759160255_23715`
**Result**: Task created and queued for requirements-analyst

### Command 3: Clean Up Old Task
```bash
.claude/queues/queue_manager.sh complete task_1758907245_4922 "Old test completed, proceeding to demo workflow"
```

**Purpose**: Complete and remove old test task to clear the queue
**Parameters**:
- `complete`: Command to mark task as completed
- `task_1758907245_4922`: Task ID to complete
- `"Old test completed, proceeding to demo workflow"`: Completion message/result

**Result**: Old task moved to completed_tasks, queue cleaned

### Command 4: Start Requirements Analysis
```bash
.claude/queues/queue_manager.sh start requirements-analyst
```

**Purpose**: Assign next pending task to requirements-analyst and mark as active
**Parameters**:
- `start`: Command to begin next task for specified agent
- `requirements-analyst`: Agent name to start working

**Output**: Returns active task ID: `task_1759160255_23715`
**Result**:
- Task moved from pending_tasks to active_workflows
- Agent status changed from idle to active
- Task started timestamp recorded

### Command 5: Complete Requirements Analysis
```bash
.claude/queues/queue_manager.sh complete task_1759160255_23715 "READY_FOR_DEVELOPMENT - Requirements analysis complete, implementation plan created, next: assembly-developer (architect role)"
```

**Purpose**: Mark requirements analysis as complete with status output
**Parameters**:
- `complete`: Command to complete active task
- `task_1759160255_23715`: Task ID to complete
- `"READY_FOR_DEVELOPMENT - Requirements analysis complete, implementation plan created, next: assembly-developer (architect role)"`: Status output and handoff information

**Result**:
- Task moved from active_workflows to completed_tasks
- Agent status changed from active to idle
- Status output captured for workflow progression
- Completion timestamp recorded

## Phase 2: Architecture Design

### Command 6: Add Architecture Task
```bash
.claude/queues/queue_manager.sh add "Architecture design for test workflow demo" "assembly-developer" "high" "Design demonstration file structure and content (architect role)"
```

**Purpose**: Create task for architecture design phase
**Parameters**:
- `add`: Command to add new task
- `"Architecture design for test workflow demo"`: Task title
- `"assembly-developer"`: Agent assignment (architect role)
- `"high"`: Priority level
- `"Design demonstration file structure and content (architect role)"`: Task description with role specification

**Output**: Returns new task ID: `task_1759161885_24168`
**Result**: Architecture task queued for assembly-developer

### Command 7: Start Architecture Phase
```bash
.claude/queues/queue_manager.sh start assembly-developer
```

**Purpose**: Begin architecture design work
**Parameters**:
- `start`: Command to start next task
- `assembly-developer`: Agent name (acting as architect)

**Output**: Returns active task ID: `task_1759161885_24168`
**Result**: Assembly-developer begins architecture work

### Command 8: Complete Architecture Phase
```bash
.claude/queues/queue_manager.sh complete task_1759161885_24168 "READY_FOR_IMPLEMENTATION - Architecture design complete, file structure planned, implementation guidance provided"
```

**Purpose**: Complete architecture phase with status transition
**Parameters**:
- `complete`: Complete active task command
- `task_1759161885_24168`: Architecture task ID
- `"READY_FOR_IMPLEMENTATION - Architecture design complete, file structure planned, implementation guidance provided"`: Status output and handoff

**Result**: Architecture complete, ready for implementation phase

## Phase 3: Implementation

### Command 9: Add Implementation Task
```bash
.claude/queues/queue_manager.sh add "Implementation of test workflow demo" "assembly-developer" "high" "Create demonstration files following architecture design (implementer role)"
```

**Purpose**: Create implementation task (same agent, different role)
**Parameters**:
- `add`: Add new task command
- `"Implementation of test workflow demo"`: Task title
- `"assembly-developer"`: Same agent, now implementer role
- `"high"`: Priority level
- `"Create demonstration files following architecture design (implementer role)"`: Task description with role clarification

**Output**: Returns new task ID: `task_1759162574_24382`
**Result**: Implementation task queued

### Command 10: Start Implementation Phase
```bash
.claude/queues/queue_manager.sh start assembly-developer
```

**Purpose**: Begin implementation work
**Parameters**:
- `start`: Start next task command
- `assembly-developer`: Agent name (now as implementer)

**Output**: Returns active task ID: `task_1759162574_24382`
**Result**: Implementation phase begins

### Command 11: Complete Implementation Phase
```bash
.claude/queues/queue_manager.sh complete task_1759162574_24382 "READY_FOR_INTEGRATION - Implementation complete, all demonstration files created with comprehensive 6502 patterns and examples"
```

**Purpose**: Complete implementation with detailed status
**Parameters**:
- `complete`: Complete task command
- `task_1759162574_24382`: Implementation task ID
- `"READY_FOR_INTEGRATION - Implementation complete, all demonstration files created with comprehensive 6502 patterns and examples"`: Comprehensive status output

**Result**: Implementation complete, ready for testing

## Phase 4: Testing & Validation

### Command 12: Add Testing Task
```bash
.claude/queues/queue_manager.sh add "Validate test workflow demo completion" "testing-agent" "critical" "Test workflow system validation and comprehensive verification"
```

**Purpose**: Create comprehensive testing and validation task
**Parameters**:
- `add`: Add task command
- `"Validate test workflow demo completion"`: Task title
- `"testing-agent"`: Testing specialist agent
- `"critical"`: Highest priority level
- `"Test workflow system validation and comprehensive verification"`: Comprehensive testing scope

**Output**: Returns new task ID: `task_1759164036_24740`
**Result**: Testing task queued with critical priority

### Command 13: Start Testing Phase
```bash
.claude/queues/queue_manager.sh start testing-agent
```

**Purpose**: Begin final testing and validation
**Parameters**:
- `start`: Start task command
- `testing-agent`: Testing specialist agent

**Output**: Returns active task ID: `task_1759164036_24740`
**Result**: Testing phase active

### Command 14: Complete Testing Phase
```bash
.claude/queues/queue_manager.sh complete task_1759164036_24740 "TESTING_COMPLETE - Multi-agent workflow system fully validated, all components operational, ready for production use"
```

**Purpose**: Complete entire workflow with final status
**Parameters**:
- `complete`: Complete task command
- `task_1759164036_24740`: Testing task ID
- `"TESTING_COMPLETE - Multi-agent workflow system fully validated, all components operational, ready for production use"`: Final system validation status

**Result**: Complete workflow validated, system ready for production

## Final System Verification

### Command 15: Final Status Check
```bash
.claude/queues/queue_manager.sh status
```

**Purpose**: Verify final system state and completed workflow
**Parameters**: None (status command)
**Output**:
- All agents idle
- No pending or active tasks
- 4 completed tasks showing complete workflow progression
- Agent activity timestamps showing progression through system

**Result**: System clean, workflow complete, ready for next enhancement

## Command Pattern Summary

### Standard Workflow Pattern
```bash
# 1. Check system status
.claude/queues/queue_manager.sh status

# 2. Add phase task
.claude/queues/queue_manager.sh add "<task_title>" "<agent_name>" "<priority>" "<description>"

# 3. Start agent work
.claude/queues/queue_manager.sh start <agent_name>

# 4. Complete with status output
.claude/queues/queue_manager.sh complete <task_id> "<STATUS_OUTPUT - description>"

# 5. Repeat for next phase
```

### Parameter Guidelines

**Task Titles**: Descriptive, phase-specific
- "Analyze [enhancement name]"
- "Architecture design for [enhancement name]"
- "Implementation of [enhancement name]"
- "Validate [enhancement name] completion"

**Agent Names**: Must match agent specifications
- `requirements-analyst`: PM role
- `assembly-developer`: Architect or implementer role
- `cpp-developer`: C++ focused architect/implementer
- `testing-agent`: Testing and validation

**Priorities**: Based on urgency and importance
- `critical`: Emergency fixes, final validation
- `high`: Important features, development phases
- `normal`: Regular development work
- `low`: Nice-to-have improvements

**Status Outputs**: Follow agent role mapping
- `READY_FOR_DEVELOPMENT`: From requirements-analyst
- `READY_FOR_IMPLEMENTATION`: From architect agents
- `READY_FOR_INTEGRATION`: From assembly implementer
- `READY_FOR_TESTING`: From C++ implementer
- `TESTING_COMPLETE`: From testing-agent

## Queue System Verification Commands

### Monitoring Commands
```bash
# Check overall queue status
.claude/queues/queue_manager.sh status

# View agent logs for specific enhancement
tail -f enhancements/test-workflow-demo/logs/*.log
ls -lt enhancements/*/logs/*.log | head -10

# Check queue operations log
tail -f .claude/logs/queue_operations.log
```

### Debugging Commands
```bash
# Validate queue file syntax
jq '.' .claude/queues/task_queue.json

# Check specific task details
jq '.completed_tasks[] | select(.id == "task_1759160255_23715")' .claude/queues/task_queue.json
```

This command reference provides the complete operational guide for managing multi-agent workflows using our queue system.