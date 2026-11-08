#include "HomeServerConnectionsController.h"

#include <QCryptographicHash>
#include <QUrl>
#include <utility>

namespace Colosseum::HomeServers {
namespace {
QString deviceId(const QString &profileId)
{
    const QByteArray digest = QCryptographicHash::hash(
        profileId.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("colosseum-") + QString::fromLatin1(digest.left(18));
}
}

HomeServerConnectionsController::HomeServerConnectionsController(
    std::shared_ptr<AccountSecretBackend> backend, QObject *parent,
    Transport transport)
    : QObject(parent), m_backend(std::move(backend)),
      m_transport(std::move(transport))
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
            {QStringLiteral("enabled"), connection.enabled}
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

void HomeServerConnectionsController::activateProfile(const QString &profileId)
{
    ++m_serial; // Invalidate callbacks before releasing the previous session.
    releaseClient();
    m_store.reset();
    m_available = false;
    m_busy = false;
    m_error = false;
    m_notice.clear();
    m_profileId = profileId;
    if (!profileId.isEmpty()) {
        auto store = std::make_unique<HomeServerConnectionStore>(
            profileId, m_backend);
        if (store->available()) {
            m_store = std::move(store);
            m_available = true;
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
    if (m_busy || !m_available || !m_store) {
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
        pending.kind, base, deviceId(m_profileId), this, m_transport);
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
    if (!m_store || !m_available || m_busy
        || !m_store->setEnabled(id, enabled))
        return false;
    emit connectionsChanged();
    return true;
}

bool HomeServerConnectionsController::removeConnection(const QString &id)
{
    if (!m_store || !m_available || m_busy || !m_store->remove(id))
        return false;
    emit connectionsChanged();
    return true;
}

} // namespace Colosseum::HomeServers
