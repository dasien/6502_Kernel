---
slug: decimal-to-hex-conversion
status: NEW
created: 2025-09-30
author: Brian Gentry
priority: medium
---

# Enhancement: Decimal to Hex Conversion Command

## Overview
**Goal:** Add a monitor command to convert decimal values to hexadecimal notation for address calculation and memory layout planning.

**User Story:**
As a 6502 assembly programmer, I want to convert decimal values to hex addresses so that I can calculate memory offsets from decimal sizes (e.g., "where does 1024 bytes fit in memory?"), convert screen positions to addresses, and translate specifications given in decimal to hex addresses.

## Context & Background
**Current State:**
- Monitor operates entirely in hexadecimal for all memory addresses and values
- All commands (R:, W:, G:, etc.) require 4-digit hex addresses
- Users must manually convert decimal sizes/offsets to hex
- Common calculations like "1024 bytes from $8000" require external conversion
- No built-in number format conversion utilities exist

**Technical Context:**
- Target platform: MFC 6502 system with 65C02 processor
- ROM available: ~680 bytes free (4096 total - 3413 used)
- RAM available: Zero page locations $E2-$EE unused by monitor and EHBasic
- Integration point: Monitor command parser (PARSE_COMMAND routine)
- Companion to H: (hex-to-decimal) command for bidirectional conversion

**Dependencies:**
- Existing PRINT_CURRENT_ADDRESS routine for hex output
- PRINT_MESSAGE and PRINT_CHAR for output display
- hex-to-decimal-conversion enhancement (companion feature)

## Requirements

### Functional Requirements
1. Accept decimal input (0-65535) via D:nnnnn syntax
2. Parse and validate decimal input (only digits 0-9)
3. Convert decimal value to 16-bit hex (0000-FFFF)
4. Display hex result in 4-digit format
5. Return to monitor command prompt after display
6. Handle invalid decimal input with appropriate error messages
7. Handle out-of-range values (> 65535) with error

### Non-Functional Requirements
- **Performance:** Conversion must complete in < 150ms at 1MHz clock speed
- **Memory:** ROM footprint ≤ 200 bytes total (parsing + conversion + display)
- **Reliability:** Must validate all input digits, handle edge cases (0, 65535, 65536)
- **Compatibility:** Must not break existing commands or memory layout

### Must Have (MVP)
- [ ] D:nnnnn command syntax parsing
- [ ] Decimal string to 16-bit binary conversion algorithm
- [ ] Hex number display in 4-digit format (reuse existing routine)
- [ ] Integration with command parser jump table
- [ ] Error handling for invalid decimal input
- [ ] Error handling for out-of-range values (> 65535)

### Should Have (if time permits)
- [ ] Accept leading zeros (D:00256 same as D:256)
- [ ] Skip leading spaces before number
- [ ] Show both formats in output (e.g., "256 = $0100")

### Won't Have (out of scope)
- Negative number support (monitor works with unsigned addresses)
- Binary output format (separate feature if needed)
- Scientific notation support
- Multiple values on one line

## Open Questions
> These need answers before architecture review

1. Should output show both formats ("256 = $0100") or just hex ("0100")?
2. What's the maximum input length to support (5 digits for 65535, or allow 6+ with range check)?
3. Should we support D: with no argument to convert current address ($0000 if unset)?
4. How should we handle decimal values that would normally be interpreted as hex (e.g., "1234")?
5. Should comma separators be allowed/ignored (e.g., "32,768")?

## Constraints & Limitations
**Technical Constraints:**
- Maximum memory usage: 200 bytes ROM
- Must not modify: Zero page $00-$10, $F0-$FF (in active use)
- Must use: Existing command parser pattern (colon-command syntax)
- Input range: Limited to 0-65535 (16-bit unsigned)

**Business/Timeline Constraints:**
- Should coordinate with H: (hex-to-decimal) command for consistent UX
- Part of calculator/utility enhancement initiative

## Success Criteria
**Definition of Done:**
- [ ] D:nnnnn command parses correctly and displays hex result
- [ ] All valid decimal inputs (0-65535) convert accurately
- [ ] Out-of-range input (>65535) shows appropriate error
- [ ] Invalid characters show appropriate error message
- [ ] Code size within 200-byte budget
- [ ] No regressions in existing monitor commands
- [ ] Help text updated with new command

**Acceptance Tests:**
1. Given command "D:0", when executed, then displays "0000"
2. Given command "D:65535", when executed, then displays "FFFF"
3. Given command "D:256", when executed, then displays "0100"
4. Given command "D:1024", when executed, then displays "0400"
5. Given command "D:32768", when executed, then displays "8000"
6. Given command "D:65536", when executed, then displays error (out of range)
7. Given command "D:12A4", when executed, then displays error (invalid decimal)
8. Given command "D:", when executed, then displays error or converts $0000
9. Edge case: D:00256 displays "0100" (leading zeros handled)

