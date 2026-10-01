#include "MalConnectionController.h"

#include "TrackerConnectionStore.h"
#include "TrackerLifecycleCoordinator.h"
#include "TrackerSyncCenterModel.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QPointer>
#include <QRandomGenerator>

#include <utility>

namespace {

constexpr qint64 kConservativeRefreshLifetimeMs =
    31LL * 24 * 60 * 60 * 1000;

TrackerProviderCapabilities malCapabilities()
{
    return TrackerProviderCapability::ReadProgress
        | TrackerProviderCapability::WriteProgress
        | TrackerProviderCapability::WriteCompletion;
}

bool sameCredential(const TrackerCredential &left,
                    const TrackerCredential &right)
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

MalConnectionController::MalConnectionController(
    const ProfilePaths &profile,
    TrackerConnectionStore *connections,
    TrackerSyncCenterModel *syncCenter,
    QObject *parent)
    : MalConnectionController(
          profile, connections, syncCenter,
          malProductionConfiguration(), nullptr, nullptr, nullptr,
          [](const QUrl &url) { return QDesktopServices::openUrl(url); },
          {}, parent)
{}

MalConnectionController::MalConnectionController(
    const ProfilePaths &profile,
    TrackerConnectionStore *connections,
    TrackerSyncCenterModel *syncCenter,
    const std::optional<MalAuthConfiguration> &configuration,
    TrackerCredentialVault *vault,
    MalHttpTransport *transport,
    MalLoopbackServer *loopback,
    BrowserOpen browserOpen,
    EntropySource entropySource,
    QObject *parent)
    : QObject(parent),
      m_profile(profile),
      m_connections(connections),
      m_syncCenter(syncCenter),
      m_configuration(configuration),
      m_defaultVault(vault ? nullptr
                           : std::make_unique<WindowsTrackerCredentialVault>()),
      m_defaultTransport(transport ? nullptr
                                   : std::make_unique<MalHttpTransport>()),
      m_defaultLoopback(loopback ? nullptr
                                 : std::make_unique<MalLoopbackServer>()),
      m_vault(vault ? vault : m_defaultVault.get()),
      m_transport(transport ? transport : m_defaultTransport.get()),
      m_loopback(loopback ? loopback : m_defaultLoopback.get()),
      m_browserOpen(browserOpen ? std::move(browserOpen)
                                : BrowserOpen([](const QUrl &url) {
                                      return QDesktopServices::openUrl(url);
                                  })),
      m_entropySource(entropySource ? std::move(entropySource)
                                    : EntropySource(systemEntropy))
{
    setObjectName(QStringLiteral("malConnectionController"));
    connect(m_loopback, &MalLoopbackServer::authorizationResult,
            this, &MalConnectionController::handleCallback);
}

MalConnectionController::~MalConnectionController()
{
    if (m_loopback)
        m_loopback->stop();
    if (m_rollbackPending)
        rollbackVault();
}

QByteArray MalConnectionController::systemEntropy(qsizetype bytes)
{
    if (bytes <= 0 || bytes > 256)
        return {};
    QByteArray result(bytes, Qt::Uninitialized);
    for (qsizetype index = 0; index < bytes; ++index)
        result[index] = static_cast<char>(
            QRandomGenerator::system()->generate() & 0xff);
    return result;
}

bool MalConnectionController::available() const
{
    return m_configuration
        && malConfigurationIsValid(*m_configuration)
        && m_vault && m_vault->isAvailable()
        && m_transport && m_loopback
        && m_connections && m_connections->healthy()
        && !m_profile.profileId().isEmpty();
}

bool MalConnectionController::busy() const
{
    return m_phase == QLatin1String("awaiting_approval")
        || m_phase == QLatin1String("connecting")
        || m_phase == QLatin1String("moving");
}

bool MalConnectionController::moveAvailable() const
{
    return m_phase == QLatin1String("move_available")
        && !m_claimingProfileId.isEmpty();
}

bool MalConnectionController::beginConnection(
    const QString &providerKey)
{
    if (providerKey.trimmed().toLower() != QLatin1String("mal")
        || !available()) {
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("MyAnimeList is not configured for this build."));
        return false;
    }
    if (busy())
        return false;
    if (m_rollbackPending && !restoreOrClearCredential())
        return false;

