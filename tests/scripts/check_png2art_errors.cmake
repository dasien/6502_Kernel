# png2art must fail cleanly on bad input: non-zero exit, one line on stderr, nothing
# on stdout, no crash. Missing file, a PNG cut off mid-stream, and a file that is not
# an image at all; bad options, names and palettes; an output that cannot be written.

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

# Bad options and palettes: refused with one stderr line, non-zero exit, no stdout.
set(_long_name "")
foreach(_i RANGE 1 64)
    string(APPEND _long_name "a")
endforeach()
string(SUBSTRING "${_long_name}" 0 63 _name63)
function(expect_refused label)
    execute_process(COMMAND "${PNG2ART}" ${ARGN}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    string(STRIP "${_err}" _errs)
    if(_rc EQUAL 0 OR NOT _rc MATCHES "^[0-9]+$")
        set(_fail "${_fail}${label}: exit '${_rc}', want a plain non-zero exit\n" PARENT_SCOPE)
    elseif(NOT _out STREQUAL "" OR _errs STREQUAL "" OR _errs MATCHES "\n")
        set(_fail "${_fail}${label}: want one stderr line and no stdout: out '${_out}' err '${_err}'\n" PARENT_SCOPE)
    endif()
endfunction()
set(_icon "${DATA}/icon.png")
expect_refused("name of 64 characters" -s 4x4 -n ${_long_name} "${_icon}")
foreach(_kw int while return static _Bool)
    expect_refused("keyword name ${_kw}" -s 4x4 -n ${_kw} "${_icon}")
endforeach()
foreach(_sz 4294967298x4 4x4294967298 99999999999999999999x4 -4x4 +4x4 " 4x4" 4x 4xx4 4x4x)
    expect_refused("size '${_sz}'" -s "${_sz}" "${_icon}")
endforeach()
foreach(_t 256 -1 +5 4294967295 4294967551 "" 1x 99999999999999999999)
    expect_refused("threshold '${_t}'" -s 4x4 -t "${_t}" "${_icon}")
endforeach()
foreach(_p pal_0x pal_plus pal_minus pal_seven pal_short pal_longline)
    expect_refused("palette ${_p}" -s 4x4 -p "${DATA}/${_p}.txt" "${_icon}")
endforeach()
# a box larger than the slot, a malformed box, a malformed rotation
expect_refused("box wider than the slot" -s 4x4 -b 6x2 "${_icon}")
expect_refused("box taller than the slot" -s 4x4 -b 2x6 "${_icon}")
expect_refused("box with an odd width" -s 4x4 -b 3x2 "${_icon}")
foreach(_b 0x0 2x0 x2 2x 2xx2 -2x2)
    expect_refused("box '${_b}'" -s 4x4 -b "${_b}" "${_icon}")
endforeach()
foreach(_r "" abc 45x 361 -361 nan)
    expect_refused("rotation '${_r}'" -s 4x4 -r "${_r}" "${_icon}")
endforeach()
# the palette message names the line
execute_process(COMMAND "${PNG2ART}" -s 4x4 -p "${DATA}/pal_0x.txt" "${_icon}"
    ERROR_VARIABLE _perr RESULT_VARIABLE _prc)
if(NOT _perr MATCHES "line 1")
    set(_fail "${_fail}palette error does not name the line: ${_perr}\n")
endif()
# a 63-character name is the longest allowed
execute_process(COMMAND "${PNG2ART}" -s 4x4 -n ${_name63} "${_icon}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    set(_fail "${_fail}63-character name refused: ${_err}\n")
endif()

# Output that cannot be written: -o into a directory that does not exist. (A write
# that fails after fopen, such as fclose on a full disk, cannot be forced portably.)
expect_refused("-o into a missing directory" -s 4x4 -o "${DATA}/no_such_dir/out.art" "${_icon}")
if(EXISTS "${DATA}/no_such_dir")
    set(_fail "${_fail}-o created a directory\n")
endif()

if(_fail)
    message(FATAL_ERROR "${_fail}")
endif()
