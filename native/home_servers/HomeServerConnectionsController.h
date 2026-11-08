#pragma once

#include "HomeServerConnectionStore.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>

namespace Colosseum::HomeServers {

class HomeServerConnectionsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList connections READ connections NOTIFY connectionsChanged)
    Q_PROPERTY(QString profileId READ profileId NOTIFY profileChanged)
    Q_PROPERTY(bool available READ available NOTIFY profileChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY statusChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY statusChanged)

public:
    using Transport = JellyfinEmbyClient::Transport;
    explicit HomeServerConnectionsController(
        std::shared_ptr<AccountSecretBackend> backend,
        QObject *parent = nullptr,
        Transport transport = {});

    QVariantList connections() const;
    QString profileId() const { return m_profileId; }
    bool available() const { return m_available; }
    bool busy() const { return m_busy; }
    QString notice() const { return m_notice; }
    bool hasError() const { return m_error; }

    void activateProfile(const QString &profileId);

    Q_INVOKABLE bool connectServer(const QString &kind,
                                   const QString &displayName,
                                   const QString &address,
                                   const QString &username,
                                   const QString &password);
    Q_INVOKABLE void cancelConnect();
    Q_INVOKABLE bool setConnectionEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool removeConnection(const QString &id);

signals:
    void connectionsChanged();
    void profileChanged();
    void statusChanged();

private:
    struct Pending {
        ServerKind kind = ServerKind::Jellyfin;
        QString name;
        QString username;
        QString password;
        QList<QUrl> candidates;
        quint64 serial = 0;
        QString profileId;
    };

    void tryCandidate(Pending pending, qsizetype index);
    void finish(const QString &message, bool error);
    void releaseClient();

    std::shared_ptr<AccountSecretBackend> m_backend;
    std::unique_ptr<HomeServerConnectionStore> m_store;
    QPointer<JellyfinEmbyClient> m_client;
    Transport m_transport;
    QString m_profileId;
    QString m_notice;
    quint64 m_serial = 0;
    bool m_available = false;
    bool m_busy = false;
    bool m_error = false;
};

} // namespace Colosseum::HomeServers
