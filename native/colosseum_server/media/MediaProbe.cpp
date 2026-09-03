#include "colosseum_server/media/MediaPipeline.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace ColosseumServer::Media {
namespace {

void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

qint64 parseClockMs(const QRegularExpressionMatch &match)
{
    return qRound64(match.captured(1).toDouble() * 3600000.0
        + match.captured(2).toDouble() * 60000.0
        + match.captured(3).toDouble() * 1000.0);
}

QString normalizeContainer(const QString &line)
{
    const QStringList parts = line.toLower().split(QRegularExpression(QStringLiteral("\\s*,\\s*")));
    if (parts.contains(QStringLiteral("mp4"))) return QStringLiteral("mp4");
    if (parts.contains(QStringLiteral("matroska"))) return QStringLiteral("matroska");
    return parts.size() > 1 ? parts.at(1).trimmed() : QString();
}

} // namespace
MediaProbe::MediaProbe(Executables executables) : m_executables(std::move(executables)) {}

bool MediaProbe::legacyProbe(const QString &mediaUrl, LegacyProbeResult *result,
                             QString *error, int timeoutMs) const
{
    if (!result || m_executables.ffmpeg.isEmpty()) {
        setError(error, QStringLiteral("no ffmpeg found"));
        return false;
    }
    const ProcessResult process = MediaProcess::run(m_executables.ffmpeg,
        {QStringLiteral("-i"), mediaUrl}, timeoutMs);
    if (process.timedOut) {
        setError(error, process.error);
        return false;
    }
    LegacyProbeResult parsed;
    const QString text = QString::fromUtf8(process.stdErr);
    const QStringList lines = text.split(QLatin1Char('\n'));
    bool parsing = false;
    const QRegularExpression durationRe(QStringLiteral("Duration: (\\d\\d):(\\d\\d):(\\d\\d(?:\\.\\d+)?)"));
    const QRegularExpression totalBitrateRe(QStringLiteral("bitrate: (\\d+)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression streamRe(QStringLiteral("Stream #0:(\\d\\d?)(?:\\((\\w+)\\))?"));
    for (QString line : lines) {
        line = line.trimmed().isEmpty() ? line : line;
        if (line.contains(QStringLiteral("Input #0"))) {
            parsing = true;
            parsed.container = normalizeContainer(line.section(QLatin1Char(':'), 0, 0));
            continue;
        }
        if (!parsing) continue;
        const auto duration = durationRe.match(line);
        if (duration.hasMatch()) {
            parsed.durationMs = parseClockMs(duration);
            const auto bitrate = totalBitrateRe.match(line);
            if (bitrate.hasMatch()) parsed.bitrate = bitrate.captured(1).toLongLong() * 1000;
        }
        const auto sm = streamRe.match(line);
        if (!sm.hasMatch()) continue;
        LegacyStream stream;
        stream.stream = sm.captured(1).toInt();
        stream.language = sm.captured(2);
        const QRegularExpression kindCodec(
            QStringLiteral(":\\s*(Video|Audio|Subtitle):\\s*([^,\\s]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto kc = kindCodec.match(line);
        if (kc.hasMatch()) {
            stream.codecType = kc.captured(1).toLower();
            stream.codecName = kc.captured(2).toLower();
        }
        const auto dims = QRegularExpression(QStringLiteral("([0-9]{3,4})x([0-9]{3,4})")).match(line);
        if (dims.hasMatch()) {
            stream.width = dims.captured(1).toInt();
            stream.height = dims.captured(2).toInt();
        }
        const auto fps = QRegularExpression(QStringLiteral("([0-9]{2})(\\.)?([0-9]{2})? fps")).match(line);
        if (fps.hasMatch()) stream.fps = fps.captured(1).toDouble();
        const auto bitrate = QRegularExpression(QStringLiteral("([0-9]{3,4}) kb/s")).match(line);
        if (bitrate.hasMatch()) stream.bitrate = bitrate.captured(1).toLongLong() * 1000;
        stream.defaultStream = line.contains(QStringLiteral("(default)"));
        parsed.streams.append(stream);
    }
    if (parsed.durationMs < 0) {
        setError(error, process.error.isEmpty()
            ? QStringLiteral("ffmpeg probe produced no duration") : process.error);
        return false;
    }
    *result = parsed;
    setError(error, {});
    return true;
}
bool MediaProbe::probeV2(const QString &mediaUrl, V2ProbeResult *result,
                         QString *error, int timeoutMs) const
{
    if (!result || m_executables.ffprobe.isEmpty()) {
        setError(error, QStringLiteral("no ffprobe found"));
        return false;
    }
    const QString entries = QStringLiteral(
        "stream=index,bit_rate,max_bit_rate,codec_type,codec_name,start_time,start_pts,"
        "r_frame_rate,sample_rate,channels,channel_layout,time_base,has_b_frames,nb_frames,"
        "width,height,color_space,color_transfer,color_primaries,codec_tag_string:"
        "stream_tags=title,language,duration,bps,number_of_bytes:"
        "format=format_name,duration,bit_rate,max_bit_rate");
    const ProcessResult process = MediaProcess::run(m_executables.ffprobe,
        {QStringLiteral("-show_entries"), entries, QStringLiteral("-print_format"),
         QStringLiteral("json"), mediaUrl}, timeoutMs);
    if (process.timedOut || process.exitCode != 0) {
        setError(error, process.error.isEmpty() ? QString::fromUtf8(process.stdErr) : process.error);
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(process.stdOut, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        setError(error, QStringLiteral("invalid ffprobe json: %1").arg(parseError.errorString()));
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    V2ProbeResult parsed;
    parsed.format.name = format.value(QStringLiteral("format_name")).toString(QStringLiteral("unknown"));
    bool okDuration = false;
    parsed.format.duration = format.value(QStringLiteral("duration")).toString().toDouble(&okDuration);
    if (!okDuration) parsed.format.duration = -1.0;
    const qint64 formatBitRate = format.value(QStringLiteral("bit_rate")).toString().toLongLong();
    const qint64 formatMaxBitRate = format.value(QStringLiteral("max_bit_rate")).toString().toLongLong();
    QHash<QString, int> typeIds;
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue &value : streams) {
        const QJsonObject object = value.toObject();
        V2Stream stream;
        stream.index = object.value(QStringLiteral("index")).toInt(-1);
        stream.track = object.value(QStringLiteral("codec_type")).toString(QStringLiteral("unknown"));
        stream.id = typeIds.value(stream.track, 0);
        typeIds[stream.track] = stream.id + 1;
        stream.codec = object.value(QStringLiteral("codec_name")).toString(QStringLiteral("unknown"));
        stream.streamBitRate = object.value(QStringLiteral("bit_rate")).toString().toLongLong();
        stream.streamMaxBitRate = object.value(QStringLiteral("max_bit_rate")).toString().toLongLong();
        bool ok = false;
        stream.startTime = object.value(QStringLiteral("start_time")).toString().toDouble(&ok);
        if (!ok) stream.startTime = -1.0;
        stream.startTimeTs = object.value(QStringLiteral("start_pts")).toString().toLongLong(&ok);
        if (!ok) stream.startTimeTs = -1;
        const QString timeBase = object.value(QStringLiteral("time_base")).toString();
        stream.timescale = timeBase.section(QLatin1Char('/'), 1, 1).toInt(&ok);
        if (!ok || stream.timescale <= 0) stream.timescale = 1;
        const QString rate = object.value(QStringLiteral("r_frame_rate")).toString();
        const double num = rate.section(QLatin1Char('/'), 0, 0).toDouble(&ok);
        bool denominatorOk = false;
        const double den = rate.section(QLatin1Char('/'), 1, 1).toDouble(&denominatorOk);
        stream.frameRate = ok && denominatorOk && den != 0.0 ? num / den : -1.0;
        const QJsonObject tags = object.value(QStringLiteral("tags")).toObject();
        stream.width = object.value(QStringLiteral("width")).toInt(-1);
        stream.height = object.value(QStringLiteral("height")).toInt(-1);
        stream.numberOfFrames = object.value(QStringLiteral("nb_frames")).toString().toLongLong(&ok);
        if (!ok) stream.numberOfFrames = -1;
        stream.hasBFrames = object.value(QStringLiteral("has_b_frames")).toInt() > 0;
        stream.isHdr = object.value(QStringLiteral("color_space")).toString() == QStringLiteral("bt2020nc")
            && object.value(QStringLiteral("color_transfer")).toString() == QStringLiteral("smpte2084")
            && object.value(QStringLiteral("color_primaries")).toString() == QStringLiteral("bt2020");
        stream.isDoVi = object.value(QStringLiteral("codec_tag_string")).toString() == QStringLiteral("dvhe");
        stream.formatBitRate = formatBitRate;
        stream.formatMaxBitRate = formatMaxBitRate;
        stream.bps = tags.value(QStringLiteral("BPS")).toString().toLongLong();
        stream.numberOfBytes = tags.value(QStringLiteral("NUMBER_OF_BYTES")).toString().toLongLong();
        stream.formatDuration = parsed.format.duration;
        stream.sampleRate = object.value(QStringLiteral("sample_rate")).toString().toInt(&ok);
        if (!ok) stream.sampleRate = -1;
        stream.channels = object.value(QStringLiteral("channels")).toInt(-1);
        stream.channelLayout = object.value(QStringLiteral("channel_layout")).toString(QStringLiteral("unknown"));
        stream.title = tags.value(QStringLiteral("title")).toString();
        stream.language = tags.value(QStringLiteral("language")).toString();
        parsed.streams.append(stream);
    }
    *result = parsed;
    setError(error, {});
    return true;
}

} // namespace ColosseumServer::Media
