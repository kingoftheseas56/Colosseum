#pragma once

#include "TrackerCanonicalDeliverySource.h"
#include "TrackerConnectionStore.h"
#include "TrackerConnectionService.h"
#include "TrackerDeliveryStore.h"
#include "TrackerMappingStore.h"

#include <QObject>

class ActivityStore;
class HistoryStore;
class ProgressStore;

// Profile-scoped composition root for tracker delivery. It owns one copy of
// the tracker connection/mapping/outbox stores and observes only durable facts
// from Colosseum's canonical owners.
class TrackerDeliveryRuntime final : public QObject
{
    Q_OBJECT
public:
    TrackerDeliveryRuntime(const ProfilePaths &profile,
                           ProgressStore *progress,
                           ActivityStore *activity,
                           HistoryStore *history,
                           QObject *parent = nullptr);

    bool start(QString *error = nullptr);
    bool healthy(QString *error = nullptr) const;
    QString lastError() const;

    TrackerConnectionStore *connectionStore();
    TrackerMappingStore *mappingStore();
    TrackerDeliveryStore *deliveryStore();
    TrackerCanonicalDeliverySource *canonicalSource();
    TrackerConnectionService *connectionService();
    bool refreshCurrentFacts(QString *error = nullptr);

private:
    bool observeCurrentFacts(QString *error);
    void observeFact(const std::optional<TrackerDeliveryFact> &fact);
    bool hasSendEnabledProvider() const;
    void setError(const QString &error);

    TrackerConnectionStore m_connections;
    TrackerMappingStore m_mappings;
    TrackerDeliveryStore m_delivery;
    TrackerCanonicalDeliverySource m_source;
    TrackerConnectionService m_connectionService;
    ProgressStore *m_progress = nullptr;
    ActivityStore *m_activity = nullptr;
    HistoryStore *m_history = nullptr;
    QString m_error;
    bool m_started = false;
};
