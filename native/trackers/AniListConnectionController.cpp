#include "AniListConnectionController.h"

#include "TrackerConnectionStore.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QPointer>
#include <QRandomGenerator>
#include <QUrlQuery>

#include <utility>

namespace {

TrackerProviderCapabilities aniListCapabilities()
{
    return TrackerProviderCapability::ReadHistory
        | TrackerProviderCapability::ReadProgress
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

QByteArray stateFromEntropy(const QByteArray &entropy)
{
    if (entropy.size() < 16)
        return {};
    return entropy.toBase64(QByteArray::Base64UrlEncoding
                            | QByteArray::OmitTrailingEquals);
}

QUrl authorizationUrl(const AniListAuthConfiguration &configuration,
                      const QByteArray &state)
{
    QUrl url = configuration.authorizationEndpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), configuration.clientId);
    query.addQueryItem(QStringLiteral("response_type"),
                       QStringLiteral("token"));
    query.addQueryItem(QStringLiteral("state"),
                       QString::fromLatin1(state));
    url.setQuery(query);
    return url;
}

} // namespace

AniListConnectionController::AniListConnectionController(
    const ProfilePaths &profile,
    TrackerConnectionStore *connections,
    QObject *parent)
    : AniListConnectionController(
          profile, connections,
          aniListProductionConfiguration(), nullptr, nullptr, nullptr,
          [](const QUrl &url) { return QDesktopServices::openUrl(url); },
          {}, parent)
{}

AniListConnectionController::AniListConnectionController(
    const ProfilePaths &profile,
    TrackerConnectionStore *connections,
    const std::optional<AniListAuthConfiguration> &configuration,
    TrackerCredentialVault *vault,
    AniListHttpTransport *transport,
    AniListLoopbackServer *loopback,
    BrowserOpen browserOpen,
    EntropySource entropySource,
    QObject *parent)
    : QObject(parent),
      m_profile(profile),
      m_connections(connections),
      m_configuration(configuration),
      m_defaultVault(vault ? nullptr
                           : std::make_unique<WindowsTrackerCredentialVault>()),
      m_defaultTransport(transport ? nullptr
                                   : std::make_unique<AniListHttpTransport>()),
      m_defaultLoopback(loopback ? nullptr
                                 : std::make_unique<AniListLoopbackServer>()),
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
    setObjectName(QStringLiteral("aniListConnectionController"));
    connect(m_loopback, &AniListLoopbackServer::tokenReceived,
            this, &AniListConnectionController::handleToken);
    connect(m_loopback, &AniListLoopbackServer::signInFailed,
            this, &AniListConnectionController::handleSignInFailure);
}

AniListConnectionController::~AniListConnectionController()
{
    if (m_loopback)
        m_loopback->stop();
    if (m_rollbackPending)
        rollbackVault();
}

QByteArray AniListConnectionController::systemEntropy(qsizetype bytes)
{
    if (bytes <= 0 || bytes > 256)
        return {};
    QByteArray result(bytes, Qt::Uninitialized);
    for (qsizetype index = 0; index < bytes; ++index)
        result[index] = static_cast<char>(
            QRandomGenerator::system()->generate() & 0xff);
    return result;
}

bool AniListConnectionController::available() const
{
    return m_configuration
        && aniListConfigurationIsValid(*m_configuration)
        && m_vault && m_vault->isAvailable()
        && m_transport && m_loopback
        && m_connections && m_connections->healthy()
        && !m_profile.profileId().isEmpty();
}

bool AniListConnectionController::busy() const
{
    return m_phase == QLatin1String("awaiting_approval")
        || m_phase == QLatin1String("connecting");
}

bool AniListConnectionController::reconnectRequired() const
{
    if (m_authenticationFailed)
        return true;
    if (!m_connections || !m_vault)
        return false;
    const auto connection =
        m_connections->connection(TrackerProviderId::AniList);
    if (!connection
        || connection->state != TrackerConnectionState::Connected) {
        return false;
    }
    const auto credential = m_vault->loadForProfile(
        m_profile.profileId(), TrackerProviderId::AniList);
    return !credential
        || credential->accessTokenExpiresAtMs
            <= QDateTime::currentMSecsSinceEpoch();
}

void AniListConnectionController::noteAuthenticationFailure()
{
    // Credential and connection record stay as they are: queued deliveries
    // are kept and resume after the user reconnects.
    m_authenticationFailed = true;
    if (!busy()) {
        setPresentation(
            QStringLiteral("reconnect_required"),
            QStringLiteral("AniList no longer accepts this sign-in. Reconnect AniList to resume syncing."));
    }
}

