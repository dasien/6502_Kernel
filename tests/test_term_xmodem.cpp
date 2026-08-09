/**
 * @file test_term_xmodem.cpp
 * @brief Headless end-to-end test of the terminal's XMODEM receive (^R).
 *
 * Runs the real TERM blob, queues the keystrokes that drive a `^R` receive of a
 * file named X.DAT, then plays the XMODEM *sender* from C++ over the ACIA (feed
 * CRC frames, read the 'C'/ACK handshake) -- the same harness shape as
 * test_acia_xmodem. Afterwards the received file is read back through the real
 * DOS FAT16 read path (FS_OPEN/FS_GETB) and compared to the payload.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "computer/Acia.h"
#include "computer/BlockDevice.h"
#include "computer/CPU6502.h"
#include "computer/Computer6502.h"
#include "computer/Memory.h"
#include "computer/PIA.h"
#include "computer/VIC.h"
#include "support/fat16_image.h"

#include <string>

using Computer::Computer6502;
using Computer::CPU6502;
using Computer::Memory;
using mfcdos_test::Fat16File;
using mfcdos_test::Fat16ImageBuilder;

namespace
{
    constexpr uint8_t SOH = 0x01, EOT = 0x04, ACK = 0x06;

    uint16_t crc16_xmodem(const uint8_t *d, size_t n)
    {
        uint16_t c = 0;
        for (size_t i = 0; i < n; ++i) {
            c ^= static_cast<uint16_t>(d[i]) << 8;
            for (int b = 0; b < 8; ++b)
                c = (c & 0x8000) ? static_cast<uint16_t>((c << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(c << 1);
        }
        return c;
    }

    // 133-byte CRC frame: SOH, blk, ~blk, 128 data, crc-hi, crc-lo.
    std::vector<uint8_t> makeFrame(uint8_t blk, const uint8_t *data128)
    {
        std::vector<uint8_t> f;
        f.push_back(SOH);
        f.push_back(blk);
        f.push_back(static_cast<uint8_t>(~blk));
        f.insert(f.end(), data128, data128 + 128);
        const uint16_t crc = crc16_xmodem(data128, 128);
        f.push_back(static_cast<uint8_t>(crc >> 8));
        f.push_back(static_cast<uint8_t>(crc & 0xFF));
        return f;
    }
}

class TermXmodemTest : public ::testing::Test
{
protected:
    Computer6502 computer;
    CPU6502 *cpu = nullptr;
    Memory *mem = nullptr;
    Computer::Acia *acia = nullptr;
    std::string image_path_;

    void SetUp() override
    {
        static int counter = 0;
        image_path_ = (std::filesystem::temp_directory_path() /
                       ("mfc_term_xmodem_" + std::to_string(++counter) + ".img")).string();

        computer.power_on();
        cpu = computer.getCpu();
        mem = computer.getMemory();
        acia = computer.getAcia();
        computer.getBlockDevice()->setImagePath(image_path_);

        std::ifstream f("../kernel/term.bin", std::ios::binary);
        ASSERT_TRUE(f.good()) << "term.bin not found - build the term_bin target";
        std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
        for (size_t i = 0; i < blob.size(); ++i)
            mem->write(static_cast<uint16_t>(0x0800 + i), blob[i]);
        cpu->reg.SP = 0xFF;
        cpu->pushByte(0xFF);
        cpu->pushByte(0xFF);
        cpu->reg.PC = 0x0800;

        /* Jumping straight to $0800 skips the kernel boot, and RESET leaves the I
         * flag set -- so the interval-timer IRQ step() pulses would never be taken
         * and the 60 Hz jiffy counter would stay at zero. The kernel CLIs before
         * handing off to a program; do the same here, or anything that waits on
         * elapsed time waits for ever. */
        cpu->setFlag(Computer::CPU6502::kInterrupt, false);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(image_path_, ec);
    }

    void writeImage(const std::vector<Fat16File> &files)
    {
        const std::vector<uint8_t> img = Fat16ImageBuilder::build(files);
        std::ofstream f(image_path_, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char *>(img.data()),
                static_cast<std::streamsize>(img.size()));
    }

    /* Step instructions, pulsing the 60 Hz interval timer as real time passes.
     *
     * On the machine the GUI pulses it; a harness that drives the CPU directly has to
     * do it itself or the jiffy counter never moves. Anything that waits on elapsed
     * time then waits forever -- which is exactly what TERM's title card did here the
     * moment it was added. Driven off the cycle counter, so it is a true 60 Hz rather
     * than an instruction-count guess. */
    static constexpr uint64_t kCyclesPerJiffy = 1000000 / 60;

    // One instruction, plus the interval timer if enough cycles have gone by. Every
    // loop that runs the CPU has to go through here, or the jiffy counter stalls
    // wherever that loop happens to be.
    bool cycle()
    {
        if (!cpu->executeSingleInstruction()) return false;
        if (cpu->getCycles() >= next_jiffy_) {
            next_jiffy_ = cpu->getCycles() + kCyclesPerJiffy;
            computer.getPia()->pulseTimerIrq();
        }
        return true;
    }

    void step(long n) { for (long i = 0; i < n; ++i) if (!cycle()) break; }

    uint64_t next_jiffy_ = kCyclesPerJiffy;

    // Read a file back through the real 6502 DOS FS (FS_OPEN read + FS_GETB).
    bool fsRead(const std::string &name, std::vector<uint8_t> &out)
    {
        const uint16_t nameAddr = 0x7000; // scratch clear of the abandoned TERM
        for (size_t i = 0; i < name.size(); ++i)
            mem->write(static_cast<uint16_t>(nameAddr + i), static_cast<uint8_t>(name[i]));
        mem->write(static_cast<uint16_t>(nameAddr + name.size()), 0);

        if (!callDos(0xAF03, nameAddr & 0xFF, nameAddr >> 8, 0)) return false; // FS_OPEN read
        out.clear();
        for (;;) {
            bool eof = false;
            const uint8_t b = callDosGetb(eof);
            if (eof) break;
            out.push_back(b);
        }
        callDos(0xAF0C, 0, 0, 0); // FS_CLOSE
        return true;
    }

    // Call a DOS routine to RTS; returns true if carry clear.
    bool callDos(uint16_t entry, uint8_t a, uint8_t x, uint8_t y)
    {
        cpu->reg.SP = 0xFF; cpu->pushByte(0xFF); cpu->pushByte(0xFF);
        cpu->reg.PC = entry; cpu->reg.A = a; cpu->reg.X = x; cpu->reg.Y = y;
        for (int i = 0; i < 3'000'000; ++i) {
            const uint16_t pc = cpu->reg.PC;
            if (pc < Memory::kDosRomStart || pc > Memory::kDosRomEnd)
                return !cpu->getFlag(CPU6502::kCarry);
            if (!cpu->executeSingleInstruction()) return false;
        }
        return false;
    }
    uint8_t callDosGetb(bool &eof)
    {
        cpu->reg.SP = 0xFF; cpu->pushByte(0xFF); cpu->pushByte(0xFF);
        cpu->reg.PC = 0xAF06; // FS_GETB
        for (int i = 0; i < 3'000'000; ++i) {
            const uint16_t pc = cpu->reg.PC;
            if (pc < Memory::kDosRomStart || pc > Memory::kDosRomEnd) {
                eof = cpu->getFlag(CPU6502::kCarry);
                return cpu->reg.A;
            }
            if (!cpu->executeSingleInstruction()) break;
        }
        eof = true; return 0;
    }
};

