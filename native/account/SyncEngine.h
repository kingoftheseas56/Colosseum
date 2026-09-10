#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountClient.h"
#include "ProfilePaths.h"
#include "SyncAdapterRegistry.h"
#include "SyncHybridClock.h"
#include "SyncStateStore.h"

#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>
#include <optional>

class SyncEngine final : public QObject {
    Q_OBJECT

public:
    enum class State {
        Inactive,
        Idle,
        Retrying,
        Blocked
    };
    Q_ENUM(State)

    explicit SyncEngine(
        AccountClient *client,
        SyncAdapterRegistry *registry,
        std::function<qint64()> nowProvider = {},
        QObject *parent = nullptr);

    bool start(
        const ProfilePaths &profile,
        const QString &deviceId,
        QString *error = nullptr);

    bool stopPreservingOutbox(
        QString *error = nullptr);

    void requestImmediateSync();
    // Explicit recovery path for a server capability/schema upgrade. A normal
    // scheduled sync keeps durable rejection markers in place; this operation
    // clears those markers once and lets the existing outbox retry.
    void retryRejectedMutations();
    void beginSignOutFlush();

    void setAutomaticSchedulingEnabled(
        bool enabled);

    void setNetworkEnabled(
        bool enabled);

    void setCategoryNetworkEnabled(
        const QString &categoryId,
        bool enabled);

    bool categoryNetworkEnabled(
        const QString &categoryId) const;

    State state() const;
    QString stateName() const;
    int pendingOutboxCount() const;
    quint64 cursor() const;
    bool active() const;
    QString lastErrorCode() const;
    QString lastErrorMessage() const;
    int quarantinedEntryCount() const;
    int rejectedMutationCount() const;
    bool historicalReplayPending() const;
    bool recoveryAvailable() const;

signals:
    void observationChanged(
        SyncEngine::State state,
        int pendingOutboxCount);

    void signOutFlushFinished(
        bool drained,
        const QString &errorCode,
        const QString &message);

    void accessTokenRejected();

    void recoveryAvailableChanged();

private:
    enum class NetworkPhase {
        None,
        Pull,
        Push
    };

    struct RequestContext {
        quint64 requestId = 0;
        qint64 sentLocalMs = 0;
        NetworkPhase phase = NetworkPhase::None;
        QStringList mutationIds;
    };

    struct PullProcessingContext {
        QList<SyncWirePullEntry> entries;
        bool hasMore = false;
        bool replayingHistorical = false;
        quint64 historicalLimit = 0;
        qsizetype index = 0;
        bool replayReachedLimit = false;
        QString firstWarningCode;
        QString firstWarningMessage;
    };

    using OwnerApplyContinuation =
        std::function<void(bool, const SyncAdapterRegistryError &)>;

    struct OwnerApplyContext {
        SyncWirePullEntry entry;
        bool replayingHistorical = false;
        bool fromQuarantine = false;
        bool recovery = false;
        quint64 profileGeneration = 0;
        OwnerApplyContinuation continuation;
    };

    void handleClientCompleted(
        quint64 requestId,
        AccountOperation operation,
        quint64 accessTokenGeneration,
        const AccountTransportReply &reply);

    void handleLocalMutation(
        const QString &categoryId,
        quint64 revision);

    bool validateLoadedState(
        QString *error) const;

    bool reconcileAllAdapters(
        QString *error = nullptr,
        bool allowSnapshotDeletes = false);

    bool reconcileCategory(
        const QString &categoryId,
        QString *error = nullptr,
        bool allowSnapshotDeletes = false);

    void enqueueMutation(
        const QString &categoryId,
        const QString &recordKey,
        int schemaVersion,
        SyncWireOperation operation,
        const QJsonValue &payload,
        qint64 localOrderMs = -1);

    void maybeRunNetwork();
    void beginPull();
    void beginPush();

    bool processPullReply(
        const AccountTransportReply &reply,
        QString *errorCode,
        QString *errorMessage);

