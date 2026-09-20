/**
 * @file Modem.cpp
 * @brief Qt adapter implementation: ACIA to TCP socket.
 */

#include "Modem.h"
#include "ACIA.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QString>

Modem::Modem(Computer::ACIA *acia, QObject *parent)
    : QObject(parent), acia_(acia), socket_(new QTcpSocket(this)), proto_(this)
{
    connect(socket_, &QTcpSocket::connected, this, [this]() {
        dialing_ = false;
        acia_->setCarrier(true);        // /DCD asserts: the 6502 can see the call
        proto_.onConnected();
    });
    connect(socket_, &QTcpSocket::readyRead, this, [this]() {
        const QByteArray d = socket_->readAll();
        if (!d.isEmpty())
            proto_.fromNetwork(reinterpret_cast<const uint8_t *>(d.constData()),
                               static_cast<size_t>(d.size()));
    });
    connect(socket_, &QTcpSocket::disconnected, this, [this]() {
        dialing_ = false;
        acia_->setCarrier(false);       // carrier drops, which is the end-of-call signal
        proto_.onDisconnected();
    });
    connect(socket_, &QAbstractSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                acia_->setCarrier(false);
                if (dialing_)
                {
                    dialing_ = false;
                    proto_.onConnectFailed();
                }
                else
                {
                    proto_.onDisconnected();
                }
            });
}

void Modem::poll()
{
    while (acia_->hostHasTx())
        proto_.fromCpu(acia_->hostRecv());
}

void Modem::dial(const std::string &host, uint16_t port)
{
    dialing_ = true;
    socket_->abort(); // drop any prior connection
    socket_->connectToHost(QString::fromStdString(host), port);
}

void Modem::hangup()
{
    dialing_ = false;
    socket_->abort();
}

void Modem::sendToNetwork(const uint8_t *data, size_t n)
{
    if (socket_->state() == QAbstractSocket::ConnectedState)
        socket_->write(reinterpret_cast<const char *>(data), static_cast<qint64>(n));
}

void Modem::sendToCpu(const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        acia_->hostSend(data[i]);
}
