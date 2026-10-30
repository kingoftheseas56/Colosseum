#include "AniListSyncRuntime.h"

#include "AniListConnectionController.h"
#include "TrackerCanonicalDeliverySource.h"
#include "TrackerConnectionStore.h"
#include "TrackerMappingStore.h"
#include "TrackerProgressImportOwner.h"
#include "TrackerSyncCenterModel.h"
#include "TrackerSyncSettingsStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

constexpr int kDeliveryDispatchCooldownMs = 1100;
constexpr qint64 kMaximumDispatchDelayMs = 60LL * 60 * 1000;
constexpr qint64 kMinimumRateLimitDelayMs = 2000;
constexpr qint64 kTransientLookupDelayMs = 30 * 1000;
constexpr int kListChunkSize = 100;

template <typename Callback>
auto guarded(const QPointer<AniListSyncRuntime> &self, Callback &&callback)
{
    return [self, callback = std::forward<Callback>(callback)](
               auto &&...arguments) mutable {
        if (!self)
            return;
        callback(std::forward<decltype(arguments)>(arguments)...);
    };
}

QString digest(const QByteArray &value)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(value, QCryptographicHash::Sha256)
            .toHex());
}

// A Colosseum fact's position inside a MAL-identified title. Anime counts
// episodes and manga counts chapters; nothing here can express a volume.
struct CanonicalUnit {
    AniListMediaKind kind = AniListMediaKind::Anime;
    QString malId;
    int unit = 0;
};

