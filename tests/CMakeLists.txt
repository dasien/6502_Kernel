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
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
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

# Hardware interrupt path (IRQ/NMI dispatch) and the 65C02 processor-control
# instructions (WAI/STP). The v2.2 interrupt path drives the 60Hz timer and the
# NMI stop key but was only ever exercised through those features.
add_executable(cpu_interrupt_tests
    test_cpu_interrupts.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
)

target_link_libraries(cpu_interrupt_tests
    gtest_main
    gtest
)

target_include_directories(cpu_interrupt_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(cpu_interrupt_tests PRIVATE cxx_std_20)

# Cycle-count accuracy for every defined 65C02 opcode, against datasheet reference
# data. Guards the bus helpers against re-introducing the double-charge that put
# every instruction over (JSR cost 11 cycles instead of 6).
add_executable(cpu_cycle_tests
    test_cpu_cycles.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
)

target_link_libraries(cpu_cycle_tests gtest_main gtest)

target_include_directories(cpu_cycle_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(cpu_cycle_tests PRIVATE cxx_std_20)

# Create unit test executable for the bankable module slot (Memory bank routing)
add_executable(memory_banking_tests
    test_memory_banking.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
)

target_link_libraries(memory_banking_tests
    gtest_main
    gtest
)

target_include_directories(memory_banking_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(memory_banking_tests PRIVATE cxx_std_20)

# Create unit test executable for the block device (MFC-DOS storage foundation:
# $FE24-$FE28 registers + host disk.img sector read/write)
add_executable(block_device_tests
    test_block_device.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
)

target_link_libraries(block_device_tests
    gtest_main
    gtest
)

target_include_directories(block_device_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(block_device_tests PRIVATE cxx_std_20)

# Create test executable for the DOS block-device primitives (runs the real
# dos.rom 6502 routines that drive the $FE24-$FE28 registers).
add_executable(dos_blockio_tests
    test_dos_blockio.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Computer6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/ResetCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/TimingCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/MapFileParser.cpp
)

target_link_libraries(dos_blockio_tests
    gtest_main
    gtest
)

target_include_directories(dos_blockio_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(dos_blockio_tests PRIVATE cxx_std_20)

# Create test executable for the DOS FAT16 driver (runs the real dos.rom 6502
# mount + directory + file-read routines against a built FAT16 image)
add_executable(dos_fat16_tests
    test_dos_fat16.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Computer6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/ResetCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/TimingCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/MapFileParser.cpp
)

target_link_libraries(dos_fat16_tests
    gtest_main
    gtest
)

target_include_directories(dos_fat16_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(dos_fat16_tests PRIVATE cxx_std_20)

# Spike: emulated 6551 ACIA + ported XMODEM receiver (headless end-to-end)
add_executable(acia_xmodem_tests
    test_acia_xmodem.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Computer6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/ResetCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/TimingCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/MapFileParser.cpp
)

target_link_libraries(acia_xmodem_tests gtest_main gtest)

target_include_directories(acia_xmodem_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(acia_xmodem_tests PRIVATE cxx_std_20)

# Regression test for the kernel PRINT_CHAR ABI contract (preserves A/X/Y,
# returns A=char, drops control chars without rendering a glyph). Runs the real
# kernel ROM and calls PRINT_CHAR via the $FF00 jump table.
add_executable(print_char_tests
    test_print_char.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Computer6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/ResetCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/TimingCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/MapFileParser.cpp
)

target_link_libraries(print_char_tests gtest_main gtest)

target_include_directories(print_char_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(print_char_tests PRIVATE cxx_std_20)

# ModemProtocol unit tests (Hayes AT parser + telnet IAC filter). Pure logic,
# NO Qt Network -- exercises the protocol via a mock host.
add_executable(modem_tests
    test_modem.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/ModemProtocol.cpp
)

target_link_libraries(modem_tests gtest_main gtest)

target_include_directories(modem_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(modem_tests PRIVATE cxx_std_20)

# VIC character-plane unit test: full 8-bit CP437 glyphs; reverse via attribute.
add_executable(vic_charplane_tests
    test_vic_charplane.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
)

target_link_libraries(vic_charplane_tests gtest_main gtest)

target_include_directories(vic_charplane_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(vic_charplane_tests PRIVATE cxx_std_20)

# SID sound-chip unit test: register map + oscillator/ADSR synthesis. Self-
# contained (Sid has no other device deps), like the VIC char-plane test.
add_executable(sid_tests
    test_sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
)

target_link_libraries(sid_tests gtest_main gtest)

target_include_directories(sid_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(sid_tests PRIVATE cxx_std_20)

# RTC unit test: register map + BCD host-time snapshot (injectable time source).
# Self-contained (Rtc has no other device deps).
add_executable(rtc_tests
    test_rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
)

target_link_libraries(rtc_tests gtest_main gtest)

target_include_directories(rtc_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(rtc_tests PRIVATE cxx_std_20)

# Headless test of the 6502 serial terminal's ANSI engine (programs/term).
# Runs the real TERM blob; the C++ side feeds ANSI and asserts on the screen.
add_executable(term_ansi_tests
    test_term_ansi.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Computer6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/ResetCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/TimingCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/MapFileParser.cpp
)

target_link_libraries(term_ansi_tests gtest_main gtest)

target_include_directories(term_ansi_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(term_ansi_tests PRIVATE cxx_std_20)

# Headless test of the terminal's XMODEM receive (^R) to the FAT16 disk.
add_executable(term_xmodem_tests
    test_term_xmodem.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Computer6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/ResetCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/TimingCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/MapFileParser.cpp
)

target_link_libraries(term_xmodem_tests gtest_main gtest)

target_include_directories(term_xmodem_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(term_xmodem_tests PRIVATE cxx_std_20)

# IRC chat-client end-to-end test (runs the real irc.bin blob)
add_executable(irc_tests
    test_irc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Computer6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/VIC.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/PIA.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/ResetCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/TimingCircuit.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/MapFileParser.cpp
)

target_link_libraries(irc_tests gtest_main gtest)

target_include_directories(irc_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/computer
)

target_compile_features(irc_tests PRIVATE cxx_std_20)

# Create integration test executable that tests real 6502 monitor commands
add_executable(monitor_integration_tests
    test_monitor_integration.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Computer6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/CPU6502.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Sid.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Rtc.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/BlockDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/computer/Acia.cpp
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

# Add module-slot bank-routing unit tests to CTest
add_test(NAME cpu_interrupt_unit_tests
    COMMAND cpu_interrupt_tests)

add_test(NAME cpu_cycle_unit_tests
    COMMAND cpu_cycle_tests)

add_test(NAME memory_banking_unit_tests
    COMMAND memory_banking_tests)

# Add block-device unit tests to CTest
add_test(NAME block_device_unit_tests
    COMMAND block_device_tests)

# Add DOS block-I/O tests to CTest (runs the dos.rom 6502 sector primitives)
add_test(NAME dos_blockio_tests
    COMMAND dos_blockio_tests)

# Add DOS FAT16 driver tests to CTest (mount + directory + file read)
add_test(NAME dos_fat16_tests
    COMMAND dos_fat16_tests)

# FAT16 reader round-trip (root + drawers) — header-only, no emulator needed.
add_executable(fat16_roundtrip_tests test_fat16_roundtrip.cpp)
target_link_libraries(fat16_roundtrip_tests gtest_main gtest)
target_include_directories(fat16_roundtrip_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fat16_roundtrip_tests PRIVATE cxx_std_20)
add_test(NAME fat16_roundtrip COMMAND fat16_roundtrip_tests)

# Add integration test to CTest
add_test(NAME monitor_integration
    COMMAND monitor_integration_tests)

# Add the ACIA + XMODEM spike test to CTest
add_test(NAME acia_xmodem
    COMMAND acia_xmodem_tests)

# Add the PRINT_CHAR ABI-contract regression test to CTest
add_test(NAME print_char
    COMMAND print_char_tests)

# Add the ModemProtocol (AT parser + telnet IAC) unit tests to CTest
add_test(NAME modem
    COMMAND modem_tests)

# Add the serial-terminal ANSI engine test to CTest
add_test(NAME term_ansi
    COMMAND term_ansi_tests)

# Add the terminal XMODEM-receive test to CTest
add_test(NAME term_xmodem
    COMMAND term_xmodem_tests)

# Add the IRC chat-client end-to-end test to CTest
add_test(NAME irc
    COMMAND irc_tests)

# Add the VIC character-plane (8-bit CP437) unit test to CTest
add_test(NAME vic_charplane
    COMMAND vic_charplane_tests)

# Add the SID sound-chip unit test to CTest
add_test(NAME sid
    COMMAND sid_tests)

# Add the RTC unit test to CTest
add_test(NAME rtc
    COMMAND rtc_tests)

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

# Drift guard: the canonical 65C02 opcode table (docs/opcode_table_65c02.md and
# src/kernel/assembler/opcodes_65c02.inc) is generated from CPU6502's handler map.
# This test regenerates in --check mode and fails if the committed table is stale,
# so the assembler/disassembler table can never silently diverge from the CPU.
find_program(PYTHON3_EXECUTABLE NAMES python3 python)
if(PYTHON3_EXECUTABLE)
    add_test(NAME opcode_table_current
        COMMAND ${PYTHON3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/gen_opcode_table.py --check)
else()
    message(WARNING "python3 not found - skipping opcode_table_current test")
endif()

# Architectural guard: kernel.asm is the BIOS, monitor.inc is the interactive
# monitor. They assemble into one ROM, so a JSR across the boundary just links and
# nothing complains -- but every BIOS -> monitor call is a wire that must be cut
# before the monitor can become a bank module, because the BIOS cannot call into an
# unmapped window. This keeps that list short, explicit and honest.
if(PYTHON3_EXECUTABLE)
    add_test(NAME kernel_bios_monitor_split
        COMMAND ${PYTHON3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/scripts/check_kernel_split.py
        ${CMAKE_SOURCE_DIR}/src/kernel)
endif()

# Host-interop guard: a mkdisk-built image must be a FAT16 volume the host's own
# checker accepts. Images are meant to be mountable on macOS/Linux/Windows, and that
# was previously verified once by hand -- which went stale the moment drawers were
# added (their '.'/'..' entries carried a bad DIR_NTRes byte, so fsck.fat reported
# every image as damaged). This keeps it honest, and keeps fsck usable as the signal
# for genuine corruption such as the two FAT copies disagreeing.
find_program(FSCK_FAT_EXECUTABLE NAMES fsck.fat fsck.vfat dosfsck
             HINTS /sbin /usr/sbin)
if(FSCK_FAT_EXECUTABLE)
    add_test(NAME fat16_image_clean
        COMMAND ${CMAKE_COMMAND}
        -DMKDISK=$<TARGET_FILE:mkdisk>
        -DFSCK=${FSCK_FAT_EXECUTABLE}
        -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/scripts/validate_fat16_image.cmake)
else()
    message(STATUS "fsck.fat not found - skipping fat16_image_clean "
                   "(install dosfstools to check host FAT16 interop)")
endif()

# The same checker, applied to an image the GUEST wrote. fat16_image_clean only
# covers host-authored images; the corruption that actually costs data (orphaned
# chains, the two FATs disagreeing, damaged drawer entries) comes out of the DOS
# write path, so the integration suite fscks its own disk after a write workload.
# Without dosfstools the check reports SKIP rather than failing the suite.
if(FSCK_FAT_EXECUTABLE)
    target_compile_definitions(monitor_integration_tests PRIVATE
        MFC_FSCK_FAT="${FSCK_FAT_EXECUTABLE}")
endif()

# examples/README.md tells users the shipped sources assemble in the built-in
# assembler. The suite builds every one of them so that promise cannot rot.
target_compile_definitions(monitor_integration_tests PRIVATE
    MFC_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")