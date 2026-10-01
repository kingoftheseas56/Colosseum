#pragma once

#include "TraktAuth.h"
#include "TrackerCredentialVault.h"
#include "account/ProfilePaths.h"

#include <QObject>
#include <QTimer>

#include <memory>
#include <optional>

class TraktHttpTransport;
class TrackerClock;
class TrackerConnectionStore;
class TrackerSyncCenterModel;
class TrackerSystemBrowser;
class DefaultTrackerSystemBrowser;

class TraktConnectionController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool moveAvailable READ moveAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY stateChanged)
    Q_PROPERTY(QString verificationUrl READ verificationUrl NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(QString providerName READ providerName CONSTANT)

public:
    TraktConnectionController(const ProfilePaths &profile, TrackerConnectionStore *connections,
                              TrackerSyncCenterModel *syncCenter, QObject *parent = nullptr);
    TraktConnectionController(const ProfilePaths &profile, TrackerConnectionStore *connections,
                              TrackerSyncCenterModel *syncCenter,
                              const std::optional<TraktAuthConfiguration> &configuration,
                              TrackerCredentialVault *vault, TrackerSystemBrowser *browser,
                              TraktAuthTransport *transport, TrackerClock *clock, QObject *parent = nullptr);
    ~TraktConnectionController() override;

    bool available() const;
    bool busy() const;
    bool moveAvailable() const;
    QString phase() const;
    QString userCode() const;
    QString verificationUrl() const;
    QString statusMessage() const;
    QString providerName() const { return QStringLiteral("Trakt"); }

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
    class SystemClock;
    void inspectSession();
    void finalizeCredential(const TraktAuthSnapshot &snapshot);
    bool restoreOrClearCredential();
    bool rollbackVault();
    void setPresentation(const QString &phase, const QString &message,
                         const QString &userCode = {}, const QUrl &verificationUrl = {});
    QString messageForError(TraktAuthError error) const;

    TrackerConnectionStore *m_connections = nullptr;
    TrackerSyncCenterModel *m_syncCenter = nullptr;
    std::optional<TraktAuthConfiguration> m_configuration;
    std::unique_ptr<WindowsTrackerCredentialVault> m_defaultVault;
    std::unique_ptr<DefaultTrackerSystemBrowser> m_defaultBrowser;
    std::unique_ptr<TraktHttpTransport> m_defaultTransport;
    std::unique_ptr<SystemClock> m_defaultClock;
    TrackerCredentialVault *m_vault = nullptr;
    TrackerSystemBrowser *m_browser = nullptr;
    TraktAuthTransport *m_transport = nullptr;
    TrackerClock *m_clock = nullptr;
    std::unique_ptr<TraktAuthSession> m_session;
    QTimer m_pollTimer;
    ProfilePaths m_profile;
    std::optional<TrackerCredential> m_priorCredential;
    QString m_claimingProfileId;
    bool m_rollbackPending = false;
    QString m_phase = QStringLiteral("idle");
    QString m_userCode;
    QUrl m_verificationUrl;
    QString m_statusMessage;
    bool m_approvalOpened = false;
    bool m_active = true;
};
