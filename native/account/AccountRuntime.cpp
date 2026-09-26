#include "AccountRuntime.h"

#include "ActivityStore.h"
#include "CollectionStore.h"
#include "AccountServiceEndpoint.h"
#include "engine/ExtensionsStore.h"
#include "LegacyPersonalStateStorage.h"
#include "ProfileAdoption.h"
#include "ProfilePreferencesStore.h"
#include "RatingsReviewsStore.h"
#include "ProgressStore.h"
#include "DownloadIntentSyncAdapter.h"
#include "watchparty/WatchPartyIdentity.h"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QDir>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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
        QStringLiteral("ratings_reviews"),
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

struct AccountRuntime::StremioImportBatch {
    QString profileId;
    QList<StremioLibraryItem> items;
    qsizetype nextItem = 0;
    int committedItems = 0;
    bool allCommitted = true;
    std::function<void(bool, int)> completion;
};

struct AccountRuntime::StremioRedoBatch {
    QString profileId;
    quint64 incarnation = 0;
    QList<StremioProviderImportRedo> redos;
    qsizetype nextRedo = 0;
};

namespace {
constexpr qsizetype kMaximumStremioEpisodePendingBytes = 16 * 1024;

std::optional<QJsonObject> episodePendingProjection(
    const StremioLibraryItem &item,
    const QString &encodedWatched) {
    if (encodedWatched.isEmpty()
        || encodedWatched.size() > kMaximumStremioEpisodePendingBytes) {
        return std::nullopt;
    }
    const StremioTheatreItemProjection canonical = StremioCodec::projectTheatreItem(item);
    if (!canonical.valid)
        return std::nullopt;
    const QJsonObject ownerProjection{
        {QStringLiteral("kind"), QStringLiteral("item")},
        {QStringLiteral("hasCollection"), canonical.hasCollection},
        {QStringLiteral("hasProgress"), canonical.hasProgress},
        {QStringLiteral("hasHistory"), canonical.hasHistory},
        {QStringLiteral("hasWatchState"), canonical.hasWatchState},
        {QStringLiteral("watched"), canonical.watched},
        {QStringLiteral("watchActionAtMs"), QString::number(canonical.watchActionAtMs)},
        {QStringLiteral("collection"), QJsonObject::fromVariantMap(canonical.collection)},
        {QStringLiteral("progress"), QJsonObject::fromVariantMap(canonical.progress)},
        {QStringLiteral("history"), QJsonObject::fromVariantMap(canonical.history)}};
    const QJsonObject pending{
        {QStringLiteral("kind"), QStringLiteral("episode_pending")},
        {QStringLiteral("watched"), encodedWatched},
        {QStringLiteral("item"), ownerProjection}};
    if (QJsonDocument(pending).toJson(QJsonDocument::Compact).size()
        > kMaximumStremioEpisodePendingBytes) {
        return std::nullopt;
    }
    return pending;
}

std::optional<StremioLibraryItem> itemFromEpisodePendingRedo(
    const StremioProviderImportRedo &redo,
    QString *encodedWatched,
    QJsonObject *canonicalProjection) {
    if (!encodedWatched || !canonicalProjection || redo.type != QLatin1String("series")
        || redo.projection.value(QStringLiteral("kind")).toString()
            != QLatin1String("episode_pending")) {
        return std::nullopt;
    }
    const QJsonValue watched = redo.projection.value(QStringLiteral("watched"));
    const QJsonValue owner = redo.projection.value(QStringLiteral("item"));
    if (!watched.isString() || watched.toString().isEmpty()
        || watched.toString().size() > kMaximumStremioEpisodePendingBytes
        || !owner.isObject()) {
        return std::nullopt;
    }
    const QJsonObject ownerProjection = owner.toObject();
    if (ownerProjection.value(QStringLiteral("kind")).toString() != QLatin1String("item"))
        return std::nullopt;
    StremioLibraryItem item;
    item.id = redo.id;
    item.type = redo.type;
    item.libraryMember = redo.libraryMember;
    item.removed = redo.removed;
    QJsonObject raw{{QStringLiteral("_id"), item.id},
                    {QStringLiteral("type"), item.type},
                    {QStringLiteral("state"), QJsonObject{{QStringLiteral("watched"), watched}}}};
    const QJsonObject collection = ownerProjection.value(QStringLiteral("collection")).toObject();
    const QString title = collection.value(QStringLiteral("title")).toString();
    if (!title.isEmpty())
        raw.insert(QStringLiteral("name"), title);
    item.raw = raw;
    *encodedWatched = watched.toString();
    *canonicalProjection = ownerProjection;
    return item;
}

QString addonTransportIdentity(const QJsonObject &document)
{
    return StremioCodec::normalizedAddonTransportUrl(
        document.value(QStringLiteral("transportUrl")).toString());
}

bool theatreAddonDocument(const QJsonObject &document)
{
    const QString transportUrl = document.value(QStringLiteral("transportUrl"))
        .toString();
    if (!transportUrl.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
        && !transportUrl.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)) {
        return false;
    }
    const QJsonArray types = document.value(QStringLiteral("manifest"))
        .toObject().value(QStringLiteral("types")).toArray();
    for (const QJsonValue &type : types) {
        const QString name = type.toString();
        if (name == QLatin1String("movie")
            || name == QLatin1String("series")
            || name == QLatin1String("anime")) {
            return true;
        }
    }
    return false;
}

QJsonArray stremioAddonDocuments(const QVariantList &rows)
{
    QJsonArray documents;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString transportUrl = row.value(QStringLiteral("transportUrl"))
            .toString();
        if (transportUrl.isEmpty())
            continue;
        QJsonObject document = QJsonObject::fromVariantMap(row);
        document.remove(QStringLiteral("id"));
        document.remove(QStringLiteral("installedAt"));
        document.remove(QStringLiteral("enabled"));
        document.remove(QStringLiteral("core"));
        document.insert(QStringLiteral("transportUrl"), transportUrl);
        if (!document.contains(QStringLiteral("transportName"))) {
            const QString name = row.value(QStringLiteral("manifest")).toMap()
                .value(QStringLiteral("name")).toString();
            if (!name.isEmpty())
                document.insert(QStringLiteral("transportName"), name);
        }
        if (theatreAddonDocument(document))
            documents.append(document);
    }
    return documents;
}

