#include "FeatureRouteComposition.h"

#include "AsyncMediaExecutor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrlQuery>

#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

namespace colosseum::server::integration {
namespace {

namespace Media = ::ColosseumServer::Media;
namespace Remote = ::Colosseum::Server::RemoteArchive;

QByteArray jsonBytes(const QJsonValue &value)
{
    if (value.isObject())
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    if (value.isArray())
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    if (value.isString())
        return QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).mid(1);
    if (value.isBool())
        return value.toBool() ? QByteArrayLiteral("true") : QByteArrayLiteral("false");
    if (value.isDouble())
        return QByteArray::number(value.toDouble());
    return QByteArrayLiteral("null");
}

app::AppResponse jsonResponse(const QJsonValue &value, int status = 200)
{
    app::AppResponse response;
    response.status = status;
    response.headers = {{QByteArrayLiteral("Content-Type"),
                         QByteArrayLiteral("application/json")}};
    response.body = jsonBytes(value);
    response.headers.push_back({QByteArrayLiteral("Content-Length"),
                                QByteArray::number(response.body.size())});
    return response;
}

app::AppResponse textResponse(int status, const QByteArray &body,
                              const QByteArray &contentType = "text/plain")
{
    app::AppResponse response;
    response.status = status;
    response.headers = {{QByteArrayLiteral("Content-Type"), contentType},
                        {QByteArrayLiteral("Content-Length"),
                         QByteArray::number(body.size())}};
    response.body = body;
    return response;
}

app::AppRequest toAppRequest(const server::HttpRequest &request,
                            const NetworkAppRouteDependencies &dependencies)
{
    app::AppRequest converted;
    converted.method = request.method.toLatin1();
    converted.path = request.path;
    converted.headers.reserve(request.headers.size());
    for (auto it = request.headers.cbegin(); it != request.headers.cend(); ++it)
        converted.headers.push_back({it.key(), it.value()});
    for (auto it = request.query.cbegin(); it != request.query.cend(); ++it) {
        for (const QString &value : it.value())
            converted.query.addQueryItem(it.key(), value);
    }
    converted.body = request.body;
    // The same router serves HTTP and HTTPS. The connection is the authority
    // for the scheme of this request; the dependency value remains a useful
    // compatibility default for direct composition harnesses.
    converted.encrypted = request.encrypted || dependencies.encrypted;
    converted.localPort = dependencies.localPort;
    return converted;
}

Remote::Request toRemoteRequest(const server::HttpRequest &request,
                                const QString &prefix)
{
    Remote::Request converted;
    converted.method = request.method.toLatin1();
    converted.path = request.path.mid(prefix.size());
    if (converted.path.isEmpty())
        converted.path = QStringLiteral("/");
    for (auto it = request.query.cbegin(); it != request.query.cend(); ++it) {
        for (const QString &value : it.value())
            converted.query.addQueryItem(it.key(), value);
    }
    converted.headers = request.headers;
    converted.body = request.body;
    if (request.cancellation) {
        converted.cancellation = std::make_shared<Remote::CancellationToken>();
        const auto cancellation = converted.cancellation;
        request.cancellation->addCancelCallback([cancellation] {
            cancellation->cancel();
        });
    }
    return converted;
}

void finishAppResponse(server::HttpResponse response, app::AppResponse result)
{
    if (response.isFinished())
        return;
    for (const auto &header : result.headers)
        response.setHeader(header.first, header.second);
    response.writeHead(result.status);
    response.end(result.body);
}

app::AppResponse remoteAsAppResponse(const Remote::Response &result)
{
    app::AppResponse converted;
    converted.status = result.status;
    for (auto it = result.headers.cbegin(); it != result.headers.cend(); ++it)
        converted.headers.push_back({it.key(), it.value()});
    converted.body = result.body;
    return converted;
}

template <typename Service>
bool dispatchService(const server::HttpRequest &request, server::HttpResponse response,
                     Service *service, app::AppResponse (Service::*handler)(const app::AppRequest &),
                     app::AppRequest converted, const std::shared_ptr<std::mutex> &serial,
                     std::shared_ptr<void> lifetime = {})
{
    if (!service)
        return false;
    AsyncMediaExecutor::run(request.cancellation,
        [service, handler, converted = std::move(converted), serial,
         lifetime = std::move(lifetime)]() mutable {
            std::lock_guard lock(*serial);
            return (service->*handler)(converted);
        },
        [response](app::AppResponse result) mutable {
            finishAppResponse(response, std::move(result));
        });
    return true;
}

bool ownsPrefix(const QString &path, const QString &prefix)
{
    return path == prefix || path.startsWith(prefix + QLatin1Char('/'));
}

bool isGetLike(const server::HttpRequest &request)
{
    return request.method == QStringLiteral("GET") || request.method == QStringLiteral("HEAD");
}

QString pathPart(const QString &value)
{
    return QUrl::fromPercentEncoding(value.toUtf8());
}

QString queryValue(const server::HttpRequest &request, const QString &name)
{
    return request.queryValues(name).value(0);
}

QUrl normalizedMediaUrl(const QString &value, const QUrl &base)
{
    const QString decoded = pathPart(value);
    QUrl result(decoded);
    if (!result.isValid() || result.scheme().isEmpty()) {
        QUrl local = base;
        QString path = decoded;
        if (!path.startsWith(QLatin1Char('/')))
            path.prepend(QLatin1Char('/'));
        local.setPath(local.path().chopped(local.path().endsWith('/') ? 1 : 0) + path);
        result = local;
    }
    return result;
}

QJsonObject v2StreamJson(const Media::V2Stream &stream)
{
    return {{QStringLiteral("id"), stream.id},
            {QStringLiteral("index"), stream.index},
            {QStringLiteral("track"), stream.track},
            {QStringLiteral("codec"), stream.codec},
            {QStringLiteral("streamBitRate"), double(stream.streamBitRate)},
            {QStringLiteral("streamMaxBitRate"), double(stream.streamMaxBitRate)},
            {QStringLiteral("startTime"), stream.startTime},
            {QStringLiteral("startTimeTs"), double(stream.startTimeTs)},
            {QStringLiteral("timescale"), stream.timescale},
            {QStringLiteral("width"), stream.width},
            {QStringLiteral("height"), stream.height},
            {QStringLiteral("frameRate"), stream.frameRate},
            {QStringLiteral("numberOfFrames"), double(stream.numberOfFrames)},
            {QStringLiteral("isHdr"), stream.isHdr},
            {QStringLiteral("isDoVi"), stream.isDoVi},
            {QStringLiteral("hasBFrames"), stream.hasBFrames},
            {QStringLiteral("formatBitRate"), double(stream.formatBitRate)},
            {QStringLiteral("formatMaxBitRate"), double(stream.formatMaxBitRate)},
            {QStringLiteral("bps"), double(stream.bps)},
            {QStringLiteral("numberOfBytes"), double(stream.numberOfBytes)},
            {QStringLiteral("formatDuration"), stream.formatDuration},
            {QStringLiteral("sampleRate"), stream.sampleRate},
            {QStringLiteral("channels"), stream.channels},
            {QStringLiteral("channelLayout"), stream.channelLayout},
            {QStringLiteral("title"), stream.title},
            {QStringLiteral("language"), stream.language}};
}

app::AppResponse v2ProbeResponse(const Media::V2ProbeResult &probe)
{
    QJsonArray streams;
    for (const Media::V2Stream &stream : probe.streams)
        streams.append(v2StreamJson(stream));
    return jsonResponse(QJsonObject{
        {QStringLiteral("format"), QJsonObject{
            {QStringLiteral("name"), probe.format.name},
            {QStringLiteral("duration"), probe.format.duration}}},
        {QStringLiteral("streams"), streams}});
}

QJsonObject trackJson(const Media::TrackInfo &track)
{
    return {{QStringLiteral("id"), track.id}, {QStringLiteral("type"), track.type},
            {QStringLiteral("language"), track.language},
            {QStringLiteral("label"), track.label}, {QStringLiteral("codec"), track.codec}};
}

app::AppResponse fixedMediaResponse(const QByteArray &body, const QByteArray &contentType,
                                    int status = 200)
{
    app::AppResponse response;
    response.status = status;
    response.headers = {{QByteArrayLiteral("Content-Type"), contentType},
                        {QByteArrayLiteral("Content-Length"),
                         QByteArray::number(body.size())}};
    response.body = body;
    return response;
}

bool handleSample(const server::HttpRequest &request, server::HttpResponse response)
{
    if (!request.path.startsWith(QStringLiteral("/samples/")))
        return false;
    if (!isGetLike(request))
        return false;
    const QString requested = request.path.mid(QStringLiteral("/samples/").size());
    for (const QString &key : Media::EmbeddedSamples::keys()) {
        const Media::EmbeddedSample sample = Media::EmbeddedSamples::get(key);
        if (requested == key + QLatin1Char('.') + sample.container) {
            response.writeHead(200, {{QByteArrayLiteral("Content-Type"), sample.mime.toUtf8()},
                                      {QByteArrayLiteral("Content-Length"),
                                       QByteArray::number(sample.bytes.size())}});
            response.end(sample.bytes);
            return true;
        }
    }
    response.writeHead(404);
    response.end();
    return true;
}

bool handleSubtitles(const server::HttpRequest &request, server::HttpResponse response,
                     const MediaRouteDependencies &dependencies,
                     const std::shared_ptr<std::mutex> &serial)
{
    if (!isGetLike(request))
        return false;
    if (request.path == QStringLiteral("/subtitlesTracks")) {
        const QUrl url = normalizedMediaUrl(queryValue(request, QStringLiteral("subsUrl")),
                                            dependencies.loopbackBaseUrl);
        AsyncMediaExecutor::run(request.cancellation,
            [url, dependencies, serial]() mutable {
                std::lock_guard lock(*serial);
                QJsonObject result;
                QString error;
                const bool ok = dependencies.subtitlesTracks
                    ? dependencies.subtitlesTracks(url, &result, &error)
                    : Media::SubtitleService::subtitlesTracks(url, &result, &error);
                const QJsonValue errorValue = ok ? QJsonValue(QJsonValue::Null) : QJsonValue(error);
                return jsonResponse(QJsonObject{{QStringLiteral("error"), errorValue},
                                                {QStringLiteral("result"), result}},
                                     ok ? 200 : 500);
            },
            [response](app::AppResponse result) mutable {
                finishAppResponse(response, std::move(result));
            });
        return true;
    }

    if (request.path == QStringLiteral("/opensubHash")) {
        const QUrl url = normalizedMediaUrl(queryValue(request, QStringLiteral("videoUrl")),
                                            dependencies.loopbackBaseUrl);
        AsyncMediaExecutor::run(request.cancellation,
            [url, dependencies, serial]() mutable {
                std::lock_guard lock(*serial);
                QString hash;
                QString error;
                const bool ok = dependencies.openSubHash
                    ? dependencies.openSubHash(url, &hash, &error)
                    : Media::SubtitleService::openSubHash(url, &hash, &error);
                return jsonResponse(QJsonObject{
                    {QStringLiteral("error"), ok ? QJsonValue(QJsonValue::Null) : QJsonValue(error)},
                    {QStringLiteral("result"), ok ? QJsonValue(hash) : QJsonValue(QJsonValue::Null)}},
                    ok ? 200 : 500);
            },
            [response](app::AppResponse result) mutable {
                finishAppResponse(response, std::move(result));
            });
        return true;
    }

    if (!request.path.startsWith(QStringLiteral("/subtitles.")))
        return false;
    const bool vtt = request.path.startsWith(QStringLiteral("/subtitles.vtt"));
    if (!vtt && !request.path.startsWith(QStringLiteral("/subtitles.srt")))
        return false;
    const QUrl url = normalizedMediaUrl(queryValue(request, QStringLiteral("from")),
                                        dependencies.loopbackBaseUrl);
    bool offsetOk = false;
    const qint64 offset = queryValue(request, QStringLiteral("offset")).toLongLong(&offsetOk);
    AsyncMediaExecutor::run(request.cancellation,
        [url, offset = offsetOk ? offset : 0, vtt, dependencies, serial]() mutable {
            std::lock_guard lock(*serial);
            QJsonObject result;
            QString error;
            const bool ok = dependencies.subtitlesTracks
                ? dependencies.subtitlesTracks(url, &result, &error)
                : Media::SubtitleService::subtitlesTracks(url, &result, &error);
            if (!ok)
                return textResponse(500, error.toUtf8());
            const QJsonArray tracks = result.value(QStringLiteral("tracks")).toArray();
            if (tracks.isEmpty())
                return textResponse(500, QByteArrayLiteral("No subtitle tracks"));
            QVector<Media::SubtitleCue> cues;
            cues.reserve(tracks.size());
            for (const QJsonValue &value : tracks) {
                const QJsonObject item = value.toObject();
                cues.append({item.value(QStringLiteral("number")).toInt(cues.size()),
                             qRound64(item.value(QStringLiteral("startTime")).toDouble()),
                             qRound64(item.value(QStringLiteral("endTime")).toDouble()),
                             item.value(QStringLiteral("text")).toString()});
            }
            return fixedMediaResponse(Media::SubtitleService::render(
                                          cues, vtt ? Media::SubtitleFormat::Vtt
                                                   : Media::SubtitleFormat::Srt, offset),
                                      vtt ? QByteArrayLiteral("text/vtt")
                                          : QByteArrayLiteral("application/x-subrip"));
        },
        [response](app::AppResponse result) mutable {
            finishAppResponse(response, std::move(result));
        });
    return true;
}

bool handleProbeOrTracks(const server::HttpRequest &request, server::HttpResponse response,
                         const MediaRouteDependencies &dependencies,
                         const std::shared_ptr<std::mutex> &serial)
{
    if (!isGetLike(request))
        return false;
    if (request.path == QStringLiteral("/probe")) {
        const QUrl url = normalizedMediaUrl(queryValue(request, QStringLiteral("url")),
                                            dependencies.loopbackBaseUrl);
        AsyncMediaExecutor::run(request.cancellation,
            [url, dependencies, serial]() mutable {
                std::lock_guard lock(*serial);
                Media::V2ProbeResult result;
                QString error;
                const QString source = url.toString();
                const bool ok = dependencies.v2Probe
                    ? dependencies.v2Probe(source, &result, &error)
                    : Media::MediaProbe(dependencies.executables).probeV2(source, &result, &error);
                if (!ok)
                    return textResponse(500, error.toUtf8(), QByteArrayLiteral("application/json"));
                return v2ProbeResponse(result);
            },
            [response](app::AppResponse result) mutable {
                finishAppResponse(response, std::move(result));
            });
        return true;
    }

    if (!request.path.startsWith(QStringLiteral("/tracks/")))
        return false;
    const QString encoded = request.path.mid(QStringLiteral("/tracks/").size());
    const QString source = pathPart(encoded);
    AsyncMediaExecutor::run(request.cancellation,
        [source, dependencies, serial]() mutable {
            std::lock_guard lock(*serial);
            QString error;
            QVector<Media::TrackInfo> tracks;
            if (dependencies.parseTracks)
                tracks = dependencies.parseTracks(source, &error);
            else {
                const QUrl url(source);
                if (url.isLocalFile())
                    tracks = Media::TrackParser::parseFile(url.toLocalFile(), 25 * 1024 * 1024, &error);
            }
            QJsonArray body;
            for (const Media::TrackInfo &track : tracks)
                body.append(trackJson(track));
            Q_UNUSED(error);
            return jsonResponse(body);
        },
        [response](app::AppResponse result) mutable {
            finishAppResponse(response, std::move(result));
        });
    return true;
}

Media::HlsV2Options hlsOptions(const server::HttpRequest &request,
                               const MediaRouteDependencies &dependencies,
                               const QUrl &mediaUrl)
{
    Media::HlsV2Options options;
    options.mediaUrl = queryValue(request, QStringLiteral("mediaURL"));
    if (options.mediaUrl.isEmpty())
        options.mediaUrl = mediaUrl.toString();
    options.maxAudioChannels = queryValue(request, QStringLiteral("maxAudioChannels"))
                                   .toInt();
    if (options.maxAudioChannels <= 0)
        options.maxAudioChannels = dependencies.defaultMaxAudioChannels;
    options.videoCodecs = request.queryValues(QStringLiteral("videoCodecs"));
    options.audioCodecs = request.queryValues(QStringLiteral("audioCodecs"));
    options.forceTranscoding = !queryValue(request, QStringLiteral("forceTranscoding")).isEmpty();
    options.profile = queryValue(request, QStringLiteral("profile"));
    options.maxWidth = queryValue(request, QStringLiteral("maxWidth")).toInt();
    return options;
}

bool handleHlsV2(const server::HttpRequest &request, server::HttpResponse response,
                 const MediaRouteDependencies &dependencies,
                 const std::shared_ptr<std::mutex> &serial)
{
    if (!isGetLike(request))
        return false;
    if (request.path != QStringLiteral("/hlsv2")
        && !ownsPrefix(request.path, QStringLiteral("/hlsv2")))
        return false;
    if (!dependencies.hlsV2Registry)
        return false;

    const QStringList parts = request.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() == 2 && parts.at(1) == QStringLiteral("status")) {
        response.writeHead(200, {{QByteArrayLiteral("Content-Type"),
                                  QByteArrayLiteral("application/json")}});
        response.end(jsonBytes(dependencies.hlsV2Registry->status()));
        return true;
    }
    if (parts.size() < 3) {
        response.writeHead(404);
        response.end();
        return true;
    }

