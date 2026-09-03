#include "NetworkAppServices.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QXmlStreamReader>

#include <algorithm>
#include <limits>

namespace colosseum::server::app {
namespace {

QByteArray lower(QByteArrayView value)
{
    return QByteArray(value.data(), value.size()).toLower();
}

AppResponse jsonResponse(const QJsonValue &value, int status = 200,
                         bool withContentType = true)
{
    AppResponse response;
    response.status = status;
    if (withContentType)
        response.headers = {{"Content-Type", "application/json"}};
    if (value.isObject())
        response.body = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    else if (value.isArray())
        response.body = QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    else if (value.isString())
        response.body = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).mid(1);
    else if (value.isBool())
        response.body = value.toBool() ? "true" : "false";
    else if (value.isDouble())
        response.body = QByteArray::number(value.toDouble());
    else
        response.body = "null";
    if (value.isString() && response.body.endsWith(']'))
        response.body.chop(1);
    return response;
}

AppResponse expressInternalError()
{
    AppResponse response;
    response.status = 500;
    response.headers = {{"Content-Type", "text/html; charset=utf-8"}};
    response.body = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<title>Error</title>\n</head>\n<body>\n<pre>Internal Server Error</pre>\n</body>\n</html>\n";
    return response;
}
QStringList optionValues(const QUrlQuery &query, const QString &name)
{
    QStringList values;
    const auto items = query.queryItems(QUrl::FullyDecoded);
    for (const auto &item : items) {
        if (item.first == name)
            values.push_back(item.second);
    }
    return values;
}

QPair<QByteArray, QByteArray> parseHeaderString(const QString &value)
{
    const qsizetype colon = value.indexOf(':');
    if (colon < 0)
        return {value.toUtf8(), {}};
    return {value.left(colon).toUtf8(), value.mid(colon + 1).toUtf8()};
}

HeaderList filterHeaders(const HeaderList &source, const QList<QByteArray> &allowed)
{
    HeaderList result;
    for (const auto &entry : source) {
        const QByteArray key = entry.first.toLower();
        if (allowed.contains(key))
            setHeader(result, key, entry.second);
    }
    return result;
}
struct ProxyOptions {
    QString destination;
    QStringList destinationHeaders;
    QStringList responseHeaders;
};

ProxyOptions parseProxyOptions(const QString &encoded)
{
    const QUrlQuery query(encoded);
    ProxyOptions options;
    options.destination = query.queryItemValue(QStringLiteral("d"), QUrl::FullyDecoded);
    options.destinationHeaders = optionValues(query, QStringLiteral("h"));
    options.responseHeaders = optionValues(query, QStringLiteral("r"));
    return options;
}

QString encodeProxyOptions(const ProxyOptions &options)
{
    QStringList parts;
    parts.push_back(QStringLiteral("d=") + QString::fromLatin1(QUrl::toPercentEncoding(options.destination)));
    for (const QString &header : options.destinationHeaders)
        parts.push_back(QStringLiteral("h=") + QString::fromLatin1(QUrl::toPercentEncoding(header)));
    for (const QString &header : options.responseHeaders)
        parts.push_back(QStringLiteral("r=") + QString::fromLatin1(QUrl::toPercentEncoding(header)));
    return parts.join('&');
}

QString proxyRoot(const ProxyOptions &options)
{
    return QStringLiteral("/proxy/") + encodeProxyOptions(options);
}
QString joinUrlPath(const QString &root, const QString &path)
{
    QString joined = root + QStringLiteral("/") + path;
    joined.replace(QRegularExpression(QStringLiteral("/{2,}")), QStringLiteral("/"));
    return joined;
}

QString rewritePlaylistUrl(const QString &line, const ProxyOptions &options,
                           const QUrl &destination)
{
    if (line.startsWith(QStringLiteral("http://")) ||
        line.startsWith(QStringLiteral("https://"))) {
        const QUrl lineUrl(line);
        if (lineUrl.scheme() != destination.scheme() || lineUrl.host() != destination.host() ||
            lineUrl.port() != destination.port()) {
            ProxyOptions cross;
            cross.destination = lineUrl.scheme() + QStringLiteral("://") + lineUrl.host();
            if (lineUrl.port() >= 0)
                cross.destination += QStringLiteral(":") + QString::number(lineUrl.port());
            cross.destinationHeaders = options.destinationHeaders;
            return joinUrlPath(proxyRoot(cross), lineUrl.path()) +
                   (lineUrl.hasQuery() ? QStringLiteral("?") + lineUrl.query(QUrl::FullyEncoded) : QString());
        }
        return joinUrlPath(proxyRoot(options), lineUrl.path()) +
               (lineUrl.hasQuery() ? QStringLiteral("?") + lineUrl.query(QUrl::FullyEncoded) : QString());
    }
    return line.startsWith('/') ? joinUrlPath(proxyRoot(options), line) : line;
}
QByteArray rewritePlaylist(const QByteArray &body, const ProxyOptions &options,
                           const QUrl &destination)
{
    const QByteArray eol = body.contains("\r\n") ? QByteArray("\r\n") : QByteArray("\n");
    QList<QByteArray> lines = body.split('\n');
    QByteArray output;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        QByteArray raw = lines[i];
        if (raw.endsWith('\r'))
            raw.chop(1);
        QString line = QString::fromUtf8(raw);
        if (!line.startsWith('#') && !line.isEmpty()) {
            line = rewritePlaylistUrl(line, options, destination);
        } else {
            const QRegularExpression uri(QStringLiteral("URI=\\\"([^\\\"]+)\\\""));
            const auto match = uri.match(line);
            if (match.hasMatch()) {
                const QString rewritten = rewritePlaylistUrl(match.captured(1), options, destination);
                line.replace(match.capturedStart(1), match.capturedLength(1), rewritten);
            }
        }
        output += line.toUtf8();
        if (i + 1 < lines.size())
            output += eol;
    }
    return output;
}

