#include "StremioSync.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace {
constexpr int kMaximumRetries = 5;
constexpr int kAuthTimeoutMs = 5 * 60 * 1000;
constexpr qsizetype kMaximumCallbackBytes = 16 * 1024;
constexpr qsizetype kMaximumIdentityResponseBytes = 64 * 1024;
constexpr qsizetype kMaximumDatastoreResponseBytes = 1024 * 1024;
constexpr int kMaximumLibraryRows = 256;
constexpr int kMaximumAddonCollectionAttempts = 3;
constexpr qsizetype kMaximumProviderRedoProjectionBytes = 16 * 1024;
constexpr qsizetype kMaximumWatchedFieldBytes = 16 * 1024;
constexpr int kMaximumEpisodeMetadataRows = 4096;

bool decodeSeriesWatchedDesired(
    const QJsonObject &desired,
    QList<StremioEpisodeIdentity> *videos,
    QSet<QString> *localWatched) {
    if (!videos || !localWatched
        || desired.value(QStringLiteral("type")).toString() != QLatin1String("series")) {
        return false;
    }
    const QString seriesId = desired.value(QStringLiteral("id")).toString().trimmed();
    const QJsonArray source = desired.value(QStringLiteral("episodes")).toArray();
    const QJsonArray sourceWatched = desired.value(QStringLiteral("episodeIds")).toArray();
    if (seriesId.isEmpty() || seriesId.size() > 512 || source.isEmpty()
        || source.size() > kMaximumEpisodeMetadataRows || sourceWatched.isEmpty()
        || sourceWatched.size() > source.size()) {
        return false;
    }
    QList<StremioEpisodeIdentity> parsedVideos;
    QSet<QString> knownIds;
    QSet<QString> knownCoordinates;
    parsedVideos.reserve(source.size());
    for (const QJsonValue &value : source) {
        if (!value.isObject())
            return false;
        const QJsonObject episode = value.toObject();
        const QJsonValue season = episode.value(QStringLiteral("season"));
        const QJsonValue number = episode.value(QStringLiteral("episode"));
        const QString id = episode.value(QStringLiteral("id")).toString();
        if (episode.size() != 3 || id.isEmpty() || id != id.trimmed() || id.size() > 512
            || id.contains(QChar::Null) || id.contains(QChar::ReplacementCharacter)
            || !season.isDouble() || !number.isDouble()
            || !std::isfinite(season.toDouble()) || !std::isfinite(number.toDouble())
            || std::floor(season.toDouble()) != season.toDouble()
            || std::floor(number.toDouble()) != number.toDouble()
            || season.toDouble() < 0 || number.toDouble() < 0
            || season.toDouble() > 10000 || number.toDouble() > 100000) {
            return false;
        }
        const StremioEpisodeIdentity identity{
            id, static_cast<int>(season.toDouble()), static_cast<int>(number.toDouble())};
        const QString coordinate = QString::number(identity.season)
            + QLatin1Char(':') + QString::number(identity.episode);
        if (knownIds.contains(id) || knownCoordinates.contains(coordinate)
            || !StremioCodec::episodeBelongsToSeries(seriesId, identity))
            return false;
        knownIds.insert(id);
        knownCoordinates.insert(coordinate);
        parsedVideos.append(identity);
    }
    QSet<QString> parsedWatched;
    for (const QJsonValue &value : sourceWatched) {
        if (!value.isString() || !knownIds.contains(value.toString())
            || parsedWatched.contains(value.toString())) {
            return false;
        }
        parsedWatched.insert(value.toString());
    }
    *videos = std::move(parsedVideos);
    *localWatched = std::move(parsedWatched);
    return true;
}

// A datastore GET is the provider-side conflict boundary.  Missing activity
// is valid legacy provider data, while a malformed timestamp is not a safe
// basis for replacing state with a retry.
std::optional<qint64> providerLastWatchedActivity(const QJsonObject &item) {
    const QJsonValue raw = item.value(QStringLiteral("state"))
                               .toObject()
                               .value(QStringLiteral("lastWatched"));
    if (raw.isUndefined() || raw.isNull())
        return qint64{0};
    if (!raw.isString())
        return std::nullopt;
    const QString text = raw.toString();
    if (text.isEmpty() || text.trimmed() != text || text.size() > 128)
        return std::nullopt;
    const QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid() || parsed.timeSpec() == Qt::LocalTime
        || parsed.toMSecsSinceEpoch() <= 0) {
        return std::nullopt;
    }
    return parsed.toMSecsSinceEpoch();
}

bool desiredActivity(const QJsonObject &desired, qint64 *milliseconds) {
    if (!milliseconds)
        return false;
    const QJsonValue value = desired.value(QStringLiteral("updatedAt"));
    bool ok = false;
    const qint64 parsed = value.isString()
        ? value.toString().toLongLong(&ok)
        : value.toVariant().toLongLong(&ok);
    if (!ok || parsed <= 0)
        return false;
    *milliseconds = parsed;
    return true;
}

QString addonTransportIdentity(const QJsonValue &value) {
    if (!value.isObject())
        return {};
    return StremioCodec::normalizedAddonTransportUrl(
        value.toObject().value(QStringLiteral("transportUrl")).toString());
}

QJsonArray deduplicatedAddonUnion(const QJsonArray &remote, const QJsonArray &local) {
    QJsonArray merged;
    QSet<QString> seen;
    const auto append = [&merged, &seen](const QJsonArray &source) {
        for (const QJsonValue &value : source) {
            const QString identity = addonTransportIdentity(value);
            if (identity.isEmpty() || seen.contains(identity))
                continue;
            seen.insert(identity);
            merged.append(value);
        }
    };
    append(remote);
    append(local);
    return merged;
}

QJsonObject mergeAddonConfiguration(const QJsonObject &remote,
                                    const QJsonObject &local) {
    // A local install/remove/reorder operation may only change the provider
    // fields Colosseum owns. Keep every other field returned by Stremio so a
    // concurrent client or a future provider version is not erased by a
    // whole-document write.
    QJsonObject merged = remote;
    for (const QString &key : {QStringLiteral("transportUrl"),
                               QStringLiteral("transportName"),
                               QStringLiteral("manifest"),
                               QStringLiteral("flags")}) {
        if (local.contains(key))
            merged.insert(key, local.value(key));
    }
    return merged;
}

QJsonArray addonCollectionDeltaReconcile(const QJsonArray &remote,
                                         const QJsonArray &baselineRemote,
                                         const QJsonArray &baselineLocal,
                                         const QJsonArray &local) {
    QHash<QString, QJsonObject> remoteByIdentity;
    QHash<QString, QJsonObject> baselineLocalByIdentity;
    QHash<QString, QJsonObject> localByIdentity;
    QStringList remoteOrder;
    QStringList baselineLocalOrder;
    QStringList localOrder;

    const auto index = [](const QJsonArray &source,
                          QHash<QString, QJsonObject> *byIdentity,
                          QStringList *order) {
        for (const QJsonValue &value : source) {
            if (!value.isObject())
                continue;
            const QString identity = addonTransportIdentity(value);
            if (identity.isEmpty() || byIdentity->contains(identity))
                continue;
            byIdentity->insert(identity, value.toObject());
            order->append(identity);
        }
    };
    index(remote, &remoteByIdentity, &remoteOrder);
    index(baselineLocal, &baselineLocalByIdentity, &baselineLocalOrder);
    index(local, &localByIdentity, &localOrder);

    QSet<QString> explicitlyRemoved;
    for (const QString &identity : baselineLocalOrder) {
        if (!localByIdentity.contains(identity))
            explicitlyRemoved.insert(identity);
    }

    QSet<QString> explicitlyChanged;
    QSet<QString> explicitlyAdded;
    for (const QString &identity : localOrder) {
        if (!baselineLocalByIdentity.contains(identity)) {
            explicitlyAdded.insert(identity);
        } else if (baselineLocalByIdentity.value(identity) != localByIdentity.value(identity)) {
            explicitlyChanged.insert(identity);
        }
    }

    QStringList baselineSurvivorOrder;
    for (const QString &identity : baselineLocalOrder) {
        if (localByIdentity.contains(identity))
            baselineSurvivorOrder.append(identity);
    }
    QStringList currentManagedOrder;
    for (const QString &identity : localOrder) {
        if (baselineLocalByIdentity.contains(identity))
            currentManagedOrder.append(identity);
    }
    bool orderChanged = baselineSurvivorOrder != currentManagedOrder;
    if (!orderChanged) {
        // A newly installed row inserted before an existing baseline row is
        // also an explicit order change, even though the baseline-only
        // subsequence above is unchanged.
        int lastBaselinePosition = -1;
        for (const QString &identity : localOrder) {
            if (!baselineLocalByIdentity.contains(identity))
                continue;
            const int position = localOrder.indexOf(identity);
            if (position < lastBaselinePosition) {
                orderChanged = true;
                break;
            }
            lastBaselinePosition = position;
        }
        if (!orderChanged) {
            const int firstAdded = std::find_if(localOrder.cbegin(), localOrder.cend(),
                [&baselineLocalByIdentity](const QString &identity) {
                    return !baselineLocalByIdentity.contains(identity);
                }) - localOrder.cbegin();
            if (firstAdded >= 0 && firstAdded < localOrder.size()) {
                for (int i = firstAdded + 1; i < localOrder.size(); ++i) {
                    if (baselineLocalByIdentity.contains(localOrder.at(i))) {
                        orderChanged = true;
                        break;
                    }
                }
            }
        }
    }

    const auto remoteRowFor = [&remoteByIdentity, &localByIdentity, &explicitlyChanged](
                                  const QString &identity) {
        if (!remoteByIdentity.contains(identity))
            return localByIdentity.value(identity);
        if (explicitlyChanged.contains(identity))
            return mergeAddonConfiguration(remoteByIdentity.value(identity),
                                           localByIdentity.value(identity));
        return remoteByIdentity.value(identity);
    };

    QJsonArray desired;
    QSet<QString> emitted;
    if (orderChanged) {
        // Keep remote-only entries intact, then let the active local owner
        // express its deliberate order. This is the stable rebase for a
        // local reorder: unknown remote rows survive and local rows become
        // the managed suffix in their current order.
        for (const QString &identity : remoteOrder) {
            if (explicitlyRemoved.contains(identity) || localByIdentity.contains(identity))
                continue;
            desired.append(remoteByIdentity.value(identity));
            emitted.insert(identity);
        }
        for (const QString &identity : localOrder) {
            if (emitted.contains(identity))
                continue;
            if (!remoteByIdentity.contains(identity)
                && !explicitlyAdded.contains(identity)
                && !explicitlyChanged.contains(identity)) {
                continue;
            }
            desired.append(remoteRowFor(identity));
            emitted.insert(identity);
        }
        return desired;
    }

    // No local reorder: preserve the fresh remote order, apply only explicit
    // removals/updates, then append new local rows in local order.
    for (const QString &identity : remoteOrder) {
        if (explicitlyRemoved.contains(identity))
            continue;
        desired.append(remoteRowFor(identity));
        emitted.insert(identity);
    }
    for (const QString &identity : localOrder) {
        if (emitted.contains(identity))
            continue;
        if (!explicitlyAdded.contains(identity)
            && !explicitlyChanged.contains(identity)) {
            continue;
        }
        desired.append(localByIdentity.value(identity));
        emitted.insert(identity);
    }
    return desired;
}

bool addonCollectionSetAccepted(const QJsonValue &result) {
    if (result.isBool())
        return result.toBool();
    return result.isObject()
        && result.toObject().value(QStringLiteral("success")).isBool()
        && result.toObject().value(QStringLiteral("success")).toBool();
}

QByteArray successPage() {
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 92\r\nConnection: close\r\n\r\n<!doctype html><title>Colosseum</title><p>Sign-in complete. You can return to Colosseum.</p>");
}

QByteArray failurePage() {
    return QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 89\r\nConnection: close\r\n\r\n<!doctype html><title>Colosseum</title><p>Sign-in could not be completed. Return to Colosseum.</p>");
}

