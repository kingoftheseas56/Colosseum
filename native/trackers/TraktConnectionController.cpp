#include "TraktConnectionController.h"

#include "SimklAuth.h"
#include "TraktHttpTransport.h"
#include "TrackerConnectionStore.h"
#include "TrackerLifecycleCoordinator.h"
#include "TrackerSyncCenterModel.h"

#include <QDateTime>

namespace {
bool sameCredential(const TrackerCredential &a, const TrackerCredential &b)
{
    return a.slot.profileId == b.slot.profileId && a.slot.providerId == b.slot.providerId
        && a.slot.remoteAccountId == b.slot.remoteAccountId && a.accessToken == b.accessToken
        && a.refreshToken == b.refreshToken && a.accessTokenExpiresAtMs == b.accessTokenExpiresAtMs
        && a.refreshTokenExpiresAtMs == b.refreshTokenExpiresAtMs && a.grantedScopes == b.grantedScopes;
}
}

class TraktConnectionController::SystemClock final : public TrackerClock
{
public:
    qint64 nowMs() const override { return QDateTime::currentMSecsSinceEpoch(); }
};

TraktConnectionController::TraktConnectionController(const ProfilePaths &profile,
                                                     TrackerConnectionStore *connections,
                                                     TrackerSyncCenterModel *syncCenter,
                                                     QObject *parent)
    : TraktConnectionController(profile, connections, syncCenter, traktProductionConfiguration(),
                                nullptr, nullptr, nullptr, nullptr, parent)
{
}

TraktConnectionController::TraktConnectionController(
    const ProfilePaths &profile, TrackerConnectionStore *connections, TrackerSyncCenterModel *syncCenter,
    const std::optional<TraktAuthConfiguration> &configuration, TrackerCredentialVault *vault,
    TrackerSystemBrowser *browser, TraktAuthTransport *transport, TrackerClock *clock, QObject *parent)
    : QObject(parent), m_connections(connections), m_syncCenter(syncCenter), m_configuration(configuration),
      m_defaultVault(vault ? nullptr : std::make_unique<WindowsTrackerCredentialVault>()),
      m_defaultBrowser(browser ? nullptr : std::make_unique<DefaultTrackerSystemBrowser>()),
      m_defaultTransport(transport ? nullptr : std::make_unique<TraktHttpTransport>()),
      m_defaultClock(clock ? nullptr : std::make_unique<SystemClock>()),
      m_vault(vault ? vault : m_defaultVault.get()),
      m_browser(browser ? browser : m_defaultBrowser.get()),
      m_transport(transport ? transport : m_defaultTransport.get()),
      m_clock(clock ? clock : m_defaultClock.get()), m_profile(profile)
{
    m_pollTimer.setInterval(500);
    connect(&m_pollTimer, &QTimer::timeout, this, &TraktConnectionController::inspectSession);
}

TraktConnectionController::~TraktConnectionController()
{
    m_pollTimer.stop();
    if (m_session) m_session->cancel();
    m_session.reset();
    if (m_rollbackPending) rollbackVault();
}

bool TraktConnectionController::available() const
{
    return m_active && m_configuration.has_value() && traktConfigurationIsValid(*m_configuration) && m_vault && m_vault->isAvailable() && m_browser
        && m_transport && m_clock && m_connections
        && (m_profile.kind() == ProfilePaths::Kind::LocalOnly || m_profile.kind() == ProfilePaths::Kind::Account);
}

bool TraktConnectionController::busy() const
{
    return m_phase == QLatin1String("requesting") || m_phase == QLatin1String("awaiting_approval")
        || m_phase == QLatin1String("connecting") || m_phase == QLatin1String("moving");
}

bool TraktConnectionController::moveAvailable() const { return m_phase == QLatin1String("move_available"); }
QString TraktConnectionController::phase() const { return m_phase; }
QString TraktConnectionController::userCode() const { return m_userCode; }
QString TraktConnectionController::verificationUrl() const { return m_verificationUrl.toString(); }
QString TraktConnectionController::statusMessage() const { return m_statusMessage; }

bool TraktConnectionController::beginConnection(const QString &providerKey)
{
    if (providerKey != QLatin1String("trakt") || !available() || busy()) {
        setPresentation(QStringLiteral("attention"), QStringLiteral("Direct Trakt is not configured for this build."));
        return false;
    }
    QString error;
    if (!m_connections->refresh(&error)) { setPresentation(QStringLiteral("attention"), error); return false; }
    m_priorCredential = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
    m_claimingProfileId.clear();
    m_rollbackPending = true;
    m_session = std::make_unique<TraktAuthSession>(
        TraktAuthBinding{m_profile.profileId(), 1}, *m_configuration, m_vault, m_transport, m_clock, m_connections);
    m_approvalOpened = false;
    setPresentation(QStringLiteral("requesting"), QStringLiteral("Requesting a secure approval code from Trakt…"));
    if (!m_session->begin()) { inspectSession(); return false; }
    m_pollTimer.start();
    return true;
}