bool AniListConnectionController::beginConnection(
    const QString &providerKey)
{
    if (providerKey.trimmed().toLower() != QLatin1String("anilist")
        || !available()) {
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("AniList is not configured for this build."));
        return false;
    }
    if (busy())
        return false;
    if (m_rollbackPending && !restoreOrClearCredential())
        return false;

    const auto existing =
        m_connections->connection(TrackerProviderId::AniList);
    if (existing
        && existing->state == TrackerConnectionState::Connected
        && !reconnectRequired()) {
        setPresentation(
            QStringLiteral("connected"),
            QStringLiteral("AniList is already connected to this profile."));
        return true;
    }

    m_priorCredential = m_vault->loadForProfile(
        m_profile.profileId(), TrackerProviderId::AniList);
    m_state = stateFromEntropy(m_entropySource(32));
    if (m_state.isEmpty()) {
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("Colosseum could not create a secure AniList authorization request."));
        return false;
    }

    QString error;
    if (!m_loopback->start(m_configuration->redirectUri, m_state, &error)) {
        m_state.clear();
        setPresentation(QStringLiteral("attention"), error);
        return false;
    }
    m_verificationUrl = authorizationUrl(*m_configuration, m_state);

    // As with MAL, the Sync Center decides where the approval page opens; the
    // outside browser is used when the user asks for it (openApprovalPage).
    setPresentation(
        QStringLiteral("awaiting_approval"),
        QStringLiteral("Sign in to AniList and approve Colosseum. This window finishes automatically."),
        m_verificationUrl);
    return true;
}

bool AniListConnectionController::openApprovalPage()
{
    return !m_verificationUrl.isEmpty()
        && m_browserOpen(m_verificationUrl);
}

void AniListConnectionController::handleSignInFailure(const QString &error)
{
    if (m_phase != QLatin1String("awaiting_approval"))
        return;
    m_loopback->stop();
    m_state.clear();
    QString message;
    if (error == QLatin1String("timeout"))
        message = QStringLiteral("AniList sign-in timed out. Start the connection again.");
    else if (error == QLatin1String("access_denied"))
        message = QStringLiteral("AniList authorization was declined.");
    else
        message = QStringLiteral("AniList did not complete authorization.");
    setPresentation(QStringLiteral("attention"), message);
}

void AniListConnectionController::handleToken(const QByteArray &accessToken,
                                              qint64 expiresInSeconds)
{
    if (m_phase != QLatin1String("awaiting_approval"))
        return;
    m_loopback->stop();
    m_state.clear();
    setPresentation(
        QStringLiteral("connecting"),
        QStringLiteral("Finishing the secure AniList connection..."));

    // Identity first: nothing is stored until AniList confirms who this is.
    QPointer<AniListConnectionController> self(this);
    m_transport->post(
        *m_configuration, accessToken, aniListViewerRequest(),
        [self, accessToken, expiresInSeconds](const AniListApiResult &result) {
            if (self)
                self->handleViewer(result, accessToken, expiresInSeconds);
        });
}

void AniListConnectionController::handleViewer(
    const AniListApiResult &result,
    const QByteArray &accessToken,
    qint64 expiresInSeconds)
{
    if (!result.succeeded()) {
        setPresentation(QStringLiteral("attention"),
                        messageForTransport(result.error));
        return;
    }
    const auto viewer = aniListParseViewer(result.document);
    if (!viewer || viewer->userId.isEmpty()) {
        setPresentation(
            QStringLiteral("attention"),
            messageForTransport(AniListTransportError::ProtocolFailure));
        return;
    }
    finalizeCredential(*viewer, accessToken, expiresInSeconds);
}

void AniListConnectionController::finalizeCredential(
    const AniListViewer &viewer,
    const QByteArray &accessToken,
    qint64 expiresInSeconds)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto existing =
        m_connections->connection(TrackerProviderId::AniList);
    const bool reconnectingSameAccount = existing
        && existing->state == TrackerConnectionState::Connected
        && existing->remoteAccountId == viewer.userId;
    if (existing
        && existing->state == TrackerConnectionState::Connected
        && !reconnectingSameAccount) {
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("This profile is connected to a different AniList account. Disconnect it first."));
        return;
    }

    TrackerCredential credential;
    credential.slot = {m_profile.profileId(),
                       TrackerProviderId::AniList,
                       viewer.userId};
    credential.accessToken = accessToken;
    credential.accessTokenExpiresAtMs = now + expiresInSeconds * 1000;
    credential.grantedScopes = {QStringLiteral("anilist:api")};

    if (!m_vault->saveAndVerify(credential)) {
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("The AniList credential could not be stored safely."));
        return;
    }
    m_rollbackPending = true;

    QString error;
    if (!m_connections->refresh(&error)) {
        if (!restoreOrClearCredential())
            return;
        setPresentation(QStringLiteral("attention"), error);
        return;
    }

    if (!reconnectingSameAccount) {
        QString claimingProfileId;
        const auto claim = m_connections->externalAccountClaim(
            TrackerProviderId::AniList, viewer.userId,
            &claimingProfileId, &error);
        if (claim == TrackerConnectionStore::ExternalAccountClaim::Indeterminate
            || (claim == TrackerConnectionStore::ExternalAccountClaim::Claimed
                && claimingProfileId != m_profile.profileId())) {
            if (!restoreOrClearCredential())
                return;
            setPresentation(
                QStringLiteral("attention"),
                claim == TrackerConnectionStore::ExternalAccountClaim::Claimed
                    ? QStringLiteral("This AniList account is connected to another profile on this device. Disconnect it there first.")
                    : (error.isEmpty()
                           ? QStringLiteral("Tracker account ownership could not be verified.")
                           : error));
            return;
        }

        const quint64 generation =
            m_connections->nextConnectionGeneration(
                TrackerProviderId::AniList);
        if (generation == 0
            || !m_connections->upsert(
                {TrackerProviderId::AniList, viewer.userId,
                 generation, now, aniListCapabilities(),
                 TrackerConnectionState::Connected},
                &error)) {
            if (!restoreOrClearCredential())
                return;
            setPresentation(
                QStringLiteral("attention"),
                error.isEmpty()
                    ? QStringLiteral("AniList could not be attached to this profile.")
                    : error);
            return;
        }
    }
    // A same-account reconnect keeps the existing connection record and
    // generation, so queued deliveries stay current.

    m_priorCredential.reset();
    m_rollbackPending = false;
    m_authenticationFailed = false;
    setPresentation(
        QStringLiteral("connected"),
        QStringLiteral("AniList is connected to this Colosseum profile."));
    emit connectionEstablished(QStringLiteral("anilist"));
}

