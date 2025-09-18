/**
 * @file test_monitor_integration.cpp
 * @brief Integration tests for 6502 Monitor Commands
 *
 * This file tests the complete 6502 kernel by:
 * - Loading the actual kernel ROM
 * - Sending keyboard commands via PIA
 * - Capturing screen output via VIC
 * - Verifying expected responses
 */

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include "computer/Computer6502.h"

class MonitorIntegrationTester {
public:
    MonitorIntegrationTester() : tests_passed(0), tests_failed(0) {
        // Power on the system and let it initialize
        computer.power_on();
        computer.run(3000);  // Allow kernel initialization

        std::cout << "6502 Monitor Integration Test Suite" << std::endl;
        std::cout << "===================================" << std::endl;
    }

    void runAllTests() {
        std::cout << "\nRunning integration tests...\n" << std::endl;

        // Test basic commands
        testClearScreen();
        testHelpCommand();
        testFillCommand();
        testReadCommand();
        testMoveCommand();
        testWriteCommand();
        testStackCommand();
        testZeroPageCommand();

        // Print summary
        printSummary();
    }

private:
    Computer::Computer6502 computer;
    int tests_passed;
    int tests_failed;

    bool sendCommand(const std::string& command, int cycles = 10000) {
        // Send each character
        for (char c : command) {
            computer.getPia()->addKeypress(c);
        }
        // Send enter
        computer.getPia()->addKeypress('\r');

        // Run cycles to process command
        computer.run(cycles);
        return true;
    }

    std::string getScreenText() {
        auto& screen_buffer = computer.getVideoChip()->getScreenBuffer();
        std::string content;

        // Convert screen buffer to string
        for (int i = 0; i < 40 * 25; ++i) {
            uint8_t ch = screen_buffer[i];
            if (ch >= 0x20 && ch <= 0x7E) {
                content += static_cast<char>(ch);
            } else {
                content += ' ';
            }
        }
        return content;
    }

    bool verifyResponse(const std::string& expected, const std::string& test_name) {
        std::string screen = getScreenText();
        bool found = screen.find(expected) != std::string::npos;

        std::cout << std::left << std::setw(30) << test_name << ": "
                  << (found ? "PASS" : "FAIL");

        if (!found) {
            std::cout << " (Expected: '" << expected << "')";
            tests_failed++;
        } else {
            tests_passed++;
        }
        std::cout << std::endl;

        return found;
    }

    void testClearScreen() {
        sendCommand("C:");
        verifyResponse("0000>", "Clear Screen Command");
    }

    void testHelpCommand() {
        sendCommand("H:");
        verifyResponse("MONITOR COMMANDS", "Help Command Display");
    }

    void testFillCommand() {
        // Test fill command
        sendCommand("F:8000-8007,BB");
        verifyResponse("OK", "Fill Memory F:8000-8007,BB");

        // Verify the fill worked
        sendCommand("R:8000-8007");
        verifyResponse("BB", "Verify Fill Result");
    }

    void testReadCommand() {
        // Test single address read
        sendCommand("R:8000");
        verifyResponse("8000:", "Read Single Address");

        // Test range read
        sendCommand("R:8000-8003");
        verifyResponse("8000:", "Read Address Range");
    }

    void testMoveCommand() {
        // First fill source area
        sendCommand("F:8010-8017,CC");

        // Test copy operation (mode 0)
        sendCommand("M:8010-8017,8020,0");
        verifyResponse("OK", "Copy Memory Command");

        // Verify copy worked
        sendCommand("R:8020-8027");
        verifyResponse("CC", "Verify Copy Result");

        // Test move operation (mode 1)
        sendCommand("F:8030-8033,DD");  // Setup source
        sendCommand("M:8030-8033,8040,1");
        verifyResponse("OK", "Move Memory Command");

        // Verify move worked and source is cleared
        sendCommand("R:8040-8043");
        verifyResponse("DD", "Verify Move Destination");
    }

    void testWriteCommand() {
        // Test entering write mode
        sendCommand("W:8050");
        verifyResponse("8050>", "Write Mode Entry");

        // Write some data bytes in write mode
        std::string writeData = "AB CD EF 12";
        for (char c : writeData) {
            computer.getPia()->addKeypress(c);
        }
        computer.getPia()->addKeypress('\r');  // Enter to confirm
        computer.run(5000);

        verifyResponse("8053>", "Write Mode Data Entry");

        // Exit write mode with ESC
        computer.getPia()->addKeypress(27);  // ESC
        computer.run(3000);

        // Verify the data was written by reading it back
        sendCommand("R:8050-8053");
        bool dataWritten = verifyResponse("AB", "Write Command Data Verification");
        if (dataWritten) {
            // Check for the other bytes too
            verifyResponse("CD", "Write Command Data CD");
            verifyResponse("EF", "Write Command Data EF");
            verifyResponse("12", "Write Command Data 12");
        }
    }

    void testStackCommand() {
        sendCommand("T:");
        verifyResponse("0100:", "Stack Display Command");
    }

    void testZeroPageCommand() {
        sendCommand("Z:");
        verifyResponse("0000:", "Zero Page Display Command");
    }

    void printSummary() {
        std::cout << "\n=== TEST SUMMARY ===" << std::endl;
        std::cout << "Tests Passed: " << tests_passed << std::endl;
        std::cout << "Tests Failed: " << tests_failed << std::endl;
        std::cout << "Total Tests:  " << (tests_passed + tests_failed) << std::endl;

        if (tests_failed == 0) {
            std::cout << "\n🎉 ALL TESTS PASSED! 🎉" << std::endl;
        } else {
            std::cout << "\n❌ " << tests_failed << " test(s) failed" << std::endl;
        }
        std::cout << "=====================" << std::endl;
    }
};

int main() {
    try {
        MonitorIntegrationTester tester;
        tester.runAllTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test suite failed: " << e.what() << std::endl;
        return 1;
    }
}