QString pathAfterPrefix(const QString &path, const QString &prefix)
{
    return path.startsWith(prefix) ? path.mid(prefix.size()) : QString();
}
QByteArray localManifest(bool catalogEnabled)
{
    if (!catalogEnabled) {
        return QByteArrayLiteral("{\"id\":\"org.stremio.local\",\"version\":\"1.10.0\",\"description\":\"Local add-on to find playable files: .torrent, .mp4, .mkv and .avi\",\"name\":\"Local Files (without catalog support)\",\"resources\":[{\"name\":\"meta\",\"types\":[\"other\"],\"idPrefixes\":[\"local:\",\"bt:\"]},{\"name\":\"stream\",\"types\":[\"movie\",\"series\"],\"idPrefixes\":[\"tt\"]}],\"types\":[\"movie\",\"series\",\"other\"],\"catalogs\":[]}");
    }
    return QByteArrayLiteral("{\"id\":\"org.stremio.local\",\"version\":\"1.10.0\",\"description\":\"Local add-on to find playable files: .torrent, .mp4, .mkv and .avi\",\"name\":\"Local Files\",\"resources\":[\"catalog\",{\"name\":\"meta\",\"types\":[\"other\"],\"idPrefixes\":[\"local:\",\"bt:\"]},{\"name\":\"stream\",\"types\":[\"movie\",\"series\"],\"idPrefixes\":[\"tt\"]}],\"types\":[\"movie\",\"series\",\"other\"],\"catalogs\":[{\"type\":\"other\",\"id\":\"local\"}]}");
}

QJsonObject localFileToJson(const LocalAddonFile &file)
{
    QJsonObject json{{"path", file.path}, {"name", file.name},
                     {"length", static_cast<double>(file.length)},
                     {"idx", file.index}};
    if (!file.parsedName.isEmpty()) json["parsedName"] = file.parsedName;
    if (!file.type.isEmpty()) json["type"] = file.type;
    if (!file.imdbId.isEmpty()) json["imdb_id"] = file.imdbId;
    if (file.season) json["season"] = file.season;
    if (file.episode) json["episode"] = file.episode;
    return json;
}
QJsonObject localEntryToJson(const LocalAddonEntry &entry)
{
    QJsonArray files;
    for (const auto &file : entry.files)
        files.push_back(localFileToJson(file));
    QJsonObject json{{"itemId", entry.itemId}, {"name", entry.name}, {"files", files}};
    if (!entry.infoHash.isEmpty()) json["ih"] = entry.infoHash;
    if (entry.dateModified.isValid()) json["dateModified"] = entry.dateModified.toString(Qt::ISODateWithMs);
    if (!entry.sources.isEmpty()) json["sources"] = QJsonArray::fromStringList(entry.sources);
    return json;
}

LocalAddonFile localFileFromJson(const QJsonObject &json)
{
    LocalAddonFile file;
    file.path = json.value("path").toString();
    file.name = json.value("name").toString();
    file.length = static_cast<qint64>(json.value("length").toDouble());
    file.index = json.value("idx").toInt();
    file.parsedName = json.value("parsedName").toString();
    file.type = json.value("type").toString();
    file.imdbId = json.value("imdb_id").toString();
    file.season = json.value("season").toInt();
    file.episode = json.value("episode").toInt();
    return file;
}
LocalAddonEntry localEntryFromJson(const QString &primaryKey, const QJsonObject &json)
{
    LocalAddonEntry entry;
    entry.primaryKey = primaryKey;
    entry.itemId = json.value("itemId").toString();
    entry.infoHash = json.value("ih").toString();
    entry.name = json.value("name").toString();
    entry.dateModified = QDateTime::fromString(json.value("dateModified").toString(), Qt::ISODateWithMs);
    for (const auto &value : json.value("files").toArray())
        entry.files.push_back(localFileFromJson(value.toObject()));
    for (const auto &value : json.value("sources").toArray())
        entry.sources.push_back(value.toString());
    return entry;
}

QString localVideoId(const LocalAddonFile &file)
{
    if (file.imdbId.isEmpty())
        return file.path;
    if (file.season && file.episode)
        return QStringLiteral("%1:%2:%3").arg(file.imdbId).arg(file.season).arg(file.episode);
    return file.imdbId;
}

QJsonObject genericMetaForEntry(const LocalAddonEntry &entry)
{
    const LocalAddonFile *imdbFile = nullptr;
    const LocalAddonFile *biggestNamedFile = nullptr;
    for (const LocalAddonFile &file : entry.files) {
        if (!imdbFile && !file.imdbId.isEmpty())
            imdbFile = &file;
        if (!file.parsedName.isEmpty() &&
            (!biggestNamedFile || file.length > biggestNamedFile->length))
            biggestNamedFile = &file;
    }
    QJsonObject meta{{"id", entry.itemId}, {"type", "other"},
                     {"name", biggestNamedFile ? biggestNamedFile->parsedName : entry.name},
                     {"showAsVideos", true}};
    if (imdbFile) {
        const QString base = QStringLiteral("https://images.metahub.space");
        meta["poster"] = base + QStringLiteral("/poster/medium/") + imdbFile->imdbId + QStringLiteral("/img");
        meta["background"] = base + QStringLiteral("/background/medium/") + imdbFile->imdbId + QStringLiteral("/img");
        meta["logo"] = base + QStringLiteral("/logo/medium/") + imdbFile->imdbId + QStringLiteral("/img");
    }
    return meta;
}

