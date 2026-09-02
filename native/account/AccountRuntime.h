#pragma once

#include "AccountAttachmentCoordinator.h"
#include "AccountBootstrapStore.h"
#include "AccountClient.h"
#include "AccountController.h"
#include "ProgressSyncAdapter.h"
#include "ActivitySyncAdapter.h"
#include "WatchStateSyncAdapter.h"
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

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>

class QQmlApplicationEngine;

namespace Colosseum::WatchParty {
class IWatchPartyAccountBridge;
}

class AccountRuntime final : public QObject {
    Q_OBJECT

public:
    explicit AccountRuntime(QObject *parent = nullptr);

    AccountController *controller();
    AccountRecoveryKeyPresenter *recoveryKeyPresenter();
    ProfileStoreRuntime *profileStores();

    void prepareForQml(QQmlApplicationEngine *engine);

    // Narrow Watch Party identity seam — supplies signed-in username +
    // current bearer to the Watch Party WSS boundary only; never exposed to
    // QML; invite delivery fail-closed until the account service exposes a
    // delivery operation.
    std::unique_ptr<Colosseum::WatchParty::IWatchPartyAccountBridge>
    createWatchPartyAccountBridge();

private:
    bool installCoreSyncAdapters(
        QString *error = nullptr);
    void clearCoreSyncAdapters();

    // ── Cloud attachment lifecycle (Arc 36 Wave 4B lane N-17) ──────────────
    // The runtime owns WHEN the AccountAttachmentCoordinator runs, never HOW:
    // begin/endAttachmentMode stay the coordinator's to drive. A flow starts
    // only for an authenticated session over an active account profile, and
    // ordinary sync scheduling is held until the flow's engine bootstrap or
    // terminal state (the engine's attachment mode itself gates ordinary
    // pull/push meanwhile).
    bool startOrResumeAttachmentFlow();
    bool attachmentFlowInFlight() const;
    void ensureAttachmentCoordinator(
        const ProfilePaths &profile);
    bool captureAttachmentBaseline();
    bool verifyAttachmentCloudState(
        QString *error) const;
    void teardownAttachmentFlow();

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
    SyncEngine m_syncEngine;
    AccountController m_controller;
    bool m_qmlPrepared = false;

    // Declared after the engine/client/stores they observe so the
    // coordinator and its verifier connections die first.
    std::unique_ptr<AccountAttachmentCoordinator>
        m_attachmentCoordinator;
    QString m_attachmentProfileId;
    bool m_attachmentOrdinarySyncHeld = false;
    QHash<QString, QSet<QString>>
        m_attachmentBaselineRecords;
    QSet<QString> m_attachmentBaselineActivity;
};
