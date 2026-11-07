#pragma once

#include "AccountCredentialStore.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>

#include <functional>

class QNetworkReply;

// Device-local Nuvio session. Sign-in is staged until the caller confirms it,
// allowing first-run cancellation without replacing a saved connection.
class NuvioApi final : public QObject {
public:
    struct Reply {
        int status = 0;
        QJsonValue data;
        QByteArray retryAfter;
    };
    using Completion = std::function<void(Reply)>;
    using Transport = std::function<void(const QString &path, bool get,
        const QJsonObject &body, const QByteArray &bearer, Completion)>;

    explicit NuvioApi(AccountCredentialStore *vault, QObject *parent = nullptr,
                      Transport transport = {});
    ~NuvioApi() override;

    void activate(const QString &profileId);
    void signIn(const QString &email, const QString &password, Completion completion);
    bool saveSignIn(const QString &nuvioProfileId = {}, int nuvioProfileIndex = 0,
                    const std::function<bool()> &commitSetting = {});
    void cancelSignIn();
    void cancelPendingRequests();
    bool disconnectAccount();

    bool connected() const { return m_saved && m_session.has_value(); }
    bool needsReconnect() const { return m_paused; }
    QString accountId() const;
    QString selectedProfileId() const;
    int selectedProfileIndex() const;

    // All requests share one queue and one token refresh. Callbacks receive
    // only data responses or status codes, never a token response.
    void rpc(const QString &operation, const QJsonObject &body, Completion completion);
    void addons(int profileIndex, Completion completion);
    void progressForRemoval(int profileIndex, const QString &contentId, const QString &type,
        qint64 beforeOrAt, int offset, Completion completion);
    void verifyProfilePin(int profileIndex, const QString &pin, Completion completion);

private:
    struct Session {
        StoredNuvioCredential credential;
        QByteArray accessToken;
        qint64 accessExpiresAtUtcMs = 0;
    };

    enum class UnauthorizedRetry { Allowed, Used, Forbidden };

    struct Operation {
        QString path;
        bool get = false;
        QJsonObject body;
        Completion completion;
        UnauthorizedRetry unauthorizedRetry = UnauthorizedRetry::Allowed;
    };

    void resetRequests();
    void enqueue(Operation operation);
    void pump();
    void refresh();
    void pause();
    void send(const QString &path, bool get, const QJsonObject &body,
              const QByteArray &bearer, Completion completion, bool singleAttempt = false);
    void sendHttp(const QString &path, bool get, const QJsonObject &body,
                  const QByteArray &bearer, Completion completion, bool singleAttempt);
    std::optional<Session> decodeSession(const Reply &reply,
        const QString &expectedAccount = {}) const;

    AccountCredentialStore *m_vault;
    QNetworkAccessManager m_network;
    Transport m_transport;
    QPointer<QNetworkReply> m_reply;
    QString m_profileId;
    std::optional<Session> m_session;
    QQueue<Operation> m_operations;
    quint64 m_generation = 0;
    qint64 m_retryAt = 0;
    bool m_busy = false;
    bool m_saved = false;
    bool m_paused = false;
    bool m_savedPaused = false;
};