// The hard domain gate. Anime: any fact the shared source files under the
// Anime domain. Manga: chapter history kinds only. `tankoban` is folded into
// the Manga domain by the shared source (and still syncs to MAL), so it is
// refused here, in the AniList-specific path.
std::optional<CanonicalUnit> canonicalUnit(const TrackerDeliveryFact &fact)
{
    if (fact.mediaDomain == TrackerMediaDomain::Anime) {
        static const QRegularExpression anime(
            QStringLiteral("^mal:([1-9][0-9]*):([1-9][0-9]*)$"));
        const QRegularExpressionMatch match = anime.match(fact.historyId);
        if (!match.hasMatch())
            return std::nullopt;
        return CanonicalUnit{AniListMediaKind::Anime, match.captured(1),
                             match.captured(2).toInt()};
    }
    if (fact.mediaDomain == TrackerMediaDomain::Manga
        && (fact.historyKind == QLatin1String("manga")
            || fact.historyKind == QLatin1String("manga_chapter"))) {
        static const QRegularExpression manga(
            QStringLiteral("^mal:([1-9][0-9]*):(chapter|ch):([1-9][0-9]*)$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = manga.match(fact.historyId);
        if (!match.hasMatch())
            return std::nullopt;
        return CanonicalUnit{AniListMediaKind::Manga, match.captured(1),
                             match.captured(3).toInt()};
    }
    return std::nullopt;
}

QString canonicalSeriesId(const AniListListItem &item)
{
    return QStringLiteral("mal:") + item.malId;
}

bool candidateKindMatches(const TrackerCanonicalTitleCandidate &candidate,
                          AniListMediaKind kind)
{
    if (kind == AniListMediaKind::Anime) {
        return candidate.historyKind == QLatin1String("anime")
            || candidate.historyKind.startsWith(QLatin1String("anime_"))
            || candidate.historyKind == QLatin1String("series");
    }
    return candidate.historyKind == QLatin1String("manga")
        || candidate.historyKind == QLatin1String("manga_chapter");
}

std::optional<TrackerCanonicalTitleCandidate> uniqueSeriesCandidate(
    const QList<TrackerCanonicalTitleCandidate> &candidates,
    const AniListListItem &item)
{
    std::optional<TrackerCanonicalTitleCandidate> result;
    if (item.malId.isEmpty())
        return result;
    const QString expectedId = canonicalSeriesId(item);
    for (const TrackerCanonicalTitleCandidate &candidate : candidates) {
        if (!candidateKindMatches(candidate, item.kind)
            || candidate.historyId.compare(expectedId,
                                           Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (result
            && (result->canonicalMediaId != candidate.canonicalMediaId
                || result->historyKind != candidate.historyKind
                || result->historyId != candidate.historyId)) {
            return std::nullopt;
        }
        result = candidate;
    }
    return result;
}

QString unitName(AniListMediaKind kind)
{
    return kind == AniListMediaKind::Anime ? QStringLiteral("episodes")
                                           : QStringLiteral("chapters");
}

QString listDisplayTitle(const AniListListItem &item)
{
    const QString title =
        item.title.isEmpty() ? QStringLiteral("Untitled AniList item")
                             : item.title;
    return QStringLiteral("%1 · %2 · %3 %4")
        .arg(title,
             aniListSafeStatusLabel(item.status),
             QString::number(item.progress),
             unitName(item.kind))
        .left(500);
}

QString listItemFingerprint(const AniListListItem &item)
{
    const QByteArray material =
        (aniListListKey(item.kind, item.aniListId)
         + QChar(0x1f) + item.status
         + QChar(0x1f) + QString::number(item.progress)
         + QChar(0x1f) + QString::number(item.totalUnits)
         + QChar(0x1f) + QString::number(item.updatedAtMs))
            .toUtf8();
    return digest(material);
}

bool sameListEntry(const AniListListItem &left, const AniListListItem &right)
{
    return left.status == right.status && left.progress == right.progress
        && left.totalUnits == right.totalUnits
        && left.malId == right.malId;
}

// One AniList write per delivery: progress is the unit number, and the entry
// becomes COMPLETED only when that unit is the known last one. A partial
// in-item position is refused before any request is built.
std::optional<AniListMutation> mutationForDelivery(
    const AniListRemoteIdentity &identity,
    const TrackerDeliveryFact &fact,
    int totalUnits)
{
    if (identity.listAggregate || identity.unitNumber <= 0
        || (fact.kind == TrackerDeliveryFactKind::Progress
            && fact.progress < 100)) {
        return std::nullopt;
    }
    AniListMutation mutation;
    mutation.intendedProgress = identity.unitNumber;
    mutation.intendedCompleted =
        totalUnits > 0 && identity.unitNumber >= totalUnits;
    const auto body = aniListSaveEntryRequest(
        identity.aniListId, mutation.intendedProgress,
        mutation.intendedCompleted ? QStringLiteral("COMPLETED")
                                   : QStringLiteral("CURRENT"));
    if (!body)
        return std::nullopt;
    mutation.body = *body;
    return mutation;
}

QString entryFingerprint(const AniListEntryState &state)
{
    return digest((state.aniListId + QChar(0x1f) + state.status
                   + QChar(0x1f) + QString::number(state.progress)
                   + QChar(0x1f) + QString::number(state.updatedAtMs))
                      .toUtf8());
}

bool documentHasGraphQlErrors(const QJsonDocument &document)
{
    return document.isObject()
        && document.object().value(QStringLiteral("errors")).isArray();
}

} // namespace

struct AniListSyncRuntime::ListFetchState {
    TrackerConnection connection;
    int phase = 0;
    int chunk = 1;
    QList<AniListListItem> items;
    QHash<QString, AniListListItem> seen;
    QByteArray snapshotMaterial;
    ListFetchCompletion completion;
};

struct AniListSyncRuntime::PullState {
    TrackerConnection connection;
    QString baseCursor;
    bool initial = true;
    bool explicitRequest = false;
};

struct AniListSyncRuntime::SnapshotState {
    TrackerConnection connection;
    QList<TrackerDeliveryFact> facts;
    int index = 0;
    QByteArray material;
    TrackerRemoteDeliverySnapshot snapshot;
    ExportSnapshotCompletion completion;
};

AniListSyncRuntime::AniListSyncRuntime(
    TrackerConnectionStore *connections,
    TrackerMappingStore *mappings,
    TrackerImportStore *imports,
    TrackerProgressImportOwner *importOwner,
    TrackerDeliveryStore *delivery,
    TrackerCanonicalDeliverySource *deliverySource,
    TrackerSyncSettingsStore *settings,
    TrackerSyncCenterModel *syncCenter,
    AniListApiClient *api,
    AniListConnectionController *controller,
    QObject *parent)
    : QObject(parent),
      m_connections(connections),
      m_mappings(mappings),
      m_imports(imports),
      m_importOwner(importOwner),
      m_delivery(delivery),
      m_deliverySource(deliverySource),
      m_settings(settings),
      m_syncCenter(syncCenter),
      m_api(api),
      m_controller(controller)
{
    if (m_syncCenter) {
        connect(m_syncCenter, &TrackerSyncCenterModel::modelChanged,
                this, [this] { dispatchNextDelivery(); });
    }
}

bool AniListSyncRuntime::available() const
{
    return m_api && m_api->available();
}

void AniListSyncRuntime::start()
{
    if (!available() || !m_connections || !m_settings)
        return;
    const auto connection =
        m_connections->connection(TrackerProviderId::AniList);
    if (connection
        && connection->state == TrackerConnectionState::Connected) {
        ensureExactDeliveryMappings(*connection);
        if (m_delivery && m_deliverySource)
            m_delivery->recoverSourceGap(m_deliverySource);
    }

    const TrackerGlobalSyncSettings global =
        m_settings->globalSettings();
    if (global.trackerSyncEnabled && global.checkOnLaunch)
        QTimer::singleShot(0, this, [this] { pullAniList(false); });
    if (global.trackerSyncEnabled && global.backgroundDelivery)
        QTimer::singleShot(0, this,
                           &AniListSyncRuntime::dispatchNextDelivery);
}

void AniListSyncRuntime::syncAll(
    const QStringList &providerKeys, quint64)
{
    if (!providerKeys.contains(QStringLiteral("anilist")))
        return;
    m_unmappedMedia.clear();
    m_unmappedFacts.clear();
    pullAniList(true);
}

void AniListSyncRuntime::connectionEstablished(
    const QString &providerKey)
{
    if (providerKey != QLatin1String("anilist"))
        return;
    // A fresh sign-in is the way out of "reconnect required".
    m_authenticationBlocked = false;
    m_unmappedMedia.clear();
    m_unmappedFacts.clear();
    const auto connection =
        m_connections
        ? m_connections->connection(TrackerProviderId::AniList)
        : std::nullopt;
    if (connection) {
        ensureExactDeliveryMappings(*connection);
        if (m_delivery && m_deliverySource)
            m_delivery->recoverSourceGap(m_deliverySource);
    }
    pullAniList(true);
    dispatchNextDelivery();
}

void AniListSyncRuntime::noteAuthenticationFailure()
{
    m_authenticationBlocked = true;
    if (m_controller)
        m_controller->noteAuthenticationFailure();
    if (m_syncCenter)
        m_syncCenter->refresh();
}

AniListSyncRuntime::StepOutcome AniListSyncRuntime::classifyFailure(
    const AniListApiResult &result, qint64 *retryAfterMs)
{
    if (result.error == AniListTransportError::AuthenticationRequired
        || result.statusCode == 401 || result.statusCode == 403) {
        return StepOutcome::AuthenticationRequired;
    }
    if (result.error == AniListTransportError::RateLimited
        || result.statusCode == 429) {
        if (retryAfterMs)
            *retryAfterMs = result.retryAfterMs;
        return StepOutcome::RateLimited;
    }
    if (result.networkFailure || result.payloadTooLarge
        || result.error == AniListTransportError::Timeout
        || result.error == AniListTransportError::NetworkFailure
        || result.error == AniListTransportError::ServerFailure
        || result.statusCode >= 500) {
        return StepOutcome::Transient;
    }
    // Any other client-side refusal fails closed.
    return StepOutcome::Unmapped;
}

// ---------------------------------------------------------------- import

void AniListSyncRuntime::pullAniList(bool explicitRequest)
{
    if (!available() || m_authenticationBlocked
        || (m_controller && m_controller->reconnectRequired())
        || !m_imports || !m_connections || !m_settings
        || !m_settings->globalSettings().trackerSyncEnabled) {
        return;
    }
    const auto connection =
        m_connections->connection(TrackerProviderId::AniList);
    if (!connection
        || connection->state != TrackerConnectionState::Connected
        || m_pullingAccounts.contains(connection->remoteAccountId)) {
        return;
    }

    m_pullingAccounts.insert(connection->remoteAccountId);
    const auto state = std::make_shared<PullState>();
    state->connection = *connection;
    state->explicitRequest = explicitRequest;
    const auto cursor = m_imports->confirmedCursor(
        TrackerProviderId::AniList, connection->remoteAccountId);
    state->initial = !cursor.has_value();
    state->baseCursor = cursor.value_or(QString());

    fetchLists(*connection,
        guarded(QPointer<AniListSyncRuntime>(this),
            [this, state](
                bool succeeded,
                const QList<AniListListItem> &items,
                const QByteArray &snapshotMaterial) {
            if (!succeeded) {
                m_pullingAccounts.remove(
                    state->connection.remoteAccountId);
                if (m_syncCenter)
                    m_syncCenter->refresh();
                return;
            }
            finishPull(state, items, snapshotMaterial);
        }));
}

void AniListSyncRuntime::fetchLists(
    const TrackerConnection &connection,
    ListFetchCompletion completion)
{
    if (!available()
        || connection.providerId != TrackerProviderId::AniList
        || connection.state != TrackerConnectionState::Connected) {
        completion(false, {}, {});
        return;
    }
    const auto state = std::make_shared<ListFetchState>();
    state->connection = connection;
    state->completion = std::move(completion);
    fetchListPage(state);
}

void AniListSyncRuntime::fetchListPage(
    const std::shared_ptr<ListFetchState> &state)
{
    if (state->phase >= 2) {
        state->completion(true, state->items,
                          state->snapshotMaterial);
        return;
    }
    const AniListMediaKind kind =
        state->phase == 0 ? AniListMediaKind::Anime
                          : AniListMediaKind::Manga;
    const auto body = aniListMediaListCollectionRequest(
        state->connection.remoteAccountId, kind, state->chunk,
        kListChunkSize);
    if (!body) {
        state->completion(false, {}, {});
        return;
    }

    m_api->post(*body,
        guarded(QPointer<AniListSyncRuntime>(this),
            [this, state, kind](const AniListApiResult &result) {
            if (!result.succeeded()) {
                qint64 ignored = 0;
                if (classifyFailure(result, &ignored)
                    == StepOutcome::AuthenticationRequired) {
                    noteAuthenticationFailure();
                }
                state->completion(false, {}, {});
                return;
            }
            const auto page =
                aniListParseMediaListCollection(result.document, kind);
            if (!page) {
                state->completion(false, {}, {});
                return;
            }
            for (const AniListListItem &item : page->items) {
                // Custom lists repeat an entry; identical repeats collapse,
                // conflicting ones fail the whole pull closed.
                const QString key =
                    aniListListKey(item.kind, item.aniListId);
                const auto seen = state->seen.constFind(key);
                if (seen != state->seen.cend()) {
                    if (!sameListEntry(*seen, item)) {
                        state->completion(false, {}, {});
                        return;
                    }
                    continue;
                }
                state->seen.insert(key, item);
                state->items.append(item);
            }
            state->snapshotMaterial +=
                result.document.toJson(QJsonDocument::Compact);

            if (page->hasNextChunk) {
                if (page->items.isEmpty()) {
                    state->completion(false, {}, {});
                    return;
                }
                ++state->chunk;
            } else {
                ++state->phase;
                state->chunk = 1;
            }
            fetchListPage(state);
        }));
}

void AniListSyncRuntime::finishPull(
    const std::shared_ptr<PullState> &state,
    const QList<AniListListItem> &items,
    const QByteArray &snapshotMaterial)
{
    for (const AniListListItem &item : items) {
        if (item.malId.isEmpty())
            continue;
        const QString key = aniListListKey(item.kind, item.malId);
        if (!m_media.contains(key)) {
            m_media.insert(
                key,
                AniListMediaRef{item.kind, item.aniListId, item.malId,
                                item.title, item.totalUnits});
        }
    }

    TrackerImportBatchDraft draft;
    draft.providerId = TrackerProviderId::AniList;
    draft.remoteAccountId =
        state->connection.remoteAccountId;
    draft.connectionGeneration =
        state->connection.connectionGeneration;
    draft.snapshotId =
        QStringLiteral("anilist-") + digest(snapshotMaterial);
    draft.proposedCursor =
        QDateTime::currentDateTimeUtc()
            .toString(Qt::ISODateWithMs);
    draft.initialImport = state->initial;
    draft.pageComplete = true;
    draft.baseCursor = state->baseCursor;

    const QList<TrackerCanonicalTitleCandidate> candidates =
        m_importOwner ? m_importOwner->userCandidates()
                      : QList<TrackerCanonicalTitleCandidate>{};

    for (const AniListListItem &remoteItem : items) {
        TrackerRemoteMediaKey remote{
            TrackerProviderId::AniList,
            state->connection.remoteAccountId,
            AniListRemoteIdentity{remoteItem.kind, remoteItem.aniListId,
                                  0, true}
                .remoteMediaId()};
        auto mapping = m_mappings
            ? m_mappings->mapping(remote)
            : std::nullopt;
        if (!mapping && m_mappings) {
            const auto candidate =
                uniqueSeriesCandidate(candidates, remoteItem);
            if (candidate) {
                QString ignored;
                if (m_mappings->upsert(
                        remote, *candidate,
                        TrackerMappingProvenance::ExactProviderIdentity,
                        &ignored)) {
                    mapping = m_mappings->mapping(remote);
                }
            }
        }

        TrackerImportRemoteItem item;
        item.providerItemId =
            QStringLiteral("anilist-list-")
            + listItemFingerprint(remoteItem);
        item.remote = remote;
        item.mapping = mapping;
        item.progress = remoteItem.progress;
        item.completed =
            remoteItem.status == QLatin1String("COMPLETED");
        item.supported = true;
        item.displayTitle = listDisplayTitle(remoteItem);
        // Deliberately no exactProgressTarget: an AniList episode/chapter
        // count is a list aggregate, not an exact Colosseum Continue
        // position, so import reviews it without mutating local progress.
        draft.items.append(std::move(item));
    }

    QString error;
    const auto batch =
        m_imports->createPreview(draft, &error);
    if (!batch) {
        m_pullingAccounts.remove(
            state->connection.remoteAccountId);
        if (m_syncCenter)
            m_syncCenter->refresh();
        return;
    }

    ensureExactDeliveryMappings(state->connection);
    if (m_delivery && m_deliverySource)
        m_delivery->recoverSourceGap(m_deliverySource);

    if (state->initial) {
        m_pullingAccounts.remove(
            state->connection.remoteAccountId);
        if (m_syncCenter)
            m_syncCenter->refresh();
        dispatchNextDelivery();
        return;
    }

    const bool automatic = m_settings->pullAutomatically(
        TrackerProviderId::AniList,
        state->connection.remoteAccountId,
        true);
    m_imports->applyRoutineSafeAsync(
        batch->batchId, m_importOwner, automatic,
        guarded(QPointer<AniListSyncRuntime>(this),
            [this, state](bool succeeded,
                          const QString &) {
            if (succeeded) {
                m_settings->recordSuccessfulSync(
                    TrackerProviderId::AniList,
                    state->connection.remoteAccountId,
                    QDateTime::currentMSecsSinceEpoch());
            }
            m_pullingAccounts.remove(
                state->connection.remoteAccountId);
            if (m_syncCenter)
                m_syncCenter->refresh();
            dispatchNextDelivery();
        }));
}

// --------------------------------------------------------------- mapping

void AniListSyncRuntime::lookupMedia(
    AniListMediaKind kind,
    const QString &malId,
    MediaCompletion completion)
{
    const QString key = aniListListKey(kind, malId);
    const auto cached = m_media.constFind(key);
    if (cached != m_media.cend()) {
        completion(MediaStep{StepOutcome::Done, *cached, 0});
        return;
    }
    const auto body = aniListMediaByMalIdRequest(malId, kind);
    if (!body || !available()) {
        completion(MediaStep{StepOutcome::Unmapped, {}, 0});
        return;
    }

    m_api->post(*body,
        guarded(QPointer<AniListSyncRuntime>(this),
            [this, kind, malId, key,
             completion = std::move(completion)](
                const AniListApiResult &result) {
            MediaStep step;
            if (!result.succeeded()) {
                // AniList answers an unknown idMal with a GraphQL 404.
                if (aniListResponseIsNotFound(result.document)) {
                    step.outcome = StepOutcome::Unmapped;
                } else {
                    step.outcome =
                        classifyFailure(result, &step.retryAfterMs);
                }
                completion(step);
                return;
            }
            const auto ref =
                aniListParseMediaLookup(result.document, kind);
            // Zero, malformed or foreign results fail closed; the record
            // must name exactly the MAL id that was asked for.
            if (!ref || ref->malId != malId) {
                step.outcome = StepOutcome::Unmapped;
                completion(step);
                return;
            }
            m_media.insert(key, *ref);
            step.outcome = StepOutcome::Done;
            step.media = *ref;
            completion(step);
        }));
}

std::optional<TrackerTitleMapping> AniListSyncRuntime::uniqueMappingFor(
    const TrackerConnection &connection,
    const TrackerDeliveryFact &fact,
    bool *ambiguous) const
{
    if (ambiguous)
        *ambiguous = false;
    std::optional<TrackerTitleMapping> result;
    if (!m_mappings)
        return result;
    for (const TrackerTitleMapping &candidate : m_mappings->mappings()) {
        if (candidate.remote.providerId != TrackerProviderId::AniList
            || candidate.remote.remoteAccountId
                != connection.remoteAccountId
            || candidate.canonical.canonicalMediaId
                != fact.canonicalMediaId
            || candidate.canonical.historyKind != fact.historyKind
            || candidate.canonical.historyId != fact.historyId) {
            continue;
        }
        if (result) {
            if (ambiguous)
                *ambiguous = true;
            return std::nullopt;
        }
        result = candidate;
    }
    return result;
}

void AniListSyncRuntime::mapFact(
    const TrackerConnection &connection,
    const TrackerDeliveryFact &fact,
    MapCompletion completion)
{
    const auto unit = canonicalUnit(fact);
    if (!m_mappings || !unit
        || connection.providerId != TrackerProviderId::AniList
        || connection.state != TrackerConnectionState::Connected) {
        completion(StepOutcome::Unmapped);
        return;
    }

    bool ambiguous = false;
    const auto existing = uniqueMappingFor(connection, fact, &ambiguous);
    if (ambiguous) {
        completion(StepOutcome::Unmapped);
        return;
    }
    if (existing) {
        const auto identity =
            aniListRemoteIdentity(existing->remote.remoteMediaId);
        completion(identity && !identity->listAggregate
                       && identity->kind == unit->kind
                       && identity->unitNumber == unit->unit
                   ? StepOutcome::Done
                   : StepOutcome::Unmapped);
        return;
    }

    lookupMedia(unit->kind, unit->malId,
        guarded(QPointer<AniListSyncRuntime>(this),
            [this, connection, fact, unit = *unit,
             completion = std::move(completion)](
                const MediaStep &step) {
            if (step.outcome == StepOutcome::AuthenticationRequired) {
                noteAuthenticationFailure();
                completion(step.outcome);
                return;
            }
            if (step.outcome == StepOutcome::RateLimited) {
                m_nextResolveAtMs =
                    QDateTime::currentMSecsSinceEpoch()
                    + qMax<qint64>(step.retryAfterMs,
                                   kMinimumRateLimitDelayMs);
                completion(step.outcome);
                return;
            }
            if (step.outcome == StepOutcome::Transient) {
                m_nextResolveAtMs =
                    QDateTime::currentMSecsSinceEpoch()
                    + kTransientLookupDelayMs;
                completion(step.outcome);
                return;
            }
            if (step.outcome != StepOutcome::Done) {
                m_unmappedMedia.insert(
                    aniListListKey(unit.kind, unit.malId));
                completion(StepOutcome::Unmapped);
                return;
            }

            // The same MAL title must not already be mapped to a different
            // AniList media for this account (a conflicting join is
            // ambiguous, not "last one wins").
            for (const TrackerTitleMapping &other : m_mappings->mappings()) {
                if (other.remote.providerId != TrackerProviderId::AniList
                    || other.remote.remoteAccountId
                        != connection.remoteAccountId) {
                    continue;
                }
                const auto otherIdentity =
                    aniListRemoteIdentity(other.remote.remoteMediaId);
                const bool sameTitle = other.canonical.historyId
                        .startsWith(QStringLiteral("mal:") + unit.malId
                                    + QLatin1Char(':'));
                if (otherIdentity && sameTitle
                    && otherIdentity->kind == unit.kind
                    && otherIdentity->aniListId != step.media.aniListId) {
                    m_unmappedMedia.insert(
                        aniListListKey(unit.kind, unit.malId));
                    completion(StepOutcome::Unmapped);
                    return;
                }
            }

            TrackerRemoteMediaKey remote{
                TrackerProviderId::AniList,
                connection.remoteAccountId,
                AniListRemoteIdentity{unit.kind, step.media.aniListId,
                                      unit.unit, false}
                    .remoteMediaId()};
            TrackerCanonicalTitleCandidate candidate{
                fact.canonicalMediaId,
                fact.historyKind,
                fact.historyId,
                fact.canonicalMediaId,
                {}};
            QString ignored;
            completion(m_mappings->upsert(
                           remote, candidate,
                           TrackerMappingProvenance::ExactProviderIdentity,
                           &ignored)
                       ? StepOutcome::Done
                       : StepOutcome::Unmapped);
        }));
}

void AniListSyncRuntime::ensureExactDeliveryMappings(
    const TrackerConnection &connection)
{
    if (!m_mappings || !m_deliverySource || !available()
        || connection.providerId != TrackerProviderId::AniList
        || connection.state != TrackerConnectionState::Connected) {
        return;
    }

    for (const TrackerDeliveryFact &fact
         : m_deliverySource->currentCommittedFacts()) {
        if (fact.origin != TrackerDeliveryOrigin::NativeLocal)
            continue;
        // Titles without a `mal:` canonical id, and Tankoban facts, are
        // never mapped and so never sent.
        const auto unit = canonicalUnit(fact);
        if (!unit
            || m_unmappedFacts.contains(fact.canonicalMediaId)
            || m_unmappedMedia.contains(
                aniListListKey(unit->kind, unit->malId))
            || m_pendingMappingKeys.contains(fact.canonicalMediaId)) {
            continue;
        }
        bool ambiguous = false;
        if (uniqueMappingFor(connection, fact, &ambiguous) || ambiguous)
            continue;
        m_pendingMappingKeys.insert(fact.canonicalMediaId);
        m_pendingMappings.append(fact);
    }
    resolveNextPendingMapping();
}

void AniListSyncRuntime::resolveNextPendingMapping()
{
    if (m_resolvingMappings || m_pendingMappings.isEmpty())
        return;
    const auto connection = m_connections
        ? m_connections->connection(TrackerProviderId::AniList)
        : std::nullopt;
    if (!connection
        || connection->state != TrackerConnectionState::Connected
        || m_authenticationBlocked) {
        m_pendingMappings.clear();
        m_pendingMappingKeys.clear();
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_nextResolveAtMs) {
        m_resolvingMappings = true;
        const qint64 delay =
            qBound<qint64>(0, m_nextResolveAtMs - now,
                           kMaximumDispatchDelayMs);
        QTimer::singleShot(static_cast<int>(delay), this, [this] {
            m_resolvingMappings = false;
            resolveNextPendingMapping();
        });
        return;
    }

    const TrackerDeliveryFact fact = m_pendingMappings.takeFirst();
    m_pendingMappingKeys.remove(fact.canonicalMediaId);
    m_resolvingMappings = true;
    mapFact(*connection, fact,
        guarded(QPointer<AniListSyncRuntime>(this),
            [this, fact](StepOutcome outcome) {
        m_resolvingMappings = false;
        if (outcome == StepOutcome::Unmapped)
            m_unmappedFacts.insert(fact.canonicalMediaId);
        if (outcome == StepOutcome::AuthenticationRequired) {
            m_pendingMappings.clear();
            m_pendingMappingKeys.clear();
            return;
        }
        if (outcome == StepOutcome::RateLimited
            || outcome == StepOutcome::Transient) {
            // Try this fact again after the delay; nothing is lost.
            m_pendingMappingKeys.insert(fact.canonicalMediaId);
            m_pendingMappings.prepend(fact);
            resolveNextPendingMapping();
            return;
        }
        if (m_pendingMappings.isEmpty()) {
            if (m_delivery && m_deliverySource)
                m_delivery->recoverSourceGap(m_deliverySource);
            dispatchNextDelivery();
            return;
        }
        QTimer::singleShot(0, this, [this] {
            resolveNextPendingMapping();
        });
    }));
}

// -------------------------------------------------------------- delivery

void AniListSyncRuntime::dispatchNextDelivery()
{
    if (m_deliveryInFlight || !available() || !m_delivery
        || !m_deliverySource || !m_settings
        || m_authenticationBlocked
        || (m_controller && m_controller->reconnectRequired())
        || !m_settings->globalSettings().trackerSyncEnabled
        || !m_settings->globalSettings().backgroundDelivery) {
        return;
    }

    const auto connection = m_connections
        ? m_connections->connection(TrackerProviderId::AniList)
        : std::nullopt;
    if (!connection
        || connection->state != TrackerConnectionState::Connected)
        return;

    ensureExactDeliveryMappings(*connection);
    m_delivery->recoverSourceGap(m_deliverySource);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_nextDeliveryAtMs) {
        const qint64 delay =
            qBound<qint64>(0, m_nextDeliveryAtMs - now,
                           kMaximumDispatchDelayMs);
        QTimer::singleShot(
            static_cast<int>(delay), this,
            &AniListSyncRuntime::dispatchNextDelivery);
        return;
    }

    const QList<TrackerDeliveryOperation> ready =
        m_delivery->readyOperations(now);
    const auto found = std::find_if(
        ready.cbegin(), ready.cend(),
        [](const TrackerDeliveryOperation &operation) {
            return operation.providerId == TrackerProviderId::AniList;
        });
    if (found == ready.cend()) {
        if (m_syncCenter)
            m_syncCenter->refresh();
        return;
    }

    QString error;
    if (!m_delivery->markDelivering(
            found->operationId, m_deliverySource,
            now, &error)) {
        if (m_syncCenter)
            m_syncCenter->refresh();
        return;
    }
    m_deliveryInFlight = true;
    sendDelivery(*found);
}

void AniListSyncRuntime::sendDelivery(
    const TrackerDeliveryOperation &operation)
{
    const auto identity =
        aniListRemoteIdentity(operation.mapping.remote.remoteMediaId);
    const auto unit = canonicalUnit(operation.fact);
    // A Tankoban fact, a non-`mal:` title, an aggregate id, or a mapping
    // that disagrees with the fact ends here, before any request exists.
    if (!identity || identity->listAggregate || !unit
        || unit->kind != identity->kind
        || unit->unit != identity->unitNumber
        || !mutationForDelivery(*identity, operation.fact, 0)) {
        finishDelivery(
            operation.operationId,
            TrackerDeliveryAttemptResult::FailedTerminal,
            TrackerDeliveryReason::UnsupportedAction);
        return;
    }

    lookupMedia(unit->kind, unit->malId,
        guarded(QPointer<AniListSyncRuntime>(this),
            [this, operation, identity = *identity](
                const MediaStep &media) {
            switch (media.outcome) {
            case StepOutcome::AuthenticationRequired:
                noteAuthenticationFailure();
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::NeedsAttention,
                    TrackerDeliveryReason::AuthenticationRequired);
                return;
            case StepOutcome::RateLimited:
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::RateLimitedKnownNotApplied,
                    TrackerDeliveryReason::ProviderRateLimited,
                    QDateTime::currentMSecsSinceEpoch()
                        + qMax<qint64>(media.retryAfterMs,
                                       kMinimumRateLimitDelayMs));
                return;
            case StepOutcome::Transient:
                // Nothing was sent yet, so this is a plain retry.
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::RetryableKnownNotApplied,
                    TrackerDeliveryReason::ProviderRetryable);
                return;
            case StepOutcome::Unmapped:
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::FailedTerminal,
                    TrackerDeliveryReason::MappingChanged);
                return;
            case StepOutcome::Done:
                break;
            }
            if (media.media.aniListId != identity.aniListId) {
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::FailedTerminal,
                    TrackerDeliveryReason::MappingChanged);
                return;
            }
            const auto mutation = mutationForDelivery(
                identity, operation.fact, media.media.totalUnits);
            if (!mutation) {
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::FailedTerminal,
                    TrackerDeliveryReason::UnsupportedAction);
                return;
            }

            m_api->post(
                mutation->body,
                guarded(QPointer<AniListSyncRuntime>(this),
                    [this, operation, identity,
                     mutation = *mutation](
                        const AniListApiResult &result) {
                    if (result.succeeded()) {
                        const auto state =
                            aniListParseSaveEntry(result.document);
                        if (state
                            && state->aniListId == identity.aniListId
                            && state->progress
                                == mutation.intendedProgress) {
                            finishDelivery(
                                operation.operationId,
                                TrackerDeliveryAttemptResult::Succeeded,
                                TrackerDeliveryReason::None);
                        } else if (documentHasGraphQlErrors(
                                       result.document)) {
                            finishDelivery(
                                operation.operationId,
                                TrackerDeliveryAttemptResult::FailedTerminal,
                                TrackerDeliveryReason::TerminalProviderRefusal);
                        } else {
                            // Accepted but not confirmed as exactly what
                            // was sent: read it back before any retry.
                            finishDelivery(
                                operation.operationId,
                                TrackerDeliveryAttemptResult::UnknownOutcome,
                                TrackerDeliveryReason::AcknowledgementLost);
                        }
                        return;
                    }
                    qint64 retryAfterMs = 0;
                    switch (classifyFailure(result, &retryAfterMs)) {
                    case StepOutcome::RateLimited:
                        finishDelivery(
                            operation.operationId,
                            TrackerDeliveryAttemptResult::RateLimitedKnownNotApplied,
                            TrackerDeliveryReason::ProviderRateLimited,
                            QDateTime::currentMSecsSinceEpoch()
                                + qMax<qint64>(retryAfterMs,
                                               kMinimumRateLimitDelayMs));
                        break;
                    case StepOutcome::AuthenticationRequired:
                        // Order matters: the delivery is parked first so
                        // the reconnect prompt never races a retry.
                        finishDelivery(
                            operation.operationId,
                            TrackerDeliveryAttemptResult::NeedsAttention,
                            TrackerDeliveryReason::AuthenticationRequired);
                        noteAuthenticationFailure();
                        break;
                    case StepOutcome::Transient:
                        finishDelivery(
                            operation.operationId,
                            TrackerDeliveryAttemptResult::UnknownOutcome,
                            TrackerDeliveryReason::AcknowledgementLost);
                        break;
                    default:
                        finishDelivery(
                            operation.operationId,
                            TrackerDeliveryAttemptResult::FailedTerminal,
                            TrackerDeliveryReason::TerminalProviderRefusal);
                        break;
                    }
                }));
        }));
}

