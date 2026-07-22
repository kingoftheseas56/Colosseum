#include "SubtitlePipeline.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

#include <QtCore/QRegularExpression>

#include <cstring>

namespace Colosseum::Player2 {
namespace {

QString avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

// FFmpeg's decoded ASS event (rect->ass) is "ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,
// Effect,Text" — NOT the .ass file's "Dialogue: Layer,Start,End,..." form. So the readable text is
// after the 8th comma; then strip {\...} override blocks.
QString plainFromAss(const char *ass)
{
    if (!ass)
        return QString();
    QString line = QString::fromUtf8(ass);
    int comma = -1;
    for (int i = 0; i < 8; ++i) {
        comma = line.indexOf(QLatin1Char(','), comma + 1);
        if (comma < 0)
            break;
    }
    QString text = comma >= 0 ? line.mid(comma + 1) : line;
    text.remove(QRegularExpression(QStringLiteral("\\{[^}]*\\}")));
    text.replace(QStringLiteral("\\N"), QStringLiteral("\n"));
    text.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    return text.trimmed();
}

} // namespace

SubtitlePipeline::SubtitlePipeline() = default;
SubtitlePipeline::~SubtitlePipeline() { close(); }

bool SubtitlePipeline::open(const AVCodecParameters *params, AVRational streamTimeBase,
                            QString *error)
{
    close();
    if (!params) {
        if (error)
            *error = QStringLiteral("No subtitle stream parameters");
        return false;
    }
    const AVCodec *codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        if (error)
            *error = QStringLiteral("No subtitle decoder for the selected stream");
        return false;
    }
    m_context = avcodec_alloc_context3(codec);
    int result = 0;
    if (!m_context || (result = avcodec_parameters_to_context(m_context, params)) < 0 ||
        (result = avcodec_open2(m_context, codec, nullptr)) < 0) {
        close();
        if (error)
            *error = QStringLiteral("Subtitle decoder open failed: %1").arg(avError(result));
        return false;
    }
    m_timeBaseNum = streamTimeBase.num > 0 ? streamTimeBase.num : 1;
    m_timeBaseDen = streamTimeBase.den > 0 ? streamTimeBase.den : 1000;
    return true;
}

void SubtitlePipeline::close()
{
    if (m_context)
        avcodec_free_context(&m_context);
}

void SubtitlePipeline::flush()
{
    if (m_context)
        avcodec_flush_buffers(m_context);
}

bool SubtitlePipeline::decode(const AVPacket *packet, quint64 generation, int streamIndex,
                              std::vector<SubtitleCue> *out, QString *error)
{
    if (!m_context || !packet)
        return true;
    AVSubtitle subtitle{};
    int got = 0;
    const int result = avcodec_decode_subtitle2(m_context, &subtitle, &got,
                                                const_cast<AVPacket *>(packet));
    if (result < 0) {
        if (error)
            *error = QStringLiteral("Subtitle decode failed: %1").arg(avError(result));
        return false;
    }
    if (!got)
        return true;

    const AVRational tb{m_timeBaseNum, m_timeBaseDen};
    const qint64 packetUs = packet->pts == AV_NOPTS_VALUE
        ? 0 : av_rescale_q(packet->pts, tb, AVRational{1, 1'000'000});
    const qint64 packetDurUs = packet->duration > 0
        ? av_rescale_q(packet->duration, tb, AVRational{1, 1'000'000}) : 0;
    // start/end_display_time are milliseconds relative to the subtitle pts. Text subtitles (SRT)
    // often leave end_display_time at 0 and carry the duration on the packet instead.
    const qint64 baseUs = packetUs + static_cast<qint64>(subtitle.start_display_time) * 1'000;
    qint64 spanUs = static_cast<qint64>(subtitle.end_display_time - subtitle.start_display_time) * 1'000;
    if (spanUs <= 0)
        spanUs = packetDurUs;
    const qint64 endUs = baseUs + (spanUs > 0 ? spanUs : 0);

    for (unsigned int i = 0; i < subtitle.num_rects; ++i) {
        const AVSubtitleRect *rect = subtitle.rects[i];
        SubtitleCue cue;
        cue.generation = generation;
        cue.streamIndex = streamIndex;
        cue.startUs = baseUs;
        cue.endUs = endUs;
        if (rect->type == SUBTITLE_ASS && rect->ass)
            cue.text = plainFromAss(rect->ass);
        else if (rect->type == SUBTITLE_TEXT && rect->text)
            cue.text = QString::fromUtf8(rect->text).trimmed();
        else if (rect->type == SUBTITLE_BITMAP) {
            cue.bitmap = true;
            cue.x = rect->x;
            cue.y = rect->y;
            cue.width = rect->w;
            cue.height = rect->h;
            // Convert the paletted bitmap to tightly packed RGBA so QML can paint it directly.
            if (rect->w > 0 && rect->h > 0 && rect->data[0] && rect->data[1]) {
                cue.rgba.resize(rect->w * rect->h * 4);
                const auto *palette = reinterpret_cast<const quint32 *>(rect->data[1]);
                const uint8_t *indices = rect->data[0];
                const int stride = rect->linesize[0];
                auto *dst = reinterpret_cast<quint32 *>(cue.rgba.data());
                for (int y = 0; y < rect->h; ++y) {
                    for (int x = 0; x < rect->w; ++x)
                        dst[y * rect->w + x] = palette[indices[y * stride + x]];
                }
            }
        }
        if (!cue.text.isEmpty() || cue.bitmap)
            out->push_back(std::move(cue));
    }
    avsubtitle_free(&subtitle);
    return true;
}

} // namespace Colosseum::Player2
