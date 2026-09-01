# Programs.cmake - compile and link the disk programs declared in
# programs/catalog.txt.
#
# This used to be nine near-identical shell scripts, one per program, executed by
# the disk target. They were the only thing in the build that needed a POSIX shell,
# and because each was a single opaque command the build could not tell a stale
# program from a fresh one -- `ninja disk` recompiled all nine every time and a
# changed header rebuilt nothing at all.
#
# Now each source is its own compile step with a real depfile, so touching
# venture.h rebuilds venture.o and nothing else, and the whole thing runs anywhere
# CMake and cc65 do.
#
# Requires mfc_read_catalog() to have run. Call mfc_add_catalog_programs() once; it
# defines a <entry>_prg target per built entry and returns their names.

# Flags every disk program shares.
#
# --signed-chars is NOT optional: cc65 defaults to unsigned char and several of
# these ports (micro-Max most visibly) assume signed, failing silently if it is
# dropped. -t none is the bare 6502 target -- the .cfg supplies the layout.
set(MFC_PRG_CFLAGS -t none --signed-chars -O)

# Read the load address out of an ld65 config's STARTADDRESS default.
#
# Taking it from the config rather than hardcoding $0800 is the point: the two-byte
# header and the link have to agree, and a .PRG whose header disagrees loads to the
# wrong place and crashes on launch with nothing to show why.
function(_mfc_load_address config out_var)
    file(READ "${config}" _text)
    if(NOT _text MATCHES "STARTADDRESS:[ \t]*default[ \t]*=[ \t]*\\$([0-9A-Fa-f]+)")
        message(FATAL_ERROR
            "${config} has no 'STARTADDRESS: default = $xxxx', so the .PRG load "
            "header cannot be derived from it.")
    endif()
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

# The .PRG name is the entry's `program` source: EDIT.PRG, CHESS.PRG, and so on.
function(_mfc_program_filename entry out_var)
    foreach(_spec IN LISTS MFC_CAT_${entry}_FILES)
        string(REPLACE "|" ";" _parts "${_spec}")
        list(GET _parts 2 _kind)
        if(_kind STREQUAL "program")
            list(GET _parts 0 _src)
            set(${out_var} "${_src}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "catalog [${entry}]: has a build recipe but no 'program' line")
endfunction()

function(mfc_add_catalog_program entry out_target)
    set(_dir    "${MFC_CAT_${entry}_DIR}")
    set(_srcdir "${CMAKE_SOURCE_DIR}/${_dir}")
    set(_outdir "${CMAKE_BINARY_DIR}/${_dir}")
    set(_config "${_srcdir}/${MFC_CAT_${entry}_CONFIG}")

    if(NOT EXISTS "${_config}")
        message(FATAL_ERROR "catalog [${entry}]: config '${_config}' does not exist")
    endif()
    # Relinking on a changed layout is not enough -- the load address is read at
    # configure time, so editing the config has to re-run cmake.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_config}")
    _mfc_load_address("${_config}" _load)
    _mfc_program_filename("${entry}" _prgname)

    # cc65 writes objects next to their sources unless told otherwise, which would
    # put build output in the source tree and let the test blobs (BuildKernel.cmake
    # compiles some of the same sources) race us for the same file. Give every
    # object an explicit home in the build tree instead.
    file(MAKE_DIRECTORY "${_outdir}")

    if(MFC_CAT_${entry}_INCLUDE)
        set(_inc -I "${_srcdir}/${MFC_CAT_${entry}_INCLUDE}")
    else()
        set(_inc "")
    endif()

    # Header tracking needs a generator that understands DEPFILE. Ninja and modern
    # Make do; Visual Studio and Xcode do not, and asking them errors at generate
    # time -- so on those the programs simply rebuild on a source change, as they
    # did under the shell scripts.
    set(_depfiles_work FALSE)
    if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set(_depfiles_work TRUE)
    endif()

    set(_objs "")
    set(_seen "")
    foreach(_src IN LISTS MFC_CAT_${entry}_SOURCES)
        get_filename_component(_base "${_src}" NAME_WE)
        # Two sources with the same stem would compile to the same object and the
        # second would silently overwrite the first.
        if(_base IN_LIST _seen)
            message(FATAL_ERROR
                "catalog [${entry}]: two sources are both named '${_base}', so one "
                "object file would overwrite the other. Rename one.")
        endif()
        list(APPEND _seen "${_base}")

        set(_obj "${_outdir}/${_base}.o")
        set(_dep "${_outdir}/${_base}.d")
        # For the progress line only: "programs/irc/../common/scrollback.c" is
        # accurate and unreadable.
        get_filename_component(_abs "${_srcdir}/${_src}" ABSOLUTE)
        file(RELATIVE_PATH _label "${CMAKE_SOURCE_DIR}" "${_abs}")
        if(_depfiles_work)
            add_custom_command(
                OUTPUT ${_obj}
                COMMAND cl65 ${MFC_PRG_CFLAGS} ${_inc}
                        -c -o ${_obj} --create-dep ${_dep} ${_srcdir}/${_src}
                DEPENDS ${_srcdir}/${_src}
                DEPFILE ${_dep}
                COMMENT "cc65 ${_label}"
                VERBATIM
            )
        else()
            add_custom_command(
                OUTPUT ${_obj}
                COMMAND cl65 ${MFC_PRG_CFLAGS} ${_inc} -c -o ${_obj} ${_srcdir}/${_src}
                DEPENDS ${_srcdir}/${_src}
                COMMENT "cc65 ${_label}"
                VERBATIM
            )
        endif()
        list(APPEND _objs ${_obj})
    endforeach()

    set(_bin "${_outdir}/${entry}.bin")
    add_custom_command(
        OUTPUT ${_bin}
        COMMAND cl65 -t none -C ${_config} ${_objs} -o ${_bin}
        DEPENDS ${_objs} ${_config}
        COMMENT "ld65 ${_dir}/${entry}.bin"
        VERBATIM
    )

    set(_prg "${_outdir}/${_prgname}")
    add_custom_command(
        OUTPUT ${_prg}
        COMMAND mkprg ${_load} ${_bin} ${_prg}
        DEPENDS ${_bin} mkprg
        COMMENT "PRG ${_dir}/${_prgname}"
        VERBATIM
    )

    add_custom_target(${entry}_prg DEPENDS ${_prg})
    set(${out_target} ${entry}_prg PARENT_SCOPE)
endfunction()

# Define a target for every catalog entry carrying a build recipe.
function(mfc_add_catalog_programs out_targets)
    set(_targets "")
    foreach(_entry IN LISTS MFC_CAT_NAMES)
        if(MFC_CAT_${_entry}_CONFIG)
            mfc_add_catalog_program(${_entry} _t)
            list(APPEND _targets ${_t})
        endif()
    endforeach()
    set(${out_targets} "${_targets}" PARENT_SCOPE)
endfunction()
