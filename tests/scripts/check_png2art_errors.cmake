# png2art must fail cleanly on bad input: non-zero exit, one line on stderr, nothing
# on stdout, no crash. Missing file, a PNG cut off mid-stream, and a file that is not
# an image at all.

cmake_minimum_required(VERSION 3.20)
if(NOT PNG2ART OR NOT DATA)
    message(FATAL_ERROR "PNG2ART and DATA must be specified")
endif()

set(_fail "")
foreach(_f truncated.png notimage.png does_not_exist.png)
    execute_process(COMMAND "${PNG2ART}" -s 4x4 "${DATA}/${_f}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(_rc EQUAL 0 OR NOT _rc MATCHES "^[0-9]+$")
        set(_fail "${_fail}${_f}: exit '${_rc}', want a plain non-zero exit\n")
    endif()
    if(NOT _out STREQUAL "")
        set(_fail "${_fail}${_f}: wrote to stdout: ${_out}\n")
    endif()
    string(STRIP "${_err}" _errs)
    if(_errs STREQUAL "" OR _errs MATCHES "\n")
        set(_fail "${_fail}${_f}: stderr is not one line: '${_err}'\n")
    endif()
endforeach()

if(_fail)
    message(FATAL_ERROR "${_fail}")
endif()
