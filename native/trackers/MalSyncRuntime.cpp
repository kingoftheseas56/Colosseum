#include "MalSyncRuntime.h"

#include "MalListStateStore.h"
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
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

constexpr int kDeliveryDispatchCooldownMs = 1100;
constexpr qint64 kMaximumDispatchDelayMs = 60LL * 60 * 1000;
constexpr int kPageLimit = 1000;
constexpr int kMaximumOffset = 100000;

template <typename Callback>
auto guarded(const QPointer<MalSyncRuntime> &self, Callback &&callback)
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

QString aggregateRemoteId(const MalListItem &item)
{
    return MalRemoteIdentity{item.kind, item.malId, 0, true}
        .remoteMediaId();
}

QString canonicalSeriesId(const MalListItem &item)
{
    return QStringLiteral("mal:") + item.malId;
}

bool candidateKindMatches(const TrackerCanonicalTitleCandidate &candidate,
                          MalMediaKind kind)
{
    if (kind == MalMediaKind::Anime) {
        return candidate.historyKind == QLatin1String("anime")
            || candidate.historyKind == QLatin1String("series");
    }
    return candidate.historyKind == QLatin1String("manga")
        || candidate.historyKind == QLatin1String("tankoban");
}

std::optional<TrackerCanonicalTitleCandidate> uniqueSeriesCandidate(
    const QList<TrackerCanonicalTitleCandidate> &candidates,
    const MalListItem &item)
{
    std::optional<TrackerCanonicalTitleCandidate> result;
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

QString listDisplayTitle(const MalListItem &item)
{
    const QString unit = item.kind == MalMediaKind::Anime
        ? QStringLiteral("episodes")
        : QStringLiteral("chapters");
    const QString title =
        item.title.isEmpty() ? QStringLiteral("Untitled MyAnimeList item")
                             : item.title;
    return QStringLiteral("%1 · %2 · %3 %4")
        .arg(title,
             malSafeStatusLabel(item.status, item.kind),
             QString::number(item.progress),
             unit)
        .left(500);
}

QString listItemFingerprint(const MalListItem &item)
{
    const QByteArray material =
        (malListKey(item.kind, item.malId)
         + QChar(0x1f) + item.status
         + QChar(0x1f) + QString::number(item.progress)
         + QChar(0x1f) + QString::number(item.totalUnits)
         + QChar(0x1f) + QString::number(item.updatedAtMs))
            .toUtf8();
    return digest(material);
}

} // namespace

struct MalSyncRuntime::ListFetchState {
    TrackerConnection connection;
    int phase = 0;
    int offset = 0;
    QList<MalListItem> items;
    QByteArray snapshotMaterial;
    ListFetchCompletion completion;
};

struct MalSyncRuntime::PullState {
    TrackerConnection connection;
    QString baseCursor;
    bool initial = true;
    bool explicitRequest = false;
};

