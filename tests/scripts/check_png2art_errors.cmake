# png2art must fail cleanly on bad input: non-zero exit, one line on stderr, nothing
# on stdout, no crash. Missing file, a PNG cut off mid-stream, and a file that is not
# an image at all; bad options, names and palettes; an output that cannot be written.

cmake_minimum_required(VERSION 3.20)
if(NOT PNG2ART OR NOT DATA OR NOT WORK)
    message(FATAL_ERROR "PNG2ART, DATA and WORK must be specified")
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
foreach(_kw int while return static _Bool bool true false nullptr constexpr typeof static_assert
        alignas thread_local asm fastcall cdecl __x _Zed __ _ _x _a1 _1 _9x _9 _0abc)
    expect_refused("keyword name ${_kw}" -s 4x4 -n ${_kw} "${_icon}")
endforeach()
foreach(_sz 4294967298x4 4x4294967298 99999999999999999999x4 -4x4 +4x4 " 4x4" 4x 4xx4 4x4x)
    expect_refused("size '${_sz}'" -s "${_sz}" "${_icon}")
endforeach()
foreach(_t 256 -1 +5 4294967295 4294967551 "" 1x 99999999999999999999)
    expect_refused("threshold '${_t}'" -s 4x4 -t "${_t}" "${_icon}")
endforeach()
# each fixture is 15 lines with one bad line, line 5, so only a per-line check catches it
foreach(_p pal_0x pal_plus pal_minus pal_seven pal_short pal_longline)
    expect_refused("palette ${_p}" -s 4x4 -p "${DATA}/${_p}.txt" "${_icon}")
    execute_process(COMMAND "${PNG2ART}" -s 4x4 -p "${DATA}/${_p}.txt" "${_icon}"
        ERROR_VARIABLE _perr)
    if(NOT _perr MATCHES "line 5 ")
        set(_fail "${_fail}palette ${_p}: error does not name line 5: ${_perr}\n")
    endif()
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
# recolour maps and ramps: slot 0 and non-hex characters are refused
foreach(_m "0=1" "1=0" "G=1" "1=G" "4-1" "4=" "=1" "41" "4=1," "4=1,,C=9" "4=12" " 4=1")
    expect_refused("map '${_m}'" -s 4x4 -m "${_m}" "${_icon}")
endforeach()
foreach(_r 0 "8,0,F" 807 G "8,G" "," "8 7" 1234567890ABCDEF)
    expect_refused("ramp '${_r}'" -s 4x4 -B "${_r}" "${_icon}")
endforeach()
# glyph mode: width must be 8, height 1..32, no palette work
foreach(_g 4x8 6x8 10x8 16x8 8x0 8x33 8x128)
    expect_refused("glyph size ${_g}" -g -s ${_g} "${DATA}/glyph.png")
endforeach()
expect_refused("glyph with -m" -g -m 4=1 "${DATA}/glyph.png")
expect_refused("glyph with -B" -g -B 8,F "${DATA}/glyph.png")
expect_refused("glyph with -p" -g -p "${DATA}/pal.txt" "${DATA}/glyph.png")
foreach(_g 8x1 8x32)
    execute_process(COMMAND "${PNG2ART}" -g -s ${_g} "${DATA}/glyph.png"
        RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT _rc EQUAL 0)
        set(_fail "${_fail}glyph size ${_g} refused\n")
    endif()
