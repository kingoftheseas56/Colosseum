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

signals:
    void observationChanged(
        SyncEngine::State state,
        int pendingOutboxCount);

    void signOutFlushFinished(
        bool drained,
        const QString &errorCode,
        const QString &message);

    void accessTokenRejected();

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

    bool processPushReply(
        const AccountTransportReply &reply,
        QString *errorCode,
        QString *errorMessage);

    bool applyWinningPullEntry(
        const SyncWirePullEntry &entry,
        QString *errorCode,
        QString *errorMessage);

    bool finishCategoryReplay(
        const QString &categoryId,
        QString *errorCode,
        QString *errorMessage);

    void rebasePendingMutations();

    quint64 persistState();
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

    State m_state = State::Inactive;
    QString m_lastErrorCode;
    QString m_lastErrorMessage;

    QSet<quint64> m_pendingPersistenceGenerations;
    int m_retryAttempt = 0;

    RequestContext m_request;
};

Q_DECLARE_METATYPE(SyncEngine::State)
