# DiskCatalog.cmake - read programs/catalog.txt, the single source of truth for
# everything that can go on an MFC disk.
#
# Adding a program used to mean three edits in three files -- a build script, a copy
# command in the disk target, and a line in diskmap.txt -- and missing one failed
# silently: the program built fine and simply was not on the disk. Now the catalog
# says it once and the build derives the rest, compiling included.
#
# After mfc_read_catalog(<path>):
#
#   MFC_CAT_NAMES              every [section] name, in file order
#   MFC_CAT_<name>_DIR         source directory, relative to the repo root
#   MFC_CAT_<name>_SOURCES     cl65 inputs relative to DIR, or "" for committed content
#   MFC_CAT_<name>_CONFIG      ld65 config relative to DIR, or "" likewise
#   MFC_CAT_<name>_INCLUDE     optional -I directory relative to DIR, or ""
#   MFC_CAT_<name>_DESC        one-line description
#   MFC_CAT_<name>_FILES       list of "SRC|DISKPATH|KIND", KIND in program/data/doc
#
# CONFIG non-empty is what marks an entry as built rather than staged as committed.
#
# A macro rather than a function so the variables land in the caller's scope.

macro(mfc_read_catalog _catalog)
    set(MFC_CAT_NAMES "")
    set(_mfc_cur "")
    file(STRINGS "${_catalog}" _mfc_lines)

    foreach(_mfc_line IN LISTS _mfc_lines)
        string(STRIP "${_mfc_line}" _mfc_line)

        if(_mfc_line STREQUAL "" OR _mfc_line MATCHES "^#")
            # comment or blank

        elseif(_mfc_line MATCHES "^\\[(.+)\\]$")
            set(_mfc_cur "${CMAKE_MATCH_1}")
            list(APPEND MFC_CAT_NAMES "${_mfc_cur}")
            set(MFC_CAT_${_mfc_cur}_FILES "")
            set(MFC_CAT_${_mfc_cur}_SOURCES "")
            set(MFC_CAT_${_mfc_cur}_CONFIG "")
            set(MFC_CAT_${_mfc_cur}_INCLUDE "")
            set(MFC_CAT_${_mfc_cur}_DESC "")
            set(MFC_CAT_${_mfc_cur}_DIR "")

        elseif(_mfc_line MATCHES "^([A-Za-z]+)[ \t]*=[ \t]*(.*)$")
            set(_mfc_key "${CMAKE_MATCH_1}")
            set(_mfc_val "${CMAKE_MATCH_2}")
            if(_mfc_cur STREQUAL "")
                message(FATAL_ERROR "catalog: '${_mfc_key}' appears before any [section]")
            endif()

            if(_mfc_key STREQUAL "dir")
                set(MFC_CAT_${_mfc_cur}_DIR "${_mfc_val}")
            elseif(_mfc_key STREQUAL "sources")
                # Space separated in the file, a CMake list here.
                string(REPLACE " " ";" _mfc_srcs "${_mfc_val}")
                list(REMOVE_ITEM _mfc_srcs "")
                set(MFC_CAT_${_mfc_cur}_SOURCES "${_mfc_srcs}")
            elseif(_mfc_key STREQUAL "config")
                set(MFC_CAT_${_mfc_cur}_CONFIG "${_mfc_val}")
            elseif(_mfc_key STREQUAL "include")
                set(MFC_CAT_${_mfc_cur}_INCLUDE "${_mfc_val}")
            elseif(_mfc_key STREQUAL "desc")
                set(MFC_CAT_${_mfc_cur}_DESC "${_mfc_val}")
            elseif(_mfc_key MATCHES "^(program|data|doc)$")
                # "SOURCE -> DISKPATH". Split on the arrow rather than by regex, so a
                # filename containing '-' cannot be mistaken for the separator.
                string(REPLACE "->" ";" _mfc_parts "${_mfc_val}")
                list(LENGTH _mfc_parts _mfc_n)
                if(NOT _mfc_n EQUAL 2)
                    message(FATAL_ERROR
                        "catalog [${_mfc_cur}]: '${_mfc_key} = ${_mfc_val}' "
                        "is not SOURCE -> DISKPATH")
                endif()
                list(GET _mfc_parts 0 _mfc_src)
                list(GET _mfc_parts 1 _mfc_dst)
                string(STRIP "${_mfc_src}" _mfc_src)
                string(STRIP "${_mfc_dst}" _mfc_dst)
                list(APPEND MFC_CAT_${_mfc_cur}_FILES
                     "${_mfc_src}|${_mfc_dst}|${_mfc_key}")
            else()
                message(FATAL_ERROR "catalog [${_mfc_cur}]: unknown key '${_mfc_key}'")
            endif()

        else()
            message(FATAL_ERROR "catalog: cannot parse line: ${_mfc_line}")
        endif()
    endforeach()

    # Half a recipe is the dangerous state: the entry looks committed, so the disk
    # quietly stages whatever stale file happens to sit in the source tree.
    foreach(_mfc_e IN LISTS MFC_CAT_NAMES)
        if(MFC_CAT_${_mfc_e}_SOURCES AND NOT MFC_CAT_${_mfc_e}_CONFIG)
            message(FATAL_ERROR "catalog [${_mfc_e}]: has 'sources' but no 'config'")
        elseif(MFC_CAT_${_mfc_e}_CONFIG AND NOT MFC_CAT_${_mfc_e}_SOURCES)
            message(FATAL_ERROR "catalog [${_mfc_e}]: has 'config' but no 'sources'")
        elseif(MFC_CAT_${_mfc_e}_INCLUDE AND NOT MFC_CAT_${_mfc_e}_CONFIG)
            message(FATAL_ERROR "catalog [${_mfc_e}]: has 'include' but nothing to build")
        endif()
    endforeach()

    # Editing the catalog has to re-run cmake, or the staging commands and the
    # generated diskmap go stale without anyone noticing.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_catalog}")
endmacro()

# Every buildable program must be in the catalog, or it silently never reaches a
# disk -- the exact failure the catalog exists to prevent. An ld65 config is what
# marks a directory as one: nothing else in programs/ has one, and a program cannot
# be linked without it.
#
# scottfree is exempt and stays exempt: it is a generator, not a program. It takes a
# Scott Adams .dat file and an output name, the .dat files are not ours to ship, and
# its twelve outputs are committed under disk/GAMES with catalog entries of their own.
function(mfc_check_catalog_covers_builds)
    file(GLOB _configs ${CMAKE_SOURCE_DIR}/programs/*/*.cfg)
    foreach(_config IN LISTS _configs)
        get_filename_component(_dir "${_config}" DIRECTORY)
        file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_dir}")
        if(_rel STREQUAL "programs/scottfree")
            continue()
        endif()
        set(_found FALSE)
        foreach(_name IN LISTS MFC_CAT_NAMES)
            if(MFC_CAT_${_name}_DIR STREQUAL _rel)
                set(_found TRUE)
                break()
            endif()
        endforeach()
        if(NOT _found)
            message(FATAL_ERROR
                "${_rel} has an ld65 config but no entry in programs/catalog.txt, so "
                "nothing it builds would reach a disk. Add a [section] for it.")
        endif()
    endforeach()
endfunction()