bool certificateValid(const HttpsCertificate &certificate)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    return certificate.notBefore.isValid() && certificate.notAfter.isValid() &&
           now >= certificate.notBefore && now <= certificate.notAfter;
}

QByteArray contentTypeJsonUtf8()
{
    return QByteArrayLiteral("application/json; charset=utf-8");
}

} // namespace

QByteArray headerValue(const HeaderList &headers, QByteArrayView name)
{
    const QByteArray target = lower(name);
    for (const auto &header : headers) {
        if (header.first.toLower() == target)
            return header.second;
    }
    return {};
}
bool hasHeader(const HeaderList &headers, QByteArrayView name)
{
    return !headerValue(headers, name).isNull();
}

void setHeader(HeaderList &headers, QByteArray name, QByteArray value)
{
    const QByteArray target = name.toLower();
    for (auto &header : headers) {
        if (header.first.toLower() == target) {
            header.first = std::move(name);
            header.second = std::move(value);
            return;
        }
    }
    headers.push_back({std::move(name), std::move(value)});
}

ProxyFetchResponse QtProxyTransport::fetch(const ProxyFetchRequest &request,
                                           const std::atomic_bool *cancelled)
{
    if (cancelled && cancelled->load())
        return {.error = QStringLiteral("cancelled")};

    QNetworkAccessManager manager;
    QNetworkRequest networkRequest(request.url);
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::ManualRedirectPolicy);
    for (const auto &header : request.headers)
        networkRequest.setRawHeader(header.first, header.second);

    QNetworkReply *reply = manager.sendCustomRequest(networkRequest, request.method);
    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
                     [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer cancelTimer;
    if (cancelled) {
        cancelTimer.setInterval(25);
        QObject::connect(&cancelTimer, &QTimer::timeout, reply, [reply, cancelled]() {
            if (cancelled->load())
                reply->abort();
        });
        cancelTimer.start();
    }
    loop.exec();

    ProxyFetchResponse response;
    response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    for (const auto &header : reply->rawHeaderPairs())
        response.headers.push_back(header);
    response.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && response.status == 0)
        response.error = reply->errorString();
    reply->deleteLater();
    return response;
}
AppResponse ProxyService::handle(const AppRequest &request,
                                 const std::atomic_bool *cancelled)
{
    static const QList<QByteArray> requestHeaders = {
        "accept", "accept-encoding", "accept-language", "connection",
        "transfer-encoding", "range", "if-range", "user-agent"};
    static const QList<QByteArray> responseHeaders = {
        "accept-ranges", "content-type", "content-length", "content-range",
        "connection", "transfer-encoding", "last-modified", "etag", "server", "date"};

    const QString mounted = pathAfterPrefix(request.path, QStringLiteral("/proxy/"));
    const qsizetype slash = mounted.indexOf('/');
    const QString encodedOptions = slash < 0 ? mounted : mounted.left(slash);
    const QString pathname = slash < 0 ? QString() : mounted.mid(slash);
    ProxyOptions options = parseProxyOptions(encodedOptions);
    QUrl destination(options.destination);
    if (!destination.isValid() || destination.scheme().isEmpty())
        return expressInternalError();
    destination.setPath(pathname);
    destination.setQuery(request.query);

    HeaderList headers = filterHeaders(request.headers, requestHeaders);
    setHeader(headers, "host", destination.authority(QUrl::FullyDecoded).toUtf8());
    for (const QString &raw : options.destinationHeaders) {
        const auto parsed = parseHeaderString(raw);
        setHeader(headers, parsed.first, parsed.second);
    }
    ProxyFetchResponse fetched;
    int redirectCount = 0;
    for (;;) {
        ProxyFetchRequest upstream;
        upstream.method = request.method;
        upstream.url = destination;
        upstream.headers = headers;
        // Stremio module 805 calls fetch without body even for POST/PUT.
        fetched = transport_.fetch(upstream, cancelled);
        if (!fetched.error.isEmpty())
            return expressInternalError();

        const QByteArray location = headerValue(fetched.headers, "location");
        if (fetched.status >= 300 && fetched.status < 400 && !location.isEmpty()) {
            if (++redirectCount >= 5)
                return expressInternalError();
            QUrl redirectBase = destination;
            redirectBase.setPath(QStringLiteral("/"));
            redirectBase.setQuery(QString());
            redirectBase.setFragment(QString());
            destination = redirectBase.resolved(QUrl(QString::fromUtf8(location)));
            headers = filterHeaders(headers, requestHeaders);
            setHeader(headers, "host", destination.authority(QUrl::FullyDecoded).toUtf8());
            for (const QString &raw : options.destinationHeaders) {
                const auto parsed = parseHeaderString(raw);
                setHeader(headers, parsed.first, parsed.second);
            }
            continue;
        }
        break;
    }

    AppResponse response;
    response.status = fetched.status;
    response.headers = filterHeaders(fetched.headers, responseHeaders);
    for (const QString &raw : options.responseHeaders) {
        const auto parsed = parseHeaderString(raw);
        setHeader(response.headers, parsed.first, parsed.second);
    }

    const QByteArray contentType = headerValue(response.headers, "content-type").toLower();
    const bool playlist = destination.path().endsWith(QStringLiteral(".m3u"), Qt::CaseInsensitive) ||
                          destination.path().endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive) ||
                          contentType.contains("mpegurl");
    if (playlist) {
        for (qsizetype i = response.headers.size() - 1; i >= 0; --i) {
            if (response.headers[i].first.compare("content-length", Qt::CaseInsensitive) == 0)
                response.headers.removeAt(i);
        }
        setHeader(response.headers, "accept-ranges", "none");
        QByteArray transfer = headerValue(response.headers, "transfer-encoding");
        if (transfer.isEmpty())
            transfer = "chunked";
        else if (!transfer.toLower().contains("chunked"))
            transfer += ", chunked";
        setHeader(response.headers, "transfer-encoding", transfer);
        response.body = rewritePlaylist(fetched.body, options, destination);
    } else {
        response.body = fetched.body;
    }
    return response;
}
ProcessYouTubeResolver::ProcessYouTubeResolver(QString executable, int timeoutMs)
    : executable_(std::move(executable)), timeoutMs_(timeoutMs)
{
}