    const auto existing =
        m_connections->connection(TrackerProviderId::Mal);
    if (existing
        && existing->state == TrackerConnectionState::Connected) {
        setPresentation(
            QStringLiteral("connected"),
            QStringLiteral("MyAnimeList is already connected to this profile."));
        return true;
    }

    m_priorCredential = m_vault->loadForProfile(
        m_profile.profileId(), TrackerProviderId::Mal);
    m_claimingProfileId.clear();
    m_token.reset();
    m_state = malStateFromEntropy(m_entropySource(32));
    m_codeVerifier =
        malPkceVerifierFromEntropy(m_entropySource(48));
    if (m_state.isEmpty() || m_codeVerifier.isEmpty()) {
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("Colosseum could not create a secure MyAnimeList authorization request."));
        return false;
    }

    QString error;
    if (!m_loopback->start(m_configuration->redirectUri, &error)) {
        setPresentation(QStringLiteral("attention"), error);
        return false;
    }
    m_verificationUrl = malAuthorizationUrl(
        *m_configuration, m_state, m_codeVerifier);
    if (m_verificationUrl.isEmpty()) {
        m_loopback->stop();
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("The MyAnimeList authorization URL could not be constructed safely."));
        return false;
    }

    // The Sync Center shows the approval page inside the app. The outside
    // browser is only used when the user asks for it (openApprovalPage).
    setPresentation(
        QStringLiteral("awaiting_approval"),
        QStringLiteral("Sign in to MyAnimeList and approve Colosseum. This window finishes automatically."),
        m_verificationUrl);
    return true;
}

bool MalConnectionController::openApprovalPage()
{
    return !m_verificationUrl.isEmpty()
        && m_browserOpen(m_verificationUrl);
}

void MalConnectionController::handleCallback(
    const QByteArray &state,
    const QByteArray &authorizationCode,
    const QString &providerError)
{
    if (m_phase != QLatin1String("awaiting_approval"))
        return;
    m_loopback->stop();

    if (!providerError.isEmpty()) {
        setPresentation(
            QStringLiteral("attention"),
            providerError == QLatin1String("access_denied")
                ? QStringLiteral("MyAnimeList authorization was declined.")
                : QStringLiteral("MyAnimeList did not complete authorization."));
        return;
    }
    if (!malConstantTimeEqual(m_state, state)
        || authorizationCode.isEmpty()) {
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("MyAnimeList returned an invalid OAuth state. Start the connection again."));
        return;
    }

    setPresentation(
        QStringLiteral("connecting"),
        QStringLiteral("Finishing the secure MyAnimeList connection…"));
    QPointer<MalConnectionController> self(this);
    m_transport->exchangeAuthorizationCode(
        *m_configuration, authorizationCode, m_codeVerifier,
        [self](const MalTokenResult &result) {
            if (self)
                self->handleToken(result);
        });
}

void MalConnectionController::handleToken(
    const MalTokenResult &result)
{
    m_state.clear();
    m_codeVerifier.clear();
    if (!result.succeeded()) {
        setPresentation(QStringLiteral("attention"),
                        messageForTransport(result.error));
        return;
    }
    m_token = result;
    QPointer<MalConnectionController> self(this);
    m_transport->fetchIdentity(
        *m_configuration, result.accessToken,
        [self](const MalIdentityResult &identity) {
            if (self)
                self->handleIdentity(identity);
        });
}

void MalConnectionController::handleIdentity(
    const MalIdentityResult &result)
{
    if (!result.succeeded() || !m_token) {
        setPresentation(QStringLiteral("attention"),
                        messageForTransport(result.error));
        return;
    }
    finalizeCredential(result.remoteAccountId);
}

