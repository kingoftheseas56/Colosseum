#include "AccountRuntime.h"

#include "ActivityStore.h"
#include "AccountServiceEndpoint.h"
#include "LegacyPersonalStateStorage.h"
#include "ProfilePreferencesStore.h"
#include "DownloadIntentSyncAdapter.h"
#include "watchparty/WatchPartyIdentity.h"

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

std::optional<LegacyPersonalStateStorage> attachmentSourceStorage(
    const ProfilePaths &accountProfile,
    const AccountAttachmentReceiptData &receipt,
    QString *error) {
    if (receipt.sourceKind
            == AccountAttachmentReceipt::sourceKindLegacyLocal()
        && receipt.sourceProfileId
               == ProfilePaths::legacyLocal().profileId()) {
        return LegacyPersonalStateStorage::forCurrentInstallation();
    }

    if (receipt.sourceKind
            == AccountAttachmentReceipt::sourceKindLocalOnly()
        && receipt.sourceProfileId
               == ProfilePaths::localOnly(
                      accountProfile.appDataRoot())
                      .profileId()) {
        const ProfilePaths sourceProfile =
            ProfilePaths::localOnly(accountProfile.appDataRoot());
        return LegacyPersonalStateStorage::forProfile(
            sourceProfile,
            error);
    }

    if (error)
        *error = QStringLiteral(
            "The account attachment source identity does not resolve to a managed profile.");
    return std::nullopt;
}

std::optional<qint64> attachmentTimestamp(
    const QJsonObject &object,
    const QString &field) {
    if (!object.contains(field))
        return qint64(0);
    const QJsonValue value = object.value(field);
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number)
        || std::floor(number) != number
        || number < 0
        || number > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return std::nullopt;
    }
    return static_cast<qint64>(number);
}

qint64 attachmentMinPositive(qint64 left, qint64 right) {
    if (left > 0 && right > 0)
        return qMin(left, right);
    return qMax(left, right);
}

bool historyExportAbsorbs(
    const QJsonValue &sourceValue,
    const QJsonValue &actualValue) {
    if (!sourceValue.isObject() || !actualValue.isObject())
        return false;
    const QJsonObject source = sourceValue.toObject();
    const QJsonObject actual = actualValue.toObject();
    if (source.value(QStringLiteral("kind"))
            != actual.value(QStringLiteral("kind"))
        || source.value(QStringLiteral("id"))
            != actual.value(QStringLiteral("id"))) {
        return false;
    }

    const auto sourceFirst = attachmentTimestamp(
        source, QStringLiteral("firstActivityAt"));
    const auto actualFirst = attachmentTimestamp(
        actual, QStringLiteral("firstActivityAt"));
    const auto sourceLast = attachmentTimestamp(
        source, QStringLiteral("lastActivityAt"));
    const auto actualLast = attachmentTimestamp(
        actual, QStringLiteral("lastActivityAt"));
    const auto sourceCompleted = attachmentTimestamp(
        source, QStringLiteral("completedAt"));
    const auto actualCompleted = attachmentTimestamp(
        actual, QStringLiteral("completedAt"));
    if (!sourceFirst.has_value()
        || !actualFirst.has_value()
        || !sourceLast.has_value()
        || !actualLast.has_value()
        || !sourceCompleted.has_value()
        || !actualCompleted.has_value()) {
        return false;
    }

    // History keeps the earliest first/completion and latest last timestamp.
    // Other fields are domain-canonical identity fields and remain stable.
    return *actualFirst == attachmentMinPositive(*actualFirst, *sourceFirst)
        && *actualLast == qMax(*actualLast, *sourceLast)
        && *actualCompleted == attachmentMinPositive(
               *actualCompleted,
               *sourceCompleted);
}