YouTubeResolution ProcessYouTubeResolver::resolveAudioVideo(const QString &id)
{
    QProcess process;
    const QString url = QStringLiteral("https://www.youtube.com/watch?v=") + id;
    process.start(executable_, {QStringLiteral("--dump-single-json"),
                                QStringLiteral("--no-playlist"),
                                QStringLiteral("--no-warnings"), url});
    if (!process.waitForStarted(3000))
        return {{}, process.errorString()};
    if (!process.waitForFinished(timeoutMs_)) {
        process.kill();
        process.waitForFinished(1000);
        return {{}, QStringLiteral("YouTube resolver timed out")};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error.isEmpty()) error = QStringLiteral("YouTube resolver failed");
        return {{}, error};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {{}, QStringLiteral("Invalid response from YouTube resolver")};
    QJsonObject best;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (const QJsonValue &value : document.object().value("formats").toArray()) {
        const QJsonObject format = value.toObject();
        if (format.value("url").toString().isEmpty() ||
            format.value("vcodec").toString() == QStringLiteral("none") ||
            format.value("acodec").toString() == QStringLiteral("none"))
            continue;
        const double height = format.value("height").toDouble();
        const double bitrate = format.value("tbr").toDouble();
        const double score = height * 1'000'000.0 + bitrate;
        if (score > bestScore) {
            bestScore = score;
            best = format;
        }
    }
    return {best, {}};
}

AppResponse YouTubeService::handle(const AppRequest &request)
{
    QString id = pathAfterPrefix(request.path, QStringLiteral("/yt/"));
    const bool wantsJson = id.endsWith(QStringLiteral(".json"));
    if (wantsJson)
        id.chop(5);
    const YouTubeResolution resolution = resolver_.resolveAudioVideo(id);
    if (!resolution.error.isEmpty()) {
        if (wantsJson)
            return jsonResponse(QJsonObject{{"err", resolution.error}}, 404);
        AppResponse response;
        response.status = 403;
        return response;
    }
    const QString url = resolution.format.value("url").toString();
    if (url.isEmpty()) {
        AppResponse response;
        response.status = 404;
        if (wantsJson) {
            response.headers = {{"Content-Type", "application/json"}};
            response.body = "{}";
        }
        return response;
    }
    if (wantsJson)
        return jsonResponse(resolution.format, 200);

    AppResponse response;
    response.status = 301;
    response.headers = {{"Location", url.toUtf8()}};
    return response;
}

QJsonObject castDeviceToJson(const CastDevice &device)
{
    return {{"facility", device.facility}, {"id", device.id}, {"name", device.name},
            {"host", device.host}, {"location", device.location}, {"type", device.type},
            {"icon", device.icon}, {"playerUIRoles", QJsonArray::fromStringList(device.playerUIRoles)},
            {"usePlayerUI", device.usePlayerUI}, {"onlyHtml5Formats", device.onlyHtml5Formats}};
}
void CastDiscoveryRegistry::collect(const CastDevice &device)
{
    if (device.id.isEmpty() || this->device(device.id).has_value())
        return;
    devices_.push_back(device);
}

std::optional<CastDevice> CastDiscoveryRegistry::device(const QString &id) const
{
    for (const auto &device : devices_) {
        if (device.id == id)
            return device;
    }
    return std::nullopt;
}

std::optional<CastDevice> CastDiscoveryRegistry::fromSsdpDescription(
    const QUrl &location, const QByteArray &xml)
{
    QString deviceType;
    QString friendlyName;
    QString udn;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        const QStringView name = reader.name();
        if (name == u"deviceType") deviceType = reader.readElementText();
        else if (name == u"friendlyName") friendlyName = reader.readElementText();
        else if (name == u"UDN") udn = reader.readElementText();
    }
    QString type;
    if (deviceType == QStringLiteral("urn:dial-multiscreen-org:device:dial:1"))
        type = QStringLiteral("chromecast");
    else if (deviceType == QStringLiteral("urn:schemas-upnp-org:device:MediaRenderer:1"))
        type = QStringLiteral("tv");
    else
        return std::nullopt;

    if (udn.startsWith(QStringLiteral("uuid:")))
        udn.remove(0, 5);
    CastDevice device;
    device.facility = QStringLiteral("SSDP");
    device.id = udn;
    device.name = friendlyName;
    device.host = location.host();
    device.location = location.toString();
    device.type = type;
    device.icon = type;
    device.playerUIRoles = {"playpause", "seek", "dub", "subtitles", "volume"};
    return device;
}

std::optional<CastDevice> CastDiscoveryRegistry::fromMdnsRecords(const QJsonObject &records)
{
    const QJsonObject srv = records.value("SRV").toObject();
    const QString target = srv.value("target").toString();
    if (target.isEmpty() || records.value("A").toString().isEmpty())
        return std::nullopt;
    CastDevice device;
    device.facility = QStringLiteral("MDNS");
    device.id = target.section(QStringLiteral(".local"), 0, 0);
    const QJsonObject txt = records.value("TXT").toObject();
    device.name = txt.value("fn").toString();
    if (device.name.isEmpty()) {
        device.name = records.value("PTR").toString();
        device.name.remove(QStringLiteral("._googlecast._tcp.local"));
    }
    device.host = records.value("A").toString();
    device.location = device.host;
    device.type = QStringLiteral("chromecast");
    device.icon = QStringLiteral("chromecast");
    device.playerUIRoles = {"playpause", "seek", "dub", "subtitles", "volume"};
    return device;
}

