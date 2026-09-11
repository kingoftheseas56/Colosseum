// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncEngine.h"

#include "SyncOwnershipInventory.h"
#include "SyncPayloadFirewall.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
constexpr int kPushBatchLimit = 100;
constexpr qsizetype kAttachmentManifestWireByteLimit = 48 * 1024;
constexpr int kIdlePullIntervalMs = 30 * 1000;
constexpr int kRetryBaseMs = 2 * 1000;
constexpr int kRetryMaximumMs = 5 * 60 * 1000;
// A remote HLC further ahead than this is treated as poisoned server data,
// not a clock observation (see processPullReply).
constexpr qint64 kMaximumRemoteClockFutureMs =
    15 * 60 * 1000;
constexpr qsizetype kPushBodyByteLimit = 64 * 1024;

QString normalizedUuid(
    const QString &value) {
    const QUuid parsed(value);
    if (parsed.isNull())
        return QString();

    return parsed.toString(
        QUuid::WithoutBraces)
        .toLower();
}

std::optional<qint64>
signedJsonInteger(
    const QJsonValue &value) {
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed =
            value.toString().toLongLong(
                &ok);
        if (ok)
            return parsed;
        return std::nullopt;
    }

    if (value.isDouble()) {
        const double number =
            value.toDouble();

        if (number
                < static_cast<double>(
                    std::numeric_limits<qint64>::min())
            || number
                > static_cast<double>(
                    std::numeric_limits<qint64>::max())) {
            return std::nullopt;
        }

        return static_cast<qint64>(
            number);
    }

    return std::nullopt;
}

const SyncWireHlc &syncMutationOrderingHlc(
    const SyncWireMutation &mutation) {
    return mutation.materializedHlc.has_value()
        ? *mutation.materializedHlc
        : mutation.hlc;
}

bool syncEntryIsCoveredByWinner(
    const SyncWinner &winner,
    const SyncWirePullEntry &entry) {
    const int ordering =
        compareSyncWireHlc(
            winner.hlc,
            syncMutationOrderingHlc(entry.mutation));
    if (ordering > 0)
        return true;
    if (ordering < 0)
        return false;

    // A semantic merge can append a later journal row while retaining the
    // existing winner HLC. The server sequence makes that canonical payload
    // advancement observable without changing the original request identity.
    return entry.serverSeq <= winner.serverSeq;
}

QString syncMutationFingerprint(
    const SyncWireMutation &mutation) {
    const QByteArray bytes =
        QJsonDocument(syncWireMutationToJson(mutation))
            .toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(
            bytes,
            QCryptographicHash::Sha256)
            .toHex());
}

bool isDurableCompatibilityFailure(
    const SyncAdapterRegistryError &error) {
    return error.failureClass
        == SyncAdapterFailureClass::Compatibility;
}

bool hasDurableSyncWarning(
    const SyncPersistentState &state) {
    return !state.rejectedMutations.isEmpty()
        || !state.quarantinedEntries.isEmpty()
        || !state.ownerRedos.isEmpty()
        || state.historicalReplayPending;
}
}

SyncEngine::SyncEngine(
    AccountClient *client,
    SyncAdapterRegistry *registry,
    std::function<qint64()> nowProvider,
    QObject *parent)
    : QObject(parent),
      m_client(client),
      m_registry(registry),
      m_nowProvider(
          nowProvider
              ? std::move(nowProvider)
              : []() {
                    return QDateTime::
                        currentMSecsSinceEpoch();
                }) {
    Q_ASSERT(m_client);
    Q_ASSERT(m_registry);

    setObjectName(
        QStringLiteral("syncEngine"));

    qRegisterMetaType<
        SyncEngine::State>();

    m_retryTimer.setSingleShot(
        true);

    connect(
        &m_retryTimer,
        &QTimer::timeout,
        this,
        [this]() {
            if (!m_active
                || !m_networkEnabled
                || m_state
                    == State::Blocked) {
                return;
            }

            clearError();
            m_initialPullPending = true;
            maybeRunNetwork();
        });

    connect(
        m_registry,
        &SyncAdapterRegistry::
            localMutationAvailable,
        this,
        &SyncEngine::
            handleLocalMutation);

    connect(
        m_registry,
        &SyncAdapterRegistry::
            adapterRegistered,
        this,
        [this](
            const QString &categoryId) {
            if (!m_active)
                return;

            QString error;
            if (!reconcileCategory(
                    categoryId,
                    &error)) {
                setBlocked(
                    QStringLiteral(
                        "adapter_snapshot_failed"),
                    error);
                return;
            }

            if (m_state == State::Blocked
                && (m_lastErrorCode
                        == QLatin1String(
                            "adapter_not_registered")
                    || m_lastErrorCode
                        == QLatin1String(
                            "adapter_snapshot_failed")
                    || m_lastErrorCode
                        == QLatin1String(
                            "unsupported_schema_version")
                    || m_lastErrorCode
                        == QLatin1String("adapter_unregistered")
                    || m_lastErrorCode
                        == QLatin1String("adapter_destroyed"))) {
                clearError();
                setState(State::Idle);
            }

            QString replayCode;
            QString replayMessage;
            if (!replayQuarantinedCategory(
                    categoryId,
                    &replayCode,
                    &replayMessage)) {
                setBlocked(
                    replayCode.isEmpty()
                        ? QStringLiteral("adapter_apply_failed")
                        : replayCode,
                    replayMessage.isEmpty()
                        ? QStringLiteral("A quarantined sync record could not be applied safely.")
                        : replayMessage);
                return;
            }
            if (!replayCode.isEmpty()) {
                m_lastErrorCode = replayCode;
                m_lastErrorMessage = replayMessage;
                setState(State::Idle);
            }

            persistState();
        });

    connect(
        m_client,
        &AccountClient::completed,
        this,
        &SyncEngine::
            handleClientCompleted);

    connect(
        &m_stateStore,
        &SyncStateStore::
            persistenceCommitted,
        this,
        &SyncEngine::
            handlePersistenceCommitted);

    connect(
        &m_stateStore,
        &SyncStateStore::
            persistenceFailed,
        this,
        &SyncEngine::
            handlePersistenceFailed);
}

bool SyncEngine::start(
    const ProfilePaths &profile,
    const QString &deviceId,
    QString *error) {
    if (profile.kind()
            != ProfilePaths::Kind::Account
        || profile.syncStatePath().isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "Sync requires an active account profile.");
        }
        return false;
    }

    const QString normalizedDevice =
        normalizedUuid(deviceId);
    if (normalizedDevice.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "Sync requires a valid account device id.");
        }
        return false;
    }

    if (m_active) {
        QString stopError;
        if (!stopPreservingOutbox(
                &stopError)) {
            if (error)
                *error = stopError;
            return false;
        }
    }

    QString loadError;
    const auto loaded =
        m_stateStore.load(
            profile.syncStatePath(),
            &loadError);
    if (!loaded.has_value()) {
        if (error)
            *error = loadError;
        return false;
    }

    m_profile = profile;
    m_statePath =
        profile.syncStatePath();
    m_deviceId =
        normalizedDevice;
    m_persistent =
        *loaded;
    ++m_profileGeneration;

    m_disabledCategories = m_requestedDisabledCategories;
    m_categoryReplayInProgress.clear();
    m_categoryReplayQueue.clear();
    QStringList replayCategories = m_persistent.pausedCategories.keys();
    replayCategories.sort();
    for (const QString &category : replayCategories) {
        const auto pausedIt = m_persistent.pausedCategories.constFind(category);
        if (pausedIt == m_persistent.pausedCategories.constEnd()
            || !pausedIt->replaying
            || m_disabledCategories.contains(category))
            continue;
        m_disabledCategories.insert(category);
        if (m_categoryReplayInProgress.isEmpty())
            m_categoryReplayInProgress = category;
        else
            m_categoryReplayQueue.append(category);
    }

    if (!validateLoadedState(
            &loadError)) {
        m_profile =
            ProfilePaths::sealed();
        m_statePath.clear();
        m_deviceId.clear();
        if (error)
            *error = loadError;
        return false;
    }

    for (const QString &category : std::as_const(m_requestedDisabledCategories)) {
        if (m_persistent.pausedCategories.contains(category))
            continue;

        SyncAdapterSnapshot snapshot;
        SyncAdapterRegistryError registryError;
        if (!m_registry->exportSnapshot(category, &snapshot, &registryError)) {
            m_profile = ProfilePaths::sealed();
            m_statePath.clear();
            m_deviceId.clear();
            if (error) {
                *error = registryError.detail.isEmpty()
                    ? registryError.code
                    : registryError.detail;
            }
            return false;
        }

        SyncPausedCategoryState paused;
        QSet<QString> tombstones;
        for (const QString &recordKey : snapshot.tombstones)
            tombstones.insert(recordKey);
        for (const SyncAdapterRecord &record : snapshot.records) {
            if (tombstones.contains(record.recordKey))
                continue;
            paused.localBaseline.insert(
                record.recordKey,
                SyncMirrorRecord{snapshot.schemaVersion, record.payload});
        }
        m_persistent.pausedCategories.insert(category, paused);
    }

    m_clock.setDeviceId(
        m_deviceId);
    m_clock.restore(
        m_persistent.hlcPhysicalMs,
        m_persistent.hlcCounter,
        m_persistent.serverOffsetMs);

    m_retryTimer.stop();
    m_request = {};
    m_pendingPersistenceGenerations.clear();
    m_persistenceCallbacks.clear();
    m_pullProcessing.reset();
    m_ownerApply.reset();
    m_ownerRedoRecoveryInProgress = false;
    m_ownerRedoBatchReady = false;
    m_ownerRedoBatchPreparing = false;
    m_startFinalizationPending = false;
    m_quarantineReplayCategory.clear();
    m_quarantineReplayRunning = false;
    m_quarantineReplaySkipped.clear();
    m_quarantineReplayCategories.clear();
    m_networkBusy = false;
    m_active = true;
    m_initialPullPending = true;
    m_pullHasMore = false;
    m_signOutFlushRequested = false;
    m_retryAttempt = 0;

    if (!hasDurableSyncWarning(m_persistent))
        clearError();
    setState(State::Idle);

    if (!m_persistent.ownerRedos.isEmpty()) {
        m_ownerRedoRecoveryInProgress = true;
        m_startFinalizationPending = true;
        beginOwnerRedoRecovery();
        emit recoveryAvailableChanged();
        return true;
    }

    finishStartAfterOwnerRedo();
    if (m_state == State::Blocked && error)
        *error = m_lastErrorMessage;
    emit recoveryAvailableChanged();
    return m_state != State::Blocked;
}

void SyncEngine::finishStartAfterOwnerRedo() {
    if (!m_active)
        return;

    m_ownerRedoRecoveryInProgress = false;
    m_startFinalizationPending = false;

    QString reconcileError;
    if (!reconcileAllAdapters(&reconcileError)) {
        setBlocked(QStringLiteral("adapter_snapshot_failed"), reconcileError);
        return;
    }

    for (const QString &category : m_registry->registeredCategories()) {
        QString replayCode;
        QString replayMessage;
        if (!replayQuarantinedCategory(category, &replayCode, &replayMessage)) {
            setBlocked(
                replayCode.isEmpty() ? QStringLiteral("adapter_apply_failed") : replayCode,
                replayMessage.isEmpty()
                    ? QStringLiteral("A quarantined sync record could not be applied safely.")
                    : replayMessage);
            return;
        }
        if (!replayCode.isEmpty()) {
            m_lastErrorCode = replayCode;
            m_lastErrorMessage = replayMessage;
        }
    }

    persistState();
    emit recoveryAvailableChanged();
}

bool SyncEngine::stopPreservingOutbox(
    QString *error) {
    if (!m_active) {
        clearError();
        setState(State::Inactive);
        return true;
    }

    m_retryTimer.stop();
    m_networkEnabled = false;
    m_signOutFlushRequested = false;
    ++m_profileGeneration;

    QString reconcileError;
    const bool ownerTransactionInFlight =
        m_ownerApply.has_value()
        || m_ownerRedoRecoveryInProgress
        || m_ownerRedoBatchPreparing
        || m_pullProcessing.has_value()
        || m_quarantineReplayRunning;
    const bool reconciled = ownerTransactionInFlight
        ? true
        : reconcileAllAdapters(&reconcileError);

    if (!m_statePath.isEmpty())
        persistState();

    QString flushError;
    const bool persisted =
        m_stateStore.flush(
            &flushError);

    m_pendingPersistenceGenerations.clear();
    m_persistenceCallbacks.clear();
    m_pullProcessing.reset();
    m_ownerApply.reset();
    m_ownerRedoRecoveryInProgress = false;
    m_ownerRedoBatchReady = false;
    m_ownerRedoBatchPreparing = false;
    m_startFinalizationPending = false;
    m_quarantineReplayCategory.clear();
    m_quarantineReplayRunning = false;
    m_quarantineReplaySkipped.clear();
    m_quarantineReplayCategories.clear();
    m_networkBusy = false;
    m_request = {};
    m_initialPullPending = false;
    m_pullHasMore = false;
    m_active = false;
    m_statePath.clear();
    m_deviceId.clear();
    m_profile =
        ProfilePaths::sealed();
    setState(State::Inactive);

    if (!reconciled
        || !persisted) {
        const QString message =
            !reconciled
            ? reconcileError
            : flushError;

        m_lastErrorCode =
            !reconciled
            ? QStringLiteral(
                  "adapter_snapshot_failed")
            : QStringLiteral(
                  "sync_persistence_failed");
        m_lastErrorMessage =
            message.isEmpty()
            ? QStringLiteral(
                  "Sync state could not be preserved completely before profile teardown.")
            : message;

        if (error)
            *error = m_lastErrorMessage;
        return false;
    }

    clearError();
    return true;
}

