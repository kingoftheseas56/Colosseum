#include "SimklConnectionController.h"

#include "SimklHttpTransport.h"
#include "TrackerConnectionStore.h"
#include "TrackerLifecycleCoordinator.h"
#include "TrackerSyncCenterModel.h"
#include "account/ProfilePaths.h"

#include <QDateTime>
#include <QUrlQuery>

namespace {

TrackerProviderCapabilities simklCapabilities()
{
    return TrackerProviderCapability::ReadHistory
        | TrackerProviderCapability::ReadProgress
        | TrackerProviderCapability::WriteProgress
        | TrackerProviderCapability::WriteCompletion
        | TrackerProviderCapability::Scrobble;
}

bool sameCredential(const TrackerCredential &left, const TrackerCredential &right)
{
    return left.slot.profileId == right.slot.profileId
        && left.slot.providerId == right.slot.providerId
        && left.slot.remoteAccountId == right.slot.remoteAccountId
        && left.accessToken == right.accessToken
        && left.refreshToken == right.refreshToken
        && left.accessTokenExpiresAtMs == right.accessTokenExpiresAtMs
        && left.refreshTokenExpiresAtMs == right.refreshTokenExpiresAtMs
        && left.grantedScopes == right.grantedScopes;
}

} // namespace

class SimklConnectionController::SystemClock final : public TrackerClock
{
public:
    qint64 nowMs() const override { return QDateTime::currentMSecsSinceEpoch(); }
};

SimklConnectionController::SimklConnectionController(
    const ProfilePaths &profile,
    TrackerConnectionStore *connections,
    TrackerSyncCenterModel *syncCenter,
    QObject *parent)
    : SimklConnectionController(profile, connections, syncCenter,
                                simklProductionConfiguration(), nullptr, nullptr,
                                nullptr, nullptr, parent)
{
}

SimklConnectionController::SimklConnectionController(
    const ProfilePaths &profile,
    TrackerConnectionStore *connections,
    TrackerSyncCenterModel *syncCenter,
    const std::optional<SimklAuthConfiguration> &configuration,
    TrackerCredentialVault *vault,
    TrackerSystemBrowser *browser,
    SimklAuthTransport *transport,
    TrackerClock *clock,
    QObject *parent)
    : QObject(parent),
      m_connections(connections),
      m_syncCenter(syncCenter),
      m_configuration(configuration),
      m_defaultVault(vault ? nullptr : std::make_unique<WindowsTrackerCredentialVault>()),
      m_defaultBrowser(browser ? nullptr : std::make_unique<DefaultTrackerSystemBrowser>()),
      m_defaultTransport(transport ? nullptr : std::make_unique<SimklHttpTransport>()),
      m_defaultClock(clock ? nullptr : std::make_unique<SystemClock>()),
      m_vault(vault ? vault : m_defaultVault.get()),
      m_browser(browser ? browser : m_defaultBrowser.get()),
      m_transport(transport ? transport : m_defaultTransport.get()),
      m_clock(clock ? clock : m_defaultClock.get()),
      m_profile(profile)
{
    setObjectName(QStringLiteral("simklConnectionController"));
    m_pollTimer.setInterval(250);
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this,
            &SimklConnectionController::inspectSession);
}

SimklConnectionController::~SimklConnectionController()
{
    // Shutdown and other teardown paths that skip the deactivation gate must
    // not silently discard a pending rollback: the prior credential exists
    // only in this controller's memory at that point.
    if (m_rollbackPending)
        rollbackVault();
}

bool SimklConnectionController::prepareForProfileDeactivation()
{
    m_pollTimer.stop();
    if (m_session)
        m_session->cancel();
    m_session.reset();
    if (!m_rollbackPending)
        return true;
    // False blocks the profile switch, exactly like an unfinalizable
    // scrobble session; the attention presentation explains the state.
    return restoreOrClearCredential();
}

bool SimklConnectionController::available() const
{
    return m_configuration.has_value() && m_vault->isAvailable()
        && m_connections && m_connections->healthy() && !m_profile.profileId().isEmpty();
}

bool SimklConnectionController::busy() const
{
    return m_phase == QLatin1String("requesting")
        || m_phase == QLatin1String("awaiting_approval")
        || m_phase == QLatin1String("connecting")
        || m_phase == QLatin1String("moving");
}

