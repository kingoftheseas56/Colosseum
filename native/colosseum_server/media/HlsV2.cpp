#include "colosseum_server/media/MediaPipeline.h"

#include <QDateTime>
#include <QJsonArray>
#include <QUrlQuery>
#include <QtMath>

#include <algorithm>

namespace ColosseumServer::Media {
namespace {

void setError(QString *error, const QString &value) { if (error) *error = value; }

QByteArray boolWord(bool value) { return value ? QByteArrayLiteral("YES") : QByteArrayLiteral("NO"); }

QByteArray quoted(const QString &value) { return value.toUtf8().replace('"', "\\\""); }

QString trackType(const QString &track)
{
    QString type = track;
    type.remove(QRegularExpression(QStringLiteral("[0-9]+$")));
    return type;
}

int trackId(const QString &track)
{
    const auto match = QRegularExpression(QStringLiteral("(\\d+)$")).match(track);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

QByteArray makeQuery(const HlsV2Options &options)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("mediaURL"), options.mediaUrl);
    if (options.maxAudioChannels > 0) query.addQueryItem(QStringLiteral("maxAudioChannels"), QString::number(options.maxAudioChannels));
    if (options.forceTranscoding) query.addQueryItem(QStringLiteral("forceTranscoding"), QStringLiteral("1"));
    for (const QString &codec : options.videoCodecs) query.addQueryItem(QStringLiteral("videoCodecs"), codec);
    for (const QString &codec : options.audioCodecs) query.addQueryItem(QStringLiteral("audioCodecs"), codec);
    if (!options.profile.isEmpty()) query.addQueryItem(QStringLiteral("profile"), options.profile);
    if (options.maxWidth > 0) query.addQueryItem(QStringLiteral("maxWidth"), QString::number(options.maxWidth));
    return query.query(QUrl::FullyEncoded).toUtf8();
}
const V2Stream *findStream(const V2ProbeResult &probe, const QString &track)
{
    const QString type = trackType(track);
    const int id = trackId(track);
    for (const V2Stream &stream : probe.streams)
        if (stream.track == type && stream.id == id) return &stream;
    return nullptr;
}

QVector<double> fixedDurations(double duration, double segmentDuration)
{
    QVector<double> result;
    if (!(duration > 0.0) || !(segmentDuration > 0.0)) return result;
    for (double at = 0.0; at < duration; at += segmentDuration)
        result.append(qMin(segmentDuration, duration - at));
    return result;
}

} // namespace

QByteArray HlsV2Playlist::master(const V2ProbeResult &probe, const QByteArray &query)
{
    const V2Stream *video = nullptr;
    QVector<const V2Stream *> audio;
    QVector<const V2Stream *> subtitles;
    for (const V2Stream &stream : probe.streams) {
        if (stream.track == QStringLiteral("video") && !video) video = &stream;
        else if (stream.track == QStringLiteral("audio")) audio.append(&stream);
        else if (stream.track == QStringLiteral("subtitle")
                 && stream.codec != QStringLiteral("dvb_subtitle")
                 && stream.codec != QStringLiteral("dvd_subtitle")
                 && stream.codec != QStringLiteral("hdmv_pgs_subtitle")
                 && stream.codec != QStringLiteral("xsub")) subtitles.append(&stream);
    }
    QByteArray out = "#EXTM3U\n#EXT-X-VERSION:7";
    if (video)
        out += "\n#EXT-X-MEDIA:TYPE=VIDEO,GROUP-ID=\"video\",NAME=\"Video\",AUTOSELECT=YES,DEFAULT=YES";
    for (const V2Stream *stream : audio) {
        const QString name = !stream->title.isEmpty() ? stream->title
            : !stream->language.isEmpty() ? stream->language : QString::number(stream->id);
        out += "\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"" + quoted(name) + "\"";
        if (!stream->language.isEmpty()) out += ",LANGUAGE=\"" + quoted(stream->language) + "\"";
        out += ",AUTOSELECT=YES,DEFAULT=" + boolWord(stream->id == 0);
        if (video) out += ",URI=\"audio" + QByteArray::number(stream->id) + ".m3u8?" + query + "\"";
    }
    for (const V2Stream *stream : subtitles) {
        const QString name = !stream->title.isEmpty() ? stream->title
            : !stream->language.isEmpty() ? stream->language : QString::number(stream->id);
        out += "\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subtitles\",NAME=\"" + quoted(name) + "\"";
        if (!stream->language.isEmpty()) out += ",LANGUAGE=\"" + quoted(stream->language) + "\"";
        out += ",AUTOSELECT=NO,DEFAULT=NO,FORCED=NO,URI=\"subtitle"
            + QByteArray::number(stream->id) + ".m3u8?" + query + "\"";
    }
    out += "\n#EXT-X-STREAM-INF:BANDWIDTH=164000";
    if (video) out += ",VIDEO=\"video\"";
    if (!audio.isEmpty()) out += ",AUDIO=\"audio\"";
    if (!subtitles.isEmpty()) out += ",SUBTITLES=\"subtitles\"";
    out += ",NAME=\"Main\"";
    if (video) out += "\nvideo0.m3u8?" + query;
    else if (!audio.isEmpty()) out += "\naudio0.m3u8?" + query;
    return out;
}

