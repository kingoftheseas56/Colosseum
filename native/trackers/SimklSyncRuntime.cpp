#include "SimklSyncRuntime.h"

#include "SimklApiClient.h"
#include "TrackerCanonicalDeliverySource.h"
#include "TrackerConnectionStore.h"
#include "TrackerMappingStore.h"
#include "TrackerProgressImportOwner.h"
#include "TrackerSyncCenterModel.h"
#include "TrackerSyncSettingsStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// In-flight provider work can outlive this runtime: the API client and the
// import journal are destroyed after it during profile teardown. Every
// asynchronous callback therefore re-enters through a null-checked guard
// instead of a raw `this` capture.
template <typename Callback>
auto guarded(const QPointer<SimklSyncRuntime> &self, Callback &&callback)
{
    return [self, callback = std::forward<Callback>(callback)](
               auto &&...arguments) mutable {
        if (!self)
            return;
        callback(std::forward<decltype(arguments)>(arguments)...);
    };
}

constexpr int kDeliveryDispatchCooldownMs = 1100;
constexpr qint64 kMaximumDispatchDelayMs = 60LL * 60 * 1000;

struct RemoteIdentity {
    QString kind;
    QString simklId;
    int season = 0;
    int episode = 0;
};

struct ParsedRemoteFact {
    TrackerRemoteMediaKey remote;
    QString providerItemId;
    QString title;
    QStringList exactLocalIds;
    int progress = 0;
    bool completed = false;
    bool supported = true;
    qint64 occurredAtMs = 0;
    TrackerImportedEventKind eventKind = TrackerImportedEventKind::Activity;
    QString fingerprint;
};

QString digest(const QByteArray &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        value, QCryptographicHash::Sha256).toHex());
}

qint64 isoMs(const QString &value)
{
    return QDateTime::fromString(value, Qt::ISODateWithMs).toMSecsSinceEpoch();
}

QString simklId(const QJsonObject &media)
{
    const QJsonValue value = media.value(QStringLiteral("ids"))
                                 .toObject().value(QStringLiteral("simkl"));
    if (value.isDouble())
        return QString::number(static_cast<qint64>(value.toDouble()));
    return value.toString().trimmed();
}

QString idString(const QJsonObject &ids, const QString &key)
{
    const QJsonValue value = ids.value(key);
    if (value.isDouble())
        return QString::number(static_cast<qint64>(value.toDouble()));
    return value.toString().trimmed();
}

QStringList exactMovieIds(const QJsonObject &media)
{
    const QJsonObject ids = media.value(QStringLiteral("ids")).toObject();
    QStringList result;
    for (const QString &key : {QStringLiteral("imdb"), QStringLiteral("tmdb")}) {
        const QString value = idString(ids, key);
        if (!value.isEmpty())
            result.append(value);
    }
    return result;
}

QStringList exactEpisodeIds(const QJsonObject &media, int season, int episode)
{
    QStringList result;
    const QJsonObject ids = media.value(QStringLiteral("ids")).toObject();
    for (const QString &key : {QStringLiteral("imdb"), QStringLiteral("tmdb"),
                               QStringLiteral("tvdb"), QStringLiteral("mal")}) {
        const QString value = idString(ids, key);
        if (value.isEmpty())
            continue;
        result.append(value + QLatin1Char(':') + QString::number(season)
                      + QLatin1Char(':') + QString::number(episode));
        result.append(value + QStringLiteral(":S%1E%2")
                      .arg(season, 2, 10, QLatin1Char('0'))
                      .arg(episode, 2, 10, QLatin1Char('0')));
    }
    return result;
}

std::optional<RemoteIdentity> parseIdentity(const QString &value)
{
    const QStringList pieces = value.split(QLatin1Char(':'));
    bool idOk = false;
    if (pieces.size() == 2
        && (pieces.first() == QLatin1String("movie")
            || pieces.first() == QLatin1String("show")
            || pieces.first() == QLatin1String("anime"))) {
        pieces.at(1).toULongLong(&idOk);
        if (idOk)
            return RemoteIdentity{pieces.first(), pieces.at(1), 0, 0};
    }
    if (pieces.size() == 4
        && (pieces.first() == QLatin1String("episode")
            || pieces.first() == QLatin1String("anime_episode"))) {
        bool seasonOk = false;
        bool episodeOk = false;
        pieces.at(1).toULongLong(&idOk);
        const int season = pieces.at(2).toInt(&seasonOk);
        const int episode = pieces.at(3).toInt(&episodeOk);
        if (idOk && seasonOk && episodeOk && season >= 0 && episode > 0)
            return RemoteIdentity{pieces.first(), pieces.at(1), season, episode};
    }
    value.toULongLong(&idOk);
    if (idOk)
        return RemoteIdentity{QStringLiteral("movie"), value, 0, 0};
    return std::nullopt;
}

