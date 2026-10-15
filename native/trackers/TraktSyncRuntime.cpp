#include "TraktSyncRuntime.h"

#include "TraktCodec.h"
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
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
template <typename Callback>
auto guarded(const QPointer<TraktSyncRuntime> &self, Callback &&callback)
{
    return [self, callback = std::forward<Callback>(callback)](auto &&...args) mutable {
        if (self) callback(std::forward<decltype(args)>(args)...);
    };
}

constexpr int kDeliveryDispatchCooldownMs = 1100;
constexpr qint64 kMaximumDispatchDelayMs = 60LL * 60LL * 1000LL;

QString digest(const QByteArray &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

std::optional<TrackerCanonicalTitleCandidate> uniqueCandidate(
    const QList<TrackerCanonicalTitleCandidate> &candidates, const QStringList &ids)
{
    std::optional<TrackerCanonicalTitleCandidate> match;
    for (const TrackerCanonicalTitleCandidate &candidate : candidates) {
        bool matches = false;
        for (const QString &id : ids) {
            if (candidate.historyId.compare(id, Qt::CaseInsensitive) == 0) { matches = true; break; }
        }
        if (!matches) continue;
        if (match && match->canonicalMediaId != candidate.canonicalMediaId) return std::nullopt;
        match = candidate;
    }
    return match;
}

std::optional<TrackerTitleMapping> uniqueMapping(
    const TrackerMappingStore *mappings, const TrackerConnection &connection,
    const TrackerDeliveryFact &fact)
{
    if (!mappings) return std::nullopt;
    std::optional<TrackerTitleMapping> result;
    for (const TrackerTitleMapping &mapping : mappings->mappings()) {
        if (mapping.remote.providerId != TrackerProviderId::Trakt
            || mapping.remote.remoteAccountId != connection.remoteAccountId
            || !TrackerDeliveryStore::mappingMatchesFact(mapping, fact)) continue;
        if (result) return std::nullopt;
        result = mapping;
    }
    return result;
}

} // namespace

struct TraktSyncRuntime::HistoryFetchState {
    TrackerConnection connection;
    int page = 1;
    int pageCount = 1;
    QJsonArray rows;
    HistoryCompletion completion;
};

struct TraktSyncRuntime::PullState {
    TrackerConnection connection;
    QString baseCursor;
    QString proposedCursor;
    bool initial = true;
    bool explicitRequest = false;
    QJsonDocument history;
    QJsonDocument playback;
};

struct TraktSyncRuntime::SnapshotState {
    TrackerConnection connection;
    QList<TrackerDeliveryFact> facts;
    ExportSnapshotCompletion completion;
    QJsonDocument history;
    QJsonDocument playback;
};

TraktSyncRuntime::TraktSyncRuntime(
    TrackerConnectionStore *connections, TrackerMappingStore *mappings,
    TrackerImportStore *imports, TrackerProgressImportOwner *importOwner,
    TrackerHistoryEvidenceStore *historyEvidence, TrackerDeliveryStore *delivery,
    TrackerCanonicalDeliverySource *deliverySource, TrackerSyncSettingsStore *settings,
    TrackerSyncCenterModel *syncCenter, TraktApiClient *api, QObject *parent)
    : QObject(parent), m_connections(connections), m_mappings(mappings), m_imports(imports),
      m_importOwner(importOwner), m_historyEvidence(historyEvidence), m_delivery(delivery),
      m_deliverySource(deliverySource), m_settings(settings), m_syncCenter(syncCenter), m_api(api)
{
    if (m_syncCenter) {
        connect(m_syncCenter, &TrackerSyncCenterModel::modelChanged, this, [this] {
            settleConfirmedEvidence();
            dispatchNextDelivery();
        });
    }
}

void TraktSyncRuntime::start()
{
    if (!m_api || !m_api->available() || !m_settings || !m_connections) return;
    const TrackerGlobalSyncSettings global = m_settings->globalSettings();
    if (global.trackerSyncEnabled && global.checkOnLaunch)
        QTimer::singleShot(0, this, [this] { pullTrakt(false); });
    if (global.trackerSyncEnabled && global.backgroundDelivery)
        QTimer::singleShot(0, this, &TraktSyncRuntime::dispatchNextDelivery);
}