void MalConnectionController::finalizeCredential(
    const QString &remoteAccountId)
{
    if (!m_token || remoteAccountId.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    TrackerCredential credential;
    credential.slot = {m_profile.profileId(),
                       TrackerProviderId::Mal,
                       remoteAccountId};
    credential.accessToken = m_token->accessToken;
    credential.refreshToken = m_token->refreshToken;
    credential.accessTokenExpiresAtMs =
        now + m_token->accessExpiresInMs;
    credential.refreshTokenExpiresAtMs = qMax(
        now + kConservativeRefreshLifetimeMs,
        credential.accessTokenExpiresAtMs + 60LL * 60 * 1000);
    credential.grantedScopes = {QStringLiteral("mal:api")};

    if (!m_vault->saveAndVerify(credential)) {
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("The MyAnimeList credential could not be stored safely."));
        return;
    }
    m_rollbackPending = true;
    m_token.reset();

    QString error;
    if (!m_connections->refresh(&error)) {
        if (!restoreOrClearCredential())
            return;
        setPresentation(QStringLiteral("attention"), error);
        return;
    }

    QString claimingProfileId;
    const auto claim = m_connections->externalAccountClaim(
        TrackerProviderId::Mal, remoteAccountId,
        &claimingProfileId, &error);
    if (claim == TrackerConnectionStore::ExternalAccountClaim::Claimed
        && !claimingProfileId.isEmpty()
        && claimingProfileId != m_profile.profileId()) {
        m_claimingProfileId = claimingProfileId;
        setPresentation(
            QStringLiteral("move_available"),
            QStringLiteral("This MyAnimeList account is connected to another profile on this device."));
        return;
    }
    if (claim == TrackerConnectionStore::ExternalAccountClaim::Indeterminate) {
        if (!restoreOrClearCredential())
            return;
        setPresentation(
            QStringLiteral("attention"),
            error.isEmpty()
                ? QStringLiteral("Tracker account ownership could not be verified.")
                : error);
        return;
    }

    const quint64 generation =
        m_connections->nextConnectionGeneration(
            TrackerProviderId::Mal);
    if (generation == 0
        || !m_connections->upsert(
            {TrackerProviderId::Mal, remoteAccountId,
             generation, now, malCapabilities(),
             TrackerConnectionState::Connected},
            &error)) {
        if (!restoreOrClearCredential())
            return;
        setPresentation(
            QStringLiteral("attention"),
            error.isEmpty()
                ? QStringLiteral("MyAnimeList could not be attached to this profile.")
                : error);
        return;
    }

    m_priorCredential.reset();
    m_claimingProfileId.clear();
    m_rollbackPending = false;
    if (m_syncCenter)
        m_syncCenter->refresh();
    setPresentation(
        QStringLiteral("connected"),
        QStringLiteral("MyAnimeList is connected to this Colosseum profile."));
    emit connectionEstablished(QStringLiteral("mal"));
}

bool MalConnectionController::moveConnectionToThisProfile()
{
    if (!moveAvailable() || !available())
        return false;
    const auto source =
        m_claimingProfileId == QLatin1String("local")
        ? std::optional<ProfilePaths>(
              ProfilePaths::localOnly(m_profile.appDataRoot()))
        : ProfilePaths::account(m_claimingProfileId,
                                m_profile.appDataRoot());
    if (!source) {
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("The profile holding this MyAnimeList account could not be opened."));
        return false;
    }

    setPresentation(
        QStringLiteral("moving"),
        QStringLiteral("Moving the MyAnimeList connection to this profile…"));
    QString error;
    if (!TrackerLifecycleCoordinator::moveConnection(
            *source, m_profile, TrackerProviderId::Mal,
            *m_vault, QDateTime::currentMSecsSinceEpoch(),
            &error)) {
        m_rollbackPending = true;
        setPresentation(
            QStringLiteral("attention"),
            error.isEmpty()
                ? QStringLiteral("The MyAnimeList connection could not be moved.")
                : error);
        return false;
    }

    m_claimingProfileId.clear();
    m_priorCredential.reset();
    m_rollbackPending = false;
    if (!m_connections->refresh(&error)) {
        setPresentation(QStringLiteral("attention"), error);
        return false;
    }
    if (m_syncCenter)
        m_syncCenter->refresh();
    setPresentation(
        QStringLiteral("connected"),
        QStringLiteral("MyAnimeList is connected to this Colosseum profile."));
    emit connectionEstablished(QStringLiteral("mal"));
    return true;
}