QString remoteId(const QString &kind, const QString &id, int season = 0, int episode = 0)
{
    if (episode > 0)
        return kind + QLatin1Char(':') + id + QLatin1Char(':')
            + QString::number(season) + QLatin1Char(':') + QString::number(episode);
    return kind + QLatin1Char(':') + id;
}

QString eventId(const QString &remote, qint64 occurredAtMs, const QString &kind)
{
    return QStringLiteral("simkl-") + digest(
        (remote + QLatin1Char('\0') + QString::number(occurredAtMs)
         + QLatin1Char('\0') + kind).toUtf8());
}

QString safeTitle(const QString &title, int season = 0, int episode = 0)
{
    QString result = title.simplified().left(420);
    if (episode > 0) {
        result += season > 0
            ? QStringLiteral(" · S%1E%2").arg(season, 2, 10, QLatin1Char('0'))
                                         .arg(episode, 2, 10, QLatin1Char('0'))
            : QStringLiteral(" · E%1").arg(episode);
    }
    return result.left(500);
}

QList<ParsedRemoteFact> parseAllItems(const QJsonDocument &document,
                                      TrackerProviderId providerId,
                                      const QString &account)
{
    QList<ParsedRemoteFact> facts;
    const QJsonObject root = document.object();
    for (const QJsonValue &value : root.value(QStringLiteral("movies")).toArray()) {
        const QJsonObject row = value.toObject();
        const QJsonObject media = row.value(QStringLiteral("movie")).toObject();
        const QString id = simklId(media);
        const QString watchedAt = row.value(QStringLiteral("last_watched_at")).toString();
        const bool completed = row.value(QStringLiteral("status")).toString()
            == QLatin1String("completed") || !watchedAt.isEmpty();
        if (id.isEmpty() || !completed)
            continue;
        const QString remote = remoteId(QStringLiteral("movie"), id);
        const qint64 at = isoMs(watchedAt);
        const QByteArray material = QJsonDocument(row).toJson(QJsonDocument::Compact);
        facts.append({{providerId, account, remote},
                      QStringLiteral("history-") + digest(material),
                      safeTitle(media.value(QStringLiteral("title")).toString()),
                      exactMovieIds(media), 100, true, true, at,
                      TrackerImportedEventKind::Completion, digest(material)});
    }

    const auto parseSeries = [&](const QString &arrayKey, bool anime) {
        for (const QJsonValue &value : root.value(arrayKey).toArray()) {
            const QJsonObject row = value.toObject();
            const QJsonObject media = row.value(QStringLiteral("show")).toObject();
            const QString id = simklId(media);
            if (id.isEmpty())
                continue;
            bool emittedEpisode = false;
            for (const QJsonValue &seasonValue : row.value(QStringLiteral("seasons")).toArray()) {
                const QJsonObject seasonObject = seasonValue.toObject();
                const int season = seasonObject.value(QStringLiteral("number")).toInt(
                    anime ? 0 : 1);
                for (const QJsonValue &episodeValue
                     : seasonObject.value(QStringLiteral("episodes")).toArray()) {
                    const QJsonObject episodeObject = episodeValue.toObject();
                    const int number = episodeObject.value(QStringLiteral("number")).toInt(
                        episodeObject.value(QStringLiteral("episode")).toInt());
                    const QString watchedAt = episodeObject.value(
                        QStringLiteral("watched_at")).toString();
                    const qint64 at = isoMs(watchedAt);
                    if (number <= 0 || at <= 0)
                        continue;
                    emittedEpisode = true;
                    const QString remote = remoteId(
                        anime ? QStringLiteral("anime_episode")
                              : QStringLiteral("episode"),
                        id, season, number);
                    const QByteArray material = QJsonDocument(episodeObject)
                                                    .toJson(QJsonDocument::Compact)
                        + remote.toUtf8();
                    facts.append({{providerId, account, remote},
                                  QStringLiteral("history-") + digest(material),
                                  safeTitle(media.value(QStringLiteral("title")).toString(),
                                            season, number),
                                  exactEpisodeIds(media, season, number),
                                  100, true, true, at,
                                  TrackerImportedEventKind::Completion,
                                  digest(material)});
                }
            }
            const int watched = row.value(QStringLiteral("watched_episodes_count")).toInt();
            if (watched > 0 && !emittedEpisode) {
                const QString remote = remoteId(
                    anime ? QStringLiteral("anime") : QStringLiteral("show"), id);
                const QByteArray material = QJsonDocument(row).toJson(QJsonDocument::Compact);
                facts.append({{providerId, account, remote},
                              QStringLiteral("aggregate-") + digest(material),
                              safeTitle(media.value(QStringLiteral("title")).toString()),
                              exactMovieIds(media), watched,
                              row.value(QStringLiteral("status")).toString()
                                  == QLatin1String("completed"),
                              false, isoMs(row.value(QStringLiteral("last_watched_at")).toString()),
                              TrackerImportedEventKind::Activity, digest(material)});
            }
        }
    };
    parseSeries(QStringLiteral("shows"), false);
    parseSeries(QStringLiteral("anime"), true);
    return facts;
}