void TraktSyncRuntime::syncAll(const QStringList &providerKeys, quint64)
{
    if (providerKeys.contains(QStringLiteral("trakt"))) {
        settleConfirmedEvidence();
        pullTrakt(true);
    }
}

void TraktSyncRuntime::connectionEstablished(const QString &providerKey)
{
    if (providerKey == QLatin1String("trakt")) pullTrakt(true);
}

void TraktSyncRuntime::pullTrakt(bool explicitRequest)
{
    if (!m_api || !m_imports || !m_connections || !m_settings
        || !m_settings->globalSettings().trackerSyncEnabled) return;
    const auto connection = m_connections->connection(TrackerProviderId::Trakt);
    if (!connection || !isCurrentConnection(*connection)
        || m_pullingAccounts.contains(connection->remoteAccountId)) return;
    m_pullingAccounts.insert(connection->remoteAccountId);
    const auto state = std::make_shared<PullState>();
    state->connection = *connection;
    state->explicitRequest = explicitRequest;
    const auto cursor = m_imports->confirmedCursor(TrackerProviderId::Trakt, connection->remoteAccountId);
    state->initial = !cursor.has_value();
    state->baseCursor = cursor.value_or(QString());
    m_api->get(QStringLiteral("/sync/last_activities"), {},
        guarded(QPointer<TraktSyncRuntime>(this), [this, state](const TraktApiResult &result) {
            if (!isCurrentConnection(state->connection)) {
                m_pullingAccounts.remove(state->connection.remoteAccountId);
                return;
            }
            if (!result.succeeded() || !result.document.isObject()) {
                m_pullingAccounts.remove(state->connection.remoteAccountId);
                if (m_syncCenter) m_syncCenter->refresh();
                return;
            }
            state->proposedCursor = digest(result.document.toJson(QJsonDocument::Compact));
            if (!state->initial && state->proposedCursor == state->baseCursor) {
                m_settings->recordSuccessfulSync(TrackerProviderId::Trakt, state->connection.remoteAccountId,
                                                 QDateTime::currentMSecsSinceEpoch());
                m_pullingAccounts.remove(state->connection.remoteAccountId);
                dispatchNextDelivery();
                if (m_syncCenter) m_syncCenter->refresh();
                return;
            }
            fetchHistoryPages(state->connection, guarded(QPointer<TraktSyncRuntime>(this),
                [this, state](std::optional<QJsonDocument> history) {
                    if (!history) {
                        m_pullingAccounts.remove(state->connection.remoteAccountId);
                        if (m_syncCenter) m_syncCenter->refresh();
                        return;
                    }
                    state->history = *history;
                    QUrlQuery query; query.addQueryItem(QStringLiteral("extended"), QStringLiteral("full"));
                    m_api->get(QStringLiteral("/sync/playback"), query,
                        guarded(QPointer<TraktSyncRuntime>(this), [this, state](const TraktApiResult &result) {
                            if (!isCurrentConnection(state->connection)) {
                                m_pullingAccounts.remove(state->connection.remoteAccountId);
                                return;
                            }
                            if (!result.succeeded() || !result.document.isArray()
                                || traktParsePlayback(result.document).size() != result.document.array().size()) {
                                m_pullingAccounts.remove(state->connection.remoteAccountId);
                                if (m_syncCenter) m_syncCenter->refresh();
                                return;
                            }
                            state->playback = result.document;
                            finishPull(state);
                        }));
                }));
        }));
}

void TraktSyncRuntime::fetchHistoryPages(const TrackerConnection &connection, HistoryCompletion completion)
{
    const auto state = std::make_shared<HistoryFetchState>();
    state->connection = connection;
    state->completion = std::move(completion);
    fetchHistoryPage(state);
}

