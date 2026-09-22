# Drift guard: no two DOS variables may share an address.
#
# Page 3 is the DOS's state area and it is nearly full. Allocations are made by
# reading the list and taking the next free byte, which works right up until two
# of them are added weeks apart and the second reader is working from a stale
# memory of the first.
#
# That is not hypothetical. DOS_THEME was placed at $03BC, which by then was
# DOS_W_ERR -- the write-error flag -- so every disk write stamped the theme
# index back to zero and the machine silently reverted to its default colours.
# It surfaced as "quitting this one game resets the theme", because that game
# writes a high-score file on the way out; any write did it. Nothing in review
# catches an overlap between two lines hundreds apart, but this does.
#
# Compares declared addresses only. A buffer that runs past its own start is a
# different failure and this will not see it.

cmake_minimum_required(VERSION 3.20)

if(NOT DOS_ASM)
    message(FATAL_ERROR "DOS_ASM must be specified")
endif()
if(NOT EXISTS "${DOS_ASM}")
    message(FATAL_ERROR "does not exist: ${DOS_ASM}")
endif()

file(STRINGS "${DOS_ASM}" _lines REGEX "^[A-Z_0-9]+ *= *\\$[0-9A-Fa-f]+")

set(_seen "")
set(_bad "")
foreach(_line IN LISTS _lines)
    if(NOT _line MATCHES "^([A-Z_0-9]+) *= *\\$([0-9A-Fa-f]+)")
        continue()
    endif()
    set(_name "${CMAKE_MATCH_1}")
    set(_addr "${CMAKE_MATCH_2}")

    # Page 3 only. Zero page and the I/O registers are deliberately shared with
    # the kernel and with each other, and equate-style constants are not storage.
    string(LENGTH "${_addr}" _len)
    if(NOT _len EQUAL 4)
        continue()
    endif()
    string(SUBSTRING "${_addr}" 0 2 _page)
    if(NOT _page STREQUAL "03")
        continue()
    endif()

    if(DEFINED _owner_${_addr})
        list(APPEND _bad "$${_addr}: ${_owner_${_addr}} and ${_name}")
    else()
        set(_owner_${_addr} "${_name}")
        list(APPEND _seen "${_addr}")
    endif()
endforeach()

list(LENGTH _seen _count)
if(_count EQUAL 0)
    message(FATAL_ERROR "parsed no page-3 variables from ${DOS_ASM} -- the guard "
                        "is not looking at what it thinks it is")
endif()

if(_bad)
    string(REPLACE ";" "\n  " _report "${_bad}")
    message(FATAL_ERROR
        "two DOS variables share an address:\n  ${_report}\n"
        "Page 3 is nearly full; take the next genuinely free byte, and re-read "
        "the list rather than remembering where it ended last time.")
endif()

message(STATUS "DOS page-3 variables: ${_count} addresses, none shared")