std::optional<StremioProviderImportRedo> providerImportRedoFromJson(
    const QJsonValue &value) {
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject object = value.toObject();
    bool generationOk = false;
    const quint64 generation = object.value(QStringLiteral("bindingGeneration"))
        .toString().toULongLong(&generationOk);
    const QJsonValue projection = object.value(QStringLiteral("projection"));
    if (!generationOk || !projection.isObject())
        return std::nullopt;
    StremioProviderImportRedo redo;
    redo.operationId = object.value(QStringLiteral("operationId")).toString();
    redo.profileId = object.value(QStringLiteral("profileId")).toString();
    redo.accountId = object.value(QStringLiteral("accountId")).toString();
    redo.bindingGeneration = generation;
    redo.id = object.value(QStringLiteral("id")).toString();
    redo.type = object.value(QStringLiteral("type")).toString();
    redo.libraryMember = object.value(QStringLiteral("libraryMember")).toBool();
    redo.removed = object.value(QStringLiteral("removed")).toBool();
    redo.projection = projection.toObject();
    return redo;
}
}

struct StremioSync::LibraryPull {
    ProfileBinding binding;
    QList<QStringList> batches;
    qsizetype nextBatch = 0;
    QList<StremioLibraryItem> items;
    std::function<void(bool, QList<StremioLibraryItem>)> completion;
};

struct StremioSync::AddonCollectionReconcile {
    ProfileBinding binding;
    QJsonArray local;
    QJsonArray desired;
    int attempts = 0;
    std::function<void(bool, QJsonArray)> completion;
};

StremioSync::StremioSync(const StremioSyncOptions &options, QObject *parent)
    : QObject(parent),
      m_options(options),
      m_stateStore(this) {
    setObjectName(QStringLiteral("stremioSyncState"));
    m_authTimeout.setSingleShot(true);
    m_retryTimer.setSingleShot(true);
    if (!m_options.clock)
        m_options.clock = [] { return QDateTime::currentMSecsSinceEpoch(); };
    if (!m_options.browserOpener) {
        m_options.browserOpener = [](const QUrl &url) {
            QDesktopServices::openUrl(url);
        };
    }
    connect(&m_callbackServer, &QTcpServer::newConnection,
            this, &StremioSync::handleIncomingConnection);
    connect(&m_authTimeout, &QTimer::timeout, this, [this] {
        cancelAuthentication();
        setStatus(QStringLiteral("notConnected"));
        finishRun();
    });
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        if (m_markerLinked)
            retryPendingNow();
    });
    connect(&m_stateStore, &StremioState::persistenceCommitted,
            this, [this](quint64 generation) {
                settlePersistence(generation, true);
            });
    connect(&m_stateStore, &StremioState::persistenceFailed,
            this, [this](quint64 generation, const QString &) {
                settlePersistence(generation, false);
            });
}

QString StremioSync::status() const { return m_status; }
int StremioSync::pendingCount() const { return m_state.pendingIntents.size(); }
qint64 StremioSync::lastSuccessAt() const { return m_state.lastSuccessAtMs; }
QString StremioSync::activeProfileId() const { return m_profileId; }
bool StremioSync::mergeComplete() const { return m_state.firstMergeComplete; }
quint64 StremioSync::completedRun() const { return m_completedRun; }
QString StremioSync::accountDisplayName() const { return m_state.displayName; }
QString StremioSync::lastResultSummary() const { return m_lastResultSummary; }
bool StremioSync::linkedAccount() const {
    return m_markerLinked && !m_state.accountId.isEmpty();
}

bool StremioSync::activateProfile(
    const QString &profileId,
    const QString &statePath,
    bool sealed,
    QString *error) {
    deactivateProfile();
    ++m_bindingGeneration;
    if (sealed || profileId.trimmed().isEmpty() || statePath.trimmed().isEmpty()) {
        setStatus(QStringLiteral("unavailable"));
        return true;
    }
    QString loadError;
    const auto pending = m_pendingStateByPath.constFind(statePath);
    const auto loaded = pending == m_pendingStateByPath.cend()
        ? m_stateStore.load(statePath, &loadError)
        : std::optional<StremioPersistentState>(*pending);
    if (!loaded.has_value()) {
        if (error)
            *error = loadError;
        setStatus(QStringLiteral("paused"));
        return false;
    }
    if (!loaded->profileId.isEmpty() && loaded->profileId != profileId) {
        if (error)
            *error = QStringLiteral("The Stremio state belongs to another profile.");
        setStatus(QStringLiteral("paused"));
        return false;
    }
    m_profileId = profileId;
    m_statePath = statePath;
    m_state = *loaded;
    m_state.profileId = profileId;
    m_state.bindingGeneration = m_bindingGeneration;
    m_hasUsableCredential = false;
    m_markerLinked = false;
    m_dispatchAllowed = !m_state.reconnectRequired
        && !m_failedPersistencePaths.contains(statePath);
    m_inFlightOperations.clear();
    if (!m_state.reconnectRequired && !m_state.accountId.isEmpty() && m_options.loadCredential) {
        const auto credential = m_options.loadCredential(m_profileId, m_state.accountId);
        m_hasUsableCredential = credential.has_value() && !credential->isEmpty();
    }
    updateConnectionStatus();
    emit stateChanged();
    return true;
}

void StremioSync::deactivateProfile() {
    cancelAuthentication();
    cancelEpisodeMetadataRequests();
    if (m_datastoreReply) {
        m_datastoreReply->abort();
        m_datastoreReply = nullptr;
    }
    m_datastoreResponse.clear();
    m_datastoreResponseTooLarge = false;
    m_retryTimer.stop();
    ++m_bindingGeneration;
    m_profileId.clear();
    m_statePath.clear();
    m_state = {};
    m_hasUsableCredential = false;
    m_markerLinked = false;
    m_dispatchAllowed = false;
    m_inFlightOperations.clear();
    m_addonCollectionReconcileActive = false;
    m_visibleSyncActive = false;
    m_visibleSyncPullComplete = false;
    m_lastResultSummary.clear();
    m_pendingVisibleSyncSummary.clear();
    emit stateChanged();
}

bool StremioSync::activateTaggedFixture() {
    if (qEnvironmentVariable("COLOSSEUM_APPDATA_TAG")
        != QByteArrayLiteral("stremio-task1-fixture"))
        return false;

    deactivateProfile();
    ++m_bindingGeneration;
    m_profileId = QStringLiteral("stremio-task1-fixture");
    m_state.profileId = m_profileId;
    m_state.bindingGeneration = m_bindingGeneration;
    // These are deliberately inert, non-secret terminal fixture facts. They prove
    // the projection can carry an acknowledged record and receipt without opening
    // an endpoint, credential, or product control.
    m_state.acknowledgedBaselines = QJsonObject{
        {QStringLiteral("fixture-record"), QStringLiteral("acknowledged")}};
    m_state.importRedoReceipts = QJsonArray{QStringLiteral("fixture-receipt")};
    m_state.lastSuccessAtMs = 1;
    m_state.firstMergeComplete = true;
    m_markerLinked = true;
    m_hasUsableCredential = false;
    m_dispatchAllowed = false;
    m_completedRun = 1;
    setStatus(QStringLiteral("synced"));
    return true;
}

