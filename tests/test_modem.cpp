/**
 * @file test_modem.cpp
 * @brief Unit tests for ModemProtocol (Hayes AT parser + telnet IAC filter).
 *
 * Pure logic, no Qt / no real socket: a MockHost captures dial/hangup requests
 * and the bytes the protocol would send to the network and to the 6502. This
 * keeps the test free of Qt Network (which is GUI-only).
 */

#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

#include "computer/ModemProtocol.h"
#include "computer/ACIA.h"

using Computer::ModemProtocol;

namespace
{
    struct MockHost : Computer::ModemHost
    {
        std::vector<std::pair<std::string, uint16_t>> dials;
        int hangups = 0;
        std::vector<uint8_t> net; // bytes sent toward the network
        std::vector<uint8_t> cpu; // bytes delivered to the 6502 (ACIA RX)

        void dial(const std::string &h, uint16_t p) override { dials.emplace_back(h, p); }
        void hangup() override { ++hangups; }
        void sendToNetwork(const uint8_t *d, size_t n) override { net.insert(net.end(), d, d + n); }
        void sendToCpu(const uint8_t *d, size_t n) override { cpu.insert(cpu.end(), d, d + n); }

        std::string cpuStr() const { return std::string(cpu.begin(), cpu.end()); }
        void clear() { net.clear(); cpu.clear(); }
    };

    void feed(ModemProtocol &m, const std::string &s)
    {
        for (char c : s) m.fromCpu(static_cast<uint8_t>(c));
    }

    constexpr uint8_t IAC = 255, DO = 253, DONT = 254, WILL = 251, WONT = 252;
    constexpr uint8_t OPT_ECHO = 1, OPT_SGA = 3;
}

TEST(ModemProtocol, AtdtDialsHostAndPort)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT bbs.example.com:2323\r");
    ASSERT_EQ(h.dials.size(), 1u);
    EXPECT_EQ(h.dials[0].first, "bbs.example.com");
    EXPECT_EQ(h.dials[0].second, 2323);
}

TEST(ModemProtocol, AtdtDefaultsToTelnetPort23)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT my.bbs.org\r");
    ASSERT_EQ(h.dials.size(), 1u);
    EXPECT_EQ(h.dials[0].first, "my.bbs.org");
    EXPECT_EQ(h.dials[0].second, 23);
}

TEST(ModemProtocol, ConnectInjectsConnectThenOnlinePipesBytes)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT host\r");
    m.onConnected();
    EXPECT_NE(h.cpuStr().find("CONNECT"), std::string::npos);
    EXPECT_TRUE(m.isOnline());

    h.clear();
    feed(m, "hi");                 // online: bytes go to the network verbatim
    ASSERT_EQ(h.net.size(), 2u);
    EXPECT_EQ(h.net[0], 'h');
    EXPECT_EQ(h.net[1], 'i');
}

TEST(ModemProtocol, OnlineEscapesLiteralFF)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT host\r"); m.onConnected(); h.clear();
    m.fromCpu(0xFF);
    ASSERT_EQ(h.net.size(), 2u);   // telnet IAC IAC
    EXPECT_EQ(h.net[0], IAC);
    EXPECT_EQ(h.net[1], IAC);
}

/* ---- carrier detect on the ACIA --------------------------------------
 * /DCD is ACTIVE LOW on a 6551, so the status bit is SET when there is NO
 * carrier. Every client keys off this instead of scanning the data stream for
 * "NO CARRIER" -- which cannot work for a binary transfer, and which once let
 * a user typing that phrase in an IRC channel knock the client offline. */

TEST(Acia, NoCarrierBitIsSetWhenTheLineIsDown)
{
    Computer::ACIA a;
    EXPECT_FALSE(a.carrier());
    EXPECT_TRUE(a.read(Computer::ACIA::kRegStatus) & Computer::ACIA::kStatusNoCarrier);
}

TEST(Acia, NoCarrierBitClearsWhenCarrierIsPresent)
{
    Computer::ACIA a;
    a.setCarrier(true);
    EXPECT_TRUE(a.carrier());
    EXPECT_FALSE(a.read(Computer::ACIA::kRegStatus) & Computer::ACIA::kStatusNoCarrier);
    a.setCarrier(false);
    EXPECT_TRUE(a.read(Computer::ACIA::kRegStatus) & Computer::ACIA::kStatusNoCarrier);
}

