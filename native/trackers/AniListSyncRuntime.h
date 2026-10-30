#pragma once

#include "AniListApiClient.h"
#include "AniListProtocol.h"
#include "TrackerDeliveryStore.h"
#include "TrackerMappingStore.h"
#include "TrackerImportStore.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

class AniListConnectionController;
class TrackerCanonicalDeliverySource;
class TrackerConnectionStore;
class TrackerMappingStore;
class TrackerProgressImportOwner;
class TrackerSyncCenterModel;
class TrackerSyncSettingsStore;

// AniList list import and progress/completion delivery. Anime syncs by
// episode and manga by CHAPTER only: a Tankoban (volume) fact never reaches
// this runtime's request path. Unlike MalSyncRuntime there is no persisted
// list snapshot: AniList entries are verified by a single-entry readback, so
// the only state kept here is in-memory.
class AniListSyncRuntime final : public QObject
{
    Q_OBJECT

public:
    using ExportSnapshotCompletion = std::function<void(
        std::optional<TrackerRemoteDeliverySnapshot>)>;

    AniListSyncRuntime(TrackerConnectionStore *connections,
                       TrackerMappingStore *mappings,
                       TrackerImportStore *imports,
                       TrackerProgressImportOwner *importOwner,
                       TrackerDeliveryStore *delivery,
                       TrackerCanonicalDeliverySource *deliverySource,
                       TrackerSyncSettingsStore *settings,
                       TrackerSyncCenterModel *syncCenter,
                       AniListApiClient *api,
                       AniListConnectionController *controller,
                       QObject *parent = nullptr);

    bool available() const;

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
    struct SnapshotState;

    // How a media lookup, mapping attempt or entry read ended. Anything but
    // Resolved/Mapped sends nothing and records nothing as delivered.
    enum class StepOutcome : quint8 {
        Done,
        Unmapped,
        RateLimited,
        Transient,
        AuthenticationRequired
    };
    struct MediaStep {
        StepOutcome outcome = StepOutcome::Transient;
        AniListMediaRef media;
        qint64 retryAfterMs = 0;
    };
    struct EntryStep {
        StepOutcome outcome = StepOutcome::Transient;
        bool present = false;
        AniListEntryState state;
        qint64 retryAfterMs = 0;
    };

    static StepOutcome classifyFailure(const AniListApiResult &result,
                                       qint64 *retryAfterMs);
    std::optional<TrackerTitleMapping> uniqueMappingFor(
        const TrackerConnection &connection,
        const TrackerDeliveryFact &fact,
        bool *ambiguous = nullptr) const;

    using ListFetchCompletion = std::function<void(
        bool, const QList<AniListListItem> &, const QByteArray &)>;
    using MediaCompletion = std::function<void(const MediaStep &)>;
    using EntryCompletion = std::function<void(const EntryStep &)>;
    using MapCompletion = std::function<void(StepOutcome)>;

    void pullAniList(bool explicitRequest);
    void fetchLists(const TrackerConnection &connection,
                    ListFetchCompletion completion);
    void fetchListPage(const std::shared_ptr<ListFetchState> &state);
    void finishPull(const std::shared_ptr<PullState> &state,
                    const QList<AniListListItem> &items,
                    const QByteArray &snapshotMaterial);

    void lookupMedia(AniListMediaKind kind,
                     const QString &malId,
                     MediaCompletion completion);
    void readEntry(const TrackerConnection &connection,
                   const QString &aniListId,
                   EntryCompletion completion);
    void mapFact(const TrackerConnection &connection,
                 const TrackerDeliveryFact &fact,
                 MapCompletion completion);
    void ensureExactDeliveryMappings(const TrackerConnection &connection);
    void resolveNextPendingMapping();

    void dispatchNextDelivery();
    void sendDelivery(const TrackerDeliveryOperation &operation);
    void finishDelivery(const QString &operationId,
                        TrackerDeliveryAttemptResult result,
                        TrackerDeliveryReason reason,
                        qint64 retryAfterAtMs = 0);
    void reconcileUnknownDelivery(const QString &operationId);
    void noteAuthenticationFailure();
    void snapshotNext(const std::shared_ptr<SnapshotState> &state);

    TrackerConnectionStore *m_connections = nullptr;
    TrackerMappingStore *m_mappings = nullptr;
    TrackerImportStore *m_imports = nullptr;
    TrackerProgressImportOwner *m_importOwner = nullptr;
    TrackerDeliveryStore *m_delivery = nullptr;
    TrackerCanonicalDeliverySource *m_deliverySource = nullptr;
    TrackerSyncSettingsStore *m_settings = nullptr;
    TrackerSyncCenterModel *m_syncCenter = nullptr;
    AniListApiClient *m_api = nullptr;
    AniListConnectionController *m_controller = nullptr;

    QSet<QString> m_pullingAccounts;
    // `anime:<malId>` / `manga:<malId>` to the AniList media it resolved to.
    QHash<QString, AniListMediaRef> m_media;
    // Media keys whose lookup failed closed this session; not retried until
    // the next explicit sync or reconnect.
    QSet<QString> m_unmappedMedia;
    // Canonical media ids whose mapping failed closed this session.
    QSet<QString> m_unmappedFacts;
    QList<TrackerDeliveryFact> m_pendingMappings;
    QSet<QString> m_pendingMappingKeys;
    bool m_resolvingMappings = false;
    bool m_deliveryInFlight = false;
    bool m_authenticationBlocked = false;
    qint64 m_nextDeliveryAtMs = 0;
    qint64 m_nextResolveAtMs = 0;
};