    const QString infoHash = pathPart(parts.at(1));
    const QString videoId = pathPart(parts.at(2));
    const QString id = infoHash + QLatin1Char('-') + videoId;
    if (parts.size() == 4 && parts.at(3) == QStringLiteral("destroy")) {
        dependencies.hlsV2Registry->destroy(id);
        response.writeHead(200);
        response.end();
        return true;
    }

    QUrl source;
    if (infoHash == QStringLiteral("file"))
        source = QUrl(QStringLiteral("file://") + videoId);
    else if (infoHash == QStringLiteral("url"))
        source = QUrl(videoId);
    else {
        source = dependencies.loopbackBaseUrl;
        source.setPath(source.path().chopped(source.path().endsWith('/') ? 1 : 0)
                       + QLatin1Char('/') + infoHash + QLatin1Char('/') + videoId);
    }
    const Media::HlsV2Options options = hlsOptions(request, dependencies, source);
    const QSharedPointer<Media::HlsV2Converter> converter =
        dependencies.hlsV2Registry->acquire(id, options);
    if (parts.size() < 4 || parts.size() > 5) {
        response.writeHead(404);
        response.end();
        return true;
    }
    QString playlist = pathPart(parts.at(3));
    if (playlist == QStringLiteral("hls.m3u8"))
        playlist = QStringLiteral("master.m3u8");
    const QString track = playlist.endsWith(QStringLiteral(".m3u8"))
        ? playlist.left(playlist.size() - 5) : playlist;
    const QString segment = parts.size() == 5 ? pathPart(parts.at(4)) : QString();

