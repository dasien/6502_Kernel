# 6502 Kernel Integration Test Suite
cmake_minimum_required(VERSION 3.20)

# Create integration test executable that tests real 6502 monitor commands
add_executable(monitor_integration_tests
    test_monitor_integration.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Computer6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/ResetCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/TimingCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/MapFileParser.cpp
)

# Include directories
target_include_directories(monitor_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

# Add compiler flags
target_compile_features(monitor_integration_tests PRIVATE cxx_std_20)

# Add integration test to CTest
add_test(NAME monitor_integration
    COMMAND monitor_integration_tests)

# Add custom test for ROM validation
add_test(NAME validate_kernel_rom
    COMMAND ${CMAKE_COMMAND}
    -DROM_FILE=${CMAKE_BINARY_DIR}/kernel.rom
    -P ${CMAKE_SOURCE_DIR}/tests/scripts/validate_rom.cmake)

# Add custom test for memory layout validation
add_test(NAME validate_memory_layout
    COMMAND ${CMAKE_COMMAND}
    -DMAP_FILE=${CMAKE_BINARY_DIR}/kernel.map
    -P ${CMAKE_SOURCE_DIR}/tests/scripts/validate_memory_layout.cmake)