bool StremioSync::startBrowserAuthentication(QString *error) {
    if (m_profileId.isEmpty() || m_statePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("A Stremio profile is not active.");
        return false;
    }
    if (!endpointAllowed()) {
        if (error)
            *error = QStringLiteral("The Stremio endpoint is not permitted.");
        return false;
    }
    cancelAuthentication();
    QByteArray nonce(32, '\0');
    for (char &byte : nonce)
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    m_callbackPath = QStringLiteral("/stremio/")
        + QString::fromLatin1(nonce.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    if (!m_callbackServer.listen(QHostAddress::LocalHost, 0)) {
        if (error)
            *error = QStringLiteral("The Stremio callback listener could not start.");
        return false;
    }
    ++m_authAttempt;
    const QUrl callback(QStringLiteral("http://127.0.0.1:%1%2")
        .arg(m_callbackServer.serverPort()).arg(m_callbackPath));
    const QUrl login = StremioCodec::browserLoginUrl(callback);
    setStatus(QStringLiteral("connecting"));
    m_authTimeout.start(kAuthTimeoutMs);
    emit browserLoginRequested(login);
    if (m_options.browserOpener)
        m_options.browserOpener(login);
    return true;
}

void StremioSync::cancelAuthentication() {
    retireProvisionalCredential();
    ++m_authAttempt;
    m_authTimeout.stop();
    m_callbackServer.close();
    if (m_callbackSocket)
        m_callbackSocket->disconnectFromHost();
    m_callbackSocket = nullptr;
    m_callbackBuffer.clear();
    m_callbackPath.clear();
    if (m_identityReply) {
        m_identityReply->abort();
        m_identityReply = nullptr;
    }
    m_identityResponse.clear();
    m_identityResponseTooLarge = false;
}

bool StremioSync::disconnectProfile(std::function<void(bool)> completion) {
    if (m_profileId.isEmpty() || m_statePath.isEmpty()) {
        if (completion)
            completion(false);
        return false;
    }

    const QString profileId = m_profileId;
    const QString accountId = m_state.accountId;

    // Retire the binding before aborting any reply: QNetworkReply::abort may
    // emit finished synchronously on this thread. Every cancellation callback
    // must therefore already observe a stale generation.
    ++m_bindingGeneration;
    m_inFlightOperations.clear();
    m_addonCollectionReconcileActive = false;
    m_visibleSyncActive = false;
    m_visibleSyncPullComplete = false;
    m_lastResultSummary.clear();
    m_pendingVisibleSyncSummary.clear();

    cancelAuthentication();
    cancelEpisodeMetadataRequests();
    if (m_datastoreReply) {
        m_datastoreReply->abort();
        m_datastoreReply = nullptr;
    }
    m_datastoreResponse.clear();
    m_datastoreResponseTooLarge = false;
    m_retryTimer.stop();

    bool credentialCleared = m_options.clearCredential
        && m_options.clearCredential(profileId);
    if (!credentialCleared && m_options.loadCredential && !accountId.isEmpty()) {
        credentialCleared = !m_options.loadCredential(profileId, accountId).has_value();
    }
    if (!credentialCleared) {
        updateConnectionStatus();
        if (completion)
            completion(false);
        return false;
    }

    m_state = {};
    m_state.profileId = profileId;
    m_state.bindingGeneration = m_bindingGeneration;
    m_hasUsableCredential = false;
    m_markerLinked = false;
    m_dispatchAllowed = false;
    m_failedPersistencePaths.remove(m_statePath);
    updateConnectionStatus();
    emit stateChanged();

    const ProfileBinding binding{profileId, m_bindingGeneration};
    persist([this, binding, profileId, completion = std::move(completion)](bool committed) mutable {
        const bool succeeded = committed && bindingCurrent(binding);
        if (succeeded) {
            emit profileDisconnected(profileId);
            finishRun();
        }
        if (completion)
            completion(succeeded);
    });
    return true;
}

bool StremioSync::connectAccount() {
    return startBrowserAuthentication();
}

bool StremioSync::disconnectCurrentProfile() {
    return disconnectProfile();
}

bool StremioSync::switchAccount() {
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    return disconnectProfile([this, binding](bool succeeded) {
        if (!succeeded || binding.profileId != m_profileId)
            return;
        startBrowserAuthentication();
    });
}

bool StremioSync::beginVisibleSync(bool reviveFailedIntents) {
    if (m_visibleSyncActive
        || (m_status != QLatin1String("synced")
            && m_status != QLatin1String("syncFailed"))
        || !linkedAccount() || !m_hasUsableCredential || !m_dispatchAllowed) {
        return false;
    }
    bool revived = false;
    if (reviveFailedIntents) {
        const qint64 now = m_options.clock();
        for (StremioPendingIntent &intent : m_state.pendingIntents) {
            if (intent.remoteAcknowledged || intent.attempts < kMaximumRetries)
                continue;
            intent.attempts = 0;
            intent.retryAtMs = now;
            revived = true;
        }
    }
    m_visibleSyncActive = true;
    m_visibleSyncPullComplete = false;
    m_lastResultSummary.clear();
    m_pendingVisibleSyncSummary.clear();
    setStatus(QStringLiteral("syncing"));
    if (revived)
        persist();
    emit stateChanged();
    return true;
}

void StremioSync::finishVisibleSync(bool succeeded, const QString &summary) {
    if (!m_visibleSyncActive)
        return;
    if (succeeded && std::any_of(
            m_state.pendingIntents.cbegin(),
            m_state.pendingIntents.cend(),
            [](const StremioPendingIntent &intent) {
                return !intent.remoteAcknowledged
                    && intent.attempts >= kMaximumRetries;
            })) {
        m_visibleSyncActive = false;
        m_visibleSyncPullComplete = false;
        m_pendingVisibleSyncSummary.clear();
        m_lastResultSummary = QStringLiteral(
            "Sync could not finish. Your Colosseum data was kept.");
        setStatus(QStringLiteral("syncFailed"));
        emit stateChanged();
        return;
    }
    if (succeeded && !m_state.pendingIntents.isEmpty()) {
        m_visibleSyncPullComplete = true;
        m_pendingVisibleSyncSummary = summary;
        emit stateChanged();
        return;
    }
    m_visibleSyncActive = false;
    m_visibleSyncPullComplete = false;
    m_pendingVisibleSyncSummary.clear();
    m_lastResultSummary = summary;
    if (succeeded) {
        setStatus(QStringLiteral("synced"));
        finishRun();
    } else {
        setStatus(QStringLiteral("syncFailed"));
        emit stateChanged();
    }
}

void StremioSync::completeVisibleSyncIfDrained() {
    if (!m_visibleSyncActive || !m_visibleSyncPullComplete
        || !m_state.pendingIntents.isEmpty())
        return;
    m_visibleSyncActive = false;
    m_visibleSyncPullComplete = false;
    m_lastResultSummary = m_pendingVisibleSyncSummary;
    m_pendingVisibleSyncSummary.clear();
    setStatus(QStringLiteral("synced"));
    finishRun();
}

void StremioSync::retireProvisionalCredential() {
    if (!m_provisionalCredential.has_value())
        return;

    const ProvisionalCredential provisional = *m_provisionalCredential;
    m_provisionalCredential.reset();
    if (m_options.clearCredential)
        m_options.clearCredential(provisional.binding.profileId);
    if (!bindingCurrent(provisional.binding))
        return;

    m_hasUsableCredential = false;
    m_dispatchAllowed = false;
    m_retryTimer.stop();
    m_state.reconnectRequired = true;
    persist();
    updateConnectionStatus();
}

void StremioSync::setMarkerLinked(bool linked) {
    if (m_markerLinked == linked)
        return;
    m_markerLinked = linked;
    if (!m_profileId.isEmpty()) {
        updateConnectionStatus();
        if (linked) {
            const ProfileBinding binding{m_profileId, m_bindingGeneration};
            QTimer::singleShot(0, this, [this, binding] {
                if (m_markerLinked && bindingCurrent(binding))
                    retryPendingNow();
            });
        }
    }
}

void StremioSync::setCredentialCallbacks(
    std::function<bool(const QString &, const QString &, const QByteArray &)> save,
    std::function<bool(const QString &)> clear,
    std::function<std::optional<QByteArray>(const QString &, const QString &)> load) {
    m_options.saveCredential = std::move(save);
    m_options.clearCredential = std::move(clear);
    m_options.loadCredential = std::move(load);
}

bool StremioSync::requestEpisodeMetadata(
    const QString &seriesId,
    EpisodeMetadataCompletion completion) {
    const QString normalizedSeriesId = seriesId.trimmed();
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    if (!bindingCurrent(binding) || normalizedSeriesId.isEmpty()
        || normalizedSeriesId.size() > 512 || m_state.accountId.isEmpty()) {
        if (completion)
            completion(false, {});
        return false;
    }
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    m_episodeMetadataRequests.insert(
        requestId,
        EpisodeMetadataRequest{
            binding, m_state.accountId, normalizedSeriesId, std::move(completion), false});
    QTimer::singleShot(10 * 1000, this, [this, requestId, binding] {
        auto request = m_episodeMetadataRequests.find(requestId);
        if (request == m_episodeMetadataRequests.end()
            || request->binding.profileId != binding.profileId
            || request->binding.generation != binding.generation) {
            return;
        }
        EpisodeMetadataCompletion completion = std::move(request->completion);
        m_episodeMetadataRequests.erase(request);
        if (completion)
            completion(false, {});
    });
    dispatchEpisodeMetadataRequest(requestId);
    return true;
}

void StremioSync::setEpisodeMetadataBridgeReady(bool ready) {
    m_episodeMetadataBridgeReady = ready;
    if (!ready)
        return;
    const QStringList requestIds = m_episodeMetadataRequests.keys();
    for (const QString &requestId : requestIds)
        dispatchEpisodeMetadataRequest(requestId);
}

void StremioSync::dispatchEpisodeMetadataRequest(const QString &requestId) {
    auto request = m_episodeMetadataRequests.find(requestId);
    if (!m_episodeMetadataBridgeReady || request == m_episodeMetadataRequests.end()
        || request->emitted || !bindingCurrent(request->binding)
        || request->accountId != m_state.accountId) {
        return;
    }
    request->emitted = true;
    emit episodeMetadataRequested(requestId, request->expectedRootId);
}

bool StremioSync::submitEpisodeMetadata(
    const QString &requestId,
    const QString &metadataRootId,
    const QVariantList &episodes) {
    const auto request = m_episodeMetadataRequests.constFind(requestId);
    if (request == m_episodeMetadataRequests.cend()
        || !bindingCurrent(request->binding)
        || request->accountId != m_state.accountId
        || metadataRootId != request->expectedRootId
        || episodes.isEmpty() || episodes.size() > kMaximumEpisodeMetadataRows) {
        return false;
    }
    QList<StremioEpisodeIdentity> resolved;
    resolved.reserve(episodes.size());
    QSet<QString> ids;
    QSet<QString> coordinates;
    for (const QVariant &value : episodes) {
        const QVariantMap episode = value.toMap();
        const QString id = episode.value(QStringLiteral("id")).toString();
        bool seasonOk = false;
        bool numberOk = false;
        const double seasonValue = episode.value(QStringLiteral("season")).toDouble(&seasonOk);
        const double numberValue = episode.value(QStringLiteral("episode")).toDouble(&numberOk);
        if (episode.size() != 3 || id.isEmpty() || id != id.trimmed() || id.size() > 512
            || id.contains(QChar::Null) || id.contains(QChar::ReplacementCharacter)
            || !seasonOk || !numberOk || !std::isfinite(seasonValue) || !std::isfinite(numberValue)
            || std::floor(seasonValue) != seasonValue || std::floor(numberValue) != numberValue
            || seasonValue < 0 || numberValue < 0 || seasonValue > 10000 || numberValue > 100000) {
            return false;
        }
        const StremioEpisodeIdentity identity{
            id, static_cast<int>(seasonValue), static_cast<int>(numberValue)};
        const QString coordinate = QString::number(identity.season)
            + QLatin1Char(':') + QString::number(identity.episode);
        if (ids.contains(id) || coordinates.contains(coordinate)
            || !StremioCodec::episodeBelongsToSeries(metadataRootId, identity)) {
            return false;
        }
        ids.insert(id);
        coordinates.insert(coordinate);
        resolved.append(identity);
    }
    EpisodeMetadataCompletion completion = std::move(request->completion);
    m_episodeMetadataRequests.remove(requestId);
    if (completion)
        completion(true, std::move(resolved));
    return true;
}

void StremioSync::cancelEpisodeMetadataRequests() {
    const QList<EpisodeMetadataRequest> requests = m_episodeMetadataRequests.values();
    m_episodeMetadataRequests.clear();
    for (const EpisodeMetadataRequest &request : requests) {
        if (request.completion)
            request.completion(false, {});
    }
}

bool StremioSync::queueIntent(
    const QString &kind,
    const QJsonObject &desired,
    QString *operationId) {
    return queueIntentInternal(kind, desired, operationId, false);
}

bool StremioSync::queueIntentInternal(
    const QString &kind,
    const QJsonObject &desired,
    QString *operationId,
    bool localReceiptDurable) {
    if (m_profileId.isEmpty() || kind.trimmed().isEmpty() || desired.isEmpty())
        return false;
    if (kind == QLatin1String("series_watched")) {
        QList<StremioEpisodeIdentity> videos;
        QSet<QString> watched;
        // Only a resolved complete metadata map may reach the durable private
        // intent.  A partial local episode subset can set bits, never invent
        // the order or clear an unknown remote episode.
        if (!decodeSeriesWatchedDesired(desired, &videos, &watched))
            return false;
    }
    StremioPendingIntent intent;
    intent.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    intent.kind = kind.trimmed();
    intent.desired = desired;
    intent.localReceiptDurable = localReceiptDurable;
    if (intent.kind == QStringLiteral("progress")) {
        const QString mediaId = desired.value(QStringLiteral("id")).toString();
        if (!mediaId.isEmpty()) {
            for (StremioPendingIntent &pending : m_state.pendingIntents) {
                if (pending.kind != intent.kind
                    || pending.desired.value(QStringLiteral("id")).toString() != mediaId
                    || pending.remoteAcknowledged || pending.localReceiptDurable
                    || pending.attempts != 0 || m_inFlightOperations.contains(pending.operationId)) {
                    continue;
                }
                pending.desired = desired;
                if (operationId)
                    *operationId = pending.operationId;
                const ProfileBinding binding{m_profileId, m_bindingGeneration};
                persist([this, operation = pending.operationId, binding](bool committed) {
                    if (committed)
                        dispatchIntent(operation, binding);
                });
                emit stateChanged();
                return true;
            }
        }
    }
    m_state.pendingIntents.append(intent);
    if (operationId)
        *operationId = intent.operationId;
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    persist([this, operation = intent.operationId, binding](bool committed) {
        if (committed)
            dispatchIntent(operation, binding);
    });
    emit stateChanged();
    return true;
}

QString StremioSync::baselineKeyForIntent(
    const QString &kind,
    const QJsonObject &desired) {
    const QString id = desired.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty())
        return {};
    // Membership is one two-valued provider fact.  Keeping add and remove
    // under separate baselines would let an acknowledged old add suppress a
    // later explicit re-add after a completed removal.
    if (kind == QLatin1String("library_add") || kind == QLatin1String("library_remove")) {
        const QString type = desired.value(QStringLiteral("type")).toString();
        if (type != QLatin1String("movie") && type != QLatin1String("series"))
            return {};
        return QStringLiteral("library_membership:") + type + QLatin1Char(':') + id;
    }
    return kind + QLatin1Char(':') + id;
}

bool StremioSync::queueReconciledIntent(
    const QString &kind,
    const QJsonObject &desired) {
    const QString baselineKey = baselineKeyForIntent(kind, desired);
    if (baselineKey.isEmpty())
        return false;
    // A durable provider removal deliberately leaves Collection untouched.
    // Its inverse difference is not a local add command, so a later owner
    // reread (including after restart) must not put the row back remotely.
    if (kind == QLatin1String("library_add")
        && suppressesInferredLibraryAddition(
            desired.value(QStringLiteral("id")).toString(),
            desired.value(QStringLiteral("type")).toString())) {
        return true;
    }
    // A current canonical Theatre membership can only be a fresh explicit
    // re-add after this profile chose local-only removal: passive Stremio
    // imports are suppressed before they reach Collection. Retire exactly
    // that private difference while journaling the new add; never treat a
    // normal Collection tombstone as a provider delete.
    const bool clearedSuppression = kind == QLatin1String("library_add")
        && clearLocalOnlyMembershipSuppression(
            desired.value(QStringLiteral("id")).toString(),
            desired.value(QStringLiteral("type")).toString());
    if (m_state.acknowledgedBaselines.value(baselineKey).toObject() == desired)
    {
        if (clearedSuppression)
            persist();
        return true;
    }
    for (const StremioPendingIntent &pending : std::as_const(m_state.pendingIntents)) {
        if (pending.kind == kind && pending.desired == desired) {
            if (clearedSuppression)
                persist();
            return true;
        }
    }
    return queueIntentInternal(kind, desired, nullptr, true);
}

