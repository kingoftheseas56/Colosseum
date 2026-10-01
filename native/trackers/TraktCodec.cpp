#include "TraktCodec.h"
#include "TraktAuth.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>

#include <cmath>
#include <limits>

namespace {

QString digest(const QByteArray &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

QString idString(const QJsonObject &ids, const QString &key)
{
    const QJsonValue value = ids.value(key);
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number <= 0 || number > 9007199254740991.0 || std::floor(number) != number)
            return {};
        return QString::number(static_cast<qint64>(number));
    }
    return value.toString().trimmed();
}

bool positiveIntegerString(const QString &value)
{
    if (value.isEmpty() || value != value.trimmed() || value.size() > 32)
        return false;
    bool ok = false;
    const qulonglong parsed = value.toULongLong(&ok);
    return ok && parsed > 0 && parsed <= 9007199254740991ULL && QString::number(parsed) == value;
}

bool externalIdValid(const QString &key, const QString &id)
{
    if (key != QLatin1String("imdb")) return positiveIntegerString(id);
    if (!id.startsWith(QStringLiteral("tt")) || id.size() <= 2 || id.size() > 32) return false;
    for (const QChar character : id.mid(2))
        if (character < QLatin1Char('0') || character > QLatin1Char('9')) return false;
    return true;
}

QString safeTitle(const QString &value)
{
    return value.simplified().left(500);
}

qint64 isoMs(const QString &value)
{
    return QDateTime::fromString(value, Qt::ISODateWithMs).toMSecsSinceEpoch();
}

QStringList exactIds(const QJsonObject &item, const QJsonObject &show, int season, int episode)
{
    QStringList ids;
    const QJsonObject mediaIds = item.value(QStringLiteral("ids")).toObject();
    for (const QString &key : {QStringLiteral("imdb"), QStringLiteral("tmdb")}) {
        const QString id = idString(mediaIds, key);
        if (externalIdValid(key, id)) ids.append(key == QLatin1String("imdb") ? id : key + QLatin1Char(':') + id);
    }
    if (episode > 0) {
        const QJsonObject showIds = show.value(QStringLiteral("ids")).toObject();
        for (const QString &key : {QStringLiteral("imdb"), QStringLiteral("tmdb"), QStringLiteral("tvdb")}) {
            const QString id = idString(showIds, key);
            if (!externalIdValid(key, id)) continue;
            const QString qualified = key == QLatin1String("imdb") ? id : key + QLatin1Char(':') + id;
            ids.append(qualified + QLatin1Char(':') + QString::number(season) + QLatin1Char(':') + QString::number(episode));
            ids.append(qualified + QStringLiteral(":S%1E%2").arg(season, 2, 10, QLatin1Char('0')).arg(episode, 2, 10, QLatin1Char('0')));
        }
    }
    ids.removeDuplicates();
    return ids;
}

QJsonObject mediaObject(const TraktMediaIdentity &identity)
{
    return {{QStringLiteral("ids"), QJsonObject{{QStringLiteral("trakt"), identity.traktId.toLongLong()}}}};
}

} // namespace

std::optional<TraktMediaIdentity> traktParseRemoteMediaId(const QString &value)
{
    const QStringList parts = value.split(QLatin1Char(':'));
    if (parts.size() != 2 || !positiveIntegerString(parts.at(1))) return std::nullopt;
    if (parts.first() == QLatin1String("movie")) return TraktMediaIdentity{TraktMediaKind::Movie, parts.at(1)};
    if (parts.first() == QLatin1String("episode")) return TraktMediaIdentity{TraktMediaKind::Episode, parts.at(1)};
    return std::nullopt;
}

QString traktRemoteMediaId(TraktMediaKind kind, const QString &traktId)
{
    if (!positiveIntegerString(traktId)) return {};
    return (kind == TraktMediaKind::Movie ? QStringLiteral("movie:") : QStringLiteral("episode:")) + traktId;
}

