# Cc65Compile.cmake - compile one cc65 source to an object at an explicit path.
#
# C is driven as two stages, cc65 then ca65, rather than through `cl65 -c`.
# cl65's compile-and-assemble mode writes its intermediate assembly next to the
# INPUT, as <source-dir>/<base>.s, and -o redirects only the final object; it has
# no flag for the intermediate. A source compiled by more than one build step
# therefore has several steps writing, assembling and deleting one shared .s,
# with nothing to order them. programs/common/scrollback.c is one: TERM and IRC
# each build it, for a test blob in BuildKernel.cmake and again for a .PRG in
# Programs.cmake. Driving the stages directly puts every intermediate where we
# ask, so no two steps share a path and nothing lands in the source tree.
# Assembly sources go straight to ca65, which has no intermediate at all.
#
# The flags every program shares are applied here, in one place.
#
# --cpu 65C02 matters more than it looks. The machine's CPU is a WDC W65C02S and
# the kernel, monitor and DOS all say so with a .PC02 directive, but the program
# toolchain said nothing -- so every .PRG was built for a plain 6502 and gave up
# the CMOS additions. cc65 emits "stz _g" for a 65C02 where it needs
# "lda #$00 / sta _g" for a 6502, and the assembly glue could not use bra, stz,
# phx/phy or zero-page indirect at all.
#
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
            COMMAND cc65 -t none --cpu 65C02 --signed-chars -O ${_inc} ${_dep_cmd}
                    -o ${_asm} ${C_SOURCE}
            COMMAND ca65 -t none --cpu 65C02 -o ${obj} ${_asm}
            DEPENDS ${C_SOURCE} ${C_DEPENDS}
            ${_depfile_arg}
            COMMENT "cc65 ${_label}"
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT ${obj}
            COMMAND ca65 -t none --cpu 65C02 -o ${obj} ${C_SOURCE}
            DEPENDS ${C_SOURCE} ${C_DEPENDS}
            COMMENT "ca65 ${_label}"
            VERBATIM
        )
    endif()
endfunction()
