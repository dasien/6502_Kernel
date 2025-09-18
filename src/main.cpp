#ifdef QT_GUI
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application properties
    app.setApplicationName("6502 Computer Emulator");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("6502 Development");

    // Create and show main window
    MainWindow window;
    window.show();

    return app.exec();
}
#else
#include <iostream>
#include <thread>
#include <chrono>
#include "Computer6502.h"

int main() {
    std::cout << "6502 Computer Emulator (Console Mode)" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "Qt not found - running in console mode" << std::endl;
    std::cout << std::endl;
    
    Computer::Computer6502 computer;
    
    // Power on the system
    std::cout << "Powering on system..." << std::endl;
    computer.power_on();
    
    // Run the system for a limited number of cycles
    std::cout << "Running test program..." << std::endl;
    std::cout << "This will write 'HELLO' to screen memory at $0400-$0404" << std::endl;
    
    // Run for enough cycles to complete kernel initialization and reach welcome message
    // Kernel needs ~800+ cycles for initialization (250 + 256 + 80 + overhead)
    computer.run(2000);
    
    std::cout << "Program execution completed." << std::endl;
    
    // Display the VIC screen buffer to show what was written to screen memory
    std::cout << "\n=== VIC SCREEN BUFFER CONTENTS ===\n";
    auto& screen_buffer = computer.getVideoChip()->getScreenBuffer();
    
    // Display only first few lines to see if welcome message appeared
    for (int line = 0; line < 10; ++line) {
        std::cout << "Line " << line << ": ";
        for (int col = 0; col < 40; ++col) {
            uint8_t ch = screen_buffer[line * 40 + col];
            if (ch >= 0x20 && ch <= 0x7E) {
                std::cout << static_cast<char>(ch);
            } else {
                std::cout << ".";
            }
        }
        std::cout << std::endl;
    }
    std::cout << "=== END SCREEN BUFFER ===\n";
    
    return 0;
}
#endif