QList<ParsedRemoteFact> parsePlaybacks(const QJsonDocument &document,
                                       TrackerProviderId providerId,
                                       const QString &account)
{
    QList<ParsedRemoteFact> facts;
    if (!document.isArray())
        return facts;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject row = value.toObject();
        const QString type = row.value(QStringLiteral("type")).toString();
        const double progress = row.value(QStringLiteral("progress")).toDouble(-1.0);
        if (!std::isfinite(progress) || progress < 0.0 || progress > 100.0)
            continue;
        QJsonObject media;
        QString remote;
        QStringList localIds;
        int season = 0;
        int episode = 0;
        if (type == QLatin1String("movie")) {
            media = row.value(QStringLiteral("movie")).toObject();
            const QString id = simklId(media);
            if (id.isEmpty())
                continue;
            remote = remoteId(QStringLiteral("movie"), id);
            localIds = exactMovieIds(media);
        } else {
            const bool anime = row.contains(QStringLiteral("anime"));
            media = row.value(anime ? QStringLiteral("anime")
                                    : QStringLiteral("show")).toObject();
            const QJsonObject episodeObject = row.value(QStringLiteral("episode")).toObject();
            season = episodeObject.value(QStringLiteral("season")).toInt(anime ? 0 : 1);
            episode = episodeObject.value(QStringLiteral("number")).toInt(
                episodeObject.value(QStringLiteral("episode")).toInt());
            const QString id = simklId(media);
            if (id.isEmpty() || episode <= 0)
                continue;
            remote = remoteId(anime ? QStringLiteral("anime_episode")
                                    : QStringLiteral("episode"),
                              id, season, episode);
            localIds = exactEpisodeIds(media, season, episode);
        }
        const QByteArray material = QJsonDocument(row).toJson(QJsonDocument::Compact);
        facts.append({{providerId, account, remote},
                      QStringLiteral("playback-") + digest(material),
                      safeTitle(media.value(QStringLiteral("title")).toString(),
                                season, episode),
                      localIds, qBound(0, qRound(progress), 100), false, true,
                      isoMs(row.value(QStringLiteral("paused_at")).toString()),
                      TrackerImportedEventKind::Activity, digest(material)});
    }
    return facts;
}

std::optional<TrackerCanonicalTitleCandidate> uniqueCandidate(
    const QList<TrackerCanonicalTitleCandidate> &candidates,
    const QStringList &ids)
{
    std::optional<TrackerCanonicalTitleCandidate> match;
    for (const TrackerCanonicalTitleCandidate &candidate : candidates) {
        bool matches = false;
        for (const QString &id : ids) {
            if (candidate.historyId.compare(id, Qt::CaseInsensitive) == 0) {
                matches = true;
                break;
            }
        }
        if (!matches)
            continue;
        if (match && match->canonicalMediaId != candidate.canonicalMediaId)
            return std::nullopt;
        match = candidate;
    }
    return match;
}

QJsonObject deliveryBody(const TrackerDeliveryOperation &operation)
{
    const auto identity = parseIdentity(operation.mapping.remote.remoteMediaId);
    if (!identity)
        return {};
    const QJsonObject ids{{QStringLiteral("simkl"), identity->simklId.toLongLong()}};
    if (operation.fact.kind == TrackerDeliveryFactKind::Progress) {
        QJsonObject body{{QStringLiteral("progress"), operation.fact.progress}};
        if (identity->kind == QLatin1String("movie"))
            body.insert(QStringLiteral("movie"), QJsonObject{{QStringLiteral("ids"), ids}});
        else {
            body.insert(identity->kind == QLatin1String("anime_episode")
                            ? QStringLiteral("anime") : QStringLiteral("show"),
                        QJsonObject{{QStringLiteral("ids"), ids}});
            QJsonObject episode{{QStringLiteral("number"), identity->episode}};
            if (identity->season > 0)
                episode.insert(QStringLiteral("season"), identity->season);
            body.insert(QStringLiteral("episode"), episode);
        }
        return body;
    }

    const QString watchedAt = QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>(operation.fact.sourceRevision), Qt::UTC).toString(Qt::ISODateWithMs);
    if (identity->kind == QLatin1String("movie")) {
        return {{QStringLiteral("movies"), QJsonArray{QJsonObject{
            {QStringLiteral("ids"), ids}, {QStringLiteral("watched_at"), watchedAt}}}}};
    }
    QJsonObject episode{{QStringLiteral("number"), identity->episode},
                        {QStringLiteral("watched_at"), watchedAt}};
    const int seasonNumber = identity->season > 0 ? identity->season : 1;
    QJsonObject season{{QStringLiteral("number"), seasonNumber},
                       {QStringLiteral("episodes"), QJsonArray{episode}}};
    QJsonObject item{{QStringLiteral("ids"), ids},
                     {QStringLiteral("seasons"), QJsonArray{season}}};
    return {{QStringLiteral("shows"), QJsonArray{item}}};
}

} // namespace

