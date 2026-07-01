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
