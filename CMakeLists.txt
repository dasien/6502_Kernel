cmake_minimum_required(VERSION 3.20)
project(6502-kernel 
    VERSION 1.0.0
    DESCRIPTION "6502 Microprocessor Emulator with Interactive Monitor"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Organize build outputs
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "6502 Computer Emulator v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0 -DDEBUG)
    else()
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()

# Include CMake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/tools/cmake)

# Find Qt (optional GUI support)
include(FindQt)

# Add subdirectories
add_subdirectory(src)

# Kernel ROM build (if cc65 available)
include(BuildKernel)

# Testing support
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ----------------------------------------------------------------
# mkfat16 - host tool to create a FAT16 disk.img for the block device.
# Reuses the FAT16 image builder shared with the filesystem tests.
# Build it, then create a disk:   ./bin/mkfat16 ../disk.img [files...]
# Or use the convenience target:  ninja sample_disk
# ----------------------------------------------------------------
add_executable(mkfat16 tools/mkfat16/mkfat16.cpp)
target_include_directories(mkfat16 PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_features(mkfat16 PRIVATE cxx_std_20)

# ----------------------------------------------------------------
# mkdisk - diskmap-driven disk image tool (create / read / update). Reuses the
# FAT16 builder + reader shared with the tests. Supersedes mkfat16.
#   ./bin/mkdisk create <image> <diskmap.txt>
#   ./bin/mkdisk read   <image> <outdir>
#   ./bin/mkdisk update <image> <diskmap.txt>
# ----------------------------------------------------------------
add_executable(mkdisk tools/mkdisk/mkdisk.cpp)
target_include_directories(mkdisk PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_features(mkdisk PRIVATE cxx_std_20)

# Assemble the default disk.img the GUI loads (${CMAKE_BINARY_DIR}/disk.img).
# Explicit target -- NOT part of `ninja`/ALL -- so routine builds never rewrite the
# disk. It stages a bundle from the repo `disk/` (config + games + diskmap) plus the
# freshly built app .PRGs, then runs `mkdisk create`. Rebuild a program's .PRG first
# (programs/<x>/build.sh) if you changed it, then `ninja disk`.
set(DISKROOT ${CMAKE_BINARY_DIR}/diskroot)
add_custom_target(disk
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${DISKROOT}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/disk ${DISKROOT}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/programs/edit/EDIT.PRG     ${DISKROOT}/EDIT.PRG
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/programs/term/TERM.PRG     ${DISKROOT}/TERM.PRG
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/programs/irc/IRC.PRG       ${DISKROOT}/IRC.PRG
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/programs/micromax/CHESS.PRG ${DISKROOT}/GAMES/CHESS.PRG
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DISKROOT}/SVAULT
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/programs/vault/VAULT.PRG    ${DISKROOT}/SVAULT/VAULT.PRG
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/programs/vault/STORY.TXT    ${DISKROOT}/SVAULT/STORY.TXT
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/programs/kpanic/KPANIC.PRG  ${DISKROOT}/GAMES/KPANIC.PRG
    COMMAND mkdisk create ${CMAKE_BINARY_DIR}/disk.img ${DISKROOT}/diskmap.txt
    DEPENDS mkdisk
    COMMENT "Assembling disk.img from disk/ + built app .PRGs"
    VERBATIM
)

# Installation rules
install(TARGETS 6502-kernel
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES ${CMAKE_BINARY_DIR}/kernel/kernel.rom
    DESTINATION share/6502-kernel
    OPTIONAL
)

# Desktop integration (Linux). The icon goes into the hicolor theme at each size
# the generator produces, which is what desktops and task switchers look up by the
# .desktop file's "Icon=mfc6502" name. Harmless elsewhere: the files simply are not
# consulted. The icon is also compiled into the binary (include/ui/AppIcon.h), so
# the running window has one whether or not these are installed.
if(UNIX AND NOT APPLE)
    foreach(icon_size 16 32 48 64 128 256 512)
        install(FILES ${CMAKE_SOURCE_DIR}/assets/mfc6502-${icon_size}.png
            DESTINATION share/icons/hicolor/${icon_size}x${icon_size}/apps
            RENAME mfc6502.png
            OPTIONAL
        )
    endforeach()
    install(FILES ${CMAKE_SOURCE_DIR}/assets/mfc6502.desktop
        DESTINATION share/applications
        OPTIONAL
    )
endif()

# ----------------------------------------------------------------
# Configuration summary
#
# QT_FOUND / QT_MULTIMEDIA_FOUND are CACHE variables, so installing a missing
# dependency later does NOT flip them on a re-run of ninja -- the build dir has
# to be deleted. The hints below say so, because silently building a mute or
# GUI-less emulator is the easiest way to lose an hour on this project.
# ----------------------------------------------------------------
message(STATUS "")
message(STATUS "======== 6502-kernel configuration ========")
if(QT_FOUND)
    message(STATUS "  Qt GUI ......... yes (Qt${QT_VERSION})")
else()
    message(STATUS "  Qt GUI ......... NO  -- console-only, not the interactive machine")
    message(STATUS "                   install qt6-base-dev, delete this build dir, re-run cmake")
endif()
if(QT_FOUND AND QT_MULTIMEDIA_FOUND)
    message(STATUS "  SID audio ...... yes")
elseif(QT_FOUND)
    message(STATUS "  SID audio ...... NO  -- the emulator runs silently")
    message(STATUS "                   install qt6-multimedia-dev, delete this build dir, re-run cmake")
endif()
if(CA65_FOUND AND LD65_FOUND)
    message(STATUS "  cc65 ROMs ...... yes")
else()
    message(STATUS "  cc65 ROMs ...... NO  -- host tools only, the emulator will not run")
endif()
if(BUILD_TESTS)
    message(STATUS "  Tests .......... yes")
else()
    message(STATUS "  Tests .......... no   (enable with -DBUILD_TESTS=ON)")
endif()
message(STATUS "===========================================")
message(STATUS "")
message(STATUS "  Build      cmake --build ${CMAKE_BINARY_DIR}")
message(STATUS "  Disk image cmake --build ${CMAKE_BINARY_DIR} --target disk")
if(BUILD_TESTS)
    message(STATUS "  Test       ctest --test-dir ${CMAKE_BINARY_DIR}")
endif()
message(STATUS "  Run        cd ${CMAKE_BINARY_DIR}/bin && ./6502-kernel")
message(STATUS "             (the ROMs and disk.img load via relative paths -- run it from bin/)")
message(STATUS "")