struct SimklSyncRuntime::PullState {
    TrackerConnection connection;
    QString baseCursor;
    QString proposedCursor;
    bool initial = true;
    bool explicitRequest = false;
    int nextRequest = 0;
    QList<QJsonDocument> documents;
    QString error;
};

struct SimklSyncRuntime::SnapshotState {
    TrackerConnection connection;
    QList<TrackerDeliveryFact> facts;
    ExportSnapshotCompletion completion;
    QJsonDocument allItems;
    QJsonDocument playback;
};

SimklSyncRuntime::SimklSyncRuntime(
    TrackerConnectionStore *connections,
    TrackerMappingStore *mappings,
    TrackerImportStore *imports,
    TrackerProgressImportOwner *importOwner,
    TrackerHistoryEvidenceStore *historyEvidence,
    TrackerDeliveryStore *delivery,
    TrackerCanonicalDeliverySource *deliverySource,
    TrackerSyncSettingsStore *settings,
    TrackerSyncCenterModel *syncCenter,
    SimklApiClient *api,
    QObject *parent)
    : QObject(parent), m_connections(connections), m_mappings(mappings),
      m_imports(imports), m_importOwner(importOwner),
      m_historyEvidence(historyEvidence), m_delivery(delivery),
      m_deliverySource(deliverySource), m_settings(settings),
      m_syncCenter(syncCenter), m_api(api)
{
    if (m_syncCenter) {
        connect(m_syncCenter, &TrackerSyncCenterModel::modelChanged,
                this, [this] {
                    settleConfirmedEvidence();
                    dispatchNextDelivery();
                });
    }
}

void SimklSyncRuntime::start()
{
    if (!m_api || !m_api->available() || !m_settings || !m_connections)
        return;
    const TrackerGlobalSyncSettings global = m_settings->globalSettings();
    if (global.trackerSyncEnabled && global.checkOnLaunch)
        QTimer::singleShot(0, this, [this] { pullSimkl(false); });
    if (global.trackerSyncEnabled && global.backgroundDelivery)
        QTimer::singleShot(0, this, &SimklSyncRuntime::dispatchNextDelivery);
}

void SimklSyncRuntime::syncAll(const QStringList &providerKeys, quint64)
{
    if (providerKeys.contains(QStringLiteral("simkl")))
        pullSimkl(true);
}

void SimklSyncRuntime::connectionEstablished(const QString &providerKey)
{
    if (providerKey == QLatin1String("simkl"))
        pullSimkl(true);
}

void SimklSyncRuntime::pullSimkl(bool explicitRequest)
{
    if (!m_api || !m_imports || !m_connections || !m_settings
        || !m_settings->globalSettings().trackerSyncEnabled)
        return;
    const auto connection = m_connections->connection(TrackerProviderId::Simkl);
    if (!connection || connection->state != TrackerConnectionState::Connected
        || m_pullingAccounts.contains(connection->remoteAccountId))
        return;
    m_pullingAccounts.insert(connection->remoteAccountId);
    const auto state = std::make_shared<PullState>();
    state->connection = *connection;
    state->explicitRequest = explicitRequest;
    const auto cursor = m_imports->confirmedCursor(
        TrackerProviderId::Simkl, connection->remoteAccountId);
    state->initial = !cursor.has_value();
    state->baseCursor = cursor.value_or(QString());
    m_api->get(QStringLiteral("/sync/activities"), {},
        guarded(QPointer<SimklSyncRuntime>(this),
            [this, state](const SimklApiResult &result) {
            if (!result.succeeded() || !result.document.isObject()) {
                m_pullingAccounts.remove(state->connection.remoteAccountId);
                if (m_syncCenter)
                    m_syncCenter->refresh();
                return;
            }
            state->proposedCursor = result.document.object()
                .value(QStringLiteral("all")).toString();
            if (state->proposedCursor.isEmpty()) {
                state->proposedCursor = QDateTime::currentDateTimeUtc()
                    .toString(Qt::ISODateWithMs);
            }
            if (!state->initial && state->proposedCursor == state->baseCursor) {
                m_settings->recordSuccessfulSync(TrackerProviderId::Simkl,
                    state->connection.remoteAccountId, QDateTime::currentMSecsSinceEpoch());
                m_pullingAccounts.remove(state->connection.remoteAccountId);
                dispatchNextDelivery();
                if (m_syncCenter)
                    m_syncCenter->refresh();
                return;
            }
            fetchNext(state);
        }));
}

