# 6502 Kernel Test Suite
cmake_minimum_required(VERSION 3.20)

# Find or fetch Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
# For Windows: Prevent overriding parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Create unit test executable for advanced commands
add_executable(kernel_tests
    test_advanced_commands.cpp
)

target_link_libraries(kernel_tests
    gtest_main
    gtest
)

target_include_directories(kernel_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(kernel_tests PRIVATE cxx_std_20)

# Create unit test executable for the CPU ALU (ADC/SBC/CMP, binary and BCD)
add_executable(cpu_alu_tests
    test_cpu_alu.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
)

target_link_libraries(cpu_alu_tests
    gtest_main
    gtest
)

target_include_directories(cpu_alu_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(cpu_alu_tests PRIVATE cxx_std_20)

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

# Add unit tests to CTest
add_test(NAME kernel_unit_tests
    COMMAND kernel_tests)

# Add CPU ALU unit tests to CTest
add_test(NAME cpu_alu_unit_tests
    COMMAND cpu_alu_tests)

# Add integration test to CTest
add_test(NAME monitor_integration
    COMMAND monitor_integration_tests)

# Add custom test for ROM validation
add_test(NAME validate_kernel_rom
    COMMAND ${CMAKE_COMMAND}
    -DROM_FILE=${CMAKE_BINARY_DIR}/kernel/kernel.rom
    -P ${CMAKE_SOURCE_DIR}/tests/scripts/validate_rom.cmake)

# Add custom test for memory layout validation
add_test(NAME validate_memory_layout
    COMMAND ${CMAKE_COMMAND}
    -DMAP_FILE=${CMAKE_BINARY_DIR}/kernel/kernel.map
    -P ${CMAKE_SOURCE_DIR}/tests/scripts/validate_memory_layout.cmake)