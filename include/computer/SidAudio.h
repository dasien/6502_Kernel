/**
 * @file SidAudio.h
 * @brief Qt adapter: streams the software SID's PCM to the default audio output.
 * @author 6502 Kernel Project
 *
 * GUI-only (links Qt Multimedia). The headless tests exercise the Sid synthesis
 * core directly via generateSamples() and never compile this file. Mirrors the
 * Acia (headless core) + Modem (Qt bridge) split.
 */

#ifndef SID_AUDIO_H
#define SID_AUDIO_H

#include <QObject>

namespace Computer { class Sid; }

class QAudioSink;
class QIODevice;

/**
 * @class SidAudio
 * @brief Pulls PCM from a Computer::Sid and plays it through a QAudioSink.
 *
 * Runs the sink in pull mode: an internal QIODevice's readData() calls
 * Sid::generateSamples() whenever the audio backend needs more samples (on Qt's
 * audio thread). The Sid guards its registers with a mutex, so the CPU can poke
 * sound registers on the emulation thread while audio plays. 44100 Hz, mono, s16.
 */
class SidAudio : public QObject
{
    Q_OBJECT
public:
    explicit SidAudio(Computer::Sid *sid, QObject *parent = nullptr);
    ~SidAudio() override;

private:
    Computer::Sid *sid_;
    QAudioSink *sink_ = nullptr;
    QIODevice *device_ = nullptr; ///< pull source (owned)
};

#endif // SID_AUDIO_H
