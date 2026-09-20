#pragma once

#include "StremioCodec.h"
#include "StremioState.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <QTcpServer>
#include <QTimer>
#include <QTcpSocket>
#include <QVariantList>

#include <functional>
#include <memory>
#include <optional>

struct StremioSyncOptions {
    QUrl apiEndpoint = QUrl(QStringLiteral("https://api.strem.io/api"));
    std::function<void(const QUrl &)> browserOpener;
    std::function<bool(const QString &, const QString &, const QByteArray &)> saveCredential;
    std::function<bool(const QString &)> clearCredential;
    std::function<std::optional<QByteArray>(const QString &, const QString &)> loadCredential;
    std::function<void(const StremioPendingIntent &, std::function<void(bool, bool)>)> intentSender;
    std::function<qint64()> clock;
    bool allowTaggedLoopbackFixture = false;
};

// Private, bounded replay metadata for an inbound provider record. This is a
// canonical Theatre projection rather than a Stremio datastore row: no
// credential, addon URL, or transport envelope is retained across a crash.
struct StremioProviderImportRedo {
    QString operationId;
    QString profileId;
    QString accountId;
    quint64 bindingGeneration = 0;
    QString id;
    QString type;
    bool libraryMember = false;
    bool removed = false;
    QJsonObject projection;
};

class StremioSync final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY stateChanged)
    Q_PROPERTY(qint64 lastSuccessAt READ lastSuccessAt NOTIFY stateChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY stateChanged)
    Q_PROPERTY(bool mergeComplete READ mergeComplete NOTIFY stateChanged)
    Q_PROPERTY(quint64 completedRun READ completedRun NOTIFY stateChanged)
    Q_PROPERTY(QString accountDisplayName READ accountDisplayName NOTIFY stateChanged)
    Q_PROPERTY(QString lastResultSummary READ lastResultSummary NOTIFY stateChanged)
    Q_PROPERTY(bool linkedAccount READ linkedAccount NOTIFY stateChanged)

