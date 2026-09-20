#include "StremioCodec.h"

#include "third_party/miniz/miniz.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QUrlQuery>

#include <cmath>

namespace {
constexpr qsizetype kMaximumCallbackBytes = 16 * 1024;
constexpr qsizetype kMaximumCredentialBytes = 4096;
constexpr qsizetype kMaximumIdentityText = 256;
constexpr qsizetype kMaximumLibraryIdText = 512;
constexpr int kMaximumLibraryBatchSize = 64;
constexpr int kMaximumLibraryRows = 256;
constexpr int kMaximumAddonRows = 64;
constexpr qsizetype kMaximumAddonUrlText = 2048;
constexpr qsizetype kMaximumAddonNameText = 256;
constexpr qsizetype kMaximumAddonDocumentBytes = 32 * 1024;
constexpr int kMaximumWatchedVideos = 4096;
constexpr qsizetype kMaximumWatchedCompressedBytes = 16 * 1024;
constexpr qsizetype kMaximumWatchedRawBytes = 8 * 1024;

bool safeIdentityText(const QString &value) {
    return !value.isEmpty()
        && value.size() <= kMaximumIdentityText
        && value.trimmed() == value
        && !value.contains(QChar::ReplacementCharacter);
}

bool safeCredential(const QByteArray &value) {
    if (value.isEmpty() || value.size() > kMaximumCredentialBytes)
        return false;
    for (const char byte : value) {
        if (static_cast<unsigned char>(byte) < 0x21
            || static_cast<unsigned char>(byte) == 0x7f) {
            return false;
        }
    }
    return true;
}

bool safeLibraryId(const QString &value) {
    return !value.isEmpty()
        && value.size() <= kMaximumLibraryIdText
        && value.trimmed() == value
        && !value.contains(QChar::ReplacementCharacter)
        && !value.contains(QChar::Null);
}

bool supportedLibraryType(const QString &value) {
    return value == QStringLiteral("movie")
        || value == QStringLiteral("series");
}

bool optionalBoolean(const QJsonObject &object, const QString &key, bool *value) {
    const auto it = object.constFind(key);
    if (it == object.constEnd()) {
        if (value)
            *value = false;
        return true;
    }
    if (!it->isBool())
        return false;
    if (value)
        *value = it->toBool();
    return true;
}

QJsonObject datastoreBase(const QByteArray &authKey) {
    if (!safeCredential(authKey))
        return {};
    return QJsonObject{
        {QStringLiteral("authKey"), QString::fromUtf8(authKey)},
        {QStringLiteral("collection"), QStringLiteral("libraryItem")}};
}

bool validAddonDocument(const QJsonObject &addon) {
    const QJsonValue transportValue = addon.value(QStringLiteral("transportUrl"));
    if (!transportValue.isString()
        || StremioCodec::normalizedAddonTransportUrl(transportValue.toString()).isEmpty()) {
        return false;
    }
    const QJsonValue name = addon.value(QStringLiteral("transportName"));
    if (!name.isUndefined()
        && (!name.isString() || name.toString().size() > kMaximumAddonNameText)) {
        return false;
    }
    const QJsonValue manifest = addon.value(QStringLiteral("manifest"));
    if (!manifest.isUndefined() && !manifest.isObject())
        return false;
    const QJsonValue flags = addon.value(QStringLiteral("flags"));
    if (!flags.isUndefined() && !flags.isObject())
        return false;
    return QJsonDocument(addon).toJson(QJsonDocument::Compact).size()
        <= kMaximumAddonDocumentBytes;
}

bool watchedFailure(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}

bool validWatchedVideos(const QList<StremioEpisodeIdentity> &videos, QString *error) {
    if (videos.isEmpty() || videos.size() > kMaximumWatchedVideos)
        return watchedFailure(error, QStringLiteral("The Stremio episode list is outside the supported bound."));
    QSet<QString> seen;
    for (const StremioEpisodeIdentity &video : videos) {
        if (!safeLibraryId(video.videoId) || video.season < 0 || video.episode < 0
            || seen.contains(video.videoId)) {
            return watchedFailure(error, QStringLiteral("The Stremio episode list is ambiguous."));
        }
        seen.insert(video.videoId);
    }
    return true;
}

bool strictPositiveDecimal(const QString &value, int *result) {
    if (value.isEmpty() || (value.size() > 1 && value.startsWith(QLatin1Char('0'))))
        return false;
    for (const QChar character : value) {
        if (!character.isDigit())
            return false;
    }
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || parsed <= 0 || parsed > kMaximumWatchedVideos)
        return false;
    if (result)
        *result = parsed;
    return true;
}

