# png2art turns an image into an @sprite block. This pins, on small fixtures whose
# output was worked out by hand (tests/data/png2art/icon.png, 8x8 scaled to 4x4):
#   - no arguments: a usage message naming the options, non-zero exit;
#   - the default output equals icon.art byte for byte, and spr2c accepts it;
#   - a transparent border around the same picture changes nothing (trim);
#   - the right-hand column is partly transparent: an alpha average of 120 stays
#     opaque and white (premultiplied, a plain average with the transparent black
#     beside it would give grey), 100 is '.', and -t 90 keeps it;
#   - a palette given with -p snaps to that palette's slots (icon_pal.art);
#   - no opaque pixel is slot 0: every row is only '.' and 1-F;
#   - the built-in palette is the one VIC::seedPalette() sets (read from VIC.cpp).
# Errors on a missing, truncated or non-image file are in check_png2art_errors.cmake.

cmake_minimum_required(VERSION 3.20)
if(NOT PNG2ART OR NOT SPR2C OR NOT DATA OR NOT WORK OR NOT VIC_CPP)
    message(FATAL_ERROR "PNG2ART, SPR2C, DATA, WORK and VIC_CPP must be specified")
endif()
file(MAKE_DIRECTORY "${WORK}")

set(_fail "")

function(run_tool outvar)
    execute_process(COMMAND "${PNG2ART}" ${ARGN}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    set(${outvar} "${_out}" PARENT_SCOPE)
    set(${outvar}_rc "${_rc}" PARENT_SCOPE)
    set(${outvar}_err "${_err}" PARENT_SCOPE)
endfunction()

function(expect_same label actual expected_file)
    file(READ "${expected_file}" _want)
    if(NOT actual STREQUAL _want)
        set(_fail "${_fail}${label}: output differs from ${expected_file}\n--- got\n${actual}--- want\n${_want}" PARENT_SCOPE)
    endif()
endfunction()

# --- usage ---------------------------------------------------------------------
run_tool(u)
if(u_rc EQUAL 0)
    set(_fail "${_fail}no arguments exited 0\n")
endif()
foreach(_opt "-s" "-n" "-b" "-r" "-m" "-B" "-t" "-p" "-o" "--dump-palette")
    string(FIND "${u_err}" "  ${_opt} " _pos)
    if(_pos EQUAL -1)
        set(_fail "${_fail}usage does not name ${_opt}\n")
    endif()
endforeach()

# --- default palette, trim, threshold, custom palette ----------------------------
run_tool(a -s 4x4 -n icon "${DATA}/icon.png")
if(NOT a_rc EQUAL 0)
    set(_fail "${_fail}icon.png failed: ${a_err}\n")
endif()
expect_same("default" "${a}" "${DATA}/icon.art")

run_tool(b -s 4x4 -n icon "${DATA}/icon_padded.png")
expect_same("padded" "${b}" "${DATA}/icon.art")

run_tool(c -s 4x4 -n icon -t 90 "${DATA}/icon.png")
expect_same("threshold 90" "${c}" "${DATA}/icon_t90.art")

run_tool(d -s 4x4 -n icon -p "${DATA}/pal.txt" "${DATA}/icon.png")
expect_same("palette" "${d}" "${DATA}/icon_pal.art")

# --- GIF input, -o, a 16-line palette, --dump-palette with -p --------------------
run_tool(g -s 4x4 -n icon "${DATA}/icon.gif")
if(NOT g_rc EQUAL 0)
    set(_fail "${_fail}icon.gif failed: ${g_err}\n")
endif()
expect_same("gif" "${g}" "${DATA}/icon_gif.art")

file(REMOVE "${WORK}/o_opt.art")
run_tool(o -s 4x4 -n icon -o "${WORK}/o_opt.art" "${DATA}/icon.png")
if(NOT o_rc EQUAL 0 OR NOT o STREQUAL "")
    set(_fail "${_fail}-o: exit ${o_rc}, stdout '${o}', stderr '${o_err}'\n")
endif()
if(EXISTS "${WORK}/o_opt.art")
    file(READ "${WORK}/o_opt.art" _o_file)
    expect_same("-o file" "${_o_file}" "${DATA}/icon.art")
else()
    set(_fail "${_fail}-o wrote no file\n")
endif()

run_tool(q -s 4x4 -n icon -p "${DATA}/pal16.txt" "${DATA}/icon.png")
expect_same("16-line palette" "${q}" "${DATA}/icon_pal16.art")

run_tool(dp --dump-palette -p "${DATA}/pal16.txt")
expect_same("dump with 16-line -p" "${dp}" "${DATA}/pal16_dump.txt")
run_tool(dq -p "${DATA}/pal.txt" --dump-palette)
expect_same("dump with 15-line -p" "${dq}" "${DATA}/pal_dump.txt")

# --- -b: a smaller box centred in the slot; -r: rotate the source first ------------
# icon.png in a 2x2 box in 4x4 is the 2x2 picture inside a ring of '.'. quad.png is
# four colour quadrants; rotated 90 degrees it is exact (blue, red / yellow, green
# from the top left), and at 45 it is a diamond. Each result must also pass spr2c.
foreach(_case "icon.png;icon;4x4;-b|2x2;icon_box2.art" "icon.png;icon;4x4;-r|90;icon_r90.art"
              "quad.png;quad;8x8;-r|90;quad_r90.art" "quad.png;quad;8x8;-r|45;quad_r45.art"
              "quad.png;quad;8x8;-r|45|-b|4x4;quad_r45_box4.art")
    list(GET _case 0 _img)
    list(GET _case 1 _nm)
    list(GET _case 2 _size)
    list(GET _case 3 _opts)
    list(GET _case 4 _exp)
    string(REPLACE "|" ";" _opts "${_opts}")
    run_tool(sh -s ${_size} -n ${_nm} ${_opts} "${DATA}/${_img}")
    if(NOT sh_rc EQUAL 0)
        set(_fail "${_fail}${_exp}: png2art failed: ${sh_err}\n")
    endif()
    expect_same("${_img} ${_opts}" "${sh}" "${DATA}/${_exp}")
    file(WRITE "${WORK}/${_exp}" "${sh}")
    execute_process(COMMAND "${SPR2C}" "${WORK}/${_exp}" "${WORK}/${_exp}.c"
        RESULT_VARIABLE _src ERROR_VARIABLE _serr)
    if(NOT _src EQUAL 0)
        set(_fail "${_fail}spr2c rejected ${_exp}: ${_serr}\n")
    endif()
endforeach()

# --- -m: recolour slot for slot; -B: brightness bands into a hand-picked ramp ---------
# quad.png unmapped is the 9/1/2/4/B picture of quad_r90's source; -m 4=D,B=3,2=A must
# equal the unmapped output with only those slots changed (committed quad_map.art).
run_tool(m0 -s 8x8 -n quad "${DATA}/quad.png")
run_tool(m1 -s 8x8 -n quad -m 4=D,B=3,2=A "${DATA}/quad.png")
expect_same("-m 4=D,B=3,2=A" "${m1}" "${DATA}/quad_map.art")
set(_by_hand "${m0}")
string(REGEX REPLACE "^([^\n]*\n)" "\\1" _by_hand "${_by_hand}")
string(REPLACE "4" "D" _by_hand "${_by_hand}")
string(REPLACE "B" "3" _by_hand "${_by_hand}")
string(REPLACE "2" "A" _by_hand "${_by_hand}")
string(REPLACE "@sprite quad 8x8" "@sprite quad 8x8" _by_hand "${_by_hand}")
if(NOT _by_hand STREQUAL m1)
    set(_fail "${_fail}-m output is not the unmapped output with only those slots changed\n")
endif()
# rock.png is one grey with texture: the plain run is flat; -B 8,7,F bins it.
run_tool(bd -s 16x16 -n rock -B 8,7,F "${DATA}/rock.png")
if(NOT bd_rc EQUAL 0)
    set(_fail "${_fail}-B failed: ${bd_err}\n")
endif()
expect_same("-B 8,7,F" "${bd}" "${DATA}/rock_band.art")
string(REPLACE "\n" ";" _bl "${bd}")
list(POP_FRONT _bl _bh)
set(_used "")
foreach(_l IN LISTS _bl)
    if(NOT _l STREQUAL "" AND NOT _l MATCHES "^[.87F]+$")
        set(_fail "${_fail}-B row '${_l}' uses a slot outside the ramp or slot 0\n")
    endif()
    string(REGEX REPLACE "[^87F]" "" _chars "${_l}")
    string(APPEND _used "${_chars}")
endforeach()
set(_distinct 0)
foreach(_s 8 7 F)
    string(FIND "${_used}" "${_s}" _p)
    if(NOT _p EQUAL -1)
        math(EXPR _distinct "${_distinct} + 1")
    endif()
endforeach()
if(_distinct LESS 2)
    set(_fail "${_fail}-B used ${_distinct} slot(s) of the ramp, want more than one\n")
endif()
foreach(_pair "quad_map;${m1}" "rock_band;${bd}")
    list(GET _pair 0 _nm)
    list(GET _pair 1 _txt)
    file(WRITE "${WORK}/${_nm}_out.art" "${_txt}")
    execute_process(COMMAND "${SPR2C}" "${WORK}/${_nm}_out.art" "${WORK}/${_nm}_out.c"
        RESULT_VARIABLE _src ERROR_VARIABLE _serr)
    if(NOT _src EQUAL 0)
        set(_fail "${_fail}spr2c rejected ${_nm}: ${_serr}\n")
    endif()
endforeach()

# --- a fully opaque region is never '.', at any threshold -------------------------
# Box edges fall on fractional pixels (64/6, 48/10); -t 255 sums to one ulp low.
foreach(_case "opaque64.png;6x4;opaque64_6x4.art" "opaque48.png;10x4;opaque48_10x4.art")
    list(GET _case 0 _img)
    list(GET _case 1 _size)
    list(GET _case 2 _exp)
    run_tool(t255 -s ${_size} -n o -t 255 "${DATA}/${_img}")
    expect_same("opaque ${_img} -t 255" "${t255}" "${DATA}/${_exp}")
    foreach(_t 0 1 100 110 128 254 255)
        run_tool(tt -s ${_size} -n o -t ${_t} "${DATA}/${_img}")
        expect_same("opaque ${_img} -t ${_t}" "${tt}" "${DATA}/${_exp}")
    endforeach()
endforeach()

# --- no opaque pixel is slot 0 ---------------------------------------------------
foreach(_text "${a}" "${c}" "${d}")
    string(REPLACE "\n" ";" _lines "${_text}")
    list(POP_FRONT _lines _header)
    foreach(_l IN LISTS _lines)
        if(NOT _l STREQUAL "" AND NOT _l MATCHES "^[.1-9A-F]+$")
            set(_fail "${_fail}row '${_l}' has a character outside . and 1-F\n")
        endif()
    endforeach()
endforeach()

# --- spr2c accepts it --------------------------------------------------------------
file(WRITE "${WORK}/icon_out.art" "${a}")
execute_process(COMMAND "${SPR2C}" "${WORK}/icon_out.art" "${WORK}/icon_out.c"
    RESULT_VARIABLE _src ERROR_VARIABLE _serr)
if(NOT _src EQUAL 0)
    set(_fail "${_fail}spr2c rejected png2art's output: ${_serr}\n")
endif()

# --- the built-in palette is VIC::seedPalette()'s ----------------------------------
file(READ "${VIC_CPP}" _vic)
string(REGEX MATCH "kCga\\[kPaletteBytes\\] = \\{([^}]*)\\}" _m "${_vic}")
set(_want "")
string(REGEX MATCHALL "[0-9]+, *[0-9]+, *[0-9]+," _triples "${CMAKE_MATCH_1}")
foreach(_t IN LISTS _triples)
    string(REGEX MATCH "([0-9]+), *([0-9]+), *([0-9]+)," _x "${_t}")
    set(_r "${CMAKE_MATCH_1}")
    set(_g "${CMAKE_MATCH_2}")
    set(_b "${CMAKE_MATCH_3}")
    set(_hex "")
    foreach(_n ${_r} ${_g} ${_b})
        math(EXPR _hi "${_n} / 16")
        math(EXPR _lo "${_n} % 16")
        string(SUBSTRING "0123456789ABCDEF" ${_hi} 1 _H)
        string(SUBSTRING "0123456789ABCDEF" ${_lo} 1 _L)
        string(APPEND _hex "${_H}${_L}")
    endforeach()
    string(APPEND _want "${_hex}\n")
endforeach()
run_tool(p --dump-palette)
list(LENGTH _triples _ntriples)
if(NOT _ntriples EQUAL 16)
    set(_fail "${_fail}found ${_ntriples} colours in VIC.cpp, not 16\n")
elseif(NOT p STREQUAL _want)
    set(_fail "${_fail}built-in palette differs from VIC::seedPalette()\n--- got\n${p}--- want\n${_want}")
endif()

if(_fail)
    message(FATAL_ERROR "${_fail}")
endif()