void SyncEngine::requestImmediateSync() {
    if (!m_active
        || !m_networkEnabled) {
        return;
    }

    if (m_state == State::Blocked) {
        const bool adapterMayHaveChanged =
            m_lastErrorCode
                == QLatin1String(
                    "adapter_not_registered")
            || m_lastErrorCode
                == QLatin1String(
                    "adapter_snapshot_failed")
            || m_lastErrorCode
                == QLatin1String(
                    "unsupported_schema_version")
            || m_lastErrorCode
                == QLatin1String("adapter_unregistered")
            || m_lastErrorCode
                == QLatin1String("adapter_destroyed");

        if (!adapterMayHaveChanged)
            return;

        clearError();
        setState(State::Idle);
    }

    m_retryTimer.stop();
    m_initialPullPending = true;
    maybeRunNetwork();
}

void SyncEngine::retryRejectedMutations() {
    if (!m_active)
        return;

    const bool hasRejectedMutations =
        !m_persistent.rejectedMutations.isEmpty();
    const bool ownerRecoveryBlocked =
        m_state == State::Blocked
        && (m_lastErrorCode == QLatin1String("adapter_apply_failed")
            || m_lastErrorCode == QLatin1String("sync_persistence_failed")
            || m_lastErrorCode == QLatin1String("adapter_unregistered")
            || m_lastErrorCode == QLatin1String("adapter_destroyed"));
    if (!hasRejectedMutations
        && !ownerRecoveryBlocked
        && m_persistent.ownerRedos.isEmpty())
        return;

    // This is intentionally an explicit, bounded recovery action. Ordinary
    // pulls and the idle timer leave rejected records parked so one old server
    // capability cannot create a retry loop. The marker is removed before the
    // retry is scheduled and persisted together with the existing outbox.
    m_persistent.rejectedMutations.clear();
    m_retryTimer.stop();
    clearError();
    setState(State::Idle);

    // A durable owner redo is the explicit recovery handle for a disk or
    // owner-lifecycle failure. Retry it before allowing network work again;
    // this keeps the existing sync-repair affordance from leaving healthy
    // categories behind a permanent global Blocked state.
    if (!m_persistent.ownerRedos.isEmpty()
        && !m_ownerApply.has_value()
        && !m_ownerRedoRecoveryInProgress
        && !m_pullProcessing.has_value()
        && !m_quarantineReplayRunning) {
        m_ownerRedoRecoveryInProgress = true;
        m_startFinalizationPending = true;
        beginOwnerRedoRecovery();
    } else {
        m_initialPullPending = true;
        persistState();
    }
    emit recoveryAvailableChanged();
}

void SyncEngine::beginSignOutFlush() {
    if (!m_active) {
        emit signOutFlushFinished(
            true,
            QString(),
            QString());
        return;
    }

    m_signOutFlushRequested = true;
    m_retryTimer.stop();

    QString reconcileError;
    const bool ownerTransactionInFlight =
        m_ownerApply.has_value()
        || m_ownerRedoRecoveryInProgress
        || m_ownerRedoBatchPreparing
        || m_pullProcessing.has_value()
        || m_quarantineReplayRunning;
    if (!ownerTransactionInFlight
        && !reconcileAllAdapters(&reconcileError)) {
        m_signOutFlushRequested = false;
        setBlocked(
            QStringLiteral(
                "adapter_snapshot_failed"),
            reconcileError);

        emit signOutFlushFinished(
            false,
            QStringLiteral(
                "adapter_snapshot_failed"),
            reconcileError);
        return;
    }

    if (m_state == State::Blocked
        && (m_lastErrorCode
                == QLatin1String(
                    "sync_persistence_failed")
            || m_lastErrorCode
                == QLatin1String(
                    "adapter_snapshot_failed"))) {
        clearError();
        setState(State::Idle);
    }

    persistState();
}

bool SyncEngine::beginAttachmentMode(
    const QString &attachmentId,
    QString *error) {
    if (!m_active) {
        if (error) {
            *error = QStringLiteral(
                "Attachment mode requires a running sync engine.");
        }
        return false;
    }

    if (m_persistent.attachmentModeActive) {
        if (error) {
            *error = QStringLiteral(
                "An attachment mode is already active.");
        }
        return false;
    }

    const QString normalized =
        normalizedUuid(attachmentId);
    if (normalized.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "Attachment mode requires a valid attachment id.");
        }
        return false;
    }

    m_persistent.attachmentModeActive = true;
    m_persistent.attachmentId = normalized;
    m_persistent.attachmentMutationIds.clear();
    m_persistent.attachmentSnapshotDone = false;
    m_persistent.attachmentSnapshotNextPageToken.clear();

    persistState();
    requestImmediateSync();
    return true;
}

bool SyncEngine::endAttachmentMode(
    QString *error) {
    if (!m_active) {
        if (error) {
            *error = QStringLiteral(
                "Attachment mode requires a running sync engine.");
        }
        return false;
    }

    if (!m_persistent.attachmentModeActive) {
        if (error) {
            *error = QStringLiteral(
                "No attachment mode is active.");
        }
        return false;
    }

    // A snapshot request in flight belongs to the mode being exited;
    // its late reply no longer matches any request context and is
    // dropped when it arrives.
    if (m_request.phase == NetworkPhase::Snapshot) {
        m_request = {};
        m_networkBusy = false;
    }

    m_persistent.attachmentModeActive = false;
    m_persistent.attachmentId.clear();
    m_persistent.attachmentMutationIds.clear();
    m_persistent.attachmentSnapshotDone = false;
    m_persistent.attachmentSnapshotNextPageToken.clear();
    m_initialPullPending = true;

    persistState();
    requestImmediateSync();
    return true;
}

bool SyncEngine::attachmentModeActive() const {
    return m_persistent.attachmentModeActive;
}

bool SyncEngine::attachmentSnapshotComplete() const {
    return m_persistent.attachmentModeActive
        && m_persistent.attachmentSnapshotDone;
}

QString SyncEngine::attachmentId() const {
    return m_persistent.attachmentId;
}

QList<SyncWireAttachmentManifestItem> SyncEngine::attachmentManifest(
    QString *error) const {
    return attachmentManifest({}, error);
}

QList<SyncWireAttachmentManifestItem> SyncEngine::attachmentManifest(
    const QSet<QString> &excludedMutationIds,
    QString *error) const {
    if (error)
        error->clear();
    if (!m_active) {
        if (error)
            *error = QStringLiteral(
                "Attachment manifest requires a running sync engine.");
        return {};
    }

    QList<SyncWireAttachmentManifestItem> result;
    result.reserve(qMin<qsizetype>(m_persistent.outbox.size(), kPushBatchLimit));
    QJsonArray wireItems;
    for (const SyncWireMutation &mutation : std::as_const(m_persistent.outbox)) {
        if (excludedMutationIds.contains(mutation.mutationId))
            continue;
        const SyncWireAttachmentManifestItem item{
            mutation, syncWireCanonicalPayloadHash(mutation)};
        QJsonArray candidate = wireItems;
        candidate.append(syncWireAttachmentManifestItemToJson(item));
        if (candidate.size() > kPushBatchLimit
            || QJsonDocument(candidate).toJson(QJsonDocument::Compact).size()
                   > kAttachmentManifestWireByteLimit) {
            if (result.isEmpty() && error) {
                *error = QStringLiteral(
                    "One attachment mutation is too large for the bounded manifest request.");
            }
            break;
        }
        wireItems = candidate;
        result.append(item);
    }
    return result;
}

bool SyncEngine::enqueueAttachmentMutations(
    const QList<SyncWireAttachmentManifestItem> &items,
    QStringList *acceptedMutationIds,
    QString *error) {
    if (acceptedMutationIds)
        acceptedMutationIds->clear();

    if (!m_active || !m_persistent.attachmentModeActive) {
        if (error)
            *error = QStringLiteral(
                "Attachment enqueue requires active attachment mode.");
        return false;
    }
    if (items.isEmpty() || items.size() > kPushBatchLimit) {
        if (error)
            *error = QStringLiteral(
                "Attachment manifest must contain between 1 and 100 mutations.");
        return false;
    }

    QSet<QString> requestIds;
    for (const SyncWireAttachmentManifestItem &item : items) {
        const SyncWireMutation &mutation = item.mutation;
        if (normalizedUuid(mutation.mutationId) != mutation.mutationId
            || normalizedUuid(mutation.deviceId) != m_deviceId
            || mutation.hlc.deviceId != mutation.deviceId
            || mutation.category.isEmpty()
            || mutation.category != mutation.category.trimmed().toLower()
            || !isValidSyncWireRecordKey(mutation.recordKey)
            || mutation.schemaVersion <= 0
            || mutation.hlc.physicalMs < 0
            || mutation.materializedHlc.has_value()
            || (mutation.operation == SyncWireOperation::Put
                && mutation.payload.isUndefined())
            || (mutation.operation == SyncWireOperation::Delete
                && !mutation.payload.isUndefined()
                && !mutation.payload.isNull())
            || requestIds.contains(mutation.mutationId)) {
            if (error)
                *error = QStringLiteral(
                    "Attachment manifest contains an invalid or mismatched mutation.");
            return false;
        }
        const QByteArray expectedHash =
            syncWireCanonicalPayloadHash(mutation);
        if (!item.canonicalPayloadHash.isEmpty()
            && item.canonicalPayloadHash != expectedHash) {
            if (error)
                *error = QStringLiteral(
                    "Attachment manifest payload hash does not match its mutation.");
            return false;
        }
        requestIds.insert(mutation.mutationId);

        for (const SyncWireMutation &existing : std::as_const(m_persistent.outbox)) {
            if (existing.mutationId != mutation.mutationId)
                continue;
            const QByteArray existingBytes =
                QJsonDocument(syncWireMutationToJson(existing))
                    .toJson(QJsonDocument::Compact);
            const QByteArray incomingBytes =
                QJsonDocument(syncWireMutationToJson(mutation))
                    .toJson(QJsonDocument::Compact);
            if (existingBytes != incomingBytes) {
                if (error)
                    *error = QStringLiteral(
                        "Attachment mutation id is already bound to different content.");
                return false;
            }
        }
    }

    const SyncPersistentState previous = m_persistent;
    m_persistent.attachmentMutationIds = requestIds.values();
    m_persistent.attachmentMutationIds.sort();
    for (const SyncWireAttachmentManifestItem &item : items) {
        const SyncWireMutation &mutation = item.mutation;
        bool alreadyPresent = false;
        for (const SyncWireMutation &existing : std::as_const(m_persistent.outbox)) {
            if (existing.mutationId == mutation.mutationId) {
                alreadyPresent = true;
                break;
            }
        }
        if (alreadyPresent) {
            if (acceptedMutationIds)
                acceptedMutationIds->append(mutation.mutationId);
            continue;
        }

        m_clock.observe(mutation.hlc, nowMs());
        m_persistent.outbox.append(mutation);
        SyncWinner winner;
        winner.hlc = mutation.hlc;
        winner.schemaVersion = mutation.schemaVersion;
        winner.operation = mutation.operation;
        m_persistent.winners[mutation.category].insert(
            mutation.recordKey, winner);
        if (mutation.operation == SyncWireOperation::Put) {
            m_persistent.mirrors[mutation.category].insert(
                mutation.recordKey,
                SyncMirrorRecord{mutation.schemaVersion, mutation.payload});
        } else {
            m_persistent.mirrors[mutation.category].remove(mutation.recordKey);
        }
        if (acceptedMutationIds)
            acceptedMutationIds->append(mutation.mutationId);
    }

    persistClockIntoState();
    const quint64 generation = persistState();
    QString persistenceError;
    if (generation == 0 || !m_stateStore.flush(&persistenceError)) {
        m_persistent = previous;
        if (error)
            *error = persistenceError.isEmpty()
                ? QStringLiteral("Attachment mutations could not be persisted safely.")
                : persistenceError;
        if (acceptedMutationIds)
            acceptedMutationIds->clear();
        return false;
    }

    emit observationChanged(m_state, pendingOutboxCount());
    return true;
}

