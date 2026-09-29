/**
 * @file SidAudio.cpp
 * @brief Qt Multimedia bridge for the software SID (pull-mode QAudioSink).
 */

#include "computer/SidAudio.h"
#include "computer/SID.h"
#include <cstdio>
#include <cstdlib>

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
        explicit SidPullDevice(Computer::SID *sid) : sid_(sid) {}

        bool isSequential() const override { return true; }

        // Pull-mode QAudioSink will not read from a device that reports no bytes
        // available, so advertise a continuous (effectively endless) stream.
        qint64 bytesAvailable() const override
        {
            return static_cast<qint64>(Computer::SID::kSampleRate) * sizeof(int16_t) +
                   QIODevice::bytesAvailable();
        }

        // The sink asks for up to maxlen bytes; fill the whole request so it
        // never underruns. Mono 16-bit => 2 bytes per frame.
        //
        // The samples come from the SID's buffer, which the emulation fills on
        // machine time; nothing is synthesized here. SID::playback() steers the
        // buffer's level with a tiny rate adjustment rather than letting gaps and
        // backlogs through -- see there.
        //
        // MFC_AUDIO_LOG=1 prints, every five seconds of audio, the buffer's level and
        // how many samples had to be padded (underrun) or thrown away (overflow).
        qint64 readData(char *data, qint64 maxlen) override
        {
            const int frames = static_cast<int>(maxlen / sizeof(int16_t));
            if (frames <= 0)
                return 0;
            sid_->playback(reinterpret_cast<int16_t *>(data), frames);
            if (log_ && (played_ += frames) >= 5 * Computer::SID::kSampleRate)
            {
                played_ = 0;
                std::fprintf(stderr, "audio: buffered %d  largest request %d  "
                             "underrun %llu  dropped %llu\n",
                             sid_->buffered(), sid_->largestRequest(),
                             static_cast<unsigned long long>(sid_->underrunSamples()),
                             static_cast<unsigned long long>(sid_->droppedSamples()));
            }
            return static_cast<qint64>(frames) * sizeof(int16_t);
        }

        qint64 writeData(const char *, qint64) override { return 0; }

    private:
        Computer::SID *sid_;
        const bool log_ = std::getenv("MFC_AUDIO_LOG") != nullptr;
        long played_ = 0;
    };
} // namespace

SidAudio::SidAudio(Computer::SID *sid, QObject *parent)
    : QObject(parent), sid_(sid)
{
    QAudioFormat format;
    format.setSampleRate(Computer::SID::kSampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice out = QMediaDevices::defaultAudioOutput();
    if (out.isNull())
        return; // no audio device available; stay silent rather than crash

    sink_ = new QAudioSink(out, format, this);
    // A short sink buffer: Qt sizes its internal ring from this (at least twice the
    // device's own buffer), and each request is at most the ring's free space.
    sink_->setBufferSize(Computer::SID::kRingKeep * static_cast<qsizetype>(sizeof(int16_t)));

    device_ = new SidPullDevice(sid_);
    // UNBUFFERED, or QIODevice keeps a read buffer of its own in front of readData():
    // Qt's small reads were each topped up to QIODevice's 16 KB chunk -- 8192 samples,
    // 186 ms -- so every request was that size whatever setBufferSize() said, and the
    // sound lagged the machine by at least that much. Unbuffered, readData() sees Qt's
    // real requests, which are bounded by the ring buffer setBufferSize() sizes.
    device_->open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    sink_->start(device_);
}

SidAudio::~SidAudio()
{
    if (sink_)
        sink_->stop();
    delete device_; // QAudioSink is parented to this; device_ is not
}