bool inflateWatched(const QByteArray &compressed, QByteArray *raw) {
    if (!raw || compressed.isEmpty() || compressed.size() > kMaximumWatchedCompressedBytes)
        return false;
    for (qsizetype capacity = 32; capacity <= kMaximumWatchedRawBytes; capacity *= 2) {
        QByteArray output(capacity, '\0');
        mz_ulong outputLength = static_cast<mz_ulong>(output.size());
        const int status = mz_uncompress(
            reinterpret_cast<unsigned char *>(output.data()),
            &outputLength,
            reinterpret_cast<const unsigned char *>(compressed.constData()),
            static_cast<mz_ulong>(compressed.size()));
        if (status == MZ_OK) {
            output.truncate(static_cast<qsizetype>(outputLength));
            *raw = output;
            return true;
        }
        if (status != MZ_BUF_ERROR)
            return false;
    }
    return false;
}

bool finiteMilliseconds(const QJsonValue &value, qint64 *milliseconds,
                        bool allowZero = false) {
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || (allowZero ? number < 0 : number <= 0)
        || std::floor(number) != number
        || number > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    if (milliseconds)
        *milliseconds = static_cast<qint64>(number);
    return true;
}

bool stremioActivityTime(const QJsonObject &state, qint64 *milliseconds) {
    const QJsonValue raw = state.value(QStringLiteral("lastWatched"));
    if (raw.isUndefined() || raw.isNull())
        return true;
    if (!raw.isString())
        return false;
    const QString text = raw.toString();
    if (text.isEmpty() || text.trimmed() != text || text.size() > 128)
        return false;
    const QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid() || parsed.timeSpec() == Qt::LocalTime)
        return false;
    const qint64 value = parsed.toMSecsSinceEpoch();
    if (value <= 0)
        return false;
    if (milliseconds)
        *milliseconds = value;
    return true;
}

QString seriesRootForStremioVideo(const QString &videoId) {
    const QStringList parts = videoId.split(QLatin1Char(':'));
    if (parts.size() < 2)
        return videoId;
    if (videoId.startsWith(QStringLiteral("tt")))
        return parts.constFirst();
    return parts.constFirst() + QLatin1Char(':') + parts.at(1);
}

bool coherentStremioVideoId(const StremioLibraryItem &item, const QString &videoId) {
    if (!safeLibraryId(videoId))
        return false;
    if (item.type == QLatin1String("movie"))
        return videoId == item.id;
    return videoId != item.id && seriesRootForStremioVideo(videoId) == item.id;
}

QString displayTitleForStremioItem(const StremioLibraryItem &item) {
    const QString title = item.raw.value(QStringLiteral("name")).toString().trimmed();
    if (safeIdentityText(title))
        return title;
    return item.id;
}
}

