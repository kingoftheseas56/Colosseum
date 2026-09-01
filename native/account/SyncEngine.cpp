// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncEngine.h"

#include "SyncOwnershipInventory.h"
#include "SyncPayloadFirewall.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
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
                            "unsupported_schema_version"))) {
                clearError();
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
    m_networkBusy = false;
    m_active = true;
    m_initialPullPending = true;
    m_pullHasMore = false;
    m_signOutFlushRequested = false;
    m_retryAttempt = 0;

    clearError();
    setState(State::Idle);

    QString reconcileError;
    if (!reconcileAllAdapters(
            &reconcileError)) {
        setBlocked(
            QStringLiteral(
                "adapter_snapshot_failed"),
            reconcileError);
        if (error)
            *error = reconcileError;
        return false;
    }

    persistState();
    return true;
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

    QString reconcileError;
    const bool reconciled =
        reconcileAllAdapters(
            &reconcileError);

    if (!m_statePath.isEmpty())
        persistState();

    QString flushError;
    const bool persisted =
        m_stateStore.flush(
            &flushError);

    m_pendingPersistenceGenerations.clear();
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
                    "unsupported_schema_version");

        if (!adapterMayHaveChanged)
            return;

        clearError();
        setState(State::Idle);
    }

    m_retryTimer.stop();
    m_initialPullPending = true;
    maybeRunNetwork();
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
    if (!reconcileAllAdapters(
            &reconcileError)) {
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

QString SyncEngine::attachmentId() const {
    return m_persistent.attachmentId;
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
    clearError();
    setState(State::Idle);
    persistState();
}

void SyncEngine::handleLocalMutation(
    const QString &categoryId,
    quint64 revision) {
    Q_UNUSED(revision);

    if (!m_active)
        return;

    if (m_disabledCategories.contains(categoryId)) {
        persistState();
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
}

bool SyncEngine::validateLoadedState(
    QString *error) const {
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
            m_persistent.cursor);
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

    const int count =
        static_cast<int>(
            qMin<qsizetype>(
                m_persistent.outbox.size(),
                kPushBatchLimit));

    QJsonArray mutations;
    QStringList mutationIds;

    for (int index = 0;
         index < count;
         ++index) {
        const SyncWireMutation &mutation =
            m_persistent.outbox.at(index);

        if (m_disabledCategories.contains(mutation.category))
            continue;

        mutations.append(
            syncWireMutationToJson(
                mutation));
        mutationIds.append(
            mutation.mutationId);
    }

    if (mutations.isEmpty()) {
        m_persistent.outbox.erase(
            std::remove_if(m_persistent.outbox.begin(),
                           m_persistent.outbox.end(),
                           [this](const SyncWireMutation &mutation) {
                               return m_disabledCategories.contains(mutation.category);
                           }),
            m_persistent.outbox.end());
        persistState();
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
            m_persistent.attachmentModeActive
                ? m_persistent.attachmentId
                : QString());
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

    for (const SyncWirePullEntry &entry :
         response->entries) {
        if (entry.serverSeq
            <= m_persistent.cursor) {
            continue;
        }

        // Poison guard: a remote HLC far in the future would permanently
        // inflate the local hybrid clock (persisted), making every later
        // local mutation clock_skew-rejected. Reject the pull instead.
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
            && !applyPullEntry(
                entry,
                errorCode,
                errorMessage)) {
            return false;
        }

        m_persistent.cursor =
            entry.serverSeq;
    }

    m_pullHasMore =
        response->hasMore;

    if (!m_pullHasMore)
        m_initialPullPending = false;

    if (!m_pullHasMore && !m_categoryReplayInProgress.isEmpty()
        && !finishCategoryReplay(m_categoryReplayInProgress,
                                 errorCode, errorMessage))
        return false;

    return true;
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
            && !applyPullEntry(
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

        if (errorCode) {
            *errorCode =
                it->code.isEmpty()
                ? QStringLiteral(
                      "sync_mutation_rejected")
                : it->code;
        }

        if (errorMessage) {
            *errorMessage =
                it->message.isEmpty()
                ? QStringLiteral(
                      "A sync mutation was rejected.")
                : it->message;
        }
        return false;
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

    emit observationChanged(
        m_state,
        pendingOutboxCount());
    return true;
}

bool SyncEngine::applyPullEntry(
    const SyncWirePullEntry &entry,
    QString *errorCode,
    QString *errorMessage) {
    const SyncWireMutation &mutation =
        entry.mutation;

    if (!entry.canonical) {
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

quint64 SyncEngine::persistState() {
    persistClockIntoState();

    if (m_statePath.isEmpty())
        return 0;

    const quint64 generation =
        m_stateStore.saveAsync(
            m_statePath,
            m_persistent);

    m_pendingPersistenceGenerations
        .insert(generation);
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