QByteArray HlsV2Playlist::media(const QString &track, const QVector<double> &durations,
                                const QByteArray &query)
{
    QByteArray out = "#EXTM3U\n#EXT-X-VERSION:7\n";
    int target = 1;
    for (double duration : durations) target = qMax(target, qCeil(duration));
    out += "#EXT-X-TARGETDURATION:" + QByteArray::number(target) + '\n';
    out += "#EXT-X-MEDIA-SEQUENCE:1\n#EXT-X-PLAYLIST-TYPE:VOD\n";
    const bool av = track.startsWith(QStringLiteral("video")) || track.startsWith(QStringLiteral("audio"));
    if (av) out += "#EXT-X-MAP:URI=\"" + track.toUtf8() + "/init.mp4?" + query + "\"\n";
    const QByteArray ext = av ? QByteArrayLiteral("m4s") : QByteArrayLiteral("vtt");
    for (int i = 0; i < durations.size(); ++i) {
        out += "#EXTINF:" + QByteArray::number(durations.at(i), 'f', 6) + ",\n";
        out += track.toUtf8() + "/segment" + QByteArray::number(i + 1) + '.' + ext + '?' + query + '\n';
    }
    out += "#EXT-X-ENDLIST";
    return out;
}
class HlsV2Converter::Impl
{
public:
    Executables tools;
    QString id;
    HlsV2Options options;
    bool destroyed = false;
    bool probed = false;
    V2ProbeResult probe;
    QString probeError;
    QString burnUrl;
    QString burnId;

    bool ensureProbe(QString *error)
    {
        if (destroyed) { setError(error, QStringLiteral("MasterConverter is destroyed")); return false; }
        if (probed) { setError(error, probeError); return probeError.isEmpty(); }
        probed = true;
        MediaProbe mediaProbe(tools);
        if (!mediaProbe.probeV2(options.mediaUrl, &probe, &probeError)) {
            setError(error, probeError); return false;
        }
        if (!(probe.format.duration > 0.0)) {
            probeError = QStringLiteral("Live media is not supported");
            setError(error, probeError); return false;
        }
        bool hasAv = false;
        for (const V2Stream &stream : probe.streams)
            if (stream.track == QStringLiteral("video") || stream.track == QStringLiteral("audio")) { hasAv = true; break; }
        if (!hasAv) {
            probeError = QStringLiteral("No video or audio streams found");
            setError(error, probeError); return false;
        }
        setError(error, {}); return true;
    }