void AniListSyncRuntime::finishDelivery(
    const QString &operationId,
    TrackerDeliveryAttemptResult result,
    TrackerDeliveryReason reason,
    qint64 retryAfterAtMs)
{
    QString ignored;
    m_delivery->recordAttemptResult(
        operationId, result,
        QDateTime::currentMSecsSinceEpoch(),
        retryAfterAtMs, reason, &ignored);
    m_deliveryInFlight = false;
    m_nextDeliveryAtMs =
        QDateTime::currentMSecsSinceEpoch()
        + kDeliveryDispatchCooldownMs;
    if (result
        == TrackerDeliveryAttemptResult::UnknownOutcome) {
        QTimer::singleShot(
            0, this, [this, operationId] {
                reconcileUnknownDelivery(operationId);
            });
    }
    if (m_syncCenter)
        m_syncCenter->refresh();
    QTimer::singleShot(
        kDeliveryDispatchCooldownMs, this,
        &AniListSyncRuntime::dispatchNextDelivery);
}

void AniListSyncRuntime::reconcileUnknownDelivery(
    const QString &operationId)
{
    const auto operation =
        m_delivery ? m_delivery->operation(operationId)
                   : std::nullopt;
    if (!operation
        || operation->providerId != TrackerProviderId::AniList
        || operation->state != TrackerDeliveryState::UnknownOutcome
        || !m_connections) {
        return;
    }
    const auto connection =
        m_connections->connection(TrackerProviderId::AniList);
    if (!connection)
        return;

    readExportSnapshotAsync(
        *connection, {operation->fact},
        guarded(QPointer<AniListSyncRuntime>(this),
            [this, operationId](
                const std::optional<
                    TrackerRemoteDeliverySnapshot> &snapshot) {
            if (!snapshot || snapshot->items.size() != 1) {
                if (m_syncCenter)
                    m_syncCenter->refresh();
                return;
            }
            const TrackerRemoteDeliveryState state =
                snapshot->items.first();
            TrackerDeliveryReadback readback =
                TrackerDeliveryReadback::Indeterminate;
            if (state.exactlyMatchesIntendedState)
                readback = TrackerDeliveryReadback::ExactPresent;
            else if (!state.present)
                readback = TrackerDeliveryReadback::Absent;
            else
                readback = TrackerDeliveryReadback::PresentDifferent;

            QString ignored;
            m_delivery->reconcileUnknown(
                operationId, readback,
                QDateTime::currentMSecsSinceEpoch(),
                &ignored);
            if (m_syncCenter)
                m_syncCenter->refresh();
            dispatchNextDelivery();
        }));
}

