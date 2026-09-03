#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <atomic>
#include <functional>

namespace ColosseumServer::Media {

struct ProcessResult
{
    int exitCode = -1;
    bool crashed = false;
    bool timedOut = false;
    QByteArray stdOut;
    QByteArray stdErr;
    QString error;
};

class MediaProcess
{
public:
    static ProcessResult run(const QString &program, const QStringList &arguments,
                             int timeoutMs = 120000,
                             const std::atomic_bool *cancelled = nullptr);
};
struct Executables
{
    QString ffmpeg;
    QString ffprobe;
    QString ffsplit;
};

class ExecutableLocator
{
public:
    static QString locate(const QString &name, const QStringList &searchIn = {});
    static Executables locateAll(const QString &applicationDir = {},
                                 const QString &ffmpegOverride = {},
                                 const QString &ffprobeOverride = {},
                                 const QString &ffsplitOverride = {});
};

struct LegacyStream
{
    QString codecType;
    QString codecName;
    int width = -1;
    int height = -1;
    int stream = -1;
    bool defaultStream = false;
    qint64 bitrate = -1;
    double fps = -1.0;
    QString language;
};
struct LegacyProbeResult
{
    QString container;
    qint64 durationMs = -1;
    qint64 bitrate = -1;
    QVector<LegacyStream> streams;
};

struct V2Format
{
    QString name = QStringLiteral("unknown");
    double duration = -1.0;
};

struct V2Stream
{
    int id = -1;
    int index = -1;
    QString track = QStringLiteral("unknown");
    QString codec = QStringLiteral("unknown");
    qint64 streamBitRate = 0;
    qint64 streamMaxBitRate = 0;
    double startTime = -1.0;
    qint64 startTimeTs = -1;
    int timescale = 1;
    int width = -1;
    int height = -1;
    double frameRate = -1.0;
    qint64 numberOfFrames = -1;
    bool isHdr = false;
    bool isDoVi = false;
    bool hasBFrames = false;
    qint64 formatBitRate = 0;
    qint64 formatMaxBitRate = 0;
    qint64 bps = 0;
    qint64 numberOfBytes = 0;
    double formatDuration = -1.0;
    int sampleRate = -1;
    int channels = -1;
    QString channelLayout = QStringLiteral("unknown");
    QString title;
    QString language;
};

struct V2ProbeResult
{
    V2Format format;
    QVector<V2Stream> streams;
};

class MediaProbe
{
public:
    explicit MediaProbe(Executables executables);
    bool legacyProbe(const QString &mediaUrl, LegacyProbeResult *result,
                     QString *error = nullptr, int timeoutMs = 20000) const;
    bool probeV2(const QString &mediaUrl, V2ProbeResult *result,
                 QString *error = nullptr, int timeoutMs = 120000) const;

private:
    Executables m_executables;
};

struct TrackInfo
{
    int id = -1;
    QString type;
    QString language;
    QString label;
    QString codec;
};

class TrackParser
{
public:
    static QVector<TrackInfo> parseFile(const QString &path,
                                        qint64 maxBytes = 25 * 1024 * 1024,
                                        QString *error = nullptr);
    static QVector<TrackInfo> parseBytes(const QByteArray &bytes,
                                         QString *error = nullptr);
};

enum class SubtitleFormat
{
    Srt,
    Vtt
};

struct SubtitleCue
{
    int number = 0;
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
};

class SubtitleService
{
public:
    static QVector<SubtitleCue> parseSrt(const QByteArray &bytes,
                                         QString *error = nullptr);
    static QByteArray render(const QVector<SubtitleCue> &cues,
                             SubtitleFormat format,
                             qint64 offsetMs = 0);
    static QString openSubHashFile(const QString &path,
                                   QString *error = nullptr);
    static bool retrieve(const QUrl &url, QByteArray *bytes,
                         QString *error = nullptr, int timeoutMs = 20000);
    static bool subtitlesTracks(const QUrl &url, QJsonObject *result,
                                QString *error = nullptr, int timeoutMs = 20000);
    static bool openSubHash(const QUrl &url, QString *hash,
                            QString *error = nullptr, int timeoutMs = 20000);
};

struct EmbeddedSample
{
    QString container;
    QString mime;
    QByteArray bytes;
};
class EmbeddedSamples
{
public:
    static QStringList keys();
    static EmbeddedSample get(const QString &key);
};

struct ProcessInvocation
{
    QString program;
    QStringList arguments;
    QString contentType;
    QHash<QString, QString> headers;
};

struct LegacySegment
{
    qint64 atMs = 0;
    qint64 durationMs = 0;
};

struct LegacyPrepared
{
    LegacyProbeResult probe;
    QVector<int> qualities;
    QVector<LegacySegment> uniformSegments;
    QVector<LegacySegment> frameSegments;
    int videoStreamIndex = -1;
};

class LegacyHls
{
public:
    static LegacyPrepared prepare(const LegacyProbeResult &probe);
    static QByteArray masterPlaylist(const LegacyPrepared &prepared,
                                     const QByteArray &base,
                                     bool multiStream = false,
                                     const QByteArray &query = {});
    static QByteArray streamPlaylist(const LegacyPrepared &prepared,
                                     int quality,
                                     const QString &prefix = {},
                                     const QByteArray &search = {},
                                     bool discontinuity = false);
    static QByteArray subtitlesPlaylist(qint64 durationMs,
                                        const QUrl &subtitleUrl,
                                        const QByteArray &host = {});
    static ProcessInvocation segmentInvocation(const Executables &tools,
                                               const LegacyPrepared &prepared,
                                               const QString &source,
                                               int segmentIndex,
                                               int quality,
                                               int stream = -1);
    static ProcessInvocation dlnaInvocation(const Executables &tools,
                                            const LegacyPrepared &prepared,
                                            const QString &source,
                                            qint64 atMs = 0);
    static ProcessInvocation thumbnailInvocation(const Executables &tools,
                                                 const QString &source,
                                                 qint64 atSeconds);
};

class HlsV2Playlist
{
public:
    static QByteArray master(const V2ProbeResult &probe,
                             const QByteArray &query);
    static QByteArray media(const QString &track,
                            const QVector<double> &durations,
                            const QByteArray &query);
};

struct HlsV2Options
{
    QString mediaUrl;
    int maxAudioChannels = 0;
    QStringList videoCodecs;
    QStringList audioCodecs;
    bool forceTranscoding = false;
    QString profile;
    int maxWidth = 0;
};

class HlsV2Converter
{
public:
    HlsV2Converter(Executables tools, QString id, HlsV2Options options);
    ~HlsV2Converter();

