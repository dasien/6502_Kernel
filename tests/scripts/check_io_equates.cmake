# Drift guard: the I/O addresses are stated in three places, and they must agree.
#
#   src/kernel/kernel_vars.inc  the ROM side; carries kernel-private workspace
#                               a .PRG has no business seeing
#   programs/common/mfc.inc     the program side; carries the two ABI jump
#                               tables the ROM does not declare
#   src/kernel/dos/dos.asm      the DOS, which includes neither and declares
#                               its own equates for the registers it drives
#
# No one of them is a subset of another, so none can include another, which
# leaves every overlap free to drift. dos.asm is the easiest to forget because
# it looks self-contained -- but it names FIO_*, PAGE_ENABLE, MON_CMDBUF, the
# VIC command port and the palette registers, and any of those moving elsewhere
# would leave the DOS quietly writing to the wrong address.
#
# Compares every name shared by any two of the three and fails on disagreement.

# Run with `cmake -P`, which starts with no policies set, so IN_LIST below is
# parsed as a plain argument rather than an operator and the script dies before
# it compares anything. Stating the project's minimum sets the policies this
# needs (CMP0057 among them) and makes the guard actually run.
cmake_minimum_required(VERSION 3.20)

foreach(_var KERNEL_VARS MFC_INC DOS_ASM)
    if(NOT ${_var})
        message(FATAL_ERROR "${_var} must be specified")
    endif()
    if(NOT EXISTS "${${_var}}")
        message(FATAL_ERROR "does not exist: ${${_var}}")
    endif()
endforeach()

# name = $ADDR, one per line, comments stripped. Anything with an expression on
# the right (ATTR_BORDER = $40 | $06) is skipped: it is a composed value, not an
# address, and the two files need not spell it the same way.
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
read_equates("${DOS_ASM}"     DA_NAMES DA)

# A file that suddenly parses to almost nothing means the regex or the file's
# shape has changed, and the guard has stopped guarding without saying so.
foreach(_pair "KV;kernel_vars.inc" "MI;mfc.inc" "DA;dos.asm")
    list(GET _pair 0 _p)
    list(GET _pair 1 _label)
    list(LENGTH ${_p}_NAMES _n)
    if(_n LESS 10)
        message(FATAL_ERROR
            "only ${_n} equates parsed out of ${_label} -- this check has stopped "
            "checking anything. Has the file been restructured?")
    endif()
endforeach()

# Compare each pair. Names are compared, not addresses, so a register that moves
# in one file and not another is caught wherever it is named twice.
set(_compared 0)
set(_bad "")
foreach(_combo "KV;kernel_vars.inc;MI;mfc.inc"
               "KV;kernel_vars.inc;DA;dos.asm"
               "MI;mfc.inc;DA;dos.asm")
    list(GET _combo 0 _pa)
    list(GET _combo 1 _la)
    list(GET _combo 2 _pb)
    list(GET _combo 3 _lb)
    foreach(_n IN LISTS ${_pb}_NAMES)
        if(_n IN_LIST ${_pa}_NAMES)
            math(EXPR _compared "${_compared} + 1")
            if(NOT ${_pa}_${_n} STREQUAL ${_pb}_${_n})
                list(APPEND _bad
                     "${_n}: ${_la} says $${${_pa}_${_n}}, ${_lb} says $${${_pb}_${_n}}")
            endif()
        endif()
    endforeach()
endforeach()

if(_bad)
    list(REMOVE_DUPLICATES _bad)
    string(REPLACE ";" "\n  " _report "${_bad}")
    message(FATAL_ERROR
        "the I/O equates disagree:\n  ${_report}\n"
        "A register that moves has to move in every file that names it.")
endif()

if(_compared LESS 20)
    message(FATAL_ERROR
        "only ${_compared} equates are shared across the three files, which is too "
        "few to be the real overlap -- this check has stopped checking anything.")
endif()

message(STATUS "I/O equates agree across all ${_compared} shared names")
