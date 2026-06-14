cmake_minimum_required(VERSION 3.20)
project(6502-kernel 
    VERSION 1.0.0
    DESCRIPTION "6502 Microprocessor Emulator with Interactive Monitor"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Organize build outputs
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "6502 Computer Emulator v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0 -DDEBUG)
    else()
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()

# Include CMake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/tools/cmake)

# Find Qt (optional GUI support)
include(FindQt)

# Add subdirectories
add_subdirectory(src)

# Kernel ROM build (if cc65 available)
include(BuildKernel)

# Testing support
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ----------------------------------------------------------------
# mkfat16 - host tool to create a FAT16 disk.img for the block device.
# Reuses the FAT16 image builder shared with the filesystem tests.
# Build it, then create a disk:   ./bin/mkfat16 ../disk.img [files...]
# Or use the convenience target:  ninja sample_disk
# ----------------------------------------------------------------
add_executable(mkfat16 tools/mkfat16/mkfat16.cpp)
target_include_directories(mkfat16 PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_features(mkfat16 PRIVATE cxx_std_20)

# Convenience: write a sample disk.img (with sample files) next to the ROMs,
# where the emulator looks for it (../disk.img relative to the bin/ dir).
add_custom_target(sample_disk
    COMMAND mkfat16 ${CMAKE_BINARY_DIR}/disk.img
    DEPENDS mkfat16
    COMMENT "Creating sample FAT16 disk image at ${CMAKE_BINARY_DIR}/disk.img"
    VERBATIM
)

# Installation rules
install(TARGETS 6502-kernel
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES ${CMAKE_BINARY_DIR}/kernel/kernel.rom
    DESTINATION share/6502-kernel
    OPTIONAL
)