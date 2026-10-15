#pragma once

#include "SimklAuth.h"
#include "TrackerCredentialVault.h"
#include "account/ProfilePaths.h"

#include <QObject>
#include <QTimer>

#include <memory>
#include <optional>

class SimklHttpTransport;
class TrackerClock;
class TrackerConnectionStore;
class TrackerSyncCenterModel;
class TrackerSystemBrowser;

// QML-safe owner for the SIMKL connection ceremony. It exposes the temporary
// approval code and status only; device codes, access tokens, refresh tokens,
// and the remote account id never cross the native boundary.
class SimklConnectionController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool moveAvailable READ moveAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY stateChanged)
    Q_PROPERTY(QString verificationUrl READ verificationUrl NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)

public:
    SimklConnectionController(const ProfilePaths &profile,
                              TrackerConnectionStore *connections,
                              TrackerSyncCenterModel *syncCenter,
                              QObject *parent = nullptr);
    // Full-dependency construction: deterministic tests inject a fake vault,
    // browser, transport, and clock instead of the production defaults.
    SimklConnectionController(const ProfilePaths &profile,
                              TrackerConnectionStore *connections,
                              TrackerSyncCenterModel *syncCenter,
                              const std::optional<SimklAuthConfiguration> &configuration,
                              TrackerCredentialVault *vault,
                              TrackerSystemBrowser *browser,
                              SimklAuthTransport *transport,
                              TrackerClock *clock,
                              QObject *parent = nullptr);
    ~SimklConnectionController() override;

    bool available() const;
    bool busy() const;
    bool moveAvailable() const;
    QString phase() const;
    QString userCode() const;
    QString verificationUrl() const;
    QString statusMessage() const;

    Q_INVOKABLE bool beginConnection(const QString &providerKey);
    Q_INVOKABLE bool openApprovalPage();
    Q_INVOKABLE bool moveConnectionToThisProfile();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dismiss();
    // Profile-switch gate, mirroring the scrobble runtime's contract: cancels
    // an in-flight ceremony and completes any pending credential rollback.
    // False means the switch must not proceed (the vault state is unresolved).
    bool prepareForProfileDeactivation();

signals:
    void stateChanged();
    void connectionEstablished(const QString &providerKey);

private:
    class SystemClock;

    void inspectSession();
    void finalizeCredential(const SimklAuthSnapshot &snapshot);
    // True when the vault is back in its exact pre-ceremony state. False
    // leaves an attention presentation that callers must not overwrite.
    bool restoreOrClearCredential();
    // Vault-only rollback with no presentation side effects; safe to call
    // from the destructor where emitting state changes is meaningless.
    bool rollbackVault();
    void setPresentation(const QString &phase,
                         const QString &message,
                         const QString &userCode = {},
                         const QUrl &verificationUrl = {});
    QString messageForError(SimklAuthError error) const;

    TrackerConnectionStore *m_connections = nullptr;
    TrackerSyncCenterModel *m_syncCenter = nullptr;
    std::optional<SimklAuthConfiguration> m_configuration;
    std::unique_ptr<WindowsTrackerCredentialVault> m_defaultVault;
    std::unique_ptr<DefaultTrackerSystemBrowser> m_defaultBrowser;
    std::unique_ptr<SimklHttpTransport> m_defaultTransport;
    std::unique_ptr<SystemClock> m_defaultClock;
    TrackerCredentialVault *m_vault = nullptr;
    TrackerSystemBrowser *m_browser = nullptr;
    SimklAuthTransport *m_transport = nullptr;
    TrackerClock *m_clock = nullptr;
    std::unique_ptr<SimklAuthSession> m_session;
    QTimer m_pollTimer;
    ProfilePaths m_profile;
    std::optional<TrackerCredential> m_priorCredential;
    QString m_claimingProfileId;
    // True while a ceremony may write or has written a fresh credential and
    // no attach or move has consumed it. Leaving the panel requires the vault
    // to match the prior state; a failed rollback keeps this set so the next
    // dismiss retries instead of discarding the prior state.
    // Teardown (deactivation gate or destructor) also honours it, so a
    // profile switch or shutdown cannot silently discard the prior
    // credential, which by then exists only in this controller's memory.
    bool m_rollbackPending = false;
    QString m_phase = QStringLiteral("idle");
    QString m_userCode;
    QUrl m_verificationUrl;
    QString m_statusMessage;
    bool m_approvalOpened = false;
};
