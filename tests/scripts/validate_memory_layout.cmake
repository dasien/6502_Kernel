# Memory Layout Validation Script
# Validates the kernel.map file for correct 6502 memory layout

if(NOT MAP_FILE)
    message(FATAL_ERROR "MAP_FILE must be specified")
endif()

if(NOT EXISTS "${MAP_FILE}")
    message(FATAL_ERROR "MAP file does not exist: ${MAP_FILE}")
endif()

# Read the MAP file
file(READ "${MAP_FILE}" MAP_CONTENT)

message(STATUS "=== MEMORY LAYOUT VALIDATION ===")
message(STATUS "MAP File: ${MAP_FILE}")

# Parse segments from the MAP file
# Expected format:
# NAME                   Start     End    Size  Align
# CODE                  00F000  00XXXX  XXXXXX  00001
# JUMPS                 00FF00  00FF11  000012  00001
# VECS                  00FFFA  00FFFF  000006  00001

set(SEGMENTS_FOUND 0)
set(TOTAL_SIZE 0)

# Look for CODE segment
if(MAP_CONTENT MATCHES "CODE[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)")
    set(CODE_START ${CMAKE_MATCH_1})
    set(CODE_END ${CMAKE_MATCH_2})
    set(CODE_SIZE_HEX ${CMAKE_MATCH_3})

    # Convert hex to decimal
    math(EXPR CODE_SIZE_DEC "0x${CODE_SIZE_HEX}")
    math(EXPR TOTAL_SIZE "${TOTAL_SIZE} + ${CODE_SIZE_DEC}")
    math(EXPR SEGMENTS_FOUND "${SEGMENTS_FOUND} + 1")

    message(STATUS "CODE segment: $${CODE_START}-$${CODE_END} (${CODE_SIZE_DEC} bytes)")

    # Validate CODE starts at F000
    if(NOT CODE_START STREQUAL "00F000")
        message(FATAL_ERROR "CODE segment should start at $F000, found: $${CODE_START}")
    endif()

else()
    message(FATAL_ERROR "CODE segment not found in MAP file")
endif()

# Look for JUMPS segment
if(MAP_CONTENT MATCHES "JUMPS[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)")
    set(JUMPS_START ${CMAKE_MATCH_1})
    set(JUMPS_END ${CMAKE_MATCH_2})
    set(JUMPS_SIZE_HEX ${CMAKE_MATCH_3})

    math(EXPR JUMPS_SIZE_DEC "0x${JUMPS_SIZE_HEX}")
    math(EXPR TOTAL_SIZE "${TOTAL_SIZE} + ${JUMPS_SIZE_DEC}")
    math(EXPR SEGMENTS_FOUND "${SEGMENTS_FOUND} + 1")

    message(STATUS "JUMPS segment: $${JUMPS_START}-$${JUMPS_END} (${JUMPS_SIZE_DEC} bytes)")

    # Validate JUMPS starts at FF00
    if(NOT JUMPS_START STREQUAL "00FF00")
        message(FATAL_ERROR "JUMPS segment should start at $FF00, found: $${JUMPS_START}")
    endif()

else()
    message(WARNING "JUMPS segment not found in MAP file")
endif()

# Look for VECS segment
if(MAP_CONTENT MATCHES "VECS[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)[ \t]+([0-9A-F]+)")
    set(VECS_START ${CMAKE_MATCH_1})
    set(VECS_END ${CMAKE_MATCH_2})
    set(VECS_SIZE_HEX ${CMAKE_MATCH_3})

    math(EXPR VECS_SIZE_DEC "0x${VECS_SIZE_HEX}")
    math(EXPR TOTAL_SIZE "${TOTAL_SIZE} + ${VECS_SIZE_DEC}")
    math(EXPR SEGMENTS_FOUND "${SEGMENTS_FOUND} + 1")

    message(STATUS "VECS segment: $${VECS_START}-$${VECS_END} (${VECS_SIZE_DEC} bytes)")

    # Validate VECS starts at FFFA
    if(NOT VECS_START STREQUAL "00FFFA")
        message(FATAL_ERROR "VECS segment should start at $FFFA, found: $${VECS_START}")
    endif()

    # Validate VECS ends at FFFF
    if(NOT VECS_END STREQUAL "00FFFF")
        message(FATAL_ERROR "VECS segment should end at $FFFF, found: $${VECS_END}")
    endif()

else()
    message(FATAL_ERROR "VECS segment not found in MAP file")
endif()

message(STATUS "Found ${SEGMENTS_FOUND} memory segments")
message(STATUS "Total code size: ${TOTAL_SIZE} bytes")

# Validate total size doesn't exceed 4KB
if(TOTAL_SIZE GREATER 4096)
    message(FATAL_ERROR "Total ROM size ${TOTAL_SIZE} exceeds 4096 bytes")
endif()

# Calculate utilization
math(EXPR UTILIZATION_PERCENT "${TOTAL_SIZE} * 100 / 4096")
message(STATUS "ROM utilization: ${UTILIZATION_PERCENT}%")

if(UTILIZATION_PERCENT GREATER 95)
    message(WARNING "ROM utilization is over 95% - consider optimization")
elseif(UTILIZATION_PERCENT GREATER 90)
    message(WARNING "ROM utilization is over 90% - monitor for future growth")
endif()

message(STATUS "Memory layout validation completed successfully")
message(STATUS "==================================")