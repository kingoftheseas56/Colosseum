#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountBootstrapStore.h"
#include "AccountClient.h"
#include "AccountController.h"
#include "ProgressSyncAdapter.h"
#include "HistorySyncAdapter.h"
#include "ProfilePreferencesSyncAdapter.h"
#include "CollectionSyncAdapter.h"
#include "AccountDeviceIdentity.h"
#include "AccountHttpTransport.h"
#include "AccountRecoveryKeyPresenter.h"
#include "SharedPcProfileCoordinator.h"
#include "SyncEngine.h"
#include "SyncAdapterRegistry.h"
#include "ProfileStoreRuntime.h"
#include "WindowsAccountCredentialStore.h"
#include "WindowsAccountSensitiveClipboard.h"

#include <QObject>

#include <memory>

class QQmlApplicationEngine;

class AccountRuntime final : public QObject {
    Q_OBJECT

public:
    explicit AccountRuntime(QObject *parent = nullptr);

    AccountController *controller();
    AccountRecoveryKeyPresenter *recoveryKeyPresenter();
    ProfileStoreRuntime *profileStores();

    void prepareForQml(QQmlApplicationEngine *engine);

private:
    bool installCoreSyncAdapters(
        QString *error = nullptr);
    void clearCoreSyncAdapters();

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
    std::unique_ptr<HistorySyncAdapter>
        m_historySyncAdapter;
    std::unique_ptr<ProfilePreferencesSyncAdapter>
        m_preferencesSyncAdapter;
    SyncEngine m_syncEngine;
    AccountController m_controller;
    bool m_qmlPrepared = false;
};