void SimklSyncRuntime::fetchNext(const std::shared_ptr<PullState> &state)
{
    static const QStringList paths{
        QStringLiteral("/sync/all-items/movies"),
        QStringLiteral("/sync/all-items/shows"),
        QStringLiteral("/sync/all-items/anime"),
        QStringLiteral("/sync/playback")};
    if (state->nextRequest >= paths.size()) {
        finishPull(state);
        return;
    }
    QUrlQuery query;
    if (state->nextRequest == 1 || state->nextRequest == 2) {
        query.addQueryItem(QStringLiteral("extended"), QStringLiteral("full"));
        query.addQueryItem(QStringLiteral("episode_watched_at"), QStringLiteral("yes"));
        query.addQueryItem(QStringLiteral("include_all_episodes"), QStringLiteral("original"));
        query.addQueryItem(QStringLiteral("language"), QStringLiteral("en"));
    }
    if (!state->initial && state->nextRequest < 3)
        query.addQueryItem(QStringLiteral("date_from"), state->baseCursor);
    const QString path = paths.at(state->nextRequest++);
    m_api->get(path, query,
        guarded(QPointer<SimklSyncRuntime>(this),
            [this, state](const SimklApiResult &result) {
            if (!result.succeeded()
                || (!result.document.isObject() && !result.document.isArray())) {
                m_pullingAccounts.remove(state->connection.remoteAccountId);
                if (m_syncCenter)
                    m_syncCenter->refresh();
                return;
            }
            state->documents.append(result.document);
            fetchNext(state);
        }));
}

void SimklSyncRuntime::finishPull(const std::shared_ptr<PullState> &state)
{
    QList<ParsedRemoteFact> parsed;
    for (int i = 0; i < qMin(3, static_cast<int>(state->documents.size())); ++i)
        parsed.append(parseAllItems(state->documents.at(i), TrackerProviderId::Simkl,
                                    state->connection.remoteAccountId));
    if (state->documents.size() > 3)
        parsed.append(parsePlaybacks(state->documents.at(3), TrackerProviderId::Simkl,
                                     state->connection.remoteAccountId));

    QByteArray snapshotMaterial;
    for (const QJsonDocument &document : state->documents)
        snapshotMaterial += document.toJson(QJsonDocument::Compact);
    const QString snapshotId = QStringLiteral("simkl-") + digest(snapshotMaterial);
    const QList<TrackerCanonicalTitleCandidate> candidates =
        m_importOwner ? m_importOwner->userCandidates()
                      : QList<TrackerCanonicalTitleCandidate>{};

    TrackerImportBatchDraft draft;
    draft.providerId = TrackerProviderId::Simkl;
    draft.remoteAccountId = state->connection.remoteAccountId;
    draft.connectionGeneration = state->connection.connectionGeneration;
    draft.snapshotId = snapshotId;
    draft.proposedCursor = state->proposedCursor;
    draft.initialImport = state->initial;
    draft.pageComplete = true;
    draft.baseCursor = state->baseCursor;
    QList<PendingEvidence> evidence;

    for (const ParsedRemoteFact &fact : parsed) {
        auto mapping = m_mappings->mapping(fact.remote);
        if (!mapping) {
            const auto candidate = uniqueCandidate(candidates, fact.exactLocalIds);
            if (candidate) {
                QString ignored;
                if (m_mappings->upsert(fact.remote, *candidate,
                                       TrackerMappingProvenance::ExactProviderIdentity,
                                       &ignored)) {
                    mapping = m_mappings->mapping(fact.remote);
                }
            }
        }
        TrackerImportRemoteItem item;
        item.providerItemId = fact.providerItemId;
        item.remote = fact.remote;
        item.mapping = mapping;
        item.progress = fact.progress;
        item.completed = fact.completed;
        item.supported = fact.supported;
        item.displayTitle = fact.title.isEmpty() ? QStringLiteral("Untitled SIMKL item")
                                                 : fact.title;
        if (mapping && fact.supported) {
            TrackerImportProgressTarget target{mapping->canonical.canonicalMediaId,
                                               mapping->canonical.historyKind,
                                               mapping->canonical.historyId,
                                               static_cast<double>(fact.progress) / 100.0,
                                               fact.completed};
            item.exactProgressTarget = target;
            if (m_importOwner)
                item.localAtPreview = m_importOwner->currentProgress(*mapping, target);
        }
        draft.items.append(std::move(item));
        if (fact.occurredAtMs > 0) {
            evidence.append({fact.remote,
                             eventId(fact.remote.remoteMediaId, fact.occurredAtMs,
                                     fact.eventKind == TrackerImportedEventKind::Completion
                                         ? QStringLiteral("completion")
                                         : QStringLiteral("activity")),
                             snapshotId, fact.occurredAtMs, fact.eventKind,
                             fact.fingerprint});
        }
    }

    QString error;
    const auto batch = m_imports->createPreview(draft, &error);
    if (!batch) {
        m_pullingAccounts.remove(state->connection.remoteAccountId);
        if (m_syncCenter)
            m_syncCenter->refresh();
        return;
    }
    m_pendingEvidence.insert(batch->batchId, evidence);
    if (state->initial) {
        m_pullingAccounts.remove(state->connection.remoteAccountId);
        if (m_syncCenter)
            m_syncCenter->refresh();
        return;
    }

    const bool automatic = m_settings->pullAutomatically(
        TrackerProviderId::Simkl, state->connection.remoteAccountId, true);
    m_imports->applyRoutineSafeAsync(batch->batchId, m_importOwner, automatic,
        guarded(QPointer<SimklSyncRuntime>(this),
            [this, state](bool succeeded, const QString &) {
            if (succeeded) {
                m_settings->recordSuccessfulSync(TrackerProviderId::Simkl,
                    state->connection.remoteAccountId, QDateTime::currentMSecsSinceEpoch());
            }
            m_pullingAccounts.remove(state->connection.remoteAccountId);
            settleConfirmedEvidence();
            dispatchNextDelivery();
            if (m_syncCenter)
                m_syncCenter->refresh();
        }));
}

