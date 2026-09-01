#include "AccountRuntime.h"

#include "AccountServiceEndpoint.h"
#include "ActivityStore.h"
#include "ProfilePreferencesStore.h"
#include "watchparty/WatchPartyIdentity.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUuid>

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

    // Sealing (sign-out, shared-PC switch) must fail closed while a cloud
    // attachment is reconciling; the coordinator's verification gate reads
    // the live stores, and the local source may never be sealed away
    // unverified.
    m_profileCoordinator
        .setAttachmentInFlightProbe(
            [this]() {
                return attachmentFlowInFlight();
            });

    connect(
        &m_profileStores,
        &ProfileStoreRuntime::
            storesAboutToChange,
        this,
        [this]() {
            clearCoreSyncAdapters();
            // The fail-closed seal guard makes this unreachable mid-flow in
            // practice; teardown here is the safety net for direct profile
            // surgery, killing the coordinator before its stores vanish.
            teardownAttachmentFlow();
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

            // The attach seam and a durable pending receipt both reconcile
            // through the coordinator here — after the session is
            // authenticated, before ordinary sync scheduling proceeds. A
            // flow in flight holds ordinary scheduling until its engine
            // bootstrap (the engine's attachment mode gates pull/push
            // itself) or its terminal state.
            if (!attachmentFlowInFlight())
                startOrResumeAttachmentFlow();

            if (!m_attachmentOrdinarySyncHeld)
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
    ActivityStore *activity =
        m_profileStores.activityStore();
    ProfilePreferencesStore *preferences =
        m_profileStores.preferencesStore();

    if (!collection
        || !progress
        || !history
        || !activity
        || !preferences) {
        if (error) {
            *error = QStringLiteral(
                "The Collection, Continue/progress, History, Activity, or profile preference owner is unavailable.");
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
    auto watchStateAdapter =
        std::make_unique<
            WatchStateSyncAdapter>(
                progress);
    auto historyAdapter =
        std::make_unique<
            HistorySyncAdapter>(
                history);
    auto activityAdapter =
        std::make_unique<
            ActivitySyncAdapter>(
                activity);
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
            watchStateAdapter.get(),
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
            historyAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(
            QStringLiteral("watch_state"));
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
            activityAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(QStringLiteral("full_history"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("watch_state"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("continue_progress"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("collection"));
        if (error) {
            *error = registryError.detail.isEmpty()
                ? registryError.code
                : registryError.detail;
        }
        return false;
    }

    if (!m_syncRegistry.registerAdapter(
            preferencesAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(
            QStringLiteral("activity_fact"));
        m_syncRegistry.unregisterAdapter(
            QStringLiteral(
                "full_history"));
        m_syncRegistry.unregisterAdapter(
            QStringLiteral("watch_state"));
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
    m_watchStateSyncAdapter =
        std::move(watchStateAdapter);
    m_historySyncAdapter =
        std::move(historyAdapter);
    m_activitySyncAdapter =
        std::move(activityAdapter);
    m_preferencesSyncAdapter =
        std::move(preferencesAdapter);

    const bool syncActivityHistory =
        preferences->syncActivityHistory();
    m_syncEngine.setCategoryNetworkEnabled(
        QStringLiteral("full_history"),
        syncActivityHistory);
    m_syncEngine.setCategoryNetworkEnabled(
        QStringLiteral("activity_fact"),
        syncActivityHistory);
    connect(
        preferences,
        &ProfilePreferencesStore::syncActivityHistoryChanged,
        this,
        [this, preferences]() {
            const bool enabled =
                preferences->syncActivityHistory();
            m_syncEngine.setCategoryNetworkEnabled(
                QStringLiteral("full_history"),
                enabled);
            m_syncEngine.setCategoryNetworkEnabled(
                QStringLiteral("activity_fact"),
                enabled);
        });
    return true;
}

void AccountRuntime::clearCoreSyncAdapters() {
    m_syncRegistry.unregisterAdapter(
        QStringLiteral("collection"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral(
            "continue_progress"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral("watch_state"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral(
            "full_history"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral("activity_fact"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral(
            "explicit_content_preference"));

    m_preferencesSyncAdapter.reset();
    m_activitySyncAdapter.reset();
    m_historySyncAdapter.reset();
    m_watchStateSyncAdapter.reset();
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

// ── Cloud attachment lifecycle (Arc 36 Wave 4B lane N-17) ───────────────────

bool AccountRuntime::startOrResumeAttachmentFlow() {
    // Requirement 1: the network attachment runs only for an authenticated
    // session (the local merge alone is the behavior otherwise), over an
    // active account profile with a running engine.
    if (m_client.accessToken().isEmpty())
        return false;

    const ProfilePaths profile =
        m_profileStores.activeProfile();
    if (profile.kind()
            != ProfilePaths::Kind::Account
        || !m_syncEngine.active()) {
        return false;
    }

    ensureAttachmentCoordinator(profile);

    bool resume = false;
    std::optional<SharedPcProfileCoordinator::
                      LocalAttachmentIdentity>
        pending;
    if (m_attachmentCoordinator
            ->hasPendingReceipt()) {
        // Requirement 2: a pending receipt reconciles through the
        // coordinator instead of a fresh identity, before ordinary sync
        // scheduling proceeds.
        resume = true;
    } else {
        pending = m_profileCoordinator
                      .takePendingLocalAttachment();
        if (!pending.has_value())
            return false;

        // An identity for any other account never reaches the wire.
        if (pending->accountId
            != profile.profileId())
            return false;
    }

    if (!captureAttachmentBaseline()) {
        // Fail closed: without the merged projection baseline the cloud
        // state cannot be verified, so the source must not be retired and
        // no flow starts.
        qWarning(
            "AccountRuntime: attachment "
            "baseline capture failed; the "
            "cloud attachment stays unused");
        return false;
    }

    QString error;
    m_attachmentOrdinarySyncHeld = true;
    const bool accepted =
        resume
            ? m_attachmentCoordinator->resumePending(
                  &error)
            : [this, &pending, &error]() {
                  AccountAttachmentCoordinator::
                      SourceIdentity source;
                  source.sourceKind =
                      pending->sourceKind;
                  source.sourceProfileId =
                      pending->sourceProfileId;
                  source.sourceSemanticDigest =
                      pending
                          ->sourceSemanticDigest;
                  source.sourceActivityDigest =
                      pending
                          ->sourceActivityDigest;

                  // One canonical lowercase UUID, durable in the receipt
                  // from the first server call onward and reused by every
                  // retry and restart of this attachment.
                  const QString attachmentId =
                      QUuid::createUuid()
                          .toString(
                              QUuid::
                                  WithoutBraces);
                  return m_attachmentCoordinator
                      ->start(
                          attachmentId,
                          source,
                          &error);
              }();

    if (!accepted) {
        // The coordinator refused before any flow and failed closed (or a
        // retired-receipt clear already finished synchronously, in which
        // case the finished handler normalized the hold below).
        qWarning(
            "AccountRuntime: attachment "
            "flow refused: %s",
            qPrintable(error));
        if (!attachmentFlowInFlight())
            m_attachmentOrdinarySyncHeld =
                false;
        return false;
    }

    return true;
}

bool AccountRuntime::attachmentFlowInFlight()
    const {
    if (!m_attachmentCoordinator)
        return false;

    switch (
        m_attachmentCoordinator->state()) {
    case AccountAttachmentCoordinator::
        State::Idle:
    case AccountAttachmentCoordinator::
        State::Completed:
    case AccountAttachmentCoordinator::
        State::Failed:
        return false;
    default:
        return true;
    }
}

void AccountRuntime::ensureAttachmentCoordinator(
    const ProfilePaths &profile) {
    if (m_attachmentCoordinator
        && m_attachmentProfileId
            == profile.profileId()) {
        return;
    }

    m_attachmentCoordinator =
        std::make_unique<
            AccountAttachmentCoordinator>(
            &m_client,
            &m_syncEngine,
            profile);
    m_attachmentProfileId =
        profile.profileId();

    connect(
        m_attachmentCoordinator.get(),
        &AccountAttachmentCoordinator::
            progress,
        this,
        [this](AccountAttachmentCoordinator::
                   State state) {
            if (state
                    == AccountAttachmentCoordinator::
                        State::
                            EngineBootstrapping
                && m_attachmentOrdinarySyncHeld) {
                // The engine entered the attachment
                // mode and gates ordinary pull/push
                // itself; the bootstrap may use the
                // network now.
                m_syncEngine.setNetworkEnabled(
                    true);
            }
        });
    connect(
        m_attachmentCoordinator.get(),
        &AccountAttachmentCoordinator::
            finished,
        this,
        [this](bool succeeded,
               const QString &errorCode,
               const QString &errorMessage) {
            m_attachmentOrdinarySyncHeld =
                false;
            // Success returns to ordinary sync;
            // failure keeps the receipt and the
            // local source resumable while ordinary
            // sync resumes (the engine's attachment
            // mode, if still held, keeps gating it).
            m_syncEngine.setNetworkEnabled(
                true);
            if (!succeeded) {
                qWarning(
                    "AccountRuntime: attachment "
                    "flow failed (receipt and "
                    "local source retained): "
                    "%s %s",
                    qPrintable(errorCode),
                    qPrintable(errorMessage));
            }
        });
    m_attachmentCoordinator
        ->setCloudStateVerifier(
            [this](QString *error) {
                return verifyAttachmentCloudState(
                    error);
            });
}

bool AccountRuntime::captureAttachmentBaseline() {
    m_attachmentBaselineRecords.clear();
    m_attachmentBaselineActivity.clear();

    const QList<SyncAdapter *> adapters = {
        m_collectionSyncAdapter.get(),
        m_progressSyncAdapter.get(),
        m_watchStateSyncAdapter.get(),
        m_historySyncAdapter.get(),
        m_activitySyncAdapter.get(),
        m_preferencesSyncAdapter.get()
    };

    QSet<QString> categories;
    for (SyncAdapter *adapter : adapters) {
        if (!adapter)
            return false;

        if (categories.contains(
                adapter->categoryId())) {
            qWarning(
                "AccountRuntime: duplicate "
                "attachment baseline "
                "category %s",
                qPrintable(
                    adapter->categoryId()));
            return false;
        }
        categories.insert(
            adapter->categoryId());

        SyncAdapterExport snapshot;
        QString error;
        if (!adapter->exportSnapshot(
                &snapshot,
                &error)) {
            qWarning(
                "AccountRuntime: attachment "
                "baseline export failed for "
                "%s: %s",
                qPrintable(
                    adapter->categoryId()),
                qPrintable(error));
            return false;
        }

        QSet<QString> keys;
        for (const SyncAdapterRecord
                 &record :
             snapshot.records) {
            keys.insert(
                record.recordKey);
        }
        m_attachmentBaselineRecords.insert(
            adapter->categoryId(),
            keys);
    }

    ActivityStore *activity =
        m_profileStores.activityStore();
    if (!activity)
        return false;

    QString error;
    const QList<QVariantMap> facts =
        activity->portableSyncFacts(
            &error);
    for (const QVariantMap &fact :
         facts) {
        m_attachmentBaselineActivity
            .insert(
                fact.value(
                    QStringLiteral(
                        "eventId"))
                    .toString());
    }
    return true;
}

bool AccountRuntime::verifyAttachmentCloudState(
    QString *error) const {
    // Liveness: the attachment's account profile must still be the active
    // one (the fail-closed seal guard makes mid-flow switches unreachable
    // in practice; this keeps a violated assumption from retiring a
    // source).
    const ProfilePaths profile =
        m_profileStores.activeProfile();
    if (profile.kind()
            != ProfilePaths::Kind::Account
        || profile.profileId()
            != m_attachmentProfileId) {
        if (error) {
            *error = QStringLiteral(
                "The attachment account "
                "profile is no longer "
                "active.");
        }
        return false;
    }

    // The applied canonical state must still reconstruct the merged
    // projection the local merge produced: every baseline record key and
    // every baseline portable Activity fact survives (union merge; newer
    // cloud winners for a key may legitimately replace its payload, but the
    // key itself must be represented).
    const QList<SyncAdapter *> adapters = {
        m_collectionSyncAdapter.get(),
        m_progressSyncAdapter.get(),
        m_watchStateSyncAdapter.get(),
        m_historySyncAdapter.get(),
        m_activitySyncAdapter.get(),
        m_preferencesSyncAdapter.get()
    };

    for (SyncAdapter *adapter : adapters) {
        if (!adapter) {
            if (error) {
                *error = QStringLiteral(
                    "A core sync adapter is "
                    "no longer installed.");
            }
            return false;
        }

        SyncAdapterExport snapshot;
        QString exportError;
        if (!adapter->exportSnapshot(
                &snapshot,
                &exportError)) {
            if (error) {
                *error = QStringLiteral(
                    "The merged projection "
                    "could not be read for "
                    "%1.")
                             .arg(
                                 adapter
                                     ->categoryId())
                         + QLatin1Char(' ')
                         + exportError;
            }
            return false;
        }

        QSet<QString> current;
        for (const SyncAdapterRecord
                 &record :
             snapshot.records) {
            current.insert(
                record.recordKey);
        }

        const QSet<QString> baseline =
            m_attachmentBaselineRecords
                .value(
                    adapter->categoryId());
        if (!current.contains(
                baseline)) {
            if (error) {
                *error = QStringLiteral(
                    "The applied canonical "
                    "state lost records "
                    "from %1.")
                             .arg(
                                 adapter
                                     ->categoryId());
            }
            return false;
        }
    }

    const ActivityStore *activity =
        m_profileStores.activityStore();
    if (!activity) {
        if (error) {
            *error = QStringLiteral(
                "The activity ledger is no "
                "longer available.");
        }
        return false;
    }

    QString factError;
    const QList<QVariantMap> facts =
        activity->portableSyncFacts(
            &factError);
    QSet<QString> currentEvents;
    for (const QVariantMap &fact :
         facts) {
        currentEvents.insert(
            fact.value(
                QStringLiteral(
                    "eventId"))
                .toString());
    }
    if (!currentEvents.contains(
            m_attachmentBaselineActivity)) {
        if (error) {
            *error = QStringLiteral(
                "The applied canonical "
                "state lost portable "
                "activity facts from "
                "the merged ledger.");
        }
        return false;
    }

    return true;
}

void AccountRuntime::teardownAttachmentFlow() {
    m_attachmentCoordinator.reset();
    m_attachmentProfileId.clear();
    m_attachmentOrdinarySyncHeld = false;
    m_attachmentBaselineRecords.clear();
    m_attachmentBaselineActivity
        .clear();
}
