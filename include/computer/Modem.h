/**
 * @file Modem.h
 * @brief Qt adapter: bridges the emulated ACIA to a TCP socket via ModemProtocol.
 * @author 6502 Kernel Project
 *
 * GUI-only (links Qt Network). The headless tests exercise ModemProtocol
 * directly through a mock ModemHost and never compile this file.
 */

#ifndef MODEM_H
#define MODEM_H

#include <QObject>
#include <QTcpSocket>
#include <cstdint>
#include <string>

#include "ModemProtocol.h"

namespace Computer { class Acia; }

/**
 * @class Modem
 * @brief Emulated Hayes modem over TCP. Owns a QTcpSocket and drives the
 *        protocol state machine; implements ModemHost against the socket + ACIA.
 *
 * poll() is called once per emulation tick to drain the ACIA's TX FIFO into the
 * protocol; inbound socket data and lifecycle events are delivered via Qt
 * signals. Everything runs on the Qt main thread (no background threads),
 * matching the rest of the emulator.
 */
class Modem : public QObject, public Computer::ModemHost
{
    Q_OBJECT
public:
    explicit Modem(Computer::Acia *acia, QObject *parent = nullptr);

    /// Drain bytes the 6502 transmitted (ACIA TX) into the protocol.
    void poll();

    // ModemHost:
    void dial(const std::string &host, uint16_t port) override;
    void hangup() override;
    void sendToNetwork(const uint8_t *data, size_t n) override;
    void sendToCpu(const uint8_t *data, size_t n) override;

private:
    Computer::Acia *acia_;
    QTcpSocket *socket_;
    Computer::ModemProtocol proto_;
    bool dialing_ = false;
};

#endif // MODEM_H
