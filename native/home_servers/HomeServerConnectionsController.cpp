#include "HomeServerConnectionsController.h"

#include <QDateTime>
#include <QSettings>
#include <QUuid>
#include <QUrl>
#include <utility>

namespace Colosseum::HomeServers {
namespace {
QString installationDeviceId()
{
    // The app identity is set by main before this controller is constructed.
    // Default QSettings therefore follows tagged test launches as well.
    QSettings settings;
    constexpr auto key = "homeServers/installationDeviceId";
    QString uuid = settings.value(QLatin1String(key)).toString();
    const QUuid parsed(uuid);
    if (parsed.isNull() || parsed.toString(QUuid::WithoutBraces) != uuid) {
        uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue(QLatin1String(key), uuid);
        settings.sync();
        if (settings.status() != QSettings::NoError)
            return {}; // No non-persistent device identity is used to sign in.
    }
    return QStringLiteral("colosseum-") + uuid;
}
}

HomeServerConnectionsController::HomeServerConnectionsController(
    std::shared_ptr<AccountSecretBackend> backend, QObject *parent,
    Transport transport)
    : QObject(parent), m_backend(std::move(backend)),
      m_transport(std::move(transport)), m_deviceId(installationDeviceId())
{
}

QVariantList HomeServerConnectionsController::connections() const
{
    QVariantList result;
    if (!m_store || !m_available)
        return result;
    for (const auto &connection : m_store->list()) {
        result.append(QVariantMap{
            {QStringLiteral("id"), connection.id},
            {QStringLiteral("name"), connection.displayName},
            {QStringLiteral("kind"), connection.kind == ServerKind::Jellyfin
                ? QStringLiteral("Jellyfin") : QStringLiteral("Emby")},
            {QStringLiteral("address"), connection.apiBase.toString()},
            {QStringLiteral("userId"), connection.userId},
            {QStringLiteral("enabled"), connection.enabled},
            {QStringLiteral("indexedCount"), m_indexSummaries.value(connection.id).total},
            {QStringLiteral("matchedCount"), m_indexSummaries.value(connection.id).identified},
            {QStringLiteral("lastRefreshMs"), m_indexSummaries.value(connection.id).syncedAtMs}
        });
    }
    return result;
}

void HomeServerConnectionsController::releaseClient()
{
    if (m_client) {
        m_client->deleteLater();
        m_client.clear();
    }
}

void HomeServerConnectionsController::finish(const QString &message, bool error)
{
    releaseClient();
    m_busy = false;
    m_error = error;
    m_notice = message;
    emit statusChanged();
}

void HomeServerConnectionsController::activateProfile(const QString &profileId,
                                                       const QString &profileRoot)
{
    ++m_serial; // Invalidate callbacks before releasing the previous session.
    releaseClient();
    releaseRefreshClient();
    m_refreshing = false;
    m_refreshingId.clear();
    m_catalogue.reset();
    m_indexSummaries.clear();
    m_store.reset();
    m_available = false;
    m_busy = false;
    m_error = false;
    m_notice.clear();
    m_profileId = profileId;
    if (!profileId.isEmpty()) {
        auto store = std::make_unique<HomeServerConnectionStore>(
            profileId, m_backend);
        if (store->available() && !m_deviceId.isEmpty()) {
            m_store = std::move(store);
            m_available = true;
            auto index = std::make_unique<HomeServerCatalogueStore>(profileId, profileRoot);
            if (index->available())
                m_catalogue = std::move(index);
            reloadSummaries();
        }
    }
    emit profileChanged();
    emit connectionsChanged();
    emit statusChanged();
}

bool HomeServerConnectionsController::connectServer(
    const QString &kind, const QString &displayName,
    const QString &address, const QString &username, const QString &password)
{
    if (m_busy || m_refreshing || !m_available || !m_store) {
        if (!m_busy)
            finish(QStringLiteral("Secure server storage is unavailable."), true);
        return false;
    }
    ServerKind provider;
    if (kind == QLatin1String("jellyfin"))
        provider = ServerKind::Jellyfin;
    else if (kind == QLatin1String("emby"))
        provider = ServerKind::Emby;
    else {
        finish(QStringLiteral("Choose Jellyfin or Emby."), true);
        return false;
    }

    const QUrl configured(address.trimmed());
    const auto candidates = HomeServerConnectionStore::candidateApiBases(
        provider, configured);
    const QString name = displayName.trimmed().isEmpty()
        ? configured.host() : displayName.trimmed();
    if (candidates.isEmpty() || name.isEmpty() || name.size() > 96
        || username.trimmed().isEmpty() || username.size() > 320
        || password.isEmpty() || password.size() > 1024) {
        finish(QStringLiteral("Enter a valid server URL, username, and password."), true);
        return false;
    }
    ++m_serial;
    m_busy = true;
    m_error = false;
    m_notice = QStringLiteral("Connecting to your server…");
    emit statusChanged();
    tryCandidate(Pending{provider, name, username.trimmed(), password,
                         candidates, m_serial, m_profileId}, 0);
    return true;
}

void HomeServerConnectionsController::tryCandidate(Pending pending, qsizetype index)
{
    if (!m_store || !m_busy || index >= pending.candidates.size()
        || pending.serial != m_serial || pending.profileId != m_profileId)
        return;
    releaseClient();
    const QUrl base = pending.candidates.at(index);
    auto *client = new JellyfinEmbyClient(
        pending.kind, base, m_deviceId, this, m_transport);
    if (!client->isValid()) {
        client->deleteLater();
        finish(QStringLiteral("The server address is not valid."), true);
        return;
    }
    m_client = client;
    const QString username = pending.username;
    const QString password = pending.password;
    client->authenticate(username, password,
        [this, pending = std::move(pending), index, base](
            ServerResult<ServerSession> result) mutable {
            if (pending.serial != m_serial || pending.profileId != m_profileId
                || !m_busy || !m_store)
                return;
            if (result.ok()) {
                const auto saved = m_store->add(pending.kind, pending.name,
                                                base, result.value);
                if (!saved) {
                    finish(QStringLiteral("Unable to save the connection securely, or it already exists."),
                           true);
                    return;
                }
                finish(QStringLiteral("Connected to your server."), false);
                emit connectionsChanged();
                return;
            }
            // A missing endpoint may mean Emby is exposed at /emby.
            // Never retry credentials on 401/403, redirects, or another origin.
            if (result.status == 404 && index + 1 < pending.candidates.size()) {
                tryCandidate(std::move(pending), index + 1);
                return;
            }
            finish(QStringLiteral("Server sign-in failed. Check the address and credentials."),
                   true);
        });
}

void HomeServerConnectionsController::cancelConnect()
{
    if (!m_busy)
        return;
    ++m_serial;
    finish(QString(), false);
}

bool HomeServerConnectionsController::setConnectionEnabled(
    const QString &id, bool enabled)
{
    if (!m_store || !m_available || m_busy || m_refreshing
        || !m_store->setEnabled(id, enabled))
        return false;
    emit connectionsChanged();
    return true;
}

bool HomeServerConnectionsController::removeConnection(const QString &id)
{
    if (!m_store || !m_available || m_busy || m_refreshing || !m_store->remove(id))
        return false;
    m_indexSummaries.remove(id);
    const bool cacheRemoved = !m_catalogue || m_catalogue->remove(id);
    emit connectionsChanged();
    return cacheRemoved;
}


void HomeServerConnectionsController::releaseRefreshClient()
{
    if (m_refreshClient) {
        m_refreshClient->deleteLater();
        m_refreshClient.clear();
    }
}

void HomeServerConnectionsController::reloadSummaries()
{
    m_indexSummaries.clear();
    if (!m_store || !m_catalogue)
        return;
    for (const auto &connection : m_store->list()) {
        const auto cached = m_catalogue->load(
            connection.id, connection.userId, connection.serverId);
        if (cached) {
            m_indexSummaries.insert(connection.id, {
                static_cast<int>(cached->items.size()), cached->identifiedCount(),
                cached->syncedAtUtcMs
            });
        }
    }
}

bool HomeServerConnectionsController::currentRefresh(
    const std::shared_ptr<RefreshJob> &job) const
{
    return job && m_refreshing && m_store && m_catalogue
        && job->serial == m_serial && job->profileId == m_profileId
        && job->connectionId == m_refreshingId && m_refreshClient;
}

bool HomeServerConnectionsController::refreshConnection(const QString &id)
{
    if (m_busy || m_refreshing || !m_available || !m_store || !m_catalogue)
        return false;
    std::optional<HomeServerConnection> connection;
    for (const auto &candidate : m_store->list()) {
        if (candidate.id == id) {
            connection = candidate;
            break;
        }
    }
    if (!connection || !connection->enabled)
        return false;
    const auto session = m_store->activeSession(id);
    if (!session)
        return false;
    auto *client = new JellyfinEmbyClient(connection->kind, connection->apiBase,
                                          m_deviceId, this, m_transport);
    if (!client->isValid()) {
        client->deleteLater();
        return false;
    }
    ++m_serial;
    m_refreshClient = client;
    m_refreshing = true;
    m_refreshingId = id;
    m_error = false;
    m_notice = QStringLiteral("Refreshing server library…");
    emit statusChanged();

    const auto job = std::make_shared<RefreshJob>();
    job->serial = m_serial;
    job->profileId = m_profileId;
    job->connectionId = id;
    job->session = *session;
    job->snapshot.profileId = m_profileId;
    job->snapshot.connectionId = id;
    job->snapshot.userId = session->userId;
    job->snapshot.serverId = session->serverId;

    client->fetchViews(*session, [this, job](
        ServerResult<QList<ServerView>> result) {
        if (!currentRefresh(job))
            return;
        if (!result.ok()) {
            failRefresh(result.status == 401);
            return;
        }
        QSet<QString> libraryIds;
        for (const auto &v : result.value) {
            const QString kind = v.collectionType.toLower();
            if (kind != QLatin1String("movies")
                && kind != QLatin1String("tvshows")
                && kind != QLatin1String("mixed"))
                continue;
            if (v.id.isEmpty() || libraryIds.contains(v.id)) {
                failRefresh();
                return;
            }
            libraryIds.insert(v.id);
            job->snapshot.libraries.append(v);
        }
        // An empty/unknown response must not wipe the previous local catalogue.
        if (job->snapshot.libraries.isEmpty()) {
            failRefresh();
            return;
        }
        fetchRefreshPage(job);
    });
    return true;
}

void HomeServerConnectionsController::fetchRefreshPage(
    const std::shared_ptr<RefreshJob> &job)
{
    if (!currentRefresh(job))
        return;
    if (job->libraryIndex >= job->snapshot.libraries.size()) {
        finishRefresh(job);
        return;
    }
    if (!m_refreshClient) {
        failRefresh();
        return;
    }
    const QString viewId = job->snapshot.libraries.at(job->libraryIndex).id;
    m_refreshClient->fetchItemsPage(job->session, viewId, job->offset, 200,
        [this, job, viewId](ServerResult<ItemPage> result) {
            if (!currentRefresh(job))
                return;
            if (!result.ok() || result.value.startIndex != job->offset
                || result.value.returnedCount < 0
                || result.value.returnedCount != result.value.items.size()
                || result.value.totalRecordCount < job->offset
                || (job->expectedTotal >= 0
                    && job->expectedTotal != result.value.totalRecordCount)
                || job->snapshot.items.size() + result.value.returnedCount > 150000) {
                failRefresh(result.status == 401);
                return;
            }
            if (job->expectedTotal < 0)
                job->expectedTotal = result.value.totalRecordCount;
            if (result.value.returnedCount == 0
                && job->offset < job->expectedTotal) {
                failRefresh();
                return;
            }
            for (auto &item : result.value.items) {
                if (job->itemIds.contains(item.id)
                    || item.userId != job->session.userId
                    || item.serverId != job->session.serverId) {
                    failRefresh();
                    return;
                }
                job->itemIds.insert(item.id);
                job->snapshot.items.append({viewId, std::move(item)});
            }
            job->offset += result.value.returnedCount;
            if (job->offset < job->expectedTotal) {
                fetchRefreshPage(job);
                return;
            }
            if (job->offset != job->expectedTotal) {
                failRefresh();
                return;
            }
            ++job->libraryIndex;
            job->offset = 0;
            job->expectedTotal = -1;
            fetchRefreshPage(job);
        });
}

void HomeServerConnectionsController::failRefresh(bool unauthorized)
{
    releaseRefreshClient();
    m_refreshing = false;
    m_refreshingId.clear();
    m_error = true;
    m_notice = unauthorized
        ? QStringLiteral("Server sign-in expired. Remove the connection and sign in again. Your previous library index was kept.")
        : QStringLiteral("Refresh failed. Your previous library index was kept.");
    emit statusChanged();
}

void HomeServerConnectionsController::finishRefresh(
    const std::shared_ptr<RefreshJob> &job)
{
    if (!currentRefresh(job))
        return;
    job->snapshot.syncedAtUtcMs = QDateTime::currentMSecsSinceEpoch();
    // One atomic replacement after all views and pages have completed.
    // Never sweep stale IDs during a partial or failed scan.
    if (!m_catalogue->replace(job->snapshot)) {
        failRefresh();
        return;
    }
    const int total = job->snapshot.items.size();
    m_indexSummaries.insert(job->connectionId, {
        total, job->snapshot.identifiedCount(), job->snapshot.syncedAtUtcMs
    });
    releaseRefreshClient();
    m_refreshing = false;
    m_refreshingId.clear();
    m_error = false;
    m_notice = QStringLiteral("Library refreshed: %1 items indexed.").arg(total);
    emit connectionsChanged();
    emit statusChanged();
}

void HomeServerConnectionsController::cancelRefresh()
{
    if (!m_refreshing)
        return;
    ++m_serial; // Ignore any response from the canceled or leaving profile.
    releaseRefreshClient();
    m_refreshing = false;
    m_refreshingId.clear();
    m_error = false;
    m_notice = QStringLiteral("Refresh canceled. Previous index preserved.");
    emit statusChanged();
}

} // namespace Colosseum::HomeServers
