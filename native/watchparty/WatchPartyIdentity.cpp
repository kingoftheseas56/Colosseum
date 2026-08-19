#include "watchparty/WatchPartyIdentity.h"

#include <utility>

namespace Colosseum::WatchParty {

bool SignedInAccountIdentity::isValid() const
{
    return !username.trimmed().isEmpty()
        && !bearerToken.isEmpty();
}

IdentityCoordinator::IdentityCoordinator(
    RoomServiceClient* service,
    IWatchPartyAccountBridge* accountBridge)
    : m_service(service),
      m_accountBridge(accountBridge)
{
}

bool IdentityCoordinator::hasSignedInIdentity() const
{
    if (!m_accountBridge)
        return false;

    const std::optional<SignedInAccountIdentity> identity =
        m_accountBridge->currentSignedInIdentity();
    return identity.has_value() && identity->isValid();
}

QString IdentityCoordinator::signedInUsername() const
{
    if (!m_accountBridge)
        return {};

    const std::optional<SignedInAccountIdentity> identity =
        m_accountBridge->currentSignedInIdentity();
    if (!identity.has_value() || !identity->isValid())
        return {};

    return identity->username;
}

IdentityActionResult IdentityCoordinator::openSignedInService(
    const QUrl& serviceUrl)
{
    const IdentityActionResult serviceResult = requireService();
    if (!serviceResult.ok())
        return serviceResult;

    SignedInAccountIdentity identity;
    const IdentityActionResult identityResult =
        requireSignedInIdentity(&identity);
    if (!identityResult.ok())
        return identityResult;

    if (!m_service->openService(serviceUrl, identity.bearerToken)) {
        return IdentityActionResult::failure(
            IdentityActionError::ServiceRejected,
            QStringLiteral(
                "Watch Party service rejected the signed-in open request"));
    }

    m_serviceIdentityMode = ServiceIdentityMode::SignedIn;
    return IdentityActionResult::success();
}

IdentityActionResult IdentityCoordinator::openGuestService(
    const QUrl& serviceUrl)
{
    const IdentityActionResult serviceResult = requireService();
    if (!serviceResult.ok())
        return serviceResult;

    const IdentityActionResult identityResult =
        requireSignedOutIdentity();
    if (!identityResult.ok())
        return identityResult;

    if (!m_service->openService(serviceUrl)) {
        return IdentityActionResult::failure(
            IdentityActionError::ServiceRejected,
            QStringLiteral(
                "Watch Party service rejected the accountless open request"));
    }

    m_serviceIdentityMode = ServiceIdentityMode::Guest;
    return IdentityActionResult::success();
}

void IdentityCoordinator::closeService()
{
    if (m_service)
        m_service->closeService();

    m_serviceIdentityMode = ServiceIdentityMode::None;
}

IdentityActionResult IdentityCoordinator::createRoom(
    const SourceDescriptor& source)
{
    const IdentityActionResult identityResult =
        requireSignedInServiceIdentity();
    if (!identityResult.ok())
        return identityResult;

    if (!m_service->createRoom(source)) {
        return IdentityActionResult::failure(
            IdentityActionError::ServiceRejected,
            QStringLiteral("Watch Party room creation was rejected"));
    }

    return IdentityActionResult::success();
}

IdentityActionResult IdentityCoordinator::joinSignedIn(
    const QString& roomId)
{
    const IdentityActionResult identityResult =
        requireSignedInServiceIdentity();
    if (!identityResult.ok())
        return identityResult;

    if (!m_service->joinSignedIn(roomId)) {
        return IdentityActionResult::failure(
            IdentityActionError::ServiceRejected,
            QStringLiteral("signed-in Watch Party join was rejected"));
    }

    return IdentityActionResult::success();
}

IdentityActionResult IdentityCoordinator::joinGuest(
    const QString& roomId,
    const QString& temporaryDisplayName)
{
    const IdentityActionResult identityResult =
        requireGuestServiceIdentity();
    if (!identityResult.ok())
        return identityResult;

    if (!m_service->joinGuest(roomId, temporaryDisplayName)) {
        return IdentityActionResult::failure(
            IdentityActionError::ServiceRejected,
            QStringLiteral("accountless Watch Party join was rejected"));
    }

    return IdentityActionResult::success();
}

IdentityActionResult IdentityCoordinator::inviteExactUsername(
    const QString& exactUsername,
    IWatchPartyAccountBridge::InviteCompletion completion)
{
    const IdentityActionResult identityResult =
        requireSignedInServiceIdentity();
    if (!identityResult.ok())
        return identityResult;

    const IdentityActionResult hostResult =
        requireLocalSignedInHost();
    if (!hostResult.ok())
        return hostResult;

    if (exactUsername.trimmed().isEmpty()) {
        return IdentityActionResult::failure(
            IdentityActionError::InvalidInviteTarget,
            QStringLiteral(
                "exact-username invite requires a non-empty username"));
    }

    // Deliberately do not trim, case-fold, autocomplete, or search. The account
    // owner receives exactly what the user supplied and owns recipient lookup.
    m_accountBridge->inviteExactUsername(
        m_service->roomId(),
        exactUsername,
        std::move(completion));

    return IdentityActionResult::success();
}

IdentityActionResult IdentityCoordinator::requireService() const
{
    if (m_service)
        return IdentityActionResult::success();

    return IdentityActionResult::failure(
        IdentityActionError::ServiceUnavailable,
        QStringLiteral("Watch Party room service is unavailable"));
}

IdentityActionResult IdentityCoordinator::requireSignedInIdentity(
    SignedInAccountIdentity* identity) const
{
    if (!m_accountBridge) {
        return IdentityActionResult::failure(
            IdentityActionError::AccountBindingUnavailable,
            QStringLiteral(
                "signed-in Watch Party actions require the adopted account binding"));
    }

    const std::optional<SignedInAccountIdentity> current =
        m_accountBridge->currentSignedInIdentity();
    if (!current.has_value()) {
        return IdentityActionResult::failure(
            IdentityActionError::SignedInRequired,
            QStringLiteral(
                "this Watch Party action requires a signed-in Colosseum account"));
    }

    if (!current->isValid()) {
        return IdentityActionResult::failure(
            IdentityActionError::InvalidSignedInIdentity,
            QStringLiteral(
                "the account binding returned an incomplete signed-in identity"));
    }

    if (identity)
        *identity = *current;

    return IdentityActionResult::success();
}

IdentityActionResult IdentityCoordinator::requireSignedOutIdentity() const
{
    if (!m_accountBridge)
        return IdentityActionResult::success();

    const std::optional<SignedInAccountIdentity> current =
        m_accountBridge->currentSignedInIdentity();
    if (!current.has_value())
        return IdentityActionResult::success();

    if (!current->isValid()) {
        return IdentityActionResult::failure(
            IdentityActionError::InvalidSignedInIdentity,
            QStringLiteral(
                "the account binding returned an incomplete signed-in identity"));
    }

    return IdentityActionResult::failure(
        IdentityActionError::SignedOutRequired,
        QStringLiteral(
            "accountless Watch Party join is available only while signed out"));
}

IdentityActionResult
IdentityCoordinator::requireSignedInServiceIdentity() const
{
    const IdentityActionResult serviceResult = requireService();
    if (!serviceResult.ok())
        return serviceResult;

    const IdentityActionResult identityResult =
        requireSignedInIdentity();
    if (!identityResult.ok())
        return identityResult;

    if (m_serviceIdentityMode != ServiceIdentityMode::SignedIn
        || !m_service->signedInCredentialPresent()) {
        return IdentityActionResult::failure(
            IdentityActionError::WrongServiceIdentity,
            QStringLiteral(
                "Watch Party service was not opened with signed-in identity"));
    }

    return IdentityActionResult::success();
}

IdentityActionResult
IdentityCoordinator::requireGuestServiceIdentity() const
{
    const IdentityActionResult serviceResult = requireService();
    if (!serviceResult.ok())
        return serviceResult;

    const IdentityActionResult identityResult =
        requireSignedOutIdentity();
    if (!identityResult.ok())
        return identityResult;

    if (m_serviceIdentityMode != ServiceIdentityMode::Guest
        || m_service->signedInCredentialPresent()) {
        return IdentityActionResult::failure(
            IdentityActionError::WrongServiceIdentity,
            QStringLiteral(
                "Watch Party service was not opened for an accountless guest"));
    }

    return IdentityActionResult::success();
}

IdentityActionResult
IdentityCoordinator::requireLocalSignedInHost() const
{
    if (!m_service->hasSession()
        || !m_service->hasSnapshot()) {
        return IdentityActionResult::failure(
            IdentityActionError::RoomSnapshotRequired,
            QStringLiteral(
                "room host authority is unavailable until the authoritative snapshot arrives"));
    }

    const RoomSnapshot snapshot = m_service->snapshot();
    if (snapshot.hostParticipantId
        != m_service->participantId()) {
        return IdentityActionResult::failure(
            IdentityActionError::HostRequired,
            QStringLiteral(
                "only the current Watch Party host can invite participants"));
    }

    for (const ParticipantState& participant : snapshot.participants) {
        if (participant.identity.participantId
            != m_service->participantId()) {
            continue;
        }

        if (!participant.host
            || participant.identity.kind != IdentityKind::SignedIn) {
            return IdentityActionResult::failure(
                IdentityActionError::HostRequired,
                QStringLiteral(
                    "Watch Party invite authority requires the signed-in room host"));
        }

        return IdentityActionResult::success();
    }

    return IdentityActionResult::failure(
        IdentityActionError::RoomSnapshotRequired,
        QStringLiteral(
            "authoritative room snapshot does not contain the local participant"));
}

QString identityActionErrorName(IdentityActionError error)
{
    switch (error) {
    case IdentityActionError::None:
        return QStringLiteral("none");
    case IdentityActionError::ServiceUnavailable:
        return QStringLiteral("serviceUnavailable");
    case IdentityActionError::AccountBindingUnavailable:
        return QStringLiteral("accountBindingUnavailable");
    case IdentityActionError::SignedInRequired:
        return QStringLiteral("signedInRequired");
    case IdentityActionError::SignedOutRequired:
        return QStringLiteral("signedOutRequired");
    case IdentityActionError::InvalidSignedInIdentity:
        return QStringLiteral("invalidSignedInIdentity");
    case IdentityActionError::WrongServiceIdentity:
        return QStringLiteral("wrongServiceIdentity");
    case IdentityActionError::InvalidInviteTarget:
        return QStringLiteral("invalidInviteTarget");
    case IdentityActionError::RoomSnapshotRequired:
        return QStringLiteral("roomSnapshotRequired");
    case IdentityActionError::HostRequired:
        return QStringLiteral("hostRequired");
    case IdentityActionError::ServiceRejected:
        return QStringLiteral("serviceRejected");
    }

    return QStringLiteral("unknown");
}

} // namespace Colosseum::WatchParty