void SyncEngine::setAutomaticSchedulingEnabled(
    bool enabled) {
    m_automaticSchedulingEnabled =
        enabled;

    if (!enabled)
        m_retryTimer.stop();
}

void SyncEngine::setNetworkEnabled(
    bool enabled) {
    if (m_networkEnabled == enabled)
        return;

    m_networkEnabled = enabled;

    if (!enabled) {
        m_retryTimer.stop();
        return;
    }

    if (m_active
        && m_state
            != State::Blocked) {
        requestImmediateSync();
    }
}

void SyncEngine::setCategoryNetworkEnabled(
    const QString &categoryId,
    bool enabled) {
    const QString category = categoryId.trimmed().toLower();
    if (category.isEmpty())
        return;

    if (!enabled)
        m_requestedDisabledCategories.insert(category);
    else
        m_requestedDisabledCategories.remove(category);

    if (!m_active)
        return;

    if (!enabled) {
        if (m_disabledCategories.contains(category))
            return;
        SyncAdapterSnapshot snapshot;
        SyncAdapterRegistryError registryError;
        if (!m_registry->exportSnapshot(category, &snapshot, &registryError)) {
            setBlocked(QStringLiteral("adapter_snapshot_failed"),
                       registryError.detail.isEmpty() ? registryError.code : registryError.detail);
            return;
        }
        SyncPausedCategoryState paused;
        QSet<QString> tombstones;
        for (const QString &recordKey : snapshot.tombstones)
            tombstones.insert(recordKey);
        for (const SyncAdapterRecord &record : snapshot.records) {
            if (tombstones.contains(record.recordKey))
                continue;
            paused.localBaseline.insert(record.recordKey,
                SyncMirrorRecord{snapshot.schemaVersion, record.payload});
        }
        m_disabledCategories.insert(category);
        for (int index = m_persistent.outbox.size() - 1; index >= 0; --index) {
            if (m_persistent.outbox.at(index).category == category) {
                paused.pendingMutations.append(m_persistent.outbox.at(index));
                m_persistent.outbox.removeAt(index);
            }
        }
        std::reverse(paused.pendingMutations.begin(), paused.pendingMutations.end());
        m_persistent.pausedCategories.insert(category, paused);
        emit observationChanged(m_state, pendingOutboxCount());
        persistState();
        return;
    }

    if (!m_disabledCategories.contains(category))
        return;
    const auto pausedIt = m_persistent.pausedCategories.constFind(category);
    if (pausedIt == m_persistent.pausedCategories.constEnd())
        return;
    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError registryError;
    if (!m_registry->exportSnapshot(category, &snapshot, &registryError)) {
        setBlocked(QStringLiteral("adapter_snapshot_failed"),
                   registryError.detail.isEmpty() ? registryError.code : registryError.detail);
        return;
    }
    SyncPausedCategoryState replay = pausedIt.value();
    replay.localOverlay.clear();
    QSet<QString> tombstones;
    for (const QString &recordKey : snapshot.tombstones)
        tombstones.insert(recordKey);
    QHash<QString, SyncAdapterRecord> current;
    for (const SyncAdapterRecord &record : snapshot.records) {
        if (tombstones.contains(record.recordKey))
            continue;
        current.insert(record.recordKey, record);
    }
    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        const auto old = replay.localBaseline.constFind(it.key());
        if (old == replay.localBaseline.constEnd()
            || old->schemaVersion != snapshot.schemaVersion
            || old->payload != it->payload)
            replay.localOverlay.insert(it.key(), SyncPausedOverlayRecord{
                SyncWireOperation::Put, snapshot.schemaVersion, it->payload, it->localOrderMs});
    }
    if (snapshot.missingRecordsAreDeletes) {
        for (auto it = replay.localBaseline.constBegin();
             it != replay.localBaseline.constEnd();
             ++it) {
            if (!current.contains(it.key())) {
                replay.localOverlay.insert(it.key(), SyncPausedOverlayRecord{
                    SyncWireOperation::Delete, snapshot.schemaVersion, QJsonValue(), -1});
            }
        }
    }
    for (const QString &recordKey : tombstones) {
        replay.localOverlay.insert(recordKey, SyncPausedOverlayRecord{
            SyncWireOperation::Delete, snapshot.schemaVersion, QJsonValue(), -1});
    }
    replay.replaying = true;
    m_persistent.pausedCategories.insert(category, replay);
    m_disabledCategories.insert(category);
    const bool startReplay = m_categoryReplayInProgress.isEmpty();
    if (startReplay) {
        m_categoryReplayInProgress = category;
        m_persistent.winners.remove(category);
        m_persistent.mirrors.remove(category);
        m_persistent.cursor = 0;
        m_initialPullPending = true;
        m_pullHasMore = false;
    } else if (!m_categoryReplayQueue.contains(category)) {
        m_categoryReplayQueue.append(category);
    }
    persistState();
    if (startReplay)
        requestImmediateSync();
}

bool SyncEngine::categoryNetworkEnabled(const QString &categoryId) const {
    const QString category = categoryId.trimmed().toLower();
    return !m_disabledCategories.contains(category)
        && !m_requestedDisabledCategories.contains(category);
}

SyncEngine::State SyncEngine::state() const {
    return m_state;
}

QString SyncEngine::stateName() const {
    return stateName(m_state);
}

int SyncEngine::pendingOutboxCount() const {
    return static_cast<int>(
        qMin<qsizetype>(
            m_persistent.outbox.size(),
            std::numeric_limits<int>::max()));
}

int SyncEngine::pendingAttachmentOutboxCount() const {
    if (!m_persistent.attachmentModeActive)
        return 0;
    const QSet<QString> attachmentIds(
        m_persistent.attachmentMutationIds.cbegin(),
        m_persistent.attachmentMutationIds.cend());
    qsizetype count = 0;
    for (const SyncWireMutation &mutation : std::as_const(m_persistent.outbox)) {
        if (attachmentIds.contains(mutation.mutationId))
            ++count;
    }
    return static_cast<int>(qMin<qsizetype>(
        count, std::numeric_limits<int>::max()));
}

quint64 SyncEngine::cursor() const {
    return m_persistent.cursor;
}

bool SyncEngine::active() const {
    return m_active;
}

QString SyncEngine::lastErrorCode() const {
    return m_lastErrorCode;
}

QString SyncEngine::lastErrorMessage() const {
    return m_lastErrorMessage;
}

int SyncEngine::quarantinedEntryCount() const {
    return static_cast<int>(
        qMin<qsizetype>(
            m_persistent.quarantinedEntries.size(),
            std::numeric_limits<int>::max()));
}

int SyncEngine::rejectedMutationCount() const {
    return static_cast<int>(
        qMin<qsizetype>(
            m_persistent.rejectedMutations.size(),
            std::numeric_limits<int>::max()));
}

bool SyncEngine::historicalReplayPending() const {
    return m_persistent.historicalReplayPending;
}

bool SyncEngine::recoveryAvailable() const {
    return hasDurableSyncWarning(m_persistent);
}

void SyncEngine::handleClientCompleted(
    quint64 requestId,
    AccountOperation operation,
    quint64 accessTokenGeneration,
    const AccountTransportReply &reply) {
    if (!m_active
        || requestId == 0
        || requestId
            != m_request.requestId) {
        return;
    }

    const NetworkPhase phase =
        m_request.phase;

    if ((phase == NetworkPhase::Pull
         && operation
             != AccountOperation::SyncPull)
        || (phase == NetworkPhase::Push
            && operation
                != AccountOperation::SyncPush)
        || (phase == NetworkPhase::Snapshot
            && operation
                != AccountOperation::
                    SyncSnapshot)) {
        return;
    }

    const qint64 sentLocalMs =
        m_request.sentLocalMs;
    const qint64 receivedLocalMs =
        nowMs();

    if (reply.errorCode
            == QLatin1String(
                "session_revoked")
        || reply.errorCode
            == QLatin1String(
                "session_invalid")) {
        m_networkBusy = false;
        m_request = {};

        if (accessTokenGeneration != 0
            && accessTokenGeneration
                != m_client->accessTokenGeneration()) {
            clearError();
            setState(State::Idle);
            m_initialPullPending = true;
            maybeRunNetwork();
            return;
        }

        setNetworkEnabled(false);
        setRetrying(
            reply.errorCode,
            QStringLiteral(
                "Sync is waiting for account authentication to recover."));
        emit accessTokenRejected();
        return;
    }

    if (reply.networkError) {
        m_networkBusy = false;
        m_request = {};

        setRetrying(
            QStringLiteral("offline"),
            QStringLiteral(
                "Sync will retry when the account service is reachable."));

        if (m_signOutFlushRequested) {
            m_signOutFlushRequested = false;
            emit signOutFlushFinished(
                false,
                QStringLiteral("offline"),
                QStringLiteral(
                    "Some changes haven't synced."));
        }
        return;
    }

    if (!isSuccess(reply)
        && reply.statusCode >= 500
        && reply.statusCode < 600) {
        m_networkBusy = false;
        m_request = {};

        setRetrying(
            QStringLiteral("service_unavailable"),
            QStringLiteral(
                "Sync will retry when the account service is reachable."));

        if (m_signOutFlushRequested) {
            m_signOutFlushRequested = false;
            emit signOutFlushFinished(
                false,
                QStringLiteral("service_unavailable"),
                QStringLiteral(
                    "Some changes haven't synced."));
        }
        return;
    }

    if (!isSuccess(reply)) {
        m_networkBusy = false;
        m_request = {};

        setBlocked(
            reply.errorCode.isEmpty()
                ? QStringLiteral(
                      "sync_service_error")
                : reply.errorCode,
            reply.errorMessage.isEmpty()
                ? QStringLiteral(
                      "Sync needs attention.")
                : reply.errorMessage);

        if (m_signOutFlushRequested) {
            m_signOutFlushRequested = false;
            emit signOutFlushFinished(
                false,
                m_lastErrorCode,
                m_lastErrorMessage);
        }
        return;
    }

    const auto serviceTime =
        signedJsonInteger(
            reply.body.value(
                QStringLiteral(
                    "server_time_ms")));
    if (serviceTime.has_value()) {
        m_clock.observeServiceTime(
            *serviceTime,
            sentLocalMs,
            receivedLocalMs);
    }

    QString errorCode;
    QString errorMessage;
    bool processed = false;

    if (phase == NetworkPhase::Snapshot) {
        processed =
            processSnapshotReply(
                reply,
                &errorCode,
                &errorMessage);
    } else if (phase == NetworkPhase::Pull) {
        processed =
            processPullReply(
                reply,
                &errorCode,
                &errorMessage);
    } else {
        processed =
            processPushReply(
                reply,
                &errorCode,
                &errorMessage);

        if (processed)
            m_initialPullPending = true;
    }

    if (phase == NetworkPhase::Pull
        && m_pullProcessing.has_value()) {
        // The transport reply has been parsed, but a remote owner still has
        // to acknowledge durable commit. Keep the engine busy and let the
        // owner receipt finish this request through finishPullProcessing().
        m_request = {};
        return;
    }

    m_networkBusy = false;
    m_request = {};

    if (!processed) {
        setBlocked(
            errorCode.isEmpty()
                ? QStringLiteral(
                      "sync_protocol_error")
                : errorCode,
            errorMessage.isEmpty()
                ? QStringLiteral(
                      "Sync needs attention.")
                : errorMessage);

        if (m_signOutFlushRequested) {
            m_signOutFlushRequested = false;
            emit signOutFlushFinished(
                false,
                m_lastErrorCode,
                m_lastErrorMessage);
        }
        return;
    }

    m_retryAttempt = 0;
    if (!errorCode.isEmpty()) {
        m_lastErrorCode = errorCode;
        m_lastErrorMessage = errorMessage;
    } else if (!hasDurableSyncWarning(m_persistent)) {
        clearError();
    }
    setState(State::Idle);
    persistState();
    emit recoveryAvailableChanged();
}

void SyncEngine::handleLocalMutation(
    const QString &categoryId,
    quint64 revision) {
    Q_UNUSED(revision);

    if (!m_active)
        return;

    if (m_disabledCategories.contains(categoryId)) {
        persistState();
        emit recoveryAvailableChanged();
        return;
    }

    QString error;
    // Union/merge during attachment replay: while the stable snapshot
    // bootstrap is running, records that exist in the mirror but are
    // missing locally are imports the account already owns — never
    // inferred deletes, even for delete-capable adapters.
    if (!reconcileCategory(
            categoryId,
            &error,
            !attachmentSnapshotPending())) {
        setBlocked(
            QStringLiteral(
                "adapter_snapshot_failed"),
            error);
        return;
    }

    persistState();
    emit recoveryAvailableChanged();
}

