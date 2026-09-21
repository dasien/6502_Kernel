/**
 * @file test_gopher.cpp
 * @brief Headless end-to-end test of the Gopher client (programs/gopher).
 *
 * Runs the real GOPHER blob and stands in for the modem and a Gopher server
 * over the ACIA: answers the host prompt from the keyboard, raises /DCD as
 * the modem adapter does, then feeds a canned menu or text file and asserts on
 * what is rendered. GOPHER sets ATQ1, so a real modem emits no result-code
 * text and carrier is the whole signal.
 *
 * What it pins, in rough order of how easily each could regress unnoticed:
 *   - RFC 1436 period-unstuffing. A line that really begins with "." is sent
 *     doubled so it cannot end the transfer early, and the client must strip
 *     the extra one. This shipped broken and nothing would have caught it.
 *   - Menu lines are split on tabs, so only the display text reaches the
 *     screen and the selector, host and port do not.
 *   - The response terminator is a lone "." and stops the read.
 *   - Following a link sends the selected item's selector, not its label.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "computer/Computer6502.h"
#include "computer/ACIA.h"
#include "computer/PIA.h"
#include "computer/VIC.h"
#include "computer/CPU6502.h"
#include "computer/Memory.h"
#include "computer/BlockDevice.h"
#include "support/fat16_image.h"

using Computer::Computer6502;
using mfcdos_test::Fat16File;
using mfcdos_test::Fat16ImageBuilder;

class GopherTest : public ::testing::Test
{
protected:
    Computer6502 c;
    Computer::ACIA *acia = nullptr;
    Computer::CPU6502 *cpu = nullptr;
    Computer::Memory *mem = nullptr;

    void SetUp() override
    {
        c.power_on();
        acia = c.getAcia();
        cpu = c.getCpu();
        mem = c.getMemory();

        std::ifstream f("../kernel/gopher.bin", std::ios::binary);
        ASSERT_TRUE(f.good()) << "gopher.bin not found - build the gopher_bin target";
        std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
        ASSERT_GE(blob.size(), 0x100u);
        for (size_t i = 0; i < blob.size(); ++i)
            mem->write(static_cast<uint16_t>(0x0800 + i), blob[i]);
        cpu->reg.SP = 0xFF;
        cpu->pushByte(0xFF);
        cpu->pushByte(0xFF);
        cpu->reg.PC = 0x0800;

        /* Jumping straight to $0800 skips the kernel boot, which leaves I set --
         * so the timer IRQ never fires and the jiffy counter never moves. The
         * title card waits on it, so without this the client never starts. */
        cpu->setFlag(Computer::CPU6502::kInterrupt, false);
    }

    static constexpr uint64_t kCyclesPerJiffy =
        Computer::Computer6502::kDefaultClockHz / Computer::Computer6502::kJiffyHz;
    uint64_t next_jiffy_ = kCyclesPerJiffy;

    bool cycle()
    {
        if (!cpu->executeSingleInstruction()) return false;
        if (cpu->getCycles() >= next_jiffy_) {
            next_jiffy_ = cpu->getCycles() + kCyclesPerJiffy;
            c.getPia()->pulseTimerIrq();
        }
        return true;
    }

    std::string image_path_;
    void TearDown() override
    {
        if (!image_path_.empty()) { std::error_code ec; std::filesystem::remove(image_path_, ec); }
    }

    /* A disk is always present on the real machine. Mount an empty one so the
     * SYSTEM/GOPHER.LST lookup fails cleanly and the client falls through to
     * the host prompt, rather than reading an unmounted device. */
    void mountDisk(const std::vector<Fat16File> &files)
    {
        static int counter = 0;
        image_path_ = (std::filesystem::temp_directory_path() /
                       ("mfc_gopher_" + std::to_string(++counter) + ".img")).string();
        const std::vector<uint8_t> img = Fat16ImageBuilder::build(files);
        std::ofstream f(image_path_, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char *>(img.data()),
                static_cast<std::streamsize>(img.size()));
        f.close();
        c.getBlockDevice()->setImagePath(image_path_);
    }

    void type(const std::string &s)
    {
        for (char ch : s) c.getPia()->addKeypress(static_cast<uint8_t>(ch));
    }

    void send(const std::string &s)
    {
        for (char ch : s) acia->hostSend(static_cast<uint8_t>(ch));
    }

    /* This fixture stands in for the modem, so it drives /DCD the way the real
     * Modem adapter does on socket connect and close. The client keys off
     * carrier, not off the CONNECT text, so asserting it is what makes a dial
     * complete. */
    void carrier(bool up) { acia->setCarrier(up); }

    std::string screen()
    {
        std::string s;
        auto *vic = c.getVideoChip();
        for (int y = 0; y < 25; ++y)
            for (int x = 0; x < 80; ++x)
                s += static_cast<char>(vic->getCharacterAt(x, y));
        return s;
    }

    /* Everything the 6502 has transmitted this test. Both wait helpers append
     * to it, so a byte sent while waiting on the screen is not lost -- which it
     * was when only runUntilTx drained the FIFO. */
    std::string tx;

    bool runUntilTx(const std::string &want, int budget = 60'000'000)
    {
        for (int i = 0; i < budget; ++i) {
            if (!cycle()) return false;
            while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
            if (tx.find(want) != std::string::npos) return true;
        }
        return false;
    }

    /* Run until `want` appears on screen. Waiting on the rendered result rather
     * than a fixed instruction count keeps this honest: the client spends most
     * of its time in the title card and the post-transfer line drain, and a
     * magic number tuned to today's timings would rot the first time either
     * changed. Returns false if it never appears. */
    bool runUntilScreen(const std::string &want, int budget = 60'000'000)
    {
        for (int i = 0; i < budget; ++i) {
            if (!cycle()) return false;
            while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
            if ((i & 0xFFFF) == 0 && screen().find(want) != std::string::npos) return true;
        }
        return screen().find(want) != std::string::npos;
    }

    /* Wait for the fetch to FINISH, not merely for some item to appear. The
     * status line carries the key hints only once the client is back at the
     * menu, so it is the completion signal; waiting on a menu entry races the
     * rest of the response still arriving -- which is exactly what surfaced
     * when the 65C02 build made the client faster. */
    bool runUntilIdle() { return runUntilScreen("Enter=open"); }
};