bool StremioSync::reconcileTheatreState(
    const QVariantList &theatreCollection,
    const QVariantList &progressEntries,
    const QHash<QString, int> &watchedMarks,
    const QHash<QString, qint64> &watchedActionAt) {
    if (m_profileId.isEmpty())
        return false;

    QHash<QString, QString> typesById;
    for (const QVariant &value : theatreCollection) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("world")).toString() != QLatin1String("theatre"))
            continue;
        const QString id = entry.value(QStringLiteral("id")).toString().trimmed();
        const QString type = entry.value(QStringLiteral("type")).toString();
        if (id.isEmpty() || id.size() > 512
            || (type != QLatin1String("movie") && type != QLatin1String("series"))) {
            continue;
        }
        typesById.insert(id, type);
        if (!queueReconciledIntent(
                QStringLiteral("library_add"),
                QJsonObject{{QStringLiteral("id"), id},
                            {QStringLiteral("type"), type}})) {
            return false;
        }
    }

    for (const QVariant &value : progressEntries) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("kind")).toString() != QLatin1String("video"))
            continue;
        const QString id = entry.value(QStringLiteral("id")).toString().trimmed();
        // Continue records describe the exact video being played.  Collection
        // membership is intentionally independent: an episode writes against
        // its provider root, and an uncollected playback may still reconcile
        // an existing provider row without inventing a library add.
        const QString libraryId = entry.value(QStringLiteral("libraryId")).toString().trimmed();
        const QString providerLibraryId = libraryId.isEmpty() ? id : libraryId;
        if (id.isEmpty() || providerLibraryId.isEmpty() || id.size() > 512
            || providerLibraryId.size() > 512) {
            continue;
        }
        const QVariantMap resume = entry.value(QStringLiteral("resume")).toMap();
        bool positionOk = false;
        bool durationOk = false;
        const double positionSeconds = resume.value(QStringLiteral("position")).toDouble(&positionOk);
        const double durationSeconds = entry.value(QStringLiteral("duration")).toDouble(&durationOk);
        const qint64 updatedAtMs = entry.value(QStringLiteral("updatedAt")).toLongLong();
        if (!positionOk || !durationOk || !std::isfinite(positionSeconds) || !std::isfinite(durationSeconds)
            || positionSeconds < 0.0 || durationSeconds <= 0.0 || updatedAtMs <= 0) {
            continue;
        }
        if (!queueReconciledIntent(
                QStringLiteral("progress"),
                QJsonObject{{QStringLiteral("id"), id},
                            {QStringLiteral("libraryId"), providerLibraryId},
                            {QStringLiteral("positionSeconds"), positionSeconds},
                            {QStringLiteral("durationSeconds"), durationSeconds},
                            {QStringLiteral("updatedAt"), QString::number(updatedAtMs)}})) {
            return false;
        }
    }

    // A series-root mark has no Stremio bitfield meaning. Only exact
    // completed episodes may be exported, and only after QML's existing
    // Theatre reader has supplied a complete ordered map for this opaque
    // series identity. A too-large local set is deliberately left pending for
    // a later bounded reconciliation rather than truncating it into data loss.
    QHash<QString, QSet<QString>> completedEpisodesBySeries;
    QSet<QString> oversizedEpisodeSeries;
    for (const QVariant &value : progressEntries) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("kind")).toString() != QLatin1String("video"))
            continue;
        bool progressOk = false;
        const double progress = entry.value(QStringLiteral("progress")).toDouble(&progressOk);
        const QString episodeId = entry.value(QStringLiteral("id")).toString().trimmed();
        if (!progressOk || !std::isfinite(progress) || progress < 0.90 || episodeId.isEmpty())
            continue;
        for (auto type = typesById.constBegin(); type != typesById.constEnd(); ++type) {
            if (type.value() != QLatin1String("series"))
                continue;
            const StremioEpisodeIdentity identity{episodeId, 0, 0};
            if (!StremioCodec::episodeBelongsToSeries(type.key(), identity)
                || oversizedEpisodeSeries.contains(type.key())) {
                continue;
            }
            QSet<QString> &completed = completedEpisodesBySeries[type.key()];
            if (!completed.contains(episodeId)
                && completed.size() >= kMaximumEpisodeMetadataRows) {
                completedEpisodesBySeries.remove(type.key());
                oversizedEpisodeSeries.insert(type.key());
                continue;
            }
            completed.insert(episodeId);
        }
    }
    for (auto completed = completedEpisodesBySeries.constBegin();
         completed != completedEpisodesBySeries.constEnd(); ++completed) {
        if (completed.value().isEmpty() || oversizedEpisodeSeries.contains(completed.key()))
            continue;
        const QString seriesId = completed.key();
        const QSet<QString> localEpisodeIds = completed.value();
        requestEpisodeMetadata(seriesId,
            [this, seriesId, localEpisodeIds](bool resolved,
                                               QList<StremioEpisodeIdentity> videos) {
                if (!resolved || videos.isEmpty()
                    || videos.size() > kMaximumEpisodeMetadataRows) {
                    return;
                }
                QSet<QString> knownIds;
                QJsonArray orderedEpisodes;
                for (const StremioEpisodeIdentity &video : videos) {
                    if (knownIds.contains(video.videoId)
                        || !StremioCodec::episodeBelongsToSeries(seriesId, video)) {
                        return;
                    }
                    knownIds.insert(video.videoId);
                    orderedEpisodes.append(QJsonObject{
                        {QStringLiteral("id"), video.videoId},
                        {QStringLiteral("season"), video.season},
                        {QStringLiteral("episode"), video.episode}});
                }
                if (!knownIds.contains(localEpisodeIds))
                    return;
                QStringList sortedLocalIds = localEpisodeIds.values();
                sortedLocalIds.sort();
                QJsonArray localIds;
                for (const QString &id : sortedLocalIds)
                    localIds.append(id);
                queueReconciledIntent(QStringLiteral("series_watched"), QJsonObject{
                    {QStringLiteral("id"), seriesId},
                    {QStringLiteral("type"), QStringLiteral("series")},
                    {QStringLiteral("episodeIds"), localIds},
                    {QStringLiteral("episodes"), orderedEpisodes}});
            });
    }

    for (auto it = watchedMarks.constBegin(); it != watchedMarks.constEnd(); ++it) {
        const QString id = it.key().trimmed();
        // The Stremio movie watched bit is the only outbound watched shape
        // available without an exact addon episode ordering. Series marks are
        // intentionally left local rather than guessed into a bitfield.
        if (typesById.value(id) != QLatin1String("movie")
            || (it.value() != -1 && it.value() != 1)) {
            continue;
        }
        const qint64 actionAtMs = watchedActionAt.value(id, 0);
        if (actionAtMs <= 0)
            continue;
        if (!queueReconciledIntent(
                QStringLiteral("watched"),
                QJsonObject{{QStringLiteral("id"), id},
                            {QStringLiteral("type"), QStringLiteral("movie")},
                            {QStringLiteral("watched"), it.value() > 0},
                            {QStringLiteral("updatedAt"), QString::number(actionAtMs)}})) {
            return false;
        }
    }
    return true;
}

bool StremioSync::pullLibraryItems(
    std::function<void(bool, QList<StremioLibraryItem>)> completion) {
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    if (!bindingCurrent(binding) || !m_markerLinked || !m_hasUsableCredential
        || !m_dispatchAllowed || !endpointAllowed() || !m_options.loadCredential
        || m_datastoreReply) {
        if (completion)
            completion(false, {});
        return false;
    }
    const auto credential = m_options.loadCredential(m_profileId, m_state.accountId);
    const StremioDatastoreRequest meta = credential.has_value()
        ? StremioCodec::datastoreMetaRequest(*credential)
        : StremioDatastoreRequest{};
    if (meta.method.isEmpty()) {
        if (completion)
            completion(false, {});
        return false;
    }
    const auto pull = std::make_shared<LibraryPull>();
    pull->binding = binding;
    pull->completion = std::move(completion);
    postDatastoreRequest(meta, binding,
        [this, pull](bool succeeded, bool, QJsonValue result) {
        if (!succeeded || !bindingCurrent(pull->binding)) {
            finishLibraryPull(pull, false);
            return;
        }
        pull->batches = StremioCodec::boundedLibraryItemBatches(
            StremioCodec::decodeLibraryItemMeta(result, nullptr, kMaximumLibraryRows));
        fetchNextLibraryBatch(pull);
    });
    return true;
}

bool StremioSync::pullAddonCollection(
    std::function<void(bool, QJsonArray)> completion) {
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    if (!bindingCurrent(binding) || !m_markerLinked || !m_hasUsableCredential
        || !m_dispatchAllowed || !endpointAllowed() || !m_options.loadCredential
        || m_datastoreReply) {
        if (completion)
            completion(false, {});
        return false;
    }
    const auto credential = m_options.loadCredential(m_profileId, m_state.accountId);
    const StremioDatastoreRequest request = credential.has_value()
        ? StremioCodec::addonCollectionGetRequest(*credential)
        : StremioDatastoreRequest{};
    if (request.method.isEmpty()) {
        if (completion)
            completion(false, {});
        return false;
    }
    postDatastoreRequest(request, binding,
        [this, binding, completion = std::move(completion)](
            bool succeeded,
            bool,
            QJsonValue result) mutable {
        if (!succeeded || !bindingCurrent(binding)) {
            if (completion)
                completion(false, {});
            return;
        }
        const StremioAddonCollectionDecode decoded =
            StremioCodec::decodeAddonCollection(result);
        if (completion)
            completion(decoded.containerValid,
                       decoded.containerValid ? decoded.addons : QJsonArray{});
    });
    return true;
}

bool StremioSync::reconcileAddonCollection(
    const QJsonArray &localAddons,
    std::function<void(bool, QJsonArray)> completion) {
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    const StremioAddonCollectionDecode decodedLocal =
        StremioCodec::decodeAddonCollection(
            QJsonObject{{QStringLiteral("addons"), localAddons}});
    if (!bindingCurrent(binding) || !m_markerLinked || !m_hasUsableCredential
        || !m_dispatchAllowed || !endpointAllowed() || !m_options.loadCredential
        || m_addonCollectionReconcileActive
        || decodedLocal.addons.size() != localAddons.size()) {
        if (completion)
            completion(false, {});
        return false;
    }

    const auto reconcile = std::make_shared<AddonCollectionReconcile>();
    reconcile->binding = binding;
    reconcile->local = decodedLocal.addons;
    reconcile->completion = std::move(completion);
    m_addonCollectionReconcileActive = true;
    // The local owner has already committed its rows. Persist the exact,
    // bounded desired owner snapshot before any provider read/write so an app
    // exit cannot turn a deliberate addon action into a guessed deletion.
    m_state.acknowledgedBaselines.insert(
        QStringLiteral("addonCollectionPending"),
        QJsonObject{{QStringLiteral("local"), reconcile->local}});
    persist([this, reconcile](bool committed) {
        if (!committed || !bindingCurrent(reconcile->binding)) {
            finishAddonCollectionReconcile(reconcile, false);
            return;
        }
        fetchAddonCollectionForReconcile(reconcile);
    });
    return true;
}

