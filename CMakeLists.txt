cmake_minimum_required(VERSION 3.30)
project(6502_Kernel)

set(CMAKE_CXX_STANDARD 20)

# Print helpful information
message(STATUS "6502 Computer Emulator Build Configuration")
message(STATUS "===========================================")

# Help CMake find Qt6 on macOS with Homebrew
if(APPLE)
    # Qt6 installed via Homebrew
    list(APPEND CMAKE_PREFIX_PATH 
        "/opt/homebrew/opt/qt"
        "/opt/homebrew/Cellar/qt/6.9.1"
        "/usr/local/opt/qt"
    )
endif()

# Try to find Qt6 first, then fall back to Qt5
find_package(Qt6 QUIET COMPONENTS Core Widgets)
if(NOT Qt6_FOUND)
    find_package(Qt5 QUIET COMPONENTS Core Widgets)
    if(NOT Qt5_FOUND)
        message(WARNING "Neither Qt6 nor Qt5 found. Building console-only version.")
        message(STATUS "To build with GUI support, install Qt6 or Qt5:")
        message(STATUS "  - macOS: brew install qt6 or brew install qt5")
        message(STATUS "  - Ubuntu/Debian: sudo apt install qt6-base-dev or sudo apt install qtbase5-dev")
        message(STATUS "  - Other: Download from https://qt.io/download")
        set(QT_FOUND FALSE)
    else()
        set(QT_FOUND TRUE)
        set(QT_VERSION 5)
        message(STATUS "Using Qt5")
    endif()
else()
    set(QT_FOUND TRUE)
    set(QT_VERSION 6)
    message(STATUS "Using Qt6")
endif()

# Enable Qt's MOC (Meta-Object Compiler) if Qt is found
if(QT_FOUND)
    set(CMAKE_AUTOMOC ON)
endif()

# Common source files
set(SOURCES
    main.cpp
    Computer/Memory.cpp
    Computer/CPU6502.cpp
    Computer/ResetCircuit.cpp
    Computer/TimingCircuit.cpp
    Computer/Computer6502.cpp
    Computer/VIC.cpp
    Computer/PIA.cpp
    Computer/MapFileParser.cpp
)

if(QT_FOUND)
    # Add Qt-specific source files
    list(APPEND SOURCES
        UI/DisplayWidget.cpp
        UI/MainWindow.cpp
    )
endif()

# Create executable
add_executable(6502_Kernel ${SOURCES})

# Add include directories for organized structure
target_include_directories(6502_Kernel PRIVATE 
    ${CMAKE_SOURCE_DIR}/Computer
    ${CMAKE_SOURCE_DIR}/UI
    ${CMAKE_SOURCE_DIR}/Kernel
)

if(QT_FOUND)
    # Link Qt libraries and define QT_GUI
    if(QT_VERSION EQUAL 6)
        target_link_libraries(6502_Kernel Qt6::Core Qt6::Widgets)
    else()
        target_link_libraries(6502_Kernel Qt5::Core Qt5::Widgets)
    endif()
    
    # Define QT_GUI to enable Qt code paths
    target_compile_definitions(6502_Kernel PRIVATE QT_GUI=1)
else()
    message(STATUS "Building console-only version (Qt not found)")
endif()

# Add custom command to build kernel ROM from assembly source
find_program(CA65_FOUND ca65)
find_program(LD65_FOUND ld65)

if(CA65_FOUND AND LD65_FOUND)
    message(STATUS "Found cc65 toolchain - will build kernel ROM automatically")
    
    # Create a target that always builds the kernel ROM
    add_custom_target(kernel_rom ALL
        COMMAND ca65 ${CMAKE_SOURCE_DIR}/Kernel/kernel.asm -o ${CMAKE_SOURCE_DIR}/Kernel/kernel.o
        COMMAND ld65 -C ${CMAKE_SOURCE_DIR}/Kernel/memory.cfg ${CMAKE_SOURCE_DIR}/Kernel/kernel.o -o ${CMAKE_SOURCE_DIR}/Kernel/kernel.rom -m ${CMAKE_SOURCE_DIR}/Kernel/kernel.map
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/Kernel/kernel.rom ${CMAKE_BINARY_DIR}/kernel.rom
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/Kernel/kernel.map ${CMAKE_BINARY_DIR}/kernel.map
        COMMENT "Building kernel ROM (always)"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM
    )
    
    
    # Make the main executable depend on kernel ROM
    add_dependencies(6502_Kernel kernel_rom)
    
else()
    message(WARNING "cc65 toolchain not found. Please install ca65 and ld65 to build kernel ROM automatically.")
    message(STATUS "You can manually build the kernel ROM with:")
    message(STATUS "  ca65 Kernel/kernel.asm -o Kernel/kernel.o")
    message(STATUS "  ld65 -C Kernel/memory.cfg Kernel/kernel.o -o Kernel/kernel.rom")
    message(STATUS "  cp Kernel/kernel.rom build/")
endif()
