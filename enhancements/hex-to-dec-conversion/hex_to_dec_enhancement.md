---
slug: hex-to-decimal-conversion
status: NEW
created: 2025-09-30
author: Brian Gentry
priority: medium
---

# Enhancement: Hex to Decimal Conversion Command

## Overview
**Goal:** Add a monitor command to convert hexadecimal values to decimal notation for quick reference during debugging and development.

**User Story:**
As a 6502 assembly programmer, I want to convert hex addresses and values to decimal so that I can quickly understand numeric magnitudes, calculate offsets in decimal, and verify address calculations without manual conversion.

## Context & Background
**Current State:**
- Monitor operates entirely in hexadecimal for all memory addresses and values
- All commands (R:, W:, G:, etc.) use 4-digit hex addresses
- Users must manually convert hex to decimal for calculations or external tool integration
- No built-in number format conversion utilities exist

**Technical Context:**
- Target platform: MFC 6502 system with 65C02 processor
- ROM available: ~680 bytes free (4096 total - 3413 used)
- RAM available: Zero page locations $E2-$EE unused by monitor and EHBasic
- Integration point: Monitor command parser (PARSE_COMMAND routine)
- Must reuse existing hex parsing and display routines

**Dependencies:**
- Existing HEX_QUAD_TO_ADDR routine for parsing hex input
- PRINT_MESSAGE and PRINT_CHAR for output display
- None (standalone feature, no external dependencies)

## Requirements

### Functional Requirements
1. Accept 4-digit hexadecimal input (0000-FFFF) via H:xxxx syntax
2. Parse and validate hex input using existing monitor hex parsing routines
3. Convert 16-bit hex value to decimal (0-65535)
4. Display decimal result to screen in human-readable format
5. Return to monitor command prompt after display
6. Handle invalid hex input with existing error messages

### Non-Functional Requirements
- **Performance:** Conversion must complete in < 100ms at 1MHz clock speed
- **Memory:** ROM footprint ≤ 150 bytes total (parsing + conversion + display)
- **Reliability:** Must validate all hex digits, handle edge cases (0000, FFFF)
- **Compatibility:** Must not break existing commands or memory layout

### Must Have (MVP)
- [ ] H:xxxx command syntax parsing
- [ ] 16-bit hex to decimal conversion algorithm
- [ ] Decimal number display (up to 5 digits: 65535)
- [ ] Integration with command parser jump table
- [ ] Error handling for invalid hex input

### Should Have (if time permits)
- [ ] Compact output format optimization
- [ ] Leading zero suppression for small values
- [ ] Alignment for readability

### Won't Have (out of scope)
- Binary output (separate feature if needed)
- Prefix detection ($xxxx vs plain hex) - all input is hex
- Multi-value conversion on one line
- Conversion history or memory of last result

## Open Questions
> These need answers before architecture review

1. Should output show both formats (e.g., "$80FF = 33023") or just decimal?
2. Should leading zeros be suppressed (e.g., "256" vs "00256")?
3. Should we add binary output for values < 256 where it's most useful?
4. What's the preferred decimal output width (left-aligned vs right-aligned in 5-char field)?

## Constraints & Limitations
**Technical Constraints:**
- Maximum memory usage: 150 bytes ROM
- Must not modify: Zero page $00-$10, $F0-$FF
- Must use: Existing command parser pattern (PARSE_COLON_COMMAND)
- Must preserve: All existing monitor functionality

**Business/Timeline Constraints:**
- Should coordinate with D: (decimal-to-hex) command for consistent UX
- Part of larger calculator/utility enhancement theme

## Success Criteria
**Definition of Done:**
- [ ] H:xxxx command parses correctly and displays decimal result
- [ ] All 16-bit values (0000-FFFF) convert accurately
- [ ] Invalid hex input shows appropriate error message
- [ ] Code size within 150-byte budget
- [ ] No regressions in existing monitor commands
- [ ] Help text updated with new command