bool StremioSync::acknowledgeAddonCollectionOwner(
    const QJsonArray &localAddons,
    std::function<void(bool)> completion) {
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    const StremioAddonCollectionDecode local = StremioCodec::decodeAddonCollection(
        QJsonObject{{QStringLiteral("addons"), localAddons}});
    const QJsonObject prior = m_state.acknowledgedBaselines.value(
        QStringLiteral("addonCollection")).toObject();
    const QJsonValue remoteValue = prior.value(QStringLiteral("remote"));
    if (!bindingCurrent(binding) || !local.containerValid
        || local.addons.size() != localAddons.size() || !remoteValue.isArray()) {
        if (completion)
            completion(false);
        return false;
    }

    QSet<QString> remoteIdentities;
    for (const QJsonValue &value : remoteValue.toArray())
        remoteIdentities.insert(addonTransportIdentity(value));
    for (const QJsonValue &value : local.addons) {
        const QString identity = addonTransportIdentity(value);
        if (identity.isEmpty() || !remoteIdentities.contains(identity)) {
            if (completion)
                completion(false);
            return false;
        }
    }

    QJsonObject baseline = prior;
    baseline.insert(QStringLiteral("local"), local.addons);
    m_state.acknowledgedBaselines.insert(QStringLiteral("addonCollection"), baseline);
    persist([this, binding, completion = std::move(completion)](bool committed) mutable {
        if (completion)
            completion(committed && bindingCurrent(binding));
    });
    return true;
}

void StremioSync::fetchAddonCollectionForReconcile(
    const std::shared_ptr<AddonCollectionReconcile> &reconcile) {
    if (!reconcile || !bindingCurrent(reconcile->binding) || !m_options.loadCredential) {
        finishAddonCollectionReconcile(reconcile, false);
        return;
    }
    const auto credential = m_options.loadCredential(
        reconcile->binding.profileId, m_state.accountId);
    const StremioDatastoreRequest request = credential.has_value()
        ? StremioCodec::addonCollectionGetRequest(*credential)
        : StremioDatastoreRequest{};
    if (request.method.isEmpty()) {
        finishAddonCollectionReconcile(reconcile, false);
        return;
    }
    postDatastoreRequest(request, reconcile->binding,
        [this, reconcile](bool succeeded, bool, QJsonValue result) {
        if (!succeeded || !bindingCurrent(reconcile->binding)) {
            finishAddonCollectionReconcile(reconcile, false);
            return;
        }
        const StremioAddonCollectionDecode remote =
            StremioCodec::decodeAddonCollection(result);
        if (!remote.containerValid) {
            finishAddonCollectionReconcile(reconcile, false);
            return;
        }
        const QJsonObject baseline = m_state.acknowledgedBaselines.value(
            QStringLiteral("addonCollection")).toObject();
        const QJsonValue baselineRemoteValue = baseline.value(QStringLiteral("remote"));
        const QJsonValue baselineLocalValue = baseline.value(QStringLiteral("local"));
        const bool hasBaseline = baselineRemoteValue.isArray()
            && baselineLocalValue.isArray();
        reconcile->desired = hasBaseline
            ? addonCollectionDeltaReconcile(remote.addons,
                                            baselineRemoteValue.toArray(),
                                            baselineLocalValue.toArray(),
                                            reconcile->local)
            : deduplicatedAddonUnion(remote.addons, reconcile->local);
        if (reconcile->desired == remote.addons) {
            finishAddonCollectionReconcile(reconcile, true, remote.addons);
            return;
        }
        writeAddonCollectionForReconcile(reconcile);
    });
}

void StremioSync::writeAddonCollectionForReconcile(
    const std::shared_ptr<AddonCollectionReconcile> &reconcile) {
    if (!reconcile || !bindingCurrent(reconcile->binding) || !m_options.loadCredential) {
        finishAddonCollectionReconcile(reconcile, false);
        return;
    }
    const auto credential = m_options.loadCredential(
        reconcile->binding.profileId, m_state.accountId);
    const StremioDatastoreRequest request = credential.has_value()
        ? StremioCodec::addonCollectionSetRequest(*credential, reconcile->desired)
        : StremioDatastoreRequest{};
    if (request.method.isEmpty()) {
        finishAddonCollectionReconcile(reconcile, false);
        return;
    }
    postDatastoreRequest(request, reconcile->binding,
        [this, reconcile](bool succeeded, bool, QJsonValue result) {
        if (!succeeded || !addonCollectionSetAccepted(result)
            || !bindingCurrent(reconcile->binding)) {
            finishAddonCollectionReconcile(reconcile, false);
            return;
        }
        verifyAddonCollectionForReconcile(reconcile);
    });
}

void StremioSync::verifyAddonCollectionForReconcile(
    const std::shared_ptr<AddonCollectionReconcile> &reconcile) {
    if (!reconcile || !bindingCurrent(reconcile->binding) || !m_options.loadCredential) {
        finishAddonCollectionReconcile(reconcile, false);
        return;
    }
    const auto credential = m_options.loadCredential(
        reconcile->binding.profileId, m_state.accountId);
    const StremioDatastoreRequest request = credential.has_value()
        ? StremioCodec::addonCollectionGetRequest(*credential)
        : StremioDatastoreRequest{};
    if (request.method.isEmpty()) {
        finishAddonCollectionReconcile(reconcile, false);
        return;
    }
    postDatastoreRequest(request, reconcile->binding,
        [this, reconcile](bool succeeded, bool, QJsonValue result) {
        if (!succeeded || !bindingCurrent(reconcile->binding)) {
            finishAddonCollectionReconcile(reconcile, false);
            return;
        }
        const StremioAddonCollectionDecode readBack =
            StremioCodec::decodeAddonCollection(result);
        if (!readBack.containerValid) {
            finishAddonCollectionReconcile(reconcile, false);
            return;
        }
        if (readBack.addons != reconcile->desired) {
            ++reconcile->attempts;
            if (reconcile->attempts >= kMaximumAddonCollectionAttempts) {
                finishAddonCollectionReconcile(reconcile, false);
                return;
            }
            // Stremio provides no compare-and-swap for whole collections.
            // Treat a mismatched readback as a concurrent mutation and rebase
            // on a new provider GET; never retry the stale body directly.
            fetchAddonCollectionForReconcile(reconcile);
            return;
        }
        finishAddonCollectionReconcile(reconcile, true, readBack.addons);
    });
}

void StremioSync::finishAddonCollectionReconcile(
    const std::shared_ptr<AddonCollectionReconcile> &reconcile,
    bool succeeded,
    const QJsonArray &settled) {
    if (!reconcile)
        return;
    const auto complete = [reconcile](bool committed, const QJsonArray &addons) {
        if (!reconcile->completion)
            return;
        const auto completion = std::move(reconcile->completion);
        completion(committed, committed ? addons : QJsonArray{});
    };
    if (!succeeded || !bindingCurrent(reconcile->binding)) {
        if (bindingCurrent(reconcile->binding))
            m_addonCollectionReconcileActive = false;
        complete(false, {});
        return;
    }
    m_state.acknowledgedBaselines.insert(
        QStringLiteral("addonCollection"),
        QJsonObject{{QStringLiteral("remote"), settled},
                    {QStringLiteral("local"), reconcile->local}});
    m_state.acknowledgedBaselines.remove(QStringLiteral("addonCollectionPending"));
    m_state.lastSuccessAtMs = m_options.clock();
    persist([this, reconcile, settled, complete](bool committed) mutable {
        if (bindingCurrent(reconcile->binding))
            m_addonCollectionReconcileActive = false;
        complete(committed && bindingCurrent(reconcile->binding), settled);
    });
}

void StremioSync::fetchNextLibraryBatch(const std::shared_ptr<LibraryPull> &pull) {
    if (!bindingCurrent(pull->binding)) {
        finishLibraryPull(pull, false);
        return;
    }
    if (pull->nextBatch >= pull->batches.size()) {
        finishLibraryPull(pull, true);
        return;
    }
    if (!m_options.loadCredential) {
        finishLibraryPull(pull, false);
        return;
    }
    const auto credential = m_options.loadCredential(pull->binding.profileId, m_state.accountId);
    if (!credential.has_value() || credential->isEmpty()) {
        finishLibraryPull(pull, false);
        return;
    }
    const QList<StremioDatastoreRequest> requests = StremioCodec::datastoreGetRequests(
        *credential, pull->batches.at(pull->nextBatch), 64);
    if (requests.size() != 1) {
        finishLibraryPull(pull, false);
        return;
    }
    ++pull->nextBatch;
    postDatastoreRequest(requests.first(), pull->binding,
        [this, pull](bool succeeded, bool, QJsonValue result) {
        if (!succeeded || !bindingCurrent(pull->binding)) {
            finishLibraryPull(pull, false);
            return;
        }
        const int remaining = kMaximumLibraryRows - pull->items.size();
        const StremioLibraryItemDecode decoded =
            StremioCodec::decodeLibraryItems(result, qMax(remaining, 0));
        for (const StremioLibraryItem &item : decoded.items)
            pull->items.append(item);
        fetchNextLibraryBatch(pull);
    });
}

void StremioSync::finishLibraryPull(
    const std::shared_ptr<LibraryPull> &pull,
    bool succeeded) {
    if (!pull || !pull->completion)
        return;
    const auto complete = [pull](bool committed) {
        if (!pull->completion)
            return;
        const auto completion = std::move(pull->completion);
        completion(committed, committed ? pull->items : QList<StremioLibraryItem>{});
    };
    if (!succeeded || !bindingCurrent(pull->binding)) {
        complete(false);
        return;
    }
    // The first pull is union-only: no absence is interpreted as a delete.
    // Its baseline is committed by AccountRuntime only after the decoded rows
    // have crossed durable canonical owners and their Neon checkpoint.
    complete(true);
}

bool StremioSync::completeFirstMerge(std::function<void(bool)> completion) {
    if (m_profileId.isEmpty()) {
        if (completion)
            completion(false);
        return false;
    }
    if (m_state.firstMergeComplete) {
        if (completion)
            completion(true);
        return true;
    }
    m_state.firstMergeComplete = true;
    persist(std::move(completion));
    emit stateChanged();
    return true;
}

bool StremioSync::beginProviderImport(
    const StremioLibraryItem &item,
    const QJsonObject &projection,
    std::function<void(bool, const QString &)> durableReceipt) {
    const QString normalizedId = item.id.trimmed();
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    if (!bindingCurrent(binding) || normalizedId.isEmpty()
        || (item.type != QLatin1String("movie") && item.type != QLatin1String("series"))
        || projection.isEmpty()
        || QJsonDocument(projection).toJson(QJsonDocument::Compact).size()
            > kMaximumProviderRedoProjectionBytes) {
        if (durableReceipt)
            durableReceipt(false, {});
        return false;
    }
    const QString receipt = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    m_state.importRedoReceipts.append(QJsonObject{
        {QStringLiteral("operationId"), receipt},
        {QStringLiteral("profileId"), binding.profileId},
        {QStringLiteral("accountId"), m_state.accountId},
        {QStringLiteral("bindingGeneration"), QString::number(binding.generation)},
        {QStringLiteral("id"), normalizedId},
        {QStringLiteral("type"), item.type},
        {QStringLiteral("libraryMember"), item.libraryMember},
        {QStringLiteral("removed"), item.removed},
        {QStringLiteral("projection"), projection}});
    persist([this, binding, receipt, durableReceipt = std::move(durableReceipt)](bool committed) {
        if (durableReceipt)
            durableReceipt(committed && bindingCurrent(binding),
                           committed && bindingCurrent(binding) ? receipt : QString());
    });
    emit stateChanged();
    return true;
}

QList<StremioProviderImportRedo> StremioSync::pendingProviderImports() const {
    QList<StremioProviderImportRedo> pending;
    if (m_profileId.isEmpty())
        return pending;
    for (const QJsonValue &value : m_state.importRedoReceipts) {
        const auto redo = providerImportRedoFromJson(value);
        if (!redo.has_value()
            || redo->profileId != m_profileId
            || redo->accountId != m_state.accountId) {
            continue;
        }
        pending.append(*redo);
    }
    return pending;
}

bool StremioSync::settleProviderImport(
    const QString &receipt,
    std::function<void(bool)> completion) {
    const QString normalizedReceipt = receipt.trimmed();
    if (normalizedReceipt.isEmpty() || m_profileId.isEmpty()) {
        if (completion)
            completion(false);
        return false;
    }
    for (qsizetype index = 0; index < m_state.importRedoReceipts.size(); ++index) {
        const QJsonValue value = m_state.importRedoReceipts.at(index);
        if (!value.isObject()
            || value.toObject().value(QStringLiteral("operationId")).toString() != normalizedReceipt) {
            continue;
        }
        m_state.importRedoReceipts.removeAt(index);
        persist(std::move(completion));
        emit stateChanged();
        return true;
    }
    if (completion)
        completion(false);
    return false;
}

