# png2art -o /dev/full: the fflush/ferror/fclose failure path must exit non-zero with
# one stderr line. Exits 77 (ctest SKIP_RETURN_CODE) where /dev/full does not exist.

cmake_minimum_required(VERSION 3.20)
if(NOT PNG2ART OR NOT DATA)
    message(FATAL_ERROR "PNG2ART and DATA must be specified")
endif()
if(NOT EXISTS /dev/full)
    message(STATUS "no /dev/full: the write-error path cannot be forced")
    cmake_language(EXIT 77)
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