StremioLoopbackCallback StremioCodec::decodeLoopbackCallback(
    const QByteArray &request,
    const QString &expectedPath) {
    StremioLoopbackCallback result;
    if (request.isEmpty() || request.size() > kMaximumCallbackBytes
        || expectedPath.isEmpty() || !expectedPath.startsWith(QLatin1Char('/'))) {
        result.error = QStringLiteral("The callback request is invalid.");
        return result;
    }

    const qsizetype lineEnd = request.indexOf("\r\n");
    if (lineEnd <= 0) {
        result.error = QStringLiteral("The callback request line is invalid.");
        return result;
    }
    const QList<QByteArray> parts = request.left(lineEnd).split(' ');
    if (parts.size() != 3 || parts.at(0) != "GET" || !parts.at(2).startsWith("HTTP/")) {
        result.error = QStringLiteral("The callback method is invalid.");
        return result;
    }

    const QUrl callback = QUrl::fromEncoded(
        QByteArrayLiteral("http://127.0.0.1") + parts.at(1));
    if (!callback.isValid() || callback.path(QUrl::FullyEncoded) != expectedPath) {
        result.error = QStringLiteral("The callback correlation path does not match.");
        return result;
    }

    const QUrlQuery query(callback);
    const QList<QString> keys = query.allQueryItemValues(QStringLiteral("key"));
    const QList<QString> authKeys = query.allQueryItemValues(QStringLiteral("authKey"));
    if (keys.size() + authKeys.size() != 1) {
        result.error = QStringLiteral("The callback credential is ambiguous.");
        return result;
    }

    const QString credential = keys.isEmpty() ? authKeys.first() : keys.first();
    const QByteArray bytes = credential.toUtf8();
    if (!safeCredential(bytes)) {
        result.error = QStringLiteral("The callback credential is invalid.");
        return result;
    }

    result.accepted = true;
    result.authKey = bytes;
    return result;
}

bool StremioCodec::isProductionEndpoint(const QUrl &endpoint) {
    if (!endpoint.isValid() || endpoint.scheme() != QStringLiteral("https")
        || endpoint.host().toLower() != QStringLiteral("api.strem.io")
        || endpoint.path(QUrl::FullyEncoded) != QStringLiteral("/api")
        || !endpoint.userInfo().isEmpty() || endpoint.hasQuery() || endpoint.hasFragment()
        || (endpoint.port() != -1 && endpoint.port() != 443)) {
        return false;
    }
    return true;
}

bool StremioCodec::isTaggedLoopbackEndpoint(const QUrl &endpoint) {
    if (qEnvironmentVariableIsEmpty("COLOSSEUM_APPDATA_TAG")
        || !endpoint.isValid() || endpoint.scheme() != QStringLiteral("http")) {
        return false;
    }
    return endpoint.host() == QStringLiteral("127.0.0.1")
        && endpoint.port() > 0;
}