endforeach()
# a 63-character name is the longest allowed
execute_process(COMMAND "${PNG2ART}" -s 4x4 -n ${_name63} "${_icon}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    set(_fail "${_fail}63-character name refused: ${_err}\n")
endif()

# Output that cannot be written: -o into a directory that does not exist. (The write
# that fails after fopen is check_png2art_full.cmake.)
expect_refused("-o into a missing directory" -s 4x4 -o "${DATA}/no_such_dir/out.art" "${_icon}")
if(EXISTS "${DATA}/no_such_dir")
    set(_fail "${_fail}-o created a directory\n")
endif()

# A failed -o leaves the existing target untouched and no stray file: the target is a
# regular file with known content, and the temporary <target>.tmp cannot be created
# because a directory of that name is in the way.
set(_wdir "${WORK}")
file(REMOVE_RECURSE "${_wdir}")
file(MAKE_DIRECTORY "${_wdir}/target.art.tmp")
file(WRITE "${_wdir}/target.art" "KEEP ME\n")
expect_refused("-o with the temporary file blocked" -s 4x4 -o "${_wdir}/target.art" "${_icon}")
file(READ "${_wdir}/target.art" _kept)
if(NOT _kept STREQUAL "KEEP ME\n")
    set(_fail "${_fail}failed -o altered the existing target: ${_kept}\n")
endif()
file(GLOB _left "${_wdir}/*")
list(SORT _left)
if(NOT _left STREQUAL "${_wdir}/target.art;${_wdir}/target.art.tmp")
    set(_fail "${_fail}failed -o left files behind: ${_left}\n")
endif()
file(REMOVE_RECURSE "${_wdir}")

# -o naming a directory is refused at once: no <dir>.tmp is created, written or deleted,
# so a user's own <dir>.tmp is left byte for byte as it was.
file(MAKE_DIRECTORY "${_wdir}/dir.art")
file(WRITE "${_wdir}/dir.art.tmp" "MINE\n")
expect_refused("-o naming a directory" -s 4x4 -o "${_wdir}/dir.art" "${_icon}")
file(READ "${_wdir}/dir.art.tmp" _mine)
if(NOT _mine STREQUAL "MINE\n")
    set(_fail "${_fail}-o naming a directory touched dir.art.tmp: ${_mine}\n")
endif()
file(GLOB _left "${_wdir}/*")
list(SORT _left)
if(NOT _left STREQUAL "${_wdir}/dir.art;${_wdir}/dir.art.tmp")
    set(_fail "${_fail}-o naming a directory changed the directory: ${_left}\n")
endif()
file(REMOVE_RECURSE "${_wdir}")

# A write that fails after <target>.tmp is open removes the tmp file and leaves the
# target as it was. Portable (macOS and Linux, no /dev/full): a file-size limit of 0
# makes the flush fail with EFBIG; SIGXFSZ is ignored so the process survives to clean up.
find_program(_SH sh)
if(_SH)
    file(MAKE_DIRECTORY "${_wdir}")
    file(WRITE "${_wdir}/target.art" "KEEP ME\n")
    execute_process(COMMAND "${_SH}" -c "trap '' XFSZ; ulimit -f 0; exec \"$0\" \"$@\""
        "${PNG2ART}" -s 4x4 -o "${_wdir}/target.art" "${_icon}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    string(STRIP "${_err}" _errs)
    if(_rc EQUAL 0 OR NOT _rc MATCHES "^[0-9]+$")
        set(_fail "${_fail}size-limited write: exit '${_rc}', want a plain non-zero exit\n")
    endif()
    if(NOT _out STREQUAL "" OR _errs STREQUAL "" OR _errs MATCHES "\n")
        set(_fail "${_fail}size-limited write: want one stderr line, no stdout: out '${_out}' err '${_err}'\n")
    endif()
    file(READ "${_wdir}/target.art" _kept)
    if(NOT _kept STREQUAL "KEEP ME\n")
        set(_fail "${_fail}size-limited write altered the target: ${_kept}\n")
    endif()
    file(GLOB _left "${_wdir}/*")
    if(NOT _left STREQUAL "${_wdir}/target.art")
        set(_fail "${_fail}size-limited write left files behind: ${_left}\n")
    endif()
    file(REMOVE_RECURSE "${_wdir}")
endif()

# A <target>.tmp that is a symlink must not have its target's mode changed by the
# kept-mode step: the pointed-at file stays 0600 though the existing target is 0644.
file(MAKE_DIRECTORY "${_wdir}")
file(WRITE "${_wdir}/real.dat" "X\n")
file(CHMOD "${_wdir}/real.dat" PERMISSIONS OWNER_READ OWNER_WRITE)
file(WRITE "${_wdir}/target.art" "OLD\n")
file(CHMOD "${_wdir}/target.art" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
file(CREATE_LINK "${_wdir}/real.dat" "${_wdir}/target.art.tmp" SYMBOLIC)
execute_process(COMMAND "${PNG2ART}" -s 4x4 -o "${_wdir}/target.art" "${_icon}" RESULT_VARIABLE _rc ERROR_VARIABLE _err)
execute_process(COMMAND ls -l "${_wdir}/real.dat" OUTPUT_VARIABLE _lsl)
if(NOT _lsl MATCHES "^-rw-------[@+.]? ")
    set(_fail "${_fail}a symlinked <target>.tmp had its target's mode changed: ${_lsl}\n")
endif()
file(REMOVE_RECURSE "${_wdir}")

# -o naming a symlink writes through and keeps the link; -o naming a read-only file is
# refused and the file is not replaced.
file(MAKE_DIRECTORY "${_wdir}")
file(WRITE "${_wdir}/real.art" "OLD\n")
file(CREATE_LINK real.art "${_wdir}/link.art" SYMBOLIC)
execute_process(COMMAND "${PNG2ART}" -s 4x4 -o "${_wdir}/link.art" "${_icon}" RESULT_VARIABLE _rc ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    set(_fail "${_fail}-o through a symlink failed: ${_err}\n")
endif()
if(NOT IS_SYMLINK "${_wdir}/link.art")
    set(_fail "${_fail}-o replaced the symlink with a file\n")
endif()
file(READ "${_wdir}/real.art" _real)
if(NOT _real MATCHES "^@sprite ")
    set(_fail "${_fail}-o through a symlink did not write the link target: ${_real}\n")
endif()
file(WRITE "${_wdir}/ro.art" "RO\n")
file(CHMOD "${_wdir}/ro.art" PERMISSIONS OWNER_READ GROUP_READ WORLD_READ)
expect_refused("-o naming a read-only file" -s 4x4 -o "${_wdir}/ro.art" "${_icon}")
file(READ "${_wdir}/ro.art" _ro)
if(NOT _ro STREQUAL "RO\n")
    set(_fail "${_fail}-o replaced a read-only file: ${_ro}\n")
endif()
file(REMOVE_RECURSE "${_wdir}")

# -o naming a dangling symlink is refused and the link is left as it was.
file(MAKE_DIRECTORY "${_wdir}")
file(CREATE_LINK missing.art "${_wdir}/dangling.art" SYMBOLIC)
expect_refused("-o naming a dangling symlink" -s 4x4 -o "${_wdir}/dangling.art" "${_icon}")
if(NOT IS_SYMLINK "${_wdir}/dangling.art" OR EXISTS "${_wdir}/missing.art")
    set(_fail "${_fail}-o replaced or wrote through a dangling symlink\n")
endif()

# -o naming a file the caller owns but cannot write although a group bit allows it: mode
# 0464 has a write bit set, so only access() sees it is unwritable. Root can write
# anything, so that variant is Skipped there (the 0444 case above still runs).
execute_process(COMMAND id -u OUTPUT_VARIABLE _uid OUTPUT_STRIP_TRAILING_WHITESPACE)
if(_uid STREQUAL "0")
    message(STATUS "SKIPPED variant: group-writable-only file, running as root")
else()
    file(WRITE "${_wdir}/grp.art" "GRP\n")
    file(CHMOD "${_wdir}/grp.art" PERMISSIONS OWNER_READ GROUP_READ GROUP_WRITE WORLD_READ)
    expect_refused("-o naming a group-writable-only file" -s 4x4 -o "${_wdir}/grp.art" "${_icon}")
    file(READ "${_wdir}/grp.art" _grp)
    if(NOT _grp STREQUAL "GRP\n")
        set(_fail "${_fail}-o replaced a group-writable-only file: ${_grp}\n")
    endif()
endif()
# A file owned by another user needs root to construct: Skipped here, covered by the
# same access() call as the group-writable-only case.

# The written file never carries setuid/setgid, even when the target had them.
if(NOT WIN32)
    file(WRITE "${_wdir}/suid.art" "OLD\n")
    execute_process(COMMAND chmod 6755 "${_wdir}/suid.art")
    execute_process(COMMAND "${PNG2ART}" -s 4x4 -o "${_wdir}/suid.art" "${_icon}" RESULT_VARIABLE _rc)
    execute_process(COMMAND ls -l "${_wdir}/suid.art" OUTPUT_VARIABLE _lsl)
    if(NOT _rc EQUAL 0 OR _lsl MATCHES "^-..[sS]" OR _lsl MATCHES "^-.....[sS]")
        set(_fail "${_fail}-o kept setuid/setgid on the written file (rc ${_rc}): ${_lsl}\n")
    endif()
endif()
file(REMOVE_RECURSE "${_wdir}")

if(_fail)
    message(FATAL_ERROR "${_fail}")
endif()