void MalConnectionController::cancel()
{
    if (m_loopback)
        m_loopback->stop();
    m_state.clear();
    m_codeVerifier.clear();
    m_token.reset();
    if (m_rollbackPending && !restoreOrClearCredential())
        return;
    m_claimingProfileId.clear();
    m_priorCredential.reset();
    setPresentation(
        QStringLiteral("cancelled"),
        QStringLiteral("MyAnimeList connection cancelled."));
}

void MalConnectionController::dismiss()
{
    if (busy())
        return;
    if (m_rollbackPending && !restoreOrClearCredential())
        return;
    m_state.clear();
    m_codeVerifier.clear();
    m_token.reset();
    m_claimingProfileId.clear();
    m_priorCredential.reset();
    setPresentation(QStringLiteral("idle"), QString());
}

bool MalConnectionController::prepareForProfileDeactivation()
{
    if (m_loopback)
        m_loopback->stop();
    m_state.clear();
    m_codeVerifier.clear();
    m_token.reset();
    if (!m_rollbackPending)
        return true;
    return restoreOrClearCredential();
}

bool MalConnectionController::rollbackVault()
{
    if (!m_vault)
        return false;
    if (m_priorCredential) {
        const auto current = m_vault->loadForProfile(
            m_profile.profileId(), TrackerProviderId::Mal);
        if (current
            && sameCredential(*current, *m_priorCredential)) {
            return true;
        }
        return m_vault->saveAndVerify(*m_priorCredential);
    }
    return m_vault->clearForProfile(
        m_profile.profileId(), TrackerProviderId::Mal);
}

bool MalConnectionController::restoreOrClearCredential()
{
    if (rollbackVault()) {
        m_rollbackPending = false;
        return true;
    }
    m_rollbackPending = true;
    setPresentation(
        QStringLiteral("attention"),
        QStringLiteral("The MyAnimeList credential change could not be finalized safely. Keep this panel open and retry."));
    return false;
}

void MalConnectionController::setPresentation(
    const QString &phase,
    const QString &message,
    const QUrl &url)
{
    const bool changed =
        m_phase != phase || m_statusMessage != message
        || m_verificationUrl != url;
    m_phase = phase;
    m_statusMessage = message;
    m_verificationUrl = url;
    if (changed)
        emit stateChanged();
}

QString MalConnectionController::messageForTransport(
    MalTransportError error) const
{
    switch (error) {
    case MalTransportError::AuthenticationRequired:
        return QStringLiteral("MyAnimeList rejected the authorization. Start the connection again.");
    case MalTransportError::AccessDenied:
        return QStringLiteral("MyAnimeList denied access to this application.");
    case MalTransportError::RateLimited:
        return QStringLiteral("MyAnimeList asked Colosseum to wait. Try again shortly.");
    case MalTransportError::PayloadTooLarge:
    case MalTransportError::ProtocolFailure:
        return QStringLiteral("MyAnimeList returned an unexpected authorization response.");
    case MalTransportError::NetworkFailure:
        return QStringLiteral("Colosseum could not reach MyAnimeList. Check the connection and try again.");
    case MalTransportError::None:
        break;
    }
    return QStringLiteral("MyAnimeList could not complete the connection.");
}