bool StremioSync::queueExplicitLibraryRemoval(
    const QString &id,
    const QString &type,
    std::function<void(bool)> journalReceipt,
    QString *operationId) {
    const QString normalizedId = id.trimmed();
    if (m_profileId.isEmpty() || normalizedId.isEmpty()
        || (type != QLatin1String("movie") && type != QLatin1String("series"))) {
        if (journalReceipt)
            journalReceipt(false);
        return false;
    }

    StremioPendingIntent intent;
    intent.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    intent.kind = QStringLiteral("library_remove");
    intent.desired = QJsonObject{
        {QStringLiteral("id"), normalizedId},
        {QStringLiteral("type"), type},
        {QStringLiteral("removed"), true}};
    // Until Stremio acknowledges the change, a passive provider pull still
    // describes membership. Keep the intentional local/remote difference
    // beside the pending operation so it cannot silently re-add the item.
    upsertMembershipDifference(normalizedId, type, false, true, true);
    m_state.pendingIntents.append(intent);
    if (operationId)
        *operationId = intent.operationId;
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    persist([this, operation = intent.operationId, binding,
             journalReceipt = std::move(journalReceipt)](bool committed) mutable {
        if (journalReceipt)
            journalReceipt(committed);
        if (committed)
            dispatchIntent(operation, binding);
    });
    emit stateChanged();
    return true;
}

bool StremioSync::recordLocalOnlyLibraryRemoval(
    const QString &id,
    const QString &type,
    std::function<void(bool)> journalReceipt) {
    const QString normalizedId = id.trimmed();
    if (m_profileId.isEmpty() || normalizedId.isEmpty()
        || (type != QLatin1String("movie") && type != QLatin1String("series"))) {
        if (journalReceipt)
            journalReceipt(false);
        return false;
    }
    upsertMembershipDifference(normalizedId, type, false, true, false);
    persist(std::move(journalReceipt));
    emit stateChanged();
    return true;
}

bool StremioSync::recordRemoteLibraryRemoval(
    const QString &id,
    const QString &type,
    std::function<void(bool)> journalReceipt) {
    const QString normalizedId = id.trimmed();
    if (m_profileId.isEmpty() || normalizedId.isEmpty()
        || (type != QLatin1String("movie") && type != QLatin1String("series"))) {
        if (journalReceipt)
            journalReceipt(false);
        return false;
    }
    upsertMembershipDifference(normalizedId, type, true, false, false);
    persist(std::move(journalReceipt));
    emit stateChanged();
    return true;
}

bool StremioSync::suppressesRemoteLibraryMembership(
    const QString &id,
    const QString &type) const {
    const QString normalizedId = id.trimmed();
    for (const QJsonValue &value : m_state.intentionalMembershipDifferences) {
        if (!value.isObject())
            continue;
        const QJsonObject difference = value.toObject();
        if (difference.value(QStringLiteral("id")).toString() != normalizedId
            || difference.value(QStringLiteral("type")).toString() != type) {
            continue;
        }
        // `false/true/*` means the local removal is durable while the remote
        // member is still present. This includes an explicit dual removal
        // awaiting its provider acknowledgement; a passive pull in that
        // window must not recreate Collection. Once the remote receipt moves
        // it to false/false, a genuine later provider re-add is eligible.
        return !difference.value(QStringLiteral("localPresent")).toBool()
            && difference.value(QStringLiteral("remotePresent")).toBool();
    }
    return false;
}

bool StremioSync::acknowledgeLocalReceipt(const QString &operationId) {
    StremioPendingIntent *intent = intentFor(operationId);
    if (!intent)
        return false;
    intent->localReceiptDurable = true;
    persist([this, operationId](bool committed) {
        if (committed)
            removeSatisfiedIntent(operationId);
    });
    emit stateChanged();
    return true;
}

void StremioSync::retryPendingNow() {
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    if (!bindingCurrent(binding))
        return;
    const qint64 now = m_options.clock();
    qint64 nextRetryAtMs = 0;
    for (const StremioPendingIntent &intent : std::as_const(m_state.pendingIntents)) {
        if (intent.remoteAcknowledged || intent.attempts >= kMaximumRetries)
            continue;
        if (intent.retryAtMs <= now) {
            dispatchIntent(intent.operationId, binding);
            continue;
        }
        if (m_markerLinked
            && (nextRetryAtMs == 0 || intent.retryAtMs < nextRetryAtMs)) {
            nextRetryAtMs = intent.retryAtMs;
        }
    }
    // A restart restores the durable deadline but not QTimer's process-local
    // schedule.  Re-arm only marker-linked work and bound each sleep to the
    // existing maximum retry cadence so malformed/far-future timestamps can
    // neither dispatch unlinked work nor overflow the timer interval.
    if (m_markerLinked && nextRetryAtMs > now && !m_retryTimer.isActive()) {
        const qint64 delay = qMin<qint64>(60 * 1000, nextRetryAtMs - now);
        m_retryTimer.start(static_cast<int>(qMax<qint64>(1, delay)));
    }
}

bool StremioSync::fixtureEndpointAllowed() const {
    return m_options.allowTaggedLoopbackFixture
        && StremioCodec::isTaggedLoopbackEndpoint(m_options.apiEndpoint);
}

bool StremioSync::endpointAllowed() const {
    return StremioCodec::isProductionEndpoint(m_options.apiEndpoint) || fixtureEndpointAllowed();
}

void StremioSync::handleIncomingConnection() {
    while (m_callbackServer.hasPendingConnections()) {
        QTcpSocket *socket = m_callbackServer.nextPendingConnection();
        if (!socket)
            return;
        if (m_callbackSocket) {
            socket->write(failurePage());
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        m_callbackSocket = socket;
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleCallbackSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void StremioSync::handleCallbackSocket(QTcpSocket *socket) {
    if (!socket || socket != m_callbackSocket)
        return;
    m_callbackBuffer += socket->readAll();
    if (m_callbackBuffer.size() > kMaximumCallbackBytes) {
        socket->write(failurePage());
        socket->disconnectFromHost();
        m_callbackSocket = nullptr;
        m_callbackBuffer.clear();
        return;
    }
    if (!m_callbackBuffer.contains("\r\n\r\n"))
        return;
    const StremioLoopbackCallback callback = StremioCodec::decodeLoopbackCallback(m_callbackBuffer, m_callbackPath);
    socket->write(callback.accepted ? successPage() : failurePage());
    socket->disconnectFromHost();
    m_callbackSocket = nullptr;
    m_callbackBuffer.clear();
    if (!callback.accepted)
        return;
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    const quint64 attempt = m_authAttempt;
    m_callbackServer.close();
    validateAuthKey(callback.authKey, binding, attempt);
}

void StremioSync::validateAuthKey(
    const QByteArray &authKey,
    const ProfileBinding &binding,
    quint64 attempt) {
    if (!bindingCurrent(binding) || attempt != m_authAttempt)
        return;
    QUrl endpoint = m_options.apiEndpoint;
    QString path = endpoint.path();
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    endpoint.setPath(path + QStringLiteral("getUser"));
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QJsonObject body;
    body.insert(QStringLiteral("authKey"), QString::fromUtf8(authKey));
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_identityReply = reply;
    m_identityResponse.clear();
    m_identityResponseTooLarge = false;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != m_identityReply)
            return;
        const QByteArray chunk = reply->read(kMaximumIdentityResponseBytes + 1);
        if (m_identityResponse.size() + chunk.size() > kMaximumIdentityResponseBytes) {
            m_identityResponse.clear();
            m_identityResponseTooLarge = true;
            reply->abort();
            return;
        }
        m_identityResponse += chunk;
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, binding, attempt, authKey] {
        const bool current = bindingCurrent(binding) && attempt == m_authAttempt && reply == m_identityReply;
        if (!current) {
            reply->deleteLater();
            return;
        }
        const QNetworkReply::NetworkError networkError = reply->error();
        const bool redirected = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid();
        const QByteArray trailing = reply->isOpen() ? reply->readAll() : QByteArray();
        if (!m_identityResponseTooLarge) {
            if (m_identityResponse.size() + trailing.size() > kMaximumIdentityResponseBytes) {
                m_identityResponse.clear();
                m_identityResponseTooLarge = true;
            } else {
                m_identityResponse += trailing;
            }
        }
        QJsonParseError parseError;
        const QJsonDocument response = QJsonDocument::fromJson(m_identityResponse, &parseError);
        reply->deleteLater();
        m_identityReply = nullptr;
        m_identityResponse.clear();
        StremioAccountIdentity identity;
        QString error;
        if (networkError != QNetworkReply::NoError || redirected || m_identityResponseTooLarge
            || parseError.error != QJsonParseError::NoError
            || !response.isObject() || !StremioCodec::decodeGetUserResult(response.object(), &identity, &error)) {
            cancelAuthentication();
            setStatus(QStringLiteral("notConnected"));
            finishRun();
            return;
        }
        if (!m_state.accountId.isEmpty() && m_state.accountId != identity.accountId) {
            m_hasUsableCredential = false;
            m_dispatchAllowed = false;
            m_retryTimer.stop();
            m_authTimeout.stop();
            setStatus(QStringLiteral("reconnectRequired"));
            finishRun();
            return;
        }
        if (!m_options.saveCredential
            || !m_options.saveCredential(binding.profileId, identity.accountId, authKey)) {
            if (bindingCurrent(binding)) {
                if (m_options.clearCredential)
                    m_options.clearCredential(binding.profileId);
                setStatus(QStringLiteral("notConnected"));
                finishRun();
            }
            return;
        }
        if (!bindingCurrent(binding) || attempt != m_authAttempt) {
            if (m_options.clearCredential)
                m_options.clearCredential(binding.profileId);
            return;
        }
        m_provisionalCredential = ProvisionalCredential{binding, attempt};
        m_hasUsableCredential = false;
        m_dispatchAllowed = false;
        m_state.accountId = identity.accountId;
        m_state.displayName = identity.displayName;
        m_state.bindingGeneration = binding.generation;
        m_state.reconnectRequired = false;
        persist([this, binding, attempt](bool committed) {
            const bool current = bindingCurrent(binding) && attempt == m_authAttempt;
            if (!committed || !current) {
                const bool isProvisional = m_provisionalCredential.has_value()
                    && m_provisionalCredential->binding.profileId == binding.profileId
                    && m_provisionalCredential->binding.generation == binding.generation
                    && m_provisionalCredential->attempt == attempt;
                if (isProvisional)
                    retireProvisionalCredential();
                if (current) {
                    m_hasUsableCredential = false;
                    m_dispatchAllowed = false;
                    m_state.reconnectRequired = true;
                    setStatus(QStringLiteral("notConnected"));
                    finishRun();
                }
                return;
            }
            m_provisionalCredential.reset();
            m_hasUsableCredential = true;
            m_dispatchAllowed = true;
            m_authTimeout.stop();
            m_markerLinked = true;
            emit profileLinkValidated(binding.profileId);
            updateConnectionStatus();
            finishRun();
        });
    });
}

void StremioSync::persist(std::function<void(bool)> continuation) {
    if (m_statePath.isEmpty()) {
        if (continuation)
            continuation(false);
        return;
    }
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    const QString path = m_statePath;
    const StremioPersistentState state = m_state;
    const quint64 generation = m_stateStore.saveAsync(path, state);
    m_pendingPersistences.insert(generation, PendingPersistence{binding, path, state});
    m_pendingStateByPath.insert(path, state);
    if (continuation)
        m_persistContinuations[generation].append(std::move(continuation));
}

