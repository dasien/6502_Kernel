# Agent Role Mapping for Enhancement Templates

## Overview

This document defines how our multi-agent system maps to the standard enhancement template roles.

## Template Roles → Agent Mapping

### **PM Subagent** ↔ **requirements-analyst**
**Primary Role**: Project management, requirements clarification, scope decisions

**Responsibilities**:
- Process "Notes for PM Subagent" sections in enhancement templates
- Analyze and clarify requirements
- Create implementation plans and staging
- Identify dependencies and constraints
- Flag scope issues and breaking changes

**Output Status**: `READY_FOR_DEVELOPMENT`

### **Architect Subagent** ↔ **assembly-developer** + **cpp-developer** (Domain-Specific)
**Primary Role**: High-level design, system architecture, technical decisions

**Responsibilities**:
- **assembly-developer** handles:
  - 6502 kernel architecture decisions
  - Memory layout and banking strategies
  - Hardware interface design
  - Performance and timing considerations
  - Low-level system integration

- **cpp-developer** handles:
  - C++ tool and framework architecture
  - Development tool design decisions
  - Testing framework architecture
  - Build system integration
  - Cross-platform compatibility

Both process "Notes for Architect Subagent" sections relevant to their domain.

**Output Status**: `READY_FOR_IMPLEMENTATION`

### **Implementer Subagent** ↔ **Dedicated Implementer Agents** (Context-Dependent)
**Primary Role**: Hands-on coding, implementation details, actual file modifications

**IMPORTANT DISTINCTION**: We now separate architect and implementer roles:
- **Architect agents** (assembly-developer, cpp-developer) create implementation plans
- **Implementer agents** execute those plans by making actual code changes

**Context-Dependent Assignment**:
- **assembly-implementer**: Assembly code implementation tasks (NEW - dedicated implementer)
- **assembly-developer**: Can do implementation but tends to default to architecture mode
- **cpp-implementer**: C++ implementation tasks (FUTURE - not yet created)
- **testing-agent**: Test implementation tasks

All process "Notes for Implementer Subagent" sections relevant to their domain.

**Output Status**:
- **assembly-implementer**: `IMPLEMENTATION_COMPLETE` or `READY_FOR_TESTING`
- **assembly-developer**: `READY_FOR_INTEGRATION`
- **cpp-implementer**: `READY_FOR_TESTING` (when created)
- **testing-agent**: `IMPLEMENTATION_COMPLETE`

### **Testing Subagent** ↔ **testing-agent**
**Primary Role**: Testing strategy, validation, quality assurance

**Responsibilities**:
- Process "Notes for Testing Subagent" sections in enhancement templates
- Design comprehensive testing strategies
- Implement unit, integration, and hardware validation tests
- Verify performance requirements and constraints
- Test backwards compatibility and regression scenarios

**Output Status**: `TESTING_COMPLETE`

## Workflow Integration

### Enhancement Processing Workflow
1. **requirements-analyst** processes PM Subagent notes → `READY_FOR_DEVELOPMENT`
2. **Domain-specific architect** (assembly or cpp) processes Architect Subagent notes → `READY_FOR_IMPLEMENTATION`
3. **Domain-specific implementer** processes Implementer Subagent notes → `READY_FOR_TESTING`/`READY_FOR_INTEGRATION`
4. **testing-agent** processes Testing Subagent notes → `TESTING_COMPLETE`

### Agent Selection Logic
When processing an enhancement template:

- **Always start with**: `requirements-analyst` (PM role)
- **Architecture phase**: Choose based on enhancement domain:
  - Kernel/hardware features → `assembly-developer`
  - Development tools/frameworks → `cpp-developer`
  - Cross-cutting concerns → Both agents
- **Implementation phase**: Same domain logic as architecture
- **Always end with**: `testing-agent` (Testing role)

## Complete Status Flow

### Updated Workflow (with dedicated implementers)

```
requirements-analyst → READY_FOR_DEVELOPMENT
         ↓
assembly-developer/cpp-developer (ARCHITECT) → READY_FOR_IMPLEMENTATION
         ↓
assembly-implementer/cpp-implementer (IMPLEMENTER) → IMPLEMENTATION_COMPLETE/READY_FOR_TESTING
         ↓
testing-agent → TESTING_COMPLETE
```

### Legacy Workflow (single agent for arch+impl)

```
requirements-analyst → READY_FOR_DEVELOPMENT
         ↓
assembly-developer/cpp-developer → READY_FOR_IMPLEMENTATION
         ↓
assembly-developer/cpp-developer → READY_FOR_INTEGRATION/READY_FOR_TESTING
         ↓
testing-agent → TESTING_COMPLETE
```

**Recommendation**: Use dedicated implementer agents when available for clearer separation of concerns.

## Template Section Ownership

| Template Section | Primary Agent | Output Status |
|------------------|---------------|---------------|
| Notes for PM Subagent | requirements-analyst | READY_FOR_DEVELOPMENT |
| Notes for Architect Subagent | assembly-developer OR cpp-developer | READY_FOR_IMPLEMENTATION |
| Notes for Implementer Subagent | **assembly-implementer** OR cpp-implementer (preferred)<br>assembly-developer OR cpp-developer (fallback) | IMPLEMENTATION_COMPLETE/READY_FOR_TESTING<br>READY_FOR_INTEGRATION/READY_FOR_TESTING |
| Notes for Testing Subagent | testing-agent | TESTING_COMPLETE |

**Note**: Prefer dedicated implementer agents when available for clearer role separation.

## Agent Communication

### Status Transitions
- `READY_FOR_DEVELOPMENT` → Triggers architecture phase
- `READY_FOR_IMPLEMENTATION` → Triggers implementation phase
- `READY_FOR_INTEGRATION` → Triggers integration testing
- `READY_FOR_TESTING` → Triggers comprehensive testing
- `TESTING_COMPLETE` → Enhancement complete

### Information Handoffs
- **Requirements → Architecture**: Implementation plan, technical constraints
- **Architecture → Implementation**: System design, interface specifications
- **Implementation → Testing**: Completed features, integration points, test requirements

## Benefits of This Approach

1. **Domain Expertise**: Agents work within their specialization areas
2. **Flexible Architecture**: Different domains can have different architects
3. **Clear Responsibilities**: Each template section has a clear owner
4. **Comprehensive Testing**: Dedicated testing agent ensures quality
5. **Workflow Integration**: Maps naturally to our existing multi-agent system
6. **Clear Status Tracking**: Each phase has defined completion criteria

## Example: New Monitor Command Enhancement

1. **requirements-analyst** processes PM notes → `READY_FOR_DEVELOPMENT`
2. **assembly-developer** processes Architect notes → `READY_FOR_IMPLEMENTATION`
3. **assembly-developer** processes Implementer notes → `READY_FOR_INTEGRATION`
4. **testing-agent** processes Testing notes → `TESTING_COMPLETE`

## Usage Guidelines

### For Enhancement Authors
- Use appropriate template sections based on the domain (kernel vs tools)
- Provide clear guidance in agent-specific notes sections
- Consider cross-domain impacts in constraint sections

### For Agent Operators
- Select the appropriate architect/implementer based on enhancement domain
- Always start with requirements-analyst (PM role)
- Always end with testing-agent (Testing role)
- Use queue system to manage workflow transitions
- Monitor status outputs to trigger next phase

This mapping ensures our agents work efficiently within their expertise while maintaining comprehensive coverage of all enhancement aspects.