#pragma once

#include "TrackerConnectionStore.h"
#include "TrackerMappingStore.h"

#include "account/ProfilePaths.h"

#include <QHash>
#include <QStringList>

#include <optional>

enum class TrackerDeliveryFactKind : quint8 {
    Progress,
    Completion
};

enum class TrackerDeliveryOrigin : quint8 {
    Unknown,
    NativeLocal,
    TrackerImport,
    AccountSync
};

enum class TrackerMediaDomain : quint8 {
    Unknown,
    Anime,
    Manga,
    Movie,
    Television
};

struct TrackerDeliveryFact {
    QString canonicalMediaId;
    QString historyKind;
    QString historyId;
    TrackerDeliveryFactKind kind = TrackerDeliveryFactKind::Progress;
    quint64 sourceRevision = 0;
    QString sourceEventId;
    int progress = 0;
    QString contentFingerprint;
    TrackerDeliveryOrigin origin = TrackerDeliveryOrigin::NativeLocal;
    TrackerMediaDomain mediaDomain = TrackerMediaDomain::Unknown;
};

// The canonical owner supplies only facts which have a durable local receipt.
// The delivery journal rechecks the same binding immediately before send.
class TrackerDeliverySource
{
public:
    virtual ~TrackerDeliverySource() = default;
    virtual bool isReady() const { return true; }
    virtual QList<TrackerDeliveryFact> currentCommittedFacts() const = 0;
    virtual bool isDurablyCurrent(const TrackerDeliveryFact &fact) const = 0;
};

enum class TrackerExportIneligibleReason : quint8 {
    None,
    SourceChanged,
    NonNativeOrigin,
    MissingMapping,
    AmbiguousMapping,
    Unsupported,
    RemoteAlreadyCurrent
};

struct TrackerRemoteDeliveryState {
    QString remoteMediaId;
    TrackerDeliveryFactKind factKind = TrackerDeliveryFactKind::Progress;
    QString sourceEventId;
    bool present = false;
    bool exactlyMatchesIntendedState = false;
    QString stateFingerprint;
    QString safeSummary;
};

struct TrackerRemoteDeliverySnapshot {
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString remoteAccountId;
    quint64 connectionGeneration = 0;
    QString snapshotId;
    qint64 observedAtMs = 0;
    bool completeForMappedItems = false;
    QList<TrackerRemoteDeliveryState> items;
};

struct TrackerExportPreviewItem {
    QString itemId;
    TrackerDeliveryFact fact;
    std::optional<TrackerTitleMapping> mapping;
    bool eligible = false;
    TrackerExportIneligibleReason reason = TrackerExportIneligibleReason::None;
    bool remoteStateKnown = false;
    bool remotePresent = false;
    bool remoteExactlyMatches = false;
    bool willChangeRemote = false;
    QString remoteStateFingerprint;
    QString remoteStateSummary;
};

struct TrackerExportPreview {
    QString previewId;
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString remoteAccountId;
    quint64 connectionGeneration = 0;
    TrackerRemoteDeliverySnapshot remoteSnapshot;
    QList<TrackerExportPreviewItem> items;
};

enum class TrackerDeliveryState : quint8 {
    Pending,
    Delivering,
    Retrying,
    Succeeded,
    NeedsAttention,
    FailedTerminal,
    UnknownOutcome
};

enum class TrackerDeliveryReason : quint8 {
    None,
    ProviderRetryable,
    ProviderRateLimited,
    AuthenticationRequired,
    UnsupportedAction,
    TerminalProviderRefusal,
    AcknowledgementLost,
    ReadbackPresent,
    ReadbackAbsent,
    ReadbackDifferent,
    ReadbackUncertain,
    StaleLocalFact,
    MappingChanged,
    RetryLimitReached,
    DestinationReviewRequired
};

struct TrackerDeliveryOperation {
    QString operationId;
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString remoteAccountId;
    quint64 connectionGeneration = 0;
    TrackerTitleMapping mapping;
    TrackerDeliveryFact fact;
    TrackerDeliveryState state = TrackerDeliveryState::Pending;
    int attemptCount = 0;
    qint64 nextAttemptAtMs = 0;
    TrackerDeliveryReason reason = TrackerDeliveryReason::None;
    QString reviewedRemoteSnapshotId;
    QString reviewedRemoteStateFingerprint;
    bool reviewedRemoteStatePresent = false;
    bool adoptedReceipt = false;
};

struct TrackerDeliveryProviderPreference {
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString remoteAccountId;
    qint64 firstExportConfirmedAtMs = 0;
    bool sendEnabled = false;
    QHash<QString, QString> reviewedFactBindings;
};

enum class TrackerDeliveryAttemptResult : quint8 {
    Succeeded,
    RetryableKnownNotApplied,
    RateLimitedKnownNotApplied,
    NeedsAttention,
    FailedTerminal,
    UnknownOutcome
};

enum class TrackerDeliveryReadback : quint8 {
    // ExactPresent is valid only when the adapter has verified the mapped item
    // and exact intended state, not merely that some provider record exists.
    ExactPresent,
    Absent,
    PresentDifferent,
    Indeterminate
};

