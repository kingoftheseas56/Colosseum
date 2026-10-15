#pragma once

#include "TrackerImportStore.h"

class ProgressStore;

// Bridges the import journal to Colosseum's canonical Continue/Progress
// owner. Provider aggregates without an exact native record never enter this
// adapter; every accepted operation writes one exact record and an idempotent
// receipt through ProgressStore's background writer.
class TrackerProgressImportOwner final : public TrackerImportOwner,
                                         public TrackerCanonicalTitleIndex
{
public:
    explicit TrackerProgressImportOwner(ProgressStore *progress);

    std::optional<TrackerImportedProgressValue> currentProgress(
        const TrackerTitleMapping &mapping) const override;
    std::optional<TrackerImportedProgressValue> currentProgress(
        const TrackerTitleMapping &mapping,
        const TrackerImportProgressTarget &target) const override;

    TrackerImportOwnerApplyResult applyImportedProgress(
        const QString &operationId,
        const TrackerTitleMapping &mapping,
        const std::optional<TrackerImportedProgressValue> &expectedLocal,
        int progress,
        bool completed,
        std::optional<TrackerImportedProgressValue> *resultingProgress = nullptr,
        QString *error = nullptr) override;

    TrackerImportOwnerApplyResult applyImportedProgressWithTarget(
        const QString &operationId,
        const TrackerTitleMapping &mapping,
        const TrackerImportProgressTarget &target,
        const std::optional<TrackerImportedProgressValue> &expectedLocal,
        int progress,
        bool completed,
        std::optional<TrackerImportedProgressValue> *resultingProgress = nullptr,
        QString *error = nullptr) override;

    void applyImportedProgressAsync(
        const QString &operationId,
        const TrackerTitleMapping &mapping,
        const TrackerImportProgressTarget &target,
        const std::optional<TrackerImportedProgressValue> &expectedLocal,
        int progress,
        bool completed,
        ApplyCallback callback) override;

    int importedProgressCount(TrackerProviderId providerId,
                              const QString &remoteAccountId) const override;
    QVariantList importedProgressRemovalPreview(
        TrackerProviderId providerId,
        const QString &remoteAccountId) const override;
    bool importedProgressSourceRemovalSuppressed(
        TrackerProviderId providerId, const QString &remoteAccountId) const override;
    void removeImportedProgressAsync(TrackerProviderId providerId,
                                     const QString &remoteAccountId,
                                     ProgressRemovalCallback callback) override;

    // Find Match is intentionally limited to named titles that already have
    // native Progress rows. Provider identities are never guessed here.
    std::optional<TrackerCanonicalTitleCandidate> exactCandidate(
        const TrackerRemoteMediaKey &remote) const override;
    bool candidateSearchAvailable() const override;
    QList<TrackerCanonicalTitleCandidate> userCandidates() const override;

private:
    static bool validTarget(const TrackerTitleMapping &mapping,
                            const TrackerImportProgressTarget &target);
    static TrackerImportedProgressValue importedValue(
        const TrackerTitleMapping &mapping,
        const TrackerImportProgressTarget &target,
        int providerProgress,
        bool providerCompleted,
        bool nativeWitnessedHistory,
        const QVariantMap &entry);

    ProgressStore *m_progress = nullptr;
};