    AsyncMediaExecutor::run(request.cancellation,
        [converter, track, segment, serial]() mutable {
            std::lock_guard lock(*serial);
            QString error;
            if (segment.isEmpty()) {
                const QByteArray body = converter->playlist(track, &error);
                if (!error.isEmpty())
                    return textResponse(500, error.toUtf8());
                return fixedMediaResponse(body, QByteArrayLiteral("application/vnd.apple.mpegurl"));
            }
            if (segment == QStringLiteral("init.mp4")) {
                const QByteArray body = converter->initSegment(track, &error);
                if (!error.isEmpty())
                    return textResponse(500, error.toUtf8());
                return fixedMediaResponse(body, QByteArrayLiteral("video/mp4"));
            }
            const QRegularExpression match(QStringLiteral("^segment(\\d+)\\.(m4s|vtt)$"));
            const auto captured = match.match(segment);
            if (!captured.hasMatch())
                return textResponse(404, {});
            const QByteArray body = converter->mediaSegment(track,
                captured.captured(1).toInt(), &error);
            if (!error.isEmpty())
                return textResponse(500, error.toUtf8());
            return fixedMediaResponse(body,
                captured.captured(2) == QStringLiteral("vtt")
                    ? QByteArrayLiteral("text/vtt") : QByteArrayLiteral("video/mp4"));
        },
        [response](app::AppResponse result) mutable {
            finishAppResponse(response, std::move(result));
        });
    return true;
}

