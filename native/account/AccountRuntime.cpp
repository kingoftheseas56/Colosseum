// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountRuntime.h"

#include "AccountServiceEndpoint.h"
#include "watchparty/WatchPartyIdentity.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace {

// File-local Watch Party account bridge implementation. Kept out of the
// header so the Watch Party identity contract (IWatchPartyAccountBridge)
// stays the only thing AccountRuntime exposes across the seam.
class WatchPartyAccountBridge final
    : public Colosseum::WatchParty::IWatchPartyAccountBridge {
public:
    WatchPartyAccountBridge(
        AccountController *controller,
        AccountClient *client)
        : m_controller(controller),
          m_client(client) {
    }

    std::optional<Colosseum::WatchParty::SignedInAccountIdentity>
    currentSignedInIdentity() const override {
        if (!m_controller
            || !m_client
            || m_controller->mode() != QStringLiteral("signedIn")
            || m_controller->username().isEmpty()
            || m_client->accessToken().isEmpty()) {
            return std::nullopt;
        }

        return Colosseum::WatchParty::SignedInAccountIdentity{
            m_controller->username(),
            m_client->accessToken()};
    }

    void inviteExactUsername(
        const QString &roomId,
        const QString &exactUsername,
        InviteCompletion completion) override {
        Q_UNUSED(roomId);
        Q_UNUSED(exactUsername);

        // The live account service has no invite-delivery operation yet;
        // fail closed rather than touch the network.
        if (completion) {
            completion(
                Colosseum::WatchParty::InviteDeliveryResult{
                    Colosseum::WatchParty::InviteDeliveryStatus::Rejected,
                    QStringLiteral("invite_delivery_unavailable")});
        }
    }

private:
    AccountController *m_controller = nullptr;
    AccountClient *m_client = nullptr;
};

} // namespace

AccountRuntime::AccountRuntime(QObject *parent)
    : QObject(parent),
      m_transport(AccountServiceEndpoint::configuredUrl()),
      m_client(&m_transport),
      m_recoveryKeyPresenter(&m_sensitiveClipboard),
      m_profileCoordinator(&m_profileStores),
      m_syncRegistry(),
      m_syncEngine(
          &m_client,
          &m_syncRegistry),
      m_controller(
          &m_client,
          &m_credentialStore,
          &m_deviceIdentity,
          &m_bootstrapStore,
          &m_recoveryKeyPresenter) {
    setObjectName(QStringLiteral("accountRuntime"));
    m_controller.setProfileCoordinator(
        &m_profileCoordinator);
    m_controller.setSyncEngine(
        &m_syncEngine);

    connect(
        &m_profileStores,
        &ProfileStoreRuntime::
            storesAboutToChange,
        this,
        [this]() {
            clearCoreSyncAdapters();
        });

    connect(
        &m_controller,
        &AccountController::
            accountProfileReadyForSync,
        this,
        [this]() {
            if (m_syncEngine.active())
                return;

            const ProfilePaths profile =
                m_profileStores.activeProfile();

            if (profile.kind()
                    != ProfilePaths::Kind::Account
                || m_controller.deviceId()
                       .isEmpty()) {
                m_controller.setSyncObservation(
                    AccountController::
                        SyncState::Blocked,
                    0);
                return;
            }

            m_syncEngine.setNetworkEnabled(
                false);

            QString error;
            if (!installCoreSyncAdapters(
                    &error)) {
                m_controller.setSyncObservation(
                    AccountController::
                        SyncState::Blocked,
                    0);
                return;
            }

            if (!m_syncEngine.start(
                    profile,
                    m_controller.deviceId(),
                    &error)) {
                m_controller.setSyncObservation(
                    AccountController::
                        SyncState::Blocked,
                    m_syncEngine
                        .pendingOutboxCount());
            }
        });

    connect(
        &m_controller,
        &AccountController::signedIn,
        this,
        [this]() {
            if (!m_syncEngine.active()) {
                const ProfilePaths profile =
                    m_profileStores.activeProfile();

                m_syncEngine.setNetworkEnabled(
                    false);

                QString error;
                if (!installCoreSyncAdapters(
                        &error)) {
                    m_controller.setSyncObservation(
                        AccountController::
                            SyncState::Blocked,
                        0);
                    return;
                }

                if (!m_syncEngine.start(
                        profile,
                        m_controller.deviceId(),
                        &error)) {
                    m_controller.setSyncObservation(
                        AccountController::
                            SyncState::Blocked,
                        m_syncEngine
                            .pendingOutboxCount());
                    return;
                }
            }

            m_syncEngine.setNetworkEnabled(
                true);
        });

}

