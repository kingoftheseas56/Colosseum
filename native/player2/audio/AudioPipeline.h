#pragma once

#include "AudioNormalizer.h"
#include "WASAPIAudioSink.h"
#include "player2/core/Player2Types.h"

#include <QtCore/QString>

struct AVChannelLayout;
struct AVFrame;
struct SwrContext;

namespace Colosseum::Player2 {

class AudioPipeline
{
public:
    explicit AudioPipeline(IAudioSink *sink);
    ~AudioPipeline();

    bool open(const AudioFormat &format, QString *error);
    bool submitDecodedFrame(AVFrame *frame, qint64 ptsUs, quint64 generation, QString *error);
    bool drain(quint64 generation, QString *error);
    void flush(quint64 generation);
    void setPaused(bool paused);
    void setVolume(float linear);
    void setMuted(bool muted);

    // Worker-thread only: the normalization filter graph is owned by the decode thread. Live mode
    // changes and seek/track flushes must reach it through these, never from the GUI thread.
    void configureNormalization(NormalizationMode mode);
    void flushFilters();
    NormalizationMode normalizationMode() const noexcept;
    qint64 normalizationLatencyUs() const noexcept;

    AudioFormat outputFormat() const noexcept;
    int queueDepthFrames() const;
    QString deviceName() const;
    AudioClockSnapshot clock() const;
    quint64 underrunCount() const;

private:
    bool ensureConverter(const AVFrame *frame, QString *error);
    bool writeConverted(uint8_t **input, int inputFrames, qint64 ptsUs,
                        quint64 generation, QString *error);
    bool writeNormalized(const AudioBuffer &buffer, quint64 generation, QString *error);
    void resetConverter();

    IAudioSink *m_sink = nullptr;
    AudioNormalizer m_normalizer;
    NormalizationMode m_mode = NormalizationMode::Smooth;
    AudioFormat m_outputFormat;
    SwrContext *m_resampler = nullptr;
    int m_inputSampleRate = 0;
    int m_inputSampleFormat = -1;
    int m_inputChannels = 0;
    int m_lastConvertedFrames = 0;
    qint64 m_nextPtsUs = 0;
    bool m_open = false;
};

} // namespace Colosseum::Player2