// Profile-private consent, outbox, cooldown, and receipt journal. It never
// owns canonical Progress, History, Activity, statistics, or provider transport.
class TrackerDeliveryStore final
{
public:
    TrackerDeliveryStore(const ProfilePaths &profile,
                         const TrackerMappingStore *mappings,
                         const TrackerConnectionStore *connections);

    static QString storagePath(const ProfilePaths &profile);
    static bool supportsProviderDelivery(TrackerProviderId providerId,
                                        TrackerMediaDomain domain);

    bool healthy(QString *error = nullptr) const;
    std::optional<TrackerExportPreview> createExportPreview(
        TrackerProviderId providerId,
        const QString &remoteAccountId,
        quint64 connectionGeneration,
        const QList<TrackerDeliveryFact> &facts,
        const TrackerRemoteDeliverySnapshot &remoteSnapshot,
        const TrackerDeliverySource *source,
        QString *error = nullptr);
    bool confirmExport(const QString &previewId,
                       const QStringList &selectedItemIds,
                       const TrackerRemoteDeliverySnapshot &currentRemoteSnapshot,
                       const TrackerDeliverySource *source,
                       qint64 confirmedAtMs,
                       QString *error = nullptr);
    bool hasFirstExportConsent(TrackerProviderId providerId,
                               const QString &remoteAccountId) const;
    bool providerSendEnabled(TrackerProviderId providerId,
                             const QString &remoteAccountId) const;
    bool setProviderSendEnabled(TrackerProviderId providerId,
                                const QString &remoteAccountId,
                                bool enabled,
                                QString *error = nullptr);
    bool removeProviderSettingsFor(TrackerProviderId providerId,
                                   const QString &remoteAccountId,
                                   QString *error = nullptr);

    bool observeCommittedFact(const TrackerDeliveryFact &fact,
                              const TrackerDeliverySource *source,
                              QString *operationId = nullptr,
                              QString *error = nullptr);
    bool recoverSourceGap(const TrackerDeliverySource *source,
                          int *enqueuedCount = nullptr,
                          QString *error = nullptr);

    QList<TrackerDeliveryOperation> operations() const;
    std::optional<TrackerDeliveryOperation> operation(const QString &operationId) const;
    bool adoptPrivateStateFrom(const TrackerDeliveryStore &source,
                               QString *error = nullptr);
    QList<TrackerDeliveryOperation> readyOperations(qint64 nowMs) const;
    bool resumeKnownUnsentAfterReconnect(TrackerProviderId providerId,
                                         const QString &remoteAccountId,
                                         quint64 connectionGeneration,
                                         QString *error = nullptr);
    int discardKnownUnsent(TrackerProviderId providerId,
                           const QString &remoteAccountId,
                           QString *error = nullptr);
    bool markDelivering(const QString &operationId,
                        const TrackerDeliverySource *source,
                        qint64 nowMs,
                        QString *error = nullptr);
    bool recordAttemptResult(const QString &operationId,
                             TrackerDeliveryAttemptResult result,
                             qint64 nowMs,
                             qint64 retryAfterAtMs = 0,
                             TrackerDeliveryReason reason = TrackerDeliveryReason::None,
                             QString *error = nullptr);
    bool reconcileUnknown(const QString &operationId,
                          TrackerDeliveryReadback result,
                          qint64 nowMs,
                          QString *error = nullptr);

#ifdef COLOSSEUM_TRACKER_DELIVERY_TESTING
    void forcePersistenceFailureForTesting(bool enabled) { m_forcePersistenceFailure = enabled; }
#endif

private:
    bool load();
    bool persist(const QList<TrackerDeliveryProviderPreference> &preferences,
                 const QList<TrackerDeliveryOperation> &operations,
                 const QHash<QString, qint64> &cooldowns,
                 QString *error) const;
    bool commit(const QList<TrackerDeliveryProviderPreference> &preferences,
                const QList<TrackerDeliveryOperation> &operations,
                const QHash<QString, qint64> &cooldowns,
                QString *error);
    bool enqueueFact(const TrackerDeliveryFact &fact,
                     TrackerProviderId providerId,
                     const QString &remoteAccountId,
                     quint64 connectionGeneration,
                     const TrackerTitleMapping &mapping,
                     QString *operationId,
                     QString *error);

    ProfilePaths m_profile;
    const TrackerMappingStore *m_mappings = nullptr;
    const TrackerConnectionStore *m_connections = nullptr;
    QString m_path;
    QList<TrackerDeliveryProviderPreference> m_preferences;
    QList<TrackerDeliveryOperation> m_operations;
    QHash<QString, qint64> m_cooldowns;
    QHash<QString, TrackerExportPreview> m_previews;
    bool m_healthy = true;
    QString m_error;
#ifdef COLOSSEUM_TRACKER_DELIVERY_TESTING
    bool m_forcePersistenceFailure = false;
#endif
};
