#include "NuvioTheatreData.h"

#include <QDateTime>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
using Category = TheatreAccountImport::Category;
using Disposition = TheatreAccountImport::Disposition;
using Item = TheatreAccountImport::Item;

const auto kMovie = QStringLiteral("movie");
const auto kSeries = QStringLiteral("series");
const auto kEpisode = QStringLiteral("episode");

QString uuid(const QJsonValue &value) {
    const QUuid id(value.toString());
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

QString imdbId(const QString &value) {
    static const QRegularExpression pattern(QStringLiteral("\\A(?:imdb:)?(tt[0-9]{1,12})\\z"));
    return pattern.match(value).captured(1);
}

bool isTmdbId(const QString &value) {
    static const QRegularExpression pattern(QStringLiteral("\\Atmdb:[1-9][0-9]{0,9}\\z"));
    return pattern.match(value).hasMatch();
}

QString cleanTitle(const QJsonValue &value, const QString &fallback) {
    const QString title = value.toString().simplified().left(180).trimmed();
    return title.isEmpty() || title.contains(QChar::Null) || title.startsWith(QLatin1Char('/'))
        || title.startsWith(QLatin1Char('\\')) || title.startsWith(QLatin1String("file:"))
        || (title.size() > 2 && title.at(1) == QLatin1Char(':')
            && (title.at(2) == QLatin1Char('/') || title.at(2) == QLatin1Char('\\')))
        ? fallback : title;
}

QString coverUrl(const QString &text) {
    const QUrl url(text, QUrl::StrictMode);
    return text.size() <= 2048 && url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty()
        && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
        ? text : QString();
}

QString libraryKey(const QString &type, const QString &id) {
    return type + QLatin1Char('/') + id;
}

// Existing Theatre records can already carry a confirmed TMDB alias alongside
// their IMDb identity. Ambiguous aliases stay unresolved. No title search.
class Identities {
public:
    explicit Identities(const NuvioTheatre::Local &local) {
        for (const QVariant &value : local.library) {
            const QVariantMap item = value.toMap();
            const QString type = item.value("type").toString();
            const QString id = item.value("id").toString();
            if (type != kMovie && type != kSeries)
                continue;
            m_library.insert(libraryKey(type, id), item);
            const qint64 tmdb = item.value("tmdbId").toLongLong();
            if (tmdb <= 0 || imdbId(id) != id)
                continue;
            const QString key = libraryKey(type, QStringLiteral("tmdb:%1").arg(tmdb));
            if (m_tmdb.contains(key) && m_tmdb.value(key) != id)
                m_tmdb[key].clear();
            else
                m_tmdb.insert(key, id);
        }
        for (const QVariant &value : local.progress) {
            const QVariantMap item = value.toMap();
            if (item.value("kind").toString() == QLatin1String("video"))
                m_progress.insert(item.value("id").toString(), item);
        }
    }

    QString canonical(const QString &type, const QString &remote) const {
        if (type != kMovie && type != kSeries)
            return {};
        if (const QString imdb = imdbId(remote); !imdb.isEmpty())
            return imdb;
        if (!isTmdbId(remote))
            return {};
        const QString key = libraryKey(type, remote);
        if (m_library.contains(key))
            return remote;
        // Theatre already supports provider-native IDs for addon metadata.
        // Retain an exact, typed TMDB identity when there is no known alias.
        // A conflicting alias is unresolved; it is never resolved by title.
        return m_tmdb.value(key, remote);
    }

    QVariantMap library(const QString &type, const QString &id) const {
        return m_library.value(libraryKey(type, id));
    }

    QVariantMap progress(const QString &id) const { return m_progress.value(id); }

private:
    QHash<QString, QVariantMap> m_library;
    QHash<QString, QVariantMap> m_progress;
    QHash<QString, QString> m_tmdb;
};

struct Identity {
    QString content;
    QString type;
    int season = -1;
    int episode = -1;

    bool isEpisode() const { return type == kSeries && season >= 0 && episode >= 0; }
    QString video() const {
        return isEpisode() ? QStringLiteral("%1:%2:%3").arg(content).arg(season).arg(episode) : content;
    }
    QString progressKey() const {
        return isEpisode() ? QStringLiteral("%1_s%2e%3").arg(content).arg(season).arg(episode) : content;
    }
    QJsonObject wire() const {
        QJsonObject result{{"content_id", content}, {"content_type", type}};
        if (isEpisode()) {
            result.insert("season", season);
            result.insert("episode", episode);
        }
        return result;
    }
};

Identity remoteIdentity(const QJsonObject &row) {
    return {row.value("content_id").toString(), row.value("content_type").toString(),
        row.value("season").toInt(-1), row.value("episode").toInt(-1)};
}

Identity localVideo(const QString &id) {
    const QString imdb = imdbId(id);
    if ((!imdb.isEmpty() && imdb == id) || isTmdbId(id))
        return {id, kMovie};
    static const QRegularExpression episode(
        QStringLiteral("\\A(tt[0-9]{1,12}|tmdb:[1-9][0-9]{0,9}):([0-9]{1,5}):([0-9]{1,5})\\z"));
    const auto matched = episode.match(id);
    if (!matched.hasMatch())
        return {};
    const Identity identity{matched.captured(1), kSeries,
        matched.captured(2).toInt(), matched.captured(3).toInt()};
    return identity.video() == id ? identity : Identity{};
}

Identity canonicalIdentity(const Identity &remote, const Identities &identities,
    bool episodeExpected) {
    if (episodeExpected && (!remote.isEpisode() || remote.season > 99999 || remote.episode > 99999))
        return {};
    const Identity canonical{identities.canonical(remote.type, remote.content), remote.type,
        remote.season, remote.episode};
    return canonical.content.isEmpty() ? Identity{} : canonical;
}

qint64 timestamp(const QJsonObject &row, const char *field) {
    return qMax<qint64>(0, row.value(QLatin1String(field)).toInteger());
}

Item libraryItem(const QJsonObject &row, const Identities &identities) {
    if (row.value("operation") == QLatin1String("delete"))
        return {Category::Library, {}, Disposition::Skipped};
    const Identity raw = remoteIdentity(row);
    const Identity local = canonicalIdentity(raw, identities, false);
    if (local.content.isEmpty())
        return {Category::Library, {}, Disposition::Unresolved};
    QVariantMap record{{"id", local.content}, {"type", local.type},
        {"title", cleanTitle(row.value("name"), local.content)}};
    const qint64 at = timestamp(row, "added_at");
    if (at > 0)
        record.insert("addedAt", at);
    const QString cover = coverUrl(row.value("poster").toString());
    if (!cover.isEmpty())
        record.insert("cover", cover);
    if (isTmdbId(raw.content))
        record.insert("tmdbId", raw.content.sliced(5).toLongLong());
    return {Category::Library, std::move(record)};
}

Item watchedItem(const QJsonObject &row, const Identities &identities) {
    const Identity raw = remoteIdentity(row);
    const bool episode = raw.type == kSeries;
    const auto category = episode ? Category::Episode : Category::Movie;
    if (row.value("operation") == QLatin1String("delete"))
        return {category, {}, Disposition::Skipped};
    const Identity local = canonicalIdentity(raw, identities, episode);
    if (local.content.isEmpty())
        return {category, {}, Disposition::Unresolved};
    const qint64 at = timestamp(row, "watched_at");
    if (at <= 0)
        return {category, {}, Disposition::Skipped};
    return {category, {{"kind", episode ? kEpisode : kMovie}, {"id", local.video()},
        {"firstActivityAt", at}, {"lastActivityAt", at}, {"completedAt", at},
        {"source", QStringLiteral("nuvio")}, {"displayId", raw.progressKey()},
        {"displayTitle", cleanTitle(row.value("title"), local.video())}, {"latestKnownAt", at}}};
}

Item progressItem(const QJsonObject &row, const Identities &identities) {
    if (row.value("operation") == QLatin1String("delete"))
        return {Category::Progress, {}, Disposition::Skipped};
    const Identity raw = remoteIdentity(row);
    const Identity local = canonicalIdentity(raw, identities, raw.type == kSeries);
    if (local.content.isEmpty() || row.value("video_id").toString() != raw.video()
        || row.value("progress_key").toString(raw.progressKey()) != raw.progressKey()) {
        return {Category::Progress, {}, Disposition::Unresolved};
    }
    const qint64 position = row.value("position").toInteger(-1);
    const qint64 duration = row.value("duration").toInteger();
    const qint64 at = timestamp(row, "last_watched");
    constexpr qint64 maxExactMilliseconds = (qint64(1) << 53) - 1;
    if (position < 0 || duration <= 0 || duration > maxExactMilliseconds || position > duration || at <= 0)
        return {Category::Progress, {}, Disposition::Skipped};
    const QVariantMap library = identities.library(local.type, local.content);
    QVariantMap record = identities.progress(local.video());
    const QString title = library.value("title", record.value("title")).toString();
    record.insert("kind", QStringLiteral("video"));
    record.insert("id", local.video());
    record.insert("libraryId", local.content);
    record.insert("type", local.type);
    if (!title.isEmpty()) {
        record.insert("title", title);
        record.insert("caption", record.value("caption", title));
    }
    const QString cover = library.value("cover", record.value("cover")).toString();
    if (!cover.isEmpty())
        record.insert("cover", cover);
    record.insert("duration", static_cast<double>(duration) / 1000.0);
    record.insert("progress", static_cast<double>(position) / duration);
    record.insert("updatedAt", at);
    record.insert("resume", QVariantMap{{"position", static_cast<double>(position) / 1000.0}});
    record.remove("watched");
    return {Category::Progress, std::move(record)};
}

struct RemoteFact {
    Identity identity;
    qint64 at = 0;
    bool deleted = false;
};

QHash<QString, RemoteFact> remoteFacts(const QJsonArray &rows, const Identities &identities,
    const char *timeField, bool mayBeEpisode) {
    QHash<QString, RemoteFact> facts;
    for (const QJsonValue &value : rows) {
        const QJsonObject row = value.toObject();
        const Identity raw = remoteIdentity(row);
        const Identity canonical = canonicalIdentity(raw, identities, mayBeEpisode && raw.type == kSeries);
        if (canonical.content.isEmpty())
            continue;
        const QString key = libraryKey(canonical.type, canonical.video());
        const RemoteFact incoming{raw, timestamp(row, timeField), row.value("operation") == QLatin1String("delete")};
        const auto current = facts.constFind(key);
        if (current == facts.constEnd() || incoming.at > current->at)
            facts.insert(key, incoming);
    }
    return facts;
}

bool canPush(qint64 localAt, const RemoteFact &remote) {
    return remote.identity.content.isEmpty()
        ? localAt > 0
        : localAt > 0 && (remote.deleted ? localAt > remote.at : remote.at > 0 && localAt > remote.at);
}
}

