#pragma once

#include "JellyfinEmbyClient.h"

#include <QList>
#include <QString>
#include <optional>

namespace Colosseum::HomeServers {

struct IndexedServerItem {
    QString libraryId;
    ServerItem item;
};

struct ServerCatalogue {
    QString profileId;
    QString connectionId;
    QString userId;
    QString serverId;
    qint64 syncedAtUtcMs = 0;
    QList<ServerView> libraries;
    QList<IndexedServerItem> items;

    int identifiedCount() const;
};

// Profile-local remote-media cache, not an identity or progress authority.
// Replaces the complete connection snapshot atomically after a full scan.
class HomeServerCatalogueStore final {
public:
    HomeServerCatalogueStore(QString profileId, QString profileRoot);

    bool available() const;
    std::optional<ServerCatalogue> load(const QString &connectionId,
                                        const QString &userId,
                                        const QString &serverId) const;
    bool replace(const ServerCatalogue &catalogue);
    bool remove(const QString &connectionId);

    static bool validConnectionId(const QString &connectionId);

private:
    QString filePath(const QString &connectionId) const;
    QString m_profileId;
    QString m_profileRoot;
};

} // namespace Colosseum::HomeServers
