#pragma once

#include "TraktAuth.h"
#include "TrackerScrobbleRuntime.h"
#include "account/ProfilePaths.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QHash>
#include <memory>

#include <functional>
#include <optional>

class QNetworkReply;
class QNetworkRequest;

struct TraktApiResult {
    int statusCode = 0;
    QJsonDocument document;
    bool networkFailure = false;
    bool payloadTooLarge = false;
    bool cancelled = false;
    qint64 retryAfterMs = 0;
    int page = 0;
    int pageCount = 0;
    bool succeeded() const { return !cancelled && !networkFailure && !payloadTooLarge && statusCode >= 200 && statusCode < 300; }
};

using TraktScrobbleSendResult = SimklScrobbleSendResult;
using TraktScrobbleReadbackResult = SimklScrobbleReadbackResult;

class TraktApiTransport {
public:
    using Completion = std::function<void(const TraktApiResult &)>;
    virtual ~TraktApiTransport() = default;
    virtual void request(const QByteArray &method, const QNetworkRequest &request,
                         const QJsonDocument &body, Completion completion) = 0;
};

class TraktApiClient final : public QObject, public SimklScrobbleTransport
{
    Q_OBJECT
public:
    using Completion = std::function<void(const TraktApiResult &)>;
    using SendCompletion = std::function<void(TraktScrobbleSendResult)>;
    using ReadbackCompletion = std::function<void(TraktScrobbleReadbackResult)>;
    TraktApiClient(const ProfilePaths &profile, TraktAuthConfiguration configuration, QObject *parent = nullptr);
    TraktApiClient(const ProfilePaths &profile, TraktAuthConfiguration configuration,
                   TrackerCredentialVault *vault, TraktApiTransport *transport,
                   TrackerClock *clock, QObject *parent = nullptr);
    bool available() const;
    bool credentialAccountMatches(const QString &remoteAccountId) const;
    void deactivate();
    void resetForConnection();
    void get(const QString &path, const QUrlQuery &query, Completion completion);
    void post(const QString &path, const QJsonDocument &body, Completion completion);
    void revokeGrantAsync(const std::function<void()> &finished = {});
    bool disconnectThenRevoke(const std::function<bool()> &disconnect);
    void send(const TrackerScrobbleIntent &intent, SendCompletion completion) override;
    void readback(const TrackerScrobbleIntent &intent, ReadbackCompletion completion) override;

private:
    using CredentialCompletion = std::function<void(std::optional<TrackerCredential>)>;
    void ensureCredential(bool forceRefresh, CredentialCompletion completion);
    void refreshCredential(const TrackerCredential &credential);
    void finishRefresh(const TraktApiResult &result, const TrackerCredential &previous);
    void request(const QByteArray &method, const QString &path, QUrlQuery query,
                 const QJsonDocument &body, bool retriedAfterUnauthorized, Completion completion);
    void issue(const QByteArray &method, const QString &path, const QUrlQuery &query,
               const QJsonDocument &body, const TrackerCredential &credential,
               bool retriedAfterUnauthorized, Completion completion);
    void brokerPost(const QString &path, const QJsonObject &body, Completion completion);
    void finishReply(QNetworkReply *reply, const std::shared_ptr<QByteArray> &buffer,
                     const std::shared_ptr<bool> &tooLarge, Completion completion);
    QUrl apiEndpoint(const QString &path, const QUrlQuery &query = {}) const;
    QUrl brokerEndpoint(const QString &path) const;
    QNetworkRequest apiRequest(const QUrl &url, const TrackerCredential &credential) const;
    QNetworkRequest brokerRequest(const QUrl &url) const;
    void revokeAccessTokenAsync(const QByteArray &accessToken, const std::function<void()> &finished);
    static bool playbackContains(const QJsonDocument &document, const TrackerScrobbleIntent &intent);
    static bool historyContains(const QJsonDocument &document, const TrackerScrobbleIntent &intent);

    ProfilePaths m_profile;
    TraktAuthConfiguration m_configuration;
    qint64 nowMs() const;
    void dispatch(const QByteArray &method, const QNetworkRequest &request,
                  const QJsonDocument &body, Completion completion);
    std::unique_ptr<TrackerCredentialVault> m_defaultVault;
    TrackerCredentialVault *m_vault = nullptr;
    TraktApiTransport *m_transport = nullptr;
    TrackerClock *m_clock = nullptr;
    bool m_active = true;
    bool m_credentialFailure = false;
    quint64 m_generation = 0;
    quint64 m_nextRequestId = 0;
    QHash<quint64, Completion> m_pendingRequests;
    QNetworkAccessManager m_network;
    bool m_refreshing = false;
    QList<CredentialCompletion> m_refreshWaiters;
};
