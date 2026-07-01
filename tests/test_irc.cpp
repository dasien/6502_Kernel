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
    // Repaint is coalesced (fires when RX goes idle); step so it lands before we scrape.
    for (int i = 0; i < 1'000'000; ++i) if (!cpu->executeSingleInstruction()) break;

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
                ":al!u@h PRIVMSG #t :\003" "4RED\017norm\r\n" // mIRC colour 4 (red)
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

    // mIRC colour honoured: "RED" cells carry IRC colour 4 (red) -> our bright
    // red fg (0x41); the "norm" after ^O reset is back to default (0x02).
    size_t rp = s.find("RED");
    ASSERT_NE(rp, std::string::npos);
    auto *vic = c.getVideoChip();
    EXPECT_EQ(vic->getColorAt(rp % 80, rp / 80), 0x41);
    size_t np = s.find("norm");
    ASSERT_NE(np, std::string::npos);
    EXPECT_EQ(vic->getColorAt(np % 80, np / 80), 0x02);
    EXPECT_NE(tx.find("NOTICE bob :\001VERSION MFC IRC 1.4\001"), std::string::npos) << tx;
    EXPECT_NE(tx.find("NICK mfc_"), std::string::npos) << tx;
}

// v1.2 channel commands: bare /names and /part act on the current channel.
TEST_F(IrcTest, ChannelCommandsTransmitProperIrc)
{
    mountDisk({});
    type("test.irc:6667\r");
    type("mfc\r");
    type("#t\r");

    std::string tx;
    bool connected = false, sent = false;
    for (int i = 0; i < 60'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT test.irc:6667") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !sent && tx.find("JOIN #t") != std::string::npos) {
            for (char ch : std::string("/names\r/whois bob\r/part\r"))
                c.getPia()->addKeypress(static_cast<uint8_t>(ch));
            sent = true;
        }
        if (sent && tx.find("PART #t") != std::string::npos) break;
    }

    EXPECT_NE(tx.find("NAMES #t"), std::string::npos) << tx;   // bare /names -> current channel
    EXPECT_NE(tx.find("WHOIS bob"), std::string::npos) << tx;
    EXPECT_NE(tx.find("PART #t"), std::string::npos) << tx;    // bare /part -> current channel
}

// /server disconnects (QUIT + modem hangup) so the app can return to the dial
// screen, instead of exiting to DOS like /quit.
TEST_F(IrcTest, ServerCommandDisconnects)
{
    mountDisk({});
    type("test.irc:6667\r");
    type("mfc\r");
    type("#t\r");

    std::string tx;
    bool connected = false, sent = false;
    for (int i = 0; i < 60'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT test.irc:6667") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !sent && tx.find("JOIN #t") != std::string::npos) {
            for (char ch : std::string("/server\r")) c.getPia()->addKeypress(static_cast<uint8_t>(ch));
            sent = true;
        }
        if (sent && tx.find("+++ATH") != std::string::npos) break;   // hung up
    }

    EXPECT_NE(tx.find("QUIT :changing servers"), std::string::npos) << tx;
    EXPECT_NE(tx.find("+++ATH"), std::string::npos) << tx;           // modem hangup issued
}

// /disconnect is an alias for /server: same QUIT + hang up + return to dial.
TEST_F(IrcTest, DisconnectAliasHangsUp)
{
    mountDisk({});
    type("test.irc:6667\r"); type("mfc\r"); type("#t\r");

    std::string tx;
    bool connected = false, sent = false;
    for (int i = 0; i < 60'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT test.irc:6667") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !sent && tx.find("JOIN #t") != std::string::npos) {
            for (char ch : std::string("/disconnect\r")) c.getPia()->addKeypress(static_cast<uint8_t>(ch));
            sent = true;
        }
        if (sent && tx.find("+++ATH") != std::string::npos) break;
    }

    EXPECT_NE(tx.find("QUIT :changing servers"), std::string::npos) << tx;
    EXPECT_NE(tx.find("+++ATH"), std::string::npos) << tx;
}

// /list sends the LIST command (with filter) and renders the server's replies.
TEST_F(IrcTest, ListCommandSendsAndRenders)
{
    mountDisk({});
    type("test.irc:6667\r");
    type("mfc\r");
    type("#t\r");

    std::string tx;
    bool connected = false, listed = false;
    long flush = 0;
    for (int i = 0; i < 60'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT test.irc:6667") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !listed && tx.find("JOIN #t") != std::string::npos) {
            for (char ch : std::string("/list >100\r")) c.getPia()->addKeypress(static_cast<uint8_t>(ch));
            listed = true;
        }
        if (listed && tx.find("LIST >100") != std::string::npos) {   // command went out; feed a reply
            for (char ch : std::string(":srv 322 mfc #cool 42 :a cool channel\r\n"))
                acia->hostSend(static_cast<uint8_t>(ch));
            if (++flush > 2'000'000) break;
        }
    }

    EXPECT_NE(tx.find("LIST >100"), std::string::npos) << tx;   // filter passed through
    EXPECT_NE(screen().find("#cool 42 a cool channel"), std::string::npos); // 322 reply rendered
}

