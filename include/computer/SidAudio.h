/**
 * @file SidAudio.h
 * @brief Qt adapter: streams the software SID's PCM to the default audio output.
 * @author 6502 Kernel Project
 *
 * GUI-only (links Qt Multimedia). The headless tests exercise the SID synthesis
 * core directly via generateSamples() and never compile this file. Mirrors the
 * ACIA (headless core) + Modem (Qt bridge) split.
 */

#ifndef SID_AUDIO_H
#define SID_AUDIO_H

#include <QObject>

namespace Computer { class SID; }

class QAudioSink;
class QIODevice;

/**
 * @class SidAudio
 * @brief Plays a Computer::SID's samples through a QAudioSink.
 *
 * Runs the sink in pull mode: an internal QIODevice's readData() calls
 * SID::playback() whenever the audio backend needs more samples (on Qt's audio
 * thread), which copies them out of the buffer the SID fills on machine time.
 * Nothing is synthesized here. 44100 Hz, mono, s16.
 */
class SidAudio : public QObject
{
    Q_OBJECT
public:
    explicit SidAudio(Computer::SID *sid, QObject *parent = nullptr);
    ~SidAudio() override;

    /// Is there an audio device playing? If not, the machine keeps its own time.
    [[nodiscard]] bool playing() const { return sink_ != nullptr; }

private:
    Computer::SID *sid_;
    QAudioSink *sink_ = nullptr;
    QIODevice *device_ = nullptr; ///< pull source (owned)
};

#endif // SID_AUDIO_H
