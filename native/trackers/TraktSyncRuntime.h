#pragma once

#include "TraktApiClient.h"
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

class TrackerCanonicalDeliverySource;
class TrackerConnectionStore;
class TrackerMappingStore;
class TrackerProgressImportOwner;
class TrackerSyncCenterModel;
class TrackerSyncSettingsStore;

class TraktSyncRuntime final : public QObject
{
    Q_OBJECT
public:
    using ExportSnapshotCompletion = std::function<void(std::optional<TrackerRemoteDeliverySnapshot>)>;
    TraktSyncRuntime(TrackerConnectionStore *connections, TrackerMappingStore *mappings,
                     TrackerImportStore *imports, TrackerProgressImportOwner *importOwner,
                     TrackerHistoryEvidenceStore *historyEvidence, TrackerDeliveryStore *delivery,
                     TrackerCanonicalDeliverySource *deliverySource, TrackerSyncSettingsStore *settings,
                     TrackerSyncCenterModel *syncCenter, TraktApiClient *api, QObject *parent = nullptr);
    void start();
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
        QString fingerprint;
    };
    struct PullState;
    struct HistoryFetchState;
    struct SnapshotState;
    using HistoryCompletion = std::function<void(std::optional<QJsonDocument>)>;
    void pullTrakt(bool explicitRequest);
    void fetchHistoryPages(const TrackerConnection &connection, HistoryCompletion completion);
    void fetchHistoryPage(const std::shared_ptr<HistoryFetchState> &state);
    void finishPull(const std::shared_ptr<PullState> &state);
    void settleConfirmedEvidence();
    void dispatchNextDelivery();
    void sendDelivery(const TrackerDeliveryOperation &operation);
    void finishDelivery(const QString &operationId, TrackerDeliveryAttemptResult result,
                        TrackerDeliveryReason reason, qint64 retryAfterMs = 0);
    void reconcileUnknownDelivery(const QString &operationId);
    void deliverExportSnapshot(const std::shared_ptr<SnapshotState> &state);
    bool isCurrentConnection(const TrackerConnection &connection) const;

    TrackerConnectionStore *m_connections = nullptr;
    TrackerMappingStore *m_mappings = nullptr;
    TrackerImportStore *m_imports = nullptr;
    TrackerProgressImportOwner *m_importOwner = nullptr;
    TrackerHistoryEvidenceStore *m_historyEvidence = nullptr;
    TrackerDeliveryStore *m_delivery = nullptr;
    TrackerCanonicalDeliverySource *m_deliverySource = nullptr;
    TrackerSyncSettingsStore *m_settings = nullptr;
    TrackerSyncCenterModel *m_syncCenter = nullptr;
    TraktApiClient *m_api = nullptr;
    QSet<QString> m_pullingAccounts;
    QHash<QString, QList<PendingEvidence>> m_pendingEvidence;
    bool m_deliveryInFlight = false;
    qint64 m_nextDeliveryAtMs = 0;
};
