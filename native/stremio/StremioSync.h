#pragma once

#include "StremioCodec.h"
#include "StremioState.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QTcpSocket>

#include <functional>
#include <optional>

struct StremioSyncOptions {
    QUrl apiEndpoint = QUrl(QStringLiteral("https://api.strem.io/api"));
    std::function<void(const QUrl &)> browserOpener;
    std::function<bool(const QString &, const QString &, const QByteArray &)> saveCredential;
    std::function<bool(const QString &)> clearCredential;
    std::function<std::optional<QByteArray>(const QString &, const QString &)> loadCredential;
    std::function<void(const StremioPendingIntent &, std::function<void(bool, bool)>)> intentSender;
    std::function<qint64()> clock;
    bool allowTaggedLoopbackFixture = false;
};

class StremioSync final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY stateChanged)
    Q_PROPERTY(qint64 lastSuccessAt READ lastSuccessAt NOTIFY stateChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY stateChanged)
    Q_PROPERTY(bool mergeComplete READ mergeComplete NOTIFY stateChanged)
    Q_PROPERTY(quint64 completedRun READ completedRun NOTIFY stateChanged)

public:
    explicit StremioSync(
        const StremioSyncOptions &options = {},
        QObject *parent = nullptr);

    QString status() const;
    int pendingCount() const;
    qint64 lastSuccessAt() const;
    QString activeProfileId() const;
    bool mergeComplete() const;
    quint64 completedRun() const;

    bool activateProfile(
        const QString &profileId,
        const QString &statePath,
        bool sealed,
        QString *error = nullptr);
    void deactivateProfile();
    bool startBrowserAuthentication(QString *error = nullptr);
    void cancelAuthentication();
    void setCredentialCallbacks(
        std::function<bool(const QString &, const QString &, const QByteArray &)> save,
        std::function<bool(const QString &)> clear,
        std::function<std::optional<QByteArray>(const QString &, const QString &)> load = {});

    bool queueIntent(
        const QString &kind,
        const QJsonObject &desired,
        QString *operationId = nullptr);
    bool acknowledgeLocalReceipt(const QString &operationId);
    void retryPendingNow();

signals:
    void stateChanged();
    void browserLoginRequested(const QUrl &url);

private:
    struct ProfileBinding {
        QString profileId;
        quint64 generation = 0;
    };

    bool fixtureEndpointAllowed() const;
    bool endpointAllowed() const;
    void handleIncomingConnection();
    void handleCallbackSocket(QTcpSocket *socket);
    void validateAuthKey(
        const QByteArray &authKey,
        const ProfileBinding &binding,
        quint64 attempt);
    void persist(std::function<void(bool)> continuation = {});
    void dispatchIntent(const QString &operationId, const ProfileBinding &binding);
    void handleIntentResult(
        const QString &operationId,
        const ProfileBinding &binding,
        bool accepted,
        bool authenticationFailure);
    void removeSatisfiedIntent(const QString &operationId);
    bool bindingCurrent(const ProfileBinding &binding) const;
    StremioPendingIntent *intentFor(const QString &operationId);
    void setStatus(const QString &status);
    void finishRun();

    StremioSyncOptions m_options;
    StremioState m_stateStore;
    QNetworkAccessManager m_network;
    QTcpServer m_callbackServer;
    QPointer<QTcpSocket> m_callbackSocket;
    QByteArray m_callbackBuffer;
    QPointer<QNetworkReply> m_identityReply;
    QTimer m_authTimeout;
    QTimer m_retryTimer;
    QString m_callbackPath;
    QString m_profileId;
    QString m_statePath;
    quint64 m_bindingGeneration = 0;
    quint64 m_authAttempt = 0;
    quint64 m_completedRun = 0;
    QString m_status = QStringLiteral("notConnected");
    bool m_hasUsableCredential = false;
    StremioPersistentState m_state;
    QHash<quint64, QList<std::function<void(bool)>>> m_persistContinuations;
};