QUrl StremioCodec::browserLoginUrl(const QUrl &callback) {
    QUrl result(QStringLiteral("https://www.stremio.com/login"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("appName"), QStringLiteral("Colosseum"));
    query.addQueryItem(QStringLiteral("appCallback"), callback.toString(QUrl::FullyEncoded));
    result.setQuery(query);
    return result;
}

bool StremioCodec::decodeGetUserResult(
    const QJsonObject &response,
    StremioAccountIdentity *identity,
    QString *error) {
    if (!identity) {
        if (error)
            *error = QStringLiteral("A Stremio identity output is required.");
        return false;
    }
    const QJsonValue result = response.value(QStringLiteral("result"));
    if (!result.isObject()) {
        if (error)
            *error = QStringLiteral("The Stremio identity response is malformed.");
        return false;
    }
    const QJsonObject object = result.toObject();
    const QString accountId = object.value(QStringLiteral("_id")).toString().trimmed();
    QString displayName = object.value(QStringLiteral("fullname")).toString().trimmed();
    if (displayName.isEmpty())
        displayName = object.value(QStringLiteral("email")).toString().trimmed();
    if (!safeIdentityText(accountId) || !safeIdentityText(displayName)) {
        if (error)
            *error = QStringLiteral("The Stremio identity response is invalid.");
        return false;
    }
    identity->accountId = accountId;
    identity->displayName = displayName;
    return true;
}

QStringList StremioCodec::decodeLibraryItemMeta(
    const QJsonValue &result,
    int *malformedRows,
    int maximumRows) {
    if (malformedRows)
        *malformedRows = 0;
    if (!result.isArray())
        return {};

    QStringList ids;
    QSet<QString> seen;
    const QJsonArray rows = result.toArray();
    for (const QJsonValue &row : rows) {
        if (ids.size() >= qBound(0, maximumRows, kMaximumLibraryRows))
            break;
        if (!row.isArray()) {
            if (malformedRows)
                ++*malformedRows;
            continue;
        }
        const QJsonArray values = row.toArray();
        if (values.size() < 2 || !values.at(0).isString()
            || (!values.at(1).isString() && !values.at(1).isDouble())) {
            if (malformedRows)
                ++*malformedRows;
            continue;
        }
        const QString id = values.at(0).toString();
        if (!safeLibraryId(id)) {
            if (malformedRows)
                ++*malformedRows;
            continue;
        }
        if (!seen.contains(id)) {
            seen.insert(id);
            ids.append(id);
        }
    }
    return ids;
}

QList<QStringList> StremioCodec::boundedLibraryItemBatches(
    const QStringList &ids,
    int maximumBatchSize) {
    const int size = qBound(1, maximumBatchSize, kMaximumLibraryBatchSize);
    QList<QStringList> batches;
    QStringList current;
    QSet<QString> seen;
    for (const QString &id : ids) {
        if (!safeLibraryId(id) || seen.contains(id))
            continue;
        seen.insert(id);
        current.append(id);
        if (current.size() == size) {
            batches.append(current);
            current.clear();
        }
    }
    if (!current.isEmpty())
        batches.append(current);
    return batches;
}

StremioLibraryItemDecode StremioCodec::decodeLibraryItems(
    const QJsonValue &result,
    int maximumRows) {
    StremioLibraryItemDecode decoded;
    if (!result.isArray()) {
        decoded.malformedRows = 1;
        return decoded;
    }
    const QJsonArray rows = result.toArray();
    const int limit = qBound(1, maximumRows, kMaximumLibraryRows);
    const int count = qMin(rows.size(), limit);
    decoded.malformedRows = rows.size() - count;
    QSet<QString> seen;
    for (int index = 0; index < count; ++index) {
        if (!rows.at(index).isObject()) {
            ++decoded.malformedRows;
            continue;
        }
        const QJsonObject raw = rows.at(index).toObject();
        const QString id = raw.value(QStringLiteral("_id")).toString();
        const QString type = raw.value(QStringLiteral("type")).toString();
        bool removed = false;
        bool temporary = false;
        const QJsonValue state = raw.value(QStringLiteral("state"));
        if (!safeLibraryId(id) || !supportedLibraryType(type)
            || !optionalBoolean(raw, QStringLiteral("removed"), &removed)
            || !optionalBoolean(raw, QStringLiteral("temp"), &temporary)
            || (!state.isUndefined() && !state.isObject())
            || seen.contains(id)) {
            ++decoded.malformedRows;
            continue;
        }
        seen.insert(id);
        decoded.items.append(StremioLibraryItem{
            id,
            type,
            removed,
            temporary,
            !removed && !temporary,
            raw});
    }
    return decoded;
}

bool StremioCodec::mergeLibraryItemPatch(
    const QJsonObject &existing,
    const QJsonObject &patch,
    QJsonObject *merged,
    QString *error) {
    if (!merged) {
        if (error)
            *error = QStringLiteral("A Stremio library patch output is required.");
        return false;
    }
    const QString id = existing.value(QStringLiteral("_id")).toString();
    const QString type = existing.value(QStringLiteral("type")).toString();
    if (!safeLibraryId(id) || !supportedLibraryType(type)) {
        if (error)
            *error = QStringLiteral("The existing Stremio library item is invalid.");
        return false;
    }
    if (patch.contains(QStringLiteral("_id"))
        && patch.value(QStringLiteral("_id")).toString() != id) {
        if (error)
            *error = QStringLiteral("A Stremio library patch cannot change its identity.");
        return false;
    }
    QJsonObject result = existing;
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
        if (it.key() == QStringLiteral("_id"))
            continue;
        if (it.key() == QStringLiteral("state") && it->isObject()) {
            QJsonObject state = result.value(QStringLiteral("state")).toObject();
            const QJsonObject statePatch = it->toObject();
            for (auto stateIt = statePatch.constBegin(); stateIt != statePatch.constEnd(); ++stateIt)
                state.insert(stateIt.key(), stateIt.value());
            result.insert(QStringLiteral("state"), state);
            continue;
        }
        result.insert(it.key(), it.value());
    }
    result.insert(QStringLiteral("_id"), id);
    *merged = result;
    return true;
}