void SimklSyncRuntime::settleConfirmedEvidence()
{
    if (!m_imports || !m_mappings || !m_historyEvidence)
        return;
    const QStringList batchIds = m_pendingEvidence.keys();
    for (const QString &batchId : batchIds) {
        const auto batch = m_imports->batch(batchId);
        if (!batch || !batch->confirmed || !batch->cursorCommitted)
            continue;
        for (const PendingEvidence &pending : m_pendingEvidence.value(batchId)) {
            const auto mapping = m_mappings->mapping(pending.remote);
            if (!mapping)
                continue;
            TrackerImportedHistoryEvidence evidence;
            evidence.mapping = *mapping;
            evidence.eventKind = pending.eventKind;
            evidence.providerEventId = pending.providerEventId;
            evidence.importSnapshotId = pending.snapshotId;
            evidence.occurredAtMs = pending.occurredAtMs;
            evidence.timestampSource = TrackerEvidenceTimestampSource::ProviderEvent;
            evidence.timestampPrecision = TrackerEvidenceTimestampPrecision::ExactMillisecond;
            evidence.eventPayloadFingerprint = pending.fingerprint;
            QString ignored;
            m_historyEvidence->record(evidence, &ignored);
        }
        m_pendingEvidence.remove(batchId);
    }
}

void SimklSyncRuntime::dispatchNextDelivery()
{
    if (m_deliveryInFlight || !m_api || !m_delivery || !m_deliverySource
        || !m_settings || !m_settings->globalSettings().trackerSyncEnabled
        || !m_settings->globalSettings().backgroundDelivery)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_nextDeliveryAtMs) {
        const qint64 delay = qBound<qint64>(0, m_nextDeliveryAtMs - now,
                                             kMaximumDispatchDelayMs);
        QTimer::singleShot(static_cast<int>(delay), this,
                           &SimklSyncRuntime::dispatchNextDelivery);
        return;
    }
    const QList<TrackerDeliveryOperation> ready = m_delivery->readyOperations(
        now);
    if (ready.isEmpty()) {
        if (m_syncCenter)
            m_syncCenter->refresh();
        return;
    }
    const TrackerDeliveryOperation operation = ready.first();
    QString error;
    if (!m_delivery->markDelivering(operation.operationId, m_deliverySource,
                                   QDateTime::currentMSecsSinceEpoch(), &error)) {
        if (m_syncCenter)
            m_syncCenter->refresh();
        return;
    }
    m_deliveryInFlight = true;
    sendDelivery(operation);
}