bool TraktConnectionController::openApprovalPage()
{
    return m_browser && !m_verificationUrl.isEmpty() && m_browser->open(m_verificationUrl);
}

void TraktConnectionController::inspectSession()
{
    if (!m_session) return;
    m_session->expireIfDue();
    const TraktAuthSnapshot snapshot = m_session->snapshot();
    switch (snapshot.phase) {
    case TraktAuthPhase::AwaitingApproval:
        setPresentation(QStringLiteral("awaiting_approval"),
                        QStringLiteral("Approve Colosseum on Trakt, then return here."),
                        snapshot.userCode, snapshot.verificationUrl);
        if (!m_approvalOpened) { m_approvalOpened = true; m_browser->open(snapshot.verificationUrl); }
        m_session->poll();
        break;
    case TraktAuthPhase::ExchangingToken:
    case TraktAuthPhase::VerifyingIdentity:
        setPresentation(QStringLiteral("connecting"), QStringLiteral("Finishing the secure Trakt connection…"),
                        m_userCode, m_verificationUrl);
        break;
    case TraktAuthPhase::CredentialReady:
        finalizeCredential(snapshot);
        break;
    case TraktAuthPhase::TimedOut:
        m_pollTimer.stop();
        setPresentation(QStringLiteral("expired"), QStringLiteral("The Trakt approval code expired. Start again for a new code."));
        break;
    case TraktAuthPhase::NeedsAttention:
        m_pollTimer.stop();
        setPresentation(QStringLiteral("attention"), messageForError(snapshot.error));
        break;
    case TraktAuthPhase::Cancelled:
        m_pollTimer.stop();
        setPresentation(QStringLiteral("cancelled"), QStringLiteral("Trakt connection cancelled."));
        break;
    default: break;
    }
}

void TraktConnectionController::finalizeCredential(const TraktAuthSnapshot &snapshot)
{
    m_pollTimer.stop();
    m_session.reset();
    QString error;
    if (!m_connections->refresh(&error)) {
        if (!restoreOrClearCredential()) return;
        setPresentation(QStringLiteral("attention"), error);
        return;
    }
    QString claimingProfileId;
    const auto claim = m_connections->externalAccountClaim(
        TrackerProviderId::Trakt, snapshot.remoteAccountId, &claimingProfileId, &error);
    if (claim == TrackerConnectionStore::ExternalAccountClaim::Claimed
        && !claimingProfileId.isEmpty() && claimingProfileId != m_profile.profileId()) {
        m_claimingProfileId = claimingProfileId;
        m_rollbackPending = true;
        setPresentation(QStringLiteral("move_available"),
                        QStringLiteral("This Trakt account is connected to another profile on this device."));
        return;
    }
    if (claim == TrackerConnectionStore::ExternalAccountClaim::Indeterminate) {
        if (!restoreOrClearCredential()) return;
        setPresentation(QStringLiteral("attention"), error.isEmpty()
            ? QStringLiteral("Trakt account ownership could not be verified.") : error);
        return;
    }
    const quint64 generation = m_connections->nextConnectionGeneration(TrackerProviderId::Trakt);
    if (generation == 0 || !m_connections->upsert({TrackerProviderId::Trakt, snapshot.remoteAccountId, generation,
            m_clock->nowMs(), traktCapabilities(), TrackerConnectionState::Connected}, &error)) {
        if (!restoreOrClearCredential()) return;
        setPresentation(QStringLiteral("attention"), error.isEmpty()
            ? QStringLiteral("Trakt could not be attached to this profile.") : error);
        return;
    }
    m_priorCredential.reset();
    m_claimingProfileId.clear();
    m_rollbackPending = false;
    if (m_syncCenter) m_syncCenter->refresh();
    setPresentation(QStringLiteral("connected"), QStringLiteral("Trakt is connected to this Colosseum profile."));
    emit connectionEstablished(QStringLiteral("trakt"));
}

