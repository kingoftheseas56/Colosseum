#pragma once

#include "JellyfinEmbyClient.h"
#include "account/credentials/AccountSecretBackend.h"

#include <QList>
#include <QUrl>
#include <memory>
#include <optional>

namespace Colosseum::HomeServers {

struct HomeServerConnection {
    QString id;
    QString displayName;
    ServerKind kind = ServerKind::Jellyfin;
    QUrl apiBase;
    QString userId;
    QString serverId;
    bool enabled = true;
    qint64 addedAtUtcMs = 0;
};

// Session tokens and connection metadata live only in the account secret
// backend. No password or token is exposed by the list() projection.
class HomeServerConnectionStore final {
public:
    HomeServerConnectionStore(
        QString profileId, std::shared_ptr<AccountSecretBackend> backend);

    bool available() const;
    QList<HomeServerConnection> list() const;
    std::optional<QString> add(ServerKind kind, const QString &displayName,
                               const QUrl &apiBase, const ServerSession &session);
    std::optional<ServerSession> activeSession(const QString &connectionId) const;
    bool replaceSession(const QString &connectionId, const ServerSession &session);
    bool setEnabled(const QString &connectionId, bool enabled);
    bool remove(const QString &connectionId);

    // Propose same-origin API prefixes only. Never send credentials during
    // discovery or silently change schemes, hosts, or ports.
    static QList<QUrl> candidateApiBases(ServerKind kind, const QUrl &configured);

private:
    struct Stored {
        HomeServerConnection connection;
        ServerSession session;
    };

    static bool validProfileId(const QString &profileId);
    static bool validConnectionId(const QString &connectionId);
    static bool validSession(const ServerSession &session);
    static bool validName(const QString &displayName);
    QString target(const QString &connectionId) const;
    std::optional<Stored> load(const QString &connectionId) const;
    bool persist(const Stored &value);

    QString m_profileId;
    QString m_prefix;
    std::shared_ptr<AccountSecretBackend> m_backend;
};

} // namespace Colosseum::HomeServers