void SimklSyncRuntime::sendDelivery(const TrackerDeliveryOperation &operation)
{
    const QJsonObject body = deliveryBody(operation);
    if (body.isEmpty()) {
        finishDelivery(operation.operationId,
                       TrackerDeliveryAttemptResult::FailedTerminal,
                       TrackerDeliveryReason::UnsupportedAction);
        return;
    }
    const QString path = operation.fact.kind == TrackerDeliveryFactKind::Completion
        ? QStringLiteral("/sync/history") : QStringLiteral("/scrobble/pause");
    m_api->post(path, {}, QJsonDocument(body),
        guarded(QPointer<SimklSyncRuntime>(this),
            [this, operation](const SimklApiResult &result) {
            const QJsonObject notFound = result.document.object()
                                             .value(QStringLiteral("not_found")).toObject();
            const bool refusedItem = !notFound.value(QStringLiteral("movies"))
                                          .toArray().isEmpty()
                || !notFound.value(QStringLiteral("shows")).toArray().isEmpty()
                || !notFound.value(QStringLiteral("episodes")).toArray().isEmpty();
            if (result.succeeded() && !refusedItem) {
                finishDelivery(operation.operationId,
                               TrackerDeliveryAttemptResult::Succeeded,
                               TrackerDeliveryReason::None);
            } else if (result.statusCode == 429) {
                finishDelivery(operation.operationId,
                               TrackerDeliveryAttemptResult::RateLimitedKnownNotApplied,
                               TrackerDeliveryReason::ProviderRateLimited,
                               QDateTime::currentMSecsSinceEpoch()
                                   + qMax<qint64>(result.retryAfterMs, 2000));
            } else if (result.statusCode == 401 || result.statusCode == 403) {
                finishDelivery(operation.operationId,
                               TrackerDeliveryAttemptResult::NeedsAttention,
                               TrackerDeliveryReason::AuthenticationRequired);
            } else if (result.statusCode == 409) {
                // 409 is not a documented SIMKL write outcome. The request may
                // or may not have been applied, so it is reconciled through
                // readback instead of being reported as delivered.
                finishDelivery(operation.operationId,
                               TrackerDeliveryAttemptResult::UnknownOutcome,
                               TrackerDeliveryReason::AcknowledgementLost);
            } else if (result.networkFailure || result.payloadTooLarge
                       || result.statusCode >= 500) {
                finishDelivery(operation.operationId,
                               TrackerDeliveryAttemptResult::UnknownOutcome,
                               TrackerDeliveryReason::AcknowledgementLost);
            } else {
                finishDelivery(operation.operationId,
                               TrackerDeliveryAttemptResult::FailedTerminal,
                               TrackerDeliveryReason::TerminalProviderRefusal);
            }
        }));
}

void SimklSyncRuntime::finishDelivery(const QString &operationId,
                                      TrackerDeliveryAttemptResult result,
                                      TrackerDeliveryReason reason,
                                      qint64 retryAfterMs)
{
    QString ignored;
    m_delivery->recordAttemptResult(operationId, result,
                                    QDateTime::currentMSecsSinceEpoch(),
                                    retryAfterMs, reason, &ignored);
    m_deliveryInFlight = false;
    m_nextDeliveryAtMs = QDateTime::currentMSecsSinceEpoch() + kDeliveryDispatchCooldownMs;
    if (result == TrackerDeliveryAttemptResult::UnknownOutcome) {
        QTimer::singleShot(0, this, [this, operationId] {
            reconcileUnknownDelivery(operationId);
        });
    }
    if (m_syncCenter)
        m_syncCenter->refresh();
    QTimer::singleShot(kDeliveryDispatchCooldownMs, this,
                       &SimklSyncRuntime::dispatchNextDelivery);
}

void SimklSyncRuntime::reconcileUnknownDelivery(const QString &operationId)
{
    const auto operation = m_delivery ? m_delivery->operation(operationId) : std::nullopt;
    if (!operation || operation->state != TrackerDeliveryState::UnknownOutcome
        || !m_connections)
        return;
    const auto connection = m_connections->connection(operation->providerId);
    if (!connection)
        return;
    readExportSnapshotAsync(*connection, {operation->fact},
        guarded(QPointer<SimklSyncRuntime>(this),
            [this, operationId](
                const std::optional<TrackerRemoteDeliverySnapshot> &snapshot) {
            if (!snapshot || snapshot->items.size() != 1) {
                // The outcome stays journaled as unknown; it is never resent
                // and never reported green without provider evidence.
                if (m_syncCenter)
                    m_syncCenter->refresh();
                return;
            }
            const TrackerRemoteDeliveryState state = snapshot->items.first();
            TrackerDeliveryReadback readback = TrackerDeliveryReadback::Indeterminate;
            if (state.exactlyMatchesIntendedState)
                readback = TrackerDeliveryReadback::ExactPresent;
            else if (!state.present)
                readback = TrackerDeliveryReadback::Absent;
            else
                readback = TrackerDeliveryReadback::PresentDifferent;
            QString ignored;
            m_delivery->reconcileUnknown(operationId, readback,
                                         QDateTime::currentMSecsSinceEpoch(), &ignored);
            if (m_syncCenter)
                m_syncCenter->refresh();
            dispatchNextDelivery();
        }));
}