bool verifyAttachmentDispositions(
    const AccountAttachmentReceiptData &receipt,
    const SyncWireAttachmentResponse &response,
    const QList<SyncWireExportPage> &pages,
    QString *error) {
    if (response.attachmentId != receipt.attachmentId
        || (!receipt.deviceId.isEmpty()
            && response.deviceId != receipt.deviceId)) {
        if (error)
            *error = QStringLiteral(
                "The attachment disposition response is bound to a different identity.");
        return false;
    }

    QHash<QString, QJsonObject> canonical;
    for (const SyncWireExportPage &page : pages) {
        for (const QJsonValue &value : page.items) {
            const QJsonObject item = value.toObject();
            canonical.insert(
                item.value(QStringLiteral("category")).toString()
                    + QChar(0x1f)
                    + item.value(QStringLiteral("key")).toString(),
                item);
        }
    }

    QHash<QString, SyncWireAttachmentDisposition> dispositions;
    for (const SyncWireAttachmentDisposition &disposition : response.dispositions)
        dispositions.insert(disposition.mutationId, disposition);
    if (dispositions.size() != receipt.manifest.size()) {
        if (error)
            *error = QStringLiteral(
                "The server did not return one disposition for every attachment mutation.");
        return false;
    }

    const QByteArray emptyHash = QCryptographicHash::hash(
        QByteArray(), QCryptographicHash::Sha256);
    const QSet<QString> lwwCategories{
        QStringLiteral("collection"),
        QStringLiteral("continue_progress"),
        QStringLiteral("watch_state"),
        QStringLiteral("desired_download_intent"),
        QStringLiteral("explicit_content_preference")};

    for (const SyncWireAttachmentManifestItem &item : receipt.manifest) {
        const SyncWireMutation &mutation = item.mutation;
        const auto dispositionIt = dispositions.constFind(mutation.mutationId);
        if (dispositionIt == dispositions.constEnd()
            || dispositionIt->category != mutation.category
            || dispositionIt->recordKey != mutation.recordKey
            || dispositionIt->operation != mutation.operation) {
            if (error)
                *error = QStringLiteral(
                    "The server disposition does not match the attachment manifest identity.");
            return false;
        }

        const QString exportKey = mutation.category
            + QChar(0x1f)
            + mutation.recordKey;
        const auto exportIt = canonical.constFind(exportKey);
        const bool hasExport = exportIt != canonical.constEnd();
        QJsonValue actualPayload;
        QByteArray actualHash = emptyHash;
        if (hasExport) {
            actualPayload = exportIt->value(QStringLiteral("payload"));
            SyncWireMutation actual = mutation;
            actual.payload = actualPayload;
            actual.operation = SyncWireOperation::Put;
            actualHash = syncWireCanonicalPayloadHash(actual);
        }
        if (dispositionIt->materializedPayloadHash != actualHash) {
            if (error)
                *error = QStringLiteral(
                    "The server disposition hash does not match the fresh canonical export.");
            return false;
        }

        if (mutation.operation == SyncWireOperation::Delete) {
            if (hasExport) {
                if (error)
                    *error = QStringLiteral(
                        "A materialized attachment delete still appears in canonical export.");
                return false;
            }
            continue;
        }

        if (!hasExport) {
            if (dispositionIt->disposition != QLatin1String("superseded")) {
                if (error)
                    *error = QStringLiteral(
                        "A materialized attachment mutation is absent from canonical export.");
                return false;
            }
            continue;
        }

        if (mutation.category == QLatin1String("full_history")) {
            if (dispositionIt->disposition != QLatin1String("materialized")
                || !historyExportAbsorbs(mutation.payload, actualPayload)) {
                if (error)
                    *error = QStringLiteral(
                        "The fresh History export does not satisfy domain merge semantics.");
                return false;
            }
            continue;
        }

        if (mutation.category == QLatin1String("activity_fact")) {
            if (dispositionIt->disposition != QLatin1String("materialized")
                || item.canonicalPayloadHash != actualHash) {
                if (error)
                    *error = QStringLiteral(
                        "The immutable Activity event was not materially absorbed.");
                return false;
            }
            continue;
        }

        if (!lwwCategories.contains(mutation.category)) {
            if (error)
                *error = QStringLiteral(
                    "The attachment contains an unknown category.");
            return false;
        }
        if (dispositionIt->disposition == QLatin1String("materialized")) {
            if (item.canonicalPayloadHash != actualHash) {
                if (error)
                    *error = QStringLiteral(
                        "A materialized LWW mutation has a different canonical payload.");
                return false;
            }
        } else if (dispositionIt->disposition != QLatin1String("superseded")) {
            if (error)
                *error = QStringLiteral(
                    "The LWW attachment disposition is invalid.");
            return false;
        }
    }
    return true;
}