void StremioSync::settlePersistence(quint64 generation, bool committed) {
    const PendingPersistence pending = m_pendingPersistences.take(generation);
    const bool activePath = !pending.path.isEmpty() && pending.path == m_statePath;
    if (!hasPendingPersistenceForPath(pending.path))
        m_pendingStateByPath.remove(pending.path);
    const QList<std::function<void(bool)>> continuations =
        m_persistContinuations.take(generation);
    for (const auto &continuation : continuations)
        continuation(committed);
    if (!committed) {
        m_failedPersistencePaths.insert(pending.path);
        if (activePath) {
            m_dispatchAllowed = false;
            m_retryTimer.stop();
            setStatus(QStringLiteral("paused"));
        }
        return;
    }
    if (activePath && !hasPendingPersistenceForPath(pending.path))
        retryPendingNow();
}

void StremioSync::dispatchIntent(const QString &operationId, const ProfileBinding &binding) {
    if (!bindingCurrent(binding) || !m_dispatchAllowed || !m_hasUsableCredential
        || hasPendingPersistenceForPath(m_statePath)
        || m_failedPersistencePaths.contains(m_statePath))
        return;
    // The datastore boundary permits one outstanding reply. A concurrent
    // durable intent is still eligible; defer it until that reply settles
    // rather than routing it through the failure/backoff path without ever
    // making a provider request.
    if (!m_options.intentSender && m_datastoreReply)
        return;
    StremioPendingIntent *intent = intentFor(operationId);
    if (!intent || intent->remoteAcknowledged || intent->attempts >= kMaximumRetries
        || m_inFlightOperations.contains(operationId))
        return;
    const StremioPendingIntent copy = *intent;
    m_inFlightOperations.insert(operationId);
    const auto complete = [this, operationId, binding](bool accepted, bool authenticationFailure) {
        handleIntentResult(operationId, binding, accepted, authenticationFailure);
    };
    if (m_options.intentSender) {
        m_options.intentSender(copy, complete);
        return;
    }
    sendIntentViaDatastore(copy, binding, complete);
}

void StremioSync::sendIntentViaDatastore(
    const StremioPendingIntent &intent,
    const ProfileBinding &binding,
    std::function<void(bool, bool)> completion) {
    const bool isLibraryRemoval = intent.kind == QLatin1String("library_remove");
    const bool isLibraryAdd = intent.kind == QLatin1String("library_add");
    const bool isProgress = intent.kind == QLatin1String("progress");
    const bool isWatched = intent.kind == QLatin1String("watched");
    const bool isSeriesWatched = intent.kind == QLatin1String("series_watched");
    if ((!isLibraryRemoval && !isLibraryAdd && !isProgress && !isWatched && !isSeriesWatched)
        || !bindingCurrent(binding) || !m_options.loadCredential) {
        completion(false, false);
        return;
    }
    const auto credential = m_options.loadCredential(binding.profileId, m_state.accountId);
    if (!credential.has_value() || credential->isEmpty()) {
        completion(false, true);
        return;
    }
    const QString id = intent.desired.value(QStringLiteral("id")).toString().trimmed();
    const QString libraryId = intent.desired.value(QStringLiteral("libraryId")).toString().trimmed();
    const QString type = intent.desired.value(QStringLiteral("type")).toString();
    const QList<StremioDatastoreRequest> gets =
        StremioCodec::datastoreGetRequests(*credential, QStringList{libraryId.isEmpty() ? id : libraryId}, 1);
    if (id.isEmpty() || gets.size() != 1) {
        completion(false, false);
        return;
    }
    postDatastoreRequest(gets.first(), binding,
        [this, binding, id, libraryId, type, isLibraryRemoval, isLibraryAdd, isProgress, isWatched,
         isSeriesWatched,
         desired = intent.desired, completion = std::move(completion)](
            bool fetched,
            bool authenticationFailure,
            QJsonValue result) mutable {
        if (!fetched) {
            completion(false, authenticationFailure);
            return;
        }
        const StremioLibraryItemDecode decoded = StremioCodec::decodeLibraryItems(result, 1);
        const QString expectedLibraryId = libraryId.isEmpty() ? id : libraryId;
        QJsonObject existing;
        if (decoded.items.size() == 1 && decoded.items.first().id == expectedLibraryId) {
            existing = decoded.items.first().raw;
        } else if (isLibraryAdd && decoded.items.isEmpty()
                   && (type == QLatin1String("movie") || type == QLatin1String("series"))) {
            // A new canonical Collection membership is an explicit add, not
            // a provider delete inference. A missing provider row is the one
            // case allowed to create the bounded libraryItem shell.
            existing = QJsonObject{{QStringLiteral("_id"), expectedLibraryId},
                                   {QStringLiteral("type"), type}};
        } else {
            completion(false, false);
            return;
        }
        if ((isLibraryRemoval || isLibraryAdd || isWatched || isSeriesWatched)
            && existing.value(QStringLiteral("type")).toString() != type) {
            completion(false, false);
            return;
        }
        if (isProgress || isWatched) {
            qint64 desiredUpdatedAtMs = 0;
            const std::optional<qint64> providerUpdatedAtMs = providerLastWatchedActivity(existing);
            if (!desiredActivity(desired, &desiredUpdatedAtMs) || !providerUpdatedAtMs.has_value()) {
                completion(false, false);
                return;
            }
            // Never make a queued local retry win over a state Stremio has
            // observed more recently.  Successful retirement leaves the
            // fresh provider value for the normal inbound/readback path and
            // prevents a stale retry from clobbering it.
            if (*providerUpdatedAtMs >= desiredUpdatedAtMs) {
                completion(true, false);
                return;
            }
        }
        QJsonObject patch;
        QList<StremioEpisodeIdentity> seriesVideos;
        QSet<QString> expectedSeriesWatched;
        if (isLibraryRemoval) {
            patch = QJsonObject{{QStringLiteral("removed"), true},
                                {QStringLiteral("temp"), false}};
        } else if (isLibraryAdd) {
            patch = QJsonObject{{QStringLiteral("removed"), false},
                                {QStringLiteral("temp"), false}};
        } else if (isProgress) {
            const QJsonValue positionValue = desired.value(QStringLiteral("positionSeconds"));
            const QJsonValue durationValue = desired.value(QStringLiteral("durationSeconds"));
            const QJsonValue updatedAtValue = desired.value(QStringLiteral("updatedAt"));
            bool timestampOk = false;
            const qint64 updatedAtMs = updatedAtValue.isString()
                ? updatedAtValue.toString().toLongLong(&timestampOk)
                : updatedAtValue.toVariant().toLongLong(&timestampOk);
            const double positionSeconds = positionValue.toDouble(-1.0);
            const double durationSeconds = durationValue.toDouble(-1.0);
            constexpr double maximumSeconds =
                static_cast<double>(std::numeric_limits<qint64>::max()) / 1000.0;
            if (!std::isfinite(positionSeconds) || !std::isfinite(durationSeconds)
                || positionSeconds < 0.0 || durationSeconds <= 0.0
                || positionSeconds > maximumSeconds || durationSeconds > maximumSeconds
                || !timestampOk || updatedAtMs <= 0) {
                completion(false, false);
                return;
            }
            patch = QJsonObject{{QStringLiteral("state"), QJsonObject{
                {QStringLiteral("video_id"), id},
                {QStringLiteral("timeOffset"), static_cast<qint64>(std::llround(positionSeconds * 1000.0))},
                {QStringLiteral("duration"), static_cast<qint64>(std::llround(durationSeconds * 1000.0))},
                {QStringLiteral("lastWatched"), QDateTime::fromMSecsSinceEpoch(
                    updatedAtMs, Qt::UTC).toString(Qt::ISODateWithMs)}}}};
        } else if (isWatched) {
            bool timestampOk = false;
            const QJsonValue updatedAtValue = desired.value(QStringLiteral("updatedAt"));
            const qint64 updatedAtMs = updatedAtValue.isString()
                ? updatedAtValue.toString().toLongLong(&timestampOk)
                : updatedAtValue.toVariant().toLongLong(&timestampOk);
            if (!desired.value(QStringLiteral("watched")).isBool()
                || !timestampOk || updatedAtMs <= 0 || type != QLatin1String("movie")) {
                completion(false, false);
                return;
            }
            patch = QJsonObject{{QStringLiteral("state"), QJsonObject{
                {QStringLiteral("flaggedWatched"), desired.value(QStringLiteral("watched")).toBool() ? 1 : 0},
                {QStringLiteral("lastWatched"), QDateTime::fromMSecsSinceEpoch(
                    updatedAtMs, Qt::UTC).toString(Qt::ISODateWithMs)}}}};
        } else if (isSeriesWatched) {
            QSet<QString> localWatched;
            if (type != QLatin1String("series")
                || !decodeSeriesWatchedDesired(desired, &seriesVideos, &localWatched)) {
                completion(false, false);
                return;
            }
            const QJsonObject currentState = existing.value(QStringLiteral("state")).toObject();
            const QJsonValue existingWatched = currentState.value(QStringLiteral("watched"));
            QSet<QString> remoteWatched;
            QString decodeError;
            if (!existingWatched.isUndefined() && !existingWatched.isNull()) {
                if (!existingWatched.isString()
                    || existingWatched.toString().size() > kMaximumWatchedFieldBytes
                    || !StremioCodec::decodeWatchedEpisodes(
                        existingWatched.toString(), seriesVideos, &remoteWatched, &decodeError)) {
                    completion(false, false);
                    return;
                }
            }
            remoteWatched.unite(localWatched);
            QString encoded;
            if (!StremioCodec::encodeWatchedEpisodes(
                    remoteWatched, seriesVideos, &encoded, &decodeError)) {
                completion(false, false);
                return;
            }
            expectedSeriesWatched = std::move(remoteWatched);
            patch = QJsonObject{{QStringLiteral("state"), QJsonObject{
                {QStringLiteral("watched"), encoded}}}};
        }
        QJsonObject merged;
        QString error;
        if (!StremioCodec::mergeLibraryItemPatch(
                existing,
                patch,
                &merged,
                &error)) {
            completion(false, false);
            return;
        }
        if (merged == existing) {
            completion(true, false);
            return;
        }
        const auto credential = m_options.loadCredential
            ? m_options.loadCredential(binding.profileId, m_state.accountId)
            : std::optional<QByteArray>{};
        if (!bindingCurrent(binding) || !credential.has_value() || credential->isEmpty()) {
            completion(false, true);
            return;
        }
        const StremioDatastoreRequest put = StremioCodec::datastorePutRequest(*credential, merged);
        if (put.method.isEmpty()) {
            completion(false, false);
            return;
        }
        const QString providerType = existing.value(QStringLiteral("type")).toString();
        postDatastoreRequest(put, binding,
            [this, binding, id, libraryId, providerType, isLibraryRemoval, isLibraryAdd, isProgress, isWatched,
             isSeriesWatched, desired, seriesVideos, expectedSeriesWatched,
             completion = std::move(completion)](
                bool written,
                bool writeAuthenticationFailure,
                QJsonValue) mutable {
            if (!written) {
                completion(false, writeAuthenticationFailure);
                return;
            }
            const auto credential = m_options.loadCredential
                ? m_options.loadCredential(binding.profileId, m_state.accountId)
                : std::optional<QByteArray>{};
            const QString providerId = libraryId.isEmpty() ? id : libraryId;
            const QList<StremioDatastoreRequest> gets = credential.has_value()
                ? StremioCodec::datastoreGetRequests(*credential, QStringList{providerId}, 1)
                : QList<StremioDatastoreRequest>{};
            if (!bindingCurrent(binding) || gets.size() != 1) {
                completion(false, !credential.has_value());
                return;
            }
            postDatastoreRequest(gets.first(), binding,
                [id, providerId, providerType, isLibraryRemoval, isLibraryAdd, isProgress, isWatched,
                 isSeriesWatched, desired, seriesVideos, expectedSeriesWatched,
                 completion = std::move(completion)](
                    bool readBack,
                    bool readBackAuthenticationFailure,
                    QJsonValue result) mutable {
                if (!readBack) {
                    completion(false, readBackAuthenticationFailure);
                    return;
                }
                const StremioLibraryItemDecode decoded = StremioCodec::decodeLibraryItems(result, 1);
                if (decoded.items.size() != 1 || decoded.items.first().id != providerId
                    || decoded.items.first().type != providerType) {
                    completion(false, false);
                    return;
                }
                const QJsonObject state = decoded.items.first().raw.value(QStringLiteral("state")).toObject();
                bool matches = false;
                if (isLibraryRemoval) {
                    matches = decoded.items.first().raw.value(QStringLiteral("removed")).toBool()
                        && !decoded.items.first().raw.value(QStringLiteral("temp")).toBool();
                } else if (isLibraryAdd) {
                    matches = !decoded.items.first().raw.value(QStringLiteral("removed")).toBool()
                        && !decoded.items.first().raw.value(QStringLiteral("temp")).toBool();
                } else if (isProgress) {
                    const qint64 positionMs = static_cast<qint64>(std::llround(
                        desired.value(QStringLiteral("positionSeconds")).toDouble() * 1000.0));
                    const qint64 durationMs = static_cast<qint64>(std::llround(
                        desired.value(QStringLiteral("durationSeconds")).toDouble() * 1000.0));
                    matches = state.value(QStringLiteral("video_id")).toString() == id
                        && state.value(QStringLiteral("timeOffset")).toInteger() == positionMs
                        && state.value(QStringLiteral("duration")).toInteger() == durationMs;
                } else if (isWatched) {
                    matches = state.value(QStringLiteral("flaggedWatched")).toInteger()
                        == (desired.value(QStringLiteral("watched")).toBool() ? 1 : 0);
                } else if (isSeriesWatched) {
                    QSet<QString> readBackWatched;
                    QString decodeError;
                    const QJsonValue watched = state.value(QStringLiteral("watched"));
                    matches = watched.isString()
                        && watched.toString().size() <= kMaximumWatchedFieldBytes
                        && StremioCodec::decodeWatchedEpisodes(
                            watched.toString(), seriesVideos, &readBackWatched, &decodeError)
                        && readBackWatched.contains(expectedSeriesWatched);
                }
                completion(matches, false);
            });
        });
    });
}