TEST_F(GopherTest, DialsSendsSelectorAndRendersAMenu)
{
    mountDisk({});
    type("test.gopher\r");                       // host prompt

    ASSERT_TRUE(runUntilTx( "ATDT test.gopher:70")) << tx;
    carrier(true);   // GOPHER sets ATQ1, so a real modem sends no text

    /* A menu: an info line, a submenu, a text file. Tab-separated
       <type><display> TAB selector TAB host TAB port, ended by a lone ".". */
    send("iWelcome to the hole\tfake\terror.host\t1\r\n"
         "1Sub menu here\t/sub\ttest.gopher\t70\r\n"
         "0A document\t/doc.txt\ttest.gopher\t70\r\n"
         ".\r\n");
    ASSERT_TRUE(runUntilIdle()) << screen();

    const std::string s = screen();
    // Display text is rendered; the selector, host and port are not.
    EXPECT_NE(s.find("Welcome to the hole"), std::string::npos) << s;
    EXPECT_NE(s.find("Sub menu here"), std::string::npos) << s;
    EXPECT_NE(s.find("A document"), std::string::npos) << s;
    EXPECT_EQ(s.find("error.host"), std::string::npos) << "raw menu fields leaked to the screen";
    EXPECT_EQ(s.find("/doc.txt"), std::string::npos) << "selector leaked into the display";
}

/* RFC 1436: "Lines beginning with periods must be prepended with an extra
 * period... The client should strip extra periods at the beginning of the
 * line." The client shipped without doing this.
 *
 * It has to be exercised through a TEXT file. A menu line's first byte is the
 * item type, so a menu line can never legitimately begin with a period and the
 * stuffing has nowhere to appear; in a document the line content is the whole
 * line, which is where it does. */
TEST_F(GopherTest, StripsTheStuffedLeadingPeriodInATextFile)
{
    mountDisk({});
    type("test.gopher\r");

    ASSERT_TRUE(runUntilTx( "ATDT test.gopher:70")) << tx;
    carrier(true);   // GOPHER sets ATQ1, so a real modem sends no text
    send("0A document\t/doc.txt\ttest.gopher\t70\r\n"
         ".\r\n");
    ASSERT_TRUE(runUntilIdle()) << screen();

    // Open it: a new call, so a new dial and carrier again.
    tx.clear();
    type("\r");
    ASSERT_TRUE(runUntilTx("ATDT test.gopher:70")) << tx;
    carrier(true);   // GOPHER sets ATQ1, so a real modem sends no text
    ASSERT_TRUE(runUntilTx("/doc.txt")) << tx;

    send("plain line\r\n"
         "..dotted line here\r\n"
         ".\r\n");
    ASSERT_TRUE(runUntilIdle()) << screen();

    const std::string s = screen();
    EXPECT_NE(s.find(".dotted line here"), std::string::npos) << s;
    EXPECT_EQ(s.find("..dotted line here"), std::string::npos)
        << "the stuffed period was not stripped";
    EXPECT_NE(s.find("plain line"), std::string::npos) << s;
}

/* Type 9: a binary download. Two things are being pinned. The client must ask
 * the modem for raw mode first, because the telnet filter would eat a $FF and
 * the byte after it out of the file. And the transfer has to end on carrier
 * dropping -- RFC 1436 gives binary no terminator, so there is nothing in the
 * data to look for, and a $FF in the payload proves the filter is really off. */
