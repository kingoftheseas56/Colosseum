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
    for (auto it = m_persistent.pausedCategories.constBegin();
         it != m_persistent.pausedCategories.constEnd(); ++it) {
        if (it->replaying && !m_disabledCategories.contains(it.key())) {
            m_disabledCategories.insert(it.key());
            m_categoryReplayInProgress = it.key();
        }
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
        for (const SyncAdapterRecord &record : snapshot.records) {
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
        for (const SyncAdapterRecord &record : snapshot.records)
            paused.localBaseline.insert(record.recordKey,
                SyncMirrorRecord{snapshot.schemaVersion, record.payload});
        m_persistent.pausedCategories.insert(category, paused);
        m_disabledCategories.insert(category);
        for (int index = m_persistent.outbox.size() - 1; index >= 0; --index)
            if (m_persistent.outbox.at(index).category == category)
                m_persistent.outbox.removeAt(index);
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
    QHash<QString, SyncAdapterRecord> current;
    for (const SyncAdapterRecord &record : snapshot.records)
        current.insert(record.recordKey, record);
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
    replay.replaying = true;
    m_persistent.pausedCategories.insert(category, replay);
    m_disabledCategories.insert(category);
    m_categoryReplayInProgress = category;
    m_persistent.winners.remove(category);
    m_persistent.mirrors.remove(category);
    m_persistent.cursor = 0;
    m_initialPullPending = true;
    m_pullHasMore = false;
    persistState();
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
                != AccountOperation::SyncPush)) {
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

    if (phase == NetworkPhase::Pull) {
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
    if (!reconcileCategory(
            categoryId,
            &error,
            true)) {
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

    for (const SyncAdapterRecord &record :
         snapshot.records) {
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
        || m_quarantineReplayRunning) {
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
    bool markerChanged = removedDisabled;
    bool markedOversize = false;
    QString warningCode;
    QString warningMessage;

    for (const SyncWireMutation &mutation :
         std::as_const(m_persistent.outbox)) {
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
    m_request.requestId =
        m_client->pushSync(
            mutations);
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
            if (candidate.mutation.hlc.physicalMs
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
                    && compareSyncWireHlc(
                           winnerIt->hlc,
                           candidate.mutation.hlc) >= 0) {
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

        if (entry.mutation.hlc.physicalMs
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

        const auto categoryIt = m_persistent.winners.constFind(entry.mutation.category);
        const auto winnerIt = categoryIt == m_persistent.winners.constEnd()
            ? QHash<QString, SyncWinner>::const_iterator()
            : categoryIt->constFind(entry.mutation.recordKey);
        const bool alreadyWon = categoryIt != m_persistent.winners.constEnd()
            && winnerIt != categoryIt->constEnd()
            && compareSyncWireHlc(winnerIt->hlc, entry.mutation.hlc) >= 0;

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
    winner.hlc = mutation.hlc;
    winner.schemaVersion = mutation.schemaVersion;
    winner.operation = mutation.operation;
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
        SyncWirePullEntry{redo.serverSeq, redo.won, redo.mutation},
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
            && compareSyncWireHlc(
                   winnerIt->hlc,
                   mutation.hlc)
                >= 0) {
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
    QHash<QString, SyncAdapterRecord> current;
    for (const SyncAdapterRecord &record : snapshot.records)
        current.insert(record.recordKey, record);
    const auto remote = m_persistent.mirrors.value(categoryId);
    const SyncPausedCategoryState replay = pausedIt.value();

    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        const auto remoteIt = remote.constFind(it.key());
        if (remoteIt != remote.constEnd()
            && remoteIt->schemaVersion == snapshot.schemaVersion
            && remoteIt->payload == it->payload)
            continue;
        if (!remote.contains(it.key())) {
            if (snapshot.missingRecordsAreDeletes) {
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