public:
    explicit StremioSync(
        const StremioSyncOptions &options = {},
        QObject *parent = nullptr);

    QString status() const;
    int pendingCount() const;
    qint64 lastSuccessAt() const;
    QString activeProfileId() const;
    bool mergeComplete() const;
    quint64 completedRun() const;
    QString accountDisplayName() const;
    QString lastResultSummary() const;
    bool linkedAccount() const;

    bool activateProfile(
        const QString &profileId,
        const QString &statePath,
        bool sealed,
        QString *error = nullptr);
    void deactivateProfile();
    // Runtime projection fixture only. The exact tag is assigned by the isolated
    // Task 1 Lanista session; no product or QML path can invoke it.
    bool activateTaggedFixture();
    bool startBrowserAuthentication(QString *error = nullptr);
    void cancelAuthentication();
    // Explicit user disconnect. Fences every old callback, clears this
    // device's vault entry and durably retires provider-only work while the
    // canonical Colosseum owners remain untouched.
    bool disconnectProfile(std::function<void(bool)> completion = {});
    Q_INVOKABLE bool connectAccount();
    Q_INVOKABLE bool disconnectCurrentProfile();
    Q_INVOKABLE bool switchAccount();
    bool beginVisibleSync(bool reviveFailedIntents = false);
    void finishVisibleSync(bool succeeded, const QString &summary);
    void setMarkerLinked(bool linked);
    void setCredentialCallbacks(
        std::function<bool(const QString &, const QString &, const QByteArray &)> save,
        std::function<bool(const QString &)> clear,
        std::function<std::optional<QByteArray>(const QString &, const QString &)> load = {});

    bool queueIntent(
        const QString &kind,
        const QJsonObject &desired,
        QString *operationId = nullptr);
    // AccountRuntime invokes this only after the Theatre owners report their
    // own durable receipt. It receives canonical snapshots, not a provider
    // envelope, and records only semantic provider differences in this
    // profile's private journal. Remote notifications use the same method as
    // invalidations; an unchanged snapshot therefore cannot create an echo.
    bool reconcileTheatreState(
        const QVariantList &theatreCollection,
        const QVariantList &progressEntries,
        const QHash<QString, int> &watchedMarks,
        const QHash<QString, qint64> &watchedActionAt);
    // Bounded native-only library pull. The decoded provider records remain
    // behind the AccountRuntime importer boundary; neither credentials nor
    // datastore envelopes enter QML or the ordinary Neon payload.
    bool pullLibraryItems(
        std::function<void(bool, QList<StremioLibraryItem>)> completion);
    // Reads the actual private addon collection. The decoded documents never
    // cross into QML or Neon; AccountRuntime applies compatible rows through
    // ExtensionsStore's durable receipt.
    bool pullAddonCollection(
        std::function<void(bool, QJsonArray)> completion);
    // Fresh-read/rebase/whole-write/readback for the one Stremio addon
    // collection. Input is already private provider-shaped data from the
    // active ExtensionsStore owner; it never crosses QML or Neon.
    bool reconcileAddonCollection(
        const QJsonArray &localAddons,
        std::function<void(bool, QJsonArray)> completion);
    // Advances the local side of the addon baseline only after
    // ExtensionsStore has durably applied the settled provider collection.
    // The snapshot contains only provider-managed Theatre rows, so unrelated
    // Stremio addons remain remote-only and can never be inferred as removals.
    bool acknowledgeAddonCollectionOwner(
        const QJsonArray &localAddons,
        std::function<void(bool)> completion = {});
    // AccountRuntime marks the first merge only after every imported owner
    // and its Neon checkpoint have committed. A successful provider pull by
    // itself is deliberately not a first-merge baseline.
    bool completeFirstMerge(std::function<void(bool)> completion = {});
    // The provider-import redo receipt reaches disk before canonical Theatre
    // owners change. It is cleared only after their Neon checkpoint commits.
    bool beginProviderImport(
        const StremioLibraryItem &item,
        const QJsonObject &projection,
        std::function<void(bool, const QString &)> durableReceipt);
    // AccountRuntime drains these only through StremioTheatreImporter while
    // the captured profile/account remains active. They are never QML data.
    QList<StremioProviderImportRedo> pendingProviderImports() const;
    bool settleProviderImport(
        const QString &receipt,
        std::function<void(bool)> completion = {});
    // Task 2's explicit cross-service removal is a named, durable operation;
    // it is not inferred from an ordinary Collection tombstone. The receipt
    // fires only after the private provider intent has reached disk, before a
    // caller is permitted to remove the canonical Theatre entry.
    bool queueExplicitLibraryRemoval(
        const QString &id,
        const QString &type,
        std::function<void(bool)> journalReceipt,
        QString *operationId = nullptr);
    // Records the other explicit removal choice: local-only removal must
    // suppress an unchanged provider membership on later passive pulls.
    bool recordLocalOnlyLibraryRemoval(
        const QString &id,
        const QString &type,
        std::function<void(bool)> journalReceipt = {});
    // A passive Stremio deletion is not copied into Collection. Its inverse
    // difference is nevertheless durable so a later relay cannot infer that
    // Colosseum should recreate the provider item.
    bool recordRemoteLibraryRemoval(
        const QString &id,
        const QString &type,
        std::function<void(bool)> journalReceipt = {});
    // A passive provider membership must not undo an intentional local-only
    // removal. This native query exposes no credential or datastore data.
    bool suppressesRemoteLibraryMembership(
        const QString &id,
        const QString &type) const;
    bool acknowledgeLocalReceipt(const QString &operationId);
    void retryPendingNow();

    // Narrow native/QML metadata bridge for the one existing Theatre metadata
    // reader. QML receives only an opaque request id and the expected series
    // id; it can return only a bounded root plus ordered episode identities.
    // The profile/account/binding fence and every completion stay native.
    using EpisodeMetadataCompletion = std::function<void(
        bool,
        QList<StremioEpisodeIdentity>)>;
    bool requestEpisodeMetadata(
        const QString &seriesId,
        EpisodeMetadataCompletion completion);
    Q_INVOKABLE void setEpisodeMetadataBridgeReady(bool ready = true);
    Q_INVOKABLE bool submitEpisodeMetadata(
        const QString &requestId,
        const QString &metadataRootId,
        const QVariantList &episodes);

signals:
    void stateChanged();
    void browserLoginRequested(const QUrl &url);
    void profileLinkValidated(const QString &profileId);
    void profileDisconnected(const QString &profileId);
    void episodeMetadataRequested(
        const QString &requestId,
        const QString &seriesId);