StremioDatastoreRequest StremioCodec::datastoreMetaRequest(const QByteArray &authKey) {
    const QJsonObject payload = datastoreBase(authKey);
    return StremioDatastoreRequest{
        payload.isEmpty() ? QString() : QStringLiteral("datastoreMeta"),
        payload};
}

QList<StremioDatastoreRequest> StremioCodec::datastoreGetRequests(
    const QByteArray &authKey,
    const QStringList &ids,
    int maximumBatchSize) {
    const QJsonObject base = datastoreBase(authKey);
    if (base.isEmpty())
        return {};
    QList<StremioDatastoreRequest> requests;
    for (const QStringList &batch : boundedLibraryItemBatches(ids, maximumBatchSize)) {
        QJsonObject payload = base;
        QJsonArray values;
        for (const QString &id : batch)
            values.append(id);
        payload.insert(QStringLiteral("ids"), values);
        payload.insert(QStringLiteral("all"), false);
        requests.append(StremioDatastoreRequest{QStringLiteral("datastoreGet"), payload});
    }
    return requests;
}

StremioDatastoreRequest StremioCodec::datastorePutRequest(
    const QByteArray &authKey,
    const QJsonObject &change) {
    const QJsonObject base = datastoreBase(authKey);
    const QString id = change.value(QStringLiteral("_id")).toString();
    const QString type = change.value(QStringLiteral("type")).toString();
    if (base.isEmpty() || !safeLibraryId(id) || !supportedLibraryType(type))
        return {};
    QJsonObject payload = base;
    payload.insert(QStringLiteral("changes"), QJsonArray{change});
    return StremioDatastoreRequest{QStringLiteral("datastorePut"), payload};
}

QString StremioCodec::normalizedAddonTransportUrl(const QString &transportUrl) {
    if (transportUrl.isEmpty() || transportUrl.size() > kMaximumAddonUrlText
        || transportUrl.trimmed() != transportUrl) {
        return {};
    }
    const QUrl parsed(transportUrl, QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        || parsed.host().isEmpty() || !parsed.userInfo().isEmpty()
        || parsed.hasFragment()) {
        return {};
    }
    QString normalized = scheme + QStringLiteral("://") + parsed.host().toLower();
    if (parsed.port() >= 0)
        normalized += QStringLiteral(":%1").arg(parsed.port());
    const QString path = parsed.path(QUrl::FullyEncoded);
    normalized += path.isEmpty() ? QStringLiteral("/") : path;
    if (parsed.hasQuery())
        normalized += QLatin1Char('?') + parsed.query(QUrl::FullyEncoded);
    return normalized;
}

StremioDatastoreRequest StremioCodec::addonCollectionGetRequest(const QByteArray &authKey) {
    if (!safeCredential(authKey))
        return {};
    return StremioDatastoreRequest{
        QStringLiteral("addonCollectionGet"),
        QJsonObject{{QStringLiteral("authKey"), QString::fromUtf8(authKey)},
                    {QStringLiteral("type"), QStringLiteral("user")},
                    {QStringLiteral("update"), false}}};
}