QVariantList localRowsForStremioAddons(
    const QJsonArray &addons,
    const QVariantList &existing)
{
    QHash<QString, QVariantMap> existingByIdentity;
    for (const QVariant &value : existing) {
        const QVariantMap row = value.toMap();
        const QString identity = StremioCodec::normalizedAddonTransportUrl(
            row.value(QStringLiteral("transportUrl")).toString());
        if (!identity.isEmpty() && !existingByIdentity.contains(identity))
            existingByIdentity.insert(identity, row);
    }

    QVariantList rows;
    for (const QJsonValue &value : addons) {
        if (!value.isObject())
            continue;
        const QJsonObject document = value.toObject();
        if (!theatreAddonDocument(document))
            continue;
        const QString identity = addonTransportIdentity(document);
        if (identity.isEmpty())
            continue;
        QVariantMap row = document.toVariantMap();
        const auto prior = existingByIdentity.constFind(identity);
        const QVariantMap old = prior == existingByIdentity.cend()
            ? QVariantMap{}
            : prior.value();
        const QVariantMap manifest = row.value(QStringLiteral("manifest")).toMap();
        const QString fallbackId = manifest.value(QStringLiteral("id")).toString();
        row.insert(QStringLiteral("id"), old.value(QStringLiteral("id"),
                                                   fallbackId.isEmpty()
                                                       ? identity
                                                       : fallbackId));
        row.insert(QStringLiteral("installedAt"), old.value(
            QStringLiteral("installedAt"), QDateTime::currentSecsSinceEpoch()));
        row.insert(QStringLiteral("enabled"), old.value(QStringLiteral("enabled"), true));
        row.insert(QStringLiteral("core"), false);
        rows.append(row);
    }
    return rows;
}
} // namespace

AccountRuntime::AccountRuntime(QObject *parent)
    : AccountRuntime(StremioSyncOptions{}, parent) {
}