MalSyncRuntime::MalSyncRuntime(
    TrackerConnectionStore *connections,
    TrackerMappingStore *mappings,
    TrackerImportStore *imports,
    TrackerProgressImportOwner *importOwner,
    TrackerDeliveryStore *delivery,
    TrackerCanonicalDeliverySource *deliverySource,
    TrackerSyncSettingsStore *settings,
    TrackerSyncCenterModel *syncCenter,
    MalApiClient *api,
    MalListStateStore *listState,
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
      m_listState(listState)
{
    if (m_syncCenter) {
        connect(m_syncCenter, &TrackerSyncCenterModel::modelChanged,
                this, [this] { dispatchNextDelivery(); });
    }
}

void MalSyncRuntime::start()
{
    if (!m_api || !m_api->available()
        || !m_connections || !m_settings)
        return;
    const auto connection =
        m_connections->connection(TrackerProviderId::Mal);
    if (connection
        && connection->state == TrackerConnectionState::Connected) {
        if (m_listState && m_listState->healthy()
            && m_listState->remoteAccountId() == connection->remoteAccountId) {
            for (const MalListItem &item : m_listState->items()) {
                m_remoteList.insert(
                    malListKey(item.kind, item.malId), item);
            }
        }
        ensureExactDeliveryMappings(*connection);
        if (m_delivery && m_deliverySource)
            m_delivery->recoverSourceGap(m_deliverySource);
    }

    const TrackerGlobalSyncSettings global =
        m_settings->globalSettings();
    if (global.trackerSyncEnabled && global.checkOnLaunch)
        QTimer::singleShot(0, this, [this] { pullMal(false); });
    if (global.trackerSyncEnabled && global.backgroundDelivery)
        QTimer::singleShot(0, this,
                           &MalSyncRuntime::dispatchNextDelivery);
}

void MalSyncRuntime::syncAll(
    const QStringList &providerKeys, quint64)
{
    if (providerKeys.contains(QStringLiteral("mal")))
        pullMal(true);
}

void MalSyncRuntime::connectionEstablished(
    const QString &providerKey)
{
    if (providerKey != QLatin1String("mal"))
        return;
    const auto connection =
        m_connections
        ? m_connections->connection(TrackerProviderId::Mal)
        : std::nullopt;
    if (connection) {
        ensureExactDeliveryMappings(*connection);
        if (m_delivery && m_deliverySource)
            m_delivery->recoverSourceGap(m_deliverySource);
    }
    pullMal(true);
}

void MalSyncRuntime::pullMal(bool explicitRequest)
{
    if (!m_api || !m_api->available()
        || !m_imports || !m_connections || !m_settings
        || !m_settings->globalSettings().trackerSyncEnabled) {
        return;
    }
    const auto connection =
        m_connections->connection(TrackerProviderId::Mal);
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
        TrackerProviderId::Mal, connection->remoteAccountId);
    state->initial = !cursor.has_value();
    state->baseCursor = cursor.value_or(QString());

    fetchLists(*connection,
        guarded(QPointer<MalSyncRuntime>(this),
            [this, state](
                bool succeeded,
                const QList<MalListItem> &items,
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

void MalSyncRuntime::fetchLists(
    const TrackerConnection &connection,
    ListFetchCompletion completion)
{
    if (!m_api || !m_api->available()
        || connection.providerId != TrackerProviderId::Mal
        || connection.state != TrackerConnectionState::Connected) {
        completion(false, {}, {});
        return;
    }
    const auto state = std::make_shared<ListFetchState>();
    state->connection = connection;
    state->completion = std::move(completion);
    fetchListPage(state);
}

void MalSyncRuntime::fetchListPage(
    const std::shared_ptr<ListFetchState> &state)
{
    if (state->phase >= 2) {
        state->completion(true, state->items,
                          state->snapshotMaterial);
        return;
    }
    if (state->offset < 0
        || state->offset > kMaximumOffset) {
        state->completion(false, {}, {});
        return;
    }

    const MalMediaKind kind =
        state->phase == 0 ? MalMediaKind::Anime
                          : MalMediaKind::Manga;
    const QString path =
        kind == MalMediaKind::Anime
        ? QStringLiteral("/users/@me/animelist")
        : QStringLiteral("/users/@me/mangalist");

    QUrlQuery query;
    query.addQueryItem(
        QStringLiteral("fields"),
        kind == MalMediaKind::Anime
            ? QStringLiteral("list_status,num_episodes")
            : QStringLiteral("list_status,num_chapters"));
    query.addQueryItem(QStringLiteral("limit"),
                       QString::number(kPageLimit));
    query.addQueryItem(QStringLiteral("offset"),
                       QString::number(state->offset));

    m_api->get(path, query,
        guarded(QPointer<MalSyncRuntime>(this),
            [this, state, kind](
                const MalApiResult &result) {
            if (!result.succeeded()
                || !result.document.isObject()) {
                state->completion(false, {}, {});
                return;
            }
            const QJsonObject object = result.document.object();
            const QJsonArray data =
                object.value(QStringLiteral("data")).toArray();
            for (const QJsonValue &value : data) {
                if (!value.isObject()) {
                    state->completion(false, {}, {});
                    return;
                }
                const auto item =
                    malListItemFromJson(value.toObject(), kind);
                if (!item) {
                    state->completion(false, {}, {});
                    return;
                }
                state->items.append(*item);
            }
            state->snapshotMaterial +=
                result.document.toJson(QJsonDocument::Compact);

            const QString next =
                object.value(QStringLiteral("paging"))
                    .toObject()
                    .value(QStringLiteral("next"))
                    .toString();
            if (!next.isEmpty()) {
                if (data.isEmpty()) {
                    state->completion(false, {}, {});
                    return;
                }
                state->offset += data.size();
            } else {
                ++state->phase;
                state->offset = 0;
            }
            fetchListPage(state);
        }));
}

void MalSyncRuntime::finishPull(
    const std::shared_ptr<PullState> &state,
    const QList<MalListItem> &items,
    const QByteArray &snapshotMaterial)
{
    m_remoteList.clear();
    for (const MalListItem &item : items)
        m_remoteList.insert(
            malListKey(item.kind, item.malId), item);

    TrackerImportBatchDraft draft;
    draft.providerId = TrackerProviderId::Mal;
    draft.remoteAccountId =
        state->connection.remoteAccountId;
    draft.connectionGeneration =
        state->connection.connectionGeneration;
    draft.snapshotId =
        QStringLiteral("mal-") + digest(snapshotMaterial);
    if (m_listState) {
        QString listStateError;
        if (!m_listState->healthy(&listStateError)
            || !m_listState->replaceAll(
                state->connection.remoteAccountId,
                draft.snapshotId, items, &listStateError)) {
            m_pullingAccounts.remove(
                state->connection.remoteAccountId);
            if (m_syncCenter)
                m_syncCenter->refresh();
            return;
        }
    }
    draft.proposedCursor =
        QDateTime::currentDateTimeUtc()
            .toString(Qt::ISODateWithMs);
    draft.initialImport = state->initial;
    draft.pageComplete = true;
    draft.baseCursor = state->baseCursor;

    const QList<TrackerCanonicalTitleCandidate> candidates =
        m_importOwner ? m_importOwner->userCandidates()
                      : QList<TrackerCanonicalTitleCandidate>{};

    for (const MalListItem &remoteItem : items) {
        TrackerRemoteMediaKey remote{
            TrackerProviderId::Mal,
            state->connection.remoteAccountId,
            aggregateRemoteId(remoteItem)};
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
            QStringLiteral("mal-list-")
            + listItemFingerprint(remoteItem);
        item.remote = remote;
        item.mapping = mapping;
        item.progress = remoteItem.progress;
        item.completed =
            remoteItem.status == QLatin1String("completed");
        item.supported = true;
        item.displayTitle = listDisplayTitle(remoteItem);
        // Deliberately no exactProgressTarget. A MAL watched/read count
        // identifies a list aggregate, not an exact Colosseum Continue
        // location. The import journal therefore records/reviews the fact
        // but classifies a mapped aggregate as non-mutating Unsupported.
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
        TrackerProviderId::Mal,
        state->connection.remoteAccountId,
        true);
    m_imports->applyRoutineSafeAsync(
        batch->batchId, m_importOwner, automatic,
        guarded(QPointer<MalSyncRuntime>(this),
            [this, state](bool succeeded,
                          const QString &) {
            if (succeeded) {
                m_settings->recordSuccessfulSync(
                    TrackerProviderId::Mal,
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

void MalSyncRuntime::ensureExactDeliveryMappings(
    const TrackerConnection &connection)
{
    if (!m_mappings || !m_deliverySource
        || connection.providerId != TrackerProviderId::Mal
        || connection.state != TrackerConnectionState::Connected) {
        return;
    }

    for (const TrackerDeliveryFact &fact
         : m_deliverySource->currentCommittedFacts()) {
        if (fact.origin != TrackerDeliveryOrigin::NativeLocal)
            continue;
        const auto identity =
            malIdentityForCanonicalFact(fact);
        if (!identity)
            continue;

        TrackerRemoteMediaKey remote{
            TrackerProviderId::Mal,
            connection.remoteAccountId,
            identity->remoteMediaId()};
        if (m_mappings->mapping(remote))
            continue;

        TrackerCanonicalTitleCandidate candidate{
            fact.canonicalMediaId,
            fact.historyKind,
            fact.historyId,
            fact.canonicalMediaId,
            {}};
        QString ignored;
        m_mappings->upsert(
            remote, candidate,
            TrackerMappingProvenance::ExactProviderIdentity,
            &ignored);
    }
}

void MalSyncRuntime::dispatchNextDelivery()
{
    if (m_deliveryInFlight || !m_api || !m_delivery
        || !m_deliverySource || !m_settings
        || !m_settings->globalSettings().trackerSyncEnabled
        || !m_settings->globalSettings().backgroundDelivery) {
        return;
    }

    const auto connection = m_connections
        ? m_connections->connection(TrackerProviderId::Mal)
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
            &MalSyncRuntime::dispatchNextDelivery);
        return;
    }

    const QList<TrackerDeliveryOperation> ready =
        m_delivery->readyOperations(now);
    const auto found = std::find_if(
        ready.cbegin(), ready.cend(),
        [](const TrackerDeliveryOperation &operation) {
            return operation.providerId == TrackerProviderId::Mal;
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

void MalSyncRuntime::sendDelivery(
    const TrackerDeliveryOperation &operation)
{
    const auto identity =
        malRemoteIdentity(operation.mapping.remote.remoteMediaId);
    if (!identity || identity->listAggregate) {
        finishDelivery(
            operation.operationId,
            TrackerDeliveryAttemptResult::FailedTerminal,
            TrackerDeliveryReason::UnsupportedAction);
        return;
    }

    const auto remote =
        m_remoteList.constFind(
            malListKey(identity->kind, identity->malId));
    const int totalUnits =
        remote == m_remoteList.cend()
            ? 0 : remote->totalUnits;
    const auto mutation = malMutationForDelivery(
        *identity, operation.fact, totalUnits);
    if (!mutation) {
        finishDelivery(
            operation.operationId,
            TrackerDeliveryAttemptResult::FailedTerminal,
            TrackerDeliveryReason::UnsupportedAction);
        return;
    }

    m_api->patchForm(
        mutation->path, mutation->form,
        guarded(QPointer<MalSyncRuntime>(this),
            [this, operation, identity = *identity,
             mutation = *mutation](
                const MalApiResult &result) {
            if (result.succeeded()) {
                MalListItem item =
                    m_remoteList.value(
                        malListKey(identity.kind,
                                   identity.malId));
                item.kind = identity.kind;
                item.malId = identity.malId;
                item.progress = mutation.intendedProgress;
                item.status = mutation.intendedCompleted
                    ? QStringLiteral("completed")
                    : (identity.kind == MalMediaKind::Anime
                        ? QStringLiteral("watching")
                        : QStringLiteral("reading"));
                m_remoteList.insert(
                    malListKey(identity.kind,
                               identity.malId),
                    item);
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::Succeeded,
                    TrackerDeliveryReason::None);
            } else if (result.statusCode == 429) {
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::RateLimitedKnownNotApplied,
                    TrackerDeliveryReason::ProviderRateLimited,
                    QDateTime::currentMSecsSinceEpoch()
                        + qMax<qint64>(
                            result.retryAfterMs, 2000));
            } else if (result.statusCode == 401
                       || result.statusCode == 403) {
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::NeedsAttention,
                    TrackerDeliveryReason::AuthenticationRequired);
            } else if (result.networkFailure
                       || result.payloadTooLarge
                       || result.statusCode >= 500) {
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::UnknownOutcome,
                    TrackerDeliveryReason::AcknowledgementLost);
            } else {
                finishDelivery(
                    operation.operationId,
                    TrackerDeliveryAttemptResult::FailedTerminal,
                    TrackerDeliveryReason::TerminalProviderRefusal);
            }
        }));
}

void MalSyncRuntime::finishDelivery(
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
        &MalSyncRuntime::dispatchNextDelivery);
}

void MalSyncRuntime::reconcileUnknownDelivery(
    const QString &operationId)
{
    const auto operation =
        m_delivery ? m_delivery->operation(operationId)
                   : std::nullopt;
    if (!operation
        || operation->providerId != TrackerProviderId::Mal
        || operation->state != TrackerDeliveryState::UnknownOutcome
        || !m_connections) {
        return;
    }
    const auto connection =
        m_connections->connection(TrackerProviderId::Mal);
    if (!connection)
        return;

    readExportSnapshotAsync(
        *connection, {operation->fact},
        guarded(QPointer<MalSyncRuntime>(this),
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

void MalSyncRuntime::readExportSnapshotAsync(
    const TrackerConnection &connection,
    const QList<TrackerDeliveryFact> &facts,
    ExportSnapshotCompletion completion)
{
    if (!m_api || !m_api->available()
        || connection.providerId != TrackerProviderId::Mal
        || connection.state != TrackerConnectionState::Connected) {
        completion(std::nullopt);
        return;
    }

    fetchLists(
        connection,
        guarded(QPointer<MalSyncRuntime>(this),
            [this, connection, facts,
             completion = std::move(completion)](
                bool succeeded,
                const QList<MalListItem> &items,
                const QByteArray &snapshotMaterial) mutable {
            if (!succeeded) {
                completion(std::nullopt);
                return;
            }
            buildRemoteSnapshot(
                connection, facts, items,
                snapshotMaterial,
                std::move(completion));
        }));
}

void MalSyncRuntime::buildRemoteSnapshot(
    const TrackerConnection &connection,
    const QList<TrackerDeliveryFact> &facts,
    const QList<MalListItem> &items,
    const QByteArray &snapshotMaterial,
    ExportSnapshotCompletion completion)
{
    QHash<QString, MalListItem> remoteList;
    for (const MalListItem &item : items) {
        remoteList.insert(
            malListKey(item.kind, item.malId), item);
    }
    m_remoteList = remoteList;

    TrackerRemoteDeliverySnapshot snapshot;
    snapshot.providerId = TrackerProviderId::Mal;
    snapshot.remoteAccountId = connection.remoteAccountId;
    snapshot.connectionGeneration =
        connection.connectionGeneration;
    snapshot.snapshotId =
        QStringLiteral("mal-state-")
        + digest(snapshotMaterial);
    snapshot.observedAtMs =
        QDateTime::currentMSecsSinceEpoch();
    snapshot.completeForMappedItems = true;

    const QList<TrackerTitleMapping> mappings =
        m_mappings ? m_mappings->mappings()
                   : QList<TrackerTitleMapping>{};

    for (const TrackerDeliveryFact &fact : facts) {
        std::optional<TrackerTitleMapping> mapping;
        bool ambiguous = false;
        for (const TrackerTitleMapping &candidate : mappings) {
            if (candidate.remote.providerId
                    != TrackerProviderId::Mal
                || candidate.remote.remoteAccountId
                    != connection.remoteAccountId
                || candidate.canonical.canonicalMediaId
                    != fact.canonicalMediaId
                || candidate.canonical.historyKind
                    != fact.historyKind
                || candidate.canonical.historyId
                    != fact.historyId) {
                continue;
            }
            if (mapping) {
                ambiguous = true;
                break;
            }
            mapping = candidate;
        }
        if (!mapping || ambiguous)
            continue;

        const auto identity =
            malRemoteIdentity(mapping->remote.remoteMediaId);
        if (!identity || identity->listAggregate)
            continue;

        TrackerRemoteDeliveryState state;
        state.remoteMediaId =
            mapping->remote.remoteMediaId;
        state.factKind = fact.kind;
        state.sourceEventId = fact.sourceEventId;

        const auto found =
            remoteList.constFind(
                malListKey(identity->kind,
                           identity->malId));
        state.present = found != remoteList.cend();
        const int totalUnits =
            state.present ? found->totalUnits : 0;
        const auto mutation =
            malMutationForDelivery(
                *identity, fact, totalUnits);
        state.exactlyMatchesIntendedState =
            state.present && mutation
            && found->progress
                == mutation->intendedProgress
            && (!mutation->intendedCompleted
                || found->status
                    == QLatin1String("completed"));
        state.stateFingerprint =
            state.present
            ? listItemFingerprint(*found)
            : QStringLiteral("absent");
        if (state.present) {
            const QString unit =
                identity->kind == MalMediaKind::Anime
                ? QStringLiteral("episodes")
                : QStringLiteral("chapters");
            state.safeSummary =
                QStringLiteral("MyAnimeList: %1 %2 · %3")
                    .arg(found->progress)
                    .arg(unit)
                    .arg(malSafeStatusLabel(
                        found->status,
                        found->kind));
        }
        snapshot.items.append(state);
    }

    completion(snapshot);
}
