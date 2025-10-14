---
slug: add-basic
status: NEW
created: 2025-09-28
author: Brian Gentry
priority: high
---

# Enhancement: Add BASIC language support to kernel via BASIC interpreter

## Overview
**Goal:** Integrate BASIC language to the kernal to allow users to activate BASIC language with a B: command and let the user 
enter and run BASIC programs.

**User Story:**
As a Kernel User, I want to run the BASIC interpreter using a B: command so that 
I can enter and run BASIC programs.  BASIC is an easier language to learn than assembly
so it will be nice to have the option to use that language.

## Context & Background
**Current State:**
- Today the monitor rom is missing a BASIC interpreter.
- The monitor has a function for printing characters and reading the keyboard at $FF00 and $FF09 respectively which BASIC will like need to hook into.
- The monitor makes use of zero page space and so does BASIC and those locations cannot be used by both.
- Other context about monitor architecture and features can be found in docs/kernel_memory_map.md
- This is an important addition to the monitor as it provides a languagae option other than raw hex input of assembly programs.

**Technical Context:**
- This is intended to run in our 6502 emulated computer
- Existing variable locations used by the monitor in zero page, stack, and other memory locations in docs/kernel_memory_map.md must be respected and not overwritten
- The BASIC system must integrate with existing monitor routines for character printing and keyboard reading
- Size permitting of the assembled BASIC file, BASIC should be loaded into the computer at $C000.  If that location does not work because the BASIC rom is too large and will overwrite the kernel at $F000, it can be relocated.
- There are memory overlaps that will have to be remediated
- IMPORTANT the zero page locations which are shared by both BASIC and the monitor must be resolved.  Check to make sure that any relocation of variable locations are not used by the other system.  For example, if you move monitor variables, you must make sure that they are not assigned either directory or via math (VAR+1 in assembly) by BASIC

**SIMPLIFIED MEMORY APPROACH:**
- Monitor command buffer ($0200-$024F) can OVERLAP with BASIC variables ($0200-$0268) because the monitor command buffer is not needed while BASIC is running
- Do not recommend a context-switching solution for zero page.  If you need to move monitor zero page addresses, move them so they are all one contiguous block.  Do not only move the addresses that are in conflict with BASIC
- Monitor variables have been relocated to $0269-$02DE to avoid conflicts with BASIC
- When exiting BASIC and returning to monitor, the system MUST clear/reinitialize the monitor command buffer and reset MON_CMDPTR, MON_CMDLEN to clean state

- **Dependencies:**
- The BASIC source file is located in src/kernel/basic.asm
- The same tools used to assemble the kernel.rom should be used (CC65 toolset)

**BUILD SYSTEM REQUIREMENTS:**
- BASIC should be built as a separate basic.rom file (NOT combined with kernel.rom)
- Create basic_memory.cfg in src/kernel/ directory (same location as existing kernel build files)
- Follow the exact same build pattern as kernel.rom (parallel process, not combined)
- C++ loader code will handle loading both kernel.rom and basic.rom separately into computer memory
- DO NOT create a new /config directory or system.rom - use existing build patterns

## Requirements

### Functional Requirements
1. The user should have the ability to launch the BASIC interpreter via the B: command
2. The interpreter should run and allow the user to enter BASIC programs and run them
3. The user can exit BASIC and return to a monitor command prompt

### Non-Functional Requirements
- **Memory:** The BASIC ROM and monitor must not use the same memory space for variable storage.  It is critical this is double checked before any recommendations are made
- **Compatibility:** There are input and output routines that will have to be mapped to existing montior routines and memory use collisions that need to be remediated.

### Must Have (MVP)
- [ ] Must launch with the B: command from the monitor
- [ ] Must not use zero page or other memory space in both the monitor and BASIC
- [ ] Must integrate with the monitor's PRINT_CHAR and GET_KEYSTROKE for printing characters to the screen and reading keyboard input
- [ ] Once launched, the user must be able to type programs in basic and run them
- [ ] Must properly clean up monitor command buffer state when returning from BASIC to monitor mode

### Should Have (if time permits)
- [ ] The ability for a user to load or save a basic program using wrappers to the monitor's existing load/save routines

### Won't Have (out of scope)
- None

## Open Questions
> These need answers before architecture review

## Constraints & Limitations
**Technical Constraints:**
- Maximum memory usage: There is no technical limit on memory usage, but must adhere to the constraints in docs/kernel_memory_map.md
- Must not break: Existing monitor features & commands
- Must use: Monitor routines for getting keyboard input and printing characters
- The location for BASIC in memory must not overwrite the monitor program, which starts at $F000
- 
**Business/Timeline Constraints:**
- None
- 
## Success Criteria
**Definition of Done:**
- [ ] Core functionality works as described
- [ ] All acceptance tests pass
- [ ] Documentation updated
- [ ] No regressions in existing features
- [ ] Performance metrics met

**Acceptance Tests:**
1. Given startup of the computer, when the user enters 'B:' and presses Enter, then the BASIC interpreter launches

