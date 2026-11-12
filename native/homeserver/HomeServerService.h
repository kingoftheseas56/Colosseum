#pragma once

#include "HomeServerClient.h"
#include "HomeServerStore.h"

#include <QJsonArray>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

struct HomeServerServiceOptions {
    std::shared_ptr<AccountSecretBackend> secrets;
    QString identityDirectory;
    QUrl plexOrigin = QUrl(QStringLiteral("https://plex.tv"));
    int pinPollIntervalMs = 1500;
};

// The QML boundary contains display metadata and commands only. Passwords are
// consumed by connectServer; bearer tokens stay in native client/storage code.
class HomeServerService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)
    Q_PROPERTY(QVariantList plexServers READ plexServers NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString plexCode READ plexCode NOTIFY changed)
    Q_PROPERTY(QString plexUrl READ plexUrl NOTIFY changed)
    Q_PROPERTY(bool httpConsentRequired READ httpConsentRequired NOTIFY changed)
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
    Q_PROPERTY(quint64 profileGeneration READ profileGeneration NOTIFY profileChanged)

public:
    explicit HomeServerService(QObject *parent = nullptr);
    explicit HomeServerService(HomeServerServiceOptions options, QObject *parent = nullptr);
    ~HomeServerService() override;
    void activate(const QString &profileId, const QString &profileRoot);
    void deactivate();

    QVariantList connections() const;
    QVariantList plexServers() const;
    bool available() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString plexCode() const { return m_pinCode; }
    QString plexUrl() const { return m_pinUrl; }
    bool httpConsentRequired() const { return m_httpConsentRequired; }
    quint64 revision() const { return m_revision; }
    quint64 profileGeneration() const { return m_profileGeneration; }

    Q_INVOKABLE void connectServer(const QString &provider, const QString &address,
                                   const QString &username, const QString &password,
                                   bool allowHttp = false);
    Q_INVOKABLE void beginPlex();
    Q_INVOKABLE void selectPlexServer(const QString &id, bool allowHttp = false);
    Q_INVOKABLE void cancelSignIn();
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE bool isInsecureAddress(const QString &address) const;

signals:
    void changed();
    void profileChanged();
    void connectionAdded(const QString &id);

private:
    void publish();
    void finishSignIn(const QString &message);
    void invalidateRequests();
    void pollPlex();
    void probePlex(const QJsonObject &candidate, int endpoint, bool allowHttp,
                   bool skippedHttp, quint64 generation);
    void saveAuthenticated(const QJsonObject &metadata, const QString &token);
    bool canSignIn();

    HomeServerClient m_client;
    HomeServerStore m_store;
    QTimer m_pinTimer;
    QJsonArray m_plexCandidates;
    QHash<QString, QString> m_connectionStatus;
    QString m_profileId;
    QString m_profileRoot;
    QString m_status;
    QString m_pinCode;
    QString m_pinUrl;
    qint64 m_pinId = 0;
    qint64 m_pinDeadline = 0;
    quint64 m_generation = 0;
    quint64 m_profileGeneration = 0;
    quint64 m_revision = 0;
    bool m_busy = false;
    bool m_httpConsentRequired = false;
};