CastingService::CastingService(CastDiscoveryRegistry &registry, CastSessionFactory &factory,
                               CastTranscoder &transcoder)
    : registry_(registry), factory_(factory), transcoder_(transcoder)
{
}

AppResponse CastingService::handle(const AppRequest &request)
{
    if (request.path == QStringLiteral("/casting/") ||
        request.path == QStringLiteral("/casting")) {
        QJsonArray devices;
        for (const auto &device : registry_.devices())
            devices.push_back(castDeviceToJson(device));
        AppResponse response = jsonResponse(devices);
        setHeader(response.headers, "Content-Type", "application/json; charset=utf8");
        return response;
    }
    if (request.path.startsWith(QStringLiteral("/casting/transcode")) ||
        request.path.startsWith(QStringLiteral("/casting/convert"))) {
        if (request.query.queryItemValue(QStringLiteral("video"), QUrl::FullyDecoded).isEmpty()) {
            AppResponse response;
            response.status = 400;
            response.body = "provide ?video";
            return response;
        }
        const bool fmp4 = !request.query.queryItemValue(QStringLiteral("fmp4"),
                                                        QUrl::FullyDecoded).isEmpty();
        return transcoder_.transcode(request, fmp4);
    }

    const QString relative = pathAfterPrefix(request.path, QStringLiteral("/casting/"));
    const QString id = relative.section('/', 0, 0);
    const auto device = registry_.device(id);
    if (!device.has_value()) {
        AppResponse response;
        response.status = 404;
        response.headers = {{"Content-Type", "text/plain; charset=utf8"}};
        response.body = "Device not found";
        return response;
    }
    if (!relative.endsWith(QStringLiteral("/player"))) {
        AppResponse response = jsonResponse(castDeviceToJson(*device));
        setHeader(response.headers, "Content-Type", "application/json; charset=utf8");
        return response;
    }
    if (!sessions_.contains(id))
        sessions_[id] = factory_.create(*device);
    if (!sessions_[id])
        return expressInternalError();

    QJsonObject &status = mediaStatus_[id];
    if (status.isEmpty()) {
        status = {{"audio", QJsonArray{}}, {"audioTrack", QJsonValue::Null},
                  {"volume", 100}, {"time", 0}, {"paused", false}, {"state", 5},
                  {"length", 0}, {"source", QJsonValue::Null},
                  {"subtitlesSrc", QJsonValue::Null}, {"subtitlesDelay", 0},
                  {"subtitlesSize", 2}};
    }

    QJsonObject params;
    for (const auto &item : request.query.queryItems(QUrl::FullyDecoded))
        params[item.first] = item.second;
    if (!request.body.isEmpty()) {
        const QJsonDocument bodyJson = QJsonDocument::fromJson(request.body);
        if (bodyJson.isObject()) {
            for (auto it = bodyJson.object().begin(); it != bodyJson.object().end(); ++it)
                params[it.key()] = it.value();
        } else {
            const QUrlQuery bodyQuery(QString::fromUtf8(request.body));
            for (const auto &item : bodyQuery.queryItems(QUrl::FullyDecoded))
                params[item.first] = item.second;
        }
    }
    for (auto it = params.begin(); it != params.end(); ++it)
        status[it.key()] = it.value();
    QString method = QStringLiteral("status");
    QJsonArray args;
    if (params.contains("formats")) method = QStringLiteral("protocolsGet");
    if (params.contains("audioTrack")) {
        method = QStringLiteral("audioTrack"); args = {params.value("audioTrack")};
    }
    if (params.contains("volume")) {
        method = QStringLiteral("volume"); args = {params.value("volume")};
    }
    if (params.contains("time")) {
        method = QStringLiteral("seek"); args = {params.value("time")};
    }
    if (params.contains("subtitlesSrc") || params.contains("subtitlesDelay") ||
        params.contains("subtitlesSize")) {
        method = QStringLiteral("subtitles");
        const int size = status.value("subtitlesSize").toVariant().toInt();
        args = {status.value("subtitlesSrc"), status.value("subtitlesDelay"),
                QJsonObject{{"fontSize", QString::number(size + 1) + QStringLiteral("vw")}}};
    }
    if (params.contains("source")) {
        const QString source = params.value("source").toString();
        method = source.isEmpty() ? QStringLiteral("close") : QStringLiteral("play");
        args = source.isEmpty() ? QJsonArray{} : QJsonArray{source};
    }
    if (params.contains("stop")) { method = QStringLiteral("stop"); args = {}; }
    if (params.contains("paused")) {
        const QJsonValue paused = params.value("paused");
        const QString text = paused.toVariant().toString().toLower();
        const bool resume = paused.isBool() ? !paused.toBool() :
                            paused.isDouble() ? paused.toDouble() == 0.0 :
                            text.isEmpty() || text == QStringLiteral("0") ||
                            text == QStringLiteral("false");
        method = resume ? QStringLiteral("resume") : QStringLiteral("pause");
        args = {};
    }

    const QJsonValue result = sessions_[id]->invoke(method, args, status);
    AppResponse response = jsonResponse(result.isUndefined() || result.isNull()
                                            ? QJsonObject{} : result);
    setHeader(response.headers, "Content-Type", "application/json; charset=utf8");
    return response;
}

LocalAddonService::LocalAddonService(bool catalogEnabled, LocalFileDiscovery &discovery,
                                     LocalFileIndexer &indexer)
    : catalogEnabled_(catalogEnabled), discovery_(discovery), indexer_(indexer)
{
}

