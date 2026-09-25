#pragma once

#include "TrackerTypes.h"
#include "TrackerDeliveryStore.h"

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

class TrackerConnectionStore;
class TrackerDeliveryStore;
class TrackerImportStore;
class TrackerImportOwner;
class TrackerHistoryEvidenceStore;
class TrackerMappingStore;
class TrackerCanonicalTitleIndex;
class TrackerScrobbleStore;
class TrackerSyncSettingsStore;
struct TrackerConnection;
struct TrackerImportBatch;

// Safe native projection for the approved Sync Center. It exposes only
// presentation scalars and opaque journal identifiers; credentials, remote
// account identifiers, canonical identity keys, and provider payloads stay
// behind their native owners.
class TrackerSyncCenterModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(quint64 revision READ revision NOTIFY modelChanged)
    Q_PROPERTY(QVariantList connectedTrackers READ connectedTrackers NOTIFY modelChanged)
    Q_PROPERTY(QVariantList catalogue READ catalogue NOTIFY modelChanged)
    Q_PROPERTY(QVariantList importReviews READ importReviews NOTIFY modelChanged)
    Q_PROPERTY(QVariantMap globalSettings READ globalSettings NOTIFY modelChanged)
    Q_PROPERTY(QVariantMap aggregateState READ aggregateState NOTIFY modelChanged)
    Q_PROPERTY(QVariantMap lastActionResult READ lastActionResult NOTIFY modelChanged)

public:
    using LivePlaybackPreferenceAction = std::function<bool(const QString &, bool)>;
    using ResumeAfterSyncAction = std::function<void()>;
    using DisconnectAction = std::function<bool(const QString &,
                                                const QString &,
                                                QString *)>;
    using RefreshDeliveryFactsAction = std::function<bool(QString *)>;
    // Called once for preview and again immediately before confirmation. The
    // adapter must read actual provider state; it must never synthesize it.
    using ReadExportSnapshotAction = std::function<std::optional<TrackerRemoteDeliverySnapshot>(
        const TrackerConnection &, const QList<TrackerDeliveryFact> &, QString *)>;

    // Sealed/accountless mode: catalogue can be shown, but no profile-owned
    // connection or setting action is admitted.
    explicit TrackerSyncCenterModel(QObject *parent = nullptr);

    // Explicit profile composition. Descriptors are the fail-closed native
    // capability catalogue; callbacks delegate live-playback shutdown and
    // resume ordering to the existing tracker runtime.
    TrackerSyncCenterModel(TrackerConnectionStore *connections,
                           TrackerImportStore *imports,
                           TrackerDeliveryStore *delivery,
                           TrackerScrobbleStore *scrobble,
                           TrackerSyncSettingsStore *settings,
                           const QList<TrackerProviderDescriptor> &catalogue,
                           LivePlaybackPreferenceAction livePlaybackAction = {},
                           ResumeAfterSyncAction resumeAfterSync = {},
                           DisconnectAction disconnectAction = {},
                           TrackerHistoryEvidenceStore *historyEvidence = nullptr,
                           RefreshDeliveryFactsAction refreshDeliveryFacts = {},
                           QObject *parent = nullptr);

    quint64 revision();
    QVariantList connectedTrackers();
    QVariantList catalogue();
    QVariantList importReviews();
    QVariantMap globalSettings();
    QVariantMap aggregateState();
    QVariantMap lastActionResult() const;

    Q_INVOKABLE QVariantMap providerDossier(const QString &providerKey);
    Q_INVOKABLE QVariantMap diagnoseRoute(const QString &providerKey);
    Q_INVOKABLE QVariantMap importReviewSnapshot(const QString &batchId,
                                                quint64 expectedRevision);
    Q_INVOKABLE QVariantMap beginExportReview(const QString &providerKey,
                                              quint64 expectedRevision);
    Q_INVOKABLE bool confirmExportReview(const QString &reviewId,
                                         const QStringList &selectedItemIds,
                                         quint64 expectedRevision);
    Q_INVOKABLE QVariantList deliveryRows(const QString &providerKey);
    Q_INVOKABLE void refresh();
    void setImportOwner(TrackerImportOwner *owner) { m_importOwner = owner; }
    void setTitleMatching(TrackerMappingStore *store,
                          const TrackerCanonicalTitleIndex *index);
    void setExportReview(TrackerDeliverySource *source,
                         ReadExportSnapshotAction readRemoteSnapshot);
    Q_INVOKABLE QVariantList titleMatchCandidates(const QString &batchId,
                                                  const QString &reviewItemId,
                                                  const QString &searchText,
                                                  quint64 expectedRevision);
    Q_INVOKABLE bool confirmTitleMatch(const QString &batchId,
                                       const QString &reviewItemId,
                                       const QString &candidateId,
                                       quint64 expectedRevision);

    Q_INVOKABLE bool setGlobalSetting(const QString &key,
                                      bool enabled,
                                      quint64 expectedRevision);
    Q_INVOKABLE bool setProviderPullAutomatically(const QString &providerKey,
                                                  bool enabled,
                                                  quint64 expectedRevision);
    Q_INVOKABLE bool setProviderSendEnabled(const QString &providerKey,
                                            bool enabled,
                                            quint64 expectedRevision);
    Q_INVOKABLE bool setLivePlaybackTrackingEnabled(const QString &providerKey,
                                                   bool enabled,
                                                   quint64 expectedRevision);
    Q_INVOKABLE bool resolveImportItem(const QString &batchId,
                                       const QString &itemId,
                                       const QString &choice,
                                       quint64 expectedRevision);
    Q_INVOKABLE bool resolveImportItems(const QString &batchId,
                                        const QStringList &itemIds,
                                        const QString &choice,
                                        quint64 expectedRevision);
    Q_INVOKABLE bool confirmImport(const QString &batchId,
                                   quint64 expectedRevision);
    Q_INVOKABLE bool disconnectTracker(const QString &providerKey,
                                       const QString &choice,
                                       quint64 expectedRevision);
    Q_INVOKABLE bool removeImportedData(const QString &providerKey,
                                        quint64 expectedRevision);
    Q_INVOKABLE bool requestSyncAll(quint64 expectedRevision);