    ProcessResult renderAv(const V2Stream &stream) const
    {
        const bool video = stream.track == QStringLiteral("video");
        const bool transcode = options.forceTranscoding
            || (video && (!options.videoCodecs.isEmpty() ? !options.videoCodecs.contains(stream.codec)
                                                        : stream.codec != QStringLiteral("h264")))
            || (!video && (!options.audioCodecs.isEmpty() ? !options.audioCodecs.contains(stream.codec)
                                                          : stream.codec != QStringLiteral("aac") && stream.codec != QStringLiteral("mp3")))
            || (!video && options.maxAudioChannels > 0 && stream.channels > options.maxAudioChannels)
            || (video && (!burnUrl.isEmpty() || !burnId.isEmpty()));
        QStringList args{QStringLiteral("-fflags"), QStringLiteral("+genpts"), QStringLiteral("-i"), options.mediaUrl,
                         QStringLiteral("-map_metadata"), QStringLiteral("-1"), QStringLiteral("-map_chapters"), QStringLiteral("-1")};
        if (video) {
            args << QStringLiteral("-map") << QStringLiteral("0:v:%1").arg(stream.id) << QStringLiteral("-an") << QStringLiteral("-sn");
            if (transcode) {
                QStringList filters;
                if (!burnId.isEmpty()) filters << QStringLiteral("subtitles='%1':stream_index=%2").arg(options.mediaUrl, burnId);
                else if (!burnUrl.isEmpty()) filters << QStringLiteral("subtitles='%1'").arg(burnUrl);
                if (options.maxWidth > 0 && stream.width > options.maxWidth)
                    filters << QStringLiteral("scale=%1:-2:flags=lanczos").arg(options.maxWidth);
                filters << QStringLiteral("format=yuv420p");
                if (!filters.isEmpty()) args << QStringLiteral("-vf") << filters.join(QLatin1Char(','));
                args << QStringLiteral("-c:v") << QStringLiteral("libx264")
                     << QStringLiteral("-preset:v") << QStringLiteral("ultrafast")
                     << QStringLiteral("-profile:v") << QStringLiteral("high")
                     << QStringLiteral("-tune:v") << QStringLiteral("zerolatency")
                     << QStringLiteral("-level") << QStringLiteral("51")
                     << QStringLiteral("-vsync") << QStringLiteral("cfr")
                     << QStringLiteral("-r:v") << QStringLiteral("24")
                     << QStringLiteral("-sc_threshold") << QStringLiteral("0")
                     << QStringLiteral("-g") << QStringLiteral("96")
                     << QStringLiteral("-keyint_min") << QStringLiteral("96")
                     << QStringLiteral("-frag_duration") << QStringLiteral("4000000");
            } else {
                args << QStringLiteral("-c:v") << QStringLiteral("copy")
                     << QStringLiteral("-force_key_frames:v") << QStringLiteral("source")
                     << QStringLiteral("-frag_duration") << QStringLiteral("6000000");
            }
        } else {
            args << QStringLiteral("-map") << QStringLiteral("0:a:%1").arg(stream.id)
                 << QStringLiteral("-vn") << QStringLiteral("-sn");
            if (transcode) {
                args << QStringLiteral("-c:a") << QStringLiteral("aac")
                     << QStringLiteral("-filter:a") << QStringLiteral("apad");
                if (options.maxAudioChannels > 0 && stream.channels > options.maxAudioChannels)
                    args << QStringLiteral("-ac:a") << QString::number(options.maxAudioChannels);
            } else {
                args << QStringLiteral("-c:a") << QStringLiteral("copy");
                if (stream.codec == QStringLiteral("aac"))
                    args << QStringLiteral("-bsf:a") << QStringLiteral("aac_adtstoasc");
            }
            args << QStringLiteral("-frag_duration") << QStringLiteral("4096000");
        }
        args << QStringLiteral("-movflags")
             << (video ? QStringLiteral("frag_keyframe+empty_moov+default_base_moof+delay_moov+dash")
                       : QStringLiteral("empty_moov+default_base_moof+delay_moov+dash"))
             << QStringLiteral("-use_editlist") << QStringLiteral("1")
             << QStringLiteral("-f") << QStringLiteral("mp4") << QStringLiteral("pipe:1");
        return MediaProcess::run(tools.ffmpeg, args, 120000);
    }
};
HlsV2Converter::HlsV2Converter(Executables tools, QString id, HlsV2Options options)
    : d(new Impl)
{
    d->tools = std::move(tools);
    d->id = std::move(id);
    d->options = std::move(options);
    if (d->options.mediaUrl.isEmpty()) d->probeError = QStringLiteral("Invalid media url");
    if (d->options.maxAudioChannels < 0) d->probeError = QStringLiteral("Invalid audio channels");
}