    QByteArray playlist(const QString &track, QString *error = nullptr);
    QByteArray initSegment(const QString &track, QString *error = nullptr);
    QByteArray mediaSegment(const QString &track, int sequenceNumber,
                            QString *error = nullptr);
    bool burnSubtitles(const QString &url, const QString &id,
                       QString *error = nullptr);
    QJsonObject status(QString *error = nullptr);
    void destroy();
    bool isDestroyed() const;

private:
    class Impl;
    QSharedPointer<Impl> d;
};

class HlsV2Registry
{
public:
    HlsV2Registry(Executables tools, int converterConcurrency,
                  int inactivityMs = 120000);

    QSharedPointer<HlsV2Converter> acquire(const QString &id,
                                           const HlsV2Options &options,
                                           qint64 nowMs = -1);
    bool contains(const QString &id) const;
    bool isDestroyed(const QString &id) const;
    void destroy(const QString &id);
    void expireInactive(qint64 nowMs = -1);
    QJsonObject status() const;
    int size() const;

private:
    struct Entry
    {
        QSharedPointer<HlsV2Converter> converter;
        qint64 touchedMs = 0;
    };
    Executables m_tools;
    int m_converterConcurrency = 1;
    int m_inactivityMs = 120000;
    QHash<QString, Entry> m_entries;
};

} // namespace ColosseumServer::Media
