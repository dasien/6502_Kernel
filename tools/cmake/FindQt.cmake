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

# Try to find Qt6 first, then fall back to Qt5 (Network: terminal TCP/modem)
find_package(Qt6 QUIET COMPONENTS Core Widgets Network)
if(NOT Qt6_FOUND)
    find_package(Qt5 QUIET COMPONENTS Core Widgets Network)
    if(NOT Qt5_FOUND)
        message(WARNING "Neither Qt6 nor Qt5 found. Building console-only version.")
        message(STATUS "To build with GUI support, install Qt6 or Qt5:")
        message(STATUS "  - macOS: brew install qt6 or brew install qt5")
        message(STATUS "  - Ubuntu/Debian: sudo apt install qt6-base-dev or sudo apt install qtbase5-dev")
        message(STATUS "  - Windows: install Qt6, then pass its prefix explicitly, e.g.")
        message(STATUS "      -DCMAKE_PREFIX_PATH=C:/Qt/6.9.1/msvc2022_64")
        message(STATUS "    (no default is guessed: the path carries the version and ABI)")
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

# Optional Qt Multimedia (audio output for the SID sound chip). If it is not
# installed the emulator still builds -- it just runs without sound.
if(QT_FOUND)
    if(QT_VERSION EQUAL 6)
        find_package(Qt6 QUIET COMPONENTS Multimedia)
        set(_qt_mm_found ${Qt6Multimedia_FOUND})
    else()
        find_package(Qt5 QUIET COMPONENTS Multimedia)
        set(_qt_mm_found ${Qt5Multimedia_FOUND})
    endif()
    if(_qt_mm_found)
        set(QT_MULTIMEDIA_FOUND TRUE CACHE BOOL "Qt Multimedia found")
        message(STATUS "Qt Multimedia found - SID audio enabled")
    else()
        set(QT_MULTIMEDIA_FOUND FALSE CACHE BOOL "Qt Multimedia found")
        message(STATUS "Qt Multimedia NOT found - building without SID audio")
    endif()
endif()

# Enable Qt's MOC (Meta-Object Compiler) if Qt is found
if(QT_FOUND)
    set(CMAKE_AUTOMOC ON)
endif()