bool SimklConnectionController::moveAvailable() const
{
    return m_phase == QLatin1String("move_available") && !m_claimingProfileId.isEmpty();
}

QString SimklConnectionController::phase() const { return m_phase; }
QString SimklConnectionController::userCode() const { return m_userCode; }
QString SimklConnectionController::verificationUrl() const
{
    return m_verificationUrl.toString();
}
QString SimklConnectionController::statusMessage() const { return m_statusMessage; }

bool SimklConnectionController::beginConnection(const QString &providerKey)
{
    if (providerKey.trimmed().toLower() != QLatin1String("simkl") || !available()) {
        setPresentation(QStringLiteral("attention"),
                        QStringLiteral("SIMKL is not configured for this build."));
        return false;
    }
    if (busy())
        return false;
    // A new ceremony must never discard an unresolved rollback: the vault
    // would silently adopt the fresh credential as the new "prior" state and
    // the true previous credential (held only here) would be lost. Resolve
    // the rollback first; only a completed rollback may start over.
    if (m_rollbackPending && !restoreOrClearCredential())
        return false;
    const auto existing = m_connections->connection(TrackerProviderId::Simkl);
    if (existing && existing->state == TrackerConnectionState::Connected) {
        setPresentation(QStringLiteral("connected"),
                        QStringLiteral("SIMKL is already connected to this profile."));
        return true;
    }

    // The ceremony may legitimately replace this profile's credential for the
    // same account. If attaching the fresh connection then fails, the vault
    // must end up holding exactly what it held before, so retain it now.
    m_priorCredential = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Simkl);
    m_claimingProfileId.clear();
    // The auth session can write a fresh credential before the next
    // controller poll sees CredentialReady. Own rollback from the start so
    // deactivation or teardown in that interval restores this snapshot.
    m_rollbackPending = true;
    m_session = std::make_unique<SimklAuthSession>(
        SimklAuthBinding{m_profile.profileId(), 1}, *m_configuration, m_vault,
        m_browser, m_transport, nullptr, m_clock, m_connections);
    m_approvalOpened = false;
    setPresentation(QStringLiteral("requesting"),
                    QStringLiteral("Requesting a secure approval code from SIMKL…"));
    if (!m_session->beginDevicePinAuthorization()) {
        inspectSession();
        return false;
    }
    m_pollTimer.start();
    return true;
}

bool SimklConnectionController::openApprovalPage()
{
    if (m_verificationUrl.isEmpty())
        return false;
    return m_browser->open(m_verificationUrl);
}

void SimklConnectionController::cancel()
{
    m_pollTimer.stop();
    if (m_session)
        m_session->cancel();
    m_session.reset();
    // Declining (or abandoning) a move must not leave a replaced credential
    // behind: while a rollback is pending, leaving requires it to complete.
    // A failed rollback keeps the attention presentation so the user sees it
    // and the next dismiss retries the rollback instead of discarding it.
    if (m_rollbackPending && !restoreOrClearCredential())
        return;
    m_claimingProfileId.clear();
    m_priorCredential.reset();
    m_rollbackPending = false;
    setPresentation(QStringLiteral("cancelled"),
                    QStringLiteral("SIMKL connection cancelled."));
}

void SimklConnectionController::dismiss()
{
    if (busy())
        return;
    m_session.reset();
    if (m_rollbackPending && !restoreOrClearCredential())
        return;
    m_claimingProfileId.clear();
    m_priorCredential.reset();
    m_rollbackPending = false;
    setPresentation(QStringLiteral("idle"), QString());
}