bool AniListConnectionController::disconnect()
{
    if (!m_connections || !m_vault || busy())
        return false;
    const auto connection =
        m_connections->connection(TrackerProviderId::AniList);
    if (connection
        && connection->state == TrackerConnectionState::Connected) {
        QString error;
        if (!m_connections->setDisconnected(
                TrackerProviderId::AniList, connection->remoteAccountId,
                connection->connectionGeneration, &error)) {
            setPresentation(QStringLiteral("attention"), error);
            return false;
        }
    }
    if (!m_vault->clearForProfile(m_profile.profileId(),
                                  TrackerProviderId::AniList)) {
        // Put the record back so state and credential stay consistent.
        if (connection) {
            m_connections->restoreConnected(
                TrackerProviderId::AniList, connection->remoteAccountId,
                connection->connectionGeneration);
        }
        setPresentation(
            QStringLiteral("attention"),
            QStringLiteral("The AniList credential could not be removed. Try again."));
        return false;
    }
    m_authenticationFailed = false;
    m_priorCredential.reset();
    setPresentation(QStringLiteral("idle"), QString());
    return true;
}

void AniListConnectionController::cancel()
{
    if (m_loopback)
        m_loopback->stop();
    m_state.clear();
    if (m_rollbackPending && !restoreOrClearCredential())
        return;
    m_priorCredential.reset();
    setPresentation(
        QStringLiteral("cancelled"),
        QStringLiteral("AniList connection cancelled."));
}

void AniListConnectionController::dismiss()
{
    if (busy())
        return;
    if (m_rollbackPending && !restoreOrClearCredential())
        return;
    m_state.clear();
    m_priorCredential.reset();
    setPresentation(QStringLiteral("idle"), QString());
}

bool AniListConnectionController::prepareForProfileDeactivation()
{
    if (m_loopback)
        m_loopback->stop();
    m_state.clear();
    if (!m_rollbackPending)
        return true;
    return restoreOrClearCredential();
}

bool AniListConnectionController::rollbackVault()
{
    if (!m_vault)
        return false;
    if (m_priorCredential) {
        const auto current = m_vault->loadForProfile(
            m_profile.profileId(), TrackerProviderId::AniList);
        if (current && sameCredential(*current, *m_priorCredential))
            return true;
        return m_vault->saveAndVerify(*m_priorCredential);
    }
    return m_vault->clearForProfile(
        m_profile.profileId(), TrackerProviderId::AniList);
}

bool AniListConnectionController::restoreOrClearCredential()
{
    if (rollbackVault()) {
        m_rollbackPending = false;
        return true;
    }
    m_rollbackPending = true;
    setPresentation(
        QStringLiteral("attention"),
        QStringLiteral("The AniList credential change could not be finalized safely. Keep this panel open and retry."));
    return false;
}

void AniListConnectionController::setPresentation(
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

QString AniListConnectionController::messageForTransport(
    AniListTransportError error) const
{
    switch (error) {
    case AniListTransportError::AuthenticationRequired:
        return QStringLiteral("AniList rejected the authorization. Start the connection again.");
    case AniListTransportError::RateLimited:
        return QStringLiteral("AniList asked Colosseum to wait. Try again shortly.");
    case AniListTransportError::PayloadTooLarge:
    case AniListTransportError::ProtocolFailure:
    case AniListTransportError::RedirectRejected:
        return QStringLiteral("AniList returned an unexpected authorization response.");
    case AniListTransportError::NetworkFailure:
    case AniListTransportError::Timeout:
    case AniListTransportError::ServerFailure:
        return QStringLiteral("Colosseum could not reach AniList. Check the connection and try again.");
    case AniListTransportError::None:
        break;
    }
    return QStringLiteral("AniList could not complete the connection.");
}
