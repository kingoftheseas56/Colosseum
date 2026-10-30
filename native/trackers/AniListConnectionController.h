#pragma once

#include "AniListHttpTransport.h"
#include "AniListLoopbackServer.h"
#include "AniListProtocol.h"
#include "TrackerCredentialVault.h"
#include "account/ProfilePaths.h"

#include <QObject>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

class TrackerConnectionStore;

// Sign-in for AniList's implicit grant. Same public surface and phases as
// MalConnectionController, with two differences: there is no code exchange
// (the loopback hands over the token itself), and there is no refresh, so an
// expired or revoked token becomes the "reconnect_required" phase. Only the
// credential and the connection state are ever touched here; queued
// deliveries belong to the delivery store and are never read or deleted.
// Unlike MAL it takes no Sync Center pointer: the owner refreshes the Sync
// Center from stateChanged / connectionEstablished.
class AniListConnectionController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool moveAvailable READ moveAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY stateChanged)
    Q_PROPERTY(QString verificationUrl READ verificationUrl NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)

public:
    using BrowserOpen = std::function<bool(const QUrl &)>;
    using EntropySource = std::function<QByteArray(qsizetype)>;

    AniListConnectionController(const ProfilePaths &profile,
                                TrackerConnectionStore *connections,
                                QObject *parent = nullptr);
    AniListConnectionController(
        const ProfilePaths &profile,
        TrackerConnectionStore *connections,
        const std::optional<AniListAuthConfiguration> &configuration,
        TrackerCredentialVault *vault,
        AniListHttpTransport *transport,
        AniListLoopbackServer *loopback,
        BrowserOpen browserOpen,
        EntropySource entropySource,
        QObject *parent = nullptr);
    ~AniListConnectionController() override;

    bool available() const;
    bool busy() const;
    bool moveAvailable() const { return false; }
    QString phase() const { return m_phase; }
    QString userCode() const { return {}; }
    QString verificationUrl() const
    {
        return m_verificationUrl.toString(QUrl::FullyEncoded);
    }
    QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE bool beginConnection(const QString &providerKey);
    Q_INVOKABLE bool openApprovalPage();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dismiss();

    // Local disconnect: marks the connection disconnected and deletes the
    // vault credential. AniList documents no revocation call for this flow.
    bool disconnect();

    // True when the stored token is missing or expired, or a caller reported
    // a 401. The connection record is left alone so nothing queued is lost.
    bool reconnectRequired() const;
    // Called by the sync runtime (S4) when the API client returns 401.
    void noteAuthenticationFailure();

    bool prepareForProfileDeactivation();

signals:
    void stateChanged();
    void connectionEstablished(const QString &providerKey);

private:
    static QByteArray systemEntropy(qsizetype bytes);
    void handleToken(const QByteArray &accessToken, qint64 expiresInSeconds);
    void handleSignInFailure(const QString &error);
    void handleViewer(const AniListApiResult &result,
                      const QByteArray &accessToken,
                      qint64 expiresInSeconds);
    void finalizeCredential(const AniListViewer &viewer,
                            const QByteArray &accessToken,
                            qint64 expiresInSeconds);
    bool rollbackVault();
    bool restoreOrClearCredential();
    void setPresentation(const QString &phase,
                         const QString &message,
                         const QUrl &url = {});
    QString messageForTransport(AniListTransportError error) const;

    ProfilePaths m_profile;
    TrackerConnectionStore *m_connections = nullptr;
    std::optional<AniListAuthConfiguration> m_configuration;

    std::unique_ptr<TrackerCredentialVault> m_defaultVault;
    std::unique_ptr<AniListHttpTransport> m_defaultTransport;
    std::unique_ptr<AniListLoopbackServer> m_defaultLoopback;
    TrackerCredentialVault *m_vault = nullptr;
    AniListHttpTransport *m_transport = nullptr;
    AniListLoopbackServer *m_loopback = nullptr;
    BrowserOpen m_browserOpen;
    EntropySource m_entropySource;

    QString m_phase = QStringLiteral("idle");
    QString m_statusMessage;
    QUrl m_verificationUrl;
    QByteArray m_state;
    std::optional<TrackerCredential> m_priorCredential;
    bool m_rollbackPending = false;
    bool m_authenticationFailed = false;
};
