#pragma once

#include "AccountBootstrapStore.h"
#include "AccountClient.h"
#include "AccountController.h"
#include "ProgressSyncAdapter.h"
#include "ActivitySyncAdapter.h"
#include "HistorySyncAdapter.h"
#include "ProfilePreferencesSyncAdapter.h"
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
#include "AccountCredentialStore.h"
#include "WindowsAccountSensitiveClipboard.h"

#include <QObject>

#include <memory>

class QQmlApplicationEngine;
class LocalDownloads;

namespace Colosseum::WatchParty {
class IWatchPartyAccountBridge;
}

class AccountRuntime final : public QObject {
    Q_OBJECT

public:
    explicit AccountRuntime(QObject *parent = nullptr);
    AccountRuntime(std::unique_ptr<AccountCredentialStore> credentialStore, QObject *parent);

    AccountController *controller();
    AccountRecoveryKeyPresenter *recoveryKeyPresenter();
    ProfileStoreRuntime *profileStores();

    void setDownloadSource(LocalDownloads *downloads);

    void prepareForQml(QQmlApplicationEngine *engine);

    // Narrow Watch Party identity seam â€” supplies signed-in username +
    // current bearer to the Watch Party WSS boundary only; never exposed to
    // QML; invite delivery fail-closed until the account service exposes a
    // delivery operation.
    std::unique_ptr<Colosseum::WatchParty::IWatchPartyAccountBridge>
    createWatchPartyAccountBridge();

private:
    bool installCoreSyncAdapters(
        QString *error = nullptr);
    void clearCoreSyncAdapters();

    AccountHttpTransport m_transport;
    AccountClient m_client;
    std::unique_ptr<AccountCredentialStore> m_credentialStore;
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
    std::unique_ptr<HistorySyncAdapter>
        m_historySyncAdapter;
    std::unique_ptr<ActivitySyncAdapter>
        m_activitySyncAdapter;
    std::unique_ptr<ProfilePreferencesSyncAdapter>
        m_preferencesSyncAdapter;
    DownloadIntentStore m_downloadIntentStore;
    std::unique_ptr<DownloadIntentSyncAdapter>
        m_downloadIntentSyncAdapter;
    LocalDownloads *m_downloadSource = nullptr;
    SyncEngine m_syncEngine;
    AccountController m_controller;
    bool m_qmlPrepared = false;
};
