#include "Modem.h"
#include "Acia.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QString>

Modem::Modem(Computer::Acia *acia, QObject *parent)
    : QObject(parent), acia_(acia), socket_(new QTcpSocket(this)), proto_(this)
{
    connect(socket_, &QTcpSocket::connected, this, [this]() {
        dialing_ = false;
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
        proto_.onDisconnected();
    });
    connect(socket_, &QAbstractSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
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