signals:
    void modelChanged();
    // Native orchestration consumes provider keys only. QML never receives an
    // account ID or runs synchronization itself.
    void syncAllRequested(const QStringList &providerKeys, quint64 revision);
    // Opens the native title-selection journey without exposing provider or
    // canonical identity keys to QML.
    void findMatchRequested(const QString &batchId,
                            const QString &reviewItemId,
                            quint64 revision);

private:
    struct ProviderCounts {
        int waiting = 0;
        int syncing = 0;
        int attention = 0;
        int unresolved = 0;
        int pending = 0;
        int knownUnsent = 0;
        int unknownOutcome = 0;
    };

    struct TitleMatchHandle {
        QString batchId;
        QString itemId;
        QString remoteMediaId;
        QString canonicalMediaId;
        quint64 revision = 0;
    };
    struct ExportReviewHandle {
        QString publicId;
        QString privatePreviewId;
        TrackerProviderId providerId = TrackerProviderId::Simkl;
        QString remoteAccountId;
        quint64 generation = 0;
        quint64 revision = 0;
        QHash<QString, QString> publicToPrivateItemId;
    };

    const TrackerProviderDescriptor *descriptor(TrackerProviderId providerId) const;
    bool profileStoresHealthy() const;
    bool ownerHealthy() const;
    bool titleMatchAvailable() const;
    bool currentConnection(TrackerProviderId providerId,
                           TrackerConnection *connection = nullptr) const;
    bool firstImportReviewed(const TrackerConnection &connection) const;
    ProviderCounts providerCounts(const TrackerConnection &connection) const;
    int importedHistoryCount(const TrackerConnection &connection) const;
    int importedProgressCount(const TrackerConnection &connection) const;
    QVariantList importedDataRemovalPreview(const TrackerConnection &connection) const;
    QString providerState(const TrackerConnection &connection,
                          const ProviderCounts &counts) const;
    TrackerProviderCapabilities effectiveCapabilities(
        const TrackerConnection &connection) const;
    QStringList eligibleProviderKeys() const;
    QVariantMap connectionCard(const TrackerConnection &connection) const;
    QVariantMap providerCatalogueCard(const TrackerProviderDescriptor &provider) const;
    QVariantList buildConnectedTrackers() const;
    QVariantList buildCatalogue() const;
    QVariantList buildImportReviews() const;
    QVariantMap buildGlobalSettings() const;
    QVariantMap buildAggregateState() const;
    QVariantList buildImportRows(const TrackerImportBatch &batch) const;
    QByteArray snapshotFingerprint() const;
    bool syncAllHandlerAvailable() const;
    QString publicImportItemId(const QString &batchId,
                               const QString &privateItemId) const;
    void refreshRevision();
    bool acceptIntent(quint64 expectedRevision, const QString &action);
    bool finishIntent(bool accepted, const QString &action, const QString &code);

    TrackerConnectionStore *m_connections = nullptr;
    TrackerImportStore *m_imports = nullptr;
    TrackerImportOwner *m_importOwner = nullptr;
    TrackerMappingStore *m_titleMatchStore = nullptr;
    const TrackerCanonicalTitleIndex *m_titleIndex = nullptr;
    TrackerHistoryEvidenceStore *m_historyEvidence = nullptr;
    TrackerDeliveryStore *m_delivery = nullptr;
    TrackerDeliverySource *m_exportSource = nullptr;
    ReadExportSnapshotAction m_readExportSnapshot;
    std::optional<ExportReviewHandle> m_exportReview;
    TrackerScrobbleStore *m_scrobble = nullptr;
    TrackerSyncSettingsStore *m_settings = nullptr;
    QList<TrackerProviderDescriptor> m_catalogue;
    LivePlaybackPreferenceAction m_livePlaybackAction;
    ResumeAfterSyncAction m_resumeAfterSync;
    DisconnectAction m_disconnectAction;
    RefreshDeliveryFactsAction m_refreshDeliveryFacts;
    QByteArray m_lastFingerprint;
    mutable QHash<QString, QString> m_privateToPublicImportItemId;
    mutable QHash<QString, QPair<QString, QString>> m_publicToPrivateImportItemId;
    mutable QHash<QString, TitleMatchHandle> m_titleMatchCandidateHandles;
    quint64 m_revision = 1;
    bool m_profileAvailable = false;
    bool m_importedDataRemovalPending = false;
    QVariantMap m_lastActionResult{{QStringLiteral("accepted"), false},
                                  {QStringLiteral("action"), QStringLiteral("none")},
                                  {QStringLiteral("code"), QStringLiteral("idle")},
                                  {QStringLiteral("revision"), 1}};
};