namespace {
const char *removalTimeField(NuvioTheatre::RemovalKind kind) {
    switch (kind) {
    case NuvioTheatre::RemovalKind::Library: return "added_at";
    case NuvioTheatre::RemovalKind::Watched: return "watched_at";
    case NuvioTheatre::RemovalKind::Progress: return "last_watched";
    }
    return "";
}

QString removalContentId(const QString &id, const QString &type) {
    if (type == kSeries) {
        const Identity episode = localVideo(id);
        if (episode.isEpisode())
            return episode.content;
    }
    return id;
}
}

QJsonObject NuvioTheatre::Removal::serialize() const {
    return {{"kind", static_cast<int>(kind)}, {"id", id}, {"type", type},
        {"aliases", QJsonArray::fromStringList(contentIds)}, {"at", at},
        {"confirmedAt", confirmedAt}, {"wholeTitle", wholeTitle}};
}

std::optional<NuvioTheatre::Removal> NuvioTheatre::Removal::deserialize(const QJsonObject &row) {
    const int kind = row.value("kind").toInt(-1);
    if (kind < 0 || kind > static_cast<int>(RemovalKind::Progress))
        return std::nullopt;
    auto removal = makeRemoval(static_cast<RemovalKind>(kind), row.value("id").toString(),
        row.value("type").toString(), row.value("wholeTitle").toBool(),
        row.value("at").toInteger(), {});
    const qint64 confirmedAt = row.value("confirmedAt").toInteger(-1);
    const auto aliases = row.value("aliases").toArray();
    if (!removal || confirmedAt < 0 || !row.value("aliases").isArray() || aliases.size() > 4)
        return std::nullopt;
    const QString root = removalContentId(removal->id, removal->type);
    for (const QJsonValue &alias : aliases) {
        const QString value = alias.toString();
        if (!isTmdbId(value) && (imdbId(value).isEmpty() || imdbId(value) != root))
            return std::nullopt;
        if (!removal->contentIds.contains(value))
            removal->contentIds.append(value);
    }
    removal->confirmedAt = confirmedAt;
    return removal;
}

