#include "AudioNormalizer.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

#include <cstring>

namespace Colosseum::Player2 {
namespace {

QString avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

// The typed mode maps to exactly one internal filter chain. Smooth has no chain (passthrough).
QString filterChainFor(NormalizationMode mode)
{
    switch (mode) {
    case NormalizationMode::Light:
        // Gentle streaming dynamic normalization, the intent of the previous dynaudnorm path.
        // Small window (framelen x gausssize ~= 300 ms) keeps latency low so audio does not underrun.
        return QStringLiteral("dynaudnorm=framelen=100:gausssize=3");
    case NormalizationMode::Full:
        // EBU R128 loudness normalization, single-pass dynamic form for live playback.
        return QStringLiteral("loudnorm=I=-16:TP=-1.5:LRA=11");
    case NormalizationMode::Smooth:
    default:
        return QString();
    }
}

} // namespace

AudioNormalizer::AudioNormalizer() = default;

AudioNormalizer::~AudioNormalizer()
{
    teardown();
}

void AudioNormalizer::teardown()
{
    if (m_frame)
        av_frame_free(&m_frame);
    if (m_graph)
        avfilter_graph_free(&m_graph);
    m_source = nullptr;
    m_sink = nullptr;
    m_pushedSamples = 0;
    m_pulledSamples = 0;
}

bool AudioNormalizer::configure(const AudioFormat &format, NormalizationMode mode, QString *error)
{
    teardown();
    m_format = format;
    m_mode = mode;
    if (format.sampleRate <= 0 || format.channels <= 0) {
        if (error)
            *error = QStringLiteral("Invalid normalizer format");
        return false;
    }
    const QString chain = filterChainFor(mode);
    if (chain.isEmpty())
        return true; // Smooth: pure passthrough, no graph.

    m_graph = avfilter_graph_alloc();
    m_frame = av_frame_alloc();
    if (!m_graph || !m_frame) {
        teardown();
        if (error)
            *error = QStringLiteral("Could not allocate normalizer graph");
        return false;
    }

    AVChannelLayout layout{};
    av_channel_layout_default(&layout, format.channels);
    char layoutName[64]{};
    av_channel_layout_describe(&layout, layoutName, sizeof(layoutName));
    av_channel_layout_uninit(&layout);

    // Packed float32 in and out so the endpoint format and audio-master clock are unchanged.
    const QByteArray sourceArgs =
        QStringLiteral("time_base=1/1000000:sample_rate=%1:sample_fmt=flt:channel_layout=%2")
            .arg(format.sampleRate)
            .arg(QString::fromLatin1(layoutName))
            .toUtf8();

    int result = avfilter_graph_create_filter(&m_source, avfilter_get_by_name("abuffer"),
                                               "in", sourceArgs.constData(), nullptr, m_graph);
    if (result >= 0) {
        result = avfilter_graph_create_filter(&m_sink, avfilter_get_by_name("abuffersink"),
                                               "out", nullptr, nullptr, m_graph);
    }
    if (result < 0) {
        teardown();
        if (error)
            *error = QStringLiteral("Normalizer endpoint filters failed: %1").arg(avError(result));
        return false;
    }

    // The trailing aformat guarantees packed float32 at the endpoint rate, so the sink needs no
    // explicit format constraint (setting one post-init only warns).
    // Force the chain back to the packed endpoint format regardless of what the loudness filter emits.
    const QString fullChain = chain +
        QStringLiteral(",aformat=sample_fmts=flt:sample_rates=%1:channel_layouts=%2")
            .arg(format.sampleRate)
            .arg(QString::fromLatin1(layoutName));

    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    if (!outputs || !inputs) {
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        teardown();
        if (error)
            *error = QStringLiteral("Could not allocate normalizer links");
        return false;
    }
    outputs->name = av_strdup("in");
    outputs->filter_ctx = m_source;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = m_sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    result = avfilter_graph_parse_ptr(m_graph, fullChain.toUtf8().constData(), &inputs, &outputs,
                                      nullptr);
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    if (result < 0 || (result = avfilter_graph_config(m_graph, nullptr)) < 0) {
        teardown();
        if (error)
            *error = QStringLiteral("Normalizer graph config failed: %1").arg(avError(result));
        return false;
    }
    return true;
}

bool AudioNormalizer::pullOutputs(std::vector<AudioBuffer> *outputs, QString *error)
{
    while (true) {
        const int result = av_buffersink_get_frame(m_sink, m_frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            return true;
        if (result < 0) {
            if (error)
                *error = QStringLiteral("Normalizer sink failed: %1").arg(avError(result));
            return false;
        }
        const int channels = m_frame->ch_layout.nb_channels;
        const int frames = m_frame->nb_samples;
        AudioBuffer buffer;
        buffer.format = m_format;
        buffer.frameCount = frames;
        buffer.ptsUs = m_frame->pts;
        buffer.bytes.resize(frames * channels * static_cast<int>(sizeof(float)));
        std::memcpy(buffer.bytes.data(), m_frame->extended_data[0], buffer.bytes.size());
        m_pulledSamples += frames;
        outputs->push_back(std::move(buffer));
        av_frame_unref(m_frame);
    }
}

bool AudioNormalizer::process(const AudioBuffer &input, std::vector<AudioBuffer> *outputs,
                              QString *error)
{
    if (m_mode == NormalizationMode::Smooth || !m_graph) {
        outputs->push_back(input); // bit-transparent passthrough
        return true;
    }
    if (input.frameCount <= 0)
        return true;

    av_frame_unref(m_frame);
    m_frame->nb_samples = input.frameCount;
    m_frame->format = AV_SAMPLE_FMT_FLT;
    av_channel_layout_default(&m_frame->ch_layout, m_format.channels);
    m_frame->sample_rate = m_format.sampleRate;
    m_frame->pts = input.ptsUs;
    if (av_frame_get_buffer(m_frame, 0) < 0) {
        if (error)
            *error = QStringLiteral("Normalizer input allocation failed");
        return false;
    }
    std::memcpy(m_frame->extended_data[0], input.bytes.constData(),
                static_cast<size_t>(input.frameCount) * m_format.channels * sizeof(float));
    const int result = av_buffersrc_add_frame_flags(m_source, m_frame,
                                                     AV_BUFFERSRC_FLAG_KEEP_REF);
    av_frame_unref(m_frame);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("Normalizer input failed: %1").arg(avError(result));
        return false;
    }
    m_pushedSamples += input.frameCount;
    return pullOutputs(outputs, error);
}

bool AudioNormalizer::drain(std::vector<AudioBuffer> *outputs, QString *error)
{
    if (m_mode == NormalizationMode::Smooth || !m_graph)
        return true;
    if (av_buffersrc_add_frame_flags(m_source, nullptr, 0) < 0)
        return true;
    return pullOutputs(outputs, error);
}

void AudioNormalizer::flush()
{
    if (m_graph)
        configure(m_format, m_mode, nullptr); // cheapest correct reset of filter state
}

qint64 AudioNormalizer::reportedLatencyUs() const noexcept
{
    if (m_mode == NormalizationMode::Smooth || m_format.sampleRate <= 0)
        return 0;
    const qint64 buffered = m_pushedSamples - m_pulledSamples;
    if (buffered <= 0)
        return 0;
    return buffered * 1'000'000 / m_format.sampleRate;
}

} // namespace Colosseum::Player2
