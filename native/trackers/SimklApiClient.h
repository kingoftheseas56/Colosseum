#pragma once

#include "SimklAuth.h"
#include "TrackerCredentialVault.h"
#include "TrackerScrobbleRuntime.h"

#include "account/ProfilePaths.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrlQuery>

#include <functional>
#include <optional>

class QNetworkReply;

struct SimklApiResult {
    int statusCode = 0;
    QJsonDocument document;
    bool networkFailure = false;
    bool payloadTooLarge = false;
    qint64 retryAfterMs = 0;

    bool succeeded() const { return statusCode >= 200 && statusCode < 300; }
};

// One profile-scoped owner for the registered SIMKL grant. It is the only
// production class allowed to refresh the grant, which prevents two callers
// from invalidating each other's access tokens.
class SimklApiClient final : public QObject, public SimklScrobbleTransport
{
    Q_OBJECT

public:
    using Completion = std::function<void(const SimklApiResult &)>;

    SimklApiClient(const ProfilePaths &profile,
                   SimklAuthConfiguration configuration,
                   QObject *parent = nullptr);

    bool available() const;
    void get(const QString &path, const QUrlQuery &query, Completion completion);
    void post(const QString &path,
              const QUrlQuery &query,
              const QJsonDocument &body,
              Completion completion);
    // The client's shared write cadence: reserves the next one-per-second
    // POST slot and runs `fire` at that instant. Public for focused cadence
    // verification; production callers are post/revoke/refresh only.
    void postAtCadence(std::function<void()> fire);
    void schedulePostAt(qint64 slotAtMs, std::function<void()> fire);    // Best-effort provider-side revocation of this profile's grant. SIMKL's
    // revoke endpoint always answers 200 and carries no outcome signal, so the
    // result is intentionally not surfaced; the callback only observes the
    // exchange finished. Never blocks: safe to call on the GUI thread.
    void revokeGrantAsync(const std::function<void()> &finished = {});
    // Runs the local disconnect first, then fires the async best-effort revoke
    // with the refresh token captured before the local credential is removed.
    bool disconnectThenRevoke(const std::function<bool()> &disconnect);

    void send(const TrackerScrobbleIntent &intent, SendCompletion completion) override;
    void readback(const TrackerScrobbleIntent &intent,
                  ReadbackCompletion completion) override;

private:
    using CredentialCompletion = std::function<void(std::optional<TrackerCredential>)>;

    void ensureCredential(bool forceRefresh, CredentialCompletion completion);
    void refreshCredential(const TrackerCredential &credential);
    void finishRefresh(const SimklApiResult &result,
                       const TrackerCredential &previous);
    void request(const QByteArray &method,
                 const QString &path,
                 QUrlQuery query,
                 const QJsonDocument &body,
                 bool retriedAfterUnauthorized,
                 Completion completion);
    void issue(const QByteArray &method,
               const QString &path,
               QUrlQuery query,
               const QJsonDocument &body,
               const TrackerCredential &credential,
               bool retriedAfterUnauthorized,
               Completion completion);
    void issueNow(const QByteArray &method,
                  const QString &path,
                  const QUrlQuery &query,
                  const QJsonDocument &body,
                  const TrackerCredential &credential,
                  bool retriedAfterUnauthorized,
                  Completion completion);
    void finishReply(QNetworkReply *reply,
                     const std::shared_ptr<QByteArray> &buffer,
                     const std::shared_ptr<bool> &tooLarge,
                     Completion completion);
    QUrl endpoint(const QString &path, QUrlQuery query) const;
    QString userAgent() const;
    void revokeTokenAsync(const QByteArray &refreshToken,
                          const std::function<void()> &finished);

    static QJsonObject scrobbleBody(const TrackerScrobbleIntent &intent);
    static bool readbackContains(const QJsonDocument &document,
                                 const TrackerScrobbleIntent &intent);

    ProfilePaths m_profile;
    SimklAuthConfiguration m_configuration;
    WindowsTrackerCredentialVault m_vault;
    QNetworkAccessManager m_network;
    bool m_refreshing = false;
    QList<CredentialCompletion> m_refreshWaiters;
    qint64 m_nextPostAtMs = 0;
    qint64 m_lastPostSentAtMs = 0;
};
