#include "AudioPipeline.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
}

#include <algorithm>

namespace Colosseum::Player2 {
namespace {

QString avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

} // namespace

AudioPipeline::AudioPipeline(IAudioSink *sink)
    : m_sink(sink)
{
}

AudioPipeline::~AudioPipeline() { resetConverter(); }

bool AudioPipeline::open(const AudioFormat &format, QString *error)
{
    if (!m_sink || format.sampleRate <= 0 || format.channels <= 0) {
        if (error)
            *error = QStringLiteral("Invalid audio pipeline configuration");
        return false;
    }
    if (!m_sink->open(format, error))
        return false;
    m_outputFormat = format;
    m_open = true;
    return true;
}

bool AudioPipeline::ensureConverter(const AVFrame *frame, QString *error)
{
    const int channels = frame->ch_layout.nb_channels;
    if (m_resampler && frame->sample_rate == m_inputSampleRate &&
        frame->format == m_inputSampleFormat && channels == m_inputChannels) {
        return true;
    }
    resetConverter();
    AVChannelLayout outputLayout{};
    AVChannelLayout inputLayout{};
    av_channel_layout_default(&outputLayout, m_outputFormat.channels);
    if (frame->ch_layout.nb_channels > 0)
        av_channel_layout_copy(&inputLayout, &frame->ch_layout);
    else
        av_channel_layout_default(&inputLayout, 2);
    int result = swr_alloc_set_opts2(&m_resampler, &outputLayout, AV_SAMPLE_FMT_FLT,
                                     m_outputFormat.sampleRate, &inputLayout,
                                     static_cast<AVSampleFormat>(frame->format),
                                     frame->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outputLayout);
    av_channel_layout_uninit(&inputLayout);
    if (result < 0 || !m_resampler || (result = swr_init(m_resampler)) < 0) {
        if (error)
            *error = QStringLiteral("Audio resampler setup failed: %1").arg(avError(result));
        resetConverter();
        return false;
    }
    m_inputSampleRate = frame->sample_rate;
    m_inputSampleFormat = frame->format;
    m_inputChannels = channels;
    return true;
}

bool AudioPipeline::writeConverted(uint8_t **input, int inputFrames, qint64 ptsUs,
                                   quint64 generation, QString *error)
{
    m_lastConvertedFrames = 0;
    const int capacity = static_cast<int>(av_rescale_rnd(
        swr_get_delay(m_resampler, m_inputSampleRate) + inputFrames,
        m_outputFormat.sampleRate, m_inputSampleRate, AV_ROUND_UP));
    if (capacity <= 0)
        return true;
    AudioBuffer output;
    output.format = m_outputFormat;
    output.ptsUs = ptsUs;
    output.bytes.resize(capacity * m_outputFormat.channels * sizeof(float));
    uint8_t *planes[] = {reinterpret_cast<uint8_t *>(output.bytes.data())};
    const int converted = swr_convert(m_resampler, planes, capacity,
                                      const_cast<const uint8_t **>(input), inputFrames);
    if (converted < 0) {
        if (error)
            *error = QStringLiteral("Audio conversion failed: %1").arg(avError(converted));
        return false;
    }
    if (converted == 0)
        return true;
    m_lastConvertedFrames = converted;
    output.frameCount = converted;
    output.bytes.resize(converted * m_outputFormat.channels * sizeof(float));
    m_nextPtsUs = output.ptsUs +
        static_cast<qint64>((converted * 1'000'000.0) / m_outputFormat.sampleRate);
    const int written = m_sink->write(output, generation, error);
    return written == converted;
}

bool AudioPipeline::submitDecodedFrame(AVFrame *frame, qint64 ptsUs, quint64 generation,
                                       QString *error)
{
    if (!m_open || !frame || !ensureConverter(frame, error))
        return false;
    return writeConverted(frame->extended_data, frame->nb_samples, ptsUs, generation, error);
}

bool AudioPipeline::drain(quint64 generation, QString *error)
{
    if (!m_resampler)
        return true;
    for (int pass = 0; pass < 8; ++pass) {
        if (!writeConverted(nullptr, 0, m_nextPtsUs, generation, error))
            return false;
        if (m_lastConvertedFrames == 0)
            break;
    }
    return true;
}

void AudioPipeline::flush(quint64 generation)
{
    resetConverter();
    m_nextPtsUs = 0;
    if (m_sink)
        m_sink->flush(generation);
}

void AudioPipeline::setVolume(float linear) { if (m_sink) m_sink->setVolume(linear); }
void AudioPipeline::setMuted(bool muted) { if (m_sink) m_sink->setMuted(muted); }
AudioFormat AudioPipeline::outputFormat() const noexcept { return m_outputFormat; }
int AudioPipeline::queueDepthFrames() const { return m_sink ? m_sink->queueDepthFrames() : 0; }
QString AudioPipeline::deviceName() const { return m_sink ? m_sink->deviceName() : QString(); }
AudioClockSnapshot AudioPipeline::clock() const { return m_sink ? m_sink->clock() : AudioClockSnapshot{}; }
quint64 AudioPipeline::underrunCount() const { return m_sink ? m_sink->underrunCount() : 0; }

void AudioPipeline::resetConverter()
{
    swr_free(&m_resampler);
    m_inputSampleRate = 0;
    m_inputSampleFormat = -1;
    m_inputChannels = 0;
}

} // namespace Colosseum::Player2
