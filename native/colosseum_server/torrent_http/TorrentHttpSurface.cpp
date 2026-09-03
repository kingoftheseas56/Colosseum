#include "TorrentHttpSurface.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace colosseum::server::torrent_http {
namespace {

constexpr auto JsonContentType = "application/json";

QByteArray serializeJson(const QJsonValue &value)
{
    if (value.isObject())
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    if (value.isArray())
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    if (value.isNull())
        return "null";
    if (value.isBool())
        return value.toBool() ? "true" : "false";
    if (value.isDouble())
        return QByteArray::number(value.toDouble(), 'g', 16);
    if (value.isString()) {
        QJsonArray wrapper{value};
        QByteArray encoded = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
        return encoded.mid(1, encoded.size() - 2);
    }
    return {};
}

std::optional<qint64> parseInt10(const QString &value)
{
    static const QRegularExpression prefix(QStringLiteral("^\\s*([+-]?\\d+)"));
    const QRegularExpressionMatch match = prefix.match(value);
    if (!match.hasMatch())
        return std::nullopt;
    bool ok = false;
    const qint64 parsed = match.captured(1).toLongLong(&ok, 10);
    return ok ? std::optional<qint64>(parsed) : std::nullopt;
}

std::optional<int> parsePriority(const QByteArray &header)
{
    if (header.isNull())
        return std::nullopt;
    const auto parsed = parseInt10(QString::fromLatin1(header));
    if (!parsed || *parsed == 0)
        return 1;
    if (*parsed > std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();
    if (*parsed < std::numeric_limits<int>::min())
        return std::numeric_limits<int>::min();
    return static_cast<int>(*parsed);
}

struct ParsedRange {
    qint64 start = 0;
    qint64 end = -1;
};

std::optional<ParsedRange> firstRange(qint64 size, const QByteArray &raw)
{
    if (raw.isEmpty() || size < 0)
        return std::nullopt;
    const QString text = QString::fromLatin1(raw);
    const qsizetype equals = text.indexOf(QLatin1Char('='));
    if (equals < 0)
        return std::nullopt;

    const QStringList candidates = text.mid(equals + 1).split(QLatin1Char(','));
    for (const QString &candidate : candidates) {
        const QStringList bounds = candidate.split(QLatin1Char('-'));
        const auto startParsed = bounds.isEmpty() ? std::nullopt : parseInt10(bounds.at(0));
        const auto endParsed = bounds.size() < 2 ? std::nullopt : parseInt10(bounds.at(1));

        qint64 start = 0;
        qint64 end = 0;
        if (!startParsed) {
            if (!endParsed)
                continue;
            start = size - *endParsed;
            end = size - 1;
        } else {
            start = *startParsed;
            end = endParsed ? *endParsed : size - 1;
        }
        if (end > size - 1)
            end = size - 1;
        if (start < 0 || start > end)
            continue;
        return ParsedRange{start, end};
    }
    // module 176 returns -1 here; module 172 indexes [0] and therefore treats it as no range.
    return std::nullopt;
}

bool isHashSegment(const QString &segment)
{
    // Upstream IH_REGEX is intentionally not anchored.
    static const QRegularExpression hash(QStringLiteral("([0-9A-Fa-f]){40}"));
    return hash.match(segment).hasMatch();
}

QRegularExpression filterRegex(const QString &spec)
{
    static const QRegularExpression slashSyntax(QStringLiteral("^/(.*)/(.*)$"));
    const QRegularExpressionMatch slash = slashSyntax.match(spec);
    if (slash.hasMatch()) {
        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
        const QString flags = slash.captured(2);
        bool flagsValid = true;
        for (const QChar flag : flags) {
            if (flag == QLatin1Char('i'))
                options |= QRegularExpression::CaseInsensitiveOption;
            else if (flag == QLatin1Char('m'))
                options |= QRegularExpression::MultilineOption;
            else if (flag == QLatin1Char('s'))
                options |= QRegularExpression::DotMatchesEverythingOption;
            else if (flag == QLatin1Char('u') || flag == QLatin1Char('g') || flag == QLatin1Char('y')) {
                // No matching-mode change is required for the boolean match used by EngineFS.
            } else {
                flagsValid = false;
                break;
            }
        }
        if (flagsValid) {
            QRegularExpression expression(slash.captured(1), options);
            if (expression.isValid())
                return expression;
        }
    }
    return QRegularExpression(spec);
}

bool matchesFilter(const QString &name, const QString &spec)
{
    const QRegularExpression expression = filterRegex(spec);
    return expression.isValid() && expression.match(name).hasMatch();
}

std::optional<int> firstFilterMatch(const QVector<TorrentFileView> &files,
                                    const QStringList &filters)
{
    for (const TorrentFileView &file : files) {
        for (const QString &filter : filters) {
            if (matchesFilter(file.name, filter))
                return file.index;
        }
    }
    return std::nullopt;
}

QStringList jsonStrings(const QJsonValue &value)
{
    QStringList result;
    if (!value.isArray())
        return result;
    for (const QJsonValue &entry : value.toArray()) {
        if (entry.isString())
            result.push_back(entry.toString());
    }
    return result;
}

bool jsonTruthy(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return false;
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toDouble() != 0.0 && !std::isnan(value.toDouble());
    if (value.isString())
        return !value.toString().isEmpty();
    return true;
}

const TorrentFileView *fileAt(const QVector<TorrentFileView> &files, int index)
{
    const auto found = std::find_if(files.cbegin(), files.cend(),
                                    [index](const TorrentFileView &file) {
                                        return file.index == index;
                                    });
    return found == files.cend() ? nullptr : &*found;
}

QByteArray mimeType(const QString &name)
{
    const QString lower = name.toLower();
    struct Entry { const char *suffix; const char *type; };
    static constexpr Entry entries[] = {
        {".mp4", "video/mp4"}, {".m4v", "video/x-m4v"}, {".mkv", "video/x-matroska"},
        {".mk3d", "video/x-matroska"}, {".mks", "video/x-matroska"},
        {".avi", "video/x-msvideo"}, {".webm", "video/webm"}, {".mov", "video/quicktime"},
        {".mpg", "video/mpeg"}, {".mpeg", "video/mpeg"}, {".wmv", "video/x-ms-wmv"},
        {".ts", "video/mp2t"}, {".m3u8", "application/vnd.apple.mpegurl"},
        {".srt", "application/x-subrip"}, {".vtt", "text/vtt"},
        {".mp3", "audio/mpeg"}, {".ogg", "audio/ogg"}, {".wav", "audio/wav"},
        {".wma", "audio/x-ms-wma"}, {".flac", "audio/flac"}, {".aac", "audio/x-aac"},
        {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".png", "image/png"},
    };
    for (const Entry &entry : entries) {
        if (lower.endsWith(QLatin1String(entry.suffix)))
            return entry.type;
    }
    return "application/octet-stream";
}

QByteArray encodedComponent(const QString &value)
{
    return QUrl::toPercentEncoding(value, QByteArray("-_.!~*'()"));
}

} // namespace

QByteArray TorrentHttpRequest::header(const QByteArray &name) const
{
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0)
            return it.value();
    }
    return {};
}

