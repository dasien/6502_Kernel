# ROM Validation Script
# Validates the compiled 6502 kernel ROM file

if(NOT ROM_FILE)
    message(FATAL_ERROR "ROM_FILE must be specified")
endif()

if(NOT EXISTS "${ROM_FILE}")
    message(FATAL_ERROR "ROM file does not exist: ${ROM_FILE}")
endif()

# Get ROM file size
file(SIZE "${ROM_FILE}" ROM_SIZE)

message(STATUS "=== ROM VALIDATION ===")
message(STATUS "ROM File: ${ROM_FILE}")
message(STATUS "ROM Size: ${ROM_SIZE} bytes")

# Validate ROM size is exactly 4KB (4096 bytes)
if(NOT ROM_SIZE EQUAL 4096)
    message(FATAL_ERROR "Invalid ROM size: ${ROM_SIZE} bytes. Expected: 4096 bytes (4KB)")
endif()

# Read ROM content to validate critical sections
file(READ "${ROM_FILE}" ROM_CONTENT HEX)

# Extract interrupt vectors (last 6 bytes at $FFFA-$FFFF)
string(LENGTH "${ROM_CONTENT}" CONTENT_LENGTH)
math(EXPR VECTOR_START "${CONTENT_LENGTH} - 12")  # Last 6 bytes * 2 hex chars
string(SUBSTRING "${ROM_CONTENT}" ${VECTOR_START} 12 VECTORS_HEX)

# Convert to uppercase for consistency
string(TOUPPER "${VECTORS_HEX}" VECTORS_HEX)

message(STATUS "Interrupt Vectors (FFFA-FFFF): ${VECTORS_HEX}")

# Validate that vectors are not all zeros or all FFs
if(VECTORS_HEX STREQUAL "000000000000")
    message(FATAL_ERROR "Interrupt vectors are all zeros - ROM may be invalid")
endif()

if(VECTORS_HEX STREQUAL "FFFFFFFFFFFF")
    message(FATAL_ERROR "Interrupt vectors are all 0xFF - ROM may be invalid")
endif()

# Check for expected kernel strings (convert some ROM content to ASCII for inspection)
# Look for the "OK" response string that should be in the ROM
string(FIND "${ROM_CONTENT}" "4F4B" OK_FOUND)  # "OK" in hex
if(OK_FOUND EQUAL -1)
    message(WARNING "Could not find 'OK' string in ROM - monitor may not work correctly")
else()
    message(STATUS "Found 'OK' response string in ROM")
endif()

# Look for monitor command help strings
string(FIND "${ROM_CONTENT}" "4D4F4E49544F52" MONITOR_FOUND)  # "MONITOR" in hex
if(MONITOR_FOUND EQUAL -1)
    message(WARNING "Could not find 'MONITOR' string in ROM - help command may not work")
else()
    message(STATUS "Found 'MONITOR' string in ROM")
endif()

message(STATUS "ROM validation completed successfully")
message(STATUS "====================")