QByteArray LocalAddonService::manifestBytes() const
{
    return localManifest(catalogEnabled_);
}
bool LocalAddonService::loadStorage(const QString &dbPath)
{
    entries_.clear();
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    QFile file(dbPath);
    if (!file.exists()) {
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.close();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly))
        return false;

    bool truncate = false;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(line);
        const QJsonObject record = document.object();
        const QString key = record.value("id").toString();
        if (!document.isObject() || record.value("v").toString() != QStringLiteral("1.10.0") ||
            key.isEmpty() || !QFileInfo(key).isReadable()) {
            truncate = true;
            continue;
        }
        entries_.insert(key, localEntryFromJson(key, record.value("entry").toObject()));
    }
    file.close();
    if (truncate) {
        QSaveFile clean(dbPath);
        if (!clean.open(QIODevice::WriteOnly))
            return false;
        for (auto it = entries_.cbegin(); it != entries_.cend(); ++it) {
            const QJsonObject record{{"id", it.key()}, {"entry", localEntryToJson(it.value())},
                                     {"v", "1.10.0"}};
            clean.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
            clean.write("\n");
        }
        if (!clean.commit())
            return false;
    }
    return true;
}

bool LocalAddonService::persistEntry(const QString &dbPath, const LocalAddonEntry &entry)
{
    QFile file(dbPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    const QJsonObject record{{"id", entry.primaryKey}, {"entry", localEntryToJson(entry)},
                             {"v", "1.10.0"}};
    const QByteArray bytes = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    return file.write(bytes) == bytes.size();
}

bool LocalAddonService::startIndexing(const QString &dbPath)
{
    if (!loadStorage(dbPath))
        return false;
    for (const QString &path : discovery_.discover()) {
        if (entries_.contains(path) || entries_.size() >= 10000)
            continue;
        const auto indexed = indexer_.indexFile(path);
        if (!indexed.has_value())
            continue;
        LocalAddonEntry entry = *indexed;
        entry.primaryKey = path;
        if (entry.files.isEmpty())
            continue;
        entries_.insert(path, entry);
        if (!persistEntry(dbPath, entry))
            return false;
    }
    return true;
}

QJsonObject LocalAddonService::catalogResponse() const
{
    QJsonArray metas;
    QSet<QString> seen;
    for (const auto &entry : entries_) {
        if (entry.itemId.isEmpty() || entry.files.isEmpty() || seen.contains(entry.itemId))
            continue;
        seen.insert(entry.itemId);
        const LocalAddonFile &file = entry.files.first();
        QJsonObject meta{{"id", entry.itemId}, {"type", "other"},
                         {"name", file.parsedName.isEmpty() ? entry.name : file.parsedName}};
        if (!file.imdbId.isEmpty())
            meta["poster"] = QStringLiteral("https://images.metahub.space/poster/medium/") +
                             file.imdbId + QStringLiteral("/img");
        else
            meta["poster"] = QJsonValue::Null;
        metas.push_back(meta);
    }
    return {{"metas", metas}};
}

QJsonObject LocalAddonService::metaResponse(const QString &id) const
{
    QList<LocalAddonEntry> matches;
    for (const auto &entry : entries_) {
        if (entry.itemId == id)
            matches.push_back(entry);
    }
    if (matches.isEmpty())
        return {};

    LocalAddonEntry aggregate = matches.first();
    for (qsizetype i = 1; i < matches.size(); ++i)
        aggregate.files.append(matches[i].files);
    std::sort(aggregate.files.begin(), aggregate.files.end(), [](const auto &a, const auto &b) {
        if (a.season != b.season) return a.season < b.season;
        return a.episode < b.episode;
    });

    QJsonArray videos;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (qsizetype i = 0; i < aggregate.files.size(); ++i) {
        const LocalAddonFile &file = aggregate.files[i];
        QJsonObject stream;
        if (!aggregate.infoHash.isEmpty()) {
            stream = {{"infoHash", aggregate.infoHash}, {"fileIdx", file.index},
                      {"title", aggregate.infoHash + QStringLiteral("/") + QString::number(file.index)}};
            if (!aggregate.sources.isEmpty()) stream["sources"] = QJsonArray::fromStringList(aggregate.sources);
        } else {
            stream = {{"title", file.path}, {"url", QStringLiteral("file://") + file.path},
                      {"subtitle", "ADDON_STREAM_LOCALFILE"}};
        }
        const QString videoId = localVideoId(file).isEmpty() ? stream.value("title").toString()
                                                              : localVideoId(file);
        QJsonObject video{{"id", videoId}, {"title", file.name}, {"stream", stream},
                          {"released", QDateTime::fromMSecsSinceEpoch(now - 60000 * i,
                                                                     Qt::UTC).toString(Qt::ISODate)}};
        video["publishedAt"] = aggregate.dateModified.isValid()
            ? aggregate.dateModified.toString(Qt::ISODate)
            : QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        if (file.season) video["season"] = file.season;
        if (file.episode) video["episode"] = file.episode;
        if (!file.imdbId.isEmpty()) {
            video["thumbnail"] = file.season && file.episode
                ? QStringLiteral("https://episodes.metahub.space/%1/%2/%3/w780.jpg")
                      .arg(file.imdbId).arg(file.season).arg(file.episode)
                : QStringLiteral("https://images.metahub.space/background/medium/") +
                      file.imdbId + QStringLiteral("/img");
        } else {
            video["thumbnail"] = QJsonValue::Null;
        }
        videos.push_back(video);
    }
    QJsonObject meta = genericMetaForEntry(aggregate);
    meta["videos"] = videos;
    return {{"meta", meta}};
}
QJsonObject LocalAddonService::streamResponse(const QString &type, const QString &id) const
{
    QJsonArray streams;
    if (!id.startsWith(QStringLiteral("tt")) ||
        (type != QStringLiteral("movie") && type != QStringLiteral("series")))
        return {{"streams", streams}};

    const QString baseId = id.section(':', 0, 0);
    for (const auto &entry : entries_) {
        if (entry.itemId == QStringLiteral("local:") + baseId && !entry.files.isEmpty()) {
            const LocalAddonFile &file = entry.files.first();
            if (file.type == type && localVideoId(file) == id) {
                streams.push_back(QJsonObject{{"id", QStringLiteral("file://") + file.path},
                                              {"url", QStringLiteral("file://") + file.path},
                                              {"subtitle", "ADDON_STREAM_LOCALFILE"},
                                              {"title", QFileInfo(file.path).fileName()}});
            }
        }
        if (entry.itemId.startsWith(QStringLiteral("bt:"))) {
            for (qsizetype i = 0; i < entry.files.size(); ++i) {
                const LocalAddonFile &file = entry.files[i];
                if (file.type == type && localVideoId(file) == id) {
                    const int index = static_cast<int>(i);
                    QJsonObject stream{{"title", QFileInfo(file.path).fileName()},
                                       {"infoHash", entry.infoHash}, {"fileIdx", index},
                                       {"id", entry.infoHash + QStringLiteral("/") + QString::number(index)}};
                    if (!entry.sources.isEmpty()) stream["sources"] = QJsonArray::fromStringList(entry.sources);
                    streams.push_back(stream);
                }
            }
        }
    }
    return {{"streams", streams}};
}
AppResponse LocalAddonService::handle(const AppRequest &request) const
{
    AppResponse response;
    response.headers = {{"Access-Control-Allow-Origin", "*"}};
    if (request.path == QStringLiteral("/local-addon/manifest.json")) {
        response.status = 200;
        response.body = manifestBytes();
        setHeader(response.headers, "Content-Type", contentTypeJsonUtf8());
        setHeader(response.headers, "Content-Length", QByteArray::number(response.body.size()));
        return response;
    }

    QString relative = pathAfterPrefix(request.path, QStringLiteral("/local-addon/"));
    if (!relative.endsWith(QStringLiteral(".json")))
        return response;
    relative.chop(5);
    const QStringList parts = relative.split('/');
    if (parts.size() < 3)
        return response;

    QJsonValue body;
    if (parts[0] == QStringLiteral("catalog"))
        body = catalogResponse();
    else if (parts[0] == QStringLiteral("meta")) {
        const QJsonObject meta = metaResponse(parts[2]);
        body = meta.isEmpty() ? QJsonValue::Null : QJsonValue(meta);
    } else if (parts[0] == QStringLiteral("stream"))
        body = streamResponse(parts[1], parts[2]);
    else
        return response;
    response = jsonResponse(body, 200);
    setHeader(response.headers, "Content-Type", contentTypeJsonUtf8());
    setHeader(response.headers, "Access-Control-Allow-Origin", "*");
    return response;
}

QJsonObject QtCertificateTransport::request(const QUrl &endpoint, const QJsonObject &payload,
                                            QString *error)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = manager.post(request,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QByteArray bytes = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        if (error) *error = reply->errorString();
        reply->deleteLater();
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    reply->deleteLater();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Invalid certificate API response");
        return {};
    }
    return document.object();
}
HttpsCertificateService::HttpsCertificateService(CertificateTransport &transport,
                                                 QString appPath, QUrl apiEndpoint)
    : transport_(transport), appPath_(std::move(appPath)), apiEndpoint_(std::move(apiEndpoint))
{
}