bool NuvioTheatre::Removal::sameTarget(const Removal &other) const {
    return kind == other.kind && id == other.id && type == other.type && wholeTitle == other.wholeTitle;
}

bool NuvioTheatre::Removal::matches(const QJsonObject &row) const {
    const Identity raw = remoteIdentity(row);
    if (type != raw.type || !contentIds.contains(raw.content))
        return false;
    if (kind == RemovalKind::Library || type == kMovie || wholeTitle)
        return true;
    const Identity episode = localVideo(id);
    return raw.season == episode.season && raw.episode == episode.episode;
}

bool NuvioTheatre::Removal::blocks(const QJsonObject &row) const {
    return matches(row) && timestamp(row, removalTimeField(kind)) <= at;
}

std::optional<NuvioTheatre::Removal> NuvioTheatre::makeRemoval(RemovalKind kind,
    const QString &id, const QString &type, bool wholeTitle, qint64 at, const Local &local) {
    if (at <= 0 || (type != kMovie && type != kSeries)
        || (wholeTitle && kind != RemovalKind::Progress)) {
        return std::nullopt;
    }
    const QString root = removalContentId(id, type);
    if ((imdbId(root).isEmpty() || imdbId(root) != root) && !isTmdbId(root))
        return std::nullopt;
    const bool needsEpisode = type == kSeries && kind != RemovalKind::Library && !wholeTitle;
    if (needsEpisode != localVideo(id).isEpisode() || (!needsEpisode && id != root))
        return std::nullopt;
    Removal removal{kind, id, type, {root}, at, 0, wholeTitle};
    if (imdbId(root) == root) {
        removal.contentIds.append(QStringLiteral("imdb:") + root);
        const qint64 tmdb = Identities(local).library(type, root).value("tmdbId").toLongLong();
        if (tmdb > 0 && tmdb <= 9999999999LL)
            removal.contentIds.append(QStringLiteral("tmdb:%1").arg(tmdb));
    }
    return removal;
}

