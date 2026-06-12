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
    # DEV TOOLS Module ROM Build Target (module bank 2)
    # ================================================================

    set(DEVTOOLS_DIR ${CMAKE_SOURCE_DIR}/src/kernel/devtools)
    set(DEVTOOLS_ASM_SOURCE ${DEVTOOLS_DIR}/devtools.asm)
    set(DEVTOOLS_CONFIG ${DEVTOOLS_DIR}/devtools_memory.cfg)
    set(DEVTOOLS_INC ${DEVTOOLS_DIR}/opcodes_65c02.inc)

    set(DEVTOOLS_OBJECT ${CMAKE_BINARY_DIR}/kernel/devtools.o)
    set(DEVTOOLS_ROM ${CMAKE_BINARY_DIR}/kernel/devtools.rom)
    set(DEVTOOLS_MAP ${CMAKE_BINARY_DIR}/kernel/devtools.map)

    # -I DEVTOOLS_DIR so .include "opcodes_65c02.inc" resolves.
    add_custom_target(devtools_rom ALL
        COMMAND ca65 ${DEVTOOLS_ASM_SOURCE} -I ${DEVTOOLS_DIR} -o ${DEVTOOLS_OBJECT}
        COMMAND ld65 -C ${DEVTOOLS_CONFIG} ${DEVTOOLS_OBJECT} -o ${DEVTOOLS_ROM} -m ${DEVTOOLS_MAP}
        COMMAND ${CMAKE_COMMAND} -E echo "DEV TOOLS module ROM built (bank 2)"
        COMMENT "Building DEV TOOLS module ROM"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${DEVTOOLS_ASM_SOURCE} ${DEVTOOLS_CONFIG} ${DEVTOOLS_INC}
        VERBATIM
    )

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