QString HttpsCertificateService::certificatePath() const
{
    return QDir(appPath_).filePath(QStringLiteral("httpsCert.json"));
}

std::optional<HttpsCertificate> HttpsCertificateService::parseAndValidate(
    const QJsonObject &value, QString *error) const
{
    HttpsCertificate certificate;
    certificate.domain = value.value("domain").toString();
    certificate.key = value.value("key").toString().toLatin1();
    certificate.cert = value.value("cert").toString().toLatin1();
    certificate.notBefore = QDateTime::fromString(value.value("notBefore").toString(), Qt::ISODate);
    certificate.notAfter = QDateTime::fromString(value.value("notAfter").toString(), Qt::ISODate);
    if (!certificateValid(certificate)) {
        if (error) *error = QStringLiteral("Could not get a valid HTTPS certificate");
        return std::nullopt;
    }
    return certificate;
}

std::optional<HttpsCertificate> HttpsCertificateService::requestNewCertificate(
    const QString &ipAddress, const QString &authKey, QString *error)
{
    QString transportError;
    const QJsonObject response = transport_.request(
        apiEndpoint_, QJsonObject{{"authKey", authKey}, {"ipAddress", ipAddress}}, &transportError);
    if (response.isEmpty()) {
        if (error) *error = transportError;
        return std::nullopt;
    }
    const QString encodedCertificate = response.value("result").toObject()
                                           .value("certificate").toString();
    QJsonParseError parseError;
    const QJsonDocument certificateDocument = QJsonDocument::fromJson(
        encodedCertificate.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !certificateDocument.isObject()) {
        if (error) *error = QStringLiteral("Could not get a valid HTTPS certificate");
        return std::nullopt;
    }
    const QJsonObject wire = certificateDocument.object();
    const QJsonObject contents = wire.value("contents").toObject();
    HttpsCertificate certificate;
    certificate.domain = ipAddress;
    certificate.domain.replace('.', '-');
    certificate.domain += wire.value("commonName").toString().replace(QStringLiteral("*"), QString());
    certificate.key = QByteArray::fromBase64(contents.value("PrivateKey").toString().toLatin1());
    certificate.cert = QByteArray::fromBase64(contents.value("Certificate").toString().toLatin1());
    certificate.notBefore = QDateTime::fromString(contents.value("NotBefore").toString(), Qt::ISODate);
    certificate.notAfter = QDateTime::fromString(contents.value("NotAfter").toString(), Qt::ISODate);
    if (!certificateValid(certificate)) {
        if (error) *error = QStringLiteral("Could not get a valid HTTPS certificate");
        return std::nullopt;
    }
    QDir().mkpath(appPath_);
    QSaveFile file(certificatePath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return std::nullopt;
    }
    const QJsonObject stored{{"domain", certificate.domain},
                             {"key", QString::fromLatin1(certificate.key)},
                             {"cert", QString::fromLatin1(certificate.cert)},
                             {"notBefore", contents.value("NotBefore").toString()},
                             {"notAfter", contents.value("NotAfter").toString()}};
    file.write(QJsonDocument(stored).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return std::nullopt;
    }
    if (error) error->clear();
    return certificate;
}