TEST_F(GopherTest, BinaryItemGoesToRawModeAndEndsOnCarrierLoss)
{
    mountDisk({});
    type("test.gopher\r");

    ASSERT_TRUE(runUntilTx( "ATDT test.gopher:70")) << tx;
    carrier(true);   // GOPHER sets ATQ1, so a real modem sends no text
    send("9An archive\t/pub/thing.zip\ttest.gopher\t70\r\n"
         ".\r\n");
    ASSERT_TRUE(runUntilIdle()) << screen();

    // Enter opens the save prompt, pre-filled from the selector.
    tx.clear();
    type("\r");
    ASSERT_TRUE(runUntilScreen("Save as")) << screen();
    EXPECT_NE(screen().find("THING.ZIP"), std::string::npos)
        << "the 8.3 name was not derived from the selector";

    type("\r");                                  // accept the derived name
    ASSERT_TRUE(runUntilTx("ATB1")) << "raw mode was not requested: " << tx;
    ASSERT_TRUE(runUntilTx("ATDT test.gopher:70")) << tx;
    carrier(true);
    ASSERT_TRUE(runUntilTx("/pub/thing.zip")) << tx;

    // A payload containing $FF, which the telnet filter would have mangled.
    const uint8_t body[] = {0x50, 0x4B, 0xFF, 0x00, 0xFF, 0xFF, 0x1A};
    for (uint8_t b : body) acia->hostSend(b);
    carrier(false);                               // the server closes: end of file
    ASSERT_TRUE(runUntilScreen("Saved")) << screen();

    // And it returned the modem to telnet framing afterwards.
    EXPECT_NE(tx.find("ATB0"), std::string::npos) << tx;
}

/* ESC must abandon a download that is still receiving. The read loop used to
 * poll the keyboard only when the ACIA FIFO ran dry, which on a fast transfer
 * it never does -- so a large file could not be abandoned at all. */
TEST_F(GopherTest, EscAbortsADownloadWhileBytesAreStillArriving)
{
    mountDisk({});
    type("test.gopher\r");

    ASSERT_TRUE(runUntilTx("ATDT test.gopher:70")) << tx;
    carrier(true);
    send("9An archive\t/pub/thing.zip\ttest.gopher\t70\r\n"
         ".\r\n");
    ASSERT_TRUE(runUntilIdle()) << screen();

    type("\r");                                   // open it
    ASSERT_TRUE(runUntilScreen("Save as")) << screen();
    type("\r");                                   // accept the derived name
    ASSERT_TRUE(runUntilTx("/pub/thing.zip")) << tx;

    /* Keep the FIFO fed but not flooded: one byte every 40 instructions, which
       is faster than the client can write them to FAT16, so it falls behind and
       the FIFO never empties. That is the condition a real download creates and
       the one the old code could not escape -- with the keyboard polled only on
       the empty path, that path is never reached. Carrier stays up throughout,
       so ESC is the only way out. */
    c.getPia()->addKeypress(0x1B);
    bool cancelled = false;
    for (int i = 0; i < 8'000'000 && !cancelled; ++i) {
        /* Feed until the client hangs up, then stop -- a real modem drops the
           line and the bytes stop with it. Keeping the fixture talking past
           the hangup would leave the client's post-transfer drain spinning for
           ever, which is a property of this stub and not of the client. */
        if (tx.find("ATH") == std::string::npos)
            acia->hostSend(static_cast<uint8_t>(i & 0xFF));
        if (!cycle()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if ((i & 0xFFF) == 0 && screen().find("Cancelled") != std::string::npos)
            cancelled = true;
    }
    ASSERT_TRUE(cancelled) << "ESC never took effect while data was arriving\n" << screen();
    EXPECT_TRUE(acia->carrier()) << "the test never dropped carrier; ESC ended it";
}

TEST_F(GopherTest, FollowingALinkSendsItsSelector)
{
    mountDisk({});
    type("test.gopher\r");

    ASSERT_TRUE(runUntilTx( "ATDT test.gopher:70")) << tx;
    carrier(true);   // GOPHER sets ATQ1, so a real modem sends no text
    send("1Sub menu here\t/sub/thing\ttest.gopher\t70\r\n"
         ".\r\n");
    ASSERT_TRUE(runUntilIdle()) << screen();

    /* Following a link is a whole new call -- Gopher closes after every
     * response -- so the client dials again and needs carrier again. */
    tx.clear();
    type("\r");                                  // Enter on the first selectable item
    ASSERT_TRUE(runUntilTx("ATDT test.gopher:70")) << tx;
    carrier(true);   // GOPHER sets ATQ1, so a real modem sends no text
    ASSERT_TRUE(runUntilTx("/sub/thing")) << tx;
    // It sent the selector, not the label it was displaying.
    EXPECT_EQ(tx.find("Sub menu here"), std::string::npos) << tx;
}
