# BuildKernel.cmake - Build 6502 kernel ROM from assembly source

# The emulator is useless without the ROMs built here, so a missing cc65 is a
# configuration error by default rather than a warning. Set -DREQUIRE_CC65=OFF
# to configure the host tools alone.
option(REQUIRE_CC65 "Fail configuration if the cc65 toolchain (ca65/ld65) is missing" ON)

# Find cc65 toolchain
find_program(CA65_FOUND ca65)
find_program(LD65_FOUND ld65)

if(CA65_FOUND AND LD65_FOUND)
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
    # programs/term/build.sh.)
    find_program(CL65_FOUND cl65)
    if(CL65_FOUND)
        set(TERM_DIR ${CMAKE_SOURCE_DIR}/programs/term)
        set(COMMON_DIR ${CMAKE_SOURCE_DIR}/programs/common)
        set(TERM_BIN ${CMAKE_BINARY_DIR}/kernel/term.bin)
        add_custom_target(term_bin ALL
            COMMAND cl65 -t none --signed-chars -O -I ${COMMON_DIR} -C ${TERM_DIR}/term.cfg
                    ${TERM_DIR}/term.c ${COMMON_DIR}/scrollback.c ${TERM_DIR}/glue.s -o ${TERM_BIN}
            COMMAND ${CMAKE_COMMAND} -E echo "TERM terminal blob built ($0800)"
            COMMENT "Building TERM terminal blob"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
            DEPENDS ${TERM_DIR}/term.c ${TERM_DIR}/glue.s ${TERM_DIR}/term.cfg
                    ${COMMON_DIR}/scrollback.c ${COMMON_DIR}/scrollback.h
            VERBATIM
        )
        # IRC chat client blob (programs/irc), same toolchain as TERM. Staged at
        # ../kernel/irc.bin for the headless test; IRC.PRG for the disk is made
        # by programs/irc/build.sh.
        set(IRC_DIR ${CMAKE_SOURCE_DIR}/programs/irc)
        set(COMMON_DIR ${CMAKE_SOURCE_DIR}/programs/common)
        set(IRC_BIN ${CMAKE_BINARY_DIR}/kernel/irc.bin)
        add_custom_target(irc_bin ALL
            COMMAND cl65 -t none --signed-chars -O -I ${COMMON_DIR} -C ${IRC_DIR}/irc.cfg
                    ${IRC_DIR}/irc.c ${COMMON_DIR}/scrollback.c ${IRC_DIR}/glue.s -o ${IRC_BIN}
            COMMAND ${CMAKE_COMMAND} -E echo "IRC chat-client blob built ($0800)"
            COMMENT "Building IRC chat-client blob"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
            DEPENDS ${IRC_DIR}/irc.c ${IRC_DIR}/glue.s ${IRC_DIR}/irc.cfg
                    ${COMMON_DIR}/scrollback.c ${COMMON_DIR}/scrollback.h
            VERBATIM
        )
        # VENTURE blob (programs/venture). Staged at ../kernel/venture.bin so the
        # headless test can load it at $0800 and drive it through the control port;
        # VENTURE.PRG for the disk is made by programs/venture/build.sh.
        set(VENTURE_DIR ${CMAKE_SOURCE_DIR}/programs/venture)
        set(VENTURE_BIN ${CMAKE_BINARY_DIR}/kernel/venture.bin)
        add_custom_target(venture_bin ALL
            COMMAND cl65 -t none --signed-chars -O -C ${VENTURE_DIR}/venture.cfg
                    ${VENTURE_DIR}/venture.c ${VENTURE_DIR}/glue.s -o ${VENTURE_BIN}
            COMMAND ${CMAKE_COMMAND} -E echo "VENTURE blob built ($0800)"
            COMMENT "Building VENTURE blob"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
            DEPENDS ${VENTURE_DIR}/venture.c ${VENTURE_DIR}/glue.s
                    ${VENTURE_DIR}/venture.cfg ${VENTURE_DIR}/venture.h
            VERBATIM
        )
    else()
        message(STATUS "cl65 not found - skipping TERM/IRC/VENTURE blobs")
    endif()

    # ================================================================
    # MFC-DOS Resident ROM Build Target ($9000-$AFFF, always mapped)
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
        COMMAND ${CMAKE_COMMAND} -E echo "MFC-DOS resident ROM built ($9000-$AFFF)"
        COMMENT "Building MFC-DOS resident ROM"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${DOS_ASM_SOURCE} ${DOS_CONFIG}
        VERBATIM
    )

else()
    # No ROMs means no machine: 6502-kernel would still compile and link, then
    # abort at startup with "Could not open kernel.rom". Fail here instead.
    set(_cc65_help
"cc65 toolchain not found (both ca65 and ld65 are required).
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
