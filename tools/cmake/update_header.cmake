# ================================================================
# Update Kernel Header with Build Information
# ================================================================
# This script updates the kernel.asm header with:
# - ROM size information from the map file
# - Memory segment details
# - Assembly command used
# - SHA-1 checksum of the ROM file
# ================================================================

cmake_minimum_required(VERSION 3.10)

# Required parameters
if(NOT DEFINED KERNEL_ASM_FILE)
    message(FATAL_ERROR "KERNEL_ASM_FILE must be defined")
endif()

if(NOT DEFINED MAP_FILE)
    message(FATAL_ERROR "MAP_FILE must be defined")
endif()

if(NOT DEFINED ROM_FILE)
    message(FATAL_ERROR "ROM_FILE must be defined")
endif()

# Read the map file to extract segment information
file(READ "${MAP_FILE}" MAP_CONTENTS)

# Extract segment information using regex
string(REGEX MATCH "CODE[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)" CODE_MATCH "${MAP_CONTENTS}")
if(CODE_MATCH)
    set(CODE_START ${CMAKE_MATCH_1})
    set(CODE_END ${CMAKE_MATCH_2})
    set(CODE_SIZE ${CMAKE_MATCH_3})
    # Convert hex to decimal for calculations
    math(EXPR CODE_SIZE_DEC "0x${CODE_SIZE}")
else()
    message(FATAL_ERROR "Could not extract CODE segment information from map file")
endif()

string(REGEX MATCH "JUMPS[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)" JUMPS_MATCH "${MAP_CONTENTS}")
if(JUMPS_MATCH)
    set(JUMPS_START ${CMAKE_MATCH_1})
    set(JUMPS_END ${CMAKE_MATCH_2})
    set(JUMPS_SIZE ${CMAKE_MATCH_3})
    math(EXPR JUMPS_SIZE_DEC "0x${JUMPS_SIZE}")
else()
    message(FATAL_ERROR "Could not extract JUMPS segment information from map file")
endif()

string(REGEX MATCH "VECS[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)" VECS_MATCH "${MAP_CONTENTS}")
if(VECS_MATCH)
    set(VECS_START ${CMAKE_MATCH_1})
    set(VECS_END ${CMAKE_MATCH_2})
    set(VECS_SIZE ${CMAKE_MATCH_3})
    math(EXPR VECS_SIZE_DEC "0x${VECS_SIZE}")
else()
    message(FATAL_ERROR "Could not extract VECS segment information from map file")
endif()

# Calculate total ROM size used
math(EXPR TOTAL_SIZE "${CODE_SIZE_DEC} + ${JUMPS_SIZE_DEC} + ${VECS_SIZE_DEC}")

# Calculate SHA-1 checksum of ROM file
execute_process(
    COMMAND shasum "${ROM_FILE}"
    OUTPUT_VARIABLE CHECKSUM_OUTPUT
    RESULT_VARIABLE CHECKSUM_RESULT
)

