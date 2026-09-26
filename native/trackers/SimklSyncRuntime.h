#pragma once

#include "SimklApiClient.h"
#include "TrackerDeliveryStore.h"
#include "TrackerHistoryEvidenceStore.h"
#include "TrackerImportStore.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

class SimklApiClient;
class TrackerCanonicalDeliverySource;
class TrackerConnectionStore;
class TrackerMappingStore;
class TrackerProgressImportOwner;
class TrackerSyncCenterModel;
class TrackerSyncSettingsStore;

// Production SIMKL pull/delivery composition. It translates provider JSON into
// the existing review and receipt journals; those journals remain the owners
// of consent, conflicts, retries, provenance, and recovery.
class SimklSyncRuntime final : public QObject
{
    Q_OBJECT

public:
    using ExportSnapshotCompletion = std::function<void(
        std::optional<TrackerRemoteDeliverySnapshot>)>;

    SimklSyncRuntime(TrackerConnectionStore *connections,
                     TrackerMappingStore *mappings,
                     TrackerImportStore *imports,
                     TrackerProgressImportOwner *importOwner,
                     TrackerHistoryEvidenceStore *historyEvidence,
                     TrackerDeliveryStore *delivery,
                     TrackerCanonicalDeliverySource *deliverySource,
                     TrackerSyncSettingsStore *settings,
                     TrackerSyncCenterModel *syncCenter,
                     SimklApiClient *api,
                     QObject *parent = nullptr);

    void start();
    // Asynchronous remote-state read used by the first-export review and by
    // unknown-outcome reconciliation. It never blocks the GUI thread; an
    // unavailable provider or a failed read delivers std::nullopt.
    void readExportSnapshotAsync(const TrackerConnection &connection,
                                 const QList<TrackerDeliveryFact> &facts,
                                 ExportSnapshotCompletion completion);

public slots:
    void syncAll(const QStringList &providerKeys, quint64 revision);
    void connectionEstablished(const QString &providerKey);

private:
    struct PendingEvidence {
        TrackerRemoteMediaKey remote;
        QString providerEventId;
        QString snapshotId;
        qint64 occurredAtMs = 0;
        TrackerImportedEventKind eventKind = TrackerImportedEventKind::Activity;
        QString fingerprint;
    };

    struct PullState;
    struct SnapshotState;

    void pullSimkl(bool explicitRequest);
    void fetchNext(const std::shared_ptr<PullState> &state);
    void finishPull(const std::shared_ptr<PullState> &state);
    void settleConfirmedEvidence();
    void dispatchNextDelivery();
    void sendDelivery(const TrackerDeliveryOperation &operation);
    void finishDelivery(const QString &operationId,
                        TrackerDeliveryAttemptResult result,
                        TrackerDeliveryReason reason,
                        qint64 retryAfterMs = 0);
    void reconcileUnknownDelivery(const QString &operationId);
    void deliverExportSnapshot(const std::shared_ptr<SnapshotState> &state,
                               const QJsonDocument &activities);

    TrackerConnectionStore *m_connections = nullptr;
    TrackerMappingStore *m_mappings = nullptr;
    TrackerImportStore *m_imports = nullptr;
    TrackerProgressImportOwner *m_importOwner = nullptr;
    TrackerHistoryEvidenceStore *m_historyEvidence = nullptr;
    TrackerDeliveryStore *m_delivery = nullptr;
    TrackerCanonicalDeliverySource *m_deliverySource = nullptr;
    TrackerSyncSettingsStore *m_settings = nullptr;
    TrackerSyncCenterModel *m_syncCenter = nullptr;
    SimklApiClient *m_api = nullptr;
    QSet<QString> m_pullingAccounts;
    QHash<QString, QList<PendingEvidence>> m_pendingEvidence;
    bool m_deliveryInFlight = false;
    qint64 m_nextDeliveryAtMs = 0;
};