TEST(Acia, CarrierDoesNotDisturbTheOtherStatusBits)
{
    Computer::ACIA a;
    a.setCarrier(true);
    a.hostSend(0x41);
    const uint8_t st = a.read(Computer::ACIA::kRegStatus);
    EXPECT_TRUE(st & Computer::ACIA::kStatusRxFull);
    EXPECT_TRUE(st & Computer::ACIA::kStatusTxEmpty);
}

/* ---- quiet mode, ATQ ------------------------------------------------- */

TEST(ModemProtocol, ResultCodesAreOnByDefault)
{
    MockHost h; ModemProtocol m(&h);
    EXPECT_FALSE(m.isQuiet());
    feed(m, "AT\r");
    EXPECT_NE(h.cpuStr().find("OK"), std::string::npos);
}

TEST(ModemProtocol, Atq1SuppressesEveryResultCode)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATQ1\r");
    EXPECT_TRUE(m.isQuiet());
    h.clear();
    feed(m, "ATE0\r");                      // would normally answer OK
    EXPECT_EQ(h.cpuStr().find("OK"), std::string::npos);
    feed(m, "ATDT host\r");
    m.onConnectFailed();                     // would normally answer NO CARRIER
    EXPECT_EQ(h.cpuStr().find("NO CARRIER"), std::string::npos);
}

/* Quiet mode is what lets a /DCD-driven client keep a clean pipe: a result
 * code arriving between connect and the server's reply would otherwise land
 * in the middle of the response. */
TEST(ModemProtocol, QuietModeLeavesTheDataStreamUntouched)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATQ1\r"); feed(m, "ATDT host\r");
    m.onConnected();
    h.clear();
    const uint8_t body[] = {'h', 'i'};
    m.fromNetwork(body, sizeof(body));
    EXPECT_EQ(h.cpuStr(), "hi");             // nothing else mixed in
}

TEST(ModemProtocol, AtzRestoresResultCodes)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATQ1\r");
    feed(m, "ATZ\r");
    EXPECT_FALSE(m.isQuiet());
}

/* ---- command-mode echo, ATE ------------------------------------------
 * On by default, as on a real modem. TERM has no local echo -- a terminal
 * relies on the far end for it -- so without this, typing an AT command shows
 * nothing at all. */

TEST(ModemProtocol, EchoIsOnByDefaultAndRepeatsTypedBytes)
{
    MockHost h; ModemProtocol m(&h);
    EXPECT_TRUE(m.isEcho());
    feed(m, "ATB1");                       // no CR yet: just the echo so far
    EXPECT_EQ(h.cpuStr(), "ATB1");
}

TEST(ModemProtocol, Ate0SilencesTheEcho)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATE0\r");
    EXPECT_FALSE(m.isEcho());
    h.clear();
    feed(m, "ATB1\r");
    EXPECT_EQ(h.cpuStr().find("ATB1"), std::string::npos);   // not echoed
    EXPECT_NE(h.cpuStr().find("OK"), std::string::npos);     // still answers
}

TEST(ModemProtocol, EchoErasesOnBackspace)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATX");
    h.clear();
    m.fromCpu(0x08);
    ASSERT_EQ(h.cpu.size(), 3u);           // BS, space, BS
    EXPECT_EQ(h.cpu[0], 0x08);
    EXPECT_EQ(h.cpu[1], ' ');
    EXPECT_EQ(h.cpu[2], 0x08);
}

TEST(ModemProtocol, AtzRestoresEchoButAthLeavesItAlone)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATE0\r");
    feed(m, "ATH\r");
    EXPECT_FALSE(m.isEcho());              // a hangup is not a reset
    feed(m, "ATZ\r");
    EXPECT_TRUE(m.isEcho());
}

TEST(ModemProtocol, OnlineBytesAreNotEchoed)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT host\r"); m.onConnected(); h.clear();
    feed(m, "hello");
    EXPECT_TRUE(h.cpu.empty());            // online is a pipe, not a console
    EXPECT_EQ(std::string(h.net.begin(), h.net.end()), "hello");
}

/* ---- raw (binary) mode, ATB1 ------------------------------------------
 * IAC doubling is correct against a telnet peer and is what makes TERM's
 * XMODEM transparent. Against a raw-TCP peer -- a Gopher server on port 70 --
 * it corrupts binary: a $FF in the data is read as the start of a negotiation
 * and takes the following byte with it. ATB1 turns the filter off. */