private:
    struct ProfileBinding {
        QString profileId;
        quint64 generation = 0;
    };

    struct PendingPersistence {
        ProfileBinding binding;
        QString path;
        StremioPersistentState state;
    };

    struct ProvisionalCredential {
        ProfileBinding binding;
        quint64 attempt = 0;
    };

    struct EpisodeMetadataRequest {
        ProfileBinding binding;
        QString accountId;
        QString expectedRootId;
        EpisodeMetadataCompletion completion;
        bool emitted = false;
    };

    struct LibraryPull;
    struct AddonCollectionReconcile;

    bool fixtureEndpointAllowed() const;
    bool endpointAllowed() const;
    void handleIncomingConnection();
    void handleCallbackSocket(QTcpSocket *socket);
    void validateAuthKey(
        const QByteArray &authKey,
        const ProfileBinding &binding,
        quint64 attempt);
    void persist(std::function<void(bool)> continuation = {});
    void settlePersistence(quint64 generation, bool committed);
    void dispatchIntent(const QString &operationId, const ProfileBinding &binding);
    void sendIntentViaDatastore(
        const StremioPendingIntent &intent,
        const ProfileBinding &binding,
        std::function<void(bool, bool)> completion);
    void postDatastoreRequest(
        const StremioDatastoreRequest &request,
        const ProfileBinding &binding,
        std::function<void(bool, bool, QJsonValue)> completion);
    void fetchNextLibraryBatch(const std::shared_ptr<LibraryPull> &pull);
    void finishLibraryPull(
        const std::shared_ptr<LibraryPull> &pull,
        bool succeeded);
    void fetchAddonCollectionForReconcile(
        const std::shared_ptr<AddonCollectionReconcile> &reconcile);
    void writeAddonCollectionForReconcile(
        const std::shared_ptr<AddonCollectionReconcile> &reconcile);
    void verifyAddonCollectionForReconcile(
        const std::shared_ptr<AddonCollectionReconcile> &reconcile);
    void finishAddonCollectionReconcile(
        const std::shared_ptr<AddonCollectionReconcile> &reconcile,
        bool succeeded,
        const QJsonArray &settled = {});
    void handleIntentResult(
        const QString &operationId,
        const ProfileBinding &binding,
        bool accepted,
        bool authenticationFailure);
    void removeSatisfiedIntent(const QString &operationId);
    bool bindingCurrent(const ProfileBinding &binding) const;
    void dispatchEpisodeMetadataRequest(const QString &requestId);
    void cancelEpisodeMetadataRequests();
    bool hasPendingPersistenceForPath(const QString &path) const;
    void retireProvisionalCredential();
    void updateConnectionStatus();
    void completeVisibleSyncIfDrained();
    StremioPendingIntent *intentFor(const QString &operationId);
    void setStatus(const QString &status);
    void finishRun();
    void upsertMembershipDifference(
        const QString &id,
        const QString &type,
        bool localPresent,
        bool remotePresent,
        bool explicitRemoteRemoval);
    bool clearLocalOnlyMembershipSuppression(
        const QString &id,
        const QString &type);
    bool suppressesInferredLibraryAddition(
        const QString &id,
        const QString &type) const;
    bool queueIntentInternal(
        const QString &kind,
        const QJsonObject &desired,
        QString *operationId,
        bool localReceiptDurable);
    bool queueReconciledIntent(
        const QString &kind,
        const QJsonObject &desired);
    static QString baselineKeyForIntent(
        const QString &kind,
        const QJsonObject &desired);

    StremioSyncOptions m_options;
    StremioState m_stateStore;
    QNetworkAccessManager m_network;
    QTcpServer m_callbackServer;
    QPointer<QTcpSocket> m_callbackSocket;
    QByteArray m_callbackBuffer;
    QPointer<QNetworkReply> m_identityReply;
    QByteArray m_identityResponse;
    bool m_identityResponseTooLarge = false;
    QPointer<QNetworkReply> m_datastoreReply;
    QByteArray m_datastoreResponse;
    bool m_datastoreResponseTooLarge = false;
    QTimer m_authTimeout;
    QTimer m_retryTimer;
    QString m_callbackPath;
    QString m_profileId;
    QString m_statePath;
    quint64 m_bindingGeneration = 0;
    quint64 m_authAttempt = 0;
    quint64 m_completedRun = 0;
    QString m_lastResultSummary;
    QString m_pendingVisibleSyncSummary;
    bool m_visibleSyncActive = false;
    bool m_visibleSyncPullComplete = false;
    QString m_status = QStringLiteral("notConnected");
    bool m_hasUsableCredential = false;
    bool m_markerLinked = false;
    bool m_dispatchAllowed = true;
    std::optional<ProvisionalCredential> m_provisionalCredential;
    StremioPersistentState m_state;
    QHash<quint64, QList<std::function<void(bool)>>> m_persistContinuations;
    QHash<quint64, PendingPersistence> m_pendingPersistences;
    QHash<QString, StremioPersistentState> m_pendingStateByPath;
    QSet<QString> m_failedPersistencePaths;
    QSet<QString> m_inFlightOperations;
    QHash<QString, EpisodeMetadataRequest> m_episodeMetadataRequests;
    bool m_episodeMetadataBridgeReady = false;
    bool m_addonCollectionReconcileActive = false;
};