    bool continuePullProcessing(
        QString *errorCode = nullptr,
        QString *errorMessage = nullptr);

    void finishPullProcessing(
        bool processed,
        const QString &errorCode,
        const QString &errorMessage);

    void beginDurableOwnerApply(
        const SyncWirePullEntry &entry,
        bool replayingHistorical,
        bool fromQuarantine,
        bool recovery,
        OwnerApplyContinuation continuation);

    void handleOwnerApplyCompletion(
        const SyncAdapterRegistryError &result);

    void recordWinningState(
        const SyncWirePullEntry &entry);

    void removeOwnerRedo(
        quint64 serverSeq);

    void beginOwnerRedoRecovery();
    void finishStartAfterOwnerRedo();
    void continueQuarantineReplay();

    bool processPushReply(
        const AccountTransportReply &reply,
        QString *errorCode,
        QString *errorMessage);

    bool applyWinningPullEntry(
        const SyncWirePullEntry &entry,
        QString *errorCode,
        QString *errorMessage,
        SyncAdapterFailureClass *failureClass = nullptr);

    bool finishCategoryReplay(
        const QString &categoryId,
        QString *errorCode,
        QString *errorMessage);

    bool replayQuarantinedCategory(
        const QString &categoryId,
        QString *errorCode,
        QString *errorMessage);

    void rebasePendingMutations();

    quint64 persistState(
        std::function<void(bool, const QString &)> callback = {});
    void persistClockIntoState();

    void handlePersistenceCommitted(
        quint64 generation);

    void handlePersistenceFailed(
        quint64 generation,
        const QString &message);

    void setState(
        State state);

    void setBlocked(
        const QString &code,
        const QString &message);

    void setRetrying(
        const QString &code,
        const QString &message);

    void clearError();
    void scheduleRetry();
    bool allOutboxEntriesParked() const;
    void completeSignOutFlushIfPossible();

    qint64 nowMs() const;

    static QString stateName(
        State state);

    static bool isSuccess(
        const AccountTransportReply &reply);

    AccountClient *m_client = nullptr;
    SyncAdapterRegistry *m_registry = nullptr;
    std::function<qint64()> m_nowProvider;

    SyncStateStore m_stateStore;
    SyncPersistentState m_persistent;
    SyncHybridClock m_clock;

    ProfilePaths m_profile =
        ProfilePaths::sealed();
    QString m_statePath;
    QString m_deviceId;

    QTimer m_retryTimer;
    bool m_automaticSchedulingEnabled = true;
    bool m_networkEnabled = false;
    bool m_active = false;
    bool m_initialPullPending = false;
    bool m_networkBusy = false;
    bool m_signOutFlushRequested = false;
    bool m_pullHasMore = false;

    QSet<QString> m_disabledCategories;
    QSet<QString> m_requestedDisabledCategories;
    QString m_categoryReplayInProgress;
    QStringList m_categoryReplayQueue;

    State m_state = State::Inactive;
    QString m_lastErrorCode;
    QString m_lastErrorMessage;

    QSet<quint64> m_pendingPersistenceGenerations;
    QHash<quint64, std::function<void(bool, const QString &)>>
        m_persistenceCallbacks;
    int m_retryAttempt = 0;

    quint64 m_profileGeneration = 0;
    std::optional<PullProcessingContext> m_pullProcessing;
    std::optional<OwnerApplyContext> m_ownerApply;
    bool m_ownerRedoRecoveryInProgress = false;
    bool m_ownerRedoBatchReady = false;
    bool m_ownerRedoBatchPreparing = false;
    bool m_startFinalizationPending = false;
    QString m_quarantineReplayCategory;
    bool m_quarantineReplayRunning = false;
    QSet<quint64> m_quarantineReplaySkipped;
    QStringList m_quarantineReplayCategories;

    RequestContext m_request;
};

Q_DECLARE_METATYPE(SyncEngine::State)
