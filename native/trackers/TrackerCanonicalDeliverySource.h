#pragma once

#include "TrackerDeliveryStore.h"

#include <QVariantMap>

class ActivityStore;
class HistoryStore;
class ProgressStore;

// Read-only projection from Colosseum's canonical Progress, Activity, and
// History owners. Imported/account-synced rows remain visible as ineligible
// facts; they are never reclassified as locally witnessed tracker writes.
class TrackerCanonicalDeliverySource final : public TrackerDeliverySource
{
public:
    TrackerCanonicalDeliverySource(ProgressStore *progress,
                                   ActivityStore *activity,
                                   HistoryStore *history);

    bool isReady() const override;
    QList<TrackerDeliveryFact> currentCommittedFacts() const override;
    bool isDurablyCurrent(const TrackerDeliveryFact &fact) const override;
    std::optional<TrackerDeliveryFact> currentProgressFact(
        const QString &kind, const QString &id) const;
    std::optional<TrackerDeliveryFact> currentActivityCompletionFact(
        const QString &eventId) const;
    std::optional<TrackerDeliveryFact> currentHistoryCompletionFact(
        const QString &eventId) const;

private:
    std::optional<QList<TrackerDeliveryFact>> buildFacts() const;
    bool hasEarlierSessionCompletionForActivity(const QVariantMap &activityEvent) const;
    bool hasEarlierSessionCompletionForHistory(const QVariantMap &historyEvent) const;

    ProgressStore *m_progress = nullptr;
    ActivityStore *m_activity = nullptr;
    HistoryStore *m_history = nullptr;
};