QString legacySource(const QString &first, const QString &second,
                     const server::HttpRequest &request,
                     const MediaRouteDependencies &dependencies)
{
    const QString from = queryValue(request, QStringLiteral("from"));
    if (!from.isEmpty())
        return pathPart(from);
    const QString decodedFirst = pathPart(first);
    const QString decodedSecond = pathPart(second);
    if (decodedFirst.size() == 40) {
        QUrl url = dependencies.loopbackBaseUrl;
        url.setPath(url.path().chopped(url.path().endsWith('/') ? 1 : 0)
                    + QLatin1Char('/') + decodedFirst + QLatin1Char('/') + decodedSecond);
        return url.toString();
    }
    if (decodedFirst == QStringLiteral("file"))
        return QStringLiteral("file://") + decodedSecond;
    if (decodedFirst == QStringLiteral("url"))
        return decodedSecond;
    return {};
}

struct LegacyOperation final
{
    enum Kind { Master, MultiMaster, Stream, Segment, Dlna, Subtitles, Thumb } kind = Master;
    int value = 0;
    int stream = -1;
    int segment = -1;
    QString first;
    QString second;
    QString subtitleLanguage;
};

std::optional<LegacyOperation> parseLegacyOperation(const server::HttpRequest &request)
{
    if (request.path == QStringLiteral("/thumb.jpg"))
        return LegacyOperation{LegacyOperation::Thumb};
    const QStringList parts = request.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return std::nullopt;
    LegacyOperation operation;
    operation.first = parts.at(0);
    operation.second = parts.at(1);
    const QString root = pathPart(operation.first);
    if (root != QStringLiteral("file") && root != QStringLiteral("url")
        && !QRegularExpression(QStringLiteral("^[0-9a-fA-F]{40}$")).match(root).hasMatch())
        return std::nullopt;
    const QString tail = parts.mid(2).join(QLatin1Char('/'));
    if (tail == QStringLiteral("hls.m3u8")) operation.kind = LegacyOperation::Master;
    else if (tail == QStringLiteral("master.m3u8")) operation.kind = LegacyOperation::MultiMaster;
    else if (tail == QStringLiteral("stream.m3u8")) operation.kind = LegacyOperation::Stream;
    else if (tail == QStringLiteral("dlna")) operation.kind = LegacyOperation::Dlna;
    else if (tail == QStringLiteral("thumb.jpg")) operation.kind = LegacyOperation::Thumb;
    else {
        const auto subtitle = QRegularExpression(QStringLiteral("^subs-([^/]+)\\.m3u8$")).match(tail);
        if (subtitle.hasMatch()) {
            operation.kind = LegacyOperation::Subtitles;
            operation.subtitleLanguage = subtitle.captured(1);
        } else {
            const auto stream = QRegularExpression(
                QStringLiteral("^stream(?:-q-(-?\\d+)|-(\\d+))\\.m3u8$")).match(tail);
            const auto segment = QRegularExpression(
                QStringLiteral("^stream(?:-q-(-?\\d+)|-(\\d+))/(\\d+)\\.ts$")).match(tail);
            if (stream.hasMatch()) {
                operation.kind = LegacyOperation::Stream;
                operation.value = stream.captured(1).isEmpty()
                    ? stream.captured(2).toInt() : stream.captured(1).toInt();
                operation.stream = stream.captured(2).isEmpty() ? -1 : operation.value;
            } else if (segment.hasMatch()) {
                operation.kind = LegacyOperation::Segment;
                operation.value = segment.captured(1).isEmpty()
                    ? segment.captured(2).toInt() : segment.captured(1).toInt();
                operation.stream = segment.captured(2).isEmpty() ? -1 : operation.value;
                operation.segment = segment.captured(3).toInt();
            } else {
                return std::nullopt;
            }
        }
    }
    Q_UNUSED(request);
    return operation;
}

