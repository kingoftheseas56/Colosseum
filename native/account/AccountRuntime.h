#pragma once

#include "AccountBootstrapStore.h"
#include "AccountAttachmentCoordinator.h"
#include "AccountLifecycleCoordinator.h"
#include "AccountClient.h"
#include "AccountController.h"
#include "ProgressSyncAdapter.h"
#include "ActivitySyncAdapter.h"
#include "HistorySyncAdapter.h"
#include "WatchStateSyncAdapter.h"
#include "ProfilePreferencesSyncAdapter.h"
#include "StremioLinkSyncAdapter.h"
#include "CollectionSyncAdapter.h"
#include "DownloadIntentStore.h"
#include "DownloadIntentSyncAdapter.h"
#include "AccountDeviceIdentity.h"
#include "AccountHttpTransport.h"
#include "AccountRecoveryKeyPresenter.h"
#include "SharedPcProfileCoordinator.h"
#include "SyncEngine.h"
#include "SyncAdapterRegistry.h"
#include "ProfileStoreRuntime.h"
#include "WindowsAccountCredentialStore.h"
#include "WindowsAccountSensitiveClipboard.h"
#include "stremio/StremioSync.h"
#include "stremio/StremioTheatreImporter.h"

#include <QJsonArray>
#include <QObject>

#include <memory>

class QQmlApplicationEngine;
class LocalDownloads;
class ExtensionsStore;

namespace Colosseum::WatchParty {
class IWatchPartyAccountBridge;
}

class AccountRuntime final : public QObject {
    Q_OBJECT

public:
    explicit AccountRuntime(QObject *parent = nullptr);
    // Native composition seam: production takes the default official Stremio
    // options, while isolated runtime tests can supply the same bounded
    // loopback option object used by StremioSync itself. No option crosses QML.
    explicit AccountRuntime(
        const StremioSyncOptions &stremioOptions,
        QObject *parent = nullptr);

    AccountController *controller();
    AccountRecoveryKeyPresenter *recoveryKeyPresenter();
    ProfileStoreRuntime *profileStores();

    void setDownloadSource(LocalDownloads *downloads);
    // ExtensionsStore retains its manifest/native responsibilities. This
    // runtime supplies only active-profile lifetime for Theatre-compatible rows.
    void setExtensionsStore(ExtensionsStore *extensions);

    // Native-only Stremio provider-import entry. Callers supply only decoded
    // library items; credentials and request envelopes remain inside the
    // transport layer and never cross into QML or the ordinary sync schema.
    bool applyStremioLibraryItem(
        const StremioLibraryItem &item,
        StremioTheatreImporter::Completion completion = {});

    void prepareForQml(QQmlApplicationEngine *engine);

    // Narrow Watch Party identity seam — supplies signed-in username +
    // current bearer to the Watch Party WSS boundary only; never exposed to
    // QML; invite delivery fail-closed until the account service exposes a
    // delivery operation.
    std::unique_ptr<Colosseum::WatchParty::IWatchPartyAccountBridge>
    createWatchPartyAccountBridge();

private:
    struct StremioImportBatch;
    struct StremioRedoBatch;

    bool installCoreSyncAdapters(
        QString *error = nullptr);
    void clearCoreSyncAdapters();
    void startOrResumeAccountAttachment();
    void activateStremioProfile();
    void activateExtensionsProfile();
    void refreshStremioLibrary();
    bool applyStremioSeriesWatchedAfterRedo(
        const StremioLibraryItem &item,
        const QString &encodedWatched,
        const QString &outerRedoReceipt,
        const QJsonObject &canonicalProjection,
        StremioTheatreImporter::Completion completion);
    void applyNextStremioLibraryItem(
        const std::shared_ptr<StremioImportBatch> &batch);
    void replayPendingStremioProviderImports();
    void replayNextStremioProviderImport(
        const std::shared_ptr<StremioRedoBatch> &batch);
    void scheduleStremioTheatreReconcile();
    void reconcileStremioTheatreState(
        const QString &profileId,
        quint64 incarnation);
    void scheduleStremioAddonReconcile();
    void reconcileStremioAddonCollection(
        const QString &profileId,
        quint64 incarnation);
    void applyStremioAddonCollection(
        const QString &profileId,
        quint64 incarnation,
        const QJsonArray &addons);

    AccountHttpTransport m_transport;
    AccountClient m_client;
    WindowsAccountCredentialStore m_credentialStore;
    AccountDeviceIdentity m_deviceIdentity;
    AccountBootstrapStore m_bootstrapStore;
    WindowsAccountSensitiveClipboard m_sensitiveClipboard;
    AccountRecoveryKeyPresenter m_recoveryKeyPresenter;
    ProfileStoreRuntime m_profileStores;
    SharedPcProfileCoordinator m_profileCoordinator;
    SyncAdapterRegistry m_syncRegistry;
    std::unique_ptr<CollectionSyncAdapter>
        m_collectionSyncAdapter;
    std::unique_ptr<ProgressSyncAdapter>
        m_progressSyncAdapter;
    std::unique_ptr<WatchStateSyncAdapter>
        m_watchStateSyncAdapter;
    std::unique_ptr<HistorySyncAdapter>
        m_historySyncAdapter;
    std::unique_ptr<ActivitySyncAdapter>
        m_activitySyncAdapter;
    std::unique_ptr<ProfilePreferencesSyncAdapter>
        m_preferencesSyncAdapter;
    std::unique_ptr<StremioLinkSyncAdapter>
        m_stremioLinkSyncAdapter;
    DownloadIntentStore m_downloadIntentStore;
    std::unique_ptr<DownloadIntentSyncAdapter>
        m_downloadIntentSyncAdapter;
    LocalDownloads *m_downloadSource = nullptr;
    ExtensionsStore *m_extensionsStore = nullptr;
    SyncEngine m_syncEngine;
    StremioSync m_stremioSync;
    std::unique_ptr<StremioTheatreImporter> m_stremioTheatreImporter;
    std::unique_ptr<AccountAttachmentCoordinator>
        m_attachmentCoordinator;
    AccountController m_controller;
    AccountLifecycleCoordinator m_lifecycleCoordinator;
    QMetaObject::Connection m_stremioMarkerConnection;
    QMetaObject::Connection m_stremioRegistryMutationConnection;
    QMetaObject::Connection m_stremioRemoteAppliedConnection;
    QMetaObject::Connection m_stremioProgressDirtyConnection;
    QMetaObject::Connection m_stremioWatchStateConnection;
    QMetaObject::Connection m_stremioCollectionDirtyConnection;
    QMetaObject::Connection m_extensionsChangedConnection;
    quint64 m_stremioProfileIncarnation = 0;
    quint64 m_stremioActiveImportCount = 0;
    bool m_stremioReconcileScheduled = false;
    bool m_stremioReconcileDeferred = false;
    bool m_stremioAddonReconcileScheduled = false;
    bool m_stremioAddonApplyInProgress = false;
    bool m_stremioAddonReconcileDeferred = false;
    bool m_qmlPrepared = false;
};
