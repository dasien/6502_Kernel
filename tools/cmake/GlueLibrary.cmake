# GlueLibrary.cmake - build libmfcglue.lib, the shared cc65 runtime glue.
#
# Every cc65 program needs the same bridge between C and the machine: the
# kernel and DOS ABI calls, the VIC register port, the ACIA. That bridge used
# to be copied into each programs/<name>/glue.s -- 127 duplicate .proc bodies
# across ten files, so a fix to dclose() had to be made seven times.
#
# It is an ar65 LIBRARY rather than an object every program links, because ld65
# treats the two differently. An object named on the command line is linked
# whole; a library member is pulled only when it resolves a symbol that is
# still undefined. With the library, EDIT carries the file calls it makes and
# not the ACIA driver it does not -- which matters when FRONTIER already uses
# 26 KB of the 30 KB a .PRG gets.
#
# Granularity is per member, so the modules are split by subsystem: a program
# touching the serial port uses all four ACIA calls, and one touching none
# pulls nothing. Worst case is a program using five of vic.s's twelve, about
# 40 wasted bytes.
#
# A program can override a library routine by defining the symbol in its own
# glue.s: that satisfies the import, so the member is never pulled. But the
# unit of pulling is the MODULE, so an overridable symbol has to sit alone in
# one. Fold _QUITDOS in beside _OUTCH and a program overriding _QUITDOS while
# calling _OUTCH drags in the module anyway and the link fails on the
# duplicate. That is why inch.s and quitdos.s are modules of one proc each.

set(MFC_GLUE_DIR ${CMAKE_SOURCE_DIR}/programs/common/glue)
set(MFC_GLUE_LIB ${CMAKE_BINARY_DIR}/lib/libmfcglue.lib
    CACHE INTERNAL "shared cc65 glue, linked by every program")

set(_glue_modules kernel console inch quitdos dir
                  vic_write vic_read vic_cursor vic_cmd font sprite palette
                  file acia rng rngseed rtc pia sound)

set(_glue_objs "")
foreach(_m IN LISTS _glue_modules)
    set(_obj ${CMAKE_BINARY_DIR}/lib/glue/${_m}.o)
    mfc_cc65_object(${_obj} SOURCE ${MFC_GLUE_DIR}/${_m}.s)
    list(APPEND _glue_objs ${_obj})
endforeach()

# ar65 appends, so a stale archive would accumulate old members forever.
add_custom_command(
    OUTPUT ${MFC_GLUE_LIB}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${MFC_GLUE_LIB}
    COMMAND ar65 a ${MFC_GLUE_LIB} ${_glue_objs}
    DEPENDS ${_glue_objs}
    COMMENT "ar65 lib/libmfcglue.lib"
    VERBATIM
)
add_custom_target(mfcglue DEPENDS ${MFC_GLUE_LIB})