void SimklConnectionController::inspectSession()
{
    if (!m_session)
        return;
    m_session->expireIfDue();
    SimklAuthSnapshot snapshot = m_session->snapshot();
    switch (snapshot.phase) {
    case SimklAuthPhase::AwaitingDeviceApproval: {
        QUrl complete = snapshot.verificationUri;
        QUrlQuery query(complete);
        query.addQueryItem(QStringLiteral("user_code"), snapshot.userCode);
        complete.setQuery(query);
        setPresentation(QStringLiteral("awaiting_approval"),
                        QStringLiteral("Approve Colosseum in the SIMKL page, then return here."),
                        snapshot.userCode, complete);
        if (!m_approvalOpened) {
            m_approvalOpened = true;
            m_browser->open(complete);
        }
        m_session->pollDevicePin();
        break;
    }
    case SimklAuthPhase::ExchangingToken:
    case SimklAuthPhase::VerifyingIdentity:
        setPresentation(QStringLiteral("connecting"),
                        QStringLiteral("Finishing the secure SIMKL connection…"),
                        m_userCode, m_verificationUrl);
        break;
    case SimklAuthPhase::CredentialReady:
        finalizeCredential(snapshot);
        break;
    case SimklAuthPhase::TimedOut:
        m_pollTimer.stop();
        setPresentation(QStringLiteral("expired"),
                        QStringLiteral("The SIMKL approval code expired. Start again for a new code."));
        break;
    case SimklAuthPhase::NeedsAttention:
        m_pollTimer.stop();
        setPresentation(QStringLiteral("attention"), messageForError(snapshot.error));
        break;
    case SimklAuthPhase::Cancelled:
        m_pollTimer.stop();
        setPresentation(QStringLiteral("cancelled"),
                        QStringLiteral("SIMKL connection cancelled."));
        break;
    default:
        break;
    }
}

void SimklConnectionController::finalizeCredential(const SimklAuthSnapshot &snapshot)
{
    m_pollTimer.stop();
    m_session.reset();
    QString error;
    if (!m_connections->refresh(&error)) {
        if (!restoreOrClearCredential())
            return; // The rollback failure message stands; nothing overwrites it.
        setPresentation(QStringLiteral("attention"), error);
        return;
    }
    // The same external account may already be actively bound to another
    // profile on this device. That is the agreed "Move connection to this
    // profile" situation, not a failure. The freshly approved credential
    // stays in this profile's vault: the move consumes it even when the
    // source profile's own token has expired. If the user declines instead,
    // cancel()/dismiss() rolls the vault back to its pre-ceremony state.
    QString claimingProfileId;
    const auto claim = m_connections->externalAccountClaim(
        TrackerProviderId::Simkl, snapshot.remoteAccountId, &claimingProfileId, &error);
    if (claim == TrackerConnectionStore::ExternalAccountClaim::Claimed
        && !claimingProfileId.isEmpty() && claimingProfileId != m_profile.profileId()) {
        m_claimingProfileId = claimingProfileId;
        // The vault now holds the fresh credential in place of the prior one;
        // leaving this state without a move requires a rollback.
        m_rollbackPending = true;
        setPresentation(QStringLiteral("move_available"),
                        QStringLiteral("This SIMKL account is connected to another profile on this device."));
        return;
    }
    if (claim == TrackerConnectionStore::ExternalAccountClaim::Indeterminate) {
        if (!restoreOrClearCredential())
            return;
        setPresentation(QStringLiteral("attention"),
                        error.isEmpty() ? QStringLiteral("Tracker account ownership could not be verified.")
                                        : error);
        return;
    }

    const quint64 generation = m_connections->nextConnectionGeneration(
        TrackerProviderId::Simkl);
    if (generation == 0 || !m_connections->upsert(
            {TrackerProviderId::Simkl, snapshot.remoteAccountId, generation,
             m_clock->nowMs(), simklCapabilities(), TrackerConnectionState::Connected},
            &error)) {
        if (!restoreOrClearCredential())
            return;
        setPresentation(QStringLiteral("attention"),
                        error.isEmpty() ? QStringLiteral("SIMKL could not be attached to this profile.")
                                        : error);
        return;
    }
    m_priorCredential.reset();
    m_claimingProfileId.clear();
    m_rollbackPending = false;
    if (m_syncCenter)
        m_syncCenter->refresh();
    setPresentation(QStringLiteral("connected"),
                    QStringLiteral("SIMKL is connected to this Colosseum profile."));
    emit connectionEstablished(QStringLiteral("simkl"));
}