app::AppResponse processResponse(const Media::ProcessInvocation &invocation,
                                 const Media::ProcessResult &result)
{
    if (result.timedOut || result.crashed || result.exitCode != 0)
        return textResponse(500, result.error.isEmpty() ? result.stdErr : result.error.toUtf8());
    app::AppResponse response = fixedMediaResponse(result.stdOut, invocation.contentType.toUtf8());
    for (auto it = invocation.headers.cbegin(); it != invocation.headers.cend(); ++it)
        app::setHeader(response.headers, it.key().toUtf8(), it.value().toUtf8());
    return response;
}

bool handleLegacy(const server::HttpRequest &request, server::HttpResponse response,
                  const MediaRouteDependencies &dependencies,
                  const std::shared_ptr<std::mutex> &serial)
{
    if (!isGetLike(request))
        return false;
    const auto operation = parseLegacyOperation(request);
    if (!operation.has_value())
        return false;
    const QString source = operation->first.isEmpty()
        ? pathPart(queryValue(request, QStringLiteral("from")))
        : legacySource(operation->first, operation->second, request, dependencies);
    if (source.isEmpty()) {
        response.writeHead(500);
        response.end(QByteArrayLiteral("media source is required"));
        return true;
    }
    const LegacyOperation op = *operation;
    const qsizetype question = request.rawTarget.indexOf('?');
    const QByteArray query = question < 0 ? QByteArray{}
                                         : request.rawTarget.mid(question + 1);
    const QByteArray host = request.header("host");
    const qint64 atMs = queryValue(request, QStringLiteral("time")).toLongLong();
    const qint64 atSeconds = queryValue(request, QStringLiteral("t")).toLongLong();
    const QUrl subtitleUrl(queryValue(request, QStringLiteral("subsUrl")).isEmpty()
                               ? queryValue(request, QStringLiteral("from"))
                               : queryValue(request, QStringLiteral("subsUrl")));

    AsyncMediaExecutor::runCancellable(request.cancellation,
        [source, op, query, host, subtitleUrl, atMs, atSeconds, dependencies, serial](
            const std::atomic_bool *cancelled) mutable {
            std::lock_guard lock(*serial);
            Media::LegacyProbeResult probe;
            QString error;
            const bool probed = dependencies.legacyProbe
                ? dependencies.legacyProbe(source, &probe, &error)
                : Media::MediaProbe(dependencies.executables).legacyProbe(source, &probe, &error);
            if (!probed)
                return textResponse(500, error.toUtf8());
            const Media::LegacyPrepared prepared = Media::LegacyHls::prepare(probe);
            switch (op.kind) {
            case LegacyOperation::Master:
                return fixedMediaResponse(Media::LegacyHls::masterPlaylist(
                    prepared, "/" + op.first.toUtf8() + "/" + op.second.toUtf8() + "/",
                    false, query), QByteArrayLiteral("application/vnd.apple.mpegurl"));
            case LegacyOperation::MultiMaster:
                return fixedMediaResponse(Media::LegacyHls::masterPlaylist(
                    prepared, "/" + op.first.toUtf8() + "/" + op.second.toUtf8() + "/",
                    true, query), QByteArrayLiteral("application/vnd.apple.mpegurl"));
            case LegacyOperation::Stream:
                return fixedMediaResponse(Media::LegacyHls::streamPlaylist(
                    prepared, op.stream >= 0 ? 0 : op.value,
                    "/" + op.first.toUtf8() + "/" + op.second.toUtf8()
                        + (op.stream >= 0 ? "/stream-" : "/stream-q-")
                        + QByteArray::number(op.value) + "/", query),
                    QByteArrayLiteral("application/vnd.apple.mpegurl"));
            case LegacyOperation::Subtitles:
                return fixedMediaResponse(Media::LegacyHls::subtitlesPlaylist(
                    probe.durationMs, subtitleUrl, host),
                    QByteArrayLiteral("application/vnd.apple.mpegurl"));
            case LegacyOperation::Segment: {
                const Media::ProcessInvocation invocation = Media::LegacyHls::segmentInvocation(
                    dependencies.executables, prepared, source, op.segment, op.value, op.stream);
                return processResponse(invocation, Media::MediaProcess::run(
                    invocation.program, invocation.arguments, 120000, cancelled));
            }
            case LegacyOperation::Dlna: {
                const Media::ProcessInvocation invocation = Media::LegacyHls::dlnaInvocation(
                    dependencies.executables, prepared, source, atMs);
                return processResponse(invocation, Media::MediaProcess::run(
                    invocation.program, invocation.arguments, 120000, cancelled));
            }
            case LegacyOperation::Thumb: {
                const Media::ProcessInvocation invocation = Media::LegacyHls::thumbnailInvocation(
                    dependencies.executables, source, atSeconds);
                return processResponse(invocation, Media::MediaProcess::run(
                    invocation.program, invocation.arguments, 120000, cancelled));
            }
            }
            return textResponse(500, QByteArrayLiteral("unsupported media route"));
        },
        [response](app::AppResponse result) mutable {
            finishAppResponse(response, std::move(result));
        });
    return true;
}