if(NOT CHECKSUM_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to calculate ROM checksum")
endif()

# Extract just the checksum (first 40 characters)
string(SUBSTRING "${CHECKSUM_OUTPUT}" 0 40 ROM_CHECKSUM)

# Assembly command (simplified version)
set(ASSEMBLY_COMMAND "ca65 kernel.asm -o kernel.o && ld65 -C memory.cfg kernel.o -o kernel.rom")

# Read the kernel.asm file
file(READ "${KERNEL_ASM_FILE}" KERNEL_CONTENT)

# Update ROM Size (Used) line
string(REGEX REPLACE
    "; ROM Size \\(Used\\): [0-9]+ bytes"
    "; ROM Size (Used): ${TOTAL_SIZE} bytes"
    KERNEL_CONTENT "${KERNEL_CONTENT}")

# Remove leading zeros from addresses for cleaner display
string(REGEX REPLACE "^0+" "" CODE_START_CLEAN "${CODE_START}")
string(REGEX REPLACE "^0+" "" CODE_END_CLEAN "${CODE_END}")
string(REGEX REPLACE "^0+" "" JUMPS_START_CLEAN "${JUMPS_START}")
string(REGEX REPLACE "^0+" "" JUMPS_END_CLEAN "${JUMPS_END}")
string(REGEX REPLACE "^0+" "" VECS_START_CLEAN "${VECS_START}")
string(REGEX REPLACE "^0+" "" VECS_END_CLEAN "${VECS_END}")

# Handle case where address is just "0" (don't make it empty)
if(NOT CODE_START_CLEAN)
    set(CODE_START_CLEAN "0")
endif()
if(NOT CODE_END_CLEAN)
    set(CODE_END_CLEAN "0")
endif()

# Update CODE segment line
string(REGEX REPLACE
    ";   CODE segment:[ \t]+\\(\\$[0-9A-F]+-\\$[0-9A-F]+\\) - [0-9]+ bytes"
    ";   CODE segment:   ($$${CODE_START_CLEAN}-$$${CODE_END_CLEAN}) - ${CODE_SIZE_DEC} bytes"
    KERNEL_CONTENT "${KERNEL_CONTENT}")

# Update JUMPS segment line
string(REGEX REPLACE
    ";   JUMPS segment:[ \t]+\\(\\$[0-9A-F]+-\\$[0-9A-F]+\\) - [0-9]+ bytes"
    ";   JUMPS segment:  ($$${JUMPS_START_CLEAN}-$$${JUMPS_END_CLEAN}) - ${JUMPS_SIZE_DEC} bytes"
    KERNEL_CONTENT "${KERNEL_CONTENT}")

# Update VECS segment line
string(REGEX REPLACE
    ";   VECS segment:[ \t]+\\(\\$[0-9A-F]+-\\$[0-9A-F]+\\) - [0-9]+ bytes"
    ";   VECS segment:   ($$${VECS_START_CLEAN}-$$${VECS_END_CLEAN}) - ${VECS_SIZE_DEC} bytes"
    KERNEL_CONTENT "${KERNEL_CONTENT}")

# Update Assembly command line
string(REGEX REPLACE
    "; Assembly:[ \t]+[^\r\n]*"
    "; Assembly:     ${ASSEMBLY_COMMAND}"
    KERNEL_CONTENT "${KERNEL_CONTENT}")

# Update Checksum line (handle case where it might be missing)
if(KERNEL_CONTENT MATCHES "; Checksum:[ \t]+[^\r\n]*")
    string(REGEX REPLACE
        "; Checksum:[ \t]+[^\r\n]*"
        "; Checksum:     ${ROM_CHECKSUM} (SHA-1)"
        KERNEL_CONTENT "${KERNEL_CONTENT}")
else()
    # Add checksum line after assembly line if it doesn't exist
    string(REGEX REPLACE
        "(; Assembly:[ \t]+[^\r\n]*)"
        "\\1\n; Checksum:     ${ROM_CHECKSUM} (SHA-1)"
        KERNEL_CONTENT "${KERNEL_CONTENT}")
endif()

# Write the updated content back to the file
file(WRITE "${KERNEL_ASM_FILE}" "${KERNEL_CONTENT}")

message(STATUS "Updated kernel header with build information:")
message(STATUS "  Total ROM Size: ${TOTAL_SIZE} bytes")
message(STATUS "  CODE: ($${CODE_START}-$${CODE_END}) - ${CODE_SIZE_DEC} bytes")
message(STATUS "  JUMPS: ($${JUMPS_START}-$${JUMPS_END}) - ${JUMPS_SIZE_DEC} bytes")
message(STATUS "  VECS: ($${VECS_START}-$${VECS_END}) - ${VECS_SIZE_DEC} bytes")
message(STATUS "  Checksum: ${ROM_CHECKSUM}")