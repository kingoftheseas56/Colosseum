#pragma once

#include "MalHttpTransport.h"
#include "MalLoopbackServer.h"
#include "TrackerCredentialVault.h"
#include "account/ProfilePaths.h"

#include <QObject>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

class TrackerConnectionStore;
class TrackerSyncCenterModel;

class MalConnectionController final : public QObject
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

    MalConnectionController(const ProfilePaths &profile,
                            TrackerConnectionStore *connections,
                            TrackerSyncCenterModel *syncCenter,
                            QObject *parent = nullptr);
    MalConnectionController(
        const ProfilePaths &profile,
        TrackerConnectionStore *connections,
        TrackerSyncCenterModel *syncCenter,
        const std::optional<MalAuthConfiguration> &configuration,
        TrackerCredentialVault *vault,
        MalHttpTransport *transport,
        MalLoopbackServer *loopback,
        BrowserOpen browserOpen,
        EntropySource entropySource,
        QObject *parent = nullptr);
    ~MalConnectionController() override;

    bool available() const;
    bool busy() const;
    bool moveAvailable() const;
    QString phase() const { return m_phase; }
    QString userCode() const { return {}; }
    QString verificationUrl() const
    {
        return m_verificationUrl.toString(QUrl::FullyEncoded);
    }
    QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE bool beginConnection(const QString &providerKey);
    Q_INVOKABLE bool openApprovalPage();
    Q_INVOKABLE bool moveConnectionToThisProfile();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dismiss();

    bool prepareForProfileDeactivation();

signals:
    void stateChanged();
    void connectionEstablished(const QString &providerKey);

private:
    static QByteArray systemEntropy(qsizetype bytes);
    void handleCallback(const QByteArray &state,
                        const QByteArray &authorizationCode,
                        const QString &providerError);
    void handleToken(const MalTokenResult &result);
    void handleIdentity(const MalIdentityResult &result);
    void finalizeCredential(const QString &remoteAccountId);
    bool rollbackVault();
    bool restoreOrClearCredential();
    void setPresentation(const QString &phase,
                         const QString &message,
                         const QUrl &url = {});
    QString messageForTransport(MalTransportError error) const;

    ProfilePaths m_profile;
    TrackerConnectionStore *m_connections = nullptr;
    TrackerSyncCenterModel *m_syncCenter = nullptr;
    std::optional<MalAuthConfiguration> m_configuration;

    std::unique_ptr<TrackerCredentialVault> m_defaultVault;
    std::unique_ptr<MalHttpTransport> m_defaultTransport;
    std::unique_ptr<MalLoopbackServer> m_defaultLoopback;
    TrackerCredentialVault *m_vault = nullptr;
    MalHttpTransport *m_transport = nullptr;
    MalLoopbackServer *m_loopback = nullptr;
    BrowserOpen m_browserOpen;
    EntropySource m_entropySource;

    QString m_phase = QStringLiteral("idle");
    QString m_statusMessage;
    QUrl m_verificationUrl;
    QByteArray m_state;
    QByteArray m_codeVerifier;
    std::optional<MalTokenResult> m_token;
    std::optional<TrackerCredential> m_priorCredential;
    QString m_claimingProfileId;
    bool m_rollbackPending = false;
};
