# Check BASIC ROM size against the bankable module window.
# The window grew to 16 KB when the kernel shrank to a 4 KB BIOS at $F000 and the
# reclaimed $E000-$EFFF went to the banks. BASIC is the only module that was near
# the old 12 KB ceiling (10,613 bytes, 86%).
file(SIZE ${BASIC_ROM_FILE} ROM_SIZE)

message(STATUS "BASIC ROM size: ${ROM_SIZE} bytes (16384 max)")

if(ROM_SIZE GREATER 16384)
    message(FATAL_ERROR "ERROR: BASIC ROM exceeds the 16KB module window (${ROM_SIZE} > 16384)")
endif()