AccountAttachmentCoordinator::SourceState inspectAttachmentSource(
    const ProfilePaths &accountProfile,
    const AccountAttachmentReceiptData &receipt,
    QString *error) {
    QString sourceError;
    const auto storage = attachmentSourceStorage(
        accountProfile,
        receipt,
        &sourceError);
    if (!storage.has_value()) {
        if (error)
            *error = sourceError;
        return AccountAttachmentCoordinator::SourceState::Ambiguous;
    }

    QString captureError;
    const auto snapshot = storage->capture(&captureError);
    if (!snapshot.has_value()) {
        if (error)
            *error = captureError;
        return AccountAttachmentCoordinator::SourceState::Ambiguous;
    }

    const QString activityPath = storage->activityDbPath();
    const QString activityDigest =
        ActivityStore::fileDigestSha256(activityPath);
    const bool activityMatches =
        activityDigest == receipt.sourceActivityDigest;
    const bool semanticMatches =
        snapshot->matchesSemanticDigest(
            receipt.sourceSemanticDigest);
    const bool empty = snapshot->isEmpty()
        // The recorded digest describes the source before retirement.  A
        // cleared source must still classify as Empty even though that
        // historical digest remains in the receipt for exact identity.
        && !QFileInfo::exists(activityPath)
        && !QFileInfo::exists(activityPath + QStringLiteral("-wal"))
        && !QFileInfo::exists(activityPath + QStringLiteral("-shm"));
    if (empty)
        return AccountAttachmentCoordinator::SourceState::Empty;
    if (semanticMatches && activityMatches)
        return AccountAttachmentCoordinator::SourceState::Matching;

    if (error)
        *error = QStringLiteral(
            "The recorded attachment source changed after promotion.");
    return AccountAttachmentCoordinator::SourceState::Changed;
}

bool retireAttachmentSource(
    const ProfilePaths &accountProfile,
    const AccountAttachmentReceiptData &receipt,
    QString *error) {
    QString sourceError;
    const auto storage = attachmentSourceStorage(
        accountProfile,
        receipt,
        &sourceError);
    if (!storage.has_value()) {
        if (error)
            *error = sourceError;
        return false;
    }

    if (!storage->clearPersonalState(error))
        return false;

    const QString activityPath = storage->activityDbPath();
    const QStringList sidecars{
        activityPath,
        activityPath + QStringLiteral("-wal"),
        activityPath + QStringLiteral("-shm")};
    for (const QString &path : sidecars) {
        if (QFileInfo::exists(path)
            && !QFile::remove(path)) {
            if (error)
                *error = QStringLiteral(
                    "The attachment Activity source could not be retired safely.");
            return false;
        }
    }
    return true;
}

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
          &m_recoveryKeyPresenter),
      m_lifecycleCoordinator(
          &m_client,
          &m_credentialStore,
          &m_controller) {
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
            // Store replacement is a sync boundary. Preserve the current
            // profile's outbox while its adapters still point at the old
            // stores; otherwise the engine stays active after the registry is
            // emptied and the next account profile is never reconciled.
            if (m_syncEngine.active()) {
                QString error;
                if (!m_syncEngine.stopPreservingOutbox(
                        &error)) {
                    m_controller.setSyncObservation(
                        AccountController::
                            SyncState::Blocked,
                        m_syncEngine
                            .pendingOutboxCount());
                }
            }
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
            if (m_syncEngine.active())
                startOrResumeAccountAttachment();
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
                startOrResumeAccountAttachment();
            }

            m_syncEngine.setNetworkEnabled(
                true);
        });

}