bool SyncEngine::validateLoadedState(
    QString *error) const {
    if (m_persistent.historicalReplayPending
        && (m_persistent.historicalReplayLimit == 0
            || m_persistent.historicalReplayCursor
                > m_persistent.historicalReplayLimit)) {
        if (error)
            *error = QStringLiteral("The historical replay checkpoint is invalid.");
        return false;
    }

    for (auto it = m_persistent.rejectedMutations.constBegin();
         it != m_persistent.rejectedMutations.constEnd();
         ++it) {
        const SyncRejectedMutation &marker = it.value();
        if (it.key() != marker.mutationId
            || marker.mutationId.isEmpty()
            || marker.category.isEmpty()
            || !isValidSyncWireRecordKey(marker.recordKey)
            || marker.code.isEmpty()
            || marker.fingerprint.isEmpty()) {
            if (error)
                *error = QStringLiteral("The durable rejected-mutation state is invalid.");
            return false;
        }
    }
    QSet<quint64> ownerRedoSequences;
    for (const SyncOwnerRedo &redo : m_persistent.ownerRedos) {
        if (redo.serverSeq == 0
            || ownerRedoSequences.contains(redo.serverSeq)
            || !redo.won
            || redo.mutation.mutationId.isEmpty()
            || redo.mutation.category.isEmpty()
            || !isValidSyncWireRecordKey(redo.mutation.recordKey)
            || redo.mutation.schemaVersion <= 0
            || (redo.mutation.operation == SyncWireOperation::Put
                && !redo.mutation.payload.isObject())
            || (redo.mutation.operation == SyncWireOperation::Delete
                && !redo.mutation.payload.isUndefined()
                && !redo.mutation.payload.isNull())) {
            if (error)
                *error = QStringLiteral("The durable owner redo state is invalid.");
            return false;
        }
        if (redo.mutation.operation == SyncWireOperation::Put) {
            const SyncPayloadValidation validation =
                SyncPayloadFirewall::validate(
                    redo.mutation.category,
                    redo.mutation.payload);
            if (!validation.allowed) {
                if (error)
                    *error = validation.detail;
                return false;
            }
        }
        const SyncOwnershipEntry *entry =
            SyncOwnershipInventory::find(redo.mutation.category);
        if (!entry
            || entry->disposition != SyncDisposition::Syncable
            || entry->ownerStatus != SyncOwnerStatus::Confirmed
            || !entry->ordinaryPayloadEligible) {
            if (error)
                *error = QStringLiteral("The durable owner redo contains a category that is not eligible for ordinary sync.");
            return false;
        }
        ownerRedoSequences.insert(redo.serverSeq);
    }

    QSet<quint64> quarantineSequences;
    for (const SyncQuarantineEntry &entry :
         m_persistent.quarantinedEntries) {
        if (entry.serverSeq == 0
            || quarantineSequences.contains(entry.serverSeq)
            || entry.code.isEmpty()
            || entry.mutation.mutationId.isEmpty()
            || entry.mutation.category.isEmpty()
            || !isValidSyncWireRecordKey(entry.mutation.recordKey)) {
            if (error)
                *error = QStringLiteral("The durable sync quarantine state is invalid.");
            return false;
        }
        quarantineSequences.insert(entry.serverSeq);
    }

    for (const SyncWireMutation &mutation :
         m_persistent.outbox) {
        if (mutation.deviceId
            != m_deviceId) {
            if (error) {
                *error = QStringLiteral(
                    "The pending sync outbox belongs to a different device identity.");
            }
            return false;
        }

        const SyncOwnershipEntry *entry =
            SyncOwnershipInventory::find(
                mutation.category);
        if (!entry
            || entry->disposition
                != SyncDisposition::Syncable
            || entry->ownerStatus
                != SyncOwnerStatus::Confirmed
            || !entry->ordinaryPayloadEligible) {
            if (error) {
                *error = QStringLiteral(
                    "The pending sync outbox contains a category that is not eligible for ordinary sync.");
            }
            return false;
        }

        if (mutation.operation
            == SyncWireOperation::Put) {
            const SyncPayloadValidation validation =
                SyncPayloadFirewall::validate(
                    mutation.category,
                    mutation.payload);
            if (!validation.allowed) {
                if (error)
                    *error = validation.detail;
                return false;
            }
        } else if (!mutation.payload.isUndefined()
                   && !mutation.payload.isNull()) {
            if (error) {
                *error = QStringLiteral(
                    "The pending sync outbox contains a delete with an ordinary payload.");
            }
            return false;
        }
    }

    for (auto categoryIt =
             m_persistent.mirrors.constBegin();
         categoryIt
             != m_persistent.mirrors.constEnd();
         ++categoryIt) {
        const SyncOwnershipEntry *entry =
            SyncOwnershipInventory::find(
                categoryIt.key());
        if (!entry
            || entry->disposition
                != SyncDisposition::Syncable
            || entry->ownerStatus
                != SyncOwnerStatus::Confirmed
            || !entry->ordinaryPayloadEligible) {
            if (error) {
                *error = QStringLiteral(
                    "The durable sync mirror contains a category that is not eligible for ordinary sync.");
            }
            return false;
        }

        for (auto recordIt =
                 categoryIt->constBegin();
             recordIt
                 != categoryIt->constEnd();
             ++recordIt) {
            const SyncPayloadValidation validation =
                SyncPayloadFirewall::validate(
                    categoryIt.key(),
                    recordIt->payload);
            if (!validation.allowed) {
                if (error)
                    *error = validation.detail;
                return false;
            }
        }
    }

    for (auto categoryIt =
             m_persistent.winners.constBegin();
         categoryIt
             != m_persistent.winners.constEnd();
         ++categoryIt) {
        const SyncOwnershipEntry *entry =
            SyncOwnershipInventory::find(
                categoryIt.key());

        if (!entry
            || entry->disposition
                != SyncDisposition::Syncable
            || entry->ownerStatus
                != SyncOwnerStatus::Confirmed
            || !entry->ordinaryPayloadEligible) {
            if (error) {
                *error = QStringLiteral(
                    "The durable sync winner state contains a category that is not eligible for ordinary sync.");
            }
            return false;
        }

        for (auto recordIt =
                 categoryIt->constBegin();
             recordIt
                 != categoryIt->constEnd();
             ++recordIt) {
            if (!isValidSyncWireRecordKey(
                    recordIt.key())
                || recordIt->schemaVersion <= 0
                || recordIt->hlc.physicalMs < 0
                || recordIt->hlc.deviceId.isEmpty()) {
                if (error) {
                    *error = QStringLiteral(
                        "The durable sync winner state contains invalid record metadata.");
                }
                return false;
            }
        }
    }

    return true;
}

bool SyncEngine::reconcileAllAdapters(
    QString *error,
    bool allowSnapshotDeletes) {
    const QStringList categories =
        m_registry->registeredCategories();

    for (const QString &category :
         categories) {
        if (m_disabledCategories.contains(category))
            continue;
        if (!reconcileCategory(
                category,
                error,
                allowSnapshotDeletes)) {
            return false;
        }
    }

    return true;
}

bool SyncEngine::reconcileCategory(
    const QString &categoryId,
    QString *error,
    bool allowSnapshotDeletes) {
    if (m_disabledCategories.contains(categoryId))
        return true;

    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError registryError;

    if (!m_registry->exportSnapshot(
            categoryId,
            &snapshot,
            &registryError)) {
        if (error) {
            *error =
                registryError.detail.isEmpty()
                ? registryError.code
                : registryError.detail;
        }
        return false;
    }

    QHash<QString, SyncMirrorRecord>
        current;
    QHash<QString, qint64>
        localOrderHints;
    QSet<QString> tombstones;
    for (const QString &recordKey : snapshot.tombstones)
        tombstones.insert(recordKey);

    for (const SyncAdapterRecord &record :
         snapshot.records) {
        if (tombstones.contains(record.recordKey))
            continue;
        current.insert(
            record.recordKey,
            SyncMirrorRecord{
                snapshot.schemaVersion,
                record.payload});
        localOrderHints.insert(
            record.recordKey,
            record.localOrderMs);
    }

    const QHash<QString, SyncMirrorRecord>
        previous =
            m_persistent.mirrors
                .value(categoryId);

    struct ChangedPut {
        QString recordKey;
        SyncMirrorRecord record;
        qint64 localOrderMs = -1;
    };

    QList<ChangedPut> changedPuts;
    for (auto it = current.constBegin();
         it != current.constEnd();
         ++it) {
        const auto previousIt =
            previous.constFind(
                it.key());

        const bool changed =
            previousIt
                == previous.constEnd()
            || previousIt->schemaVersion
                != it->schemaVersion
            || previousIt->payload
                != it->payload;

        if (!changed)
            continue;

        changedPuts.append(
            ChangedPut{
                it.key(),
                it.value(),
                localOrderHints.value(
                    it.key(),
                    -1)});
    }

    std::sort(
        changedPuts.begin(),
        changedPuts.end(),
        [](const ChangedPut &left,
           const ChangedPut &right) {
            const bool leftHasOrder =
                left.localOrderMs > 0;
            const bool rightHasOrder =
                right.localOrderMs > 0;

            if (leftHasOrder
                != rightHasOrder) {
                return leftHasOrder;
            }

            if (leftHasOrder
                && left.localOrderMs
                    != right.localOrderMs) {
                return left.localOrderMs
                    < right.localOrderMs;
            }

            return left.recordKey
                < right.recordKey;
        });

    for (const ChangedPut &changed :
         changedPuts) {
        enqueueMutation(
            categoryId,
            changed.recordKey,
            snapshot.schemaVersion,
            SyncWireOperation::Put,
            changed.record.payload,
            changed.localOrderMs);
    }

    QStringList tombstoneKeys = tombstones.values();
    tombstoneKeys.sort();
    for (const QString &recordKey : tombstoneKeys) {
        bool pendingDelete = false;
        for (const SyncWireMutation &pending : std::as_const(m_persistent.outbox)) {
            if (pending.category != categoryId || pending.recordKey != recordKey)
                continue;
            if (pending.operation == SyncWireOperation::Delete)
                pendingDelete = true;
        }
        if (pendingDelete)
            continue;

        // A durable winner with DELETE already acknowledges this owner
        // tombstone. Keep the marker for old-device replay, but do not author
        // a fresh mutation on every reconciliation/restart. Rejected DELETEs
        // remain eligible for explicit repair, so their marker must not be
        // mistaken for an acknowledgement.
        const auto winnerCategory = m_persistent.winners.constFind(categoryId);
        const auto winnerIt = winnerCategory == m_persistent.winners.constEnd()
            ? QHash<QString, SyncWinner>::const_iterator()
            : winnerCategory->constFind(recordKey);
        if (winnerCategory != m_persistent.winners.constEnd()
            && winnerIt != winnerCategory->constEnd()
            && winnerIt->operation == SyncWireOperation::Delete)
            continue;
        enqueueMutation(
            categoryId,
            recordKey,
            snapshot.schemaVersion,
            SyncWireOperation::Delete,
            QJsonValue());
    }

    QStringList previousKeys =
        previous.keys();
    previousKeys.sort();

    if (!allowSnapshotDeletes
        || !snapshot.missingRecordsAreDeletes) {
        return true;
    }

    for (const QString &recordKey :
         previousKeys) {
        if (current.contains(recordKey))
            continue;
        if (tombstones.contains(recordKey))
            continue;

        enqueueMutation(
            categoryId,
            recordKey,
            snapshot.schemaVersion,
            SyncWireOperation::Delete,
            QJsonValue());
    }

    return true;
}