## Security & Safety Considerations
- None

## UI/UX Considerations (if applicable)
- Upon starting the computer, the user should see the welcome message.  From the main prompt the user types B: to enter the BASIC interpreter.
- The user should exit basic using a warm restart from within the interpreter or some other command as approprriate to return to the montior prompt.
- We will need to document the B: command and any details about it as part of a *_command.md file and update the command_help.md, kernel_flow, kernel_command_infrastructure and the readme.md file at the root of the project
- We will need to update the help command located in the monitor with this new command as well

## Testing Strategy
**Unit Tests:**
- None

**Integration Tests:**
- None

**Manual Test Scenarios:**
1. Run the emulated computer
2. At the main prompt, type 'B:' command
3. Observe that the BASIC interpreter is launched and working
4. Type a short BASIC program and run it to see if it works
5. Exit BASIC using a to be definted method and observe that the monitor prompt shows up again and we are back in the montior program

## References & Research
- [Kernel Memory Map](../../docs/kernel_memory_map.md)
- [Kernel Flow](../../docs/kernel_flow.md)
- [Kernel Command Infrastructure](../../docs/kernel_command_infrastructure.md)
- [Help Command Doc](../../docs/command_help.md)
- [Main Readme file](../Readme.md)
- [BASIC assembly source](../../src/kernel/basic.asm)
- [EHBasic Website](http://www.6502.org/users/mycorner/6502/ehbasic/index.html)
- [Example of porting EHBASIC](https://mike42.me/blog/2021-09-porting-basic-to-my-6502-computer)

## MANDATORY DOCUMENT HEADER (ALL AGENTS - NO EXCEPTIONS)

**CRITICAL**: Every single document you create MUST start with this exact header format:

```markdown
---
enhancement: add-basic-interpreter
task_id: [YOUR_TASK_ID]
agent: [YOUR_AGENT_NAME]
created: [CURRENT_DATETIME]
---
```

**EXAMPLE FOR TASK task_1759348065_71462 by assembly-developer:**
```markdown
---
enhancement: add-basic-interpreter
task_id: task_1759348065_71462
agent: assembly-developer
created: 2025-10-01 20:22:05
---
```

**FAILURE TO INCLUDE THIS HEADER MAKES YOUR DOCUMENT INVALID AND UNUSABLE.**

## Notes for PM Subagent
> Instructions for how to process this enhancement

**FOCUS ON BUSINESS REQUIREMENTS - NOT TECHNICAL SOLUTIONS**

- Analyze WHAT the user wants (B: command functionality, BASIC program execution)
- Create user stories and acceptance criteria
- Identify THAT memory conflicts exist between monitor and BASIC (don't solve them)
- Flag areas requiring technical specialist input (memory layout, I/O integration)
- Define success criteria and testing requirements
- **DO NOT make specific technical implementation decisions**
- **DO NOT choose memory addresses or system components to modify**
- **DEFER all technical HOW decisions to architecture specialists**
- If any requirement or context reading is unclear, ask before proceeding

## Notes for Architect Subagent
> Key architectural considerations

**AGENT ASSIGNMENT**: This enhancement requires assembly-developer for architecture design and implementation phases due to 6502 assembly programming requirements.

**CRITICAL: Follow this exact methodology to avoid missing memory conflicts**

### Memory Conflict Analysis Methodology (MANDATORY STEPS)

#### Step 1: Analyze basic.asm Memory Usage
1. **Zero Page Analysis**: Read src/kernel/basic.asm and extract ALL zero page variable definitions
   - Search for patterns like: `VARNAME = $XX` where XX is 00-FF
   - Search for `.EQU` or `.SET` directives with zero page addresses
   - **CRITICAL: Search for calculated addresses**: `VARNAME = OTHERVAR+1`, `VARNAME = OTHERVAR+2`, etc.
   - **CRITICAL: Check ALL comments**: Comments like "; = $XX" or "; XXXX end" indicate usage
   - **CRITICAL: Trace dependency chains using this algorithm**:
     ```
     FOR each variable assignment in basic.asm:
       IF assignment is "VAR = $XX" THEN mark $XX as USED by VAR
       IF assignment is "VAR = OTHERVAR+N" THEN:
         1. Find OTHERVAR's address (call it BASE)
         2. Calculate actual address = BASE + N
         3. Mark calculated address as USED by VAR
         4. Create usage entry: "$XX used by VAR (calculated from OTHERVAR+N)"
     ```
     Example: `Itempl = $11`, `Itemph = Itempl+1`, `nums_1=Itempl`, `nums_2 = nums_1+1`, `nums_3 = nums_1+2`
     - Step 1: Mark $11 used by Itempl
     - Step 2: Find Itempl=$11, calculate $11+1=$12, mark $12 used by Itemph
     - Step 3: Find nums_1=$11, calculate $11+2=$13, mark $13 used by nums_3
   - **CRITICAL: Check for implied ranges**: If code shows `Decss = $EF` and comments mention `$FF decimal string end`, assume $EF-$FF range is used
   - **CRITICAL EXAMPLE**: Monitor HEX_LOOKUP_TABLE ($F0-$FF) CONFLICTS with BASIC Decss=$EF, Decssp1=$F0. The range $EF-$FF is used by BASIC decimal string processing. DO NOT relocate monitor variables to $E2-$F1 or similar - this creates conflicts with BASIC's $EF-$FF usage.
   - Document every single zero page location used with the ACTUAL source (direct assignment, calculated, or comment-indicated)
   - Play close attention to gaps in usage.  Do not just assume that because the first variable is assigned $00 and the last is $E0 that a system uses all the memory between
   - Look at the comments for variable assignment in the files as they often indicate length and thus an implied usage of the memory after the declared address.  This is often the case where the start address of a buffer is declared and the comments will indicate buffer length
   - Consider all duplicate assignments as unsafe.  If an address is used in the montior and BASIC, it should be considered critical to resolve
   - **CREATE A COMPLETE ADDRESS MAP**: List EVERY address from $00-$FF showing what uses it (BASIC var name, Monitor var name, or UNUSED)
  
2. **Extended Memory Analysis**: Identify all memory ranges basic.asm uses
   - Stack usage patterns
   - Variable storage areas (especially $0200-$03FF range)
   - Buffer locations
   - I/O workspace areas

#### Step 2: Analyze Monitor Memory Usage
1. **Read kernel_memory_map.md completely** - document current monitor usage
2. **Read kernel.asm** - extract actual variable definitions and usage
3. **Create comprehensive map** of monitor memory usage:
   - Zero page: exact addresses and what each is used for
   - $0200-$03FF: command buffers, variables, workspace
   - Any other memory areas used

#### Step 3: Conflict Detection (SYSTEMATIC APPROACH)
1. **Zero Page Conflicts**: Create a table showing:
   ```
   Address | Monitor Usage | BASIC Usage | Conflict? | Resolution Plan
   $20     | MON_VAR1     | BAS_VAR1   | YES       | Move MON_VAR1 to $XX
   $21     | MON_VAR1+1   | BAS_VAR2   | YES       | Move MON_VAR1 to $XX
   ```

2. **$0200-$02FF Range Analysis**:
   - Monitor uses: $0200-$024F (command buffer), $0250-$027E (variables)
   - BASIC uses: Document exactly what BASIC uses in this range
   - Identify ALL overlapping areas
   - Create relocation plan for conflicts

#### Step 4: Create Relocation Strategy
1. **Find unused zero page locations**: Scan $00-$FF for gaps not used by either system
2. **Plan monitor variable moves**: Choose new addresses that don't conflict with BASIC
3. **Validate relocation addresses**: Ensure new locations don't create secondary conflicts
4. **Document all changes required**: List every monitor variable that needs relocation

#### Step 5: Integration Planning
- I/O integration with monitor's PRINT_CHAR ($FF00) and GET_KEYSTROKE ($FF09)
- Build process integration
- Memory layout documentation updates

### Critical Requirements
- **ZERO PAGE CONFLICTS MUST BE COMPLETELY RESOLVED** - no overlapping usage allowed
- **ALL $0200-$02FF conflicts must be identified and resolved**
- **Document EVERY memory address change required**
- **Validate that relocated addresses don't create new conflicts**
- **Provide complete before/after memory maps**

### Deliverables Required
1. **Memory Conflict Analysis Report**: Complete table of all conflicts found
2. **Relocation Plan**: Exact addresses for all moved variables
3. **Updated Memory Map**: New layout with all changes documented
4. **Integration Architecture**: How BASIC calls monitor I/O functions
5. **Build Process Design**: How to assemble and load BASIC ROM

## Document Creation Standards (ALL AGENTS)
> Required metadata header for all generated documents

**MANDATORY**: Every document created by any agent MUST include this header at the top:

```markdown
---
enhancement: add-basic-interpreter
task_id: [TASK_ID]
agent: [AGENT_NAME]
created: [YYYY-MM-DD HH:MM:SS]
---
```

Example:
```markdown
---
enhancement: add-basic-interpreter
task_id: task_1759337100_69981
agent: assembly-developer
created: 2025-10-01 16:45:08
---
```

This helps track document provenance and versions across the multi-agent workflow.

## Notes for Implementer Subagent
> Implementation guidance

- Reuse existing command patterns found in (../docs/kernel_command_infrastructure.md) where possible
- Add tests in (../tests/test_advanced_commands.cpp) to launch the BASIC interpreter and type a short program (ex. PRINT "HELLO") and run it. 
- Update the following documentation
  - [Kernel Flow](../../docs/kernel_flow.md)
  - [Kernel Command Infrastructure](../../docs/kernel_command_infrastructure.md)
  - [Help Command Doc](../../docs/command_help.md)
  - [Main Readme file](../Readme.md)
- Create a new document which follows others in the (../docs) folder called basic_command.md and describes the command in detail similar to how other *_command.md files appear