QJsonObject traktScrobbleBody(const QString &remoteMediaId, int progressHundredths)
{
    const auto identity = traktParseRemoteMediaId(remoteMediaId);
    if (!identity || progressHundredths < 0 || progressHundredths > 10000) return {};
    const double progress = static_cast<double>(progressHundredths) / 100.0;
    QJsonObject body{{QStringLiteral("progress"), progress}};
    body.insert(identity->kind == TraktMediaKind::Movie ? QStringLiteral("movie") : QStringLiteral("episode"), mediaObject(*identity));
    return body;
}

QJsonObject traktHistoryBody(const QString &remoteMediaId, qint64 watchedAtMs)
{
    const auto identity = traktParseRemoteMediaId(remoteMediaId);
    if (!identity || watchedAtMs <= 0) return {};
    QJsonObject item = mediaObject(*identity);
    item.insert(QStringLiteral("watched_at"), QDateTime::fromMSecsSinceEpoch(watchedAtMs, Qt::UTC).toString(Qt::ISODateWithMs));
    return identity->kind == TraktMediaKind::Movie
        ? QJsonObject{{QStringLiteral("movies"), QJsonArray{item}}}
        : QJsonObject{{QStringLiteral("episodes"), QJsonArray{item}}};
}

QString traktSettingsUuid(const QJsonDocument &document)
{
    if (!document.isObject()) return {};
    const QJsonObject root = document.object();
    QString uuid = root.value(QStringLiteral("user")).toObject().value(QStringLiteral("ids")).toObject().value(QStringLiteral("uuid")).toString().trimmed();
    const QUuid parsed(uuid);
    return parsed.isNull() ? QString() : parsed.toString(QUuid::WithoutBraces).toLower();
}

QList<TraktRemoteFact> traktParseHistory(const QJsonDocument &document)
{
    QList<TraktRemoteFact> facts;
    if (!document.isArray()) return facts;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject row = value.toObject();
        const QString type = row.value(QStringLiteral("type")).toString();
        const qint64 historyId = row.value(QStringLiteral("id")).toInteger();
        const qint64 watchedAt = isoMs(row.value(QStringLiteral("watched_at")).toString());
        if (historyId <= 0 || watchedAt <= 0) continue;
        QJsonObject media;
        QJsonObject show;
        TraktMediaKind kind;
        int season = 0;
        int episode = 0;
        if (type == QLatin1String("movie")) {
            media = row.value(QStringLiteral("movie")).toObject();
            kind = TraktMediaKind::Movie;
        } else if (type == QLatin1String("episode")) {
            media = row.value(QStringLiteral("episode")).toObject();
            show = row.value(QStringLiteral("show")).toObject();
            kind = TraktMediaKind::Episode;
            season = media.value(QStringLiteral("season")).toInt();
            episode = media.value(QStringLiteral("number")).toInt();
        } else {
            continue;
        }
        const QString traktId = idString(media.value(QStringLiteral("ids")).toObject(), QStringLiteral("trakt"));
        const QString remote = traktRemoteMediaId(kind, traktId);
        if (remote.isEmpty()) continue;
        QString title = kind == TraktMediaKind::Movie ? media.value(QStringLiteral("title")).toString() : show.value(QStringLiteral("title")).toString();
        if (kind == TraktMediaKind::Episode)
            title += QStringLiteral(" · S%1E%2").arg(season, 2, 10, QLatin1Char('0')).arg(episode, 2, 10, QLatin1Char('0'));
        const QByteArray material = QJsonDocument(row).toJson(QJsonDocument::Compact);
        facts.append({remote, QStringLiteral("history:") + QString::number(historyId), safeTitle(title),
                      exactIds(media, show, season, episode), 100, true, watchedAt, digest(material), true, 100.0});
    }
    return facts;
}