QStringList TorrentHttpRequest::queryValues(const QString &name) const
{
    return query.value(name);
}

bool TorrentHttpRequest::queryTruthy(const QString &name) const
{
    const QStringList values = query.value(name);
    return !values.isEmpty() && !values.first().isEmpty();
}

StreamLease::StreamLease(std::function<void()> close)
    : close_(std::move(close))
{
}

StreamLease::~StreamLease()
{
    close();
}

void StreamLease::close()
{
    bool expected = false;
    if (!closed_.compare_exchange_strong(expected, true))
        return;
    if (close_)
        close_();
}

TorrentHttpSurface::TorrentHttpSurface(TorrentHttpBackend &backend, TorrentCreateSource &source)
    : backend_(backend), source_(source)
{
}

TorrentHttpReply TorrentHttpSurface::jsonReply(const QJsonValue &value, int status) const
{
    TorrentHttpReply response;
    response.status = status;
    response.headers.insert("content-type", JsonContentType);
    response.body = serializeJson(value);
    return response;
}

TorrentHttpReply TorrentHttpSurface::emptyReply(int status) const
{
    TorrentHttpReply response;
    response.status = status;
    return response;
}

bool TorrentHttpSurface::dispatch(const TorrentHttpRequest &request, ReplyCallback reply)
{
    const QByteArray method = request.method.toUpper();
    const QString path = request.path.isEmpty() ? QStringLiteral("/") : request.path;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    if (method == "GET" && path == QStringLiteral("/favicon.ico")) {
        TorrentHttpReply response = emptyReply(404);
        response.headers.insert("content-type", JsonContentType);
        reply(std::move(response));
        return true;
    }

    if (method == "GET" && path == QStringLiteral("/stats.json")) {
        QJsonValue value = backend_.globalStats();
        if (value.isObject() && request.queryValues(QStringLiteral("sys")).contains(QStringLiteral("1"))) {
            QJsonObject object = value.toObject();
            object.insert(QStringLiteral("sys"), backend_.systemStats());
            value = object;
        }
        reply(jsonReply(value));
        return true;
    }

    if (method == "GET" && path == QStringLiteral("/removeAll")) {
        // Module 172 deliberately does not wait for asynchronous engine destruction here.
        backend_.removeAll();
        reply(jsonReply(QJsonObject{}));
        return true;
    }

    if (path == QStringLiteral("/create")) {
        createFromBody(request, std::move(reply));
        return true;
    }

    if (parts.size() == 2 && parts.at(1) == QStringLiteral("create")) {
        createByHash(parts.at(0), request, std::move(reply));
        return true;
    }

    if (method == "GET" && parts.size() == 2 && parts.at(1) == QStringLiteral("remove")) {
        const QString hash = parts.at(0).toLower();
        backend_.remove(hash, [this, reply = std::move(reply)]() mutable {
            reply(jsonReply(QJsonObject{}));
        });
        return true;
    }

    if (method == "GET" && parts.size() == 2 && parts.at(1) == QStringLiteral("stats.json")) {
        reply(jsonReply(backend_.stats(parts.at(0).toLower(), std::nullopt)));
        return true;
    }

    if (method == "GET" && parts.size() == 3 && parts.at(2) == QStringLiteral("stats.json")) {
        bool ok = false;
        const int index = parts.at(1).toInt(&ok);
        reply(jsonReply(backend_.stats(parts.at(0).toLower(), ok ? std::optional<int>(index)
                                                                 : std::nullopt)));
        return true;
    }

    if ((method == "GET" || method == "HEAD") && parts.size() >= 2) {
        serveMedia(request, parts, std::move(reply));
        return true;
    }

    return false;
}