void SyncEngine::enqueueMutation(
    const QString &categoryId,
    const QString &recordKey,
    int schemaVersion,
    SyncWireOperation operation,
    const QJsonValue &payload,
    qint64 localOrderMs) {
    SyncWireMutation mutation;
    mutation.mutationId =
        QUuid::createUuid()
            .toString(
                QUuid::WithoutBraces)
            .toLower();
    mutation.deviceId =
        m_deviceId;
    mutation.category =
        categoryId;
    mutation.recordKey =
        recordKey;
    mutation.schemaVersion =
        schemaVersion;
    mutation.hlc =
        m_clock.nextFromLocalOrder(
            localOrderMs,
            nowMs());
    mutation.operation =
        operation;
    mutation.payload =
        operation
                == SyncWireOperation::Put
            ? payload
            : QJsonValue();

    // Latest-wins: drop any still-pending unacknowledged mutation for the
    // same record before appending. Without this, rapid successive edits of
    // one record push N stacked mutations (and count N accepted server
    // mutations) where only the final state is meaningful.
    for (int index =
             m_persistent.outbox.size()
             - 1;
         index >= 0; --index) {
        const SyncWireMutation &pending =
            m_persistent.outbox.at(index);
        if (pending.category == categoryId
            && pending.recordKey
                == recordKey) {
            m_persistent.outbox
                .removeAt(index);
        }
    }

    for (auto it = m_persistent.rejectedMutations.begin();
         it != m_persistent.rejectedMutations.end();) {
        if (it->category == categoryId
            && it->recordKey == recordKey) {
            it = m_persistent.rejectedMutations.erase(it);
        } else {
            ++it;
        }
    }

    m_persistent.outbox.append(
        mutation);

    SyncWinner winner;
    winner.hlc =
        mutation.hlc;
    winner.schemaVersion =
        schemaVersion;
    winner.operation =
        operation;
    m_persistent.winners[
        categoryId]
        .insert(
            recordKey,
            winner);

    if (operation
        == SyncWireOperation::Put) {
        m_persistent.mirrors[
            categoryId]
            .insert(
                recordKey,
                SyncMirrorRecord{
                    schemaVersion,
                    payload});
    } else {
        m_persistent.mirrors[
            categoryId]
            .remove(recordKey);
    }

    emit observationChanged(
        m_state,
        pendingOutboxCount());
}

void SyncEngine::maybeRunNetwork() {
    if (!m_active
        || !m_networkEnabled
        || m_networkBusy
        || m_state == State::Blocked
        || m_ownerRedoRecoveryInProgress
        || m_startFinalizationPending
        || m_quarantineReplayRunning
        || !m_pendingPersistenceGenerations
                .isEmpty()) {
        return;
    }

    if (m_signOutFlushRequested) {
        completeSignOutFlushIfPossible();

        if (!m_signOutFlushRequested)
            return;

        if (!m_persistent.outbox.isEmpty()) {
            beginPush();
            return;
        }
    }

    // The attachment bootstrap gates everything else: the stable
    // snapshot must run to completion before ordinary pull resumes or
    // an attached push leaves the outbox.
    if (attachmentSnapshotPending()) {
        beginSnapshot();
        return;
    }

    if (m_initialPullPending
        || m_pullHasMore) {
        beginPull();
        return;
    }

    if (!m_persistent.outbox.isEmpty()) {
        beginPush();
        return;
    }

    if (!hasDurableSyncWarning(m_persistent))
        clearError();
    setState(State::Idle);

    if (m_automaticSchedulingEnabled) {
        m_retryTimer.start(
            kIdlePullIntervalMs);
    }
}

void SyncEngine::beginPull() {
    if (!m_active
        || !m_networkEnabled
        || m_networkBusy
        || m_ownerRedoRecoveryInProgress
        || m_startFinalizationPending
        || m_quarantineReplayRunning
        || attachmentSnapshotPending()) {
        return;
    }

    m_retryTimer.stop();
    m_networkBusy = true;
    m_request = {};
    m_request.phase =
        NetworkPhase::Pull;
    m_request.sentLocalMs =
        nowMs();
    m_request.requestId =
        m_client->pullSync(
            m_persistent.historicalReplayPending
                ? m_persistent.historicalReplayCursor
                : m_persistent.cursor);
}

void SyncEngine::beginSnapshot() {
    if (!m_active
        || !m_networkEnabled
        || m_networkBusy
        || !attachmentSnapshotPending()) {
        return;
    }

    m_retryTimer.stop();
    m_networkBusy = true;
    m_request = {};
    m_request.phase =
        NetworkPhase::Snapshot;
    m_request.sentLocalMs =
        nowMs();
    // An empty token fetches the first page; the durable continuation
    // token resumes exactly where the bootstrap left off.
    m_request.requestId =
        m_client->pullSyncSnapshot(
            m_persistent
                .attachmentSnapshotNextPageToken);
}

bool SyncEngine::attachmentSnapshotPending() const {
    return m_persistent.attachmentModeActive
        && !m_persistent.attachmentSnapshotDone;
}

void SyncEngine::beginPush() {
    if (!m_active
        || !m_networkEnabled
        || m_networkBusy
        || m_persistent.outbox
               .isEmpty()) {
        return;
    }

    m_retryTimer.stop();

    const qsizetype beforeDisabledRemoval =
        m_persistent.outbox.size();
    m_persistent.outbox.erase(
        std::remove_if(
            m_persistent.outbox.begin(),
            m_persistent.outbox.end(),
            [this](const SyncWireMutation &mutation) {
                return m_disabledCategories.contains(mutation.category);
            }),
        m_persistent.outbox.end());
    const bool removedDisabled =
        beforeDisabledRemoval != m_persistent.outbox.size();

    QJsonArray mutations;
    QStringList mutationIds;
    const QSet<QString> attachmentMutationIds(
        m_persistent.attachmentMutationIds.cbegin(),
        m_persistent.attachmentMutationIds.cend());
    std::optional<bool> attachedBatch;
    bool markerChanged = removedDisabled;
    bool markedOversize = false;
    QString warningCode;
    QString warningMessage;

    for (const SyncWireMutation &mutation :
         std::as_const(m_persistent.outbox)) {
        const bool mutationIsAttached =
            m_persistent.attachmentModeActive
            && attachmentMutationIds.contains(mutation.mutationId);
        if (m_persistent.attachmentModeActive
            && !attachmentMutationIds.isEmpty()
            && !mutationIsAttached) {
            continue;
        }
        if (attachedBatch.has_value()
            && *attachedBatch != mutationIsAttached) {
            break;
        }
        if (!attachedBatch.has_value())
            attachedBatch = mutationIsAttached;
        const QString fingerprint =
            syncMutationFingerprint(mutation);
        const auto rejectedIt =
            m_persistent.rejectedMutations.constFind(
                mutation.mutationId);
        if (rejectedIt != m_persistent.rejectedMutations.constEnd()) {
            if (rejectedIt->fingerprint == fingerprint)
                continue;
            m_persistent.rejectedMutations.remove(
                mutation.mutationId);
            markerChanged = true;
        }

        QJsonArray candidate = mutations;
        candidate.append(
            syncWireMutationToJson(
                mutation));
        if (candidate.size() > kPushBatchLimit)
            break;

        const QByteArray candidateBytes =
            syncWirePushRequestBytes(candidate);
        if (candidateBytes.size() > kPushBodyByteLimit) {
            if (syncWirePushRequestBytes(QJsonArray{
                    syncWireMutationToJson(mutation)})
                    .size()
                > kPushBodyByteLimit) {
                const SyncRejectedMutation marker{
                    mutation.mutationId,
                    mutation.category,
                    mutation.recordKey,
                    QStringLiteral("sync_mutation_oversize"),
                    QStringLiteral("This sync record is larger than the 64 KiB wire limit and was held for repair."),
                    fingerprint};
                const auto existing =
                    m_persistent.rejectedMutations.constFind(
                        mutation.mutationId);
                if (existing == m_persistent.rejectedMutations.constEnd()
                    || existing->fingerprint != marker.fingerprint
                    || existing->code != marker.code) {
                    m_persistent.rejectedMutations.insert(
                        mutation.mutationId,
                        marker);
                    markerChanged = true;
                }
                if (warningCode.isEmpty()) {
                    warningCode = marker.code;
                    warningMessage = marker.message;
                }
                markedOversize = true;
                continue;
            }
            // The current record fits by itself but would exceed this batch;
            // leave it for the next request so later records cannot overtake
            // it and successful acknowledgements remain contiguous.
            break;
        }

        mutations = candidate;
        mutationIds.append(
            mutation.mutationId);
    }

    if (!warningCode.isEmpty()) {
        m_lastErrorCode = warningCode;
        m_lastErrorMessage = warningMessage;
    }

    if (markedOversize) {
        if (mutations.isEmpty()) {
            if (markerChanged)
                persistState();
            setState(State::Idle);
            completeSignOutFlushIfPossible();
            return;
        }
        // Send the fitting prefix/suffix in this request. The oversize entry
        // is parked durably and must not starve unrelated acknowledgements.
        if (markerChanged)
            persistState();
    }

    if (mutations.isEmpty()) {
        if (markerChanged)
            persistState();
        else if (!hasDurableSyncWarning(m_persistent))
            clearError();
        setState(State::Idle);
        if (m_automaticSchedulingEnabled)
            m_retryTimer.start(kIdlePullIntervalMs);
        return;
    }

    m_networkBusy = true;
    m_request = {};
    m_request.phase =
        NetworkPhase::Push;
    m_request.sentLocalMs =
        nowMs();
    m_request.mutationIds =
        mutationIds;
    // Attached pushes stamp the envelope with the active attachment id
    // while mutation identity, batching, retry, clock, and persistence
    // semantics stay exactly as they are for ordinary pushes.
    m_request.requestId =
        m_client->pushSync(
            mutations,
            attachedBatch.value_or(false)
                ? m_persistent.attachmentId
                : QString());
}

void SyncEngine::finishPullProcessing(
    bool processed,
    const QString &errorCode,
    const QString &errorMessage) {
    if (!m_active)
        return;

    m_networkBusy = false;
    m_request = {};

    if (!processed) {
        setBlocked(
            errorCode.isEmpty() ? QStringLiteral("sync_protocol_error") : errorCode,
            errorMessage.isEmpty() ? QStringLiteral("Sync needs attention.") : errorMessage);

        if (m_signOutFlushRequested) {
            m_signOutFlushRequested = false;
            emit signOutFlushFinished(false, m_lastErrorCode, m_lastErrorMessage);
        }
        return;
    }

    m_retryAttempt = 0;
    if (!errorCode.isEmpty()) {
        m_lastErrorCode = errorCode;
        m_lastErrorMessage = errorMessage;
    } else if (!hasDurableSyncWarning(m_persistent)) {
        clearError();
    }
    setState(State::Idle);
    persistState();
    emit recoveryAvailableChanged();
}

bool SyncEngine::processPullReply(
    const AccountTransportReply &reply,
    QString *errorCode,
    QString *errorMessage) {
    const auto response =
        syncWirePullResponseFromJson(
            reply.body);
    if (!response.has_value()) {
        if (errorCode) {
            *errorCode =
                QStringLiteral(
                    "sync_protocol_error");
        }
        if (errorMessage) {
            *errorMessage =
                QStringLiteral(
                    "The sync service returned an invalid pull response.");
        }
        return false;
    }

    PullProcessingContext context;
    context.entries = response->entries;
    context.hasMore = response->hasMore;
    context.replayingHistorical = m_persistent.historicalReplayPending;
    context.historicalLimit = context.replayingHistorical
        ? m_persistent.historicalReplayLimit
        : 0;
    m_pullProcessing = std::move(context);

    const bool completed = continuePullProcessing(errorCode, errorMessage);
    if (!completed)
        return false;
    // A durable owner operation leaves the context installed until its receipt.
    return true;
}

