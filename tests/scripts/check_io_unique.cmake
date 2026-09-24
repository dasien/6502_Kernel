# Drift guard: no two names may claim the same I/O register or ABI entry.
#
# check_io_equates compares NAMES: a register that moves in one file and not
# another is caught wherever it is named twice. What that cannot see is two
# different names landing on one address, which is how new registers collide.
# The I/O page and the ABI tables are allocated like page 3 -- read the list, take
# the next free slot -- and two additions made on different branches both take the
# same one.
#
# That happened. The VIC palette took $FECB-$FECC while the frame counter, written
# against the tree before the palette landed, took $FECB too. Every name agreed
# with itself across files, so the name comparison passed; only a rebase conflict
# in an unrelated line made it visible. check_dos_vars guards page 3 the same way
# after DOS_THEME landed on DOS_W_ERR; this is its counterpart for registers.
#
# Two namespaces are checked, because the names differ by convention between them:
#   - the assembly includes (kernel_vars.inc, mfc.inc, dos.asm) as one: the same
#     register must carry the same name wherever it appears, so a second name at
#     an address is a second thing, even when the two names are in different files;
#   - the C++ chip headers as one: `static constexpr uint16_t kReg... = 0x....`,
#     which is the decoder itself. Two chips claiming one address shadow each
#     other silently, since Memory takes whichever it asks first.
#
# Ranges: the I/O page $FE00-$FEFF, the kernel ABI $FF00-$FF5F and the DOS ABI
# $AF00-$AF3F. Zero page, page 3 and plain constants are someone else's problem.

cmake_minimum_required(VERSION 3.20)

if(NOT ASM_FILES OR NOT CPP_FILES)
    message(FATAL_ERROR "ASM_FILES and CPP_FILES must be specified")
endif()
# Passed '|'-separated: a ';' in an add_test argument would split it into several.
string(REPLACE "|" ";" ASM_FILES "${ASM_FILES}")
string(REPLACE "|" ";" CPP_FILES "${CPP_FILES}")

function(in_guarded_range hex out)
    math(EXPR _a "0x${hex}")
    if((_a GREATER_EQUAL 65024 AND _a LESS_EQUAL 65279) OR     # $FE00-$FEFF
       (_a GREATER_EQUAL 65280 AND _a LESS_EQUAL 65375) OR     # $FF00-$FF5F
       (_a GREATER_EQUAL 44800 AND _a LESS_EQUAL 44863))       # $AF00-$AF3F
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# Known aliases: one register, two names. Each is a wart to remove, not a pattern to
# copy. The kernel calls the host file-I/O registers FILE_*, while the DOS, BASIC and
# the assembler call them FIO_*, so check_io_equates, which matches by name, cannot
# see one of them move while the other stays put. Listing them here turns that blind
# spot into a check: each pair must still name the same address, below.
set(_aliases
    "FILE_COMMAND=FIO_COMMAND"
    "FILE_STATUS=FIO_STATUS"
    "FILE_NAME_BUF=FIO_NAME")

# Record NAME at HEX in namespace NS; report a second, different name there.
macro(claim ns hex name where)
    string(TOUPPER "${hex}" _h)
    set(_addr_${ns}_${name} "${_h}")
    in_guarded_range("${_h}" _in)
    if(_in)
        math(EXPR _counted_${ns} "${_counted_${ns}} + 1")
        if(DEFINED _owner_${ns}_${_h})
            set(_o "${_owner_${ns}_${_h}}")
            if(NOT _o STREQUAL "${name}" AND
               NOT "${_o}=${name}" IN_LIST _aliases AND NOT "${name}=${_o}" IN_LIST _aliases)
                list(APPEND _bad "$${_h}: ${_o} (${_where_${ns}_${_h}}) and ${name} (${where})")
            endif()
        else()
            set(_owner_${ns}_${_h} "${name}")
            set(_where_${ns}_${_h} "${where}")
        endif()
    endif()
endmacro()

set(_bad "")
set(_counted_asm 0)
set(_counted_cpp 0)

foreach(_f IN LISTS ASM_FILES)
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "does not exist: ${_f}")
    endif()
    get_filename_component(_leaf "${_f}" NAME)
    file(STRINGS "${_f}" _lines)
    foreach(_line IN LISTS _lines)
        string(REGEX REPLACE ";.*$" "" _line "${_line}")
        if(_line MATCHES "^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*\\$([0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f])[ \t]*$")
            claim(asm "${CMAKE_MATCH_2}" "${CMAKE_MATCH_1}" "${_leaf}")
        endif()
    endforeach()
endforeach()

foreach(_f IN LISTS CPP_FILES)
    get_filename_component(_leaf "${_f}" NAME)
    file(STRINGS "${_f}" _lines REGEX "static constexpr uint16_t k[A-Za-z0-9_]+ *= *0x")
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "static constexpr uint16_t (k[A-Za-z0-9_]+) *= *0x([0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]) *;")
            claim(cpp "${CMAKE_MATCH_2}" "${CMAKE_MATCH_1}" "${_leaf}")
        endif()
    endforeach()
endforeach()

# The aliases have to stay aliases. If one name moves and its partner does not, the
# pair no longer collides above and would pass silently, so check it here.
foreach(_pair IN LISTS _aliases)
    string(REPLACE "=" ";" _names "${_pair}")
    list(GET _names 0 _n1)
    list(GET _names 1 _n2)
    if(NOT DEFINED _addr_asm_${_n1} OR NOT DEFINED _addr_asm_${_n2})
        list(APPEND _bad "alias ${_pair}: one name is no longer declared -- drop the entry, "
                         "or restore the name")
    elseif(NOT _addr_asm_${_n1} STREQUAL _addr_asm_${_n2})
        list(APPEND _bad "alias ${_pair}: ${_n1} is $${_addr_asm_${_n1}} but ${_n2} is "
                         "$${_addr_asm_${_n2}} -- one register, and it moved under one name")
    endif()
endforeach()

# A guard that parses nothing passes everything. Both sides hold dozens of
# registers; far fewer means a file moved or its format changed.
if(_counted_asm LESS 40 OR _counted_cpp LESS 40)
    message(FATAL_ERROR
        "only ${_counted_asm} assembly and ${_counted_cpp} C++ register addresses were "
        "found -- this check has stopped checking anything. Were the files restructured?")
endif()

if(_bad)
    list(REMOVE_DUPLICATES _bad)
    string(REPLACE ";" "\n  " _report "${_bad}")
    message(FATAL_ERROR
        "two names claim the same address:\n  ${_report}\n"
        "Take the next genuinely free address -- re-read docs/board.md rather than "
        "remembering where the page ended last time.")
endif()

message(STATUS "I/O and ABI addresses: ${_counted_asm} assembly and ${_counted_cpp} C++ "
               "claims, no address named twice")