// -------------------------------------------------------------- readback

void AniListSyncRuntime::readEntry(
    const TrackerConnection &connection,
    const QString &aniListId,
    EntryCompletion completion)
{
    const auto body = aniListMediaListEntryRequest(
        connection.remoteAccountId, aniListId);
    if (!body || !available()) {
        completion(EntryStep{StepOutcome::Transient, false, {}, 0});
        return;
    }
    m_api->post(*body,
        guarded(QPointer<AniListSyncRuntime>(this),
            [this, aniListId, completion = std::move(completion)](
                const AniListApiResult &result) {
            EntryStep step;
            if (!result.succeeded()) {
                // No list entry yet is a GraphQL 404, not a failure.
                if (aniListResponseIsNotFound(result.document)) {
                    step.outcome = StepOutcome::Done;
                    step.present = false;
                } else {
                    step.outcome =
                        classifyFailure(result, &step.retryAfterMs);
                    if (step.outcome == StepOutcome::Unmapped)
                        step.outcome = StepOutcome::Transient;
                }
                completion(step);
                return;
            }
            const auto state = aniListParseMediaListEntry(result.document);
            if (!state || state->aniListId != aniListId) {
                step.outcome = StepOutcome::Transient;
                completion(step);
                return;
            }
            step.outcome = StepOutcome::Done;
            step.present = true;
            step.state = *state;
            completion(step);
        }));
}

