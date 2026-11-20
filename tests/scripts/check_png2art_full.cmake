# png2art -o /dev/full: the fflush/ferror/fclose failure path must exit non-zero with
# one stderr line. Where /dev/full does not exist it prints PNG2ART_SKIP and exits 0,
# which ctest reports as skipped (SKIP_REGULAR_EXPRESSION; works on every CMake 3.20+,
# unlike cmake_language(EXIT 77), which needs 3.29).

cmake_minimum_required(VERSION 3.20)
if(NOT PNG2ART OR NOT DATA OR NOT WORK)
    message(FATAL_ERROR "PNG2ART, DATA and WORK must be specified")
endif()
if(NOT EXISTS /dev/full)
    message(STATUS "PNG2ART_SKIP no /dev/full: the write-error path cannot be forced")
    return()
endif()
execute_process(COMMAND "${PNG2ART}" -s 4x4 -o /dev/full "${DATA}/icon.png"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
string(STRIP "${_err}" _errs)
if(_rc EQUAL 0 OR NOT _rc MATCHES "^[0-9]+$")
    message(FATAL_ERROR "-o /dev/full: exit '${_rc}', want a plain non-zero exit")
endif()
if(NOT _out STREQUAL "" OR _errs STREQUAL "" OR _errs MATCHES "\n")
    message(FATAL_ERROR "-o /dev/full: want one stderr line and no stdout: out '${_out}' err '${_err}'")
endif()

# The same failure with a regular-file target, so png2art writes <target>.tmp: the
# temporary is a symlink to /dev/full, the write fails, and the cleanup must remove it,
# leaving the previous target content.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/target.art" "KEEP ME\n")
file(CREATE_LINK /dev/full "${WORK}/target.art.tmp" SYMBOLIC)
execute_process(COMMAND ls -lL /dev/full OUTPUT_VARIABLE _full_before)
string(REGEX MATCH "^[^ ]+ +[0-9]+ +[^ ]+ +[^ ]+" _full_before "${_full_before}")   # mode, links, owner, group
execute_process(COMMAND "${PNG2ART}" -s 4x4 -o "${WORK}/target.art" "${DATA}/icon.png"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
string(STRIP "${_err}" _errs)
if(_rc EQUAL 0 OR NOT _rc MATCHES "^[0-9]+$")
    message(FATAL_ERROR "failed tmp write: exit '${_rc}', want a plain non-zero exit")
endif()
if(NOT _out STREQUAL "" OR _errs STREQUAL "" OR _errs MATCHES "\n")
    message(FATAL_ERROR "failed tmp write: want one stderr line and no stdout: out '${_out}' err '${_err}'")
endif()
file(READ "${WORK}/target.art" _kept)
if(NOT _kept STREQUAL "KEEP ME\n")
    message(FATAL_ERROR "failed tmp write altered the target: ${_kept}")
endif()
file(GLOB _left "${WORK}/*")
if(NOT _left STREQUAL "${WORK}/target.art")
    message(FATAL_ERROR "failed tmp write left files behind: ${_left}")
endif()
execute_process(COMMAND ls -lL /dev/full OUTPUT_VARIABLE _full_after)
string(REGEX MATCH "^[^ ]+ +[0-9]+ +[^ ]+ +[^ ]+" _full_after "${_full_after}")
if(NOT _full_before STREQUAL _full_after)
    message(FATAL_ERROR "png2art changed /dev/full: '${_full_before}' became '${_full_after}'")
endif()
file(REMOVE_RECURSE "${WORK}")
