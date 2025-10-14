# Check BASIC ROM size
file(SIZE ${BASIC_ROM_FILE} ROM_SIZE)

message(STATUS "BASIC ROM size: ${ROM_SIZE} bytes (12288 max)")

if(ROM_SIZE GREATER 12288)
    message(FATAL_ERROR "ERROR: BASIC ROM exceeds 12KB limit (${ROM_SIZE} > 12288)")
endif()