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
        testMoveOverlapBackward();
        testSearchCommand();
        testWriteCommand();
        testStackCommand();
        testZeroPageCommand();
        testHexToDecimal();
        testDecimalToHex();

        // Print summary
        printSummary();
    }

private:
    Computer::Computer6502 computer;
    int tests_passed;
    int tests_failed;

    // Default cycle budget is generous so multi-line output (e.g. the help
    // listing, ~18 lines) fully prints and the keystroke queue drains before
    // the next command - otherwise a backlog delays later commands past their
    // verification point.
    bool sendCommand(const std::string& command, int cycles = 60000) {
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

    // Clear the screen so a following verifyResponse() can't match stale text
    // (e.g. "OK" left by a prior command, or hex digits inside an echoed command).
    void clearScreen() {
        sendCommand("C:");
    }

    // Read a byte straight from emulator RAM - robust, unlike screen scraping.
    uint8_t readMem(uint16_t address) {
        return computer.getMemory()->read(address);
    }

    bool verifyMemEquals(uint16_t address, uint8_t expected, const std::string& test_name) {
        uint8_t actual = readMem(address);
        bool ok = (actual == expected);

        std::cout << std::left << std::setw(30) << test_name << ": "
                  << (ok ? "PASS" : "FAIL");
        if (!ok) {
            std::cout << " (mem[$" << std::hex << std::uppercase << address
                      << "] = $" << std::setw(2) << std::setfill('0')
                      << static_cast<int>(actual) << ", expected $"
                      << static_cast<int>(expected) << std::dec << std::setfill(' ') << ")";
            tests_failed++;
        } else {
            tests_passed++;
        }
        std::cout << std::endl;
        return ok;
    }

    // Verify a command did NOT produce a range error on a freshly cleared screen.
    bool verifyNoRangeError(const std::string& test_name) {
        std::string screen = getScreenText();
        bool errored = screen.find("RANGE") != std::string::npos;

        std::cout << std::left << std::setw(30) << test_name << ": "
                  << (errored ? "FAIL" : "PASS");
        if (errored) {
            std::cout << " (unexpected RANGE? error)";
            tests_failed++;
        } else {
            tests_passed++;
        }
        std::cout << std::endl;
        return !errored;
    }

    // Drain a paged dump (T:/Z:/large ranges page at 24 lines and block on a
    // keypress); ESC aborts it and returns to the prompt so the next command
    // isn't swallowed by the pending page break.
    void drainPaging() {
        computer.getPia()->addKeypress(27);  // ESC
        computer.run(20000);
    }

    void testClearScreen() {
        sendCommand("C:");
        verifyResponse("0000>", "Clear Screen Command");
    }

    void testHelpCommand() {
        // Help is now triggered by '?' (no colon); 'H:' is the hex-to-decimal
        // command. The help listing (~18 lines) fits on one page, no paging.
        clearScreen();
        sendCommand("?");
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

    // Regression: a valid M: range must NOT fall through into the range-error
    // handler (bug: BCS to the next line meant M: always printed RANGE? and the
    // copy/move engine was unreachable). Verified via RAM, not screen scraping.
    void testMoveCommand() {
        // --- Copy (mode 0) ---
        sendCommand("F:8010-8017,CC");   // source = CC
        sendCommand("F:8020-8027,00");   // destination pre-cleared so CC proves the copy
        clearScreen();
        sendCommand("M:8010-8017,8020,0");
        verifyNoRangeError("M: Copy No RANGE Error");
        verifyMemEquals(0x8020, 0xCC, "M: Copy Dest Written");
        verifyMemEquals(0x8027, 0xCC, "M: Copy Dest End Written");
        verifyMemEquals(0x8010, 0xCC, "M: Copy Source Preserved");

        // --- Move (mode 1): destination written, source cleared ---
        sendCommand("F:8030-8033,DD");
        sendCommand("F:8040-8043,00");
        clearScreen();
        sendCommand("M:8030-8033,8040,1");
        verifyNoRangeError("M: Move No RANGE Error");
        verifyMemEquals(0x8040, 0xDD, "M: Move Dest Written");
        verifyMemEquals(0x8030, 0x00, "M: Move Source Cleared");
    }

    // Regression: large (>256 byte) overlapping move that goes through the
    // backward-copy path. Exercises the destination-end calculation where the
    // high byte of the block offset must be added (bug: offset_hi was discarded
    // and "ADC $00" read zero-page $00 instead of propagating carry).
    void testMoveOverlapBackward() {
        // Source $8000-$82FF (768 bytes): first half 11, second half 22.
        sendCommand("F:8000-817F,11");
        sendCommand("F:8180-82FF,22");
        sendCommand("F:8300-837F,00");   // clear the top of the destination tail
        clearScreen();
        // Copy up by $80 into an overlapping region -> backward copy.
        // dest range $8080-$837F; dest_end = $8080 + $2FF = $837F.
        sendCommand("M:8000-82FF,8080,0", 60000);
        verifyNoRangeError("M: Overlap Move No RANGE");
        // Last destination byte must come from source end ($82FF = 22).
        // If offset_hi were lost, dest_end is ~$200 too low and $837F stays 00.
        verifyMemEquals(0x837F, 0x22, "M: Overlap Dest End (offset_hi)");
        verifyMemEquals(0x8080, 0x11, "M: Overlap Dest Start");
        verifyMemEquals(0x8200, 0x22, "M: Overlap Dest Midpoint");
    }

    // Regression: a valid X: range must NOT fall through into the range-error
    // handler (same class of bug as M:). Match address is chosen so it does not
    // collide with any hex in the echoed command text.
    void testSearchCommand() {
        sendCommand("F:8100-81FF,00");
        // Place a distinctive 2-byte pattern DE AD at $8155.
        sendCommand("W:8155");
        for (char c : std::string("DE AD")) {
            computer.getPia()->addKeypress(c);
        }
        computer.getPia()->addKeypress('\r');
        computer.run(5000);
        computer.getPia()->addKeypress(27);  // ESC out of write mode
        computer.run(3000);

        clearScreen();
        sendCommand("X:8100-81FF,DE AD");
        verifyNoRangeError("X: Search No RANGE Error");
        verifyResponse("8155", "X: Search Finds Pattern");
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

        // After writing 4 bytes from $8050, the prompt advances to the next
        // address ($8054), not the last-written one.
        verifyResponse("8054>", "Write Mode Data Entry");

        // Exit write mode with ESC
        computer.getPia()->addKeypress(27);  // ESC
        computer.run(3000);

        // Verify the data was written straight from RAM (robust).
        verifyMemEquals(0x8050, 0xAB, "Write Data $8050 = AB");
        verifyMemEquals(0x8051, 0xCD, "Write Data $8051 = CD");
        verifyMemEquals(0x8052, 0xEF, "Write Data $8052 = EF");
        verifyMemEquals(0x8053, 0x12, "Write Data $8053 = 12");
    }

    void testStackCommand() {
        // T: dumps 32 lines and pages at 24; verify the first page shows the
        // base address, then drain the page break so the next test is clean.
        clearScreen();
        sendCommand("T:");
        verifyResponse("0100:", "Stack Display Command");
        drainPaging();
    }

    void testZeroPageCommand() {
        clearScreen();
        sendCommand("Z:");
        verifyResponse("0000:", "Zero Page Display Command");
        drainPaging();
    }

    // H: hex->decimal. Exercises the double-dabble (binary->BCD via decimal
    // mode) conversion across boundary, carry-propagation, and max cases.
    void testHexToDecimal() {
        clearScreen();
        sendCommand("H:0000");
        verifyResponse("#0", "Hex->Dec zero");

        clearScreen();
        sendCommand("H:000A");
        verifyResponse("#10", "Hex->Dec small (10)");

        clearScreen();
        sendCommand("H:0064");          // carry from tens into hundreds
        verifyResponse("#100", "Hex->Dec 100");

        clearScreen();
        sendCommand("H:0102");
        verifyResponse("#258", "Hex->Dec 258");

        clearScreen();
        sendCommand("H:FFFF");          // 16-bit maximum, all 5 digits
        verifyResponse("#65535", "Hex->Dec max (65535)");
    }

    // D: decimal->hex (unchanged, but guards the round trip with H:).
    void testDecimalToHex() {
        clearScreen();
        sendCommand("D:65535");
        verifyResponse("$FFFF", "Dec->Hex max (65535)");

        clearScreen();
        sendCommand("D:258");
        verifyResponse("$0102", "Dec->Hex 258");
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