void TraktSyncRuntime::fetchHistoryPage(const std::shared_ptr<HistoryFetchState> &state)
{
    if (!isCurrentConnection(state->connection)) { state->completion(std::nullopt); return; }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(state->page));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("100"));
    query.addQueryItem(QStringLiteral("extended"), QStringLiteral("full"));
    m_api->get(QStringLiteral("/sync/history"), query,
        guarded(QPointer<TraktSyncRuntime>(this), [this, state](const TraktApiResult &result) {
            if (!isCurrentConnection(state->connection)) { state->completion(std::nullopt); return; }
            if (!result.succeeded() || !result.document.isArray()
                || traktParseHistory(result.document).size() != result.document.array().size()) {
                state->completion(std::nullopt); return;
            }
            for (const QJsonValue &value : result.document.array()) state->rows.append(value);
            const int pageCount = result.pageCount > 0 ? result.pageCount : 1;
            if (pageCount > 10000) { state->completion(std::nullopt); return; }
            if (state->page < pageCount) { ++state->page; fetchHistoryPage(state); return; }
            state->completion(QJsonDocument(state->rows));
        }));
}

void TraktSyncRuntime::finishPull(const std::shared_ptr<PullState> &state)
{
    if (!isCurrentConnection(state->connection) || !m_mappings) {
        m_pullingAccounts.remove(state->connection.remoteAccountId);
        return;
    }
    const QList<TraktRemoteFact> historyFacts = traktParseHistory(state->history);
    const QList<TraktRemoteFact> playbackFacts = traktParsePlayback(state->playback);
    QHash<QString, TraktRemoteFact> current;
    QList<PendingEvidence> evidence;
    const QString snapshotId = QStringLiteral("trakt-") + digest(
        state->history.toJson(QJsonDocument::Compact) + state->playback.toJson(QJsonDocument::Compact));
    for (const TraktRemoteFact &fact : historyFacts) {
        const TrackerRemoteMediaKey remote{TrackerProviderId::Trakt, state->connection.remoteAccountId, fact.remoteMediaId};
        const auto previous = current.constFind(fact.remoteMediaId);
        if (previous == current.cend() || fact.occurredAtMs > previous->occurredAtMs)
            current.insert(fact.remoteMediaId, fact);
        evidence.append({remote, fact.providerItemId, snapshotId, fact.occurredAtMs, fact.fingerprint});
    }
    for (const TraktRemoteFact &fact : playbackFacts) {
        const auto previous = current.constFind(fact.remoteMediaId);
        if (previous == current.cend() || fact.occurredAtMs > previous->occurredAtMs)
            current.insert(fact.remoteMediaId, fact);
    }

    const QList<TrackerCanonicalTitleCandidate> candidates = m_importOwner
        ? m_importOwner->userCandidates() : QList<TrackerCanonicalTitleCandidate>{};
    TrackerImportBatchDraft draft;
    draft.providerId = TrackerProviderId::Trakt;
    draft.remoteAccountId = state->connection.remoteAccountId;
    draft.connectionGeneration = state->connection.connectionGeneration;
    draft.snapshotId = snapshotId;
    draft.proposedCursor = state->proposedCursor;
    draft.initialImport = state->initial;
    draft.pageComplete = true;
    draft.baseCursor = state->baseCursor;

    for (const TraktRemoteFact &fact : current) {
        const TrackerRemoteMediaKey remote{TrackerProviderId::Trakt, state->connection.remoteAccountId, fact.remoteMediaId};
        auto mapping = m_mappings->mapping(remote);
        if (!mapping) {
            const auto candidate = uniqueCandidate(candidates, fact.exactLocalIds);
            if (candidate) {
                QString ignored;
                if (m_mappings->upsert(remote, *candidate, TrackerMappingProvenance::ExactProviderIdentity, &ignored))
                    mapping = m_mappings->mapping(remote);
            }
        }
        TrackerImportRemoteItem item;
        item.providerItemId = fact.providerItemId;
        item.remote = remote;
        item.mapping = mapping;
        item.progress = fact.progress;
        item.completed = fact.completed;
        item.supported = traktParseRemoteMediaId(fact.remoteMediaId).has_value();
        item.displayTitle = fact.displayTitle.isEmpty() ? QStringLiteral("Untitled Trakt item") : fact.displayTitle;
        if (mapping && item.supported) {
            const QString historyKind = mapping->canonical.historyKind;
            const QString progressKind = historyKind == QLatin1String("movie")
                || historyKind == QLatin1String("episode") ? QStringLiteral("video") : historyKind;
            TrackerImportProgressTarget target{mapping->canonical.canonicalMediaId, progressKind,
                                               mapping->canonical.historyId, fact.exactProgress / 100.0, fact.completed};
            item.exactProgressTarget = target;
            if (m_importOwner) item.localAtPreview = m_importOwner->currentProgress(*mapping, target);
        }
        draft.items.append(std::move(item));
    }

    QString error;
    const auto batch = m_imports->createPreview(draft, &error);
    if (!batch) {
        m_pullingAccounts.remove(state->connection.remoteAccountId);
        if (m_syncCenter) m_syncCenter->refresh();
        return;
    }
    m_pendingEvidence.insert(batch->batchId, evidence);
    if (state->initial) {
        m_pullingAccounts.remove(state->connection.remoteAccountId);
        if (m_syncCenter) m_syncCenter->refresh();
        return;
    }
    const bool automatic = m_settings->pullAutomatically(TrackerProviderId::Trakt, state->connection.remoteAccountId, true);
    m_imports->applyRoutineSafeAsync(batch->batchId, m_importOwner, automatic,
        guarded(QPointer<TraktSyncRuntime>(this), [this, state](bool succeeded, const QString &) {
            if (!isCurrentConnection(state->connection)) {
                m_pullingAccounts.remove(state->connection.remoteAccountId);
                return;
            }
            if (succeeded) m_settings->recordSuccessfulSync(TrackerProviderId::Trakt, state->connection.remoteAccountId, QDateTime::currentMSecsSinceEpoch());
            m_pullingAccounts.remove(state->connection.remoteAccountId);
            settleConfirmedEvidence();
            dispatchNextDelivery();
            if (m_syncCenter) m_syncCenter->refresh();
        }));
}

