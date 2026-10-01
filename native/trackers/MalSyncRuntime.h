#pragma once

#include "MalApiClient.h"
#include "MalProtocol.h"
#include "TrackerDeliveryStore.h"
#include "TrackerImportStore.h"

#include <QHash>
#include <QObject>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

class MalListStateStore;
class TrackerCanonicalDeliverySource;
class TrackerConnectionStore;
class TrackerMappingStore;
class TrackerProgressImportOwner;
class TrackerSyncCenterModel;
class TrackerSyncSettingsStore;

class MalSyncRuntime final : public QObject
{
    Q_OBJECT

public:
    using ExportSnapshotCompletion = std::function<void(
        std::optional<TrackerRemoteDeliverySnapshot>)>;

    MalSyncRuntime(TrackerConnectionStore *connections,
                   TrackerMappingStore *mappings,
                   TrackerImportStore *imports,
                   TrackerProgressImportOwner *importOwner,
                   TrackerDeliveryStore *delivery,
                   TrackerCanonicalDeliverySource *deliverySource,
                   TrackerSyncSettingsStore *settings,
                   TrackerSyncCenterModel *syncCenter,
                   MalApiClient *api,
                   MalListStateStore *listState,
                   QObject *parent = nullptr);

    void start();
    void readExportSnapshotAsync(
        const TrackerConnection &connection,
        const QList<TrackerDeliveryFact> &facts,
        ExportSnapshotCompletion completion);

public slots:
    void syncAll(const QStringList &providerKeys, quint64 revision);
    void connectionEstablished(const QString &providerKey);

private:
    struct ListFetchState;
    struct PullState;

    using ListFetchCompletion = std::function<void(
        bool, const QList<MalListItem> &, const QByteArray &)>;

    void pullMal(bool explicitRequest);
    void fetchLists(const TrackerConnection &connection,
                    ListFetchCompletion completion);
    void fetchListPage(const std::shared_ptr<ListFetchState> &state);
    void finishPull(const std::shared_ptr<PullState> &state,
                    const QList<MalListItem> &items,
                    const QByteArray &snapshotMaterial);
    void ensureExactDeliveryMappings(const TrackerConnection &connection);
    void dispatchNextDelivery();
    void sendDelivery(const TrackerDeliveryOperation &operation);
    void finishDelivery(const QString &operationId,
                        TrackerDeliveryAttemptResult result,
                        TrackerDeliveryReason reason,
                        qint64 retryAfterAtMs = 0);
    void reconcileUnknownDelivery(const QString &operationId);
    void buildRemoteSnapshot(
        const TrackerConnection &connection,
        const QList<TrackerDeliveryFact> &facts,
        const QList<MalListItem> &items,
        const QByteArray &snapshotMaterial,
        ExportSnapshotCompletion completion);

    TrackerConnectionStore *m_connections = nullptr;
    TrackerMappingStore *m_mappings = nullptr;
    TrackerImportStore *m_imports = nullptr;
    TrackerProgressImportOwner *m_importOwner = nullptr;
    TrackerDeliveryStore *m_delivery = nullptr;
    TrackerCanonicalDeliverySource *m_deliverySource = nullptr;
    TrackerSyncSettingsStore *m_settings = nullptr;
    TrackerSyncCenterModel *m_syncCenter = nullptr;
    MalApiClient *m_api = nullptr;
    MalListStateStore *m_listState = nullptr;

    QSet<QString> m_pullingAccounts;
    QHash<QString, MalListItem> m_remoteList;
    bool m_deliveryInFlight = false;
    qint64 m_nextDeliveryAtMs = 0;
};