void TorrentHttpSurface::createByHash(const QString &rawHash,
                                      const TorrentHttpRequest &request,
                                      ReplyCallback reply)
{
    const QString hash = rawHash.toLower();
    const QJsonObject body = request.body;
    backend_.ensureEngine(hash, body,
                          [this, hash, body, reply = std::move(reply)](const QString &error) mutable {
        if (!error.isEmpty()) {
            reply(emptyReply(500));
            return;
        }

        QJsonValue statsValue = backend_.stats(hash, std::nullopt);
        if (!statsValue.isObject()) {
            reply(jsonReply(statsValue));
            return;
        }

        QJsonObject object = statsValue.toObject();
        const QVector<TorrentFileView> files = backend_.files(hash);
        const QStringList filters = jsonStrings(body.value(QStringLiteral("fileMustInclude")));
        if (!filters.isEmpty()) {
            const auto selected = firstFilterMatch(files, filters);
            if (selected)
                object.insert(QStringLiteral("guessedFileIdx"), *selected);
        }

        const QJsonValue guessValue = body.value(QStringLiteral("guessFileIdx"));
        if (jsonTruthy(guessValue) && !object.contains(QStringLiteral("guessedFileIdx"))) {
            const QJsonObject hint = guessValue.isObject() ? guessValue.toObject() : QJsonObject{};
            const auto selected = backend_.guessFileIndex(hash, hint);
            if (selected)
                object.insert(QStringLiteral("guessedFileIdx"), *selected);
        }
        reply(jsonReply(object));
    });
}

