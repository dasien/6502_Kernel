# BuildKernel.cmake - Build 6502 kernel ROM from assembly source

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
        COMMAND ca65 ${KERNEL_ASM_SOURCE} -o ${KERNEL_OBJECT}
        COMMAND ld65 -C ${KERNEL_CONFIG} ${KERNEL_OBJECT} -o ${KERNEL_ROM} -m ${KERNEL_MAP}
        COMMAND ${CMAKE_COMMAND} -E echo "================================================================"
        COMMAND ${CMAKE_COMMAND} -E echo "ROM BUILD COMPLETE - SIZE ANALYSIS"
        COMMAND ${CMAKE_COMMAND} -E echo "================================================================"
        COMMAND ${CMAKE_COMMAND} -DROM_FILE=${KERNEL_ROM} -DMAP_FILE=${KERNEL_MAP} -P ${CMAKE_SOURCE_DIR}/tools/cmake/rom_size.cmake
        COMMAND ${CMAKE_COMMAND} -E echo "================================================================"
        COMMENT "Building kernel ROM in build directory"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${KERNEL_ASM_SOURCE} ${KERNEL_CONFIG}
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
    # ASSEMBLER Module ROM Build Target (module bank 2)
    # ================================================================

    set(ASSEMBLER_DIR ${CMAKE_SOURCE_DIR}/src/kernel/assembler)
    set(ASSEMBLER_ASM_SOURCE ${ASSEMBLER_DIR}/assembler.asm)
    set(ASSEMBLER_CONFIG ${ASSEMBLER_DIR}/assembler_memory.cfg)
    set(ASSEMBLER_INC ${ASSEMBLER_DIR}/opcodes_65c02.inc)

    set(ASSEMBLER_OBJECT ${CMAKE_BINARY_DIR}/kernel/assembler.o)
    set(ASSEMBLER_ROM ${CMAKE_BINARY_DIR}/kernel/assembler.rom)
    set(ASSEMBLER_MAP ${CMAKE_BINARY_DIR}/kernel/assembler.map)

    # -I ASSEMBLER_DIR so .include "opcodes_65c02.inc" resolves.
    add_custom_target(assembler_rom ALL
        COMMAND ca65 ${ASSEMBLER_ASM_SOURCE} -I ${ASSEMBLER_DIR} -o ${ASSEMBLER_OBJECT}
        COMMAND ld65 -C ${ASSEMBLER_CONFIG} ${ASSEMBLER_OBJECT} -o ${ASSEMBLER_ROM} -m ${ASSEMBLER_MAP}
        COMMAND ${CMAKE_COMMAND} -E echo "ASSEMBLER module ROM built (bank 2)"
        COMMENT "Building ASSEMBLER module ROM"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${ASSEMBLER_ASM_SOURCE} ${ASSEMBLER_CONFIG} ${ASSEMBLER_INC}
        VERBATIM
    )

    # ================================================================
    # FORTH Module ROM Build Target (module bank 3)
    # ================================================================
    # FIG-Forth 6502 (Ragsdale Rel 1.1). forth.s is generated from the
    # byte-verified vendor/fig-forth/figforth.s by make_module.py; see that
    # directory for provenance and the $0200 byte-identical check.

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
        set(TERM_BIN ${CMAKE_BINARY_DIR}/kernel/term.bin)
        add_custom_target(term_bin ALL
            COMMAND cl65 -t none --signed-chars -O -C ${TERM_DIR}/term.cfg
                    ${TERM_DIR}/term.c ${TERM_DIR}/glue.s -o ${TERM_BIN}
            COMMAND ${CMAKE_COMMAND} -E echo "TERM terminal blob built ($0800)"
            COMMENT "Building TERM terminal blob"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
            DEPENDS ${TERM_DIR}/term.c ${TERM_DIR}/glue.s ${TERM_DIR}/term.cfg
            VERBATIM
        )
    else()
        message(STATUS "cl65 not found - skipping TERM terminal blob (term_bin)")
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
    message(WARNING "cc65 toolchain not found. Please install ca65 and ld65 to build kernel ROM automatically.")
    message(STATUS "You can manually build the kernel ROM with:")
    message(STATUS "  ca65 kernel/asm/kernel.asm -o build/kernel/kernel.o")
    message(STATUS "  ld65 -C kernel/config/memory.cfg build/kernel/kernel.o -o build/kernel/kernel.rom")
endif()