std::optional<HttpsCertificate> HttpsCertificateService::cachedCertificate(QString *error) const
{
    QFile file(certificatePath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Could not get a valid HTTPS certificate");
        return std::nullopt;
    }
    return parseAndValidate(document.object(), error);
}

QStringList SystemNetworkInterfaceProvider::ipv4Interfaces(QString *error)
{
    Q_UNUSED(error);
    QStringList result;
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
                result.push_back(address.toString());
        }
    }
    result.removeDuplicates();
    return result;
}

NetworkRouteService::NetworkRouteService(HttpsCertificateService &certificates,
                                         NetworkInterfaceProvider &interfaces,
                                         HardwareAccelerationProfiler &profiler,
                                         int httpPort, int httpsPort, QUrl webUiLocation)
    : certificates_(certificates), interfaces_(interfaces), profiler_(profiler),
      httpPort_(httpPort), httpsPort_(httpsPort), webUiLocation_(std::move(webUiLocation))
{
}

AppResponse NetworkRouteService::heartbeat() const
{
    AppResponse response;
    response.status = 200;
    response.headers = {{"Content-Type", "application/json"}};
    response.body = "{\"success\":true}";
    return response;
}

AppResponse NetworkRouteService::root(const AppRequest &request) const
{
    const QByteArray host = headerValue(request.headers, "host");
    if (host.isEmpty())
        return expressInternalError();
    const QByteArray protocol = request.encrypted ? QByteArray("https://") : QByteArray("http://");
    const QByteArray serverUrl = protocol + host;
    QByteArray location = webUiLocation_.toString().toUtf8();
    location += location.contains('?') ? '&' : '?';
    location += "streamingServer=" + QUrl::toPercentEncoding(QString::fromUtf8(serverUrl));
    AppResponse response;
    response.status = 307;
    response.headers = {{"Location", location}};
    return response;
}

AppResponse NetworkRouteService::networkInfo()
{
    QString error;
    const QStringList addresses = interfaces_.ipv4Interfaces(&error);
    if (!error.isEmpty()) {
        AppResponse response;
        response.status = 500;
        response.headers = {{"Content-Type", "text/plain"},
                            {"Content-Length", QByteArray::number(error.toUtf8().size())}};
        response.body = error.toUtf8();
        return response;
    }
    return jsonResponse(QJsonObject{{"availableInterfaces", QJsonArray::fromStringList(addresses)}});
}

AppResponse NetworkRouteService::deviceInfo()
{
    const QJsonValue profiles = profiler_.profile(httpPort_);
    return jsonResponse(QJsonObject{{"availableHardwareAccelerations", profiles}});
}

AppResponse NetworkRouteService::profilerResult()
{
    const QJsonValue profiles = profiler_.profile(httpPort_);
    if (profiles.isNull() || profiles.isUndefined()) {
        AppResponse response;
        response.status = 500;
        response.headers = {{"Content-Type", "text/plain"}};
        response.body = "No viable hardware acceleration profiles detected";
        setHeader(response.headers, "Content-Length", QByteArray::number(response.body.size()));
        return response;
    }
    return jsonResponse(profiles, 200, false);
}

AppResponse NetworkRouteService::getHttps(const AppRequest &request)
{
    const QString ipAddress = request.query.queryItemValue(QStringLiteral("ipAddress"),
                                                           QUrl::FullyDecoded);
    const QString authKey = request.query.queryItemValue(QStringLiteral("authKey"),
                                                         QUrl::FullyDecoded);
    QString error;
    const auto certificate = certificates_.requestNewCertificate(ipAddress, authKey, &error);
    if (!certificate.has_value()) {
        AppResponse response;
        response.status = 500;
        response.headers = {{"Content-Type", "text/plain"}};
        response.body = "Cannot get valid certificate";
        setHeader(response.headers, "Content-Length", QByteArray::number(response.body.size()));
        return response;
    }
    const QJsonObject body{{"ipAddress", ipAddress}, {"domain", certificate->domain},
                           {"port", httpsPort_}};
    AppResponse response = jsonResponse(body);
    setHeader(response.headers, "Content-Length", QByteArray::number(response.body.size()));
    return response;
}

AppResponse NetworkRouteService::handle(const AppRequest &request)
{
    if (request.path == QStringLiteral("/heartbeat"))
        return heartbeat();
    if (request.path == QStringLiteral("/"))
        return root(request);
    if (request.path == QStringLiteral("/network-info"))
        return networkInfo();
    if (request.path == QStringLiteral("/device-info"))
        return deviceInfo();
    if (request.path == QStringLiteral("/hwaccel-profiler"))
        return profilerResult();
    if (request.path == QStringLiteral("/get-https"))
        return getHttps(request);
    return {};
}

} // namespace colosseum::server::app