bool NuvioTheatre::isSuppressed(RemovalKind kind, const QJsonObject &row, const QList<Removal> &removals) {
    return std::any_of(removals.cbegin(), removals.cend(), [&](const Removal &removal) {
        return removal.kind == kind && removal.blocks(row);
    });
}

void NuvioTheatre::suppressRemoved(Snapshot &snapshot, const QList<Removal> &removals) {
    if (removals.isEmpty())
        return;
    const auto filter = [&](QJsonArray &rows, RemovalKind kind) {
        QJsonArray current;
        for (const QJsonValue &value : std::as_const(rows)) {
            const auto row = value.toObject();
            if (row.value("operation") == QLatin1String("delete") || !isSuppressed(kind, row, removals))
                current.append(value);
        }
        rows = std::move(current);
    };
    filter(snapshot.library, RemovalKind::Library);
    filter(snapshot.watched, RemovalKind::Watched);
    filter(snapshot.progress, RemovalKind::Progress);
}

QJsonArray NuvioTheatre::deletionKeys(const Removal &removal, const QJsonArray &remoteRows) {
    QJsonArray keys;
    QSet<QString> progressKeys;
    for (const QJsonValue &value : remoteRows) {
        const QJsonObject row = value.toObject();
        if (row.value("operation") == QLatin1String("delete") || !removal.blocks(row))
            continue;
        const Identity raw = remoteIdentity(row);
        if (removal.kind == RemovalKind::Progress) {
            const QString key = row.value("progress_key").toString(raw.progressKey());
            if (!key.isEmpty() && key.size() <= 512 && !key.contains(QChar::Null) && !progressKeys.contains(key)) {
                progressKeys.insert(key);
                keys.append(key);
            }
        } else if (removal.kind == RemovalKind::Library) {
            keys.append(QJsonObject{{"content_id", raw.content}, {"content_type", raw.type}});
        } else {
            keys.append(QJsonObject{{"content_id", raw.content},
                {"season", raw.isEpisode() ? QJsonValue(raw.season) : QJsonValue()},
                {"episode", raw.isEpisode() ? QJsonValue(raw.episode) : QJsonValue()}});
        }
    }
    return keys;
}

