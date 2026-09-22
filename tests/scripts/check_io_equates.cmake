# Drift guard: programs/common/mfc.inc against src/kernel/kernel_vars.inc.
#
# The I/O register addresses are stated twice on purpose. kernel_vars.inc is the
# ROM side and carries kernel-private workspace a .PRG has no business seeing;
# mfc.inc is the program side and carries the two ABI jump tables the ROM does
# not declare. Neither is a subset of the other, so neither can include the
# other -- which leaves the overlap free to drift.
#
# This compares every name the two files share and fails on the first
# disagreement. A register that moves has to move in both.

# Run with `cmake -P`, which starts with no policies set, so IN_LIST below is
# parsed as a plain argument rather than an operator and the script dies before
# it compares anything. Stating the project's minimum sets the policies this
# needs (CMP0057 among them) and makes the guard actually run.
cmake_minimum_required(VERSION 3.20)

foreach(_var KERNEL_VARS MFC_INC)
    if(NOT ${_var})
        message(FATAL_ERROR "${_var} must be specified")
    endif()
    if(NOT EXISTS "${${_var}}")
        message(FATAL_ERROR "does not exist: ${${_var}}")
    endif()
endforeach()

# name = $ADDR, one per line, comments stripped.
function(read_equates path out_names out_prefix)
    file(STRINGS "${path}" _lines)
    set(_names "")
    foreach(_line IN LISTS _lines)
        string(REGEX REPLACE ";.*$" "" _line "${_line}")
        if(_line MATCHES "^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*(\\$?[0-9A-Fa-f]+)[ \t]*$")
            list(APPEND _names "${CMAKE_MATCH_1}")
            string(TOUPPER "${CMAKE_MATCH_2}" _val)
            string(REPLACE "$" "" _val "${_val}")
            set(${out_prefix}_${CMAKE_MATCH_1} "${_val}" PARENT_SCOPE)
        endif()
    endforeach()
    set(${out_names} "${_names}" PARENT_SCOPE)
endfunction()

read_equates("${KERNEL_VARS}" KV_NAMES KV)
read_equates("${MFC_INC}"     MI_NAMES MI)

set(_shared 0)
set(_bad "")
foreach(_n IN LISTS MI_NAMES)
    if(_n IN_LIST KV_NAMES)
        math(EXPR _shared "${_shared} + 1")
        if(NOT KV_${_n} STREQUAL MI_${_n})
            list(APPEND _bad "${_n}: kernel_vars.inc says $${KV_${_n}}, mfc.inc says $${MI_${_n}}")
        endif()
    endif()
endforeach()

if(_bad)
    string(REPLACE ";" "\n  " _report "${_bad}")
    message(FATAL_ERROR
        "programs/common/mfc.inc disagrees with src/kernel/kernel_vars.inc:\n  ${_report}\n"
        "The program side and the ROM side must name the same address.")
endif()

# A guard that checks nothing is worse than no guard: if the overlap collapses
# because a file was restructured, say so rather than passing quietly.
if(_shared LESS 20)
    message(FATAL_ERROR
        "only ${_shared} equates are shared between mfc.inc and kernel_vars.inc, "
        "which is too few to be the real overlap -- this check has stopped "
        "checking anything. Has either file been restructured?")
endif()

message(STATUS "mfc.inc agrees with kernel_vars.inc on all ${_shared} shared equates")