TEST_F(TermXmodemTest, ReceivesFileToFat16)
{
    writeImage({}); // empty disk to receive into

    // 256-byte payload = two full XMODEM blocks (no padding to reason about).
    std::vector<uint8_t> payload(256);
    for (int i = 0; i < 256; ++i) payload[i] = static_cast<uint8_t>(0x41 + (i % 26));
    const std::vector<uint8_t> f1 = makeFrame(1, &payload[0]);
    const std::vector<uint8_t> f2 = makeFrame(2, &payload[128]);

    // Queue the keys that drive: ^R, "X.DAT", RETURN.
    auto *pia = computer.getPia();
    pia->addKeypress(0x12); // Ctrl-R
    for (char ch : std::string("X.DAT")) pia->addKeypress(ch);
    pia->addKeypress(0x0D);

    // Play the XMODEM sender as the terminal drives the receive.
    int state = 0; // 0=await 'C', 1=sent f1, 2=sent f2, 3=sent EOT, 4=done
    for (int i = 0; i < 40'000'000 && state < 4; ++i) {
        if (!cycle()) break;
        while (acia->hostHasTx()) {
            const uint8_t t = acia->hostRecv();
            if (state == 0 && t == 'C') { for (uint8_t b : f1) acia->hostSend(b); state = 1; }
            else if (state == 1 && t == ACK) { for (uint8_t b : f2) acia->hostSend(b); state = 2; }
            else if (state == 2 && t == ACK) { acia->hostSend(EOT); state = 3; }
            else if (state == 3 && t == ACK) { state = 4; }
        }
    }
    EXPECT_EQ(state, 4) << "XMODEM receive handshake did not complete";

    step(2'000'000); // let do_recv close/flush the file

    std::vector<uint8_t> got;
    ASSERT_TRUE(fsRead("X.DAT", got)) << "received file not found on the disk";
    ASSERT_EQ(got.size(), payload.size());
    EXPECT_EQ(got, payload);
}

// ^D loads DIAL.LST, shows a numbered menu, and a digit dials that entry's
// address: the terminal must transmit "ATDT <addr>\r" for the chosen line.
TEST_F(TermXmodemTest, DialListMenuDialsChosenEntry)
{
    auto bytesOf = [](const std::string &s) {
        return std::vector<uint8_t>(s.begin(), s.end());
    };
    // Two entries with distinctive addresses; a comment and blank line to skip.
    // The dial-list lives in the SYSTEM drawer (TERM opens SYSTEM/DIAL.LST).
    writeImage({{"DIAL.LST", bytesOf("# my boards\r\n"
                                     "test.bbs.one:1234  First Board\r\n"
                                     "\r\n"
                                     "host.two:2323  Second Board\r\n"), "SYSTEM"}});

    // ^D opens the menu; "2" picks the second entry.
    auto *pia = computer.getPia();
    pia->addKeypress(0x04); // Ctrl-D
    pia->addKeypress('2');

    // Run, draining anything the terminal transmits into a string.
    std::string tx;
    for (int i = 0; i < 12'000'000; ++i) {
        if (!cycle()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (tx.find('\r') != std::string::npos) break; // dial line sent
    }

    // The menu rendered (scrape the screen for an entry's display name)...
    std::string screen;
    auto *vic = computer.getVideoChip();
    for (int y = 0; y < 25; ++y)
        for (int x = 0; x < 80; ++x)
            screen += static_cast<char>(vic->getCharacterAt(x, y));
    EXPECT_NE(screen.find("Saved BBSes"), std::string::npos) << "dial menu header missing";
    EXPECT_NE(screen.find("First Board"), std::string::npos) << "entry 1 name missing";

    // ...and choosing entry 2 dialed its address.
    EXPECT_EQ(tx, "ATDT host.two:2323\r") << "got: " << tx;
}
