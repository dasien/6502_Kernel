/**
 * @file SidAudio.cpp
 * @brief Qt Multimedia bridge for the software SID (pull-mode QAudioSink).
 */

#include "computer/SidAudio.h"
#include "computer/Sid.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

namespace
{
    // A QIODevice the QAudioSink pulls from: each read synthesizes fresh SID PCM.
    class SidPullDevice : public QIODevice
    {
    public:
        explicit SidPullDevice(Computer::Sid *sid) : sid_(sid) {}

        bool isSequential() const override { return true; }

        // Pull-mode QAudioSink will not read from a device that reports no bytes
        // available, so advertise a continuous (effectively endless) stream.
        qint64 bytesAvailable() const override
        {
            return static_cast<qint64>(Computer::Sid::kSampleRate) * sizeof(int16_t) +
                   QIODevice::bytesAvailable();
        }

        // The sink asks for up to maxlen bytes; fill the whole request so it
        // never underruns. Mono 16-bit => 2 bytes per frame.
        qint64 readData(char *data, qint64 maxlen) override
        {
            const int frames = static_cast<int>(maxlen / sizeof(int16_t));
            if (frames <= 0)
                return 0;
            sid_->generateSamples(reinterpret_cast<int16_t *>(data), frames);
            return static_cast<qint64>(frames) * sizeof(int16_t);
        }

        qint64 writeData(const char *, qint64) override { return 0; }

    private:
        Computer::Sid *sid_;
    };
} // namespace

SidAudio::SidAudio(Computer::Sid *sid, QObject *parent)
    : QObject(parent), sid_(sid)
{
    QAudioFormat format;
    format.setSampleRate(Computer::Sid::kSampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice out = QMediaDevices::defaultAudioOutput();
    if (out.isNull())
        return; // no audio device available; stay silent rather than crash

    sink_ = new QAudioSink(out, format, this);

    device_ = new SidPullDevice(sid_);
    device_->open(QIODevice::ReadOnly);
    sink_->start(device_);
}

SidAudio::~SidAudio()
{
    if (sink_)
        sink_->stop();
    delete device_; // QAudioSink is parented to this; device_ is not
}
