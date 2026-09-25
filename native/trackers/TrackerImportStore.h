#pragma once

#include "TrackerMappingStore.h"
#include "TrackerConnectionStore.h"

#include "account/ProfilePaths.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

enum class TrackerImportClassification : quint8 {
    NewProgress,
    ExactMatch,
    RemoteAdvance,
    Disagreement,
    NeedsMatching,
    Unsupported,
    Duplicate
};

enum class TrackerImportResolution : quint8 {
    None,
    UseProviderProgress,
    KeepColosseum,
    LeaveUnresolved
};

enum class TrackerImportItemState : quint8 {
    ReviewRequired,
    AwaitingApply,
    Applying,
    Applied,
    KeptLocal,
    Unresolved,
    NonMutating,
    Superseded
};

// A provider's watched-count is not a Continue value. This optional payload is
// present only when the provider fact identifies an exact Colosseum resume
// record and a value that ProgressStore can represent without inventing a
// resume location.
struct TrackerImportProgressTarget {
    QString canonicalMediaId;
    QString kind;
    QString id;
    double fraction = 0.0;
    bool completed = false;
};

// This is deliberately the smallest projection the import journal needs. It
// is an adapter boundary, not a second Progress owner or a QML DTO.
struct TrackerImportedProgressValue {
    QString canonicalMediaId;
    QString historyKind;
    QString historyId;
    int progress = 0;
    bool completed = false;
    qint64 revision = 0;
    bool nativeWitnessedHistory = false;
    std::optional<TrackerImportProgressTarget> exactProgressTarget;
    // Digest of the exact local Progress record, including metadata and
    // provenance. The owner compares this at apply time to reject any edit
    // made after preview, even when progress and updatedAt happen to match.
    QString ownerRevisionToken;
};

struct TrackerImportRemoteItem {
    QString providerItemId;
    TrackerRemoteMediaKey remote;
    std::optional<TrackerTitleMapping> mapping;
    int progress = 0;
    bool completed = false;
    bool supported = true;
    bool duplicate = false;
    bool contradictsNativeHistory = false;
    std::optional<TrackerImportedProgressValue> localAtPreview;
    // Sanitized provider title used only for the review surface. Raw provider
    // payloads and opaque remote IDs remain inside the native import journal.
    QString displayTitle;
    std::optional<TrackerImportProgressTarget> exactProgressTarget;
};

struct TrackerImportBatchDraft {
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString remoteAccountId;
    quint64 connectionGeneration = 0;
    QString snapshotId;
    QString proposedCursor;
    bool initialImport = true;
    bool pageComplete = true;
    QList<TrackerImportRemoteItem> items;
    QString baseCursor;
};

struct TrackerImportItem {
    QString itemId;
    QString operationId;
    TrackerImportRemoteItem remote;
    TrackerImportClassification classification = TrackerImportClassification::NeedsMatching;
    TrackerImportResolution resolution = TrackerImportResolution::None;
    TrackerImportItemState state = TrackerImportItemState::ReviewRequired;
    std::optional<TrackerImportedProgressValue> localAfterSettlement;
    bool localBaselineCaptured = false;
};

struct TrackerImportBatch {
    QString batchId;
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString remoteAccountId;
    quint64 connectionGeneration = 0;
    QString snapshotId;
    QString proposedCursor;
    bool initialImport = true;
    bool pageComplete = true;
    bool confirmed = false;
    QList<TrackerImportItem> items;
    QString baseCursor;
    bool cursorCommitted = false;
};

enum class TrackerImportOwnerApplyResult : quint8 {
    Applied,
    AlreadyApplied,
    Stale,
    Unsupported,
    Failed
};

// A concrete native Progress owner implements this later. The journal writes
// `Applying` before this call. Operation IDs bind the canonical target and
// progress effect; an owner must reject receipt reuse for a different effect.
class TrackerImportOwner
{
public:
    using ApplyCallback = std::function<void(
        TrackerImportOwnerApplyResult,
        std::optional<TrackerImportedProgressValue>,
        const QString &)>;
    using ProgressRemovalCallback = std::function<void(bool, int, const QString &)>;

