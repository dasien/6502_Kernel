# spr2c turns sprite and glyph art into the bytes three programs' pictures are made
# of, so a fault in it would scramble every one of them at once, silently. This pins
# the byte layout on small pictures whose bytes can be worked out by hand, and pins
# that bad art stops the build rather than drawing wrong.
#
#   - a 4x2 sprite: row-major, left pixel in the high nibble;
#   - a 32x16 sprite: two slots, the LEFT slot's 128 bytes first -- the order the
#     chip composes a wide sprite in, so a whole picture streams into pattern RAM;
#   - an 8x2 glyph, one of whose rows starts with '#', which is ink, not a comment;
#   - a short row, a stray character and a missing row each fail, naming the line.

cmake_minimum_required(VERSION 3.20)
if(NOT SPR2C OR NOT WORK)
    message(FATAL_ERROR "SPR2C and WORK must be specified")
endif()
file(MAKE_DIRECTORY "${WORK}")

set(_fail "")

# --- a picture that should work ----------------------------------------------
set(_row16a "1...............")         # slot 0: pixel 0 = 1
set(_row16b "...............F")         # slot 1: pixel 15 = F
set(_blank  "................")
set(_wide "")
foreach(_i RANGE 15)
    if(_i EQUAL 0)
        string(APPEND _wide "${_row16a}${_row16b}\n")
    else()
        string(APPEND _wide "${_blank}${_blank}\n")
    endif()
endforeach()
file(WRITE "${WORK}/good.art"
"# a comment between pictures
@sprite tiny 4x2
.1F.
B..3

@sprite wide 32x16
${_wide}
@glyph g 8x2
#......#
.######.
")
execute_process(COMMAND "${SPR2C}" "${WORK}/good.art" "${WORK}/good.c"
                RESULT_VARIABLE _rc ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    list(APPEND _fail "a valid .art file was rejected: ${_err}")
else()
    # Every byte, in order: tiny's 4, wide's 256, the glyph's 2.
    file(READ "${WORK}/good.c" _c)
    string(REGEX MATCHALL "0x[0-9A-F][0-9A-F]" _b "${_c}")
    list(LENGTH _b _n)
    if(NOT _n EQUAL 262)
        list(APPEND _fail "expected 262 bytes, found ${_n}")
    else()
        list(SUBLIST _b 0 4 _tiny)
        if(NOT _tiny STREQUAL "0x01;0xF0;0xB0;0x03")
            list(APPEND _fail "the 4x2 sprite packed as ${_tiny}, not 01 F0 B0 03")
        endif()
        # wide starts at 4. Byte 0 of the LEFT slot holds pixel 0 = 1; byte 128 --
        # the right slot's first -- is pixels 16-17, clear; byte 135 is pixel 31 = F.
        list(GET _b 4 _w0)
        list(GET _b 132 _w128)
        list(GET _b 139 _w135)
        if(NOT _w0 STREQUAL "0x10")
            list(APPEND _fail "the 32x16 sprite starts ${_w0}, not its left slot's 0x10")
        endif()
        if(NOT _w128 STREQUAL "0x00" OR NOT _w135 STREQUAL "0x0F")
            list(APPEND _fail "the right slot is not after the left one (byte 128 ${_w128}, 135 ${_w135})")
        endif()
        list(SUBLIST _b 260 2 _g)
        if(NOT _g STREQUAL "0x81;0x7E")
            list(APPEND _fail "the glyph packed as ${_g}, not 81 7E (a row starting '#' is ink)")
        endif()
    endif()
endif()

# --- pictures that must stop the build ---------------------------------------
function(expect_rejected label text needle)
    file(WRITE "${WORK}/${label}.art" "${text}")
    execute_process(COMMAND "${SPR2C}" "${WORK}/${label}.art" "${WORK}/${label}.c"
                    RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(_rc EQUAL 0)
        set(_fail "${_fail};${label}: accepted, should have failed" PARENT_SCOPE)
    elseif(NOT _err MATCHES "${needle}")
        set(_fail "${_fail};${label}: failed, but said '${_err}'" PARENT_SCOPE)
    endif()
    if(EXISTS "${WORK}/${label}.c" OR EXISTS "${WORK}/${label}.c.tmp")
        set(_fail "${_fail};${label}: left an output file behind" PARENT_SCOPE)
    endif()
endfunction()

expect_rejected(short_row "@sprite a 4x2\n.1F\n....\n"        "short_row.art:2: wrong row width")
expect_rejected(bad_char  "@sprite a 4x1\n.1G.\n"             "bad_char.art:2: a sprite row")
expect_rejected(too_few   "@sprite a 4x3\n....\n@glyph b 8x1\n........\n" "too_few.art:3: picture ends early")
expect_rejected(twice     "@glyph a 8x1\n........\n@glyph a 8x1\n........\n" "name used twice")

list(REMOVE_ITEM _fail "")
if(_fail)
    string(REPLACE ";" "\n  " _report "${_fail}")
    message(FATAL_ERROR "spr2c:\n  ${_report}")
endif()
message(STATUS "spr2c: byte layout and rejections as specified")