**Acceptance Tests:**
1. Given command "H:0000", when executed, then displays "0"
2. Given command "H:FFFF", when executed, then displays "65535"
3. Given command "H:0100", when executed, then displays "256"
4. Given command "H:80FF", when executed, then displays "33023"
5. Given command "H:GGGG", when executed, then displays "VALUE?" error
6. Given command "H:", when executed, then displays error or uses current address
7. Edge case: H:8000 displays "32768" (signed/unsigned boundary)

## Security & Safety Considerations
- Input validation: Ensure all characters are valid hex (0-9, A-F)
- Buffer safety: Reuse existing parsing routines that validate length
- Error handling: Use existing error message system (MSG_VALUE_ERROR)
- No memory corruption: Conversion uses only scratch variables

## UI/UX Considerations
- **Input format:** H:xxxx (consistent with existing R:, W:, G: commands)
- **Output format:** Simple decimal number, optionally with hex reference
- **Error messages:** Reuse existing "VALUE?" message for invalid hex
- **Prompt behavior:** Return to normal monitor prompt after display
- **Help integration:** Add "H:XXXX HEX TO DECIMAL" to help display

## Testing Strategy
**Unit Tests:**
- Hex parsing: Valid inputs (0000, FFFF, 8000, A5A5)
- Hex parsing: Invalid inputs (GGGG, 123, 12345)
- Conversion accuracy: Boundary values (0, 255, 256, 32767, 32768, 65535)
- Conversion accuracy: Random sampling across full range

**Integration Tests:**
- Command parsing integration with existing parser
- Error handling integration with existing error system
- Display integration with screen output routines
- No interference with other commands (before/after H: command)

**Manual Test Scenarios:**
1. Enter "H:0000", verify displays "0" or "00000"
2. Enter "H:FFFF", verify displays "65535"
3. Enter "H:0100", use result to verify screen offset calculation
4. Enter "H:ZZZZ", verify shows VALUE? error
5. Rapid sequential conversions to test stability
6. H: command followed by other commands to test state cleanup

## References & Research
- Classic 6502 monitors (Woz Monitor, SuperMon) - typically hex-only
- Modern conversion utilities show value in multiple formats
- 16-bit binary-to-decimal conversion algorithms for 6502
- Existing monitor code: HEX_QUAD_TO_ADDR for parsing pattern
- Existing monitor code: PRINT_MESSAGE for output pattern

## Notes for PM Subagent
> Instructions for how to process this enhancement

- Coordinate output format with D: (decimal-to-hex) command for consistency
- Consider whether binary output should be included or left for future enhancement
- Verify 150-byte budget is adequate for conversion algorithm chosen
- Flag if showing both hex and decimal simultaneously seems redundant

## Notes for Architect Subagent
> Key architectural considerations

- Reuse existing HEX_QUAD_TO_ADDR for parsing hex input (zero page: MON_CURRADDR_LO/HI)
- Design 16-bit binary-to-decimal conversion using division by 10 method
- Consider using monitor scratch variables ($0259-$028F range) for conversion workspace
- Optimize for code size over speed (conversions are infrequent)
- Pattern output after existing memory dump formatting for consistency
- Add to command jump table (CMD_JUMP_COMPACT_LO/HI, CMD_INDEX_MAP)
- Ensure no conflicts with future calculator command if implemented

## Notes for Implementer Subagent
> Implementation guidance

- Reuse PARSE_COLON_COMMAND pattern from existing commands
- Implement binary-to-decimal using repeated division by 10
- Store decimal digits in temporary buffer, print in reverse
- Suppress leading zeros for cleaner output
- Use existing PRINT_CHAR for output character-by-character
- Add command entry to MODE_PREFIX_TABLE if needed
- Update HELP_MSG_TABLE with new command
- Test with edge cases: 0000, FFFF, 8000, 0001

## Notes for Testing Subagent
> Testing and validation guidance

- Focus testing on boundary values (0, 255, 256, 32767, 32768, 65535)
- Validate conversion accuracy with independent decimal calculator
- Test error handling with invalid hex input (non-hex characters)
- Verify no memory corruption in scratch areas
- Test integration with command parser (command before/after H:)
- Include timing tests to verify < 100ms completion at 1MHz
- Verify ROM size increase stays within budget
- Test display formatting at various cursor positions
- Manual hardware testing if emulator differs from real hardware behavior