StremioDatastoreRequest StremioCodec::addonCollectionSetRequest(
    const QByteArray &authKey,
    const QJsonArray &addons) {
    if (!safeCredential(authKey) || addons.size() > kMaximumAddonRows)
        return {};
    for (const QJsonValue &value : addons) {
        if (!value.isObject() || !validAddonDocument(value.toObject()))
            return {};
    }
    return StremioDatastoreRequest{
        QStringLiteral("addonCollectionSet"),
        QJsonObject{{QStringLiteral("authKey"), QString::fromUtf8(authKey)},
                    {QStringLiteral("type"), QStringLiteral("user")},
                    {QStringLiteral("addons"), addons}}};
}

StremioAddonCollectionDecode StremioCodec::decodeAddonCollection(
    const QJsonValue &result,
    int maximumRows) {
    StremioAddonCollectionDecode decoded;
    const int limit = qBound(1, maximumRows, kMaximumAddonRows);
    QJsonArray rows;
    if (result.isArray()) {
        rows = result.toArray();
    } else if (result.isObject()) {
        const QJsonValue addons = result.toObject().value(QStringLiteral("addons"));
        if (!addons.isArray()) {
            decoded.malformedRows = 1;
            return decoded;
        }
        rows = addons.toArray();
    } else {
        decoded.malformedRows = 1;
        return decoded;
    }
    decoded.containerValid = true;
    for (const QJsonValue &value : rows) {
        if (decoded.addons.size() >= limit)
            break;
        if (!value.isObject() || !validAddonDocument(value.toObject())) {
            ++decoded.malformedRows;
            continue;
        }
        decoded.addons.append(value);
    }
    return decoded;
}