bool AccountRuntime::installCoreSyncAdapters(
    QString *error) {
    clearCoreSyncAdapters();

    if (m_profileStores
            .activeProfile()
            .kind()
        != ProfilePaths::Kind::Account) {
        if (error) {
            *error = QStringLiteral(
                "Core sync adapters require an active account profile.");
        }
        return false;
    }

    CollectionStore *collection =
        m_profileStores.collectionStore();
    ProgressStore *progress =
        m_profileStores.progressStore();
    HistoryStore *history =
        m_profileStores.historyStore();
    ProfilePreferencesStore *preferences =
        m_profileStores.preferencesStore();

    if (!collection
        || !progress
        || !history
        || !preferences) {
        if (error) {
            *error = QStringLiteral(
                "The Collection, Continue/progress, History, or profile preference owner is unavailable.");
        }
        return false;
    }

    auto collectionAdapter =
        std::make_unique<
            CollectionSyncAdapter>(
                collection);
    auto progressAdapter =
        std::make_unique<
            ProgressSyncAdapter>(
                progress);
    auto historyAdapter =
        std::make_unique<
            HistorySyncAdapter>(
                history);
    auto preferencesAdapter =
        std::make_unique<
            ProfilePreferencesSyncAdapter>(
                preferences);

    SyncAdapterRegistryError registryError;
    if (!m_syncRegistry.registerAdapter(
            collectionAdapter.get(),
            &registryError)) {
        if (error) {
            *error =
                registryError.detail.isEmpty()
                ? registryError.code
                : registryError.detail;
        }
        return false;
    }

    if (!m_syncRegistry.registerAdapter(
            progressAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(
            QStringLiteral("collection"));

        if (error) {
            *error =
                registryError.detail.isEmpty()
                ? registryError.code
                : registryError.detail;
        }
        return false;
    }

    if (!m_syncRegistry.registerAdapter(
            historyAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(
            QStringLiteral(
                "continue_progress"));
        m_syncRegistry.unregisterAdapter(
            QStringLiteral("collection"));

        if (error) {
            *error =
                registryError.detail.isEmpty()
                ? registryError.code
                : registryError.detail;
        }
        return false;
    }

    if (!m_syncRegistry.registerAdapter(
            preferencesAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(
            QStringLiteral(
                "full_history"));
        m_syncRegistry.unregisterAdapter(
            QStringLiteral(
                "continue_progress"));
        m_syncRegistry.unregisterAdapter(
            QStringLiteral("collection"));

        if (error) {
            *error =
                registryError.detail.isEmpty()
                ? registryError.code
                : registryError.detail;
        }
        return false;
    }

    m_collectionSyncAdapter =
        std::move(collectionAdapter);
    m_progressSyncAdapter =
        std::move(progressAdapter);
    m_historySyncAdapter =
        std::move(historyAdapter);
    m_preferencesSyncAdapter =
        std::move(preferencesAdapter);
    return true;
}

void AccountRuntime::clearCoreSyncAdapters() {
    m_syncRegistry.unregisterAdapter(
        QStringLiteral("collection"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral(
            "continue_progress"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral(
            "full_history"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral(
            "explicit_content_preference"));

    m_preferencesSyncAdapter.reset();
    m_historySyncAdapter.reset();
    m_progressSyncAdapter.reset();
    m_collectionSyncAdapter.reset();
}

ProfileStoreRuntime *AccountRuntime::profileStores() {
    return &m_profileStores;
}

AccountController *AccountRuntime::controller() {
    return &m_controller;
}

AccountRecoveryKeyPresenter *
AccountRuntime::recoveryKeyPresenter() {
    return &m_recoveryKeyPresenter;
}

std::unique_ptr<Colosseum::WatchParty::IWatchPartyAccountBridge>
AccountRuntime::createWatchPartyAccountBridge() {
    return std::make_unique<WatchPartyAccountBridge>(
        &m_controller, &m_client);
}

void AccountRuntime::prepareForQml(QQmlApplicationEngine *engine) {
    Q_ASSERT(engine);
    if (!engine || m_qmlPrepared)
        return;

    m_profileStores.prepareForQml(engine);

    engine->rootContext()->setContextProperty(
        QStringLiteral("AccountController"),
        &m_controller);
    // Named "AccountRecoveryPresenter", not "AccountRecoveryKey": the QML
    // directory import `import "account"` (Main.qml) implicitly exposes
    // every qml/account/*.qml file as a type by its filename, and
    // qml/account/AccountRecoveryKey.qml (the one-time key display page)
    // already claims that identifier. A same-named context property is
    // shadowed by the imported type when referenced as a bare identifier,
    // so `typeof AccountRecoveryKey` was always the type reference, never
    // this presenter -- silently breaking every live binding to it.
    engine->rootContext()->setContextProperty(
        QStringLiteral("AccountRecoveryPresenter"),
        &m_recoveryKeyPresenter);

    m_qmlPrepared = true;
    m_controller.restoreRememberedSession();
}
