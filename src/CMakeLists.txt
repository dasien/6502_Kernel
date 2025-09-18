# Source CMakeLists.txt - Build the main executable

# Common source files
set(SOURCES
    main.cpp
    computer/Memory.cpp
    computer/CPU6502.cpp
    computer/ResetCircuit.cpp
    computer/TimingCircuit.cpp
    computer/Computer6502.cpp
    computer/VIC.cpp
    computer/PIA.cpp
    computer/MapFileParser.cpp
)

# Qt headers that need MOC processing
set(QT_HEADERS)

if(QT_FOUND)
    # Add Qt-specific source files
    list(APPEND SOURCES
        ui/DisplayWidget.cpp
        ui/MainWindow.cpp
    )

    # Add Qt headers for MOC processing
    list(APPEND QT_HEADERS
        ${CMAKE_SOURCE_DIR}/include/ui/DisplayWidget.h
        ${CMAKE_SOURCE_DIR}/include/ui/MainWindow.h
    )
endif()

# Create executable
add_executable(6502-kernel ${SOURCES} ${QT_HEADERS})

# Add include directories for organized structure
target_include_directories(6502-kernel PRIVATE 
    ${CMAKE_SOURCE_DIR}/include/computer
    ${CMAKE_SOURCE_DIR}/include/ui
    ${CMAKE_SOURCE_DIR}/include
)

if(QT_FOUND)
    # Link Qt libraries and define QT_GUI
    if(QT_VERSION EQUAL 6)
        target_link_libraries(6502-kernel Qt6::Core Qt6::Widgets)
    else()
        target_link_libraries(6502-kernel Qt5::Core Qt5::Widgets)
    endif()
    
    # Define QT_GUI to enable Qt code paths
    target_compile_definitions(6502-kernel PRIVATE QT_GUI=1)
else()
    message(STATUS "Building console-only version (Qt not found)")
endif()