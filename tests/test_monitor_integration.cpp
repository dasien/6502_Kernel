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
        // Allow kernel initialization. RESET now also zeroes the 12KB module
        // window ($B000-$DFFF), which is ~90K cycles, so give boot enough room
        // to reach the command prompt before the first test runs.
        computer.run(200000);

        std::cout << "6502 Monitor Integration Test Suite" << std::endl;
        std::cout << "===================================" << std::endl;
    }

    void runAllTests() {
        std::cout << "\nRunning integration tests...\n" << std::endl;

        // Test basic commands
        testClearScreen();
        testHelpCommand();
        testScrollIntegrity();
        testBankMenu();
        testDevtoolsModule();
        testDisassembler();
        testDisassemblerBackspace();
        testDisassemblerEscMidline();
        testModuleStatePreservedAcrossLaunch();
        testAssembler();
        testTwoPassAssembler();
        testTwoPassDirectives();
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
        testDecimalOverflowNoCorruption();
        testMoveDestEqualsEnd();
        testMoveOverlapClearKeepsData();
        testEscAtPromptNoError();

        // Must run last: it launches BASIC (bank 1), which keeps running and
        // would otherwise consume the keystrokes of any following test.
        testBankLaunch();

        // Print summary
        printSummary();
    }

    // Number of failed assertions (0 = all passed). Lets main() return a
    // non-zero exit code so ctest actually flags integration regressions.
    int failureCount() const { return tests_failed; }

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

    // Verify the screen does NOT contain a given substring (e.g. an error msg).
    bool verifyAbsent(const std::string& unwanted, const std::string& test_name) {
        bool found = getScreenText().find(unwanted) != std::string::npos;
        std::cout << std::left << std::setw(30) << test_name << ": "
                  << (found ? "FAIL" : "PASS");
        if (found) {
            std::cout << " (unexpected '" << unwanted << "')";
            tests_failed++;
        } else {
            tests_passed++;
        }
        std::cout << std::endl;
        return !found;
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
        // The '?' and '.' meta-commands are now listed too (v2.2.6).
        verifyResponse("RECALL LAST COMMAND", "Help lists the . recall command");
    }

    // Regression: SCROLL_SCREEN once interleaved its four page-copies, which
    // corrupted every byte spanning a screen page boundary (a line printed
    // across $04FF/$0500 etc. would come out blended, e.g. "ZERO PAGE" -> "ZERO
    // PAGE MORY"). The help listing is 18 lines; two consecutive '?' (no clear
    // between them) prints 36 lines and forces ~11 scrolls. After that the most
    // recent help must still be on screen with every line intact. We assert the
    // full text of several lines, including long ones whose bytes straddle a
    // page boundary after scrolling - a cross-page scroll bug would split them.
    void testScrollIntegrity() {
        clearScreen();
        sendCommand("?");
        sendCommand("?");   // second help without clearing -> forces scrolling
        verifyResponse("M:XXXX-YYYY,ZZZZ,B (B:0=COPY 1=MOVE)",
                       "Scroll: M: help line intact");
        verifyResponse("X:XXXX-YYYY,PATTERN SEARCH MEMORY",
                       "Scroll: X: help line intact");
        verifyResponse("R:XXXX(-YYYY) READ FROM MEMORY",
                       "Scroll: R: help line intact");
        verifyResponse("Z:     PRINT ZERO PAGE",
                       "Scroll: Z: help line intact");
        verifyResponse(".      RECALL LAST COMMAND",
                       "Scroll: . help line intact");
    }

    // Deliver a single keystroke (no trailing CR) and run, for the bank menu's
    // single-key selection prompt.
    void sendKey(uint8_t key, int cycles = 60000) {
        computer.getPia()->addKeypress(key);
        computer.run(cycles);
    }

    // B: opens the module bank menu (Phase 3). It lists MODULE_DIR (BASIC = bank
    // 1, dev tools = bank 2) and waits for a single-key selection; ESC cancels.
    void testBankMenu() {
        clearScreen();
        sendCommand("B:");
        verifyResponse("MODULE BANKS", "Bank menu header shown");
        verifyResponse("BASIC", "Bank menu lists BASIC");
        verifyResponse("ASSEMBLER", "Bank menu lists the dev tools module");
        verifyResponse("SELECT", "Bank menu shows the selection prompt");

        // ESC cancels; the monitor must be responsive again afterward.
        sendKey(0x1B);
        clearScreen();
        sendCommand("R:8000");
        verifyResponse("8000:", "Monitor responsive after bank-menu ESC");
    }

    // End-to-end round-trip through the dev-tools module (bank 2): B: -> 2 maps
    // the bank and jumps in (banner prints), then ESC makes the module JMP $FF12,
    // which unmaps the bank (window back to RAM) and returns to the monitor.
    // Unlike BASIC, the module returns cleanly, so this can run mid-suite.
    void testDevtoolsModule() {
        clearScreen();
        sendCommand("B:");
        sendKey('2', 200000);    // select dev tools -> module banner
        verifyMemEquals(0xFE23, 0x02, "B: select maps bank 2 (MODULE_BANK)");
        verifyResponse("DEV TOOLS", "Dev tools module launches from bank 2");

        sendKey(0x1B, 200000);   // ESC -> module returns via $FF12
        verifyMemEquals(0xFE23, 0x00, "Module return unmaps bank (window = RAM)");
        clearScreen();
        sendCommand("R:8000");
        verifyResponse("8000:", "Monitor responsive after module return");
    }

    // The dev-tools disassembler (D xxxx). Poke a known 65C02 sequence into RAM
    // at $0800 (below the module window, so it stays RAM whatever bank is mapped),
    // launch the module, disassemble it, and check the decoded text - including a
    // 65C02-only mode and a computed branch target.
    void testDisassembler() {
        Computer::Memory *mem = computer.getMemory();
        const uint8_t code[] = {
            0xA9, 0x05,        // LDA #$05      immediate
            0x8D, 0x00, 0x04,  // STA $0400     absolute
            0xB2, 0xFB,        // LDA ($FB)     zero-page indirect (65C02)
            0x80, 0xF7         // BRA $0800     relative (target = $0809 - 9)
        };
        for (size_t i = 0; i < sizeof(code); ++i)
            mem->write(static_cast<uint16_t>(0x0800 + i), code[i]);

        clearScreen();
        sendCommand("B:");
        sendKey('2', 200000);            // launch dev tools (bank 2)

        for (char c : std::string("D0800"))
            computer.getPia()->addKeypress(c);
        computer.getPia()->addKeypress('\r');
        computer.run(500000);

        verifyResponse("LDA #$05",  "Disasm: immediate operand");
        verifyResponse("STA $0400", "Disasm: absolute operand");
        verifyResponse("LDA ($FB)", "Disasm: zero-page indirect (65C02)");
        verifyResponse("BRA $0800", "Disasm: relative branch target");

        sendKey(0x1B, 200000);           // ESC -> return to monitor
        verifyMemEquals(0xFE23, 0x00, "Disasm: module returned (bank unmapped)");
    }

    // Backspace in the address entry: type a wrong digit, erase it, finish, and
    // confirm the corrected address ($0800) is what gets disassembled.
    void testDisassemblerBackspace() {
        Computer::Memory *mem = computer.getMemory();
        mem->write(0x0800, 0xA9);        // LDA #$05 marker at $0800
        mem->write(0x0801, 0x05);

        clearScreen();
        sendCommand("B:");
        sendKey('2', 200000);            // launch dev tools

        // "D085" then backspace (drops the 5 -> $0008) then "00" -> $0800.
        for (char c : std::string("D085"))
            computer.getPia()->addKeypress(c);
        computer.getPia()->addKeypress(0x08);   // backspace
        for (char c : std::string("00"))
            computer.getPia()->addKeypress(c);
        computer.getPia()->addKeypress('\r');
        computer.run(500000);

        verifyResponse("0800: A9 05", "Disasm: backspace corrected the address");

        sendKey(0x1B, 200000);
    }

    // Mid-line ESC cancels the entry and the module reprompts and stays usable
    // (regression: ESC-cancel used to restart the read loop without reprinting
    // the prompt, leaving the module on a blank line).
    void testDisassemblerEscMidline() {
        computer.getMemory()->write(0x0800, 0xEA);   // NOP marker

        clearScreen();
        sendCommand("B:");
        sendKey('2', 200000);

        for (char c : std::string("D08"))            // partial address...
            computer.getPia()->addKeypress(c);
        computer.getPia()->addKeypress(0x1B);        // ...ESC cancels the line
        computer.run(200000);

        // The typed text is erased in place (not left on a stale line above).
        verifyAbsent("D08", "Disasm: ESC erases the typed line in place");

        // Module must accept a fresh command afterward.
        for (char c : std::string("D0800"))
            computer.getPia()->addKeypress(c);
        computer.getPia()->addKeypress('\r');
        computer.run(500000);
        verifyResponse("0800: EA", "Disasm: usable after mid-line ESC cancel");

        sendKey(0x1B, 200000);
    }

    // Edge case of reusing the monitor's scratch via the extended ABI: a module's
    // disassemble (K_PARSE_HEX) overwrites MON_CURRADDR ($14/$15), but the launch
    // save/restore must leave the monitor's current address intact on return.
    void testModuleStatePreservedAcrossLaunch() {
        clearScreen();
        sendCommand("R:1234");                   // set the monitor's current address
        const uint8_t pre_lo = readMem(0x14);
        const uint8_t pre_hi = readMem(0x15);

        sendCommand("B:");
        sendKey('2', 200000);                    // launch dev tools
        for (char c : std::string("D0400"))      // module sets MON_CURRADDR := $0400
            computer.getPia()->addKeypress(c);
        computer.getPia()->addKeypress('\r');
        computer.run(500000);
        sendKey(0x1B, 200000);                   // exit back to the monitor

        verifyMemEquals(0x14, pre_lo, "MON_CURRADDR lo restored after module");
        verifyMemEquals(0x15, pre_hi, "MON_CURRADDR hi restored after module");

        // And the monitor's command buffer is usable again (it was the module's
        // line-input scratch via K_READ_LINE).
        clearScreen();
        sendCommand("R:8000");
        verifyResponse("8000:", "Monitor command input works after module reuse");
    }

    // End-to-end: selecting BASIC from the menu maps bank 1 into the window and
    // jumps into the module. Verifies the bank register, that the window now
    // shows bank-1 ROM (LAB_COLD opcode $A0), and that BASIC's banner prints.
    // Runs LAST (BASIC keeps running afterward), so it can't disturb other tests.
    void testBankLaunch() {
        clearScreen();
        sendCommand("B:");
        sendKey('1', 200000);    // select BASIC -> EhBASIC "Memory size ?" prompt
        verifyMemEquals(0xFE23, 0x01, "B: select maps bank 1 (MODULE_BANK)");
        verifyMemEquals(0xB000, 0xA0, "Window shows bank-1 ROM (LAB_COLD opcode)");
        sendKey('\r', 2000000);  // accept default memory size -> sign-on banner
        verifyResponse("MFC BASIC", "BASIC launches from bank 1");
    }

    // The dev-tools line assembler (A xxxx). Assembles a sequence covering many
    // addressing modes (incl. 65C02 (zp), accumulator, and a computed branch)
    // and verifies the emitted bytes straight from memory.
    void testAssembler() {
        clearScreen();
        sendCommand("B:");
        sendKey('2', 200000);            // launch dev tools

        sendCommand("A0800");            // assemble mode at $0800
        sendCommand("LDA #$05");         // A9 05      immediate
        sendCommand("STA $0400");        // 8D 00 04   absolute
        sendCommand("NOP");              // EA         implied
        sendCommand("ASL A");            // 0A         accumulator
        sendCommand("LDA ($40)");        // B2 40      zp indirect (65C02)
        sendCommand("STA $10,X");        // 95 10      zp,X
        sendCommand("LDA ($30,X)");      // A1 30      (zp,X)
        sendCommand("LDA ($20),Y");      // B1 20      (zp),Y
        sendCommand("BEQ $080F");        // F0 FE      relative (to self -> -2)
        sendCommand("JSR $FF00");        // 20 00 FF   absolute (JSR: no-suffix handler)
        sendCommand("JMP $1234");        // 4C 34 12   absolute
        sendCommand("");                 // empty line exits assemble mode

        verifyMemEquals(0x0800, 0xA9, "ASM: LDA #imm opcode");
        verifyMemEquals(0x0801, 0x05, "ASM: LDA #imm operand");
        verifyMemEquals(0x0802, 0x8D, "ASM: STA abs opcode");
        verifyMemEquals(0x0803, 0x00, "ASM: STA abs lo");
        verifyMemEquals(0x0804, 0x04, "ASM: STA abs hi");
        verifyMemEquals(0x0805, 0xEA, "ASM: NOP");
        verifyMemEquals(0x0806, 0x0A, "ASM: ASL A (accumulator)");
        verifyMemEquals(0x0807, 0xB2, "ASM: LDA (zp) 65C02 opcode");
        verifyMemEquals(0x0808, 0x40, "ASM: LDA (zp) operand");
        verifyMemEquals(0x0809, 0x95, "ASM: STA zp,X");
        verifyMemEquals(0x080B, 0xA1, "ASM: LDA (zp,X)");
        verifyMemEquals(0x080D, 0xB1, "ASM: LDA (zp),Y");
        verifyMemEquals(0x080F, 0xF0, "ASM: BEQ opcode");
        verifyMemEquals(0x0810, 0xFE, "ASM: BEQ offset (target-PC-2)");
        verifyMemEquals(0x0811, 0x20, "ASM: JSR abs opcode");
        verifyMemEquals(0x0812, 0x00, "ASM: JSR abs lo");
        verifyMemEquals(0x0813, 0xFF, "ASM: JSR abs hi");
        verifyMemEquals(0x0814, 0x4C, "ASM: JMP abs opcode");
        verifyMemEquals(0x0815, 0x34, "ASM: JMP abs lo");
        verifyMemEquals(0x0816, 0x12, "ASM: JMP abs hi");

        sendKey(0x1B, 200000);           // exit the module back to the monitor
        verifyMemEquals(0xFE23, 0x00, "ASM: module returned (bank unmapped)");
    }

    // The two-pass assembler (B = build the source buffer at $A000). Pokes a
    // small program with labels, a forward branch (BEQ DONE), a backward branch
    // (BNE LOOP), and an absolute label ref (JMP START) into the source buffer,
    // builds it, and verifies the emitted bytes (so labels + forward refs work).
    void testTwoPassAssembler() {
        Computer::Memory *mem = computer.getMemory();
        const char *src =
            ".ORG $0800\n"
            "START: LDA #$00\n"
            "BEQ DONE\n"
            "LOOP: INX\n"
            "BNE LOOP\n"
            "NOP\n"
            "DONE: JMP START\n"
            ".END\n";
        uint16_t a = 0xA000;
        for (const char *p = src; *p; ++p)
            mem->write(a++, static_cast<uint8_t>(*p));
        mem->write(a, 0x00);             // source terminator

        clearScreen();
        sendCommand("B:");
        sendKey('2', 200000);            // launch dev tools
        sendCommand("B", 300000);        // build

        verifyMemEquals(0x0800, 0xA9, "2pass: LDA #imm");
        verifyMemEquals(0x0801, 0x00, "2pass: imm operand");
        verifyMemEquals(0x0802, 0xF0, "2pass: BEQ opcode");
        verifyMemEquals(0x0803, 0x04, "2pass: BEQ forward offset");
        verifyMemEquals(0x0804, 0xE8, "2pass: INX (LOOP)");
        verifyMemEquals(0x0805, 0xD0, "2pass: BNE opcode");
        verifyMemEquals(0x0806, 0xFD, "2pass: BNE backward offset");
        verifyMemEquals(0x0807, 0xEA, "2pass: NOP");
        verifyMemEquals(0x0808, 0x4C, "2pass: JMP opcode");
        verifyMemEquals(0x0809, 0x00, "2pass: JMP target lo (label)");
        verifyMemEquals(0x080A, 0x08, "2pass: JMP target hi (label)");

        sendKey(0x1B, 200000);           // exit the module
    }

    // Two-pass directives + expressions: NAME = expr assignment, .ASCII, .BYTE,
    // .WORD (with a label and a literal), and #<MSG / #>MSG / #COUNT+1 operand
    // expressions. Verifies the emitted bytes.
    void testTwoPassDirectives() {
        Computer::Memory *mem = computer.getMemory();
        const char *src =
            ".ORG $0900\n"
            "COUNT = 3\n"
            "MSG: .ASCII \"HI\"\n"
            ".BYTE COUNT,$FF\n"
            ".WORD MSG,$1234\n"
            "LDA #<MSG\n"
            "LDA #>MSG\n"
            "LDX #COUNT+1\n"
            ".END\n";
        uint16_t a = 0xA000;
        for (const char *p = src; *p; ++p)
            mem->write(a++, static_cast<uint8_t>(*p));
        mem->write(a, 0x00);

        clearScreen();
        sendCommand("B:");
        sendKey('2', 200000);
        sendCommand("B", 300000);

        verifyMemEquals(0x0900, 0x48, "dir: .ASCII 'H'");
        verifyMemEquals(0x0901, 0x49, "dir: .ASCII 'I'");
        verifyMemEquals(0x0902, 0x03, "dir: .BYTE COUNT (=3)");
        verifyMemEquals(0x0903, 0xFF, "dir: .BYTE $FF");
        verifyMemEquals(0x0904, 0x00, "dir: .WORD MSG lo");
        verifyMemEquals(0x0905, 0x09, "dir: .WORD MSG hi");
        verifyMemEquals(0x0906, 0x34, "dir: .WORD $1234 lo");
        verifyMemEquals(0x0907, 0x12, "dir: .WORD $1234 hi");
        verifyMemEquals(0x0909, 0x00, "expr: #<MSG (low byte)");
        verifyMemEquals(0x090B, 0x09, "expr: #>MSG (high byte)");
        verifyMemEquals(0x090D, 0x04, "expr: #COUNT+1");

        sendKey(0x1B, 200000);
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

    // Regression: D: overflow must not corrupt the stack. "D:65536" drives the
    // high-byte-carry overflow path (65530 + 6) that previously did an unbalanced
    // PLA and trashed the return address. The monitor must survive: a following
    // command still produces its expected output.
    void testDecimalOverflowNoCorruption() {
        clearScreen();
        sendCommand("D:65536");          // overflow -> RANGE? but must NOT crash
        clearScreen();
        sendCommand("H:00FF");           // if the stack was corrupted, this never runs
        verifyResponse("#255", "D: overflow does not corrupt stack");
    }

    // Regression: M: with dest == source-end must use the backward copy. Forward
    // copy would clobber source[end] before reading it. Set $0900-$0902 = AA BB CC,
    // copy that 3-byte range to dest $0902 (== end); $0904 must end up CC.
    void testMoveDestEqualsEnd() {
        computer.getMemory()->write(0x0900, 0xAA);
        computer.getMemory()->write(0x0901, 0xBB);
        computer.getMemory()->write(0x0902, 0xCC);
        clearScreen();
        sendCommand("M:0900-0902,0902,0");   // copy, dest == end
        verifyMemEquals(0x0904, 0xCC, "M: dest==end uses backward copy");
        verifyMemEquals(0x0903, 0xBB, "M: dest==end middle byte");
        verifyMemEquals(0x0902, 0xAA, "M: dest==end overlap byte");
    }

    // Regression: an overlapping MOVE (mode 1) must clear only the VACATED
    // source bytes, not the bytes that now hold moved data. Mirrors the reported
    // case: fill $0900-$0907 with FF, move to $0907 (dest == source-end). All 8
    // bytes must survive at $0907-$090E; only $0900-$0906 are cleared.
    void testMoveOverlapClearKeepsData() {
        for (uint16_t a = 0x0900; a <= 0x0907; ++a)
            computer.getMemory()->write(a, 0xFF);
        clearScreen();
        sendCommand("M:0900-0907,0907,1");        // move, dest == end (overlap)
        verifyMemEquals(0x0906, 0x00, "M: overlap move clears vacated byte");
        verifyMemEquals(0x0907, 0xFF, "M: overlap move keeps dest-start byte");
        verifyMemEquals(0x090E, 0xFF, "M: overlap move keeps dest-end byte");
        verifyMemEquals(0x090F, 0x00, "M: overlap move leaves past-dest clear");
    }

    // Regression: a bare ESC at the command prompt is a clean no-op, not ERROR?.
    void testEscAtPromptNoError() {
        clearScreen();
        computer.getPia()->addKeypress(27);   // ESC at an empty prompt
        computer.run(20000);
        verifyAbsent("ERROR", "ESC at prompt is a no-op (no ERROR?)");
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
        return tester.failureCount() == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Test suite failed: " << e.what() << std::endl;
        return 1;
    }
}