bool handleRemote(const server::HttpRequest &request, server::HttpResponse response,
                  const RemoteArchiveRouteDependencies &dependencies,
                  const std::shared_ptr<std::mutex> &serial)
{
    struct Binding final { QString prefix; Remote::ArchiveKind kind; };
    const QVector<Binding> archives = {
        {QStringLiteral("/rar"), Remote::ArchiveKind::Rar},
        {QStringLiteral("/zip"), Remote::ArchiveKind::Zip},
        {QStringLiteral("/7zip"), Remote::ArchiveKind::SevenZip},
        {QStringLiteral("/tar"), Remote::ArchiveKind::Tar},
        {QStringLiteral("/tgz"), Remote::ArchiveKind::Tgz}};
    for (const Binding &binding : archives) {
        if (!ownsPrefix(request.path, binding.prefix))
            continue;
        if (!dependencies.archives)
            return false;
        const Remote::Request converted = toRemoteRequest(request, binding.prefix);
        AsyncMediaExecutor::run(request.cancellation,
            [service = dependencies.archives, kind = binding.kind,
             converted, serial]() mutable {
                std::lock_guard lock(*serial);
                return remoteAsAppResponse(service->handle(kind, converted));
            },
            [response](app::AppResponse result) mutable {
                finishAppResponse(response, std::move(result));
            });
        return true;
    }

    const auto dispatchRemoteService = [&](const QString &prefix, auto *service) -> bool {
        if (!ownsPrefix(request.path, prefix))
            return false;
        if (!service)
            return false;
        const Remote::Request converted = toRemoteRequest(request, prefix);
        AsyncMediaExecutor::run(request.cancellation,
            [service, converted, serial]() mutable {
                std::lock_guard lock(*serial);
                return remoteAsAppResponse(service->handle(converted));
            },
            [response](app::AppResponse result) mutable {
                finishAppResponse(response, std::move(result));
            });
        return true;
    };
    if (dispatchRemoteService(QStringLiteral("/ftp"), dependencies.ftp))
        return true;
    if (dispatchRemoteService(QStringLiteral("/nzb"), dependencies.nzb))
        return true;
    return false;
}

