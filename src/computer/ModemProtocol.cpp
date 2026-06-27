#include "ModemProtocol.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace Computer
{
    void ModemProtocol::result(const char *code)
    {
        std::string s = "\r\n";
        s += code;
        s += "\r\n";
        host_->sendToCpu(reinterpret_cast<const uint8_t *>(s.data()), s.size());
    }

    // ---- 6502 -> modem ----------------------------------------------------

    void ModemProtocol::fromCpu(uint8_t byte)
    {
        if (state_ == State::Online)
        {
            // Watch for the "+++" escape back to command mode. (Guard-time
            // refinement is deferred; three consecutive '+' is enough here.)
            if (byte == '+')
            {
                if (++plus_count_ >= 3)
                {
                    plus_count_ = 0;
                    state_ = State::Command;
                    result("OK");
                }
                return; // hold the '+'s out of the stream until we know
            }
            // A non-'+' breaks a partial escape: flush any held '+'s first.
            for (int i = 0; i < plus_count_; ++i)
            {
                const uint8_t plus = '+';
                host_->sendToNetwork(&plus, 1);
            }
            plus_count_ = 0;

            if (byte == kIAC)
            {
                const uint8_t esc[2] = {kIAC, kIAC}; // telnet-escape a literal $FF
                host_->sendToNetwork(esc, 2);
            }
            else
            {
                host_->sendToNetwork(&byte, 1);
            }
            return;
        }

        // Command mode: accumulate an AT line.
        if (byte == '\r' || byte == '\n')
        {
            if (!cmd_line_.empty())
            {
                parseAt(cmd_line_);
                cmd_line_.clear();
            }
            return;
        }
        if (byte == 0x08 || byte == 0x7F) // backspace / delete
        {
            if (!cmd_line_.empty()) cmd_line_.pop_back();
            return;
        }
        if (byte >= 0x20 && byte < 0x7F && cmd_line_.size() < 64)
        {
            cmd_line_.push_back(static_cast<char>(byte));
        }
    }

    void ModemProtocol::parseAt(const std::string &line)
    {
        std::string up;
        up.reserve(line.size());
        for (char c : line) up.push_back(static_cast<char>(std::toupper((unsigned char)c)));

        if (up.rfind("AT", 0) != 0) // must start with "AT"
        {
            result("ERROR");
            return;
        }

        // Dial: ATD, ATDT, ATDP, optionally with spaces, then host[:port].
        size_t i = 2;
        if (i < up.size() && up[i] == 'D')
        {
            ++i;
            if (i < up.size() && (up[i] == 'T' || up[i] == 'P')) ++i;
            // Take the dial target from the ORIGINAL line (preserve host case).
            std::string target = line.substr(i);
            // trim leading spaces
            size_t s = target.find_first_not_of(' ');
            if (s == std::string::npos) { result("ERROR"); return; }
            target = target.substr(s);

            std::string host = target;
            uint16_t port = 23; // telnet default
            const size_t colon = target.rfind(':');
            if (colon != std::string::npos)
            {
                host = target.substr(0, colon);
                const std::string ps = target.substr(colon + 1);
                int p = 0;
                for (char c : ps)
                {
                    if (c < '0' || c > '9') { p = -1; break; }
                    p = p * 10 + (c - '0');
                }
                if (p <= 0 || p > 65535) { result("ERROR"); return; }
                port = static_cast<uint16_t>(p);
            }
            if (host.empty()) { result("ERROR"); return; }

            state_ = State::Dialing;
            suppress_no_carrier_ = false;
            host_->dial(host, port);
            return; // CONNECT/NO CARRIER arrives via the lifecycle callbacks
        }

        // Hang up (ATH / ATH0) and reset (ATZ): drop any connection, answer OK.
        // The connection may still be up even in command mode (after a +++
        // escape), so key off the carrier, not the command/online state.
        if (up.rfind("ATH", 0) == 0 || up.rfind("ATZ", 0) == 0)
        {
            if (connected_)
            {
                suppress_no_carrier_ = true; // local hangup: no NO CARRIER
                host_->hangup();
            }
            state_ = State::Command;
            result("OK");
            return;
        }

        // ATE (echo), AT (bare), and anything else recognized-enough: answer OK.
        result("OK");
    }

    // ---- network -> modem (telnet filter) ---------------------------------

    void ModemProtocol::telnetReply(uint8_t verb, uint8_t opt)
    {
        const uint8_t r[3] = {kIAC, verb, opt};
        host_->sendToNetwork(r, 3);
    }

    void ModemProtocol::fromNetwork(const uint8_t *data, size_t n)
    {
        std::vector<uint8_t> out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            const uint8_t b = data[i];
            switch (tn_)
            {
            case Tn::Data:
                if (b == kIAC) tn_ = Tn::Iac;
                else out.push_back(b);
                break;
            case Tn::Iac:
                if (b == kIAC) { out.push_back(kIAC); tn_ = Tn::Data; } // literal $FF
                else if (b == kWILL) tn_ = Tn::Will;
                else if (b == kWONT) tn_ = Tn::Wont;
                else if (b == kDO) tn_ = Tn::Do;
                else if (b == kDONT) tn_ = Tn::Dont;
                else if (b == kSB) tn_ = Tn::Sb;
                else tn_ = Tn::Data; // other 2-byte command: consume
                break;
            case Tn::Will: // remote WILL x -> we refuse: DONT x
                telnetReply(kDONT, b); tn_ = Tn::Data; break;
            case Tn::Wont:
                tn_ = Tn::Data; break; // nothing to do
            case Tn::Do: // remote DO x -> we refuse: WONT x
                telnetReply(kWONT, b); tn_ = Tn::Data; break;
            case Tn::Dont:
                tn_ = Tn::Data; break;
            case Tn::Sb: // subnegotiation: discard until IAC SE
                if (b == kIAC) tn_ = Tn::SbIac;
                break;
            case Tn::SbIac:
                if (b == kSE) tn_ = Tn::Data;
                else tn_ = Tn::Sb; // IAC IAC inside SB, or stray: stay in SB
                break;
            }
        }
        if (!out.empty()) host_->sendToCpu(out.data(), out.size());
    }

    // ---- socket lifecycle -------------------------------------------------

    void ModemProtocol::onConnected()
    {
        state_ = State::Online;
        connected_ = true;
        tn_ = Tn::Data;
        plus_count_ = 0;
        result("CONNECT");
    }

    void ModemProtocol::onConnectFailed()
    {
        state_ = State::Command;
        connected_ = false;
        result("NO CARRIER");
    }

    void ModemProtocol::onDisconnected()
    {
        const bool wasConnected = connected_;
        connected_ = false;
        state_ = State::Command;
        if (wasConnected && !suppress_no_carrier_) result("NO CARRIER");
        suppress_no_carrier_ = false;
    }
} // namespace Computer