void StremioSync::postDatastoreRequest(
    const StremioDatastoreRequest &request,
    const ProfileBinding &binding,
    std::function<void(bool, bool, QJsonValue)> completion) {
    if (!bindingCurrent(binding) || !endpointAllowed() || request.method.isEmpty()
        || request.payload.isEmpty() || m_datastoreReply) {
        completion(false, false, {});
        return;
    }
    QUrl endpoint = m_options.apiEndpoint;
    QString path = endpoint.path();
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    endpoint.setPath(path + request.method);
    QNetworkRequest networkRequest(endpoint);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = m_network.post(
        networkRequest, QJsonDocument(request.payload).toJson(QJsonDocument::Compact));
    m_datastoreReply = reply;
    m_datastoreResponse.clear();
    m_datastoreResponseTooLarge = false;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != m_datastoreReply)
            return;
        const QByteArray chunk = reply->read(kMaximumDatastoreResponseBytes + 1);
        if (m_datastoreResponse.size() + chunk.size() > kMaximumDatastoreResponseBytes) {
            m_datastoreResponse.clear();
            m_datastoreResponseTooLarge = true;
            reply->abort();
            return;
        }
        m_datastoreResponse += chunk;
    });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, binding, completion = std::move(completion)]() mutable {
        const bool current = bindingCurrent(binding) && reply == m_datastoreReply;
        if (!current) {
            reply->deleteLater();
            return;
        }
        const QNetworkReply::NetworkError networkError = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool redirected = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid();
        const QByteArray trailing = reply->isOpen() ? reply->readAll() : QByteArray();
        if (!m_datastoreResponseTooLarge) {
            if (m_datastoreResponse.size() + trailing.size() > kMaximumDatastoreResponseBytes) {
                m_datastoreResponse.clear();
                m_datastoreResponseTooLarge = true;
            } else {
                m_datastoreResponse += trailing;
            }
        }
        QJsonParseError parseError;
        const QJsonDocument body = QJsonDocument::fromJson(m_datastoreResponse, &parseError);
        reply->deleteLater();
        m_datastoreReply = nullptr;
        m_datastoreResponse.clear();
        const auto resumePending = [this, binding] {
            if (bindingCurrent(binding))
                retryPendingNow();
        };
        const bool authenticationFailure = status == 401 || status == 403;
        if (networkError != QNetworkReply::NoError || redirected || m_datastoreResponseTooLarge
            || parseError.error != QJsonParseError::NoError || !body.isObject()
            || !body.object().contains(QStringLiteral("result"))) {
            completion(false, authenticationFailure, {});
            QTimer::singleShot(0, this, resumePending);
            return;
        }
        completion(true, false, body.object().value(QStringLiteral("result")));
        QTimer::singleShot(0, this, resumePending);
    });
}

void StremioSync::handleIntentResult(
    const QString &operationId,
    const ProfileBinding &binding,
    bool accepted,
    bool authenticationFailure) {
    if (!bindingCurrent(binding))
        return;
    m_inFlightOperations.remove(operationId);
    StremioPendingIntent *intent = intentFor(operationId);
    if (!intent)
        return;
    if (accepted) {
        intent->remoteAcknowledged = true;
        if (intent->kind == QLatin1String("library_remove")) {
            upsertMembershipDifference(
                intent->desired.value(QStringLiteral("id")).toString(),
                intent->desired.value(QStringLiteral("type")).toString(),
                false,
                false,
                true);
        }
        const QString baselineKey = baselineKeyForIntent(intent->kind, intent->desired);
        bool superseded = false;
        if (!baselineKey.isEmpty()) {
            for (const StremioPendingIntent &candidate : std::as_const(m_state.pendingIntents)) {
                if (candidate.operationId == operationId || candidate.remoteAcknowledged
                    || baselineKeyForIntent(candidate.kind, candidate.desired) != baselineKey) {
                    continue;
                }
                // The newer operation remains authoritative until its own
                // provider readback succeeds. An older in-flight acknowledgement
                // may remove only itself; it must not publish an older baseline.
                superseded = true;
                break;
            }
            if (!superseded)
                m_state.acknowledgedBaselines.insert(baselineKey, intent->desired);
        }
        m_state.lastSuccessAtMs = m_options.clock();
        persist([this, operationId](bool committed) {
            if (committed)
                removeSatisfiedIntent(operationId);
        });
        emit stateChanged();
        return;
    }
    if (authenticationFailure) {
        m_hasUsableCredential = false;
        m_dispatchAllowed = false;
        m_retryTimer.stop();
        m_state.reconnectRequired = true;
        if (m_options.clearCredential)
            m_options.clearCredential(binding.profileId);
        persist();
        setStatus(QStringLiteral("reconnectRequired"));
        return;
    }
    ++intent->attempts;
    if (intent->attempts >= kMaximumRetries) {
        if (m_visibleSyncActive) {
            m_visibleSyncActive = false;
            m_visibleSyncPullComplete = false;
            m_pendingVisibleSyncSummary.clear();
            m_lastResultSummary = QStringLiteral(
                "Sync could not finish. Your Colosseum data was kept.");
        }
        setStatus(QStringLiteral("syncFailed"));
        persist();
        return;
    }
    const qint64 delay = qMin<qint64>(60 * 1000, 1000LL << qMin(intent->attempts, 5));
    intent->retryAtMs = m_options.clock() + delay;
    persist();
    m_retryTimer.start(static_cast<int>(delay));
    emit stateChanged();
}

void StremioSync::removeSatisfiedIntent(const QString &operationId) {
    StremioPendingIntent *intent = intentFor(operationId);
    if (!intent || !intent->remoteAcknowledged || !intent->localReceiptDurable)
        return;
    for (qsizetype index = 0; index < m_state.pendingIntents.size(); ++index) {
        if (m_state.pendingIntents.at(index).operationId == operationId) {
            m_state.pendingIntents.removeAt(index);
            persist([this](bool committed) {
                if (committed)
                    completeVisibleSyncIfDrained();
            });
            emit stateChanged();
            return;
        }
    }
}

bool StremioSync::bindingCurrent(const ProfileBinding &binding) const {
    return !binding.profileId.isEmpty()
        && binding.profileId == m_profileId
        && binding.generation == m_bindingGeneration;
}

bool StremioSync::hasPendingPersistenceForPath(const QString &path) const {
    for (const PendingPersistence &pending : m_pendingPersistences) {
        if (pending.path == path)
            return true;
    }
    return false;
}

void StremioSync::updateConnectionStatus() {
    if (m_profileId.isEmpty())
        return;
    if (m_failedPersistencePaths.contains(m_statePath)) {
        setStatus(QStringLiteral("paused"));
        return;
    }
    if (m_state.accountId.isEmpty()) {
        setStatus(m_markerLinked ? QStringLiteral("reconnectRequired") : QStringLiteral("notConnected"));
        return;
    }
    setStatus(!m_state.reconnectRequired && m_markerLinked && m_hasUsableCredential && m_dispatchAllowed
        ? QStringLiteral("synced")
        : QStringLiteral("reconnectRequired"));
}

StremioPendingIntent *StremioSync::intentFor(const QString &operationId) {
    for (StremioPendingIntent &intent : m_state.pendingIntents) {
        if (intent.operationId == operationId)
            return &intent;
    }
    return nullptr;
}

void StremioSync::setStatus(const QString &statusValue) {
    if (m_status == statusValue)
        return;
    m_status = statusValue;
    emit stateChanged();
}

void StremioSync::finishRun() {
    ++m_completedRun;
    emit stateChanged();
}

void StremioSync::upsertMembershipDifference(
    const QString &id,
    const QString &type,
    bool localPresent,
    bool remotePresent,
    bool explicitRemoteRemoval) {
    QJsonObject difference{
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), type},
        {QStringLiteral("localPresent"), localPresent},
        {QStringLiteral("remotePresent"), remotePresent},
        {QStringLiteral("explicitRemoteRemoval"), explicitRemoteRemoval}};
    for (qsizetype index = 0; index < m_state.intentionalMembershipDifferences.size(); ++index) {
        const QJsonValue existing = m_state.intentionalMembershipDifferences.at(index);
        if (existing.isObject()
            && existing.toObject().value(QStringLiteral("id")).toString() == id
            && existing.toObject().value(QStringLiteral("type")).toString() == type) {
            m_state.intentionalMembershipDifferences.replace(index, difference);
            return;
        }
    }
    m_state.intentionalMembershipDifferences.append(difference);
}

bool StremioSync::clearLocalOnlyMembershipSuppression(
    const QString &id,
    const QString &type) {
    const QString normalizedId = id.trimmed();
    for (qsizetype index = 0; index < m_state.intentionalMembershipDifferences.size(); ++index) {
        const QJsonValue value = m_state.intentionalMembershipDifferences.at(index);
        if (!value.isObject())
            continue;
        const QJsonObject difference = value.toObject();
        if (difference.value(QStringLiteral("id")).toString() != normalizedId
            || difference.value(QStringLiteral("type")).toString() != type
            || difference.value(QStringLiteral("localPresent")).toBool()
            || !difference.value(QStringLiteral("remotePresent")).toBool()
            || difference.value(QStringLiteral("explicitRemoteRemoval")).toBool()) {
            continue;
        }
        m_state.intentionalMembershipDifferences.removeAt(index);
        return true;
    }
    return false;
}

bool StremioSync::suppressesInferredLibraryAddition(
    const QString &id,
    const QString &type) const {
    const QString normalizedId = id.trimmed();
    for (const QJsonValue &value : m_state.intentionalMembershipDifferences) {
        if (!value.isObject())
            continue;
        const QJsonObject difference = value.toObject();
        if (difference.value(QStringLiteral("id")).toString() != normalizedId
            || difference.value(QStringLiteral("type")).toString() != type) {
            continue;
        }
        return difference.value(QStringLiteral("localPresent")).toBool()
            && !difference.value(QStringLiteral("remotePresent")).toBool();
    }
    return false;
}