TEST(ModemProtocol, RawModeIsOffByDefault)
{
    MockHost h; ModemProtocol m(&h);
    EXPECT_FALSE(m.isRaw());
}

TEST(ModemProtocol, Atb1EnablesRawAndAtb0Restores)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATB1\r");
    EXPECT_TRUE(m.isRaw());
    EXPECT_NE(h.cpuStr().find("OK"), std::string::npos);
    feed(m, "ATB0\r");
    EXPECT_FALSE(m.isRaw());
}

TEST(ModemProtocol, AtbRejectsAnythingButZeroOrOne)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATB7\r");
    EXPECT_FALSE(m.isRaw());
    EXPECT_NE(h.cpuStr().find("ERROR"), std::string::npos);
}

TEST(ModemProtocol, RawModeSendsLiteralFFUndoubled)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATB1\r"); feed(m, "ATDT host\r"); m.onConnected(); h.clear();
    m.fromCpu(0xFF);
    ASSERT_EQ(h.net.size(), 1u);          // not IAC IAC
    EXPECT_EQ(h.net[0], 0xFF);
}

TEST(ModemProtocol, RawModePassesInboundFFThrough)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATB1\r"); feed(m, "ATDT host\r"); m.onConnected(); h.clear();
    const uint8_t body[] = {0x01, 0xFF, 0x02, 0xFF, 0xFF, 0x03};
    m.fromNetwork(body, sizeof(body));
    ASSERT_EQ(h.cpu.size(), sizeof(body));
    for (size_t i = 0; i < sizeof(body); ++i) EXPECT_EQ(h.cpu[i], body[i]) << "at " << i;
}

/* The same bytes WITHOUT raw mode: proof the filter is what eats them, so this
   test fails if the default path ever stops being telnet-correct. */
TEST(ModemProtocol, TelnetModeSwallowsAnUnpairedInboundFF)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT host\r"); m.onConnected(); h.clear();
    const uint8_t body[] = {0x01, 0xFF, 0x02, 0x03};
    m.fromNetwork(body, sizeof(body));
    // $FF 0x02 is read as a two-byte command and consumed.
    ASSERT_EQ(h.cpu.size(), 2u);
    EXPECT_EQ(h.cpu[0], 0x01);
    EXPECT_EQ(h.cpu[1], 0x03);
}

TEST(ModemProtocol, RawModeClearsOnLocalHangup)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATB1\r"); feed(m, "ATDT host\r"); m.onConnected();
    feed(m, "+++"); feed(m, "ATH\r");
    EXPECT_FALSE(m.isRaw());
}

TEST(ModemProtocol, RawModeClearsOnRemoteDrop)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATB1\r"); feed(m, "ATDT host\r"); m.onConnected();
    m.onDisconnected();
    EXPECT_FALSE(m.isRaw());
}

TEST(ModemProtocol, TelnetInboundIsFilteredAndRefused)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT host\r"); m.onConnected(); h.clear();

    // "HI" + IAC DO ECHO + IAC WILL SGA + "X" + IAC IAC (literal 0xFF)
    const uint8_t in[] = {'H', 'I', IAC, DO, OPT_ECHO, IAC, WILL, OPT_SGA, 'X', IAC, IAC};
    m.fromNetwork(in, sizeof(in));

    // 6502 sees only the payload, with the literal 0xFF unescaped.
    ASSERT_EQ(h.cpu.size(), 4u);
    EXPECT_EQ(h.cpu[0], 'H');
    EXPECT_EQ(h.cpu[1], 'I');
    EXPECT_EQ(h.cpu[2], 'X');
    EXPECT_EQ(h.cpu[3], 0xFF);

    // DO ECHO -> WONT ECHO (refused); WILL SGA -> DO SGA (suppress-go-ahead accepted).
    const std::vector<uint8_t> expect = {IAC, WONT, OPT_ECHO, IAC, DO, OPT_SGA};
    EXPECT_EQ(h.net, expect);
}

TEST(ModemProtocol, TelnetAcceptsSuppressGoAhead)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT host\r"); m.onConnected(); h.clear();

    const uint8_t in[] = {IAC, DO, OPT_SGA};   // server asks us to suppress GA
    m.fromNetwork(in, sizeof(in));
    const std::vector<uint8_t> expect = {IAC, WILL, OPT_SGA}; // we agree
    EXPECT_EQ(h.net, expect);
    EXPECT_TRUE(h.cpu.empty()); // negotiation stripped from the 6502 stream
}

