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
        COMMAND ${CMAKE_COMMAND} -E copy ${KERNEL_ROM} ${CMAKE_BINARY_DIR}/kernel.rom
        COMMAND ${CMAKE_COMMAND} -E copy ${KERNEL_MAP} ${CMAKE_BINARY_DIR}/kernel.map
        COMMENT "Building kernel ROM in build directory"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kernel
        DEPENDS ${KERNEL_ASM_SOURCE} ${KERNEL_CONFIG}
        VERBATIM
    )
    
    # Make the main executable depend on kernel ROM
    add_dependencies(6502-kernel kernel_rom)
    
else()
    message(WARNING "cc65 toolchain not found. Please install ca65 and ld65 to build kernel ROM automatically.")
    message(STATUS "You can manually build the kernel ROM with:")
    message(STATUS "  ca65 kernel/asm/kernel.asm -o build/kernel/kernel.o")
    message(STATUS "  ld65 -C kernel/config/memory.cfg build/kernel/kernel.o -o build/kernel/kernel.rom")
endif()