HlsV2Converter::~HlsV2Converter() { destroy(); }

QByteArray HlsV2Converter::playlist(const QString &track, QString *error)
{
    if (!d->probeError.isEmpty() && !d->probed) { setError(error, d->probeError); return {}; }
    if (!d->ensureProbe(error)) return {};
    const QByteArray query = makeQuery(d->options);
    if (track == QStringLiteral("master")) return HlsV2Playlist::master(d->probe, query);
    const V2Stream *stream = findStream(d->probe, track);
    if (!stream) { setError(error, QStringLiteral("Track not found")); return {}; }
    double segmentDuration = 10.0;
    if (stream->track == QStringLiteral("video")) segmentDuration = 6.0;
    else if (stream->track == QStringLiteral("audio")) segmentDuration = 4.096;
    setError(error, {});
    return HlsV2Playlist::media(track, fixedDurations(d->probe.format.duration, segmentDuration), query);
}

QByteArray HlsV2Converter::initSegment(const QString &track, QString *error)
{
    if (!d->ensureProbe(error)) return {};
    const V2Stream *stream = findStream(d->probe, track);
    if (!stream || (stream->track != QStringLiteral("video") && stream->track != QStringLiteral("audio"))) {
        setError(error, QStringLiteral("init segment is available only for A/V streams")); return {};
    }
    const ProcessResult process = d->renderAv(*stream);
    if (process.timedOut || process.exitCode != 0) {
        setError(error, process.error.isEmpty() ? QString::fromUtf8(process.stdErr) : process.error); return {};
    }
    const qsizetype moof = process.stdOut.indexOf("moof");
    if (moof < 4) { setError(error, QStringLiteral("ffmpeg produced no fragmented media")); return {}; }
    setError(error, {});
    return process.stdOut.left(moof - 4);
}
QByteArray HlsV2Converter::mediaSegment(const QString &track, int sequenceNumber,
                                        QString *error)
{
    if (!d->ensureProbe(error)) return {};
    if (sequenceNumber <= 0) { setError(error, QStringLiteral("Sequence number out of range")); return {}; }
    const V2Stream *stream = findStream(d->probe, track);
    if (!stream) { setError(error, QStringLiteral("Track not found")); return {}; }
    if (stream->track == QStringLiteral("subtitle")) {
        const ProcessResult process = MediaProcess::run(d->tools.ffmpeg,
            {QStringLiteral("-i"), d->options.mediaUrl, QStringLiteral("-map"),
             QStringLiteral("0:s:%1").arg(stream->id), QStringLiteral("-f"),
             QStringLiteral("webvtt"), QStringLiteral("pipe:1")}, 120000);
        if (process.timedOut || process.exitCode != 0) {
            setError(error, process.error.isEmpty() ? QString::fromUtf8(process.stdErr) : process.error);
            return {};
        }
        if (sequenceNumber != 1) { setError(error, QStringLiteral("Sequence number out of range")); return {}; }
        setError(error, {}); return process.stdOut;
    }
    const ProcessResult process = d->renderAv(*stream);
    if (process.timedOut || process.exitCode != 0) {
        setError(error, process.error.isEmpty() ? QString::fromUtf8(process.stdErr) : process.error); return {};
    }
    QVector<qsizetype> starts;
    qsizetype pos = 0;
    while ((pos = process.stdOut.indexOf("moof", pos)) >= 4) {
        starts.append(pos - 4); pos += 4;
    }
    if (sequenceNumber > starts.size()) { setError(error, QStringLiteral("Sequence number out of range")); return {}; }
    const qsizetype begin = starts.at(sequenceNumber - 1);
    const qsizetype end = sequenceNumber < starts.size() ? starts.at(sequenceNumber) : process.stdOut.size();
    setError(error, {});
    return process.stdOut.mid(begin, end - begin);
}