bool SyncEngine::continuePullProcessing(
    QString *errorCode,
    QString *errorMessage) {
    if (!m_pullProcessing.has_value())
        return true;

    if (m_ownerRedoBatchPreparing)
        return true;

    PullProcessingContext &batchContext = *m_pullProcessing;
    if (!m_ownerRedoBatchReady) {
        bool addedRedo = false;
        const quint64 processedCursor = batchContext.replayingHistorical
            ? m_persistent.historicalReplayCursor
            : m_persistent.cursor;
        for (const SyncWirePullEntry &candidate :
             std::as_const(batchContext.entries)) {
            if (candidate.serverSeq <= processedCursor)
                continue;
            if (batchContext.replayingHistorical
                && candidate.serverSeq > batchContext.historicalLimit)
                break;
            const SyncWireHlc &candidateOrderingHlc =
                syncMutationOrderingHlc(candidate.mutation);
            if (candidateOrderingHlc.physicalMs
                    > nowMs() + kMaximumRemoteClockFutureMs) {
                if (errorCode)
                    *errorCode = QStringLiteral("sync_protocol_error");
                if (errorMessage)
                    *errorMessage = QStringLiteral(
                        "The sync service served a clock value that is implausibly far in the future.");
                m_pullProcessing.reset();
                return false;
            }
            if (!candidate.won
                || m_disabledCategories.contains(candidate.mutation.category))
                continue;
            const auto categoryIt =
                m_persistent.winners.constFind(candidate.mutation.category);
            if (categoryIt != m_persistent.winners.constEnd()) {
                const auto winnerIt =
                    categoryIt->constFind(candidate.mutation.recordKey);
                if (winnerIt != categoryIt->constEnd()
                    && syncEntryIsCoveredByWinner(
                           *winnerIt,
                           candidate)) {
                    continue;
                }
            }
            bool alreadyRedo = false;
            for (const SyncOwnerRedo &redo : std::as_const(m_persistent.ownerRedos)) {
                if (redo.serverSeq == candidate.serverSeq) {
                    alreadyRedo = true;
                    break;
                }
            }
            if (!alreadyRedo) {
                m_persistent.ownerRedos.append(SyncOwnerRedo{
                    candidate.serverSeq,
                    candidate.won,
                    candidate.mutation,
                    batchContext.replayingHistorical,
                    false});
                addedRedo = true;
            }
        }

        m_ownerRedoBatchReady = true;
        if (addedRedo) {
            m_ownerRedoBatchPreparing = true;
            const quint64 generation = m_profileGeneration;
            persistState(
                [this, generation](bool committed, const QString &message) {
                    if (!m_active || generation != m_profileGeneration)
                        return;
                    m_ownerRedoBatchPreparing = false;
                    if (!committed) {
                        m_ownerRedoBatchReady = false;
                        m_pullProcessing.reset();
                        finishPullProcessing(
                            false,
                            QStringLiteral("sync_persistence_failed"),
                            message.isEmpty()
                                ? QStringLiteral("Sync state could not be stored safely before owner apply.")
                                : message);
                        return;
                    }
                    QString code;
                    QString detail;
                    const bool completed = continuePullProcessing(&code, &detail);
                    if (!completed) {
                        finishPullProcessing(false, code, detail);
                    } else if (!m_pullProcessing.has_value()) {
                        finishPullProcessing(true, code, detail);
                    }
                });
            return true;
        }
    }

    PullProcessingContext &context = *m_pullProcessing;
    while (context.index < context.entries.size()) {
        const SyncWirePullEntry entry = context.entries.at(context.index++);
        const quint64 processedCursor = context.replayingHistorical
            ? m_persistent.historicalReplayCursor
            : m_persistent.cursor;
        if (entry.serverSeq <= processedCursor)
            continue;

        if (context.replayingHistorical
            && entry.serverSeq > context.historicalLimit) {
            context.replayReachedLimit = true;
            break;
        }

        const SyncWireHlc &entryOrderingHlc =
            syncMutationOrderingHlc(entry.mutation);
        if (entryOrderingHlc.physicalMs
                > nowMs() + kMaximumRemoteClockFutureMs) {
            if (errorCode)
                *errorCode = QStringLiteral("sync_protocol_error");
            if (errorMessage)
                *errorMessage = QStringLiteral(
                    "The sync service served a clock value that is implausibly far in the future.");
            m_pullProcessing.reset();
            return false;
        }

        m_clock.observe(entry.mutation.hlc, nowMs());
        if (entry.mutation.materializedHlc.has_value())
            m_clock.observe(entryOrderingHlc, nowMs());

        const auto categoryIt = m_persistent.winners.constFind(entry.mutation.category);
        const auto winnerIt = categoryIt == m_persistent.winners.constEnd()
            ? QHash<QString, SyncWinner>::const_iterator()
            : categoryIt->constFind(entry.mutation.recordKey);
        const bool alreadyWon = categoryIt != m_persistent.winners.constEnd()
            && winnerIt != categoryIt->constEnd()
            && syncEntryIsCoveredByWinner(*winnerIt, entry);

        if (!entry.won || alreadyWon) {
            // The page checkpoint may have predeclared a redo for a later
            // entry before an earlier winner for the same logical record was
            // applied. Once the durable winner already covers this entry,
            // retire that redundant redo together with the cursor advance.
            removeOwnerRedo(entry.serverSeq);
            if (context.replayingHistorical)
                m_persistent.historicalReplayCursor = entry.serverSeq;
            else
                m_persistent.cursor = entry.serverSeq;
            continue;
        }

        if (m_disabledCategories.contains(entry.mutation.category)) {
            // Disabled categories are acknowledged by durable sync metadata
            // without touching the owner. A page checkpoint can still have
            // predeclared this row, so it must not survive as a startup redo.
            removeOwnerRedo(entry.serverSeq);
            recordWinningState(entry);
            if (context.replayingHistorical)
                m_persistent.historicalReplayCursor = entry.serverSeq;
            else
                m_persistent.cursor = entry.serverSeq;
            continue;
        }

        const bool replayingHistorical = context.replayingHistorical;
        const quint64 generation = m_profileGeneration;
        beginDurableOwnerApply(
            entry,
            replayingHistorical,
            false,
            false,
            [this, generation](
                bool progressed,
                const SyncAdapterRegistryError &result) {
                if (!m_active || generation != m_profileGeneration)
                    return;

                if (!progressed) {
                    m_ownerRedoBatchReady = false;
                    m_pullProcessing.reset();
                    finishPullProcessing(
                        false,
                        result.code.isEmpty()
                            ? QStringLiteral("adapter_apply_failed")
                            : result.code,
                        result.detail);
                    return;
                }

                QString code;
                QString message;
                if (result.failureClass == SyncAdapterFailureClass::Compatibility) {
                    code = result.code;
                    message = result.detail;
                    if (m_pullProcessing.has_value()
                        && m_pullProcessing->firstWarningCode.isEmpty()) {
                        m_pullProcessing->firstWarningCode = code;
                        m_pullProcessing->firstWarningMessage = message;
                    }
                }
                const bool completed = continuePullProcessing(&code, &message);
                if (!completed) {
                    finishPullProcessing(false, code, message);
                    return;
                }
                if (!m_pullProcessing.has_value())
                    finishPullProcessing(true, code, message);
            });

        // beginDurableOwnerApply keeps m_ownerApply set until the receipt. If
        // a future owner implementation completes synchronously, the callback
        // above has already advanced the context and the loop can continue.
        if (m_ownerApply.has_value())
            return true;
    }

    m_pullHasMore = context.hasMore && !context.replayReachedLimit;
    if (!m_pullHasMore) {
        m_initialPullPending = false;
        if (context.replayingHistorical) {
            m_persistent.cursor = qMax(
                m_persistent.cursor,
                m_persistent.historicalReplayCursor);
            m_persistent.historicalReplayPending = false;
            m_persistent.historicalReplayCursor = 0;
            m_persistent.historicalReplayLimit = 0;
        }
    }

    if (!m_pullHasMore && !m_categoryReplayInProgress.isEmpty()
        && !finishCategoryReplay(m_categoryReplayInProgress,
                                 &context.firstWarningCode,
                                 &context.firstWarningMessage)) {
        if (errorCode)
            *errorCode = context.firstWarningCode;
                    if (errorMessage)
            *errorMessage = context.firstWarningMessage;
                m_pullProcessing.reset();
                return false;
    }

    if (errorCode)
        *errorCode = context.firstWarningCode;
    if (errorMessage)
        *errorMessage = context.firstWarningMessage;
    m_ownerRedoBatchReady = false;
    m_pullProcessing.reset();
    return true;
}

void SyncEngine::beginDurableOwnerApply(
    const SyncWirePullEntry &entry,
    bool replayingHistorical,
    bool fromQuarantine,
    bool recovery,
    OwnerApplyContinuation continuation) {
    if (!m_active || m_ownerApply.has_value())
        return;

    OwnerApplyContext context;
    context.entry = entry;
    context.replayingHistorical = replayingHistorical;
    context.fromQuarantine = fromQuarantine;
    context.recovery = recovery;
    context.profileGeneration = m_profileGeneration;
    context.continuation = std::move(continuation);
    m_ownerApply = std::move(context);

    bool hasRedo = false;
    for (const SyncOwnerRedo &redo : std::as_const(m_persistent.ownerRedos)) {
        if (redo.serverSeq == entry.serverSeq) {
            hasRedo = true;
            break;
        }
    }
    if (!hasRedo) {
        m_persistent.ownerRedos.append(SyncOwnerRedo{
            entry.serverSeq,
            entry.won,
            entry.mutation,
            replayingHistorical,
            fromQuarantine});
    }

    const quint64 generation = m_profileGeneration;
    auto startOwner = [this, generation]() {
        if (!m_active
            || generation != m_profileGeneration
            || !m_ownerApply.has_value()) {
            return;
        }

        const OwnerApplyContext context = *m_ownerApply;
        SyncAdapterMutation incoming;
        incoming.categoryId = context.entry.mutation.category;
        incoming.recordKey = context.entry.mutation.recordKey;
        incoming.schemaVersion = context.entry.mutation.schemaVersion;
        incoming.operation = context.entry.mutation.operation;
        incoming.payload = context.entry.mutation.payload;

        SyncAdapterRegistryError startError;
        const bool started = m_registry->applyRemoteAsync(
            incoming,
            [this, generation](const SyncAdapterRegistryError &result) {
                if (!m_active || generation != m_profileGeneration)
                    return;
                handleOwnerApplyCompletion(result);
            },
            &startError);
        if (!started)
            handleOwnerApplyCompletion(startError);
    };

    if (m_ownerRedoBatchReady && !recovery && !fromQuarantine) {
        // Keep the owner context installed until the event-loop turn. This
        // also prevents synchronous test/donor adapters from re-entering the
        // pull loop while its stack still holds a context reference.
        QMetaObject::invokeMethod(
            this,
            [startOwner]() { startOwner(); },
            Qt::QueuedConnection);
        return;
    }

    persistState(
        [this, generation, startOwner](bool committed, const QString &message) {
            if (!m_active
                || generation != m_profileGeneration
                || !m_ownerApply.has_value()) {
                return;
            }

            if (!committed) {
                SyncAdapterRegistryError result;
                result.code = QStringLiteral("sync_persistence_failed");
                result.detail = message.isEmpty()
                    ? QStringLiteral("Sync state could not be stored safely before owner apply.")
                    : message;
                result.failureClass = SyncAdapterFailureClass::Owner;
                handleOwnerApplyCompletion(result);
                return;
            }

            startOwner();
        });
}

void SyncEngine::handleOwnerApplyCompletion(
    const SyncAdapterRegistryError &result) {
    if (!m_ownerApply.has_value())
        return;

    OwnerApplyContext context = std::move(*m_ownerApply);
    m_ownerApply.reset();

    if (!m_active || context.profileGeneration != m_profileGeneration)
        return;

    const SyncWirePullEntry &entry = context.entry;
    const bool compatibility =
        result.failureClass == SyncAdapterFailureClass::Compatibility
        && !result.code.isEmpty();

    if (!result.isEmpty() && !compatibility) {
        if (context.continuation)
            context.continuation(false, result);
        return;
    }

    if (compatibility) {
        if (context.fromQuarantine) {
            m_quarantineReplaySkipped.insert(entry.serverSeq);
        } else {
            bool present = false;
            for (const SyncQuarantineEntry &quarantined :
                 std::as_const(m_persistent.quarantinedEntries)) {
                if (quarantined.serverSeq == entry.serverSeq) {
                    present = true;
                    break;
                }
            }
            if (!present) {
                m_persistent.quarantinedEntries.append(SyncQuarantineEntry{
                    entry.serverSeq,
                    entry.won,
                    entry.mutation,
                    result.code.isEmpty()
                        ? QStringLiteral("sync_record_incompatible")
                        : result.code,
                    result.detail.isEmpty()
                        ? QStringLiteral("A remote sync record was retained because the local owner could not materialize it.")
                        : result.detail});
            }
        }
        removeOwnerRedo(entry.serverSeq);
        if (!context.fromQuarantine) {
            if (context.replayingHistorical)
                m_persistent.historicalReplayCursor = entry.serverSeq;
            else
                m_persistent.cursor = entry.serverSeq;
        }
        if (!m_ownerRedoBatchReady || context.recovery || context.fromQuarantine)
            persistState();
        if (context.continuation)
            context.continuation(true, result);
        return;
    }

    removeOwnerRedo(entry.serverSeq);
    recordWinningState(entry);
    if (!context.fromQuarantine) {
        if (context.replayingHistorical)
            m_persistent.historicalReplayCursor = entry.serverSeq;
        else
            m_persistent.cursor = entry.serverSeq;
    } else {
        QList<SyncQuarantineEntry> remaining;
        remaining.reserve(m_persistent.quarantinedEntries.size());
        for (const SyncQuarantineEntry &quarantined :
             std::as_const(m_persistent.quarantinedEntries)) {
            if (quarantined.serverSeq != entry.serverSeq)
                remaining.append(quarantined);
        }
        m_persistent.quarantinedEntries = remaining;
    }

    if (!m_ownerRedoBatchReady || context.recovery || context.fromQuarantine)
        persistState();
    if (context.continuation)
        context.continuation(true, result);
}

