/**
 * @file test_irc.cpp
 * @brief Headless end-to-end test of the IRC chat client (programs/irc).
 *
 * Runs the real IRC blob, plays the connect handshake from C++ over the ACIA
 * (the test stands in for the modem + server): answers the Server/Nick/Channel
 * prompts via the keyboard, injects the modem's CONNECT, then feeds IRC server
 * lines. Asserts the client's transmitted registration (NICK/USER/JOIN), its
 * PING->PONG keepalive, and that an inbound PRIVMSG renders as "<nick> text".
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "computer/Computer6502.h"
#include "computer/Acia.h"
#include "computer/PIA.h"
#include "computer/VIC.h"
#include "computer/CPU6502.h"
#include "computer/Memory.h"
#include "computer/BlockDevice.h"
#include "support/fat16_image.h"

using Computer::Computer6502;
using mfcdos_test::Fat16File;
using mfcdos_test::Fat16ImageBuilder;

class IrcTest : public ::testing::Test
{
protected:
    Computer6502 c;
    Computer::Acia *acia = nullptr;
    Computer::CPU6502 *cpu = nullptr;
    Computer::Memory *mem = nullptr;

    void SetUp() override
    {
        c.power_on();
        acia = c.getAcia();
        cpu = c.getCpu();
        mem = c.getMemory();

        std::ifstream f("../kernel/irc.bin", std::ios::binary);
        ASSERT_TRUE(f.good()) << "irc.bin not found - build the irc_bin target";
        std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
        ASSERT_GE(blob.size(), 0x100u);
        for (size_t i = 0; i < blob.size(); ++i)
            mem->write(static_cast<uint16_t>(0x0800 + i), blob[i]);
        cpu->reg.SP = 0xFF;
        cpu->pushByte(0xFF);
        cpu->pushByte(0xFF);
        cpu->reg.PC = 0x0800;
    }

    std::string image_path_;
    void TearDown() override
    {
        if (!image_path_.empty()) { std::error_code ec; std::filesystem::remove(image_path_, ec); }
    }

    // Mount a FAT16 disk so the IRC client can read IRC.LST.
    void mountDisk(const std::vector<Fat16File> &files)
    {
        static int counter = 0;
        image_path_ = (std::filesystem::temp_directory_path() /
                       ("mfc_irc_" + std::to_string(++counter) + ".img")).string();
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

    std::string screen()
    {
        std::string s;
        auto *vic = c.getVideoChip();
        for (int y = 0; y < 25; ++y)
            for (int x = 0; x < 80; ++x)
                s += static_cast<char>(vic->getCharacterAt(x, y));
        return s;
    }
};

TEST_F(IrcTest, RegistersDisplaysAndPongs)
{
    // A disk is always present on the real machine; mount an empty one so the
    // "no SYSTEM/IRC.LST" lookup fails cleanly (server menu skipped -> manual
    // prompt) rather than reading an unmounted device.
    mountDisk({});

    // Answer the Server / Nick / Channel prompts (RETURN-terminated).
    type("test.irc:6667\r");
    type("mfc\r");
    type("#t\r");

    std::string tx;
    bool connected = false, fed = false;
    for (int i = 0; i < 60'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());

        if (!connected && tx.find("ATDT test.irc:6667") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n"))
                acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !fed && tx.find("JOIN #t") != std::string::npos) {
            for (char ch : std::string(":bob!u@h PRIVMSG #t :hello there\r\n"
                                       "PING :xyz\r\n"))
                acia->hostSend(static_cast<uint8_t>(ch));
            fed = true;
        }
        if (fed && tx.find("PONG :xyz") != std::string::npos) break; // full token drained
    }

    // Registration the client sent on connect.
    EXPECT_NE(tx.find("ATDT test.irc:6667"), std::string::npos) << tx;
    EXPECT_NE(tx.find("NICK mfc"), std::string::npos) << tx;
    EXPECT_NE(tx.find("USER mfc 0 * :mfc"), std::string::npos) << tx;
    EXPECT_NE(tx.find("JOIN #t"), std::string::npos) << tx;
    // Keepalive: PING :xyz -> PONG :xyz.
    EXPECT_NE(tx.find("PONG :xyz"), std::string::npos) << tx;
    // The inbound PRIVMSG rendered into the chat region.
    EXPECT_NE(screen().find("<bob> hello there"), std::string::npos);
}

// v1.1: event rendering (JOIN / CTCP ACTION), CTCP VERSION reply, nick-in-use
// (433) auto-retry, and the status bar showing online state.
TEST_F(IrcTest, RendersEventsAndHandlesNickInUse)
{
    mountDisk({});
    type("test.irc:6667\r");
    type("mfc\r");
    type("#t\r");

    std::string tx;
    bool connected = false, fed = false;
    for (int i = 0; i < 60'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT test.irc:6667") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !fed && tx.find("JOIN #t") != std::string::npos) {
            const std::string ev =
                ":alice!u@h JOIN #t\r\n"
                ":alice!u@h PRIVMSG #t :\001ACTION waves\001\r\n"
                ":bob!u@h PRIVMSG mfc :\001VERSION\001\r\n"
                ":srv 252 mfc 5 :operator(s) online\r\n"    // numeric w/ a count param
                ":srv 433 * mfc :Nickname is in use\r\n";
            for (char ch : ev) acia->hostSend(static_cast<uint8_t>(ch));
            fed = true;
        }
        if (fed && tx.find("NICK mfc_") != std::string::npos) break; // 433 retry sent
    }
    // The 433 handler sends NICK (what we broke on) and only *then* paints its
    // notice; step a little more so that repaint lands before we scrape.
    for (int i = 0; i < 2'000'000; ++i) if (!cpu->executeSingleInstruction()) break;

    const std::string s = screen();
    EXPECT_NE(s.find("* alice joined #t"), std::string::npos);        // JOIN event
    EXPECT_NE(s.find("* alice waves"), std::string::npos);            // CTCP ACTION
    EXPECT_NE(s.find("* nick in use, trying mfc_"), std::string::npos);
    EXPECT_NE(s.find("5 operator(s) online"), std::string::npos);     // numeric param kept
    EXPECT_NE(s.find("[online]"), std::string::npos);                 // status bar
    EXPECT_NE(tx.find("NOTICE bob :\001VERSION MFC IRC 1.1\001"), std::string::npos) << tx;
    EXPECT_NE(tx.find("NICK mfc_"), std::string::npos) << tx;
}

TEST_F(IrcTest, ServerMenuPicksFromList)
{
    auto bytesOf = [](const std::string &s) {
        return std::vector<uint8_t>(s.begin(), s.end());
    };
    // The server list lives in the SYSTEM drawer (IRC opens SYSTEM/IRC.LST).
    mountDisk({{"IRC.LST", bytesOf("# my nets\r\n"
                                   "alpha.irc:6667  Alpha Net\r\n"
                                   "beta.irc:7000  Beta Net\r\n"), "SYSTEM"}});

    c.getPia()->addKeypress('2');   // pick the 2nd server from the menu
    type("ircuser\r");              // Nick
    type("#x\r");                   // Channel

    std::string tx;
    bool connected = false;
    for (int i = 0; i < 60'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT beta.irc:7000") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && tx.find("JOIN #x") != std::string::npos) break;
    }

    const std::string s = screen();
    EXPECT_NE(s.find("IRC servers:"), std::string::npos);
    EXPECT_NE(s.find("Beta Net"), std::string::npos);          // menu rendered
    EXPECT_NE(tx.find("ATDT beta.irc:7000"), std::string::npos) << tx;  // dialed pick #2
    EXPECT_NE(tx.find("NICK ircuser"), std::string::npos) << tx;
}
