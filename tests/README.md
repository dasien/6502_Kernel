# 6502 Kernel Integration Test Suite

This directory contains comprehensive **integration tests** for the 6502 Kernel project. Unlike traditional unit testing, we test the complete 6502 system by loading the actual kernel ROM, sending keyboard commands, and verifying screen output.

## Why Integration Testing?

Testing a 6502 kernel requires a different approach than typical software:
1. **Real Hardware Simulation** - We test the actual ROM running in emulation
2. **Interactive Commands** - Monitor commands are tested as users would use them
3. **Complete System Validation** - Tests the entire pipeline: keyboard → CPU → memory → screen
4. **Assembly Code Reality** - No need to mock 6502 assembly routines

## Test Categories

### Integration Tests
- **test_monitor_integration.cpp** - Tests all monitor commands via emulated keyboard/screen
- **test_advanced_commands.cpp** - Tests advanced command parsing (F:, M:, X:)

### ROM Validation Tests
- **test_kernel_rom.cpp** - Validates the compiled kernel ROM file
- **scripts/validate_rom.cmake** - ROM size and structure validation
- **scripts/validate_memory_layout.cmake** - Memory layout validation from MAP file

## Running Tests

### Prerequisites
- Google Test framework (automatically downloaded if not found)
- CMake 3.20 or later
- Built kernel ROM and MAP files

### Build and Run Tests
```bash
# Enable tests in CMake configuration
cmake -DBUILD_TESTS=ON -G Ninja ..
ninja

# Run all tests via CTest
ctest

# Run integration tests directly
./bin/monitor_integration_tests

# Run with verbose output
ctest --verbose
```

### Test Output
```
6502 Monitor Integration Test Suite
===================================

Running integration tests...

Clear Screen Command          : PASS
Help Command Display          : PASS
Fill Memory F:8000-8007,BB    : PASS
Verify Fill Result            : PASS
Read Single Address           : PASS
Read Address Range            : PASS
Copy Memory Command           : PASS
Verify Copy Result            : PASS
Move Memory Command           : PASS
Write Mode Entry              : PASS
Stack Display Command         : PASS
Zero Page Display Command     : PASS

=== TEST SUMMARY ===
Tests Passed: 12
Tests Failed: 0
Total Tests:  12

🎉 ALL TESTS PASSED! 🎉
```

## Test Coverage

### Monitor Commands Tested
- **C:** - Clear Screen (basic validation)
- **F:XXXX-YYYY,ZZ** - Fill Memory (syntax parsing, boundary checking)
- **G:XXXX** - Go/Run (address validation)
- **H:** - Help (command detection)
- **L:XXXX,FILENAME** - Load File (address parsing)
- **M:XXXX-YYYY,ZZZZ,B** - Move/Copy Memory (complex parsing, overlap detection)
- **R:XXXX(-YYYY)** - Read Memory (single address and range parsing)
- **S:XXXX-YYYY,FILENAME** - Save Memory (range and filename parsing)
- **T:** - Stack Dump (basic validation)
- **W:XXXX** - Write Memory (address parsing, mode detection)
- **X:XXXX-YYYY,PATTERN** - Search Memory (pattern parsing, multi-byte support)
- **Z:** - Zero Page Dump (basic validation)

### Parsing Functions Tested
- Hex character to nibble conversion
- Hex pair to byte conversion
- Hex quad to address conversion
- Address range parsing
- Multi-byte pattern parsing
- Error handling and validation

### ROM Validation
- ROM size (exactly 4KB)
- Interrupt vectors (NMI, RESET, IRQ)
- Jump table presence and format
- Expected string constants
- Entry point validation
- ROM utilization analysis

### Memory Layout Validation
- Segment positioning (CODE at $F000, JUMPS at $FF00, VECS at $FFFA)
- Segment sizes and boundaries
- Total ROM usage calculation
- Memory map consistency

## Error Testing

Each test category includes negative test cases:
- Invalid command syntax
- Out-of-bounds addresses
- Invalid hex characters
- Range errors (end < start)
- Buffer overflows
- Pattern length violations

## Test Data

Tests use realistic 6502 data patterns:
- Memory addresses: $0000-$FFFF range
- 6502 opcodes: JMP ($4C), LDA ($A9), etc.
- ROM boundaries: $F000-$FFFF
- Zero page: $00-$FF
- Stack: $0100-$01FF

## Extending Tests

To add new tests:

1. Create new test file in this directory
2. Include in `CMakeLists.txt`
3. Follow GoogleTest conventions
4. Include both positive and negative test cases
5. Document expected behavior
6. Test boundary conditions

### Test File Template
```cpp
#include <gtest/gtest.h>

class MyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test setup
    }
};

TEST_F(MyTest, TestFunction) {
    EXPECT_TRUE(someCondition);
    EXPECT_EQ(expected, actual);
}
```

## Continuous Integration

These tests are designed to:
- Validate every build
- Catch regressions in monitor functionality
- Ensure ROM size constraints
- Verify memory layout consistency
- Test all command parsing edge cases

The test suite provides confidence that monitor commands work correctly and the kernel ROM is properly structured for 6502 execution.