void TorrentHttpSurface::createFromBody(const TorrentHttpRequest &request, ReplyCallback reply)
{
    auto finishCreate = [this, reply](QByteArray bytes) mutable {
        backend_.createFromTorrent(bytes,
                                   [this, reply = std::move(reply)](const QString &hash,
                                                                   const QString &error) mutable {
            if (!error.isEmpty()) {
                reply(emptyReply(500));
                return;
            }
            reply(jsonReply(backend_.stats(hash.toLower(), std::nullopt)));
        });
    };

    const QJsonValue blob = request.body.value(QStringLiteral("blob"));
    if (blob.isString()) {
        finishCreate(QByteArray::fromHex(blob.toString().toLatin1()));
        return;
    }

    const QJsonValue from = request.body.value(QStringLiteral("from"));
    if (!from.isString()) {
        reply(emptyReply(500));
        return;
    }

    source_.load(from.toString(),
                 [this, finishCreate = std::move(finishCreate), reply](QByteArray bytes,
                                                                       QString error) mutable {
        Q_UNUSED(this);
        if (!error.isEmpty()) {
            reply(emptyReply(500));
            return;
        }
        finishCreate(std::move(bytes));
    });
}

void TorrentHttpSurface::serveMedia(const TorrentHttpRequest &request,
                                    const QStringList &parts,
                                    ReplyCallback reply)
{
    const QString rawHash = parts.at(0);
    if (!isHashSegment(rawHash)) {
        // A 64-character first segment has its own upstream "Not implemented yet" error,
        // but both paths are externally the same 500/empty response.
        reply(emptyReply(500));
        return;
    }
    const QString hash = rawHash.toLower();

    QJsonObject options;
    const QStringList trackers = request.queryValues(QStringLiteral("tr"));
    if (!trackers.isEmpty()) {
        const QJsonObject defaults = backend_.defaultEngineOptions(hash);
        const QJsonObject defaultPeer = defaults.value(QStringLiteral("peerSearch")).toObject();
        QJsonObject peerSearch;
        if (defaultPeer.contains(QStringLiteral("min")))
            peerSearch.insert(QStringLiteral("min"), defaultPeer.value(QStringLiteral("min")));
        if (defaultPeer.contains(QStringLiteral("max")))
            peerSearch.insert(QStringLiteral("max"), defaultPeer.value(QStringLiteral("max")));
        QJsonArray sources;
        for (const QString &tracker : trackers)
            sources.push_back(tracker);
        peerSearch.insert(QStringLiteral("sources"), sources);
        options.insert(QStringLiteral("peerSearch"), peerSearch);
    }

    backend_.ensureEngine(hash, options,
                          [this, request, parts, hash, reply = std::move(reply)](const QString &error) mutable {
        if (!error.isEmpty()) {
            reply(emptyReply(500));
            return;
        }

        const QVector<TorrentFileView> files = backend_.files(hash);
        int selectedIndex = -1;
        bool selected = false;

        const QStringList filters = request.queryValues(QStringLiteral("f"));
        if (!filters.isEmpty()) {
            const auto filtered = firstFilterMatch(files, filters);
            if (filtered) {
                selectedIndex = *filtered;
                selected = true;
            }
        }

        if (!selected) {
            bool numberOk = false;
            const double numeric = parts.at(1).toDouble(&numberOk);
            if (!numberOk) {
                const QString wanted = QUrl::fromPercentEncoding(parts.at(1).toUtf8());
                const auto found = std::find_if(files.cbegin(), files.cend(),
                                                [&wanted](const TorrentFileView &file) {
                                                    return file.name == wanted;
                                                });
                if (found != files.cend()) {
                    selectedIndex = found->index;
                    selected = true;
                }
            } else if (numeric == -1.0) {
                const auto guessed = backend_.guessFileIndex(hash, QJsonObject{});
                if (guessed) {
                    selectedIndex = *guessed;
                    selected = true;
                }
            } else if (std::isfinite(numeric) && std::floor(numeric) == numeric
                       && numeric >= std::numeric_limits<int>::min()
                       && numeric <= std::numeric_limits<int>::max()) {
                selectedIndex = static_cast<int>(numeric);
                selected = true;
            }
        }

        const TorrentFileView *file = selected ? fileAt(files, selectedIndex) : nullptr;
        if (!file) {
            reply(emptyReply(500));
            return;
        }

        if (request.queryTruthy(QStringLiteral("external"))) {
            TorrentHttpReply response = emptyReply(307);
            QByteArray location = "/" + hash.toUtf8() + "/" + encodedComponent(file->name);
            if (request.queryTruthy(QStringLiteral("download")))
                location += "?download=1";
            response.headers.insert("location", location);
            reply(std::move(response));
            return;
        }

        backend_.streamOpened(hash, file->index);
        auto lease = std::make_shared<StreamLease>([this, hash, index = file->index]() {
            backend_.streamClosed(hash, index);
        });

        TorrentHttpReply response;
        response.status = 200;
        response.streamLease = lease;
        response.headers.insert("accept-ranges", "bytes");
        response.headers.insert("content-type", mimeType(file->name));
        response.headers.insert("cache-control", "max-age=0, no-cache");
        response.headers.insert("transfermode.dlna.org", "Streaming");
        response.headers.insert("contentfeatures.dlna.org",
                                "DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=017000 00000000000000000000000000");

        if (request.queryTruthy(QStringLiteral("download"))) {
            response.headers.insert("content-disposition",
                                    QByteArray("attachment; filename=\"") + file->name.toUtf8() + "\";");
        }
        if (request.queryTruthy(QStringLiteral("subtitles"))) {
            response.headers.insert("captioninfo.sec",
                                    request.queryValues(QStringLiteral("subtitles")).first().toUtf8());
        }

        const QByteArray rawRange = request.header("range");
        // Provenance: module 172 uses `circularBuffer || prewarmStream`, so any truthy
        // circular-buffer mode suppresses this open-ended-range prewarm.
        if (!rawRange.isEmpty() && rawRange.endsWith('-')) {
            const QJsonObject defaults = backend_.defaultEngineOptions(hash);
            if (!jsonTruthy(defaults.value(QStringLiteral("circularBuffer"))))
                backend_.prewarm(hash, file->index);
        }
        const auto range = firstRange(file->length, rawRange);
        const QByteArray priorityHeader = request.header("enginefs-prio");
        const auto priority = priorityHeader.isEmpty()
            ? std::optional<int>{}
            : parsePriority(priorityHeader);

        qint64 start = 0;
        qint64 end = file->length - 1;
        if (range) {
            response.status = 206;
            start = range->start;
            end = range->end;
            response.headers.insert("content-length", QByteArray::number(end - start + 1));
            response.headers.insert("content-range",
                                    "bytes " + QByteArray::number(start) + "-" + QByteArray::number(end)
                                        + "/" + QByteArray::number(file->length));
        } else {
            response.headers.insert("content-length", QByteArray::number(file->length));
        }

        if (request.method.toUpper() != "HEAD")
            response.readPlan = TorrentReadPlan{hash, file->index, start, end, priority};
        reply(std::move(response));
    });
}

} // namespace colosseum::server::torrent_http