bool HlsV2Converter::burnSubtitles(const QString &url, const QString &id, QString *error)
{
    if (d->destroyed) { setError(error, QStringLiteral("MasterConverter is destroyed")); return false; }
    if (url.isEmpty() && id.isEmpty()) { setError(error, QStringLiteral("subtitle url or id required")); return false; }
    d->burnUrl = url; d->burnId = id; setError(error, {}); return true;
}
QJsonObject HlsV2Converter::status(QString *error)
{
    QJsonObject result;
    result.insert(QStringLiteral("id"), d->id);
    result.insert(QStringLiteral("destroyed"), d->destroyed);
    result.insert(QStringLiteral("query"), QString::fromUtf8(makeQuery(d->options)));
    QJsonObject probe;
    probe.insert(QStringLiteral("ready"), d->probed && d->probeError.isEmpty());
    probe.insert(QStringLiteral("error"), d->probeError);
    result.insert(QStringLiteral("probe"), probe);
    if (!d->destroyed && !d->probed) {
        QString ignored;
        d->ensureProbe(&ignored);
    }
    setError(error, d->destroyed ? QStringLiteral("MasterConverter is destroyed") : QString());
    return result;
}

void HlsV2Converter::destroy()
{
    if (!d) return;
    d->destroyed = true;
    d->burnUrl.clear();
    d->burnId.clear();
}

bool HlsV2Converter::isDestroyed() const { return !d || d->destroyed; }

HlsV2Registry::HlsV2Registry(Executables tools, int converterConcurrency, int inactivityMs)
    : m_tools(std::move(tools)), m_converterConcurrency(qMax(1, converterConcurrency)),
      m_inactivityMs(qMax(1, inactivityMs))
{
}

QSharedPointer<HlsV2Converter> HlsV2Registry::acquire(const QString &id,
                                                       const HlsV2Options &options,
                                                       qint64 nowMs)
{
    if (nowMs < 0) nowMs = QDateTime::currentMSecsSinceEpoch();
    auto existing = m_entries.find(id);
    if (existing != m_entries.end()) {
        existing->touchedMs = nowMs;
        return existing->converter;
    }
    while (m_entries.size() >= m_converterConcurrency) {
        auto oldest = m_entries.begin();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
            if (it->touchedMs < oldest->touchedMs) oldest = it;
        oldest->converter->destroy();
        m_entries.erase(oldest);
    }
    Entry entry{QSharedPointer<HlsV2Converter>::create(m_tools, id, options), nowMs};
    m_entries.insert(id, entry);
    return entry.converter;
}
bool HlsV2Registry::contains(const QString &id) const
{
    return m_entries.contains(id);
}

bool HlsV2Registry::isDestroyed(const QString &id) const
{
    const auto it = m_entries.constFind(id);
    return it != m_entries.cend() && it->converter->isDestroyed();
}

void HlsV2Registry::destroy(const QString &id)
{
    const auto it = m_entries.find(id);
    if (it != m_entries.end()) it->converter->destroy();
}

void HlsV2Registry::expireInactive(qint64 nowMs)
{
    if (nowMs < 0) nowMs = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->touchedMs < nowMs - m_inactivityMs) {
            it->converter->destroy();
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
}
QJsonObject HlsV2Registry::status() const
{
    QJsonObject result;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        QJsonObject item;
        item.insert(QStringLiteral("touched"), double(it->touchedMs));
        item.insert(QStringLiteral("destroyed"), it->converter->isDestroyed());
        result.insert(it.key(), item);
    }
    return result;
}

int HlsV2Registry::size() const { return m_entries.size(); }

} // namespace ColosseumServer::Media
