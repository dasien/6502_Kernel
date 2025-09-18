# rom_size.cmake - Analyze ROM file size and memory usage
# This script is called during the build process to report ROM size information

# ROM file paths are passed as parameters
# If not provided, fall back to default locations
if(NOT ROM_FILE)
    set(ROM_FILE "${CMAKE_BINARY_DIR}/kernel/kernel.rom")
endif()
if(NOT MAP_FILE)
    set(MAP_FILE "${CMAKE_BINARY_DIR}/kernel/kernel.map")
endif()

# Check if ROM file exists
if(NOT EXISTS ${ROM_FILE})
    message(FATAL_ERROR "ROM file not found: ${ROM_FILE}")
endif()

# Get ROM file size
file(SIZE ${ROM_FILE} ROM_SIZE)

# Convert to hex for display
math(EXPR ROM_SIZE_HEX "${ROM_SIZE}" OUTPUT_FORMAT HEXADECIMAL)

# Calculate ROM utilization (assuming 4KB ROM space from $F000-$FFFF)
set(ROM_CAPACITY 4096)
math(EXPR ROM_USAGE_PERCENT "${ROM_SIZE} * 100 / ${ROM_CAPACITY}")

# Parse map file if it exists to get detailed segment information
set(CODE_SIZE "Unknown")
set(JUMPS_SIZE "Unknown")
set(VECS_SIZE "Unknown")

if(EXISTS ${MAP_FILE})
    file(READ ${MAP_FILE} MAP_CONTENT)
    
    # Extract segment information from map file
    # Look for lines like: "CODE                  00F000  00FE76  000E77  00001"
    string(REGEX MATCH "CODE[ ]+[0-9A-F]+[ ]+[0-9A-F]+[ ]+([0-9A-F]+)" CODE_MATCH ${MAP_CONTENT})
    if(CODE_MATCH)
        string(REGEX REPLACE "CODE[ ]+[0-9A-F]+[ ]+[0-9A-F]+[ ]+([0-9A-F]+)" "\\1" CODE_SIZE_HEX ${CODE_MATCH})
        math(EXPR CODE_SIZE "0x${CODE_SIZE_HEX}")
    endif()
    
    string(REGEX MATCH "JUMPS[ ]+[0-9A-F]+[ ]+[0-9A-F]+[ ]+([0-9A-F]+)" JUMPS_MATCH ${MAP_CONTENT})
    if(JUMPS_MATCH)
        string(REGEX REPLACE "JUMPS[ ]+[0-9A-F]+[ ]+[0-9A-F]+[ ]+([0-9A-F]+)" "\\1" JUMPS_SIZE_HEX ${JUMPS_MATCH})
        math(EXPR JUMPS_SIZE "0x${JUMPS_SIZE_HEX}")
    endif()
    
    string(REGEX MATCH "VECS[ ]+[0-9A-F]+[ ]+[0-9A-F]+[ ]+([0-9A-F]+)" VECS_MATCH ${MAP_CONTENT})
    if(VECS_MATCH)
        string(REGEX REPLACE "VECS[ ]+[0-9A-F]+[ ]+[0-9A-F]+[ ]+([0-9A-F]+)" "\\1" VECS_SIZE_HEX ${VECS_MATCH})
        math(EXPR VECS_SIZE "0x${VECS_SIZE_HEX}")
    endif()
endif()

# Output size analysis
message("ROM FILE: kernel.rom")
message("TOTAL SIZE: ${ROM_SIZE} bytes (${ROM_SIZE_HEX})")
message("ROM CAPACITY: ${ROM_CAPACITY} bytes (4KB)")
message("UTILIZATION: ${ROM_USAGE_PERCENT}% of available ROM space")

if(NOT CODE_SIZE STREQUAL "Unknown")
    message("SEGMENT BREAKDOWN:")
    message("  CODE segment:   ${CODE_SIZE} bytes")
    message("  JUMPS segment:  ${JUMPS_SIZE} bytes")  
    message("  VECS segment:   ${VECS_SIZE} bytes")
    math(EXPR TOTAL_SEGMENTS "${CODE_SIZE} + ${JUMPS_SIZE} + ${VECS_SIZE}")
    message("  Total segments: ${TOTAL_SEGMENTS} bytes")
endif()

# Calculate remaining space
math(EXPR REMAINING "${ROM_CAPACITY} - ${ROM_SIZE}")
message("REMAINING SPACE: ${REMAINING} bytes")

# Show warnings if ROM is getting full
if(ROM_USAGE_PERCENT GREATER 90)
    message("WARNING: ROM utilization is over 90%!")
elseif(ROM_USAGE_PERCENT GREATER 75)
    message("NOTICE: ROM utilization is over 75%")
endif()

# Success message
message("ROM build completed successfully.")