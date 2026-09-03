#include "colosseum_server/media/MediaPipeline.h"

#include <QtMath>

namespace ColosseumServer::Media {
namespace {

QByteArray secs(qint64 ms) { return QByteArray::number(double(ms) / 1000.0, 'f', 3); }

bool transmux(const LegacyStream &stream)
{
    const QString name = stream.codecName.toLower();
    if (stream.codecType == QStringLiteral("video")) return name.contains(QStringLiteral("h264"));
    if (stream.codecType == QStringLiteral("audio")) return name.contains(QStringLiteral("aac"));
    return false;
}

int estimateBitrate(int width, int height, double fps)
{
    return qRound(height * width * fps * 0.11 / 1024.0);
}

QByteArray addQuery(const QByteArray &query)
{
    return query.isEmpty() ? QByteArray() : QByteArrayLiteral("?") + query;
}

} // namespace

LegacyPrepared LegacyHls::prepare(const LegacyProbeResult &probe)
{
    LegacyPrepared result;
    result.probe = probe;
    for (int i = 0; i < probe.streams.size(); ++i)
        if (probe.streams.at(i).codecType == QStringLiteral("video")) { result.videoStreamIndex = i; break; }
    if (result.videoStreamIndex >= 0) {
        const int height = probe.streams.at(result.videoStreamIndex).height;
        for (int q : {320, 480, 720}) if (height < 0 || q <= height) result.qualities.append(q);
        if (result.qualities.isEmpty() && height > 0) result.qualities.append(height);
    }
    for (qint64 at = 0; at < probe.durationMs; at += 6000)
        result.uniformSegments.append({at, qMin<qint64>(6000, probe.durationMs - at)});
    return result;
}
QByteArray LegacyHls::masterPlaylist(const LegacyPrepared &prepared,
                                     const QByteArray &base, bool multiStream,
                                     const QByteArray &query)
{
    QByteArray out = "#EXTM3U\n#EXT-X-VERSION:4\n";
    const QByteArray q = addQuery(query);
    if (multiStream) {
        for (const LegacyStream &stream : prepared.probe.streams) {
            if (stream.codecType == QStringLiteral("audio")) {
                out += "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",";
                if (!stream.language.isEmpty())
                    out += "LANGUAGE=\"" + stream.language.toUtf8() + "\",AUTOSELECT=YES,";
                if (stream.defaultStream) out += "DEFAULT=YES,";
                out += "NAME=\"" + (!stream.language.isEmpty() ? stream.language.toUtf8()
                    : QByteArrayLiteral("Audio ") + QByteArray::number(stream.stream))
                    + "\",URI=\"stream-" + QByteArray::number(stream.stream) + ".m3u8" + q + "\"\n";
            }
        }
        for (const LegacyStream &stream : prepared.probe.streams) {
            if (stream.codecType != QStringLiteral("video")) continue;
            out += "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" + QByteArray::number(prepared.probe.bitrate)
                + ",AUDIO=\"audio\"\n" + base + "stream-" + QByteArray::number(stream.stream)
                + ".m3u8" + q + '\n';
        }
        return out;
    }
    if (prepared.videoStreamIndex < 0) return out;
    const LegacyStream &video = prepared.probe.streams.at(prepared.videoStreamIndex);
    int program = 0;
    for (int quality : prepared.qualities) {
        const int bandwidth = qRound(1000.0 * estimateBitrate(
            qRound(double(quality) * video.width / qMax(1, video.height)), quality,
            video.fps > 0 ? video.fps : 24.0));
        out += "#EXT-X-STREAM-INF:PROGRAM-ID=" + QByteArray::number(program++)
            + ",BANDWIDTH=" + QByteArray::number(bandwidth) + '\n'
            + base + "stream-q-" + QByteArray::number(quality) + ".m3u8" + q + '\n';
        break;
    }
    return out;
}
QByteArray LegacyHls::streamPlaylist(const LegacyPrepared &prepared, int quality,
                                     const QString &prefix, const QByteArray &search,
                                     bool discontinuity)
{
    if (quality != 0 && quality != -1 && quality != int('o')
        && !prepared.qualities.contains(quality)) return {};
    const QVector<LegacySegment> &segments = prepared.uniformSegments;
    QByteArray out = "#EXTM3U\n#EXT-X-VERSION:4\n";
    qint64 maxDuration = 0;
    for (const LegacySegment &segment : segments) maxDuration = qMax(maxDuration, segment.durationMs);
    out += "#EXT-X-TARGETDURATION:" + QByteArray::number(qCeil(double(maxDuration) / 1000.0)) + '\n';
    out += "#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n";
    for (int i = 0; i < segments.size(); ++i) {
        if (discontinuity) out += "#EXT-X-DISCONTINUITY\n";
        out += "#EXTINF:" + secs(segments.at(i).durationMs) + ",\n";
        out += prefix.toUtf8() + QByteArray::number(i) + ".ts" + search + '\n';
    }
    out += "#EXT-X-ENDLIST\n";
    return out;
}

QByteArray LegacyHls::subtitlesPlaylist(qint64 durationMs, const QUrl &subtitleUrl,
                                        const QByteArray &host)
{
    QByteArray out = "#EXTM3U\n#EXT-X-VERSION:4\n";
    out += "#EXT-X-TARGETDURATION:" + QByteArray::number(qCeil(double(durationMs) / 1000.0)) + '\n';
    out += "#EXT-X-MEDIA-SEQUENCE:1\n#EXT-X-PLAYLIST-TYPE:VOD\n";
    out += "#EXTINF:" + secs(durationMs) + ",\n";
    out += "http://" + host + "/subtitles.vtt?from=" + QUrl::toPercentEncoding(subtitleUrl.toString()) + '\n';
    out += "#EXT-X-ENDLIST\n";
    return out;
}
ProcessInvocation LegacyHls::segmentInvocation(const Executables &tools,
                                               const LegacyPrepared &prepared,
                                               const QString &source, int segmentIndex,
                                               int quality, int streamIndex)
{
    ProcessInvocation invocation;
    invocation.program = tools.ffmpeg;
    invocation.contentType = QStringLiteral("video/mp2t");
    invocation.headers.insert(QStringLiteral("X-HLS-Flow"), QStringLiteral("transcoder"));
    if (segmentIndex < 0 || segmentIndex >= prepared.uniformSegments.size()) return invocation;
    const LegacySegment segment = prepared.uniformSegments.at(segmentIndex);
    QStringList args{QStringLiteral("-ss"), QString::fromLatin1(secs(segment.atMs)),
        QStringLiteral("-t"), QString::fromLatin1(secs(segment.durationMs)),
        QStringLiteral("-i"), source, QStringLiteral("-af"), QStringLiteral("aselect=gt(n\\,1)")};
    if (streamIndex >= 0) {
        const auto it = std::find_if(prepared.probe.streams.cbegin(), prepared.probe.streams.cend(),
            [streamIndex](const LegacyStream &s) { return s.stream == streamIndex; });
        if (it != prepared.probe.streams.cend()) {
            args << QStringLiteral("-map") << QStringLiteral("0:%1").arg(streamIndex);
            if (it->codecType == QStringLiteral("video"))
                args << QStringLiteral("-c:v") << (transmux(*it) ? QStringLiteral("copy") : QStringLiteral("libx264"));
            else if (it->codecType == QStringLiteral("audio"))
                args << QStringLiteral("-c:a") << (transmux(*it) ? QStringLiteral("copy") : QStringLiteral("aac"));
        }
    } else {
        const LegacyStream *video = prepared.videoStreamIndex >= 0 ? &prepared.probe.streams.at(prepared.videoStreamIndex) : nullptr;
        if (video && quality > 0 && video->height - quality > 100)
            args << QStringLiteral("-vf") << QStringLiteral("scale=-2:%1").arg(quality);
        args << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-pix_fmt")
             << QStringLiteral("yuv420p") << QStringLiteral("-preset") << QStringLiteral("veryfast")
             << QStringLiteral("-tune") << QStringLiteral("zerolatency")
             << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-strict")
             << QStringLiteral("experimental") << QStringLiteral("-ac") << QStringLiteral("2");
    }
    args << QStringLiteral("-copyts") << QStringLiteral("-mpegts_copyts") << QStringLiteral("1")
         << QStringLiteral("-f") << QStringLiteral("mpegts") << QStringLiteral("-threads")
         << QStringLiteral("0") << QStringLiteral("pipe:1");
    invocation.arguments = args;
    return invocation;
}

ProcessInvocation LegacyHls::dlnaInvocation(const Executables &tools,
                                            const LegacyPrepared &prepared,
                                            const QString &source, qint64 atMs)
{
    ProcessInvocation invocation;
    invocation.program = tools.ffmpeg;
    bool videoCopy = true, audioCopy = true;
    for (const LegacyStream &stream : prepared.probe.streams) {
        if (stream.codecType == QStringLiteral("video")) videoCopy = videoCopy && transmux(stream);
        if (stream.codecType == QStringLiteral("audio")) audioCopy = audioCopy && transmux(stream);
    }
    invocation.arguments = {QStringLiteral("-ss"), QString::number(atMs / 1000.0, 'f', 3),
        QStringLiteral("-i"), source, QStringLiteral("-c:v"), videoCopy ? QStringLiteral("copy") : QStringLiteral("libx264"),
        QStringLiteral("-c:a"), audioCopy ? QStringLiteral("copy") : QStringLiteral("aac"),
        QStringLiteral("-strict"), QStringLiteral("-2"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-tune"), QStringLiteral("zerolatency"), QStringLiteral("-f"), QStringLiteral("mpegts"),
        QStringLiteral("pipe:1")};
    invocation.contentType = QStringLiteral("video/vnd.dlna.mpeg-tts");
    invocation.headers.insert(QStringLiteral("transferMode.dlna.org"), QStringLiteral("Streaming"));
    invocation.headers.insert(QStringLiteral("contentFeatures.dlna.org"),
        QStringLiteral("DLNA.ORG_PN=AVC_TS_BL_CIF15_AAC_540;DLNA.ORG_FLAGS=ED100000000000000000000000000000"));
    return invocation;
}

ProcessInvocation LegacyHls::thumbnailInvocation(const Executables &tools,
                                                 const QString &source, qint64 atSeconds)
{
    ProcessInvocation invocation;
    invocation.program = tools.ffmpeg;
    invocation.arguments = {QStringLiteral("-ss"), QString::number(atSeconds), QStringLiteral("-i"), source,
        QStringLiteral("-r"), QStringLiteral("1"), QStringLiteral("-vframes"), QStringLiteral("1"),
        QStringLiteral("-f"), QStringLiteral("image2"), QStringLiteral("-vcodec"), QStringLiteral("mjpeg"),
        QStringLiteral("pipe:1")};
    invocation.contentType = QStringLiteral("image/jpg");
    return invocation;
}

} // namespace ColosseumServer::Media