QList<NuvioTheatre::Profile> NuvioTheatre::profiles(const QJsonArray &rows, const QString &accountId) {
    QList<Profile> result;
    QSet<int> seenIndexes;
    QSet<QString> seenIds;
    if (rows.size() > 6)
        return {};
    for (const QJsonValue &value : rows) {
        const QJsonObject row = value.toObject();
        const QString id = uuid(row.value("id"));
        const int index = row.value("profile_index").toInt();
        if (id.isEmpty() || accountId.isEmpty() || uuid(row.value("user_id")) != accountId
            || index < 1 || index > 6 || seenIndexes.contains(index) || seenIds.contains(id)) {
            return {};
        }
        seenIds.insert(id);
        seenIndexes.insert(index);
        const QJsonValue pinExpiry = row.value("pin_locked_until");
        const QDateTime lockedUntil = QDateTime::fromString(pinExpiry.toString(), Qt::ISODateWithMs);
        const bool locked = row.value("pin_enabled").toBool(true)
            || (!pinExpiry.isNull() && !pinExpiry.isUndefined()
                && (!lockedUntil.isValid() || lockedUntil > QDateTime::currentDateTimeUtc()));
        result.append({id, index, cleanTitle(row.value("name"), QObject::tr("Profile %1").arg(index)),
            locked, row.value("uses_primary_addons").toBool()});
    }
    std::sort(result.begin(), result.end(), [](const Profile &left, const Profile &right) {
        return left.index < right.index;
    });
    return result;
}

QList<Item> NuvioTheatre::project(const Snapshot &snapshot, const Local &local) {
    QList<Item> items;
    const Identities initial(local);
    Local known = local;
    for (const QJsonValue &value : snapshot.library) {
        Item item = libraryItem(value.toObject(), initial);
        if (item.disposition == Disposition::Matched)
            known.library.append(item.record);
        items.append(std::move(item));
    }
    const Identities identities(known);
    for (const QJsonValue &value : snapshot.watched)
        items.append(watchedItem(value.toObject(), identities));
    for (const QJsonValue &value : snapshot.progress)
        items.append(progressItem(value.toObject(), identities));
    for (const QJsonValue &value : snapshot.addons) {
        const QJsonObject row = value.toObject();
        items.append({Category::Addon, {{"url", row.value("url").toString()},
            {"enabled", row.value("enabled").toBool(true)}}});
    }
    return items;
}

