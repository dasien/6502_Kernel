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

    // We refuse both options: DO ECHO -> WONT ECHO, WILL SGA -> DONT SGA.
    const std::vector<uint8_t> expect = {IAC, WONT, OPT_ECHO, IAC, DONT, OPT_SGA};
    EXPECT_EQ(h.net, expect);
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