bool AccountRuntime::installCoreSyncAdapters(
    QString *error) {
    clearCoreSyncAdapters();

    const ProfilePaths profile =
        m_profileStores.activeProfile();

    if (profile
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

    if (!m_downloadIntentStore.activate(profile, error))
        return false;

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
            watchStateAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(QStringLiteral("full_history"));
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
            activityAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(QStringLiteral("watch_state"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("full_history"));
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
        m_syncRegistry.unregisterAdapter(QStringLiteral("watch_state"));
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

    auto downloadIntentAdapter =
        std::make_unique<DownloadIntentSyncAdapter>(
            &m_downloadIntentStore);
    if (!m_syncRegistry.registerAdapter(
            downloadIntentAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(QStringLiteral("explicit_content_preference"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("watch_state"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("full_history"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("continue_progress"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("collection"));
        if (error) {
            *error = registryError.detail.isEmpty()
                ? registryError.code : registryError.detail;
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
    m_downloadIntentSyncAdapter =
        std::move(downloadIntentAdapter);

    const auto activityHistorySyncEnabled = [preferences]() {
        return preferences->syncActivityHistory()
            && preferences->keepActivityHistory();
    };
    const bool syncActivityHistory = activityHistorySyncEnabled();
    m_syncEngine.setCategoryNetworkEnabled(
        QStringLiteral("full_history"),
        syncActivityHistory);
    m_syncEngine.setCategoryNetworkEnabled(
        QStringLiteral("activity_fact"),
        syncActivityHistory);
    m_syncEngine.setCategoryNetworkEnabled(
        QStringLiteral("watch_state"),
        true);
    connect(
        preferences,
        &ProfilePreferencesStore::syncActivityHistoryChanged,
        this,
        [this, preferences, activityHistorySyncEnabled]() {
            const bool enabled = activityHistorySyncEnabled();
            m_syncEngine.setCategoryNetworkEnabled(
                QStringLiteral("full_history"),
                enabled);
            m_syncEngine.setCategoryNetworkEnabled(
                QStringLiteral("activity_fact"),
                enabled);
        });
    connect(
        preferences,
        &ProfilePreferencesStore::keepActivityHistoryChanged,
        this,
        [this, activityHistorySyncEnabled]() {
            const bool enabled = activityHistorySyncEnabled();
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
    m_attachmentCoordinator.reset();

    m_syncRegistry.unregisterAdapter(
        QStringLiteral("collection"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral(
            "continue_progress"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral(
            "full_history"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral("activity_fact"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral("watch_state"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral(
            "explicit_content_preference"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral("desired_download_intent"));
    m_downloadIntentStore.deactivate();

    m_preferencesSyncAdapter.reset();
    m_activitySyncAdapter.reset();
    m_historySyncAdapter.reset();
    m_progressSyncAdapter.reset();
    m_watchStateSyncAdapter.reset();
    m_collectionSyncAdapter.reset();
    m_downloadIntentSyncAdapter.reset();
}

void AccountRuntime::startOrResumeAccountAttachment() {
    if (!m_syncEngine.active())
        return;

    const ProfilePaths profile =
        m_profileStores.activeProfile();
    if (profile.kind() != ProfilePaths::Kind::Account
        || m_controller.accountId().isEmpty()
        || m_controller.deviceId().isEmpty()) {
        return;
    }

    AccountAttachmentReceipt::ReadResult pending =
        AccountAttachmentReceipt::read(profile);
    if (pending.status == AccountAttachmentReceipt::ReadStatus::Missing)
        return;
    if (pending.status == AccountAttachmentReceipt::ReadStatus::Invalid) {
        m_controller.setSyncObservation(
            AccountController::SyncState::Blocked,
            m_syncEngine.pendingOutboxCount());
        return;
    }

    if ((!pending.data.accountId.isEmpty()
         && pending.data.accountId != m_controller.accountId())
        || (!pending.data.deviceId.isEmpty()
            && pending.data.deviceId != m_controller.deviceId())) {
        // An attachment receipt is account/device bound evidence.  Never
        // resume it under a different authenticated session.
        m_controller.setSyncObservation(
            AccountController::SyncState::Blocked,
            m_syncEngine.pendingOutboxCount());
        return;
    }

    if (pending.data.accountId.isEmpty()
        || pending.data.deviceId.isEmpty()) {
        pending.data.accountId = m_controller.accountId();
        pending.data.deviceId = m_controller.deviceId();
        QString bindingError;
        if (!AccountAttachmentReceipt::save(
                profile,
                pending.data,
                &bindingError)) {
            m_controller.setSyncObservation(
                AccountController::SyncState::Blocked,
                m_syncEngine.pendingOutboxCount());
            return;
        }
    }

    // SyncEngine has already reconciled the active domain adapters by the
    // time AccountRuntime reaches this seam. Freeze that exact bounded
    // mutation set in the account receipt before the server Begin request;
    // the coordinator later enqueues the same identities in attachment mode.
    if (pending.data.manifest.isEmpty()) {
        QString manifestError;
        const QSet<QString> preexisting(
            pending.data.preexistingMutationIds.cbegin(),
            pending.data.preexistingMutationIds.cend());
        const QList<SyncWireAttachmentManifestItem> manifest =
            m_syncEngine.attachmentManifest(preexisting, &manifestError);
        if (!manifestError.isEmpty()) {
            m_controller.setSyncObservation(
                AccountController::SyncState::Blocked,
                m_syncEngine.pendingOutboxCount());
            return;
        }
        pending.data.manifest = manifest;
        QString manifestSaveError;
        if (!AccountAttachmentReceipt::save(
                profile,
                pending.data,
                &manifestSaveError)) {
            m_controller.setSyncObservation(
                AccountController::SyncState::Blocked,
                m_syncEngine.pendingOutboxCount());
            return;
        }
    }

    m_attachmentCoordinator =
        std::make_unique<AccountAttachmentCoordinator>(
            &m_client,
            &m_syncEngine,
            profile,
            this);

    m_attachmentCoordinator->setSourceLifecycle(
        [profile](
            const AccountAttachmentReceiptData &receipt,
            QString *error) {
            return inspectAttachmentSource(
                profile,
                receipt,
                error);
        },
        [profile](
            const AccountAttachmentReceiptData &receipt,
            QString *error) {
            return retireAttachmentSource(
                profile,
                receipt,
                error);
        });

    m_attachmentCoordinator->setCloudStateVerifier(
        [this, profile](QString *error) {
            const AccountAttachmentReceipt::ReadResult receipt =
                AccountAttachmentReceipt::read(profile);
            if (receipt.status != AccountAttachmentReceipt::ReadStatus::Ok) {
                if (error)
                    *error = receipt.error.isEmpty()
                        ? QStringLiteral(
                              "The account attachment receipt is unavailable during verification.")
                        : receipt.error;
                return false;
            }
            if (receipt.data.accountId != m_controller.accountId()
                || receipt.data.deviceId != m_controller.deviceId()) {
                if (error)
                    *error = QStringLiteral(
                        "The account attachment receipt is bound to a different session.");
                return false;
            }

            for (const SyncWireAttachmentManifestItem &item :
                 receipt.data.manifest) {
                SyncAdapterSnapshot snapshot;
                SyncAdapterRegistryError registryError;
                if (!m_syncRegistry.exportSnapshot(
                        item.mutation.category,
                        &snapshot,
                        &registryError)) {
                    if (error)
                        *error = registryError.detail.isEmpty()
                            ? registryError.code
                            : registryError.detail;
                    return false;
                }

                const auto record = std::find_if(
                    snapshot.records.constBegin(),
                    snapshot.records.constEnd(),
                    [&item](const SyncAdapterRecord &candidate) {
                        return candidate.recordKey
                            == item.mutation.recordKey;
                    });
                if (item.mutation.operation
                        == SyncWireOperation::Delete) {
                    if (record != snapshot.records.constEnd()
                        && !snapshot.tombstones.contains(
                               item.mutation.recordKey)) {
                        if (error)
                            *error = QStringLiteral(
                                "The attachment delete was not absorbed by the active owner.");
                        return false;
                    }
                    continue;
                }

                if (record == snapshot.records.constEnd()) {
                    if (error)
                        *error = QStringLiteral(
                            "The active owner did not absorb the attachment mutation.");
                    return false;
                }

                if (item.mutation.category
                        == QLatin1String("full_history")) {
                    if (!historyExportAbsorbs(
                            item.mutation.payload,
                            record->payload)) {
                        if (error)
                            *error = QStringLiteral(
                                "The History owner did not absorb the attachment merge.");
                        return false;
                    }
                } else if (item.mutation.category
                               == QLatin1String("activity_fact")) {
                    SyncWireMutation actual = item.mutation;
                    actual.payload = record->payload;
                    if (item.canonicalPayloadHash
                        != syncWireCanonicalPayloadHash(actual)) {
                        if (error)
                            *error = QStringLiteral(
                                "The Activity owner did not retain the immutable event.");
                        return false;
                    }
                } else if (item.mutation.category == QLatin1String("collection")
                           || item.mutation.category == QLatin1String("continue_progress")
                           || item.mutation.category == QLatin1String("watch_state")
                           || item.mutation.category == QLatin1String("desired_download_intent")
                           || item.mutation.category == QLatin1String("explicit_content_preference")) {
                    // Before the server commit there is no certified
                    // supersession disposition. The active owner must still
                    // contain the exact source payload.
                    if (item.mutation.payload != record->payload) {
                        if (error)
                            *error = QStringLiteral(
                                "The active owner changed an LWW attachment before server proof.");
                        return false;
                    }
                } else {
                    if (error)
                        *error = QStringLiteral(
                            "The attachment contains an unknown category.");
                    return false;
                }
            }
            return true;
        });

    m_attachmentCoordinator->setCanonicalExportVerifier(
        [this](
            const QList<SyncWireExportPage> &pages,
            QString *error) {
            QHash<QString, QJsonObject> canonical;
            for (const SyncWireExportPage &page : pages) {
                for (const QJsonValue &value : page.items) {
                    if (!value.isObject())
                        continue;
                    const QJsonObject item = value.toObject();
                    canonical.insert(
                        item.value(QStringLiteral("category")).toString()
                            + QChar(0x1f)
                            + item.value(QStringLiteral("key")).toString(),
                        item);
                }
            }

            const ProfilePaths profile =
                m_profileStores.activeProfile();
            const auto receipt =
                AccountAttachmentReceipt::read(profile);
            if (receipt.status != AccountAttachmentReceipt::ReadStatus::Ok) {
                if (error)
                    *error = receipt.error.isEmpty()
                        ? QStringLiteral(
                              "The account attachment receipt is unavailable during export verification.")
                        : receipt.error;
                return false;
            }
            for (const SyncWireAttachmentManifestItem &item :
                 receipt.data.manifest) {
                if (item.mutation.operation
                    == SyncWireOperation::Delete) {
                    continue;
                }
                const QString key = item.mutation.category
                    + QChar(0x1f)
                    + item.mutation.recordKey;
                const auto found = canonical.constFind(key);
                if (found == canonical.constEnd()) {
                    if (error)
                        *error = QStringLiteral(
                            "The fresh canonical export does not absorb the attachment source.");
                    return false;
                }

                const QJsonValue actual =
                    found->value(QStringLiteral("payload"));
                if (item.mutation.category == QLatin1String("full_history")) {
                    if (!historyExportAbsorbs(
                            item.mutation.payload,
                            actual)) {
                        if (error)
                            *error = QStringLiteral(
                                "The fresh History export does not satisfy domain merge semantics.");
                        return false;
                    }
                } else if (item.mutation.category == QLatin1String("activity_fact")) {
                    SyncWireMutation materialized = item.mutation;
                    materialized.payload = actual;
                    if (item.canonicalPayloadHash
                        != syncWireCanonicalPayloadHash(materialized)) {
                        if (error)
                            *error = QStringLiteral(
                                "The fresh Activity export does not contain the immutable event.");
                        return false;
                    }
                } else if (item.mutation.category == QLatin1String("collection")
                           || item.mutation.category == QLatin1String("continue_progress")
                           || item.mutation.category == QLatin1String("watch_state")
                           || item.mutation.category == QLatin1String("desired_download_intent")
                           || item.mutation.category == QLatin1String("explicit_content_preference")) {
                    // LWW supersession is accepted only through the server's
                    // per-manifest disposition verifier above. This legacy
                    // callback remains exact-match for isolated harnesses.
                    if (item.mutation.payload != actual) {
                        if (error)
                            *error = QStringLiteral(
                                "The fresh LWW export differs without a certified disposition.");
                        return false;
                    }
                } else {
                    if (error)
                        *error = QStringLiteral(
                            "The attachment contains an unknown category.");
                    return false;
                }
            }
            return true;
        });

    m_attachmentCoordinator->setAttachmentDispositionVerifier(
        [this, profile](
            const SyncWireAttachmentResponse &response,
            const QList<SyncWireExportPage> &pages,
            QString *error) {
            const AccountAttachmentReceipt::ReadResult receipt =
                AccountAttachmentReceipt::read(profile);
            if (receipt.status != AccountAttachmentReceipt::ReadStatus::Ok) {
                if (error)
                    *error = receipt.error.isEmpty()
                        ? QStringLiteral(
                              "The account attachment receipt is unavailable during disposition verification.")
                        : receipt.error;
                return false;
            }
            if (receipt.data.accountId != m_controller.accountId()
                || receipt.data.deviceId != m_controller.deviceId()) {
                if (error)
                    *error = QStringLiteral(
                        "The account attachment receipt is bound to a different session.");
                return false;
            }
            return verifyAttachmentDispositions(
                receipt.data,
                response,
                pages,
                error);
        });

    QString resumeError;
    if (!m_attachmentCoordinator->resumePending(&resumeError)) {
        m_controller.setSyncObservation(
            AccountController::SyncState::Blocked,
            m_syncEngine.pendingOutboxCount());
    }
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
    engine->rootContext()->setContextProperty(
        QStringLiteral("AccountLifecycle"),
        &m_lifecycleCoordinator);
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
    if (m_lifecycleCoordinator.hasPendingDeletion())
        m_lifecycleCoordinator.resumePendingDeletion();
    else
        m_controller.restoreRememberedSession();
}