void SyncEngine::recordWinningState(
    const SyncWirePullEntry &entry) {
    const SyncWireMutation &mutation = entry.mutation;
    SyncWinner winner;
    winner.hlc = syncMutationOrderingHlc(mutation);
    winner.schemaVersion = mutation.schemaVersion;
    winner.operation = mutation.operation;
    winner.serverSeq = entry.serverSeq;
    m_persistent.winners[mutation.category].insert(mutation.recordKey, winner);

    if (mutation.operation == SyncWireOperation::Put) {
        m_persistent.mirrors[mutation.category].insert(
            mutation.recordKey,
            SyncMirrorRecord{mutation.schemaVersion, mutation.payload});
    } else {
        m_persistent.mirrors[mutation.category].remove(mutation.recordKey);
    }
}

void SyncEngine::removeOwnerRedo(
    quint64 serverSeq) {
    QList<SyncOwnerRedo> remaining;
    remaining.reserve(m_persistent.ownerRedos.size());
    for (const SyncOwnerRedo &redo : std::as_const(m_persistent.ownerRedos)) {
        if (redo.serverSeq != serverSeq)
            remaining.append(redo);
    }
    m_persistent.ownerRedos = remaining;
}

void SyncEngine::beginOwnerRedoRecovery() {
    if (!m_active || !m_ownerRedoRecoveryInProgress || m_ownerApply.has_value())
        return;

    if (m_persistent.ownerRedos.isEmpty()) {
        finishStartAfterOwnerRedo();
        return;
    }

    const SyncOwnerRedo redo = m_persistent.ownerRedos.first();
    beginDurableOwnerApply(
        SyncWirePullEntry{redo.serverSeq, redo.won, false, redo.mutation},
        redo.replayingHistorical,
        redo.fromQuarantine,
        true,
        [this](bool progressed, const SyncAdapterRegistryError &result) {
            if (!m_active || !m_ownerRedoRecoveryInProgress)
                return;
            if (!progressed) {
                setBlocked(
                    result.code.isEmpty()
                        ? QStringLiteral("adapter_apply_failed")
                        : result.code,
                    result.detail.isEmpty()
                        ? QStringLiteral("A pending remote owner operation could not be recovered.")
                        : result.detail);
                return;
            }
            beginOwnerRedoRecovery();
        });

}

bool SyncEngine::processSnapshotReply(
    const AccountTransportReply &reply,
    QString *errorCode,
    QString *errorMessage) {
    const auto response =
        syncWireSnapshotResponseFromJson(
            reply.body);
    if (!response.has_value()) {
        if (errorCode) {
            *errorCode =
                QStringLiteral(
                    "sync_protocol_error");
        }
        if (errorMessage) {
            *errorMessage =
                QStringLiteral(
                    "The sync service returned an invalid snapshot response.");
        }
        return false;
    }

    for (const SyncWirePullEntry &entry :
         response->entries) {
        // Snapshot pages are sorted by (category, record_key), not by
        // server_seq: an entry at or below the engine cursor is a row
        // unchanged since the last ordinary pull, so it is skipped;
        // everything above it merges in through the ordinary pull-entry
        // validation.
        if (entry.serverSeq
                <= m_persistent.cursor) {
            continue;
        }

        // Poison guard, as with ordinary pull: a remote HLC far in the
        // future would permanently inflate the persisted hybrid clock.
        if (entry.mutation.hlc.physicalMs
                > nowMs() + kMaximumRemoteClockFutureMs) {
            if (errorCode) {
                *errorCode = QStringLiteral("sync_protocol_error");
            }
            if (errorMessage) {
                *errorMessage = QStringLiteral(
                    "The sync service served a clock value that is implausibly far in the future.");
            }
            return false;
        }

        m_clock.observe(
            entry.mutation.hlc,
            nowMs());

        if ((entry.canonical || entry.won)
            && !applyWinningPullEntry(
                    entry,
                    errorCode,
                    errorMessage)) {
            return false;
        }
    }

    if (response->hasMore) {
        // The continuation token is durable before the next page is
        // requested, so a crash or restart resumes this page stream
        // instead of restarting the bootstrap.
        m_persistent
            .attachmentSnapshotNextPageToken =
            response->nextPageToken;
        return true;
    }

    // Bootstrap complete. The frozen baseline cursor advances the
    // engine cursor only when ahead — never regressing it — and
    // ordinary pull then resumes strictly after it.
    m_persistent
        .attachmentSnapshotNextPageToken
        .clear();
    m_persistent.attachmentSnapshotDone =
        true;
    if (response->cursor
            > m_persistent.cursor) {
        m_persistent.cursor =
            response->cursor;
    }
    m_initialPullPending = true;
    return true;
}

bool SyncEngine::processPushReply(
    const AccountTransportReply &reply,
    QString *errorCode,
    QString *errorMessage) {
    const auto response =
        syncWirePushResponseFromJson(
            reply.body);
    if (!response.has_value()) {
        if (errorCode) {
            *errorCode =
                QStringLiteral(
                    "sync_protocol_error");
        }
        if (errorMessage) {
            *errorMessage =
                QStringLiteral(
                    "The sync service returned an invalid push response.");
        }
        return false;
    }

    QHash<QString, SyncWirePushResult>
        byMutationId;

    for (const SyncWirePushResult &result :
         response->results) {
        if (byMutationId.contains(
                result.mutationId)) {
            if (errorCode) {
                *errorCode =
                    QStringLiteral(
                        "sync_protocol_error");
            }
            if (errorMessage) {
                *errorMessage =
                    QStringLiteral(
                        "The sync service returned duplicate mutation results.");
            }
            return false;
        }

        byMutationId.insert(
            result.mutationId,
            result);
    }

    if (byMutationId.size()
        != m_request.mutationIds.size()) {
        if (errorCode) {
            *errorCode =
                QStringLiteral(
                    "sync_protocol_error");
        }
        if (errorMessage) {
            *errorMessage =
                QStringLiteral(
                    "The sync service returned an incomplete mutation result set.");
        }
        return false;
    }

    QSet<QString> acknowledged;
    bool sawClockSkew = false;
    QList<SyncWireHlc> skewCurrentWinners;
    QString firstRejectionCode;
    QString firstRejectionMessage;

    for (const QString &mutationId :
         m_request.mutationIds) {
        const auto it =
            byMutationId.constFind(
                mutationId);

        if (it == byMutationId.constEnd()) {
            if (errorCode) {
                *errorCode =
                    QStringLiteral(
                        "sync_protocol_error");
            }
            if (errorMessage) {
                *errorMessage =
                    QStringLiteral(
                        "The sync service omitted a mutation result.");
            }
            return false;
        }

        if (it->accepted) {
            acknowledged.insert(
                mutationId);
            m_persistent.rejectedMutations.remove(
                mutationId);

            // The local winner already represents this request. Retain its
            // acknowledged journal sequence so the later pull echo remains
            // idempotent; a distinct same-HLC merge row can still advance by
            // sequence through syncEntryIsCoveredByWinner().
            const auto pendingIt = std::find_if(
                m_persistent.outbox.constBegin(),
                m_persistent.outbox.constEnd(),
                [mutationId](const SyncWireMutation &mutation) {
                    return mutation.mutationId == mutationId;
                });
            if (pendingIt != m_persistent.outbox.constEnd()) {
                const auto categoryIt =
                    m_persistent.winners.find(pendingIt->category);
                if (categoryIt != m_persistent.winners.end()) {
                    const auto winnerIt =
                        categoryIt->find(pendingIt->recordKey);
                    if (winnerIt != categoryIt->end()
                        && compareSyncWireHlc(
                               winnerIt->hlc,
                               pendingIt->hlc)
                            == 0
                        && winnerIt->operation == pendingIt->operation) {
                        winnerIt->serverSeq = it->serverSeq;
                    }
                }
            }
            continue;
        }

        if (it->code
            == QLatin1String(
                "clock_skew")) {
            sawClockSkew = true;

            if (it->current.has_value()) {
                skewCurrentWinners.append(
                    it->current->hlc);
            }
            continue;
        }

        const auto pendingIt = std::find_if(
            m_persistent.outbox.constBegin(),
            m_persistent.outbox.constEnd(),
            [mutationId](const SyncWireMutation &mutation) {
                return mutation.mutationId == mutationId;
            });
        if (pendingIt != m_persistent.outbox.constEnd()) {
            m_persistent.rejectedMutations.insert(
                mutationId,
                SyncRejectedMutation{
                    mutationId,
                    pendingIt->category,
                    pendingIt->recordKey,
                    it->code.isEmpty()
                        ? QStringLiteral("sync_mutation_rejected")
                        : it->code,
                    it->message.isEmpty()
                        ? QStringLiteral("A sync mutation was rejected and remains queued for repair.")
                        : it->message,
                    syncMutationFingerprint(*pendingIt)});
        }

        if (firstRejectionCode.isEmpty()) {
            firstRejectionCode = it->code.isEmpty()
                ? QStringLiteral("sync_mutation_rejected")
                : it->code;
            firstRejectionMessage = it->message.isEmpty()
                ? QStringLiteral("A sync mutation was rejected and remains queued for repair.")
                : it->message;
        }
    }

    if (!acknowledged.isEmpty()) {
        QList<SyncWireMutation> remaining;
        remaining.reserve(
            m_persistent.outbox.size());

        for (const SyncWireMutation &mutation :
             m_persistent.outbox) {
            if (!acknowledged.contains(
                    mutation.mutationId)) {
                remaining.append(
                    mutation);
            }
        }

        m_persistent.outbox =
            remaining;
    }

    if (sawClockSkew) {
        m_clock.rebaseRejectedFuture(
            nowMs());

        for (const SyncWireHlc &winner :
             skewCurrentWinners) {
            m_clock.observe(
                winner,
                nowMs());
        }

        rebasePendingMutations();
    }

    if (!firstRejectionCode.isEmpty()) {
        if (errorCode)
            *errorCode = firstRejectionCode;
        if (errorMessage)
            *errorMessage = firstRejectionMessage;
    }

    emit observationChanged(
        m_state,
        pendingOutboxCount());
    return true;
}

bool SyncEngine::applyWinningPullEntry(
    const SyncWirePullEntry &entry,
    QString *errorCode,
    QString *errorMessage,
    SyncAdapterFailureClass *failureClass) {
    const SyncWireMutation &mutation =
        entry.mutation;

    const auto categoryIt =
        m_persistent.winners.constFind(
            mutation.category);

    if (categoryIt
        != m_persistent.winners.constEnd()) {
        const auto winnerIt =
            categoryIt->constFind(
                mutation.recordKey);

        if (winnerIt
                != categoryIt->constEnd()
            && syncEntryIsCoveredByWinner(
                   *winnerIt,
                   entry)) {
            return true;
        }
    }

    SyncAdapterMutation incoming;
    incoming.categoryId =
        mutation.category;
    incoming.recordKey =
        mutation.recordKey;
    incoming.schemaVersion =
        mutation.schemaVersion;
    incoming.operation =
        mutation.operation;
    incoming.payload =
        mutation.payload;

    if (!m_disabledCategories.contains(mutation.category)) {
        SyncAdapterRegistryError registryError;
        if (!m_registry->applyRemote(incoming, &registryError)) {
            if (failureClass)
                *failureClass = registryError.failureClass;
            if (errorCode)
                *errorCode = registryError.code;
            if (errorMessage)
                *errorMessage = registryError.detail;
            return false;
        }
    }

    SyncWinner winner;
    winner.hlc =
        syncMutationOrderingHlc(mutation);
    winner.schemaVersion =
        mutation.schemaVersion;
    winner.operation =
        mutation.operation;
    winner.serverSeq =
        entry.serverSeq;

    m_persistent.winners[
        mutation.category]
        .insert(
            mutation.recordKey,
            winner);

    if (mutation.operation
        == SyncWireOperation::Put) {
        m_persistent.mirrors[
            mutation.category]
            .insert(
                mutation.recordKey,
                SyncMirrorRecord{
                    mutation.schemaVersion,
                    mutation.payload});
    } else {
        m_persistent.mirrors[
            mutation.category]
            .remove(
                mutation.recordKey);
    }

    return true;
}

bool SyncEngine::replayQuarantinedCategory(
    const QString &categoryId,
    QString *errorCode,
    QString *errorMessage) {
    if (m_disabledCategories.contains(categoryId))
        return true;
    Q_UNUSED(errorCode);
    Q_UNUSED(errorMessage);

    if (m_quarantineReplayRunning) {
        if (m_quarantineReplayCategory != categoryId
            && !m_quarantineReplayCategories.contains(categoryId)) {
            m_quarantineReplayCategories.append(categoryId);
        }
        return true;
    }

    m_quarantineReplayCategory = categoryId;
    m_quarantineReplayRunning = true;
    m_quarantineReplaySkipped.clear();
    m_quarantineReplayCategories.clear();
    continueQuarantineReplay();
    return true;
}