bool handleSettings(const server::HttpRequest &request, server::HttpResponse response,
                    const FeatureRouteDependencies &dependencies)
{
    if (request.path != QStringLiteral("/settings"))
        return false;
    if (!isGetLike(request) && request.method != QStringLiteral("POST"))
        return false;
    if (request.method == QStringLiteral("POST")) {
        if (dependencies.settings && request.hasJsonBody && request.jsonBody.isObject()) {
            dependencies.settings->extend(request.jsonBody.object());
            dependencies.settings->save();
        }
        response.writeHead(200, {{QByteArrayLiteral("Content-Type"),
                                  QByteArrayLiteral("application/json")} });
        response.end(QByteArrayLiteral("{\"success\":true}"));
        return true;
    }
    QJsonObject body{{QStringLiteral("options"), QJsonObject{}},
                     {QStringLiteral("values"), dependencies.settings
                          ? QJsonValue(dependencies.settings->values()) : QJsonValue(QJsonObject{})},
                     {QStringLiteral("baseUrl"), dependencies.networkApp.engineUrl}};
    const QByteArray bytes = jsonBytes(body);
    response.writeHead(200, {{QByteArrayLiteral("Content-Type"),
                              QByteArrayLiteral("application/json")},
                            {QByteArrayLiteral("Content-Length"),
                             QByteArray::number(bytes.size())}});
    response.end(bytes);
    return true;
}

} // namespace

