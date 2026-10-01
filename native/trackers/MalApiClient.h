#pragma once

#include "MalHttpTransport.h"
#include "TrackerCredentialVault.h"
#include "account/ProfilePaths.h"

#include <QObject>
#include <QUrlQuery>

#include <functional>
#include <memory>
#include <optional>

class MalApiClient final : public QObject
{
    Q_OBJECT

public:
    using Completion = std::function<void(const MalApiResult &)>;

    MalApiClient(const ProfilePaths &profile,
                 const MalAuthConfiguration &configuration,
                 QObject *parent = nullptr);
    MalApiClient(const ProfilePaths &profile,
                 const MalAuthConfiguration &configuration,
                 TrackerCredentialVault *vault,
                 MalHttpTransport *transport,
                 QObject *parent = nullptr);

    bool available() const;

    void get(const QString &path,
             const QUrlQuery &query,
             Completion completion);
    void patchForm(const QString &path,
                 const QUrlQuery &form,
                 Completion completion);

    static std::optional<TrackerCredential> rotatedCredential(
        const TrackerCredential &prior,
        const MalTokenResult &token,
        qint64 nowMs);

    // Current official MAL documentation exposes token refresh but no
    // provider-side OAuth revocation endpoint. Disconnect is therefore a
    // local lifecycle operation: credentials and connection state are removed
    // from Colosseum, while the user can separately revoke the app in MAL.
    bool disconnectThenForget(const std::function<bool()> &disconnect);

private:
    using AccessCompletion =
        std::function<void(std::optional<QByteArray>, MalTransportError)>;

    void withAccessToken(bool forceRefresh, AccessCompletion completion);
    void finishRefresh(const MalTokenResult &result,
                       const TrackerCredential &prior);
    void getAttempt(const QString &path,
                    const QUrlQuery &query,
                    bool retried,
                    Completion completion);
    void putAttempt(const QString &path,
                    const QUrlQuery &form,
                    bool retried,
                    Completion completion);

    ProfilePaths m_profile;
    MalAuthConfiguration m_configuration;
    std::unique_ptr<TrackerCredentialVault> m_defaultVault;
    std::unique_ptr<MalHttpTransport> m_defaultTransport;
    TrackerCredentialVault *m_vault = nullptr;
    MalHttpTransport *m_transport = nullptr;
    bool m_refreshInFlight = false;
    QList<AccessCompletion> m_refreshWaiters;
};
