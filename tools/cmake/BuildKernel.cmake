# BuildKernel.cmake - Build 6502 kernel ROM from assembly source

# The emulator is useless without the ROMs built here, so a missing cc65 is a
# configuration error by default rather than a warning. Set -DREQUIRE_CC65=OFF
# to configure the host tools alone.
option(REQUIRE_CC65 "Fail configuration if the cc65 toolchain (ca65/ld65) is missing" ON)

# Find cc65 toolchain
find_program(CA65_FOUND ca65)
find_program(LD65_FOUND ld65)
find_program(AR65_FOUND ar65)

if(CA65_FOUND AND LD65_FOUND AND AR65_FOUND)
    message(STATUS "Found cc65 toolchain - will build kernel ROM automatically")
    
    # Create kernel build directory in build tree
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/kernel)
    
    # Define source and config paths
    set(KERNEL_ASM_SOURCE ${CMAKE_SOURCE_DIR}/src/kernel/kernel.asm)
    set(KERNEL_CONFIG ${CMAKE_SOURCE_DIR}/src/kernel/memory.cfg)
    
    # Define build outputs in build directory
    set(KERNEL_OBJECT ${CMAKE_BINARY_DIR}/kernel/kernel.o)
    set(KERNEL_ROM ${CMAKE_BINARY_DIR}/kernel/kernel.rom)
    set(KERNEL_MAP ${CMAKE_BINARY_DIR}/kernel/kernel.map)
    
    # Create a target that builds the kernel ROM in build directory
    add_custom_target(kernel_rom ALL
        COMMAND ca65 ${KERNEL_ASM_SOURCE} -I ${CMAKE_SOURCE_DIR}/src/kernel -o ${KERNEL_OBJECT}
        COMMAND ld65 -C ${KERNEL_CONFIG} ${KERNEL_OBJECT} -o ${KERNEL_ROM} -m ${KERNEL_MAP}
        COMMAND ${CMAKE_COMMAND} -E echo "================================================================"
        COMMAND ${CMAKE_COMMAND} -E echo "ROM BUILD COMPLETE - SIZE ANALYSIS"
        COMMAND ${CMAKE_COMMAND} -E echo "================================================================"
        COMMAND ${CMAKE_COMMAND} -DROM_FILE=${KERNEL_ROM} -DMAP_FILE=${KERNEL_MAP} -P ${CMAKE_SOURCE_DIR}/tools/cmake/rom_size.cmake
        COMMAND ${CMAKE_COMMAND} -E echo "================================================================"
        COMMENT "Building kernel ROM in build directory"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${KERNEL_ASM_SOURCE} ${KERNEL_CONFIG}
                ${CMAKE_SOURCE_DIR}/src/kernel/kernel_vars.inc
        VERBATIM
    )
    
    # Make the main executable depend on kernel ROM
    add_dependencies(6502-kernel kernel_rom)

    # ================================================================
    # BASIC ROM Build Target
    # ================================================================

    # Define BASIC source and config paths
    set(BASIC_ASM_SOURCE ${CMAKE_SOURCE_DIR}/src/kernel/basic.asm)
    set(BASIC_CONFIG ${CMAKE_SOURCE_DIR}/src/kernel/basic_memory.cfg)

    # Define BASIC build outputs
    set(BASIC_OBJECT ${CMAKE_BINARY_DIR}/kernel/basic.o)
    set(BASIC_ROM ${CMAKE_BINARY_DIR}/kernel/basic.rom)
    set(BASIC_MAP ${CMAKE_BINARY_DIR}/kernel/basic.map)
    set(BASIC_LST ${CMAKE_BINARY_DIR}/kernel/basic.lst)

    # Create BASIC ROM build target
    add_custom_target(basic_rom ALL
        COMMAND ca65 ${BASIC_ASM_SOURCE} -o ${BASIC_OBJECT} --listing ${BASIC_LST}
        COMMAND ld65 -C ${BASIC_CONFIG} ${BASIC_OBJECT} -o ${BASIC_ROM} -m ${BASIC_MAP}
        COMMAND ${CMAKE_COMMAND} -E echo "================================================================"
        COMMAND ${CMAKE_COMMAND} -E echo "BASIC ROM BUILD COMPLETE"
        COMMAND ${CMAKE_COMMAND} -E echo "================================================================"
        COMMAND ${CMAKE_COMMAND} -DBASIC_ROM_FILE=${BASIC_ROM} -P ${CMAKE_SOURCE_DIR}/tools/cmake/check_basic_size.cmake
        COMMENT "Building BASIC ROM"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${BASIC_ASM_SOURCE} ${BASIC_CONFIG}
        VERBATIM
    )

    # ================================================================
    # ================================================================

    # Monitor module ROM (bank 4). The monitor moved out of kernel ROM: as a disk
    # program it would load at $0800 and overwrite the code it exists to debug.
    # -I src/kernel so .include "kernel_vars.inc" resolves.
    set(MONITOR_ASM_SOURCE ${CMAKE_SOURCE_DIR}/src/kernel/monitor.asm)
    set(MONITOR_CONFIG ${CMAKE_SOURCE_DIR}/src/kernel/monitor_memory.cfg)
    set(MONITOR_OBJECT ${CMAKE_BINARY_DIR}/kernel/monitor.o)
    set(MONITOR_ROM ${CMAKE_BINARY_DIR}/kernel/monitor.rom)
    set(MONITOR_MAP ${CMAKE_BINARY_DIR}/kernel/monitor.map)

    add_custom_target(monitor_rom ALL
        COMMAND ca65 ${MONITOR_ASM_SOURCE} -I ${CMAKE_SOURCE_DIR}/src/kernel -I ${CMAKE_SOURCE_DIR}/src/kernel/assembler -o ${MONITOR_OBJECT}
        COMMAND ld65 -C ${MONITOR_CONFIG} ${MONITOR_OBJECT} -o ${MONITOR_ROM} -m ${MONITOR_MAP}
        COMMENT "Building monitor module ROM (bank 4)"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${MONITOR_ASM_SOURCE} ${MONITOR_CONFIG}
                ${CMAKE_SOURCE_DIR}/src/kernel/kernel_vars.inc
                ${CMAKE_SOURCE_DIR}/src/kernel/assembler/assembler.inc
                ${CMAKE_SOURCE_DIR}/src/kernel/assembler/opcodes_65c02.inc
        VERBATIM
    )

    set(FORTH_DIR ${CMAKE_SOURCE_DIR}/src/kernel/forth)
    set(FORTH_ASM_SOURCE ${FORTH_DIR}/forth.s)
    set(FORTH_CONFIG ${FORTH_DIR}/forth_memory.cfg)

    set(FORTH_OBJECT ${CMAKE_BINARY_DIR}/kernel/forth.o)
    set(FORTH_ROM ${CMAKE_BINARY_DIR}/kernel/forth.rom)
    set(FORTH_MAP ${CMAKE_BINARY_DIR}/kernel/forth.map)

    add_custom_target(forth_rom ALL
        COMMAND ca65 ${FORTH_ASM_SOURCE} -o ${FORTH_OBJECT}
        COMMAND ld65 -C ${FORTH_CONFIG} ${FORTH_OBJECT} -o ${FORTH_ROM} -m ${FORTH_MAP}
        COMMAND ${CMAKE_COMMAND} -E echo "FORTH module ROM built (bank 3)"
        COMMENT "Building FORTH module ROM"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${FORTH_ASM_SOURCE} ${FORTH_CONFIG}
        VERBATIM
    )

    # ================================================================
    # XMODEM spike blob (serial/6551 ACIA proof; not a kernel module)
    # ================================================================
    # Daryl Rictor's XMODEM/CRC (vendor/xmodem), retargeted to the $FE29 ACIA and
    # relocated to $2000. A flat binary the headless ACIA test loads into RAM.
    # Assembled with --ignore-case (the original mixes label case).

    set(XMODEM_DIR ${CMAKE_SOURCE_DIR}/vendor/xmodem)
    set(XMODEM_ASM_SOURCE ${XMODEM_DIR}/xmodem.s)
    set(XMODEM_CONFIG ${XMODEM_DIR}/xmodem.cfg)

    set(XMODEM_OBJECT ${CMAKE_BINARY_DIR}/kernel/xmodem.o)
    set(XMODEM_BIN ${CMAKE_BINARY_DIR}/kernel/xmodem.bin)
    set(XMODEM_MAP ${CMAKE_BINARY_DIR}/kernel/xmodem.map)

    add_custom_target(xmodem_bin ALL
        COMMAND ca65 --ignore-case ${XMODEM_ASM_SOURCE} -o ${XMODEM_OBJECT}
        COMMAND ld65 -C ${XMODEM_CONFIG} ${XMODEM_OBJECT} -o ${XMODEM_BIN} -m ${XMODEM_MAP}
        COMMAND ${CMAKE_COMMAND} -E echo "XMODEM spike blob built ($2000)"
        COMMENT "Building XMODEM spike blob"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${XMODEM_ASM_SOURCE} ${XMODEM_CONFIG}
        VERBATIM
    )

    # ================================================================
    # TERM serial-terminal blob (for the headless ANSI test)
    # ================================================================
    # The serial ANSI terminal (programs/term), built with cl65 (C + glue) as a
    # flat $0800 image and staged in the kernel build dir so the headless ANSI
    # test loads it at ../kernel/term.bin. (TERM.PRG for the disk is produced by
    # the catalog build.)
    find_program(CL65_FOUND cl65)
    if(CL65_FOUND)
        # ------------------------------------------------------------------
        # Raw $0800 test blobs for the headless program tests
        # ------------------------------------------------------------------
        # The tests load a flat image from ../kernel/<name>.bin; the .PRG that
        # ships on the disk is a separate product of the catalog build
        # (Programs.cmake).
        #
        # Every source is compiled to its own object under the build tree, by
        # mfc_cc65_object(); see Cc65Compile.cmake for why the compile is two
        # stages rather than one cl65 call.
        #
        # mfc_add_test_blob(<name>
        #     DIR      <source directory>
        #     CONFIG   <ld65 config, relative to DIR>
        #     SOURCES  <source...>          (relative to DIR, or absolute)
        #     [INCLUDE <dir>]               (added as -I)
        #     [LABELS]                      (emit <name>.lbl for the test harness)
        #     [DEPENDS <extra file...>]     (headers the depfile cannot cover)
        #     [MESSAGE <text>])
        function(mfc_add_test_blob name)
            cmake_parse_arguments(B "LABELS" "DIR;CONFIG;INCLUDE;MESSAGE" "SOURCES;DEPENDS" ${ARGN})
            set(_out ${CMAKE_BINARY_DIR}/kernel/${name}.bin)
            set(_objdir ${CMAKE_BINARY_DIR}/kernel/blobs/${name})

            set(_objs "")
            set(_seen "")
            foreach(_src IN LISTS B_SOURCES)
                # A source may be named relative to DIR, or absolutely, as the
                # shared programs/common sources are.
                if(IS_ABSOLUTE ${_src})
                    set(_path ${_src})
                else()
                    set(_path ${B_DIR}/${_src})
                endif()
                get_filename_component(_base ${_path} NAME_WE)
                # Two sources with the same stem would compile to the same object
                # and the second would silently overwrite the first.
                if(_base IN_LIST _seen)
                    message(FATAL_ERROR
                        "blob ${name}: two sources are both named '${_base}', so "
                        "one object file would overwrite the other. Rename one.")
                endif()
                list(APPEND _seen ${_base})

                set(_obj ${_objdir}/${_base}.o)
                mfc_cc65_object(${_obj}
                    SOURCE  ${_path}
                    INCLUDE ${B_INCLUDE}
                    DEPENDS ${B_DEPENDS}
                )
                list(APPEND _objs ${_obj})
            endforeach()

            if(B_LABELS)
                set(_labels -Ln ${CMAKE_BINARY_DIR}/kernel/${name}.lbl)
            else()
                set(_labels "")
            endif()

            add_custom_command(
                OUTPUT ${_out}
                # cl65, not bare ld65: the link needs the cc65 runtime library
                # for the target, and cl65 is what supplies none.lib. libmfcglue
                # goes last, for the reason given in GlueLibrary.cmake.
                COMMAND cl65 -t none -C ${B_DIR}/${B_CONFIG} ${_objs} ${MFC_GLUE_LIB}
                        -o ${_out} ${_labels}
                COMMAND ${CMAKE_COMMAND} -E echo "${B_MESSAGE}"
                DEPENDS ${_objs} ${B_DIR}/${B_CONFIG} ${MFC_GLUE_LIB}
                COMMENT "ld65 kernel/${name}.bin"
                VERBATIM
            )
            add_custom_target(${name}_bin ALL DEPENDS ${_out})
        endfunction()

        set(COMMON_DIR ${CMAKE_SOURCE_DIR}/programs/common)

        # TERM, the serial ANSI terminal, for the headless ANSI test.
        mfc_add_test_blob(term
            DIR      ${CMAKE_SOURCE_DIR}/programs/term
            CONFIG   term.cfg
            SOURCES  term.c ${COMMON_DIR}/scrollback.c
            INCLUDE  ${COMMON_DIR}
            DEPENDS  ${COMMON_DIR}/scrollback.h
            MESSAGE  "TERM terminal blob built ($0800)"
        )

        # IRC, the chat client, same toolchain as TERM.
        mfc_add_test_blob(irc
            DIR      ${CMAKE_SOURCE_DIR}/programs/irc
            CONFIG   irc.cfg
            SOURCES  irc.c ${COMMON_DIR}/scrollback.c
            INCLUDE  ${COMMON_DIR}
            DEPENDS  ${COMMON_DIR}/scrollback.h
            MESSAGE  "IRC chat-client blob built ($0800)"
        )

        # VENTURE. LABELS emits the label file the test harness reads the game's
        # own coordinates from, instead of inferring them from the screen.
        mfc_add_test_blob(venture
            DIR      ${CMAKE_SOURCE_DIR}/programs/venture
            CONFIG   venture.cfg
            SOURCES  venture.c
            LABELS
            DEPENDS  ${CMAKE_SOURCE_DIR}/programs/venture/venture.h
            MESSAGE  "VENTURE blob built ($0800)"
        )

        # KERNEL PANIC. Same shape as VENTURE, including the labels: steps 7-8 are
        # juice and balance, and the only way to hold a scroller's simulation still
        # while judging either is to read its own state by name.
        mfc_add_test_blob(kpanic
            DIR      ${CMAKE_SOURCE_DIR}/programs/kpanic
            CONFIG   kpanic.cfg
            SOURCES  kpanic.c
            LABELS
            DEPENDS  ${CMAKE_SOURCE_DIR}/programs/kpanic/kpanic.h
            MESSAGE  "KERNEL PANIC blob built ($0800)"
        )

        # GOPHER, the Gopher document browser.
        mfc_add_test_blob(gopher
            DIR      ${CMAKE_SOURCE_DIR}/programs/gopher
            CONFIG   gopher.cfg
            SOURCES  gopher.c glue.s
            MESSAGE  "GOPHER blob built ($0800)"
        )

        # EDIT.
        mfc_add_test_blob(edit
            DIR      ${CMAKE_SOURCE_DIR}/programs/edit
            CONFIG   edit.cfg
            SOURCES  edit.c
            MESSAGE  "EDIT blob built ($0800)"
        )
    else()
        message(STATUS "cl65 not found - skipping TERM/IRC/VENTURE/EDIT blobs")
    endif()

    # ================================================================
    # MFC-DOS Resident ROM Build Target ($8800-$AFFF, always mapped)
    # ================================================================

    set(DOS_DIR ${CMAKE_SOURCE_DIR}/src/kernel/dos)
    set(DOS_ASM_SOURCE ${DOS_DIR}/dos.asm)
    set(DOS_CONFIG ${DOS_DIR}/dos_memory.cfg)

    set(DOS_OBJECT ${CMAKE_BINARY_DIR}/kernel/dos.o)
    set(DOS_ROM ${CMAKE_BINARY_DIR}/kernel/dos.rom)
    set(DOS_MAP ${CMAKE_BINARY_DIR}/kernel/dos.map)

    add_custom_target(dos_rom ALL
        COMMAND ca65 ${DOS_ASM_SOURCE} -I ${DOS_DIR} -o ${DOS_OBJECT}
        COMMAND ld65 -C ${DOS_CONFIG} ${DOS_OBJECT} -o ${DOS_ROM} -m ${DOS_MAP}
        COMMAND ${CMAKE_COMMAND} -E echo "MFC-DOS resident ROM built ($8800-$AFFF)"
        COMMENT "Building MFC-DOS resident ROM"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${DOS_ASM_SOURCE} ${DOS_CONFIG}
        VERBATIM
    )

else()
    # No ROMs means no machine: 6502-kernel would still compile and link, then
    # abort at startup with "Could not open kernel.rom". Fail here instead.
    set(_cc65_help
"cc65 toolchain not found (ca65, ld65 and ar65 are all required).
The emulator cannot run without the ROMs cc65 builds -- 6502-kernel would
compile and then abort at startup on a missing kernel.rom.
Install it:
  Debian/Ubuntu/Mint   sudo apt install cc65
  Fedora               sudo dnf install cc65
  Arch                 sudo pacman -S cc65
  macOS                brew install cc65
To configure the host tools anyway (no runnable emulator):
  cmake -DREQUIRE_CC65=OFF ...")
    if(REQUIRE_CC65)
        message(FATAL_ERROR ${_cc65_help})
    else()
        message(WARNING ${_cc65_help})
    endif()
endif()
