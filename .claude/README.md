# 6502 Kernel Multi-Agent System

This directory contains a comprehensive multi-agent system for managing the 6502 Kernel development project. The system implements specialized agents with automated workflow transitions through hooks.

## Architecture Overview

The multi-agent system follows the single-responsibility principle with four specialized agents:

1. **Requirements Analyst** - Requirements analysis and implementation planning
2. **C++ Developer** - Systems programming and development tools
3. **6502 Assembly Developer** - Low-level kernel and hardware programming
4. **Testing Agent** - Comprehensive testing and validation

## Agent Specifications

### Requirements Analyst (`requirements-analyst.md`)
- **Purpose**: Analyzes requirements, creates implementation plans, manages project scope
- **Tools**: Read, Write, Glob, Grep, WebSearch, WebFetch
- **Outputs**: `READY_FOR_DEVELOPMENT` status
- **Use Case**: Start of new features or major project phases

### C++ Developer (`cpp-developer.md`)
- **Purpose**: Expert C++ programming for development tools and emulation
- **Tools**: Read, Write, Edit, MultiEdit, Bash, Glob, Grep, Task
- **Outputs**: `READY_FOR_TESTING` status
- **Use Case**: Building development tools, simulators, testing frameworks

### 6502 Assembly Developer (`assembly-developer.md`)
- **Purpose**: Low-level kernel programming and hardware interfacing
- **Tools**: Read, Write, Edit, MultiEdit, Bash, Glob, Grep, WebSearch, WebFetch
- **Outputs**: `READY_FOR_INTEGRATION` status
- **Use Case**: Kernel code, hardware drivers, monitor programs

### Testing Agent (`testing-agent.md`)
- **Purpose**: Comprehensive testing including unit, integration, hardware validation
- **Tools**: Read, Write, Edit, MultiEdit, Bash, Glob, Grep, Task
- **Outputs**: `TESTING_COMPLETE` status
- **Use Case**: Validation of all implemented components

## Workflow Patterns

### Sequential Development Flow
```
Requirements Analyst → C++ Developer → Testing Agent
                    → Assembly Developer → Testing Agent
```

### Parallel Development Flow
```
Requirements Analyst → C++ Developer     → Testing Agent
                    ↘ Assembly Developer ↗
```

### Integration Flow
```
C++ Developer + Assembly Developer → Testing Agent → READY_FOR_RELEASE
```

## Hook System

The project implements automated workflow transitions through two main hooks:

### SubagentStop Hook (`hooks/on-subagent-stop.sh`)
- Triggers when any subagent completes its task
- Analyzes agent output for status markers
- Suggests next steps based on completion status
- Manages workflow transitions between agents

### General Stop Hook (`hooks/on-stop.sh`)
- Triggers on any command or agent completion
- Provides project status assessment
- Suggests contextual next actions
- Lists available agents and their purposes

## Usage Instructions

### Starting a New Feature
1. Launch Requirements Analyst agent:
   ```
   Use Task tool with subagent_type: "requirements-analyst"
   ```
2. Follow hook suggestions for next steps
3. Launch appropriate development agents based on requirements

### Development Phase
- **For C++ components**: Use `cpp-developer` agent
- **For Assembly components**: Use `assembly-developer` agent
- **For parallel development**: Launch both agents simultaneously

### Testing Phase
- Launch `testing-agent` after development completion
- Follow testing protocols for comprehensive validation
- Validate both components and integration

### Status Tracking
The hook system provides automatic status tracking with these states:
- `READY_FOR_DEVELOPMENT` - Requirements complete, ready to code
- `READY_FOR_TESTING` - Implementation complete, ready to test
- `READY_FOR_INTEGRATION` - Assembly complete, ready to integrate
- `TESTING_COMPLETE` - All testing passed, ready for release

## Agent Communication

Agents communicate through:
1. **Status Markers**: Standardized completion messages
2. **Hook System**: Automated workflow transitions
3. **Shared Documentation**: Common understanding of project structure
4. **File-based Handoffs**: Agents work on shared codebase

## Best Practices

### Agent Selection
- Use the **most specialized agent** for each task
- Consider **parallel execution** for independent components
- Follow **hook suggestions** for optimal workflow

### Task Handoffs
- Ensure **clear completion status** from each agent
- Review **agent output** before proceeding to next phase
- Use **hooks for guidance** on next steps

### Quality Assurance
- Always run **Testing Agent** before considering work complete
- Validate **integration points** between C++ and Assembly components
- Follow **project-specific standards** documented in each agent spec

## Project-Specific Configuration

### Memory Constraints
All agents understand the 6502's 64KB memory limitations and project memory map.

### Hardware Context
Agents are configured for Commodore 64 hardware (VIC-II, SID, CIA) or compatible systems.

### Build System Integration
All development agents integrate with the CMake/Ninja build system.

### Testing Requirements
Testing agent includes both automated testing and hardware validation protocols.

## Troubleshooting

### Hook Execution Issues
- Verify hook scripts are executable: `chmod +x .claude/hooks/*.sh`
- Check JSON syntax in `settings.local.json`
- Review hook output in Claude Code console

### Agent Selection Confusion
- Review agent specifications in `.claude/agents/` directory
- Check hook suggestions for guidance
- Use general-purpose agents for unclear tasks

### Status Transition Problems
- Verify agents output correct status markers
- Check hook logic for status recognition
- Manual intervention may be required for custom workflows

## Future Extensions

The system is designed for extensibility:
- Additional specialized agents can be added to `.claude/agents/`
- New hooks can be created for custom workflows
- Agent tools and permissions can be adjusted as needed
- Status markers and workflow transitions can be customized

## Integration with Existing Project

This multi-agent system integrates with:
- **CLAUDE.md**: Project documentation and context
- **CMake Build System**: Build integration and testing
- **Git Workflow**: Version control and collaboration
- **Hardware Testing**: Physical validation protocols

The system enhances the existing development workflow without disrupting established practices.