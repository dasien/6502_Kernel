# FindQt.cmake - Find Qt5 or Qt6 for the project

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
        set(QT_FOUND FALSE CACHE BOOL "Qt found")
    else()
        set(QT_FOUND TRUE CACHE BOOL "Qt found")
        set(QT_VERSION 5 CACHE STRING "Qt version")
        message(STATUS "Using Qt5")
    endif()
else()
    set(QT_FOUND TRUE CACHE BOOL "Qt found")
    set(QT_VERSION 6 CACHE STRING "Qt version")
    message(STATUS "Using Qt6")
endif()

# Enable Qt's MOC (Meta-Object Compiler) if Qt is found
if(QT_FOUND)
    set(CMAKE_AUTOMOC ON)
endif()