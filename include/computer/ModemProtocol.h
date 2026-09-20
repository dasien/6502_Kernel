/**
 * @file ModemProtocol.h
 * @brief Hayes-modem + telnet protocol state machine (pure logic, no Qt).
 * @author 6502 Kernel Project
 */

#ifndef MODEMPROTOCOL_H
#define MODEMPROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Computer
{
    /**
     * @class ModemHost
     * @brief Callback seam between the protocol logic and its transport.
     *
     * Lets ModemProtocol be unit-tested without Qt or a real socket: the Qt
     * adapter (Modem) implements these against a QTcpSocket + the ACIA, while
     * tests implement them against in-memory buffers.
     */
    class ModemHost
    {
    public:
        virtual ~ModemHost() = default;
        /// Open a TCP connection to host:port (async; expect onConnected/onConnectFailed).
        virtual void dial(const std::string &host, uint16_t port) = 0;
        /// Drop the current connection.
        virtual void hangup() = 0;
        /// Send bytes out to the network (already telnet-encoded).
        virtual void sendToNetwork(const uint8_t *data, size_t n) = 0;
        /// Deliver bytes to the 6502 (push onto the ACIA RX FIFO).
        virtual void sendToCpu(const uint8_t *data, size_t n) = 0;
    };

    /**
     * @class ModemProtocol
     * @brief Emulated Hayes modem: AT command interpreter (offline) + transparent
     *        byte pipe with a telnet IAC filter (online).
     *
     * Offline (command mode) it accumulates the 6502's serial output into AT
     * command lines: `ATDT host[:port]` dials (default port 23), `ATH`/`ATZ` hang
     * up, `+++` escapes back from online to command mode. It answers with the
     * usual result codes injected into the 6502's receive path (`OK`, `CONNECT`,
     * `NO CARRIER`, `ERROR`).
     *
     * Online it passes bytes both ways, escaping a literal $FF the 6502 sends as
     * telnet `IAC IAC`, and filtering inbound telnet negotiation (refusing all
     * options) so the 6502 sees a clean byte stream.
     *
     * RAW MODE (`ATB1`, cleared by `ATB0`, a hangup or a reset) turns that
     * filter off. IAC doubling is correct against a telnet peer and is what
     * makes TERM's XMODEM transparent: the BBS un-doubles what we double and
     * doubles what we un-double. Against a peer that is NOT telnet it corrupts
     * binary -- a Gopher server on port 70 sending a $FF loses it and the byte
     * after it to a negotiation nobody sent. Raw mode is for those: the byte
     * pipe becomes literal in both directions. It is off by default, so a
     * client that never asks is unaffected, and it clears on hangup so a stale
     * mode cannot leak into the next call.
     *
     * Command-mode input is echoed back to the 6502 (`ATE1`, the default;
     * `ATE0` turns it off, `ATZ` restores it). A real modem does this and it is
     * the only reason typing an AT command into TERM shows anything: TERM has
     * no local echo, because a terminal relies on the far end for it.
     *
     * `ATQ1` suppresses result codes entirely (`ATQ0` restores them, `ATZ`
     * resets). A program that watches /DCD wants a clean pipe and has no use
     * for text aimed at a human -- GOPHER sets it, because a result code
     * arriving between CONNECT and the server's reply would otherwise land in
     * the middle of the response. TERM leaves codes on: someone is reading.
     */
    class ModemProtocol
    {
    public:
        explicit ModemProtocol(ModemHost *host) : host_(host) {}

        /// A byte the 6502 transmitted (drained from the ACIA TX FIFO).
        void fromCpu(uint8_t byte);
        /// Raw bytes received from the network (may contain telnet IAC).
        void fromNetwork(const uint8_t *data, size_t n);

        /// Socket lifecycle notifications from the host adapter.
        void onConnected();
        void onConnectFailed();
        void onDisconnected();

        [[nodiscard]] bool isOnline() const { return state_ == State::Online; }
        /// True when the telnet filter is off (ATB1). Public for tests.
        [[nodiscard]] bool isRaw() const { return raw_; }
        /// True when command-mode input is echoed (ATE1). Public for tests.
        [[nodiscard]] bool isEcho() const { return echo_; }
        /// True when result codes are suppressed (ATQ1). Public for tests.
        [[nodiscard]] bool isQuiet() const { return quiet_; }

        /// Telnet protocol bytes (public for tests / the adapter).
        static constexpr uint8_t kIAC = 255;
        static constexpr uint8_t kSE = 240;
        static constexpr uint8_t kSB = 250;
        static constexpr uint8_t kWILL = 251;
        static constexpr uint8_t kWONT = 252;
        static constexpr uint8_t kDO = 253;
        static constexpr uint8_t kDONT = 254;
        static constexpr uint8_t kOptSGA = 3;    // suppress-go-ahead (accepted)
        static constexpr uint8_t kOptTType = 24; // terminal-type (RFC 1091)
        static constexpr uint8_t kOptNAWS = 31;  // negotiate-about-window-size
        static constexpr uint8_t kTTypeIS = 0;   // TERMINAL-TYPE IS
        static constexpr uint8_t kTTypeSEND = 1; // TERMINAL-TYPE SEND

    private:
        enum class State { Command, Dialing, Online };
        enum class Tn { Data, Iac, Will, Wont, Do, Dont, Sb, SbIac };

        ModemHost *host_;
        State state_ = State::Command;
        bool connected_ = false;      ///< carrier present (socket open); independent of command/online
        std::string cmd_line_;        ///< AT command line accumulator (command mode)
        int plus_count_ = 0;          ///< consecutive '+' seen online (the +++ escape)
        bool suppress_no_carrier_ = false; ///< local ATH/ATZ hangup -> no NO CARRIER
        bool raw_ = false;            ///< ATB1: pass $FF through untouched, both ways
        bool echo_ = true;            ///< ATE1: echo command-mode input back to the CPU
        bool quiet_ = false;          ///< ATQ1: emit no result codes at all
        Tn tn_ = Tn::Data;            ///< inbound telnet filter state
        std::vector<uint8_t> sb_;     ///< subnegotiation bytes collected between SB and SE

        void parseAt(const std::string &line);
        void result(const char *code);     ///< inject "\r\n<code>\r\n" to the 6502
        void telnetReply(uint8_t verb, uint8_t opt);
        void telnetSubReply(const uint8_t *body, size_t n); ///< IAC SB <body> IAC SE
        void handleSubneg();               ///< act on a completed subnegotiation
    };
} // namespace Computer

#endif // MODEMPROTOCOL_H