void SimklSyncRuntime::readExportSnapshotAsync(
    const TrackerConnection &connection,
    const QList<TrackerDeliveryFact> &facts,
    ExportSnapshotCompletion completion)
{
    if (!m_api || !m_api->available() || connection.providerId != TrackerProviderId::Simkl) {
        completion(std::nullopt);
        return;
    }
    const auto state = std::make_shared<SnapshotState>();
    state->connection = connection;
    state->facts = facts;
    state->completion = std::move(completion);
    QUrlQuery allQuery;
    allQuery.addQueryItem(QStringLiteral("extended"), QStringLiteral("full"));
    allQuery.addQueryItem(QStringLiteral("episode_watched_at"), QStringLiteral("yes"));
    allQuery.addQueryItem(QStringLiteral("include_all_episodes"), QStringLiteral("original"));
    m_api->get(QStringLiteral("/sync/all-items"), allQuery,
        guarded(QPointer<SimklSyncRuntime>(this),
            [this, state](const SimklApiResult &result) {
            if (!result.succeeded()) {
                state->completion(std::nullopt);
                return;
            }
            state->allItems = result.document;
            m_api->get(QStringLiteral("/sync/playback"), {},
                guarded(QPointer<SimklSyncRuntime>(this),
                    [this, state](const SimklApiResult &result) {
                    if (!result.succeeded()) {
                        state->completion(std::nullopt);
                        return;
                    }
                    state->playback = result.document;
                    m_api->get(QStringLiteral("/sync/activities"), {},
                        guarded(QPointer<SimklSyncRuntime>(this),
                            [this, state](const SimklApiResult &result) {
                            if (!result.succeeded() || !result.document.isObject()) {
                                state->completion(std::nullopt);
                                return;
                            }
                            deliverExportSnapshot(state, result.document);
                        }));
                }));
        }));
}

void SimklSyncRuntime::deliverExportSnapshot(const std::shared_ptr<SnapshotState> &state,
                                             const QJsonDocument &activities)
{
    QList<ParsedRemoteFact> remoteFacts = parseAllItems(
        state->allItems, TrackerProviderId::Simkl, state->connection.remoteAccountId);
    remoteFacts.append(parsePlaybacks(state->playback, TrackerProviderId::Simkl,
                                      state->connection.remoteAccountId));
    QHash<QString, ParsedRemoteFact> completions;
    QHash<QString, ParsedRemoteFact> progress;
    for (const ParsedRemoteFact &fact : remoteFacts) {
        if (fact.completed)
            completions.insert(fact.remote.remoteMediaId, fact);
        else if (fact.progress > 0)
            progress.insert(fact.remote.remoteMediaId, fact);
    }
    const QByteArray material = state->allItems.toJson(QJsonDocument::Compact)
        + state->playback.toJson(QJsonDocument::Compact);
    TrackerRemoteDeliverySnapshot snapshot;
    snapshot.providerId = TrackerProviderId::Simkl;
    snapshot.remoteAccountId = state->connection.remoteAccountId;
    snapshot.connectionGeneration = state->connection.connectionGeneration;
    snapshot.snapshotId = QStringLiteral("simkl-state-") + digest(material);
    snapshot.observedAtMs = isoMs(activities.object()
                                      .value(QStringLiteral("all")).toString());
    if (snapshot.observedAtMs <= 0)
        snapshot.observedAtMs = 1;
    snapshot.completeForMappedItems = true;
    for (const TrackerDeliveryFact &fact : state->facts) {
        std::optional<TrackerTitleMapping> mapping;
        for (const TrackerTitleMapping &candidate : m_mappings->mappings()) {
            if (candidate.remote.providerId == TrackerProviderId::Simkl
                && candidate.remote.remoteAccountId == state->connection.remoteAccountId
                && candidate.canonical.canonicalMediaId == fact.canonicalMediaId
                && candidate.canonical.historyKind == fact.historyKind
                && candidate.canonical.historyId == fact.historyId) {
                if (mapping) {
                    mapping.reset();
                    break;
                }
                mapping = candidate;
            }
        }
        if (!mapping)
            continue;
        const QHash<QString, ParsedRemoteFact> &states =
            fact.kind == TrackerDeliveryFactKind::Completion ? completions : progress;
        const auto found = states.constFind(mapping->remote.remoteMediaId);
        TrackerRemoteDeliveryState remoteState;
        remoteState.remoteMediaId = mapping->remote.remoteMediaId;
        remoteState.factKind = fact.kind;
        remoteState.sourceEventId = fact.sourceEventId;
        remoteState.present = found != states.cend();
        remoteState.exactlyMatchesIntendedState = remoteState.present
            && (fact.kind == TrackerDeliveryFactKind::Completion
                || found->progress == fact.progress);
        remoteState.stateFingerprint = remoteState.present
            ? found->fingerprint : QStringLiteral("absent");
        remoteState.safeSummary = remoteState.present
            ? (fact.kind == TrackerDeliveryFactKind::Completion
                   ? QStringLiteral("Already watched on SIMKL")
                   : QStringLiteral("SIMKL progress: %1%").arg(found->progress))
            : QString();
        snapshot.items.append(remoteState);
    }
    state->completion(snapshot);
}