NuvioTheatre::Writes NuvioTheatre::reconcile(const Snapshot &snapshot, const Local &local) {
    const Identities identities(local);
    const auto library = remoteFacts(snapshot.library, identities, "added_at", false);
    const auto watched = remoteFacts(snapshot.watched, identities, "watched_at", true);
    const auto progress = remoteFacts(snapshot.progress, identities, "last_watched", true);
    Writes writes;
    for (const QVariant &value : local.library) {
        const QVariantMap record = value.toMap();
        const QString id = record.value("id").toString();
        const QString type = record.value("type").toString();
        const qint64 at = record.value("addedAt").toLongLong();
        if (identities.canonical(type, id) != id || id.isEmpty())
            continue;
        const RemoteFact current = library.value(libraryKey(type, id));
        if (!canPush(at, current))
            continue;
        const QString key = current.identity.content.isEmpty() ? id : current.identity.content;
        QJsonObject row = Identity{key, type}.wire();
        row.insert("added_at", at);
        row.insert("name", cleanTitle(QJsonValue(record.value("title").toString()), id));
        const QString cover = coverUrl(record.value("cover").toString());
        if (!cover.isEmpty())
            row.insert("poster", cover);
        writes.library.append(row);
    }

    QHash<QString, qint64> watchedTimes;
    QHash<QString, QString> watchedTitles;
    QSet<QString> movieHistoryIds;
    for (const QVariant &value : local.history) {
        const QVariantMap record = value.toMap();
        const QString kind = record.value("kind").toString();
        const QString id = record.value("id").toString();
        const Identity identity = localVideo(id);
        if (identity.content.isEmpty() || (kind != kEpisode && kind != kMovie)
            || identity.isEpisode() != (kind == kEpisode)) {
            continue;
        }
        if (kind == kMovie)
            movieHistoryIds.insert(id);
        qint64 at = record.value("completedAt").toLongLong();
        const QString source = record.value("source").toString();
        if ((source == QLatin1String("nuvio") || source == QLatin1String("stremio")) && at > 0)
            at = qMax(at, record.value("latestKnownAt").toLongLong());
        if (at > 0) {
            watchedTimes.insert(id, at);
            watchedTitles.insert(id, record.value("displayTitle").toString());
        }
    }
    for (auto mark = local.movieMarks.cbegin(); mark != local.movieMarks.cend(); ++mark) {
        const Identity identity = localVideo(mark.key());
        const qint64 at = local.movieMarkTimes.value(mark.key());
        if (identity.content.isEmpty() || identity.isEpisode())
            continue;
        const RemoteFact current = watched.value(libraryKey(kMovie, mark.key()));
        // WatchedMark also stores series-root overrides. An IMDb-shaped key
        // alone does not establish that the mark belongs to a movie.
        if (identities.library(kMovie, mark.key()).isEmpty() && !movieHistoryIds.contains(mark.key())
            && current.identity.content.isEmpty()) {
            continue;
        }
        if (at <= 0) {
            if (mark.value() == -1)
                watchedTimes.remove(mark.key());
            continue;
        }
        if (mark.value() == 1)
            watchedTimes.insert(mark.key(), qMax(at, watchedTimes.value(mark.key())));
        if (mark.value() != -1 || at < watchedTimes.value(mark.key()))
            continue;
        watchedTimes.remove(mark.key());
        if (local.unwatchAfter > 0 && at > local.unwatchAfter
            && !current.deleted && !current.identity.content.isEmpty() && canPush(at, current)) {
            writes.unwatchMovies.append(QJsonObject{{"content_id", current.identity.content},
                {"local_id", mark.key()}, {"action_at", at}});
        }
    }
    QHash<QString, qint64> implicitCompletions;
    for (const QVariant &value : local.progress) {
        const QVariantMap record = value.toMap();
        if (record.value("kind").toString() != QLatin1String("video"))
            continue;
        const QString id = record.value("id").toString();
        const Identity identity = localVideo(id);
        const qint64 at = record.value("updatedAt").toLongLong();
        const QVariantMap resume = record.value("resume").toMap();
        if (identity.content.isEmpty() || at <= 0
            || record.value("libraryId", identity.content).toString() != identity.content
            || (!identity.isEpisode()
                && (record.value("type").toString() == kSeries
                    || resume.value("subType").toString() == kSeries
                    || !identities.library(kSeries, id).isEmpty())))
            continue;
        const RemoteFact current = progress.value(libraryKey(identity.type, id));
        if (!canPush(at, current)
            || (current.identity.content.isEmpty() && at <= snapshot.progressWindowFloor))
            continue;
        bool positionOk = false;
        bool durationOk = false;
        const double position = resume.value("position").toDouble(&positionOk);
        const double duration = record.value("duration").toDouble(&durationOk);
        if (!positionOk || !durationOk || !std::isfinite(position) || !std::isfinite(duration)
            || position < 0 || duration <= 0 || position > duration || duration > 8640000.0)
            continue;
        const qint64 positionMs = std::llround(position * 1000);
        const qint64 durationMs = std::llround(duration * 1000);
        // Nuvio creates a watched fact as a side effect of a completion push.
        // Do not let an older position undo a newer explicit unwatched action.
        const bool completes = durationMs >= 60000 && positionMs >= 0.90 * durationMs;
        if (completes) {
            const RemoteFact remoteWatched = watched.value(libraryKey(identity.type, id));
            const qint64 localMarkAt = local.movieMarkTimes.value(id);
            const bool newerUnwatch = identity.isEpisode()
                ? local.episodeRemovalTimes.value(id) >= at
                : local.movieMarks.value(id) == -1 && (localMarkAt <= 0 || localMarkAt >= at);
            if (newerUnwatch || (!remoteWatched.identity.content.isEmpty()
                && (remoteWatched.at == 0 || remoteWatched.at > at))) {
                continue;
            }
            implicitCompletions.insert(id, at);
        }
        const Identity target = current.identity.content.isEmpty() ? identity : current.identity;
        QJsonObject row = target.wire();
        row.insert("video_id", target.video());
        row.insert("position", positionMs);
        row.insert("duration", durationMs);
        row.insert("last_watched", at);
        writes.progress.append(row);
    }
    for (auto fact = watchedTimes.cbegin(); fact != watchedTimes.cend(); ++fact) {
        const Identity identity = localVideo(fact.key());
        const RemoteFact current = watched.value(libraryKey(identity.type, fact.key()));
        if (fact.value() <= implicitCompletions.value(fact.key()) || !canPush(fact.value(), current))
            continue;
        QJsonObject row = (current.identity.content.isEmpty() ? identity : current.identity).wire();
        row.insert("watched_at", fact.value());
        row.insert("title", cleanTitle(QJsonValue(watchedTitles.value(fact.key())), fact.key()));
        writes.watched.append(row);
    }
    return writes;
}