AccountRuntime::AccountRuntime(
    const StremioSyncOptions &stremioOptions,
    QObject *parent)
    : QObject(parent),
      m_transport(AccountServiceEndpoint::configuredUrl()),
      m_client(&m_transport),
      m_recoveryKeyPresenter(&m_sensitiveClipboard),
      m_profileCoordinator(
          &m_profileStores,
          QString(),
          StremioCredentialAdoptionCallbacks{
              [this](const QString &profileId, const QString &accountId)
                  -> std::optional<QByteArray> {
                  const auto credential = m_credentialStore.loadStremio(
                      profileId, accountId);
                  return credential.has_value()
                      ? std::optional<QByteArray>(credential->authKey)
                      : std::nullopt;
              },
              [this](const QString &profileId,
                     const QString &accountId,
                     const QByteArray &authKey) {
                  return m_credentialStore.saveStremio(
                      StoredStremioCredential{profileId, accountId, authKey});
              },
              [this](const QString &profileId) {
                  return m_credentialStore.clearStremio(profileId);
              }},
          RatingsReviewsPrivateAdoptionCallbacks{
              [](const RatingsReviewsPrivateProfileBinding &source,
                 const RatingsReviewsPrivateProfileBinding &destination,
                 RatingsReviewsStore *destinationCanonical,
                 QString *error) {
                  return RatingsReviewsDelivery::handoffPrivateState(
                      source, destination, destinationCanonical, error);
              }}),
      m_syncRegistry(),
      m_syncEngine(
          &m_client,
          &m_syncRegistry),
      m_stremioSync(stremioOptions),
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
    m_stremioSync.setCredentialCallbacks(
        [this](const QString &profileId, const QString &accountId, const QByteArray &authKey) {
            return m_credentialStore.saveStremio(
                StoredStremioCredential{profileId, accountId, authKey});
        },
        [this](const QString &profileId) {
            return m_credentialStore.clearStremio(profileId);
        },
        [this](const QString &profileId, const QString &accountId)
            -> std::optional<QByteArray> {
            const auto credential = m_credentialStore.loadStremio(profileId, accountId);
            if (!credential.has_value())
                return std::nullopt;
            return credential->authKey;
        });
    connect(
        &m_stremioSync,
        &StremioSync::profileLinkValidated,
        this,
        [this](const QString &profileId) {
            ProfilePreferencesStore *preferences = m_profileStores.preferencesStore();
            const bool currentProfile = preferences
                && m_profileStores.activeProfile().profileId() == profileId;
            const bool persisted = currentProfile
                && preferences->setMainSyncProvider(QStringLiteral("stremio"));
            m_stremioSync.setMarkerLinked(
                persisted && preferences->mainSyncProvider() == QStringLiteral("stremio"));
            if (persisted) {
                QTimer::singleShot(0, this, [this] { syncStremioNow(); });
            }
        });
    connect(
        &m_stremioSync,
        &StremioSync::profileDisconnected,
        this,
        [this](const QString &profileId) {
            ProfilePreferencesStore *preferences = m_profileStores.preferencesStore();
            if (!preferences
                || m_profileStores.activeProfile().profileId() != profileId) {
                return;
            }
            preferences->setMainSyncProvider(QString());
        });
    m_stremioRegistryMutationConnection = connect(
        &m_syncRegistry,
        &SyncAdapterRegistry::localMutationAvailable,
        this,
        [this](const QString &categoryId, quint64) {
            if (categoryId == QLatin1String("collection")
                || categoryId == QLatin1String("continue_progress")
                || categoryId == QLatin1String("watch_state")) {
                scheduleStremioTheatreReconcile();
            }
        });
    m_stremioRemoteAppliedConnection = connect(
        &m_syncRegistry,
        &SyncAdapterRegistry::remoteApplied,
        this,
        [this](const QString &categoryId, const QString &, quint64) {
            if (categoryId == QLatin1String("collection")
                || categoryId == QLatin1String("continue_progress")
                || categoryId == QLatin1String("watch_state")) {
                // A remote registry notification is an invalidation only. The
                // deferred durable reread below decides whether its canonical
                // Theatre state differs from the provider; it is never a new
                // local action by itself.
                scheduleStremioTheatreReconcile();
            }
        });

    m_stremioPeriodicTimer.setInterval(15 * 60 * 1000);
    connect(&m_stremioPeriodicTimer, &QTimer::timeout,
            this, [this] { runStremioSync(false); });
    m_stremioPeriodicTimer.start();
    if (QGuiApplication *application = qobject_cast<QGuiApplication *>(
            QCoreApplication::instance())) {
        connect(application, &QGuiApplication::applicationStateChanged,
                this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive
                || m_stremioSync.lastSuccessAt() <= 0)
                return;
            const qint64 age = QDateTime::currentMSecsSinceEpoch()
                - m_stremioSync.lastSuccessAt();
            if (age >= 5 * 60 * 1000)
                runStremioSync(false);
        });
    }

    connect(
        &m_profileStores,
        &ProfileStoreRuntime::
            storesAboutToChange,
        this,
        [this]() {
            m_ratingsReviewsDelivery.deactivateProfile();
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
            m_stremioTheatreImporter.reset();
            ++m_stremioProfileIncarnation;
            m_stremioActiveImportCount = 0;
            m_stremioReconcileScheduled = false;
            m_stremioReconcileDeferred = false;
            m_stremioAddonReconcileScheduled = false;
            m_stremioAddonApplyInProgress = false;
            m_stremioAddonReconcileDeferred = false;
            m_stremioSync.deactivateProfile();
            if (m_extensionsStore)
                m_extensionsStore->deactivateProfile();
        });

    connect(
        &m_profileStores,
        &ProfileStoreRuntime::storesChanged,
        this,
        [this]() {
            activateRatingsReviewsDelivery();
            activateExtensionsProfile();
            activateStremioProfile();
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
    RatingsReviewsStore *ratingsReviews =
        m_profileStores.ratingsReviewsStore();
    QString ratingsReviewsError;

    if (!collection
        || !progress
        || !history
        || !activity
        || !preferences
        || !ratingsReviews
        || !ratingsReviews->healthy(&ratingsReviewsError)) {
        if (error) {
            *error = ratingsReviewsError.isEmpty()
                ? QStringLiteral(
                      "A required account sync owner is unavailable.")
                : ratingsReviewsError;
        }
        return false;
    }

    if (!m_downloadIntentStore.activate(profile, error))
        return false;

    auto ratingsReviewsAdapter =
        std::make_unique<RatingsReviewsSyncAdapter>(
            ratingsReviews);
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
    auto stremioLinkAdapter =
        std::make_unique<StremioLinkSyncAdapter>(preferences);
    auto ratingsReviewsConversionAdapter =
        std::make_unique<RatingsReviewsConversionSyncAdapter>(preferences);

    SyncAdapterRegistryError registryError;
    if (!m_syncRegistry.registerAdapter(
            ratingsReviewsAdapter.get(),
            &registryError)) {
        if (error) {
            *error = registryError.detail.isEmpty()
                ? registryError.code
                : registryError.detail;
        }
        return false;
    }

    if (!m_syncRegistry.registerAdapter(
            collectionAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(
            QStringLiteral("ratings_reviews"));
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
        m_syncRegistry.unregisterAdapter(
            QStringLiteral("ratings_reviews"));

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
        m_syncRegistry.unregisterAdapter(
            QStringLiteral("ratings_reviews"));

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
        m_syncRegistry.unregisterAdapter(QStringLiteral("ratings_reviews"));
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
        m_syncRegistry.unregisterAdapter(QStringLiteral("ratings_reviews"));
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
        m_syncRegistry.unregisterAdapter(
            QStringLiteral("ratings_reviews"));

        if (error) {
            *error =
                registryError.detail.isEmpty()
                ? registryError.code
                : registryError.detail;
        }
        return false;
    }

    if (!m_syncRegistry.registerAdapter(
            stremioLinkAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(QStringLiteral("explicit_content_preference"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("activity_fact"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("watch_state"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("full_history"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("continue_progress"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("collection"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("ratings_reviews"));
        if (error)
            *error = registryError.detail.isEmpty() ? registryError.code : registryError.detail;
        return false;
    }

    auto downloadIntentAdapter =
        std::make_unique<DownloadIntentSyncAdapter>(
            &m_downloadIntentStore);
    if (!m_syncRegistry.registerAdapter(
            downloadIntentAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(QStringLiteral("stremio_link"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("explicit_content_preference"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("watch_state"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("full_history"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("continue_progress"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("collection"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("ratings_reviews"));
        if (error) {
            *error = registryError.detail.isEmpty()
                ? registryError.code : registryError.detail;
        }
        return false;
    }

    if (!m_syncRegistry.registerAdapter(
            ratingsReviewsConversionAdapter.get(),
            &registryError)) {
        m_syncRegistry.unregisterAdapter(QStringLiteral("desired_download_intent"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("stremio_link"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("explicit_content_preference"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("activity_fact"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("watch_state"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("full_history"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("continue_progress"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("collection"));
        m_syncRegistry.unregisterAdapter(QStringLiteral("ratings_reviews"));
        if (error)
            *error = registryError.detail.isEmpty() ? registryError.code : registryError.detail;
        return false;
    }

    m_ratingsReviewsSyncAdapter =
        std::move(ratingsReviewsAdapter);
    m_ratingsReviewsConversionSyncAdapter =
        std::move(ratingsReviewsConversionAdapter);
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
    m_stremioLinkSyncAdapter =
        std::move(stremioLinkAdapter);
    m_downloadIntentSyncAdapter =
        std::move(downloadIntentAdapter);

    // Account profile activation creates the importer before account-session
    // setup starts the ordinary engine. Bind the now-complete registry here
    // so provider imports use the same remote-applied fences and local
    // mutation observation as every other account owner.
    if (m_stremioTheatreImporter)
        m_stremioTheatreImporter->setSyncAdapterRegistry(&m_syncRegistry);

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
    m_syncEngine.setCategoryNetworkEnabled(
        QStringLiteral("ratings_reviews"),
        true);
    m_syncEngine.setCategoryNetworkEnabled(
        QStringLiteral("ratings_reviews_conversion_maps"),
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
    m_syncRegistry.unregisterAdapter(QStringLiteral("ratings_reviews_conversion_maps"));

    m_syncRegistry.unregisterAdapter(
        QStringLiteral("ratings_reviews"));
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
    m_syncRegistry.unregisterAdapter(QStringLiteral("stremio_link"));
    m_syncRegistry.unregisterAdapter(
        QStringLiteral("desired_download_intent"));
    m_downloadIntentStore.deactivate();

    m_preferencesSyncAdapter.reset();
    m_ratingsReviewsConversionSyncAdapter.reset();
    m_ratingsReviewsSyncAdapter.reset();
    m_stremioLinkSyncAdapter.reset();
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
                    if (item.mutation.category
                            == QLatin1String("ratings_reviews")) {
                        const qint64 deletedAtMs =
                            snapshot.tombstoneEventMs.value(
                                item.mutation.recordKey, -1);
                        if (record != snapshot.records.constEnd()
                            || !snapshot.tombstones.contains(
                                   item.mutation.recordKey)
                            || !item.mutation.deletedAtMs.has_value()
                            || deletedAtMs != *item.mutation.deletedAtMs) {
                            if (error)
                                *error = QStringLiteral(
                                    "The Ratings & Reviews delete timestamp was not absorbed exactly.");
                            return false;
                        }
                    } else if (record != snapshot.records.constEnd()
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
                           || item.mutation.category == QLatin1String("ratings_reviews")
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
                    if (item.mutation.category
                            == QLatin1String("ratings_reviews")) {
                        const QString key = item.mutation.category
                            + QChar(0x1f)
                            + item.mutation.recordKey;
                        if (canonical.contains(key)) {
                            if (error)
                                *error = QStringLiteral(
                                    "The fresh Ratings & Reviews export still contains a deleted key.");
                            return false;
                        }
                    }
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
                           || item.mutation.category == QLatin1String("ratings_reviews")
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

RatingsReviewsDelivery *AccountRuntime::ratingsReviewsDelivery() {
    return &m_ratingsReviewsDelivery;
}

void AccountRuntime::activateRatingsReviewsDelivery() {
    const ProfilePaths paths = m_profileStores.activeProfile();
    if (paths.kind() == ProfilePaths::Kind::Sealed)
        return;

    // A promoted first-account profile is not allowed to activate its private
    // delivery lane until the separate RR handoff is durably verified.  Older
    // accounts without an adoption journal remain valid.
    if (paths.kind() == ProfilePaths::Kind::Account
        && QFileInfo::exists(paths.adoptionJournalPath())) {
        QString adoptionError;
        const auto adoption = ProfileAdoption::open(paths, &adoptionError);
        if (!adoption.has_value()
            || (adoption->snapshot().ratingsReviewsPrivateHandoffMarkerRecorded
                && !adoption->snapshot().ratingsReviewsPrivateHandoffVerified)) {
            qWarning() << "Ratings/Reviews delivery activation blocked by private adoption:"
                       << (adoptionError.isEmpty()
                               ? QStringLiteral("private handoff is incomplete")
                               : adoptionError);
            return;
        }
    }

    RatingsReviewsPrivatePaths privatePaths;
    if (paths.kind() == ProfilePaths::Kind::LegacyLocal) {
        const LegacyPersonalStateStorage &legacy = m_profileStores.legacyStorage();
        privatePaths = {
            legacy.devicePrivateRatingsReviewsProviderMappingsPath(),
            legacy.devicePrivateRatingsReviewsDeliveryOutboxPath(),
            legacy.devicePrivateRatingsReviewsDeliveryReceiptsPath()};
    } else {
        privatePaths = {
            paths.ratingsReviewsProviderMappingsPath(),
            paths.ratingsReviewsDeliveryOutboxPath(),
            paths.ratingsReviewsDeliveryReceiptsPath()};
    }

    QString error;
    if (!m_ratingsReviewsDelivery.activateProfile(
            {paths.profileId(), privatePaths, m_profileStores.ratingsReviewsStore()},
            &error)) {
        qWarning() << "Ratings/Reviews delivery activation blocked:" << error;
    }
}

AccountController *AccountRuntime::controller() {
    return &m_controller;
}

void AccountRuntime::setExtensionsStore(ExtensionsStore *extensions) {
    if (m_extensionsChangedConnection)
        disconnect(m_extensionsChangedConnection);
    m_extensionsStore = extensions;
    if (m_extensionsStore) {
        m_extensionsChangedConnection = connect(
            m_extensionsStore,
            &ExtensionsStore::changed,
            this,
            [this] { scheduleStremioAddonReconcile(); });
    }
    activateExtensionsProfile();
    scheduleStremioAddonReconcile();
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
    activateRatingsReviewsDelivery();

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
    engine->rootContext()->setContextProperty(
        QStringLiteral("stremioSyncState"),
        &m_stremioSync);
    engine->rootContext()->setContextProperty(
        QStringLiteral("StremioActions"),
        this);

    activateStremioProfile();
    m_stremioSync.activateTaggedFixture();

    m_qmlPrepared = true;
    if (m_lifecycleCoordinator.hasPendingDeletion())
        m_lifecycleCoordinator.resumePendingDeletion();
    else
        m_controller.restoreRememberedSession();
}

bool AccountRuntime::applyStremioLibraryItem(
    const StremioLibraryItem &item,
    StremioTheatreImporter::Completion completion) {
    if (!m_stremioTheatreImporter) {
        if (completion) {
            completion(false,
                       QStringLiteral("The active Stremio Theatre importer is unavailable."));
        }
        return false;
    }
    const QString profileId = m_profileStores.activeProfile().profileId();
    const quint64 incarnation = m_stremioProfileIncarnation;
    const auto settled = std::make_shared<bool>(false);
    ++m_stremioActiveImportCount;
    const auto finish = std::make_shared<StremioTheatreImporter::Completion>(
        [this, profileId, incarnation, settled,
         completion = std::move(completion)](bool committed, const QString &error) mutable {
            if (*settled)
                return;
            *settled = true;
            if (profileId == m_profileStores.activeProfile().profileId()
                && incarnation == m_stremioProfileIncarnation
                && m_stremioActiveImportCount > 0) {
                --m_stremioActiveImportCount;
                if (m_stremioActiveImportCount == 0 && m_stremioReconcileDeferred) {
                    m_stremioReconcileDeferred = false;
                    scheduleStremioTheatreReconcile();
                }
            }
            if (completion)
                completion(committed, error);
        });
    const QJsonValue watched = item.raw.value(QStringLiteral("state"))
        .toObject().value(QStringLiteral("watched"));
    if (item.type == QLatin1String("series")
        && !watched.isUndefined() && !watched.isNull()
        && !watched.isString()) {
        (*finish)(false, QStringLiteral("The Stremio series watched field is malformed."));
        return false;
    }
    if (item.type == QLatin1String("series")
        && watched.isString() && !watched.toString().isEmpty()) {
        const auto projection = episodePendingProjection(item, watched.toString());
        if (!projection.has_value()) {
            (*finish)(false, QStringLiteral("The Stremio series watch replay is malformed."));
            return false;
        }
        const bool started = m_stremioSync.beginProviderImport(
            item,
            *projection,
            [this, item, encodedWatched = watched.toString(), profileId, incarnation, finish](
                bool committed, const QString &receipt) {
                if (!committed || receipt.isEmpty()
                    || profileId != m_profileStores.activeProfile().profileId()
                    || incarnation != m_stremioProfileIncarnation) {
                    (*finish)(false,
                              QStringLiteral("The Stremio series watch replay was not committed."));
                    return;
                }
                if (!applyStremioSeriesWatchedAfterRedo(
                        item, encodedWatched, receipt, {}, *finish)) {
                    (*finish)(false,
                              QStringLiteral("The Stremio series watch replay could not start."));
                }
            });
        if (!started && !*settled) {
            (*finish)(false, QStringLiteral("The Stremio series watch replay could not start."));
        }
        return started;
    }
    const bool started = m_stremioTheatreImporter->apply(
        item,
        [finish](bool committed, const QString &error) {
            (*finish)(committed, error);
        });
    if (!started && !*settled) {
        (*finish)(false, QStringLiteral("The Stremio provider import could not start."));
    }
    return started;
}

bool AccountRuntime::applyStremioSeriesWatchedAfterRedo(
    const StremioLibraryItem &item,
    const QString &encodedWatched,
    const QString &outerRedoReceipt,
    const QJsonObject &canonicalProjection,
    StremioTheatreImporter::Completion completion) {
    if (!m_stremioTheatreImporter || item.type != QLatin1String("series")
        || encodedWatched.isEmpty() || outerRedoReceipt.isEmpty()) {
        if (completion) {
            completion(false,
                       QStringLiteral("The Stremio series watch owners are unavailable."));
        }
        return false;
    }
    const QString profileId = m_profileStores.activeProfile().profileId();
    const quint64 incarnation = m_stremioProfileIncarnation;
    const auto afterCanonicalOwners =
        [this, item, encodedWatched, outerRedoReceipt, profileId, incarnation,
         completion = std::move(completion)](bool committed, const QString &error) mutable {
            if (!committed || profileId != m_profileStores.activeProfile().profileId()
                || incarnation != m_stremioProfileIncarnation) {
                if (completion)
                    completion(false, error.isEmpty()
                                        ? QStringLiteral("The Stremio series owner did not commit.")
                                        : error);
                return;
            }
            if (!m_stremioSync.requestEpisodeMetadata(
                    item.id,
                    [this, item, encodedWatched, outerRedoReceipt, profileId, incarnation,
                     completion = std::move(completion)](
                        bool resolved, QList<StremioEpisodeIdentity> videos) mutable {
                        if (!resolved || videos.isEmpty()
                            || profileId != m_profileStores.activeProfile().profileId()
                            || incarnation != m_stremioProfileIncarnation) {
                            if (completion) {
                                completion(false,
                                           QStringLiteral("The Stremio episode metadata is unavailable."));
                            }
                            return;
                        }
                        if (!m_stremioTheatreImporter->applyWatchedEpisodes(
                                item, encodedWatched, videos,
                                [this, outerRedoReceipt, profileId, incarnation,
                                 completion = std::move(completion)](
                                    bool episodeCommitted, const QString &episodeError) mutable {
                                    if (!episodeCommitted
                                        || profileId != m_profileStores.activeProfile().profileId()
                                        || incarnation != m_stremioProfileIncarnation) {
                                        if (completion)
                                            completion(false, episodeError);
                                        return;
                                    }
                                    if (!m_stremioSync.settleProviderImport(
                                            outerRedoReceipt,
                                            [completion = std::move(completion)](bool settled) mutable {
                                                if (completion) {
                                                    completion(
                                                        settled,
                                                        settled ? QString()
                                                                : QStringLiteral(
                                                                    "The Stremio series watch replay could not settle."));
                                                }
                                            })) {
                                        if (completion) {
                                            completion(false,
                                                       QStringLiteral(
                                                           "The Stremio series watch replay could not settle."));
                                        }
                                    }
                                })) {
                            if (completion) {
                                completion(false,
                                           QStringLiteral("The Stremio episode import could not start."));
                            }
                        }
                    })) {
                if (completion) {
                    completion(false,
                               QStringLiteral("The Stremio episode metadata request could not start."));
                }
            }
        };
    const bool started = canonicalProjection.isEmpty()
        ? m_stremioTheatreImporter->apply(item, afterCanonicalOwners)
        : m_stremioTheatreImporter->applyCanonicalProjectionAfterRedo(
            item, canonicalProjection, afterCanonicalOwners);
    if (!started && completion) {
        completion(false, QStringLiteral("The Stremio series owner could not start."));
    }
    return started;
}

void AccountRuntime::refreshStremioLibrary(
    std::function<void(bool, int)> completion) {
    const QString profileId = m_profileStores.activeProfile().profileId();
    if (profileId.isEmpty() || !m_stremioTheatreImporter) {
        if (completion)
            completion(false, 0);
        return;
    }
    m_stremioSync.pullLibraryItems(
        [this, profileId, completion = std::move(completion)](
            bool succeeded, QList<StremioLibraryItem> items) mutable {
            if (!succeeded || profileId != m_profileStores.activeProfile().profileId()) {
                if (completion)
                    completion(false, 0);
                return;
            }
            const auto batch = std::make_shared<StremioImportBatch>();
            batch->profileId = profileId;
            batch->items = std::move(items);
            batch->completion = std::move(completion);
            applyNextStremioLibraryItem(batch);
        });
}

void AccountRuntime::applyNextStremioLibraryItem(
    const std::shared_ptr<StremioImportBatch> &batch) {
    if (!batch || batch->profileId != m_profileStores.activeProfile().profileId()
        || !m_stremioTheatreImporter) {
        return;
    }
    if (batch->nextItem >= batch->items.size()) {
        // A missing or ambiguous episode map leaves its own durable redo for
        // a later bridge/restart attempt. It must not falsely publish the
        // first-merge baseline while that provider record remains unsettled.
        if (m_stremioSync.pendingProviderImports().isEmpty()) {
            m_stremioSync.completeFirstMerge(
                [batch](bool committed) {
                    if (batch->completion) {
                        auto completion = std::move(batch->completion);
                        completion(committed && batch->allCommitted,
                                   batch->committedItems);
                    }
                });
        } else if (batch->completion) {
            auto completion = std::move(batch->completion);
            completion(false, batch->committedItems);
        }
        return;
    }
    const StremioLibraryItem item = batch->items.at(batch->nextItem++);
    applyStremioLibraryItem(
        item,
        [this, batch, item](bool committed, const QString &error) {
            batch->allCommitted = batch->allCommitted && committed;
            if (committed) {
                ++batch->committedItems;
            } else {
                qWarning().noquote()
                    << "[stremio] provider import did not settle for"
                    << item.id << ":" << error;
            }
            // Provider rows are isolated: an unavailable metadata map is
            // private pending work, never a reason to stop the next valid
            // library record from reaching its canonical owner.
            applyNextStremioLibraryItem(batch);
        });
}

bool AccountRuntime::syncStremioNow() {
    return runStremioSync(true);
}

bool AccountRuntime::runStremioSync(bool reviveFailedIntents) {
    if (!m_stremioSync.beginVisibleSync(reviveFailedIntents))
        return false;
    const QString profileId = m_profileStores.activeProfile().profileId();
    const quint64 incarnation = m_stremioProfileIncarnation;
    const quint64 generation = ++m_visibleSyncGeneration;
    refreshStremioLibrary(
        [this, profileId, incarnation, generation](bool libraryCommitted, int itemCount) {
            if (generation != m_visibleSyncGeneration
                || profileId != m_profileStores.activeProfile().profileId()
                || incarnation != m_stremioProfileIncarnation) {
                return;
            }
            if (!libraryCommitted || !m_extensionsStore) {
                m_stremioSync.finishVisibleSync(
                    false, QStringLiteral("Sync could not finish. Your Colosseum data was kept."));
                return;
            }
            const int ownerRevision = m_extensionsStore->revision();
            const QJsonArray local = stremioAddonDocuments(m_extensionsStore->stremioRows());
            const bool started = m_stremioSync.reconcileAddonCollection(
                local,
                [this, profileId, incarnation, generation, ownerRevision, itemCount](
                    bool addonsSettled, QJsonArray settled) {
                    if (generation != m_visibleSyncGeneration
                        || profileId != m_profileStores.activeProfile().profileId()
                        || incarnation != m_stremioProfileIncarnation) {
                        return;
                    }
                    if (!addonsSettled || !m_extensionsStore
                        || ownerRevision != m_extensionsStore->revision()) {
                        m_stremioSync.finishVisibleSync(
                            false, QStringLiteral("Sync could not finish. Your Colosseum data was kept."));
                        return;
                    }
                    const QVariantList rows = localRowsForStremioAddons(
                        settled, m_extensionsStore->stremioRows());
                    m_stremioAddonApplyInProgress = true;
                    const bool applyStarted = m_extensionsStore->applyTheatreRows(
                        rows,
                        [this, profileId, incarnation, generation, itemCount, settled](
                            bool committed, const QString &) {
                            if (generation != m_visibleSyncGeneration
                                || profileId != m_profileStores.activeProfile().profileId()
                                || incarnation != m_stremioProfileIncarnation) {
                                return;
                            }
                            if (!committed || !m_extensionsStore) {
                                m_stremioAddonApplyInProgress = false;
                                m_stremioSync.finishVisibleSync(
                                    false, QStringLiteral("Sync could not finish. Your Colosseum data was kept."));
                                return;
                            }
                            const QJsonArray owner = stremioAddonDocuments(
                                m_extensionsStore->stremioRows());
                            m_stremioSync.acknowledgeAddonCollectionOwner(
                                owner,
                                [this, generation, itemCount, addonCount = settled.size()](bool acknowledged) {
                                    if (generation != m_visibleSyncGeneration)
                                        return;
                                    m_stremioAddonApplyInProgress = false;
                                    const QString summary = acknowledged
                                        ? QStringLiteral("Sync complete · %1 library items · %2 addons")
                                              .arg(itemCount).arg(addonCount)
                                        : QStringLiteral("Sync could not finish. Your Colosseum data was kept.");
                                    m_stremioSync.finishVisibleSync(acknowledged, summary);
                                    if (acknowledged) {
                                        scheduleStremioTheatreReconcile();
                                        m_stremioSync.retryPendingNow();
                                    }
                                    if (std::exchange(m_stremioAddonReconcileDeferred, false))
                                        scheduleStremioAddonReconcile();
                                });
                        });
                    if (!applyStarted) {
                        m_stremioAddonApplyInProgress = false;
                        m_stremioSync.finishVisibleSync(
                            false, QStringLiteral("Sync could not finish. Your Colosseum data was kept."));
                    }
                });
            if (!started) {
                m_stremioSync.finishVisibleSync(
                    false, QStringLiteral("Sync could not finish. Your Colosseum data was kept."));
            }
        });
    return true;
}

bool AccountRuntime::removeTheatreItem(
    const QString &id,
    const QString &type,
    bool alsoStremio) {
    const QString normalizedId = id.trimmed();
    const QString normalizedType = type.trimmed().toLower();
    CollectionStore *collection = m_profileStores.collectionStore();
    if (!collection || normalizedId.isEmpty()
        || (normalizedType != QLatin1String("movie")
            && normalizedType != QLatin1String("series"))) {
        return false;
    }
    if (!m_stremioSync.linkedAccount()) {
        return !alsoStremio
            && collection->remove(QStringLiteral("theatre"), normalizedId);
    }
    const QString profileId = m_profileStores.activeProfile().profileId();
    const quint64 incarnation = m_stremioProfileIncarnation;
    const auto removeOwner = [this, profileId, incarnation, normalizedId](bool committed) {
        if (!committed
            || profileId != m_profileStores.activeProfile().profileId()
            || incarnation != m_stremioProfileIncarnation) {
            return;
        }
        if (CollectionStore *current = m_profileStores.collectionStore())
            current->remove(QStringLiteral("theatre"), normalizedId);
    };
    return alsoStremio
        ? m_stremioSync.queueExplicitLibraryRemoval(
              normalizedId, normalizedType, removeOwner)
        : m_stremioSync.recordLocalOnlyLibraryRemoval(
              normalizedId, normalizedType, removeOwner);
}

void AccountRuntime::replayPendingStremioProviderImports() {
    if (!m_stremioTheatreImporter)
        return;
    const QList<StremioProviderImportRedo> redos = m_stremioSync.pendingProviderImports();
    if (redos.isEmpty())
        return;
    const auto batch = std::make_shared<StremioRedoBatch>();
    batch->profileId = m_profileStores.activeProfile().profileId();
    batch->incarnation = m_stremioProfileIncarnation;
    batch->redos = redos;
    replayNextStremioProviderImport(batch);
}

void AccountRuntime::replayNextStremioProviderImport(
    const std::shared_ptr<StremioRedoBatch> &batch) {
    if (!batch || batch->profileId != m_profileStores.activeProfile().profileId()
        || batch->incarnation != m_stremioProfileIncarnation
        || !m_stremioTheatreImporter
        || batch->nextRedo >= batch->redos.size()) {
        return;
    }
    const StremioProviderImportRedo redo = batch->redos.at(batch->nextRedo++);
    ++m_stremioActiveImportCount;
    const auto settled = std::make_shared<bool>(false);
    const auto replayCompletion = [this, batch, settled, operationId = redo.operationId](
        bool committed, const QString &) {
            if (*settled)
                return;
            *settled = true;
            if (batch->profileId != m_profileStores.activeProfile().profileId()
                || batch->incarnation != m_stremioProfileIncarnation) {
                return;
            }
            if (m_stremioActiveImportCount > 0)
                --m_stremioActiveImportCount;
            if (!committed) {
                // A nested series replay can settle a later inner redo before
                // this captured batch reaches it. Skip only that already
                // retired receipt; a still-pending malformed/missing map
                // remains private work and does not block unrelated records.
                const QList<StremioProviderImportRedo> pendingRedos =
                    m_stremioSync.pendingProviderImports();
                const bool stillPending = std::any_of(
                    pendingRedos.cbegin(), pendingRedos.cend(),
                    [&operationId](const StremioProviderImportRedo &candidate) {
                        return candidate.operationId == operationId;
                    });
                if (!stillPending)
                    replayNextStremioProviderImport(batch);
                return;
            }
            replayNextStremioProviderImport(batch);
        };
    bool started = false;
    if (redo.projection.value(QStringLiteral("kind")).toString()
        == QLatin1String("episode_pending")) {
        QString encodedWatched;
        QJsonObject canonicalProjection;
        const auto item = itemFromEpisodePendingRedo(
            redo, &encodedWatched, &canonicalProjection);
        if (item.has_value()) {
            started = applyStremioSeriesWatchedAfterRedo(
                *item, encodedWatched, redo.operationId,
                canonicalProjection, replayCompletion);
        }
    } else {
        started = m_stremioTheatreImporter->replayProviderImport(redo, replayCompletion);
    }
    if (!started && !*settled
        && batch->profileId == m_profileStores.activeProfile().profileId()
        && batch->incarnation == m_stremioProfileIncarnation
        && m_stremioActiveImportCount > 0) {
        --m_stremioActiveImportCount;
    }
}

void AccountRuntime::scheduleStremioTheatreReconcile() {
    const QString profileId = m_profileStores.activeProfile().profileId();
    if (profileId.isEmpty() || !m_stremioTheatreImporter)
        return;
    if (m_stremioActiveImportCount > 0) {
        m_stremioReconcileDeferred = true;
        return;
    }
    if (m_stremioReconcileScheduled)
        return;
    const quint64 incarnation = m_stremioProfileIncarnation;
    m_stremioReconcileScheduled = true;
    QTimer::singleShot(0, this, [this, profileId, incarnation] {
        m_stremioReconcileScheduled = false;
        reconcileStremioTheatreState(profileId, incarnation);
    });
}

void AccountRuntime::reconcileStremioTheatreState(
    const QString &profileId,
    quint64 incarnation) {
    if (profileId != m_profileStores.activeProfile().profileId()
        || incarnation != m_stremioProfileIncarnation
        || m_stremioActiveImportCount > 0) {
        return;
    }
    ProfilePreferencesStore *preferences = m_profileStores.preferencesStore();
    ProgressStore *progress = m_profileStores.progressStore();
    if (!preferences || !progress
        || preferences->mainSyncProvider() != QLatin1String("stremio")) {
        return;
    }
    // `recordSilent` has no visual revision by design, but it still schedules
    // a writer snapshot. This receipt is the required owner-durability fence;
    // the GUI thread never blocks on a flush.
    progress->requestDurableReceipt(
        [this, profileId, incarnation](bool committed, const QString &) {
            if (!committed
                || profileId != m_profileStores.activeProfile().profileId()
                || incarnation != m_stremioProfileIncarnation
                || m_stremioActiveImportCount > 0) {
                return;
            }
            ProfilePreferencesStore *preferences = m_profileStores.preferencesStore();
            CollectionStore *collection = m_profileStores.collectionStore();
            ProgressStore *currentProgress = m_profileStores.progressStore();
            if (!preferences || !collection || !currentProgress
                || preferences->mainSyncProvider() != QLatin1String("stremio")) {
                return;
            }
            m_stremioSync.reconcileTheatreState(
                collection->items(QStringLiteral("theatre")),
                currentProgress->syncEntries(),
                currentProgress->syncWatchedMarks(),
                currentProgress->syncWatchedMarkActionTimes());
        });
}

void AccountRuntime::scheduleStremioAddonReconcile()
{
    const QString profileId = m_profileStores.activeProfile().profileId();
    ProfilePreferencesStore *preferences = m_profileStores.preferencesStore();
    if (!m_extensionsStore || profileId.isEmpty() || !preferences
        || preferences->mainSyncProvider() != QLatin1String("stremio")) {
        return;
    }
    if (m_stremioAddonApplyInProgress) {
        m_stremioAddonReconcileDeferred = true;
        return;
    }
    if (m_stremioAddonReconcileScheduled)
        return;
    const quint64 incarnation = m_stremioProfileIncarnation;
    m_stremioAddonReconcileScheduled = true;
    QTimer::singleShot(0, this, [this, profileId, incarnation] {
        m_stremioAddonReconcileScheduled = false;
        reconcileStremioAddonCollection(profileId, incarnation);
    });
}

void AccountRuntime::reconcileStremioAddonCollection(
    const QString &profileId,
    quint64 incarnation)
{
    if (!m_extensionsStore
        || profileId != m_profileStores.activeProfile().profileId()
        || incarnation != m_stremioProfileIncarnation) {
        return;
    }
    const int ownerRevision = m_extensionsStore->revision();
    const QJsonArray local = stremioAddonDocuments(m_extensionsStore->stremioRows());
    const bool started = m_stremioSync.reconcileAddonCollection(
        local,
        [this, profileId, incarnation, ownerRevision](bool succeeded,
                                                       QJsonArray settled) {
            if (profileId != m_profileStores.activeProfile().profileId()
                || incarnation != m_stremioProfileIncarnation) {
                return;
            }
            if (!succeeded)
                return;
            if (!m_extensionsStore
                || ownerRevision != m_extensionsStore->revision()) {
                // A local action won the race while the provider round trip
                // was in flight. Never overwrite it with an older readback.
                scheduleStremioAddonReconcile();
                return;
            }
            applyStremioAddonCollection(profileId, incarnation, settled);
        });
    if (!started
        && profileId == m_profileStores.activeProfile().profileId()
        && incarnation == m_stremioProfileIncarnation
        && m_stremioSync.status() == QLatin1String("synced")) {
        // A same-profile local mutation can arrive while another addon round
        // owns the single datastore reply. Retry only while the binding is
        // connected; disconnected profiles wait for the next reconnect marker
        // instead of spinning a timer against an unavailable provider.
        QTimer::singleShot(100, this, [this] { scheduleStremioAddonReconcile(); });
    }
}

void AccountRuntime::applyStremioAddonCollection(
    const QString &profileId,
    quint64 incarnation,
    const QJsonArray &addons)
{
    if (!m_extensionsStore
        || profileId != m_profileStores.activeProfile().profileId()
        || incarnation != m_stremioProfileIncarnation) {
        return;
    }
    const QVariantList rows = localRowsForStremioAddons(
        addons, m_extensionsStore->stremioRows());
    m_stremioAddonApplyInProgress = true;
    const bool started = m_extensionsStore->applyTheatreRows(
        rows,
        [this, profileId, incarnation](bool committed, const QString &) {
            if (profileId != m_profileStores.activeProfile().profileId()
                || incarnation != m_stremioProfileIncarnation) {
                return;
            }
            if (!committed || !m_extensionsStore) {
                m_stremioAddonApplyInProgress = false;
                QTimer::singleShot(100, this, [this] {
                    scheduleStremioAddonReconcile();
                });
                return;
            }
            const QJsonArray local = stremioAddonDocuments(
                m_extensionsStore->stremioRows());
            const bool acknowledgementStarted =
                m_stremioSync.acknowledgeAddonCollectionOwner(
                    local,
                    [this, profileId, incarnation](bool acknowledged) {
                        if (profileId != m_profileStores.activeProfile().profileId()
                            || incarnation != m_stremioProfileIncarnation) {
                            return;
                        }
                        m_stremioAddonApplyInProgress = false;
                        const bool deferred = std::exchange(
                            m_stremioAddonReconcileDeferred, false);
                        if (deferred) {
                            scheduleStremioAddonReconcile();
                        } else if (!acknowledged) {
                            QTimer::singleShot(100, this, [this] {
                                scheduleStremioAddonReconcile();
                            });
                        }
                    });
            if (!acknowledgementStarted) {
                m_stremioAddonApplyInProgress = false;
                QTimer::singleShot(100, this, [this] {
                    scheduleStremioAddonReconcile();
                });
            }
        });
    if (!started)
        m_stremioAddonApplyInProgress = false;
}

void AccountRuntime::activateStremioProfile() {
    const ProfilePaths profile = m_profileStores.activeProfile();
    ++m_stremioProfileIncarnation;
    ++m_visibleSyncGeneration;
    m_stremioActiveImportCount = 0;
    m_stremioReconcileScheduled = false;
    m_stremioReconcileDeferred = false;
    m_stremioAddonReconcileScheduled = false;
    m_stremioAddonApplyInProgress = false;
    m_stremioAddonReconcileDeferred = false;
    const bool sealed = profile.kind() == ProfilePaths::Kind::Sealed;
    const bool accountlessLocal = profile.kind() == ProfilePaths::Kind::LocalOnly
        || profile.kind() == ProfilePaths::Kind::LegacyLocal;
    const QString stremioStatePath = profile.kind() == ProfilePaths::Kind::LegacyLocal
        ? m_profileStores.legacyStorage().devicePrivateStremioStatePath()
        : profile.stremioSyncStatePath();
    QString ignored;
    m_stremioSync.activateProfile(
        profile.profileId(),
        stremioStatePath,
        sealed,
        &ignored);
    m_stremioTheatreImporter = std::make_unique<StremioTheatreImporter>(
        m_profileStores.collectionStore(),
        m_profileStores.progressStore(),
        m_profileStores.historyStore(),
        this);
    m_stremioTheatreImporter->activate(profile.profileId());
    // Local-only profiles have no ordinary account registry ownership. They
    // still durably apply provider state through the real direct owners; an
    // empty registry would incorrectly reject every replay/import before the
    // profile can later seed Neon during adoption.
    if (m_syncEngine.active())
        m_stremioTheatreImporter->setSyncAdapterRegistry(&m_syncRegistry);
    m_stremioTheatreImporter->setStremioSync(&m_stremioSync);
    m_stremioTheatreImporter->setNeonCheckpoint(
        [this, accountlessLocal](StremioTheatreImporter::Completion completion) {
            if (!m_syncEngine.active()) {
                if (accountlessLocal) {
                    // Local-only canonical owners remain durable and become
                    // ordinary-sync candidates later during account adoption.
                    completion(true, {});
                } else {
                    // An account profile still owes its Neon relay. Keep the
                    // private redo unsatisfied until the ordinary engine is
                    // active; the owner state itself is never rolled back.
                    completion(false, QStringLiteral("The account sync engine is inactive."));
                }
                return;
            }
            QString error;
            if (!m_syncEngine.checkpointProviderImport(
                    std::move(completion), &error)) {
                // checkpointProviderImport invokes completion on every
                // established failure path; this covers only a failed start
                // whose callback could not be registered.
            }
        });
    // A crash can leave a committed provider redo between its private journal
    // receipt and the owner/Neon completion. Drain only this profile's
    // captured incarnation through the same importer/checkpoint path; remote
    // notifications remain invalidations and cannot create a second action.
    replayPendingStremioProviderImports();
    if (m_stremioProgressDirtyConnection)
        disconnect(m_stremioProgressDirtyConnection);
    if (m_stremioWatchStateConnection)
        disconnect(m_stremioWatchStateConnection);
    if (m_stremioCollectionDirtyConnection)
        disconnect(m_stremioCollectionDirtyConnection);
    if (ProgressStore *progress = m_profileStores.progressStore()) {
        m_stremioProgressDirtyConnection = connect(
            progress,
            &ProgressStore::syncDirty,
            this,
            [this] { scheduleStremioTheatreReconcile(); });
        m_stremioWatchStateConnection = connect(
            progress,
            &ProgressStore::watchStateChanged,
            this,
            [this] { scheduleStremioTheatreReconcile(); });
    }
    if (CollectionStore *collection = m_profileStores.collectionStore()) {
        m_stremioCollectionDirtyConnection = connect(
            collection,
            &CollectionStore::syncDirty,
            this,
            [this] { scheduleStremioTheatreReconcile(); });
    }
    if (m_stremioMarkerConnection)
        disconnect(m_stremioMarkerConnection);
    ProfilePreferencesStore *preferences = m_profileStores.preferencesStore();
    if (!preferences)
        return;
    m_stremioSync.setMarkerLinked(
        preferences->mainSyncProvider() == QStringLiteral("stremio"));
    m_stremioMarkerConnection = connect(
        preferences,
        &ProfilePreferencesStore::mainSyncProviderChanged,
        this,
        [this, preferences]() {
            if (preferences != m_profileStores.preferencesStore())
                return;
            const bool linked = preferences->mainSyncProvider() == QStringLiteral("stremio");
            m_stremioSync.setMarkerLinked(linked);
            if (linked) {
                scheduleStremioTheatreReconcile();
                QTimer::singleShot(0, this, [this] { runStremioSync(false); });
            }
        });
    if (preferences->mainSyncProvider() == QStringLiteral("stremio")) {
        scheduleStremioTheatreReconcile();
        QTimer::singleShot(0, this, [this] { runStremioSync(false); });
    }
}

void AccountRuntime::activateExtensionsProfile() {
    if (!m_extensionsStore)
        return;
    const ProfilePaths profile = m_profileStores.activeProfile();
    const QString profileRoot = profile.kind() == ProfilePaths::Kind::LegacyLocal
        ? m_profileStores.legacyStorage().devicePrivateProfileRoot()
        : profile.profileRoot();
    if (profile.kind() == ProfilePaths::Kind::Sealed
        || profileRoot.isEmpty()) {
        m_extensionsStore->deactivateProfile();
        return;
    }
    const QString theatreIndexPath = QDir::cleanPath(
        profileRoot + QStringLiteral("/extensions/installed.json"));
    if (!m_extensionsStore->activateProfile(profile.profileId(), theatreIndexPath))
        m_extensionsStore->deactivateProfile();
}