// A burst of many LIST replies must leave the screen with each entry intact on
// its own row (no merged/overlapping lines).
TEST_F(IrcTest, ListBurstRowsStayIntact)
{
    mountDisk({});
    type("test.irc:6667\r"); type("mfc\r"); type("#t\r");

    std::string tx;
    bool connected = false, fed = false;
    long flush = 0;
    for (int i = 0; i < 80'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !fed && tx.find("JOIN #t") != std::string::npos) {
            std::string ev;
            for (int c = 0; c < 30; ++c)                    // long (wrapping) topics
                ev += ":srv 322 mfc #chan" + std::to_string(c) + " " + std::to_string(c) +
                      " :topic " + std::string(90, 'x' + (c % 20)) + "\r\n";
            for (char ch : ev) acia->hostSend(static_cast<uint8_t>(ch));
            fed = true;
        }
        if (fed && ++flush > 5'000'000) break;
    }

    // No chat row should contain two channel headers (that would be a merge).
    auto *vic = c.getVideoChip();
    for (int y = 0; y < 23; ++y) {
        std::string row;
        for (int x = 0; x < 80; ++x) row += static_cast<char>(vic->getCharacterAt(x, y));
        size_t first = row.find("#chan");
        if (first != std::string::npos)
            EXPECT_EQ(row.find("#chan", first + 1), std::string::npos)
                << "row " << y << " has two channel headers: [" << row << "]";
    }
}

// The newest line sits on the bottom chat row (row 22), earlier lines just
// above it, each at column 0 (no mid-screen offset / gaps).
TEST_F(IrcTest, NewestLineAtBottomRow)
{
    mountDisk({});
    type("test.irc:6667\r"); type("mfc\r"); type("#t\r");

    std::string tx;
    bool connected = false, fed = false;
    long flush = 0;
    for (int i = 0; i < 60'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !fed && tx.find("JOIN #t") != std::string::npos) {
            for (char ch : std::string("NOTICE AUTH :one\r\nNOTICE AUTH :two\r\nNOTICE AUTH :three\r\n"))
                acia->hostSend(static_cast<uint8_t>(ch));
            fed = true;
        }
        if (fed && ++flush > 2'000'000) break;
    }

    auto *vic = c.getVideoChip();
    auto rowText = [&](int y) { std::string r; for (int x = 0; x < 80; ++x) r += (char)vic->getCharacterAt(x, y); return r; };
    EXPECT_EQ(rowText(22).rfind("* three", 0), 0u); // newest at row 22, col 0
    EXPECT_EQ(rowText(21).rfind("* two", 0), 0u);   // previous at row 21, col 0
    EXPECT_EQ(rowText(20).rfind("* one", 0), 0u);   // and row 20
}

// Scrollback: after enough lines to overflow the 23-row chat region, PgUp
// (ESC[5~) pages back to lines that scrolled off the top; End (ESC[F) snaps
// back to the live tail (newest line on the bottom chat row).
TEST_F(IrcTest, ScrollbackPageUpShowsOlderLines)
{
    mountDisk({});
    type("test.irc:6667\r"); type("mfc\r"); type("#t\r");

    // 40 distinct lines > 23 rows, so line01..line17 scroll off the top.
    std::string feed;
    for (int n = 1; n <= 40; ++n) {
        std::string nn = (n < 10 ? "0" : "") + std::to_string(n);
        feed += "NOTICE AUTH :line" + nn + "\r\n";
    }

    std::string tx;
    bool connected = false, fed = false;
    long flush = 0;
    for (int i = 0; i < 80'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !fed && tx.find("JOIN #t") != std::string::npos) {
            for (char ch : feed) acia->hostSend(static_cast<uint8_t>(ch));
            fed = true;
        }
        if (fed && ++flush > 5'000'000) break;
    }

    auto *vic = c.getVideoChip();
    auto rowText = [&](int y) { std::string r; for (int x = 0; x < 80; ++x) r += (char)vic->getCharacterAt(x, y); return r; };
    auto statusRow = [&]() { return rowText(24); };

    // Live tail: the newest line sits on the bottom chat row, oldest is gone.
    ASSERT_EQ(rowText(22).rfind("* line40", 0), 0u) << rowText(22);
    EXPECT_EQ(screen().find("* line01"), std::string::npos);   // scrolled off

    // Page up: ESC[5~ walks the view back to the oldest retained lines.
    type("\x1b[5~");
    for (int i = 0; i < 2'000'000; ++i) if (!cpu->executeSingleInstruction()) break;
    EXPECT_NE(screen().find("* line01"), std::string::npos) << "PgUp should reveal line01";
    EXPECT_NE(statusRow().find("review"), std::string::npos) << statusRow();

    // End: ESC[F snaps back to the live tail; line40 is on the bottom row again.
    type("\x1b[F");
    for (int i = 0; i < 2'000'000; ++i) if (!cpu->executeSingleInstruction()) break;
    EXPECT_EQ(rowText(22).rfind("* line40", 0), 0u) << rowText(22);
    EXPECT_EQ(statusRow().find("review"), std::string::npos) << statusRow();
}