## Security & Safety Considerations
- Input validation: Ensure all characters are valid decimal digits (0-9)
- Range checking: Detect values > 65535 before conversion
- Buffer safety: Limit input length to prevent overflow
- Error handling: Use existing error message system
- No memory corruption: Conversion uses only designated scratch variables

## UI/UX Considerations
- **Input format:** D:nnnnn (consistent with H:xxxx, R:xxxx, W:xxxx pattern)
- **Output format:** Simple 4-digit hex, optionally with decimal reference
- **Error messages:** Reuse "VALUE?" for invalid decimal, "RANGE?" for > 65535
- **Prompt behavior:** Return to normal monitor prompt after display
- **Help integration:** Add "D:NNNNN DECIMAL TO HEX" to help display

## Testing Strategy
**Unit Tests:**
- Decimal parsing: Valid inputs (0, 1, 255, 256, 65535)
- Decimal parsing: Invalid inputs (ABC, 1G5, empty)
- Decimal parsing: Out of range (65536, 99999, 100000)
- Conversion accuracy: Powers of 2 (256, 512, 1024, 2048, 4096, 8192, 16384, 32768)
- Conversion accuracy: Powers of 10 (10, 100, 1000, 10000)
- Conversion accuracy: Edge cases (0, 1, 65534, 65535)

**Integration Tests:**
- Command parsing integration with existing parser
- Error handling integration with existing error system
- Display integration with screen output routines
- Workflow test: D:1024 then R:<result> to verify address usable
- Cross-validation: D:256 then H:<result> should return "256"

**Manual Test Scenarios:**
1. Enter "D:1024", verify displays "0400", then use in R:0400 command
2. Enter "D:256", verify displays "0100", use for screen offset calculation
3. Enter "D:65535", verify displays "FFFF" (max value)
4. Enter "D:65536", verify shows error (overflow)
5. Enter "D:ABC", verify shows VALUE? error
6. Sequential conversions: D:100, D:200, D:300 to verify stability
7. D: command followed by other commands to test state cleanup

## References & Research
- Decimal string to binary conversion algorithms for 6502
- Multiply-by-10 and add-digit approach (compact but slower)
- Horner's method for polynomial evaluation
- Look-up table approach for powers of 10 (faster but uses more ROM)
- Classic BASIC interpreters' decimal input parsing routines
- EHBasic's LAB_2887 (get FAC1 from string) for reference implementation

## Notes for PM Subagent
> Instructions for how to process this enhancement

- Coordinate output format with H: (hex-to-decimal) command - both should use similar display style
- Verify maximum input length decision (5 digits vs 6 digits with range check)
- Consider whether showing both formats in output adds value or clutter
- Flag if 200-byte budget seems insufficient for decimal parsing + conversion
- Evaluate whether to support D: with no argument (convert current address)

## Notes for Architect Subagent
> Key architectural considerations

- Design decimal string parser (similar to HEX_QUAD_TO_ADDR but for decimal)
- Implement decimal-to-binary conversion algorithm - recommend multiply-by-10 approach
- Use monitor scratch variables for conversion workspace (avoid zero page if possible)
- Consider temporary storage: MON_FILL_VALUE ($0284) or nearby unused locations
- Output using existing PRINT_CURRENT_ADDRESS routine (reuse hex display logic)
- Add to command jump table at appropriate position ('D' = $44, offset $44-$43 = $01)
- Integrate with PARSE_COLON_COMMAND or create custom decimal parser
- Optimize for code size: unrolled loops for small digit counts, compact error handling

## Notes for Implementer Subagent
> Implementation guidance

- Create PARSE_DECIMAL_VALUE routine to parse D:nnnnn format
- Implement conversion using multiply-by-10 and add-digit in loop
- Store result in MON_CURRADDR_HI/LO for compatibility with display routines
- Reuse PRINT_CURRENT_ADDRESS for hex output (already formats as 4 digits)
- Add range checking: if value > 65535, use MSG_RANGE_ERROR
- For invalid digits, use MSG_VALUE_ERROR
- Add command entry: CMD_INDEX_MAP['D'-'C'] = new_index
- Add jump table entries: CMD_JUMP_COMPACT_LO/HI
- Update HELP_MSG_TABLE: "D:NNNNN DECIMAL TO HEX"
- Test edge cases thoroughly: 0, 1, 255, 256, 65534, 65535, 65536

## Notes for Testing Subagent
> Testing and validation guidance

- Focus testing on decimal parsing accuracy (all digits 0-9 valid)
- Validate conversion algorithm with known values (powers of 10, powers of 2)
- Test range boundaries: 65535 (max valid), 65536 (first invalid)
- Verify error messages: invalid characters, out of range values
- Cross-validate with H: command: D:256 → $0100, then H:0100 → 256
- Test with leading zeros: D:00256 should give same result as D:256
- Verify integration with command parser (no interference with other commands)
- Test memory safety: ensure no overflow of temporary variables
- Manual hardware testing for timing verification at 1MHz
- Regression test: ensure all existing commands still function correctly