// A BBS that probes for terminal type must learn we are an ANSI terminal, so it
// serves enhanced ANSI/CP437 rather than falling back to plain ASCII.
TEST(ModemProtocol, TelnetAnnouncesAnsiTerminalType)
{
    constexpr uint8_t SB = 250, SE = 240, TTYPE = 24, SEND = 1, IS = 0;
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT host\r"); m.onConnected(); h.clear();

    // Server: IAC DO TERMINAL-TYPE  -> we reply IAC WILL TERMINAL-TYPE.
    const uint8_t doTtype[] = {IAC, DO, TTYPE};
    m.fromNetwork(doTtype, sizeof(doTtype));
    EXPECT_EQ(h.net, (std::vector<uint8_t>{IAC, WILL, TTYPE}));
    h.clear();

    // Server: IAC SB TERMINAL-TYPE SEND IAC SE -> we reply IS "ansi-bbs".
    const uint8_t send[] = {IAC, SB, TTYPE, SEND, IAC, SE};
    m.fromNetwork(send, sizeof(send));
    std::vector<uint8_t> want = {IAC, SB, TTYPE, IS};
    for (char c : std::string("ansi-bbs")) want.push_back(static_cast<uint8_t>(c));
    want.push_back(IAC); want.push_back(SE);
    EXPECT_EQ(h.net, want);
    EXPECT_TRUE(h.cpu.empty()); // negotiation never reaches the 6502
}

// NAWS: we accept and immediately report an 80x25 window.
TEST(ModemProtocol, TelnetReportsWindowSize)
{
    constexpr uint8_t SB = 250, SE = 240, NAWS = 31;
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT host\r"); m.onConnected(); h.clear();

    const uint8_t doNaws[] = {IAC, DO, NAWS};
    m.fromNetwork(doNaws, sizeof(doNaws));
    const std::vector<uint8_t> want = {IAC, WILL, NAWS,
                                       IAC, SB, NAWS, 0, 80, 0, 25, IAC, SE};
    EXPECT_EQ(h.net, want);
}

TEST(ModemProtocol, PlusEscapeThenHangup)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT host\r"); m.onConnected(); h.clear();

    feed(m, "+++");                // escape back to command mode
    EXPECT_FALSE(m.isOnline());
    EXPECT_NE(h.cpuStr().find("OK"), std::string::npos);
    EXPECT_TRUE(h.net.empty());    // the +++ is not forwarded to the network

    h.clear();
    feed(m, "ATH\r");              // hang up
    EXPECT_EQ(h.hangups, 1);
    EXPECT_NE(h.cpuStr().find("OK"), std::string::npos);
    EXPECT_EQ(h.cpuStr().find("NO CARRIER"), std::string::npos); // local hangup is quiet
}

TEST(ModemProtocol, PartialPlusRunIsForwarded)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "ATDT host\r"); m.onConnected(); h.clear();
    feed(m, "++x");                // not an escape: the two '+' and 'x' all go out
    EXPECT_TRUE(m.isOnline());
    const std::vector<uint8_t> expect = {'+', '+', 'x'};
    EXPECT_EQ(h.net, expect);
}

TEST(ModemProtocol, ConnectFailedAndRemoteDropGiveNoCarrier)
{
    MockHost h1; ModemProtocol m1(&h1);
    feed(m1, "ATDT host\r");
    m1.onConnectFailed();
    EXPECT_NE(h1.cpuStr().find("NO CARRIER"), std::string::npos);

    MockHost h2; ModemProtocol m2(&h2);
    feed(m2, "ATDT host\r"); m2.onConnected(); h2.clear();
    m2.onDisconnected();           // remote closed
    EXPECT_NE(h2.cpuStr().find("NO CARRIER"), std::string::npos);
}

TEST(ModemProtocol, BareAtIsOkNonAtIsError)
{
    MockHost h; ModemProtocol m(&h);
    feed(m, "AT\r");
    EXPECT_NE(h.cpuStr().find("OK"), std::string::npos);
    h.clear();
    feed(m, "HELLO\r");
    EXPECT_NE(h.cpuStr().find("ERROR"), std::string::npos);
}