bool TraktConnectionController::moveConnectionToThisProfile()
{
    if (!moveAvailable() || !available()) return false;
    const auto source = m_claimingProfileId == QLatin1String("local")
        ? std::optional<ProfilePaths>(ProfilePaths::localOnly(m_profile.appDataRoot()))
        : ProfilePaths::account(m_claimingProfileId, m_profile.appDataRoot());
    if (!source) { setPresentation(QStringLiteral("attention"), QStringLiteral("The profile holding this Trakt account could not be opened.")); return false; }
    setPresentation(QStringLiteral("moving"), QStringLiteral("Moving the Trakt connection to this profile…"));
    QString error;
    if (!TrackerLifecycleCoordinator::moveConnection(*source, m_profile, TrackerProviderId::Trakt,
            *m_vault, m_clock->nowMs(), &error)) {
        m_rollbackPending = true;
        setPresentation(QStringLiteral("attention"), error.isEmpty()
            ? QStringLiteral("The Trakt connection could not be moved.") : error);
        return false;
    }
    m_claimingProfileId.clear();
    m_priorCredential.reset();
    m_rollbackPending = false;
    if (!m_connections->refresh(&error)) setPresentation(QStringLiteral("attention"), error);
    else {
        if (m_syncCenter) m_syncCenter->refresh();
        setPresentation(QStringLiteral("connected"), QStringLiteral("Trakt is connected to this Colosseum profile."));
        emit connectionEstablished(QStringLiteral("trakt"));
    }
    return true;
}

void TraktConnectionController::cancel()
{
    m_pollTimer.stop();
    if (m_session) m_session->cancel();
    m_session.reset();
    if (m_rollbackPending && !restoreOrClearCredential()) return;
    m_priorCredential.reset();
    m_claimingProfileId.clear();
    m_rollbackPending = false;
    setPresentation(QStringLiteral("cancelled"), QStringLiteral("Trakt connection cancelled."));
}

void TraktConnectionController::dismiss()
{
    if (busy()) return;
    m_session.reset();
    if (m_rollbackPending && !restoreOrClearCredential()) return;
    m_priorCredential.reset();
    m_claimingProfileId.clear();
    m_rollbackPending = false;
    setPresentation(QStringLiteral("idle"), QString());
}

bool TraktConnectionController::prepareForProfileDeactivation()
{
    m_pollTimer.stop();
    if (m_session) m_session->cancel();
    m_session.reset();
    if (m_rollbackPending && !restoreOrClearCredential()) return false;
    m_priorCredential.reset();
    m_claimingProfileId.clear();
    m_rollbackPending = false;
    m_active = false;
    return true;
}

bool TraktConnectionController::restoreOrClearCredential()
{
    if (rollbackVault()) { m_rollbackPending = false; return true; }
    m_rollbackPending = true;
    setPresentation(QStringLiteral("attention"),
        QStringLiteral("Windows could not finalize the Trakt connection change. Keep this panel open and try closing again."));
    return false;
}

bool TraktConnectionController::rollbackVault()
{
    if (m_priorCredential) {
        const auto current = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
        if (current && sameCredential(*current, *m_priorCredential)) return true;
        return m_vault->saveAndVerify(*m_priorCredential);
    }
    return m_vault->clearForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
}

void TraktConnectionController::setPresentation(const QString &phaseValue, const QString &message,
                                                const QString &code, const QUrl &url)
{
    const bool changed = m_phase != phaseValue || m_statusMessage != message || m_userCode != code || m_verificationUrl != url;
    m_phase = phaseValue;
    m_statusMessage = message;
    m_userCode = code;
    m_verificationUrl = url;
    if (changed) emit stateChanged();
}

QString TraktConnectionController::messageForError(TraktAuthError error) const
{
    switch (error) {
    case TraktAuthError::AccessDenied: return QStringLiteral("Trakt authorization was denied.");
    case TraktAuthError::Expired: return QStringLiteral("The Trakt approval expired. Start again for a new code.");
    case TraktAuthError::RateLimited: return QStringLiteral("Trakt asked Colosseum to slow down. Try again shortly.");
    case TraktAuthError::Transport: return QStringLiteral("Colosseum could not reach Trakt.");
    case TraktAuthError::MissingStableAccount: return QStringLiteral("Trakt did not return a stable account identity.");
    case TraktAuthError::AccountChangeRequired: return QStringLiteral("Disconnect the current Trakt account before connecting another one.");
    case TraktAuthError::CredentialStore: return QStringLiteral("Windows could not safely store the Trakt connection.");
    case TraktAuthError::Configuration: return QStringLiteral("Direct Trakt is not configured for this build.");
    default: return QStringLiteral("Trakt could not complete the connection. Try again.");
    }
}
