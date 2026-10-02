# Drift guard: every DOS command is in HELP.
#
# A verb is two edits in dos.asm: a keyword in DOS_VERB_TAB, which is what makes
# it run, and a help line in DOS_HELP_TABLE, which is the only way anyone finds
# out it exists. Nothing ties the two together, and the second was missed twice:
# SHUTDOWN had its help line written and never put in the table, and BANKS never
# had one at all. Both worked; neither was listed.
#
# The rule: each keyword in DOS_VERB_TAB appears, in capitals and as a whole
# word, in some help line the table lists -- so an alias counts as covered when
# its line names it ("CATALOG [pat]  list files (CAT)", "CLS, CLEAR ...").

cmake_minimum_required(VERSION 3.20)

if(NOT DOS_ASM)
    message(FATAL_ERROR "DOS_ASM must be specified")
endif()
file(READ "${DOS_ASM}" src)

# The text of a `NAME: .BYTE "syntax", $09, "description", 0` label: every quoted
# string on the line, joined, so a help line's description counts as well as its
# syntax.
macro(label_string label out)
    string(REGEX MATCH "\n${label}:[ \t]*\\.BYTE[^\n]*" _line "${src}")
    if(NOT _line)
        message(FATAL_ERROR "no string for ${label} in dos.asm")
    endif()
    string(REGEX MATCHALL "\"[^\"]*\"" _parts "${_line}")
    string(REPLACE ";" " " _parts "${_parts}")
    string(REPLACE "\"" "" ${out} "${_parts}")
endmacro()

# The verb table: from DOS_VERB_TAB: to its $0000 terminator.
string(REGEX MATCH "\nDOS_VERB_TAB:(.*)" _rest "${src}")
string(FIND "${CMAKE_MATCH_1}" "$0000" _end)
string(SUBSTRING "${CMAKE_MATCH_1}" 0 ${_end} verbs)
string(REGEX MATCHALL "KW_[A-Z0-9_]+" kw_labels "${verbs}")
if(NOT kw_labels)
    message(FATAL_ERROR "found no keywords in DOS_VERB_TAB")
endif()

# The help table: from DOS_HELP_TABLE: to DOS_HELP_COUNT.
string(REGEX MATCH "\nDOS_HELP_TABLE:(.*)\nDOS_HELP_COUNT" _m "${src}")
string(REGEX MATCHALL "DH_[A-Z0-9_]+" dh_labels "${CMAKE_MATCH_1}")
set(help "")
foreach(dh IN LISTS dh_labels)
    label_string(${dh} text)
    string(APPEND help " ${text} ")
endforeach()
# A help line is "syntax", TAB, "description"; words are runs of capitals.
string(REGEX REPLACE "[^A-Z]" " " help "${help}")

set(missing "")
foreach(kw IN LISTS kw_labels)
    label_string(${kw} word)
    if(NOT help MATCHES " ${word} ")
        list(APPEND missing "${word} (${kw})")
    endif()
endforeach()

if(missing)
    string(REPLACE ";" "\n    " missing "${missing}")
    message(FATAL_ERROR
        "DOS commands that run but HELP does not list:\n\n    ${missing}\n\n"
        "Add a DH_ line for each to DOS_HELP_TABLE in dos.asm, or name it in an "
        "existing line if it is an alias.")
endif()
list(LENGTH kw_labels n)
message(STATUS "all ${n} DOS command keywords are listed by HELP")