bool StremioCodec::decodeWatchedEpisodes(
    const QString &field,
    const QList<StremioEpisodeIdentity> &videos,
    QSet<QString> *watchedVideoIds,
    QString *error) {
    if (!watchedVideoIds)
        return watchedFailure(error, QStringLiteral("A Stremio watched output is required."));
    if (!validWatchedVideos(videos, error))
        return false;
    const int payloadSeparator = field.lastIndexOf(QLatin1Char(':'));
    if (payloadSeparator <= 0)
        return watchedFailure(error, QStringLiteral("The Stremio watched field is malformed."));
    const int lengthSeparator = field.lastIndexOf(QLatin1Char(':'), payloadSeparator - 1);
    if (lengthSeparator <= 0)
        return watchedFailure(error, QStringLiteral("The Stremio watched field is malformed."));

    const QString anchorVideoId = field.left(lengthSeparator);
    int anchorLength = 0;
    if (!safeLibraryId(anchorVideoId)
        || !strictPositiveDecimal(field.mid(lengthSeparator + 1, payloadSeparator - lengthSeparator - 1),
                                  &anchorLength)) {
        return watchedFailure(error, QStringLiteral("The Stremio watched anchor is malformed."));
    }
    const QByteArray compressed = QByteArray::fromBase64(
        field.mid(payloadSeparator + 1).toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    if (compressed.isEmpty())
        return watchedFailure(error, QStringLiteral("The Stremio watched payload is malformed."));

    int anchorIndex = -1;
    for (int index = 0; index < videos.size(); ++index) {
        if (videos.at(index).videoId == anchorVideoId) {
            anchorIndex = index;
            break;
        }
    }
    if (anchorIndex < 0)
        return watchedFailure(error, QStringLiteral("The Stremio watched anchor is not in the episode map."));

    QByteArray raw;
    if (!inflateWatched(compressed, &raw))
        return watchedFailure(error, QStringLiteral("The Stremio watched payload could not be decompressed."));
    const qint64 offset = static_cast<qint64>(anchorLength) - anchorIndex - 1;
    QSet<QString> decoded;
    for (int index = 0; index < videos.size(); ++index) {
        const qint64 bitIndex = index + offset;
        if (bitIndex < 0 || bitIndex >= raw.size() * 8)
            continue;
        const unsigned char byte = static_cast<unsigned char>(raw.at(bitIndex / 8));
        if ((byte & (1u << (bitIndex % 8))) != 0)
            decoded.insert(videos.at(index).videoId);
    }
    *watchedVideoIds = decoded;
    return true;
}

bool StremioCodec::encodeWatchedEpisodes(
    const QSet<QString> &watchedVideoIds,
    const QList<StremioEpisodeIdentity> &videos,
    QString *field,
    QString *error) {
    if (!field)
        return watchedFailure(error, QStringLiteral("A Stremio watched field output is required."));
    if (!validWatchedVideos(videos, error))
        return false;
    QByteArray raw((videos.size() + 7) / 8, '\0');
    for (int index = 0; index < videos.size(); ++index) {
        if (!watchedVideoIds.contains(videos.at(index).videoId))
            continue;
        raw[index / 8] = static_cast<char>(
            static_cast<unsigned char>(raw.at(index / 8)) | (1u << (index % 8)));
    }
    QByteArray compressed(static_cast<qsizetype>(mz_compressBound(static_cast<mz_ulong>(raw.size()))), '\0');
    mz_ulong compressedLength = static_cast<mz_ulong>(compressed.size());
    if (mz_compress2(
            reinterpret_cast<unsigned char *>(compressed.data()),
            &compressedLength,
            reinterpret_cast<const unsigned char *>(raw.constData()),
            static_cast<mz_ulong>(raw.size()),
            MZ_DEFAULT_COMPRESSION) != MZ_OK) {
        return watchedFailure(error, QStringLiteral("The Stremio watched payload could not be compressed."));
    }
    compressed.truncate(static_cast<qsizetype>(compressedLength));
    *field = videos.last().videoId
        + QLatin1Char(':')
        + QString::number(videos.size())
        + QLatin1Char(':')
        + QString::fromLatin1(compressed.toBase64());
    return true;
}

bool StremioCodec::episodeBelongsToSeries(
    const QString &seriesId,
    const StremioEpisodeIdentity &episode) {
    return safeLibraryId(seriesId)
        && safeLibraryId(episode.videoId)
        && episode.season >= 0
        && episode.episode >= 0
        && episode.videoId != seriesId
        && seriesRootForStremioVideo(episode.videoId) == seriesId;
}

bool StremioCodec::movieFlaggedWatched(
    const StremioLibraryItem &item,
    bool *watched) {
    if (!watched || item.type != QLatin1String("movie"))
        return false;
    const QJsonValue stateValue = item.raw.value(QStringLiteral("state"));
    if (stateValue.isUndefined()) {
        *watched = false;
        return true;
    }
    if (!stateValue.isObject())
        return false;
    const QJsonValue flagged = stateValue.toObject().value(QStringLiteral("flaggedWatched"));
    if (flagged.isUndefined() || flagged.isNull()) {
        *watched = false;
        return true;
    }
    if (!flagged.isDouble() || !std::isfinite(flagged.toDouble())
        || std::floor(flagged.toDouble()) != flagged.toDouble()) {
        return false;
    }
    *watched = flagged.toDouble() > 0;
    return true;
}

StremioTheatreItemProjection StremioCodec::projectTheatreItem(
    const StremioLibraryItem &item) {
    StremioTheatreItemProjection projected;
    if (!safeLibraryId(item.id) || !supportedLibraryType(item.type)
        || !item.raw.value(QStringLiteral("state")).isUndefined()
            && !item.raw.value(QStringLiteral("state")).isObject()) {
        projected.error = QStringLiteral("The Stremio library item is malformed.");
        return projected;
    }

    if (item.libraryMember) {
        projected.collection = {
            {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("id"), item.id},
            {QStringLiteral("type"), item.type},
            {QStringLiteral("title"), displayTitleForStremioItem(item)}};
        projected.hasCollection = true;
    }

    const QJsonValue stateValue = item.raw.value(QStringLiteral("state"));
    if (stateValue.isObject()) {
        const QJsonObject state = stateValue.toObject();
        const QJsonValue videoValue = state.value(QStringLiteral("video_id"));
        const bool hasPlaybackState = !videoValue.isUndefined()
            || !state.value(QStringLiteral("timeOffset")).isUndefined()
            || !state.value(QStringLiteral("duration")).isUndefined();
        if (hasPlaybackState) {
            const QString videoId = videoValue.toString();
            qint64 offsetMs = 0;
            qint64 durationMs = 0;
            if (!videoValue.isString()
                || !coherentStremioVideoId(item, videoId)
                || !finiteMilliseconds(state.value(QStringLiteral("timeOffset")), &offsetMs, true)
                || !finiteMilliseconds(state.value(QStringLiteral("duration")), &durationMs)
                || offsetMs > durationMs) {
                projected.error = QStringLiteral("The Stremio playback state is malformed.");
                projected.collection.clear();
                projected.hasCollection = false;
                return projected;
            }
            if (offsetMs > 0) {
                projected.progress = {
                    {QStringLiteral("kind"), QStringLiteral("video")},
                    {QStringLiteral("id"), videoId},
                    {QStringLiteral("libraryId"), item.id},
                    {QStringLiteral("duration"), static_cast<double>(durationMs) / 1000.0},
                    {QStringLiteral("progress"), static_cast<double>(offsetMs) / durationMs},
                    {QStringLiteral("resume"), QVariantMap{
                        {QStringLiteral("position"), static_cast<double>(offsetMs) / 1000.0}}}};
                qint64 activityMs = 0;
                if (!stremioActivityTime(state, &activityMs)) {
                    projected.error = QStringLiteral("The Stremio playback time is malformed.");
                    projected.collection.clear();
                    projected.progress.clear();
                    projected.hasCollection = false;
                    return projected;
                }
                if (activityMs > 0)
                    projected.progress.insert(QStringLiteral("updatedAt"), activityMs);
                projected.hasProgress = true;
            }
        }

        // `flaggedWatched` owns current movie watched/unwatched state. It is
        // distinct from the cumulative History record below: an explicit
        // zero can clear the current state, while an undated watched flag is
        // still valid but must never invent a History timestamp.
        const QJsonValue flaggedWatched = state.value(QStringLiteral("flaggedWatched"));
        if (item.type == QLatin1String("movie")
            && !flaggedWatched.isUndefined() && !flaggedWatched.isNull()) {
            bool movieWatched = false;
            qint64 activityMs = 0;
            if (!movieFlaggedWatched(item, &movieWatched)
                || !stremioActivityTime(state, &activityMs)) {
                projected.error = QStringLiteral("The Stremio watched time is missing or malformed.");
                projected.collection.clear();
                projected.progress.clear();
                projected.hasCollection = false;
                projected.hasProgress = false;
                return projected;
            }
            projected.hasWatchState = true;
            projected.watched = movieWatched;
            projected.watchActionAtMs = activityMs;
            if (movieWatched && activityMs > 0) {
                projected.history = {
                    {QStringLiteral("kind"), QStringLiteral("movie")},
                    {QStringLiteral("id"), item.id},
                    {QStringLiteral("firstActivityAt"), activityMs},
                    {QStringLiteral("lastActivityAt"), activityMs},
                    {QStringLiteral("completedAt"), activityMs},
                    {QStringLiteral("source"), QStringLiteral("stremio")},
                    {QStringLiteral("displayId"), item.id},
                    {QStringLiteral("displayTitle"), displayTitleForStremioItem(item)},
                    {QStringLiteral("latestKnownAt"), activityMs}};
                projected.hasHistory = true;
            }
        }
    }

    projected.valid = true;
    return projected;
}
