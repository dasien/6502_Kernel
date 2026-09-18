# Cc65Compile.cmake - compile one cc65 source to an object at an explicit path.
#
# Why this exists rather than a plain `cl65 -c -o out.o in.c`.
#
# cl65 in compile-and-assemble mode writes its intermediate assembly next to the
# INPUT, as <source-dir>/<base>.s, and -o redirects only the final object. Two
# build steps that compile the same C file therefore write, assemble and delete
# the same .s, and there is nothing to order them. programs/common/scrollback.c
# is compiled by both TERM and IRC, twice over: once for each test blob in
# BuildKernel.cmake and once for each .PRG in Programs.cmake. Run in parallel
# they raced, and the failures were intermittent and unhelpful --
# "Cannot open input file .../scrollback.s" when one step deleted the file the
# other was about to assemble, or "ld65: Read error at position 8192
# (file corrupt?)" when a linker read an object another step was still writing.
#
# cl65 has no flag for the intermediate's location, so the fix is to stop using
# its one-shot mode for C and drive the two stages directly: cc65 emits assembly
# where we ask, and ca65 assembles it to the object we ask for. Nothing lands in
# the source tree and no two steps share a path. Assembly sources go straight to
# ca65, which has no intermediate to collide over.
#
# The flags every 6502 program shares are applied here, in one place.
# --signed-chars is NOT optional: cc65 defaults to unsigned char and several of
# these ports (micro-Max most visibly) assume signed, failing silently if it is
# dropped. -t none is the bare 6502 target -- the .cfg supplies the layout at
# link time.
#
# mfc_cc65_object(<out-object> SOURCE <file> [INCLUDE <dir>] [DEPENDS <file...>])
#
# Declares the rule; the caller depends on <out-object>. Callers must ensure two
# sources never map to the same object path.
function(mfc_cc65_object obj)
    cmake_parse_arguments(C "" "SOURCE;INCLUDE" "DEPENDS" ${ARGN})

    get_filename_component(_ext ${C_SOURCE} EXT)
    get_filename_component(_dir ${obj} DIRECTORY)
    get_filename_component(_base ${obj} NAME_WE)
    file(MAKE_DIRECTORY ${_dir})

    # The same source is built more than once -- scrollback.c four times over --
    # so the progress line names the object, not the source, or the four steps
    # are indistinguishable in the build log.
    file(RELATIVE_PATH _label ${CMAKE_BINARY_DIR} ${obj})

    if(C_INCLUDE)
        set(_inc -I ${C_INCLUDE})
    else()
        set(_inc "")
    endif()

    # Header tracking needs a generator that understands DEPFILE. Ninja and
    # modern Make do; Visual Studio and Xcode do not, and asking them errors at
    # generate time -- so there the caller's explicit DEPENDS carries it.
    set(_depfile_arg "")
    set(_dep_cmd "")
    if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set(_dep ${_dir}/${_base}.d)
        set(_depfile_arg DEPFILE ${_dep})
        set(_dep_cmd --create-dep ${_dep})
    endif()

    if(_ext STREQUAL ".c")
        set(_asm ${_dir}/${_base}.s)
        add_custom_command(
            OUTPUT ${obj}
            COMMAND cc65 -t none --signed-chars -O ${_inc} ${_dep_cmd}
                    -o ${_asm} ${C_SOURCE}
            COMMAND ca65 -t none -o ${obj} ${_asm}
            DEPENDS ${C_SOURCE} ${C_DEPENDS}
            ${_depfile_arg}
            COMMENT "cc65 ${_label}"
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT ${obj}
            COMMAND ca65 -t none -o ${obj} ${C_SOURCE}
            DEPENDS ${C_SOURCE} ${C_DEPENDS}
            COMMENT "ca65 ${_label}"
            VERBATIM
        )
    endif()
endfunction()
