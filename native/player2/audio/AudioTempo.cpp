#include "AudioTempo.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Colosseum::Player2 {
namespace {

QString avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

constexpr double kMinSpeed = 0.5;
constexpr double kMaxSpeed = 2.0;

} // namespace

AudioTempo::AudioTempo() = default;
AudioTempo::~AudioTempo() { teardown(); }

void AudioTempo::teardown()
{
    if (m_graph)
        avfilter_graph_free(&m_graph);
    if (m_frame)
        av_frame_free(&m_frame);
    m_graph = nullptr;
    m_source = nullptr;
    m_sink = nullptr;
    m_frame = nullptr;
}

bool AudioTempo::configure(const AudioFormat &format, double speed, QString *error)
{
    teardown();
    m_format = format;
    m_speed = std::clamp(speed, kMinSpeed, kMaxSpeed);
    if (format.sampleRate <= 0 || format.channels <= 0) {
        if (error)
            *error = QStringLiteral("Invalid tempo format");
        return false;
    }
    if (std::abs(m_speed - 1.0) < 1e-6)
        return true; // 1.0×: pure passthrough, no graph.

    m_graph = avfilter_graph_alloc();
    m_frame = av_frame_alloc();
    if (!m_graph || !m_frame) {
        teardown();
        if (error)
            *error = QStringLiteral("Could not allocate tempo graph");
        return false;
    }

    AVChannelLayout layout{};
    av_channel_layout_default(&layout, format.channels);
    char layoutName[64]{};
    av_channel_layout_describe(&layout, layoutName, sizeof(layoutName));
    av_channel_layout_uninit(&layout);

    // Microsecond time_base so a frame's pts IS its media time in µs; atempo compresses that pts by
    // 1/speed, and pullOutputs multiplies back by speed to recover the input media time.
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
            *error = QStringLiteral("Tempo endpoint filters failed: %1").arg(avError(result));
        return false;
    }

    const QString fullChain =
        QStringLiteral("atempo=%1,aformat=sample_fmts=flt:sample_rates=%2:channel_layouts=%3")
            .arg(m_speed, 0, 'f', 6)
            .arg(format.sampleRate)
            .arg(QString::fromLatin1(layoutName));

    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    if (!outputs || !inputs) {
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        teardown();
        if (error)
            *error = QStringLiteral("Could not allocate tempo links");
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
            *error = QStringLiteral("Tempo graph config failed: %1").arg(avError(result));
        return false;
    }
    return true;
}

bool AudioTempo::pullOutputs(std::vector<AudioBuffer> *outputs, QString *error)
{
    while (true) {
        const int result = av_buffersink_get_frame(m_sink, m_frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            return true;
        if (result < 0) {
            if (error)
                *error = QStringLiteral("Tempo sink failed: %1").arg(avError(result));
            return false;
        }
        const int channels = m_frame->ch_layout.nb_channels;
        const int frames = m_frame->nb_samples;
        AudioBuffer buffer;
        buffer.format = m_format;
        buffer.frameCount = frames;
        buffer.speed = m_speed;
        // atempo's output pts is the compressed timeline (µs, from our 1/1'000'000 time_base); × speed
        // recovers the input media time this block represents.
        const qint64 compressedUs = m_frame->pts == AV_NOPTS_VALUE
            ? 0
            : av_rescale_q(m_frame->pts, av_buffersink_get_time_base(m_sink),
                           AVRational{1, 1'000'000});
        buffer.ptsUs = static_cast<qint64>(std::llround(compressedUs * m_speed));
        buffer.bytes.resize(frames * channels * static_cast<int>(sizeof(float)));
        std::memcpy(buffer.bytes.data(), m_frame->extended_data[0], buffer.bytes.size());
        outputs->push_back(std::move(buffer));
        av_frame_unref(m_frame);
    }
}

bool AudioTempo::process(const AudioBuffer &input, std::vector<AudioBuffer> *outputs, QString *error)
{
    if (std::abs(m_speed - 1.0) < 1e-6 || !m_graph) {
        outputs->push_back(input); // 1.0×: bit-transparent passthrough
        return true;
    }
    if (input.frameCount <= 0)
        return true;

    av_frame_unref(m_frame);
    m_frame->nb_samples = input.frameCount;
    m_frame->format = AV_SAMPLE_FMT_FLT;
    av_channel_layout_default(&m_frame->ch_layout, m_format.channels);
    m_frame->sample_rate = m_format.sampleRate;
    m_frame->pts = input.ptsUs; // µs, matching the 1/1'000'000 source time_base
    if (av_frame_get_buffer(m_frame, 0) < 0) {
        if (error)
            *error = QStringLiteral("Tempo input allocation failed");
        return false;
    }
    std::memcpy(m_frame->extended_data[0], input.bytes.constData(),
                static_cast<size_t>(input.frameCount) * m_format.channels * sizeof(float));
    const int result = av_buffersrc_add_frame_flags(m_source, m_frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    av_frame_unref(m_frame);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("Tempo input failed: %1").arg(avError(result));
        return false;
    }
    return pullOutputs(outputs, error);
}

bool AudioTempo::drain(std::vector<AudioBuffer> *outputs, QString *error)
{
    if (std::abs(m_speed - 1.0) < 1e-6 || !m_graph)
        return true;
    if (av_buffersrc_add_frame_flags(m_source, nullptr, 0) < 0)
        return true;
    return pullOutputs(outputs, error);
}

void AudioTempo::flush()
{
    if (m_graph)
        configure(m_format, m_speed, nullptr); // cheapest correct reset of filter state
}

} // namespace Colosseum::Player2