QList<TraktRemoteFact> traktParsePlayback(const QJsonDocument &document)
{
    QList<TraktRemoteFact> facts;
    if (!document.isArray()) return facts;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject row = value.toObject();
        const QString type = row.value(QStringLiteral("type")).toString();
        const qint64 playbackId = row.value(QStringLiteral("id")).toInteger();
        const qint64 pausedAt = isoMs(row.value(QStringLiteral("paused_at")).toString());
        if (playbackId <= 0 || pausedAt <= 0) continue;
        const double rawProgress = row.value(QStringLiteral("progress")).toDouble(-1.0);
        if (!std::isfinite(rawProgress) || rawProgress < 0.0 || rawProgress > 100.0) continue;
        QJsonObject media;
        QJsonObject show;
        TraktMediaKind kind;
        int season = 0;
        int episode = 0;
        if (type == QLatin1String("movie")) {
            media = row.value(QStringLiteral("movie")).toObject();
            kind = TraktMediaKind::Movie;
        } else if (type == QLatin1String("episode")) {
            media = row.value(QStringLiteral("episode")).toObject();
            show = row.value(QStringLiteral("show")).toObject();
            kind = TraktMediaKind::Episode;
            season = media.value(QStringLiteral("season")).toInt();
            episode = media.value(QStringLiteral("number")).toInt();
        } else {
            continue;
        }
        const QString traktId = idString(media.value(QStringLiteral("ids")).toObject(), QStringLiteral("trakt"));
        const QString remote = traktRemoteMediaId(kind, traktId);
        if (remote.isEmpty()) continue;
        QString title = kind == TraktMediaKind::Movie ? media.value(QStringLiteral("title")).toString() : show.value(QStringLiteral("title")).toString();
        if (kind == TraktMediaKind::Episode)
            title += QStringLiteral(" · S%1E%2").arg(season, 2, 10, QLatin1Char('0')).arg(episode, 2, 10, QLatin1Char('0'));
        const QByteArray material = QJsonDocument(row).toJson(QJsonDocument::Compact);
        facts.append({remote, QStringLiteral("playback:") + QString::number(playbackId), safeTitle(title),
                      exactIds(media, show, season, episode), qBound(0, qRound(rawProgress), 100), false,
                      pausedAt, digest(material), false, rawProgress});
    }
    return facts;
}

bool traktShouldSuppressLegacyRelay(bool directConnected, bool scrobbleCapable, bool liveEnabled)
{
    return directConnected && scrobbleCapable && liveEnabled;
}

std::optional<TrackerCredential> traktRotatedCredential(const TrackerCredential &previous,
                                                        const QJsonDocument &document,
                                                        qint64 nowMs)
{
    if (!document.isObject() || nowMs < 0 || previous.slot.providerId != TrackerProviderId::Trakt)
        return std::nullopt;
    const QJsonObject object = document.object();
    const QByteArray access = object.value(QStringLiteral("access_token")).toString().toUtf8();
    const QByteArray refresh = object.value(QStringLiteral("refresh_token")).toString().toUtf8();
    const qint64 expiresSeconds = object.value(QStringLiteral("expires_in")).toInteger();
    const qint64 createdSeconds = object.value(QStringLiteral("created_at")).toInteger();
    if (expiresSeconds <= 0 || expiresSeconds > std::numeric_limits<qint64>::max() / 1000
        || createdSeconds < 0 || createdSeconds > std::numeric_limits<qint64>::max() / 1000
        || !traktAccountUuidIsCanonical(previous.slot.remoteAccountId) || previous.slot.profileId.isEmpty())
        return std::nullopt;
    const qint64 expiresMs = expiresSeconds * 1000;
    qint64 createdAtMs = createdSeconds * 1000;
    if (createdAtMs <= 0) createdAtMs = nowMs;
    if (access.isEmpty() || refresh.isEmpty() || expiresMs <= 0
        || createdAtMs > std::numeric_limits<qint64>::max() - expiresMs
        || createdAtMs + expiresMs <= nowMs)
        return std::nullopt;
    TrackerCredential next = previous;
    next.accessToken = access;
    next.refreshToken = refresh;
    next.accessTokenExpiresAtMs = createdAtMs + expiresMs;
    next.refreshTokenExpiresAtMs = std::numeric_limits<qint64>::max();
    next.grantedScopes = {};
    return next;
}