bool SimklConnectionController::moveConnectionToThisProfile()
{
    if (!moveAvailable() || !available())
        return false;
    // The claim scan names profile directories: account profiles by UUID,
    // the local-only profile by its fixed "local" directory name, which
    // ProfilePaths::account() would reject.
    const auto source = m_claimingProfileId == QLatin1String("local")
        ? std::optional<ProfilePaths>(ProfilePaths::localOnly(m_profile.appDataRoot()))
        : ProfilePaths::account(m_claimingProfileId, m_profile.appDataRoot());
    if (!source) {
        setPresentation(QStringLiteral("attention"),
                        QStringLiteral("The profile holding this SIMKL account could not be opened."));
        return false;
    }
    setPresentation(QStringLiteral("moving"),
                    QStringLiteral("Moving the SIMKL connection to this profile…"));
    QString error;
    if (!TrackerLifecycleCoordinator::moveConnection(
            *source, m_profile, TrackerProviderId::Simkl, *m_vault,
            m_clock->nowMs(), &error)) {
        // The fresh credential stays in the vault: the failure may be
        // transient, and it is a valid grant for the same account. Leaving
        // this state still requires a rollback, so a later dismiss restores
        // the prior credential rather than discarding it.
        m_rollbackPending = true;
        setPresentation(QStringLiteral("attention"),
                        error.isEmpty() ? QStringLiteral("The SIMKL connection could not be moved.")
                                        : error);
        return false;
    }
    m_claimingProfileId.clear();
    m_priorCredential.reset();
    m_rollbackPending = false;
    if (!m_connections->refresh(&error))
        setPresentation(QStringLiteral("attention"), error);
    else {
        if (m_syncCenter)
            m_syncCenter->refresh();
        setPresentation(QStringLiteral("connected"),
                        QStringLiteral("SIMKL is connected to this Colosseum profile."));
        emit connectionEstablished(QStringLiteral("simkl"));
    }
    return true;
}

bool SimklConnectionController::restoreOrClearCredential()
{
    // finalizeCredential and the decline paths run after the ceremony already
    // stored a fresh credential. Rolling back must leave the vault exactly as
    // it was before the ceremony started: the retained prior credential when
    // one existed, or an empty slot when it did not. The caller must surface
    // a false return; the vault state is then unknown and needs attention.
    if (rollbackVault()) {
        m_rollbackPending = false;
        return true;
    }
    m_rollbackPending = true;
    setPresentation(QStringLiteral("attention"),
                    QStringLiteral("Windows could not finalize the SIMKL connection change. "
                                   "Keep this panel open and try closing again; if it keeps "
                                   "failing, reconnect SIMKL from Connections."));
    return false;
}

bool SimklConnectionController::rollbackVault()
{
    if (m_priorCredential) {
        const auto current = m_vault->loadForProfile(m_profile.profileId(),
                                                     TrackerProviderId::Simkl);
        if (current && sameCredential(*current, *m_priorCredential))
            return true;
        return m_vault->saveAndVerify(*m_priorCredential);
    }
    return m_vault->clearForProfile(m_profile.profileId(), TrackerProviderId::Simkl);
}

void SimklConnectionController::setPresentation(const QString &newPhase,
                                                 const QString &message,
                                                 const QString &code,
                                                 const QUrl &url)
{
    const bool changed = m_phase != newPhase || m_statusMessage != message
        || m_userCode != code || m_verificationUrl != url;
    m_phase = newPhase;
    m_statusMessage = message;
    m_userCode = code;
    m_verificationUrl = url;
    if (changed)
        emit stateChanged();
}

QString SimklConnectionController::messageForError(SimklAuthError error) const
{
    switch (error) {
    case SimklAuthError::MissingPermission:
        return QStringLiteral("SIMKL did not grant the read and write permissions Colosseum requested.");
    case SimklAuthError::AccountChangeRequired:
        return QStringLiteral("Disconnect the current SIMKL account before connecting another one.");
    case SimklAuthError::CredentialStore:
    case SimklAuthError::CredentialRecoveryFailed:
        return QStringLiteral("Windows could not safely store the SIMKL connection. Try again.");
    case SimklAuthError::Expired:
        return QStringLiteral("The SIMKL approval expired. Start again for a new code.");
    case SimklAuthError::RateLimited:
        return QStringLiteral("SIMKL asked Colosseum to wait. Try connecting again shortly.");
    case SimklAuthError::Transport:
        return QStringLiteral("Colosseum could not reach SIMKL. Check the connection and try again.");
    case SimklAuthError::MissingStableAccount:
        return QStringLiteral("SIMKL did not return a stable account identity.");
    default:
        return QStringLiteral("SIMKL could not complete the connection. Try again.");
    }
}