void mountFeatureRoutes(server::HttpRouter &router,
                        const FeatureRouteDependencies &dependencies)
{
    mountFeatureRoutes(router,
                       std::make_shared<FeatureRouteDependencies>(dependencies));
}

void mountFeatureRoutes(
    server::HttpRouter &router,
    const std::shared_ptr<FeatureRouteDependencies> &dependencies)
{
    if (!dependencies)
        return;
    const auto mediaSerial = std::make_shared<std::mutex>();
    const auto appSerial = std::make_shared<std::mutex>();
    const auto remoteSerial = std::make_shared<std::mutex>();

    router.all(QStringLiteral("/*"),
        [dependencies, mediaSerial, appSerial, remoteSerial](
            server::HttpRequest &request, server::HttpResponse response) mutable {
            const FeatureRouteDependencies &captured = *dependencies;
            if (handleSample(request, response))
                return true;
            if (handleHlsV2(request, response, captured.media, mediaSerial))
                return true;
            if (handleSubtitles(request, response, captured.media, mediaSerial))
                return true;
            if (handleProbeOrTracks(request, response, captured.media, mediaSerial))
                return true;
            if (handleLegacy(request, response, captured.media, mediaSerial))
                return true;
            if (handleSettings(request, response, captured))
                return true;

            const QString &path = request.path;
            const auto appRequest = toAppRequest(request, captured.networkApp);
            if (path == QStringLiteral("/") || path == QStringLiteral("/heartbeat")
                || path == QStringLiteral("/network-info")
                || path == QStringLiteral("/device-info")
                || path == QStringLiteral("/hwaccel-profiler")
                || path == QStringLiteral("/get-https")) {
                if (!isGetLike(request) || !captured.networkApp.network)
                    return false;
                return dispatchService(request, response, captured.networkApp.network,
                                       &app::NetworkRouteService::handle,
                                       appRequest, appSerial,
                                       captured.networkApp.networkLifetime);
            }
            if (path == QStringLiteral("/proxy") || ownsPrefix(path, QStringLiteral("/proxy"))) {
                if (!captured.networkApp.proxy)
                    return false;
                auto cancelled = std::make_shared<std::atomic_bool>(false);
                if (request.cancellation)
                    request.cancellation->addCancelCallback([cancelled] {
                        cancelled->store(true, std::memory_order_release);
                    });
                AsyncMediaExecutor::run(request.cancellation,
                    [service = captured.networkApp.proxy, appRequest,
                     cancelled, appSerial]() mutable {
                        std::lock_guard lock(*appSerial);
                        return service->handle(appRequest, cancelled.get());
                    },
                    [response](app::AppResponse result) mutable {
                        finishAppResponse(response, std::move(result));
                    });
                return true;
            }
            if (ownsPrefix(path, QStringLiteral("/yt"))) {
                return dispatchService(request, response, captured.networkApp.youtube,
                                       &app::YouTubeService::handle,
                                       appRequest, appSerial);
            }
            if (ownsPrefix(path, QStringLiteral("/casting"))) {
                return dispatchService(request, response, captured.networkApp.casting,
                                       &app::CastingService::handle,
                                       appRequest, appSerial);
            }
            if (ownsPrefix(path, QStringLiteral("/local-addon"))) {
                if (!captured.networkApp.localAddon)
                    return false;
                AsyncMediaExecutor::run(request.cancellation,
                    [service = captured.networkApp.localAddon, appRequest, appSerial]() mutable {
                        std::lock_guard lock(*appSerial);
                        return service->handle(appRequest);
                    },
                    [response](app::AppResponse result) mutable {
                        finishAppResponse(response, std::move(result));
                    });
                return true;
            }
            return handleRemote(request, response, captured.remoteArchive, remoteSerial);
        });
}

} // namespace colosseum::server::integration