// While reviewing, a newly arrived line must not disturb the frozen view: it
// queues into history (status shows the review indicator) instead of scrolling.
TEST_F(IrcTest, ReviewHoldsWhileNewLinesArrive)
{
    mountDisk({});
    type("test.irc:6667\r"); type("mfc\r"); type("#t\r");

    std::string feed;
    for (int n = 1; n <= 40; ++n) {
        std::string nn = (n < 10 ? "0" : "") + std::to_string(n);
        feed += "NOTICE AUTH :line" + nn + "\r\n";
    }

    std::string tx;
    bool connected = false, fed = false;
    long flush = 0;
    for (int i = 0; i < 80'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !fed && tx.find("JOIN #t") != std::string::npos) {
            for (char ch : feed) acia->hostSend(static_cast<uint8_t>(ch));
            fed = true;
        }
        if (fed && ++flush > 5'000'000) break;
    }

    // Enter review at the very top.
    type("\x1b[5~");
    for (int i = 0; i < 2'000'000; ++i) if (!cpu->executeSingleInstruction()) break;
    auto *vic = c.getVideoChip();
    auto rowText = [&](int y) { std::string r; for (int x = 0; x < 80; ++x) r += (char)vic->getCharacterAt(x, y); return r; };
    ASSERT_NE(screen().find("* line01"), std::string::npos);
    const std::string top_before = rowText(0);

    // A fresh line arrives while reviewing: it must not appear on screen or move
    // the view; it only queues below (the [review +N] indicator still shows).
    for (char ch : std::string("NOTICE AUTH :freshline\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
    for (int i = 0; i < 2'000'000; ++i) if (!cpu->executeSingleInstruction()) break;

    EXPECT_EQ(rowText(0), top_before) << "review view moved";
    EXPECT_EQ(screen().find("freshline"), std::string::npos) << "new line leaked onto the frozen view";
    EXPECT_NE(rowText(24).find("review"), std::string::npos) << rowText(24);
}

// UTF-8 down-convert (nbsp -> space) and long lines wrap instead of truncating.
TEST_F(IrcTest, FoldsUtf8AndWrapsLongLines)
{
    mountDisk({});
    type("test.irc:6667\r");
    type("mfc\r");
    type("#t\r");

    std::string tx;
    bool connected = false, fed = false;
    long flush = 0;
    for (int i = 0; i < 60'000'000; ++i) {
        if (!cpu->executeSingleInstruction()) break;
        while (acia->hostHasTx()) tx += static_cast<char>(acia->hostRecv());
        if (!connected && tx.find("ATDT test.irc:6667") != std::string::npos) {
            for (char ch : std::string("CONNECT\r\n")) acia->hostSend(static_cast<uint8_t>(ch));
            connected = true;
        }
        if (connected && !fed && tx.find("JOIN #t") != std::string::npos) {
            // "A<nbsp>B" (U+00A0 = C2 A0) should render "A B", not "A??B"; and a
            // >80-char message must wrap so its tail ("END") still appears.
            std::string ev = ":al!u@h PRIVMSG #t :A\xC2\xA0""B\r\n";
            ev += ":al!u@h PRIVMSG #t :" + std::string(80, 'X') + "END\r\n";
            ev += ":al!u@h PRIVMSG #t :gap" + std::string(8, ' ') + "here\r\n"; // space run
            // mIRC formatting: ^C4,1 (colour) + ^B (bold) around "RedBold".
            ev += ":al!u@h PRIVMSG #t :\x03""4,1\x02""RedBold\x0f done\r\n";
            for (char ch : ev) acia->hostSend(static_cast<uint8_t>(ch));
            fed = true;
        }
        if (fed && ++flush > 3'000'000) break;   // let both messages render
    }

    const std::string s = screen();
    EXPECT_NE(s.find("A B"), std::string::npos);          // nbsp folded to a space
    EXPECT_EQ(s.find("\xC2"), std::string::npos);         // no raw UTF-8 byte on screen
    EXPECT_NE(s.find("END"), std::string::npos);          // long line wrapped, tail kept
    EXPECT_NE(s.find("gap here"), std::string::npos);     // run of spaces collapsed to one
    EXPECT_EQ(s.find("gap  here"), std::string::npos);    // (not two)
    EXPECT_NE(s.find("RedBold done"), std::string::npos); // mIRC codes stripped, text kept
    EXPECT_EQ(s.find("4,1"), std::string::npos);          // colour-spec digits not left behind
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