void TraktSyncRuntime::settleConfirmedEvidence()
{
    if (!m_imports || !m_mappings || !m_historyEvidence) return;
    const QStringList batchIds = m_pendingEvidence.keys();
    for (const QString &batchId : batchIds) {
        const auto batch = m_imports->batch(batchId);
        if (!batch || !batch->confirmed || !batch->cursorCommitted) continue;
        const auto connection = m_connections ? m_connections->connection(TrackerProviderId::Trakt) : std::nullopt;
        if (!connection || connection->remoteAccountId != batch->remoteAccountId
            || connection->connectionGeneration != batch->connectionGeneration
            || !isCurrentConnection(*connection)) {
            m_pendingEvidence.remove(batchId);
            continue;
        }
        for (const PendingEvidence &pending : m_pendingEvidence.value(batchId)) {
            const auto accepted = std::find_if(batch->items.cbegin(), batch->items.cend(),
                [&pending](const TrackerImportItem &item) {
                    return item.remote.remote.remoteMediaId == pending.remote.remoteMediaId
                        && item.remote.providerItemId == pending.providerEventId
                        && (item.state == TrackerImportItemState::Applied
                            || (item.state == TrackerImportItemState::NonMutating
                                && item.classification == TrackerImportClassification::ExactMatch));
                });
            if (accepted == batch->items.cend()) continue;
            const auto mapping = m_mappings->mapping(pending.remote);
            if (!mapping || pending.occurredAtMs <= 0) continue;
            TrackerImportedHistoryEvidence evidence;
            evidence.mapping = *mapping;
            evidence.eventKind = TrackerImportedEventKind::Completion;
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

void TraktSyncRuntime::dispatchNextDelivery()
{
    if (m_deliveryInFlight || !m_api || !m_delivery || !m_deliverySource || !m_settings
        || !m_settings->globalSettings().trackerSyncEnabled || !m_settings->globalSettings().backgroundDelivery) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_nextDeliveryAtMs) {
        QTimer::singleShot(static_cast<int>(qBound<qint64>(0, m_nextDeliveryAtMs - now, kMaximumDispatchDelayMs)),
                           this, &TraktSyncRuntime::dispatchNextDelivery);
        return;
    }
    const QList<TrackerDeliveryOperation> ready = m_delivery->readyOperations(now);
    const auto found = std::find_if(ready.cbegin(), ready.cend(), [](const TrackerDeliveryOperation &op) {
        return op.providerId == TrackerProviderId::Trakt;
    });
    if (found == ready.cend()) { if (m_syncCenter) m_syncCenter->refresh(); return; }
    const TrackerDeliveryOperation operation = *found;
    const auto connection = m_connections ? m_connections->connection(TrackerProviderId::Trakt) : std::nullopt;
    if (!connection || !isCurrentConnection(*connection)
        || operation.remoteAccountId != connection->remoteAccountId
        || operation.connectionGeneration != connection->connectionGeneration) return;
    QString error;
    if (!m_delivery->markDelivering(operation.operationId, m_deliverySource, now, &error)) {
        if (m_syncCenter) m_syncCenter->refresh();
        return;
    }
    m_deliveryInFlight = true;
    sendDelivery(operation);
}

void TraktSyncRuntime::sendDelivery(const TrackerDeliveryOperation &operation)
{
    const auto connection = m_connections ? m_connections->connection(TrackerProviderId::Trakt) : std::nullopt;
    if (!connection || !isCurrentConnection(*connection)
        || connection->remoteAccountId != operation.remoteAccountId
        || connection->connectionGeneration != operation.connectionGeneration) {
        m_deliveryInFlight = false;
        return;
    }
    QJsonObject body;
    QString path;
    if (operation.fact.kind == TrackerDeliveryFactKind::Completion) {
        body = traktHistoryBody(operation.mapping.remote.remoteMediaId, static_cast<qint64>(operation.fact.sourceRevision));
        path = QStringLiteral("/sync/history");
    } else {
        body = traktScrobbleBody(operation.mapping.remote.remoteMediaId, qBound(0, operation.fact.progress * 100, 10000));
        path = QStringLiteral("/scrobble/pause");
    }
    if (body.isEmpty()) {
        finishDelivery(operation.operationId, TrackerDeliveryAttemptResult::FailedTerminal, TrackerDeliveryReason::UnsupportedAction);
        return;
    }
    m_api->post(path, QJsonDocument(body), guarded(QPointer<TraktSyncRuntime>(this),
        [this, operation, connection = *connection](const TraktApiResult &result) {
            if (!isCurrentConnection(connection)) { m_deliveryInFlight = false; return; }
            if (result.succeeded()) {
                bool acknowledged = false;
                const auto identity = traktParseRemoteMediaId(operation.mapping.remote.remoteMediaId);
                const QJsonObject response = result.document.object();
                if (operation.fact.kind == TrackerDeliveryFactKind::Completion) {
                    const QString type = identity->kind == TraktMediaKind::Movie ? QStringLiteral("movies") : QStringLiteral("episodes");
                    acknowledged = response.value(QStringLiteral("added")).toObject().value(type).toInt() == 1
                        && response.value(QStringLiteral("not_found")).toObject().value(type).toArray().isEmpty();
                } else {
                    const QString type = identity->kind == TraktMediaKind::Movie ? QStringLiteral("movie") : QStringLiteral("episode");
                    acknowledged = response.value(QStringLiteral("action")).toString() == QLatin1String("pause")
                        && QString::number(response.value(type).toObject().value(QStringLiteral("ids")).toObject()
                                               .value(QStringLiteral("trakt")).toInteger()) == identity->traktId
                        && std::abs(response.value(QStringLiteral("progress")).toDouble(-1) - operation.fact.progress) <= 0.005;
                }
                finishDelivery(operation.operationId,
                    acknowledged ? TrackerDeliveryAttemptResult::Succeeded : TrackerDeliveryAttemptResult::UnknownOutcome,
                    acknowledged ? TrackerDeliveryReason::None : TrackerDeliveryReason::AcknowledgementLost);
            }
            else if (result.statusCode == 429)
                finishDelivery(operation.operationId, TrackerDeliveryAttemptResult::RateLimitedKnownNotApplied,
                               TrackerDeliveryReason::ProviderRateLimited, QDateTime::currentMSecsSinceEpoch() + qMax<qint64>(result.retryAfterMs, 2000));
            else if (result.statusCode == 401 || result.statusCode == 403)
                finishDelivery(operation.operationId, TrackerDeliveryAttemptResult::NeedsAttention, TrackerDeliveryReason::AuthenticationRequired);
            else if (result.networkFailure || result.payloadTooLarge || result.statusCode >= 500 || result.statusCode == 409)
                finishDelivery(operation.operationId, TrackerDeliveryAttemptResult::UnknownOutcome, TrackerDeliveryReason::AcknowledgementLost);
            else
                finishDelivery(operation.operationId, TrackerDeliveryAttemptResult::FailedTerminal, TrackerDeliveryReason::TerminalProviderRefusal);
        }));
}

void TraktSyncRuntime::finishDelivery(const QString &operationId, TrackerDeliveryAttemptResult result,
                                      TrackerDeliveryReason reason, qint64 retryAfterMs)
{
    QString ignored;
    m_delivery->recordAttemptResult(operationId, result, QDateTime::currentMSecsSinceEpoch(), retryAfterMs, reason, &ignored);
    m_deliveryInFlight = false;
    m_nextDeliveryAtMs = QDateTime::currentMSecsSinceEpoch() + kDeliveryDispatchCooldownMs;
    if (result == TrackerDeliveryAttemptResult::UnknownOutcome)
        QTimer::singleShot(0, this, [this, operationId] { reconcileUnknownDelivery(operationId); });
    if (m_syncCenter) m_syncCenter->refresh();
    QTimer::singleShot(kDeliveryDispatchCooldownMs, this, &TraktSyncRuntime::dispatchNextDelivery);
}

void TraktSyncRuntime::reconcileUnknownDelivery(const QString &operationId)
{
    const auto operation = m_delivery ? m_delivery->operation(operationId) : std::nullopt;
    if (!operation || operation->state != TrackerDeliveryState::UnknownOutcome || !m_connections) return;
    const auto connection = m_connections->connection(TrackerProviderId::Trakt);
    if (!connection || operation->remoteAccountId != connection->remoteAccountId
        || operation->connectionGeneration != connection->connectionGeneration) return;
    readExportSnapshotAsync(*connection, {operation->fact},
        guarded(QPointer<TraktSyncRuntime>(this), [this, operationId](const std::optional<TrackerRemoteDeliverySnapshot> &snapshot) {
            if (!snapshot || snapshot->items.size() != 1) { if (m_syncCenter) m_syncCenter->refresh(); return; }
            const TrackerRemoteDeliveryState state = snapshot->items.first();
            TrackerDeliveryReadback readback = TrackerDeliveryReadback::Indeterminate;
            if (state.exactlyMatchesIntendedState) readback = TrackerDeliveryReadback::ExactPresent;
            else if (!state.present) readback = TrackerDeliveryReadback::Absent;
            else readback = TrackerDeliveryReadback::PresentDifferent;
            QString ignored;
            m_delivery->reconcileUnknown(operationId, readback, QDateTime::currentMSecsSinceEpoch(), &ignored);
            if (m_syncCenter) m_syncCenter->refresh();
            dispatchNextDelivery();
        }));
}

void TraktSyncRuntime::readExportSnapshotAsync(const TrackerConnection &connection,
                                               const QList<TrackerDeliveryFact> &facts,
                                               ExportSnapshotCompletion completion)
{
    if (!m_api || !m_api->available() || !isCurrentConnection(connection)) {
        completion(std::nullopt);
        return;
    }
    const auto state = std::make_shared<SnapshotState>();
    state->connection = connection;
    state->facts = facts;
    state->completion = std::move(completion);
    fetchHistoryPages(connection, guarded(QPointer<TraktSyncRuntime>(this),
        [this, state](std::optional<QJsonDocument> history) {
            if (!history) { state->completion(std::nullopt); return; }
            state->history = *history;
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("extended"), QStringLiteral("full"));
            m_api->get(QStringLiteral("/sync/playback"), query,
                guarded(QPointer<TraktSyncRuntime>(this), [this, state](const TraktApiResult &result) {
                    if (!isCurrentConnection(state->connection)) { state->completion(std::nullopt); return; }
                    if (!result.succeeded() || !result.document.isArray()
                        || traktParsePlayback(result.document).size() != result.document.array().size()) {
                        state->completion(std::nullopt);
                        return;
                    }
                    state->playback = result.document;
                    deliverExportSnapshot(state);
                }));
        }));
}

void TraktSyncRuntime::deliverExportSnapshot(const std::shared_ptr<SnapshotState> &state)
{
    if (!isCurrentConnection(state->connection)) { state->completion(std::nullopt); return; }
    QMultiHash<QString, TraktRemoteFact> completions;
    QHash<QString, TraktRemoteFact> progress;
    for (const TraktRemoteFact &fact : traktParseHistory(state->history)) completions.insert(fact.remoteMediaId, fact);
    for (const TraktRemoteFact &fact : traktParsePlayback(state->playback)) progress.insert(fact.remoteMediaId, fact);
    TrackerRemoteDeliverySnapshot snapshot;
    snapshot.providerId = TrackerProviderId::Trakt;
    snapshot.remoteAccountId = state->connection.remoteAccountId;
    snapshot.connectionGeneration = state->connection.connectionGeneration;
    snapshot.snapshotId = QStringLiteral("trakt-state-") + digest(
        state->history.toJson(QJsonDocument::Compact) + state->playback.toJson(QJsonDocument::Compact));
    snapshot.observedAtMs = QDateTime::currentMSecsSinceEpoch();
    snapshot.completeForMappedItems = true;
    for (const TrackerDeliveryFact &fact : state->facts) {
        const auto mapping = uniqueMapping(m_mappings, state->connection, fact);
        if (!mapping) continue;
        const QString remoteId = mapping->remote.remoteMediaId;
        TrackerRemoteDeliveryState remoteState;
        remoteState.remoteMediaId = remoteId;
        remoteState.factKind = fact.kind;
        remoteState.sourceEventId = fact.sourceEventId;
        if (fact.kind == TrackerDeliveryFactKind::Completion) {
            const auto found = completions.constFind(remoteId);
            remoteState.present = found != completions.cend();
            remoteState.exactlyMatchesIntendedState = false;
            const auto watches = completions.values(remoteId);
            for (const TraktRemoteFact &watch : watches) {
                if (watch.occurredAtMs == static_cast<qint64>(fact.sourceRevision)) {
                    remoteState.exactlyMatchesIntendedState = true;
                    break;
                }
            }
            remoteState.stateFingerprint = remoteState.present ? found->fingerprint : QStringLiteral("absent");
            remoteState.safeSummary = remoteState.present ? QStringLiteral("Already watched on Trakt") : QString();
        } else {
            const auto found = progress.constFind(remoteId);
            remoteState.present = found != progress.cend();
            remoteState.exactlyMatchesIntendedState = remoteState.present
                && std::abs(found->exactProgress - static_cast<double>(fact.progress)) <= 0.005;
            remoteState.stateFingerprint = remoteState.present ? found->fingerprint : QStringLiteral("absent");
            remoteState.safeSummary = remoteState.present ? QStringLiteral("Trakt progress: %1%").arg(found->progress) : QString();
        }
        snapshot.items.append(remoteState);
    }
    state->completion(snapshot);
}

bool TraktSyncRuntime::isCurrentConnection(const TrackerConnection &connection) const
{
    const auto current = m_connections ? m_connections->connection(TrackerProviderId::Trakt) : std::nullopt;
    return m_api && m_api->available() && current
        && connection.providerId == TrackerProviderId::Trakt
        && current->state == TrackerConnectionState::Connected
        && current->remoteAccountId == connection.remoteAccountId
        && current->connectionGeneration == connection.connectionGeneration
        && m_api->credentialAccountMatches(connection.remoteAccountId);
}