void SyncEngine::continueQuarantineReplay() {
    if (!m_active || !m_quarantineReplayRunning)
        return;

    for (const SyncQuarantineEntry &quarantined :
         std::as_const(m_persistent.quarantinedEntries)) {
        if (quarantined.mutation.category != m_quarantineReplayCategory
            || !quarantined.won
            || m_quarantineReplaySkipped.contains(quarantined.serverSeq)) {
            continue;
        }

        const quint64 generation = m_profileGeneration;
        beginDurableOwnerApply(
            SyncWirePullEntry{
                quarantined.serverSeq,
                quarantined.won,
                false,
                quarantined.mutation},
            false,
            true,
            false,
            [this, generation](
                bool progressed,
                const SyncAdapterRegistryError &result) {
                if (!m_active || generation != m_profileGeneration)
                    return;
                if (!progressed) {
                    m_quarantineReplayRunning = false;
                    setBlocked(
                        result.code.isEmpty()
                            ? QStringLiteral("adapter_apply_failed")
                            : result.code,
                        result.detail.isEmpty()
                            ? QStringLiteral("A quarantined sync record could not be applied safely.")
                            : result.detail);
                    return;
                }
                continueQuarantineReplay();
            });
        return;
    }

    m_quarantineReplaySkipped.clear();
    if (!m_quarantineReplayCategories.isEmpty()) {
        m_quarantineReplayCategory = m_quarantineReplayCategories.takeFirst();
        continueQuarantineReplay();
        return;
    }

    m_quarantineReplayCategory.clear();
    m_quarantineReplayRunning = false;
    persistState();
    if (!hasDurableSyncWarning(m_persistent))
        clearError();
    if (!m_startFinalizationPending)
        maybeRunNetwork();
}

bool SyncEngine::finishCategoryReplay(
    const QString &categoryId,
    QString *errorCode,
    QString *errorMessage) {
    const auto pausedIt = m_persistent.pausedCategories.constFind(categoryId);
    if (pausedIt == m_persistent.pausedCategories.constEnd())
        return true;

    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError registryError;
    if (!m_registry->exportSnapshot(categoryId, &snapshot, &registryError)) {
        if (errorCode) *errorCode = registryError.code;
        if (errorMessage) *errorMessage = registryError.detail;
        return false;
    }
    QSet<QString> tombstones;
    for (const QString &recordKey : snapshot.tombstones)
        tombstones.insert(recordKey);
    QHash<QString, SyncAdapterRecord> current;
    for (const SyncAdapterRecord &record : snapshot.records) {
        if (tombstones.contains(record.recordKey))
            continue;
        current.insert(record.recordKey, record);
    }
    const auto remote = m_persistent.mirrors.value(categoryId);
    const SyncPausedCategoryState replay = pausedIt.value();

    for (const SyncWireMutation &pending : replay.pendingMutations) {
        bool present = false;
        for (const SyncWireMutation &existing : std::as_const(m_persistent.outbox)) {
            if (existing.mutationId == pending.mutationId) {
                present = true;
                break;
            }
        }
        if (!present)
            m_persistent.outbox.append(pending);
    }

    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        const auto remoteIt = remote.constFind(it.key());
        if (remoteIt != remote.constEnd()
            && remoteIt->schemaVersion == snapshot.schemaVersion
            && remoteIt->payload == it->payload)
            continue;
        if (!remote.contains(it.key())) {
            if (snapshot.missingRecordsAreDeletes) {
                if (replay.localOverlay.contains(it.key()))
                    continue;
                if (!m_registry->applyRemote(SyncAdapterMutation{categoryId, it.key(),
                        snapshot.schemaVersion, SyncWireOperation::Delete, QJsonValue()}, &registryError)) {
                    if (errorCode) *errorCode = registryError.code;
                    if (errorMessage) *errorMessage = registryError.detail;
                    return false;
                }
            } else if (!replay.localOverlay.contains(it.key())) {
                enqueueMutation(categoryId, it.key(), snapshot.schemaVersion,
                                SyncWireOperation::Put, it->payload, it->localOrderMs);
            }
        }
    }
    for (auto it = remote.constBegin(); it != remote.constEnd(); ++it) {
        if (!m_registry->applyRemote(SyncAdapterMutation{categoryId, it.key(),
                it->schemaVersion, SyncWireOperation::Put, it->payload}, &registryError)) {
            if (errorCode) *errorCode = registryError.code;
            if (errorMessage) *errorMessage = registryError.detail;
            return false;
        }
    }

    QStringList overlayKeys = replay.localOverlay.keys();
    overlayKeys.sort();
    for (const QString &key : overlayKeys) {
        const SyncPausedOverlayRecord &overlay = replay.localOverlay.value(key);
        if (overlay.operation == SyncWireOperation::Delete
            && !snapshot.missingRecordsAreDeletes) {
            continue;
        }
        if (!m_registry->applyRemote(SyncAdapterMutation{categoryId, key,
                overlay.schemaVersion, overlay.operation, overlay.payload}, &registryError)) {
            if (errorCode) *errorCode = registryError.code;
            if (errorMessage) *errorMessage = registryError.detail;
            return false;
        }
        enqueueMutation(categoryId, key, overlay.schemaVersion, overlay.operation,
                        overlay.payload, overlay.localOrderMs);
    }
    m_persistent.pausedCategories.remove(categoryId);
    m_disabledCategories.remove(categoryId);
    m_categoryReplayInProgress.clear();
    while (!m_categoryReplayQueue.isEmpty()) {
        const QString next = m_categoryReplayQueue.takeFirst();
        const auto nextIt = m_persistent.pausedCategories.constFind(next);
        if (nextIt == m_persistent.pausedCategories.constEnd()
            || !nextIt->replaying
            || !m_disabledCategories.contains(next)) {
            continue;
        }
        m_categoryReplayInProgress = next;
        m_persistent.winners.remove(next);
        m_persistent.mirrors.remove(next);
        m_persistent.cursor = 0;
        m_initialPullPending = true;
        m_pullHasMore = false;
        break;
    }
    persistState();
    return true;
}

void SyncEngine::rebasePendingMutations() {
    for (SyncWireMutation &mutation :
         m_persistent.outbox) {
        m_persistent.rejectedMutations.remove(
            mutation.mutationId);
        mutation.mutationId =
            QUuid::createUuid()
                .toString(
                    QUuid::WithoutBraces)
                .toLower();
        mutation.deviceId =
            m_deviceId;
        mutation.hlc =
            m_clock.next(
                nowMs());

        SyncWinner winner;
        winner.hlc =
            mutation.hlc;
        winner.schemaVersion =
            mutation.schemaVersion;
        winner.operation =
            mutation.operation;

        m_persistent.winners[
            mutation.category]
            .insert(
                mutation.recordKey,
                winner);
    }
}

quint64 SyncEngine::persistState(
    std::function<void(bool, const QString &)> callback) {
    persistClockIntoState();

    if (m_statePath.isEmpty()) {
        if (callback)
            callback(false, QStringLiteral("The sync state path is unavailable."));
        return 0;
    }

    const quint64 generation =
        m_stateStore.saveAsync(
            m_statePath,
            m_persistent);

    m_pendingPersistenceGenerations
        .insert(generation);
    if (callback)
        m_persistenceCallbacks.insert(generation, std::move(callback));
    return generation;
}

void SyncEngine::persistClockIntoState() {
    m_persistent.hlcPhysicalMs =
        m_clock.physicalMs();
    m_persistent.hlcCounter =
        m_clock.counter();
    m_persistent.serverOffsetMs =
        m_clock.serverOffsetMs();
}

void SyncEngine::handlePersistenceCommitted(
    quint64 generation) {
    if (!m_pendingPersistenceGenerations
             .remove(generation)) {
        return;
    }

    const auto callbackIt = m_persistenceCallbacks.find(generation);
    if (callbackIt != m_persistenceCallbacks.end()) {
        auto callback = std::move(callbackIt.value());
        m_persistenceCallbacks.erase(callbackIt);
        if (callback)
            callback(true, QString());
    }

    if (!m_active)
        return;

    if (!m_pendingPersistenceGenerations
             .isEmpty()) {
        return;
    }

    if (m_signOutFlushRequested)
        completeSignOutFlushIfPossible();

    maybeRunNetwork();
}

void SyncEngine::handlePersistenceFailed(
    quint64 generation,
    const QString &message) {
    if (!m_pendingPersistenceGenerations
             .remove(generation)) {
        return;
    }

    const auto callbackIt = m_persistenceCallbacks.find(generation);
    if (callbackIt != m_persistenceCallbacks.end()) {
        auto callback = std::move(callbackIt.value());
        m_persistenceCallbacks.erase(callbackIt);
        if (callback)
            callback(false, message);
    }

    if (!m_active)
        return;

    setBlocked(
        QStringLiteral(
            "sync_persistence_failed"),
        message.isEmpty()
            ? QStringLiteral(
                  "Sync state could not be stored safely.")
            : message);

    if (m_signOutFlushRequested) {
        m_signOutFlushRequested = false;
        emit signOutFlushFinished(
            false,
            QStringLiteral(
                "sync_persistence_failed"),
            m_lastErrorMessage);
    }
}

void SyncEngine::setState(
    State state) {
    if (m_state == state)
        return;

    m_state = state;
    emit observationChanged(
        m_state,
        pendingOutboxCount());
}

void SyncEngine::setBlocked(
    const QString &code,
    const QString &message) {
    m_retryTimer.stop();
    m_lastErrorCode =
        code;
    m_lastErrorMessage =
        message;
    setState(State::Blocked);
}

void SyncEngine::setRetrying(
    const QString &code,
    const QString &message) {
    m_lastErrorCode =
        code;
    m_lastErrorMessage =
        message;
    setState(State::Retrying);
    scheduleRetry();
}

void SyncEngine::clearError() {
    m_lastErrorCode.clear();
    m_lastErrorMessage.clear();
}

void SyncEngine::scheduleRetry() {
    if (!m_active
        || !m_networkEnabled
        || !m_automaticSchedulingEnabled
        || m_state == State::Blocked) {
        return;
    }

    const int exponent =
        qMin(
            m_retryAttempt,
            7);

    const qint64 scaled =
        static_cast<qint64>(
            kRetryBaseMs)
        << exponent;

    const int delay =
        static_cast<int>(
            qMin<qint64>(
                scaled,
                kRetryMaximumMs));

    ++m_retryAttempt;
    m_retryTimer.start(delay);
}

bool SyncEngine::allOutboxEntriesParked() const {
    if (m_persistent.outbox.isEmpty())
        return false;

    for (const SyncWireMutation &mutation : m_persistent.outbox) {
        const auto marker = m_persistent.rejectedMutations.constFind(
            mutation.mutationId);
        if (marker == m_persistent.rejectedMutations.constEnd()
            || marker->fingerprint != syncMutationFingerprint(mutation)) {
            return false;
        }
    }
    return true;
}

void SyncEngine::completeSignOutFlushIfPossible() {
    if (!m_signOutFlushRequested
        || m_networkBusy
        || !m_pendingPersistenceGenerations
                .isEmpty()) {
        return;
    }

    if (m_persistent.outbox.isEmpty()) {
        m_signOutFlushRequested = false;
        emit signOutFlushFinished(
            true,
            QString(),
            QString());
        return;
    }

    if (allOutboxEntriesParked()) {
        m_signOutFlushRequested = false;
        emit signOutFlushFinished(
            false,
            QStringLiteral("sync_unsynced_retained"),
            QStringLiteral(
                "Some changes remain queued because they need sync repair."));
        return;
    }

    if (m_state == State::Blocked) {
        m_signOutFlushRequested = false;
        emit signOutFlushFinished(
            false,
            m_lastErrorCode.isEmpty()
                ? QStringLiteral(
                      "sync_blocked")
                : m_lastErrorCode,
            m_lastErrorMessage.isEmpty()
                ? QStringLiteral(
                      "Some changes haven't synced.")
                : m_lastErrorMessage);
        return;
    }

    if (!m_networkEnabled) {
        m_signOutFlushRequested = false;
        emit signOutFlushFinished(
            false,
            QStringLiteral("offline"),
            QStringLiteral(
                "Some changes haven't synced."));
    }
}

qint64 SyncEngine::nowMs() const {
    return m_nowProvider
        ? m_nowProvider()
        : QDateTime::
              currentMSecsSinceEpoch();
}

QString SyncEngine::stateName(
    State state) {
    switch (state) {
    case State::Inactive:
        return QStringLiteral("inactive");
    case State::Idle:
        return QStringLiteral("idle");
    case State::Retrying:
        return QStringLiteral("retrying");
    case State::Blocked:
        return QStringLiteral("blocked");
    }

    return QStringLiteral("inactive");
}

bool SyncEngine::isSuccess(
    const AccountTransportReply &reply) {
    return !reply.networkError
        && reply.statusCode >= 200
        && reply.statusCode < 300;
}