    virtual ~TrackerImportOwner() = default;
    virtual std::optional<TrackerImportedProgressValue> currentProgress(
        const TrackerTitleMapping &mapping) const = 0;
    virtual std::optional<TrackerImportedProgressValue> currentProgress(
        const TrackerTitleMapping &mapping,
        const TrackerImportProgressTarget &) const
    {
        return currentProgress(mapping);
    }
    // The canonical owner performs the expected-state comparison and write as
    // one operation. It preserves nativeWitnessedHistory from expectedLocal;
    // the import journal must never split that decision into a read and write.
    virtual TrackerImportOwnerApplyResult applyImportedProgress(
        const QString &operationId,
        const TrackerTitleMapping &mapping,
        const std::optional<TrackerImportedProgressValue> &expectedLocal,
        int progress,
        bool completed,
        std::optional<TrackerImportedProgressValue> *resultingProgress = nullptr,
        QString *error = nullptr) = 0;
    virtual TrackerImportOwnerApplyResult applyImportedProgressWithTarget(
        const QString &operationId,
        const TrackerTitleMapping &mapping,
        const TrackerImportProgressTarget &,
        const std::optional<TrackerImportedProgressValue> &expectedLocal,
        int progress,
        bool completed,
        std::optional<TrackerImportedProgressValue> *resultingProgress = nullptr,
        QString *error = nullptr)
    {
        return applyImportedProgress(operationId, mapping, expectedLocal,
                                     progress, completed, resultingProgress, error);
    }
    virtual void applyImportedProgressAsync(
        const QString &operationId,
        const TrackerTitleMapping &mapping,
        const TrackerImportProgressTarget &target,
        const std::optional<TrackerImportedProgressValue> &expectedLocal,
        int progress,
        bool completed,
        ApplyCallback callback)
    {
        std::optional<TrackerImportedProgressValue> result;
        QString error;
        const TrackerImportOwnerApplyResult status = applyImportedProgressWithTarget(
            operationId, mapping, target, expectedLocal, progress, completed, &result, &error);
        if (callback)
            callback(status, std::move(result), error);
    }
    virtual int importedProgressCount(TrackerProviderId, const QString &) const
    {
        return 0;
    }
    virtual QVariantList importedProgressRemovalPreview(
        TrackerProviderId, const QString &) const
    {
        return {};
    }
    virtual bool importedProgressSourceRemovalSuppressed(
        TrackerProviderId, const QString &) const
    {
        return false;
    }
    virtual void removeImportedProgressAsync(TrackerProviderId,
                                             const QString &,
                                             ProgressRemovalCallback callback)
    {
        if (callback)
            callback(false, 0, QStringLiteral(
                "This Progress owner does not support tracker-source removal."));
    }
};

// Profile-private preview, review, and receipt journal. It does not own
// canonical Progress, History, Activity, statistics, account sync, or any
// provider transport. Its only effect boundary is TrackerImportOwner.
class TrackerImportStore final : public QObject
{
public:
    using CompletionCallback = std::function<void(bool, const QString &)>;

    TrackerImportStore(const ProfilePaths &profile,
                       const TrackerMappingStore *mappings,
                       const TrackerConnectionStore *connections);

    static QString storagePath(const ProfilePaths &profile);

    bool healthy(QString *error = nullptr) const;
    std::optional<TrackerImportBatch> createPreview(const TrackerImportBatchDraft &draft,
                                                     QString *error = nullptr);
    std::optional<TrackerImportBatch> batch(const QString &batchId) const;
    QList<TrackerImportBatch> batches() const;
    bool adoptPrivateStateFrom(const TrackerImportStore &source,
                               QString *error = nullptr);
    bool resolve(const QString &batchId,
                 const QString &itemId,
                 TrackerImportResolution resolution,
                 QString *error = nullptr);
    bool resolveSelected(const QString &batchId,
                         const QStringList &itemIds,
                         TrackerImportResolution resolution,
                         QString *error = nullptr);
    bool resolveAll(const QString &batchId,
                    TrackerImportClassification classification,
                    TrackerImportResolution resolution,
                    QString *error = nullptr);
    bool confirm(const QString &batchId, QString *error = nullptr);
    bool applyConfirmed(const QString &batchId, TrackerImportOwner *owner,
                        QString *error = nullptr);
    bool applyRoutineSafe(const QString &batchId,
                          TrackerImportOwner *owner,
                          bool automaticPullApproved,
                          QString *error = nullptr);
    void applyConfirmedAsync(const QString &batchId,
                             TrackerImportOwner *owner,
                             CompletionCallback callback);
    void applyRoutineSafeAsync(const QString &batchId,
                               TrackerImportOwner *owner,
                               bool automaticPullApproved,
                               CompletionCallback callback);
    bool recover(TrackerImportOwner *owner, QString *error = nullptr);
    void recoverAsync(TrackerImportOwner *owner, CompletionCallback callback);
    std::optional<QString> confirmedCursor(TrackerProviderId providerId,
                                           const QString &remoteAccountId) const;

#ifdef COLOSSEUM_TRACKER_IMPORT_TESTING
    void forcePersistenceFailureForTesting(bool enabled) { m_forcePersistenceFailure = enabled; }
#endif

private:
    bool load();
    bool persist(const QList<TrackerImportBatch> &batches,
                 const QHash<QString, QString> &cursors,
                 QString *error) const;
    bool applyBatch(const QString &batchId, TrackerImportOwner *owner, QString *error);
    void applyBatchAsync(const QString &batchId,
                         TrackerImportOwner *owner,
                         CompletionCallback callback);
    bool commitBatch(const TrackerImportBatch &batch,
                     const QHash<QString, QString> &cursors,
                     QString *error);

    ProfilePaths m_profile;
    const TrackerMappingStore *m_mappings = nullptr;
    const TrackerConnectionStore *m_connections = nullptr;
    QString m_path;
    QList<TrackerImportBatch> m_batches;
    QHash<QString, QString> m_cursors;
    bool m_healthy = true;
    QString m_error;
    QSet<QString> m_asyncBatches;
#ifdef COLOSSEUM_TRACKER_IMPORT_TESTING
    bool m_forcePersistenceFailure = false;
#endif
};