void AniListSyncRuntime::readExportSnapshotAsync(
    const TrackerConnection &connection,
    const QList<TrackerDeliveryFact> &facts,
    ExportSnapshotCompletion completion)
{
    if (!available()
        || connection.providerId != TrackerProviderId::AniList
        || connection.state != TrackerConnectionState::Connected) {
        completion(std::nullopt);
        return;
    }

    const auto state = std::make_shared<SnapshotState>();
    state->connection = connection;
    state->facts = facts;
    state->completion = std::move(completion);
    state->snapshot.providerId = TrackerProviderId::AniList;
    state->snapshot.remoteAccountId = connection.remoteAccountId;
    state->snapshot.connectionGeneration = connection.connectionGeneration;
    state->snapshot.observedAtMs = QDateTime::currentMSecsSinceEpoch();
    state->snapshot.completeForMappedItems = true;
    snapshotNext(state);
}

void AniListSyncRuntime::snapshotNext(
    const std::shared_ptr<SnapshotState> &state)
{
    if (state->index >= state->facts.size()) {
        state->snapshot.snapshotId =
            QStringLiteral("anilist-state-") + digest(state->material);
        state->completion(state->snapshot);
        return;
    }
    const TrackerDeliveryFact fact = state->facts.at(state->index++);

    // An unmapped or out-of-scope fact is omitted, exactly as MAL omits it;
    // the export preview reports it as missing a mapping.
    mapFact(state->connection, fact,
        guarded(QPointer<AniListSyncRuntime>(this),
            [this, state, fact](StepOutcome mapped) {
        if (mapped == StepOutcome::Unmapped) {
            snapshotNext(state);
            return;
        }
        if (mapped != StepOutcome::Done) {
            state->completion(std::nullopt);
            return;
        }
        bool ambiguous = false;
        const auto mapping =
            uniqueMappingFor(state->connection, fact, &ambiguous);
        const auto identity = mapping
            ? aniListRemoteIdentity(mapping->remote.remoteMediaId)
            : std::nullopt;
        const auto unit = canonicalUnit(fact);
        if (!mapping || ambiguous || !identity || identity->listAggregate
            || !unit) {
            snapshotNext(state);
            return;
        }

        lookupMedia(unit->kind, unit->malId,
            guarded(QPointer<AniListSyncRuntime>(this),
                [this, state, fact, mapping = *mapping,
                 identity = *identity](const MediaStep &media) {
            if (media.outcome == StepOutcome::Unmapped
                || (media.outcome == StepOutcome::Done
                    && media.media.aniListId != identity.aniListId)) {
                snapshotNext(state);
                return;
            }
            if (media.outcome != StepOutcome::Done) {
                if (media.outcome == StepOutcome::AuthenticationRequired)
                    noteAuthenticationFailure();
                state->completion(std::nullopt);
                return;
            }
            readEntry(state->connection, identity.aniListId,
                guarded(QPointer<AniListSyncRuntime>(this),
                    [this, state, fact, mapping, identity,
                     totalUnits = media.media.totalUnits](
                        const EntryStep &entry) {
                if (entry.outcome != StepOutcome::Done) {
                    if (entry.outcome
                        == StepOutcome::AuthenticationRequired) {
                        noteAuthenticationFailure();
                    }
                    state->completion(std::nullopt);
                    return;
                }
                TrackerRemoteDeliveryState remote;
                remote.remoteMediaId = mapping.remote.remoteMediaId;
                remote.factKind = fact.kind;
                remote.sourceEventId = fact.sourceEventId;
                remote.present = entry.present;
                const auto mutation =
                    mutationForDelivery(identity, fact, totalUnits);
                remote.exactlyMatchesIntendedState =
                    entry.present && mutation
                    && entry.state.progress == mutation->intendedProgress
                    && (!mutation->intendedCompleted
                        || entry.state.status
                            == QLatin1String("COMPLETED"));
                remote.stateFingerprint =
                    entry.present ? entryFingerprint(entry.state)
                                  : QStringLiteral("absent");
                if (entry.present) {
                    remote.safeSummary =
                        QStringLiteral("AniList: %1 %2 · %3")
                            .arg(entry.state.progress)
                            .arg(unitName(identity.kind))
                            .arg(aniListSafeStatusLabel(
                                entry.state.status));
                }
                state->material +=
                    (remote.remoteMediaId + QChar(0x1f)
                     + remote.stateFingerprint + QChar(0x1e))
                        .toUtf8();
                state->snapshot.items.append(remote);
                snapshotNext(state);
            }));
        }));
    }));
}
