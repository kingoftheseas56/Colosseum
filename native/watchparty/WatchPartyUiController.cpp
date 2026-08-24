#include "watchparty/WatchPartyUiController.h"

#include "watchparty/WebSocketWatchPartyTransport.h"
#include "watchparty/WatchPartySource.h"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QVariant>

#include <cmath>
#include <utility>

namespace Colosseum::WatchParty {
namespace {

constexpr int kMaxRoomIdLength = 128;
constexpr int kMaxGuestNameLength = 80;
constexpr int kMaxChatLength = 1000;
constexpr int kMaxReactionLength = 32;

bool finiteNonNegative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

QString defaultIdentityErrorText(IdentityActionError error)
{
    switch (error) {
    case IdentityActionError::AccountBindingUnavailable:
        return QStringLiteral("A Colosseum account connection is not available yet.");
    case IdentityActionError::SignedInRequired:
    case IdentityActionError::InvalidSignedInIdentity:
        return QStringLiteral("Sign in before starting a Watch Party.");
    case IdentityActionError::SignedOutRequired:
        return QStringLiteral("Guest join is only available while signed out.");
    case IdentityActionError::InvalidInviteTarget:
        return QStringLiteral("Enter an exact username.");
    case IdentityActionError::HostRequired:
        return QStringLiteral("Only the host can do that.");
    case IdentityActionError::RoomSnapshotRequired:
        return QStringLiteral("The room is not ready yet.");
    case IdentityActionError::WrongServiceIdentity:
        return QStringLiteral("The Watch Party connection belongs to a different identity mode.");
    case IdentityActionError::ServiceUnavailable:
    case IdentityActionError::ServiceRejected:
        return QStringLiteral("Watch Party service is unavailable.");
    case IdentityActionError::None:
        break;
    }
    return QStringLiteral("Watch Party could not complete that action.");
}


QString defaultRoomServiceErrorText(RoomServiceErrorCode error)
{
    switch (error) {
    case RoomServiceErrorCode::Unauthenticated:
        return QStringLiteral("Sign in again before using this Watch Party.");
    case RoomServiceErrorCode::RoomNotFound:
        return QStringLiteral("That Watch Party does not exist.");
    case RoomServiceErrorCode::RoomFull:
        return QStringLiteral("This Watch Party is full.");
    case RoomServiceErrorCode::RoomEnded:
        return QStringLiteral("That Watch Party has ended.");
    case RoomServiceErrorCode::ParticipantRemoved:
        return QStringLiteral("You were removed from this Watch Party.");
    case RoomServiceErrorCode::NotAuthorized:
        return QStringLiteral("You are not allowed to do that in this Watch Party.");
    case RoomServiceErrorCode::InvalidSource:
        return QStringLiteral("This Watch Party source is not supported.");
    case RoomServiceErrorCode::InvalidMessage:
        return QStringLiteral("The Watch Party service rejected an invalid message.");
    case RoomServiceErrorCode::ProtocolVersionMismatch:
        return QStringLiteral("Watch Party protocol versions do not match.");
    case RoomServiceErrorCode::RateLimited:
        return QStringLiteral("Watch Party is receiving commands too quickly. Try again.");
    case RoomServiceErrorCode::InvalidRequest:
    case RoomServiceErrorCode::NotConnected:
    case RoomServiceErrorCode::NoSession:
    case RoomServiceErrorCode::ProtocolFailure:
    case RoomServiceErrorCode::TransportFailure:
    case RoomServiceErrorCode::ServerRejected:
    case RoomServiceErrorCode::None:
        break;
    }
    return QStringLiteral("Watch Party service reported an error.");
}

} // namespace

UiController::UiController(PlayerSyncController* playerSync, QObject* parent)
    : QObject(parent),
      m_ownedWebSocketTransport(new WebSocketTransport(this)),
      m_transport(m_ownedWebSocketTransport),
      m_playerSync(playerSync)
{
    initialize(m_transport, nullptr);
}

UiController::UiController(ITransport* transport,
                           PlayerSyncController* playerSync,
                           IWatchPartyAccountBridge* accountBridge,
                           QObject* parent)
    : QObject(parent),
      m_transport(transport),
      m_playerSync(playerSync),
      m_accountBridge(accountBridge)
{
    initialize(m_transport, m_accountBridge);
}

UiController::~UiController()
{
    clearServiceHandlers();
    if (m_playerSync)
        disconnect(m_playerSync, nullptr, this, nullptr);
}

void UiController::initialize(ITransport* transport,
                              IWatchPartyAccountBridge* accountBridge)
{
    m_transport = transport;
    m_accountBridge = accountBridge;
    m_service = std::make_unique<RoomServiceClient>(m_transport);
    m_identity =
        std::make_unique<IdentityCoordinator>(m_service.get(), m_accountBridge);
    installServiceHandlers();

    if (m_playerSync) {
        connect(
            m_playerSync,
            &PlayerSyncController::timelineCommandRequested,
            this,
            [this](const QString& type,
                   bool hasPosition,
                   double positionSeconds) {
                onTimelineCommandRequested(type, hasPosition, positionSeconds);
            });
        connect(
            m_playerSync,
            &PlayerSyncController::changed,
            this,
            &UiController::observabilityChanged);
    }
}

void UiController::installServiceHandlers()
{
    if (!m_service)
        return;

    m_service->setSessionHandler(
        [this](const QString& roomId, const QString& participantId) {
            onSessionEstablished(roomId, participantId);
        });
    m_service->setSnapshotHandler(
        [this](const RoomSnapshot& snapshot) {
            onSnapshot(snapshot);
        });
    m_service->setTimelineHandler(
        [this](const TimelineState& timeline) {
            onTimeline(timeline);
        });
    m_service->setParticipantHandler(
        [this](const ParticipantState& participant) {
            onParticipant(participant);
        });
    m_service->setHostChangedHandler(
        [this](const QString& hostParticipantId) {
            onHostChanged(hostParticipantId);
        });
    m_service->setChatHandler(
        [this](const ChatEvent& event) {
            onChat(event);
        });
    m_service->setReactionHandler(
        [this](const ReactionEvent& event) {
            onReaction(event);
        });
    m_service->setRoomEndedHandler(
        [this] {
            onRoomEnded();
        });
    m_service->setErrorHandler(
        [this](const RoomServiceError& error) {
            onServiceError(error);
        });
    m_service->setTransportStateHandler(
        [this](TransportState state) {
            onTransportState(state);
        });
}

void UiController::clearServiceHandlers()
{
    if (!m_service)
        return;

    m_service->setSessionHandler({});
    m_service->setSnapshotHandler({});
    m_service->setTimelineHandler({});
    m_service->setParticipantHandler({});
    m_service->setHostChangedHandler({});
    m_service->setChatHandler({});
    m_service->setReactionHandler({});
    m_service->setRoomEndedHandler({});
    m_service->setErrorHandler({});
    m_service->setTransportStateHandler({});
}

bool UiController::configureServiceUrl(const QUrl& serviceUrl)
{
    if (m_inRoom
        || (m_transport && m_transport->state() != TransportState::Closed)
        || m_pendingAction != PendingAction::None) {
        setError(
            QStringLiteral("serviceBusy"),
            QStringLiteral("Leave the current Watch Party before changing its service."));
        return false;
    }

    TransportOpenOptions options;
    options.serviceUrl = serviceUrl;
    QString detail;
    if (!options.isValid(&detail)) {
        m_serviceUrl = {};
        if (m_serviceConfigured) {
            m_serviceConfigured = false;
            Q_EMIT serviceChanged();
        }
        setError(QStringLiteral("invalidService"), detail);
        return false;
    }

    const bool changed =
        !m_serviceConfigured || m_serviceUrl != options.serviceUrl;
    m_serviceUrl = options.serviceUrl;
    m_serviceConfigured = true;
    if (changed)
        Q_EMIT serviceChanged();
    return true;
}

bool UiController::setAccountBridge(IWatchPartyAccountBridge* accountBridge)
{
    if (m_inRoom
        || (m_transport && m_transport->state() != TransportState::Closed)
        || m_pendingAction != PendingAction::None) {
        setError(
            QStringLiteral("identityBusy"),
            QStringLiteral("Leave the current Watch Party before changing accounts."));
        return false;
    }

    if (m_accountBridge == accountBridge)
        return true;

    m_identity.reset();
    m_ownedAccountBridge.reset();
    m_accountBridge = accountBridge;
    m_identity =
        std::make_unique<IdentityCoordinator>(m_service.get(), m_accountBridge);
    m_serviceIdentityMode = ServiceIdentityMode::None;
    Q_EMIT identityChanged();
    return true;
}

bool UiController::setOwnedAccountBridge(
    std::unique_ptr<IWatchPartyAccountBridge> accountBridge)
{
    if (m_inRoom
        || (m_transport && m_transport->state() != TransportState::Closed)
        || m_pendingAction != PendingAction::None) {
        setError(
            QStringLiteral("identityBusy"),
            QStringLiteral("Leave the current Watch Party before changing accounts."));
        return false;
    }

    IWatchPartyAccountBridge* rawBridge = accountBridge.get();
    m_identity.reset();
    m_ownedAccountBridge = std::move(accountBridge);
    m_accountBridge = rawBridge;
    m_identity =
        std::make_unique<IdentityCoordinator>(m_service.get(), m_accountBridge);
    m_serviceIdentityMode = ServiceIdentityMode::None;
    Q_EMIT identityChanged();
    return true;
}

bool UiController::signedIn() const
{
    return m_identity && m_identity->hasSignedInIdentity();
}

QString UiController::signedInUsername() const
{
    return m_identity ? m_identity->signedInUsername() : QString();
}

QString UiController::transportState() const
{
    return m_transport
        ? transportStateName(m_transport->state())
        : QStringLiteral("closed");
}

int UiController::bufferingParticipantCount() const
{
    int count = 0;
    for (const QVariant& rowValue : m_participants) {
        const QVariantMap row = rowValue.toMap();
        if (row.value(QStringLiteral("connected")).toBool()
            && row.value(QStringLiteral("ready")).toBool()
            && row.value(QStringLiteral("syncStatus")).toString()
                == QStringLiteral("buffering")) {
            ++count;
        }
    }
    return count;
}

QString UiController::hostIdentityKind() const
{
    if (!m_service || !m_service->hasSnapshot())
        return QStringLiteral("none");

    const RoomSnapshot snapshot = m_service->snapshot();
    for (const ParticipantState& participant : snapshot.participants) {
        if (participant.identity.participantId
            == snapshot.hostParticipantId) {
            return identityKindName(participant.identity.kind);
        }
    }
    return QStringLiteral("none");
}

QString UiController::localSyncStatus() const
{
    if (!m_inRoom)
        return QStringLiteral("inactive");
    if (!m_localSourceReady)
        return QStringLiteral("sourceUnavailable");
    if (!m_playerSync || !m_playerSync->active())
        return QStringLiteral("unknown");
    return m_playerSync->syncStatus();
}

bool UiController::busy() const
{
    return m_pendingAction != PendingAction::None
        || m_inviteBusy
        || m_phase == QStringLiteral("connecting")
        || m_phase == QStringLiteral("establishing")
        || m_phase == QStringLiteral("synchronizing")
        || m_phase == QStringLiteral("ending");
}

bool UiController::canInvite() const
{
    return m_inRoom && m_localIsHost && signedIn() && !m_inviteBusy
        && m_accountBridge && m_accountBridge->exactUsernameInviteAvailable();
}

bool UiController::canToggleControlMode() const
{
    return m_inRoom && m_localIsHost;
}

bool UiController::canEnd() const
{
    return m_inRoom && m_localIsHost;
}

bool UiController::startParty(const QVariantMap& sourceCandidate)
{
    clearFeedback();

    if (!m_serviceConfigured) {
        setError(
            QStringLiteral("serviceUnavailable"),
            QStringLiteral("Watch Party service is not configured."));
        return false;
    }
    if (m_inRoom || m_pendingAction != PendingAction::None) {
        setError(
            QStringLiteral("roomActive"),
            QStringLiteral("A Watch Party is already active or starting."));
        return false;
    }
    if (!signedIn()) {
        setError(
            QStringLiteral("signedInRequired"),
            QStringLiteral("Sign in before starting a Watch Party."));
        return false;
    }

    const SourceInspection inspection =
        SourceInspector::inspectCandidate(sourceCandidate);
    if (!inspection.eligible()) {
        setError(
            QStringLiteral("unsupportedSource"),
            QStringLiteral(
                "Watch Party requires an exact torrent source or a verified debrid source."));
        return false;
    }

    return beginPendingAction(
        PendingAction::Create,
        inspection.descriptor.normalized());
}

bool UiController::joinRoom(const QString& requestedRoomId,
                            const QString& guestDisplayName)
{
    clearFeedback();

    if (!m_serviceConfigured) {
        setError(
            QStringLiteral("serviceUnavailable"),
            QStringLiteral("Watch Party service is not configured."));
        return false;
    }
    if (m_inRoom || m_pendingAction != PendingAction::None) {
        setError(
            QStringLiteral("roomActive"),
            QStringLiteral("A Watch Party is already active or joining."));
        return false;
    }

    const QString roomId = trimmedRoomId(requestedRoomId);
    if (roomId.isEmpty()) {
        setError(
            QStringLiteral("invalidRoomId"),
            QStringLiteral("Enter a Room ID."));
        return false;
    }

    if (signedIn())
        return beginPendingAction(PendingAction::JoinSignedIn, {}, roomId);

    const QString guestName =
        trimmedSingleLine(guestDisplayName, kMaxGuestNameLength);
    if (guestName.isEmpty()) {
        setError(
            QStringLiteral("guestNameRequired"),
            QStringLiteral("Enter a temporary display name."));
        return false;
    }

    return beginPendingAction(
        PendingAction::JoinGuest, {}, roomId, guestName);
}

bool UiController::beginPendingAction(PendingAction action,
                                      const SourceDescriptor& source,
                                      const QString& requestedRoomId,
                                      const QString& guestDisplayName)
{
    m_pendingAction = action;
    m_pendingSource = source;
    m_pendingRoomId = requestedRoomId;
    m_pendingGuestDisplayName = guestDisplayName;
    Q_EMIT stateChanged();

    if (!ensureServiceIdentityForPendingAction()) {
        resetPendingAction();
        return false;
    }

    if (m_transport && m_transport->state() == TransportState::Connected)
        return dispatchPendingAction();

    return true;
}

bool UiController::ensureServiceIdentityForPendingAction()
{
    if (!m_identity || !m_transport)
        return false;

    const ServiceIdentityMode requiredMode =
        m_pendingAction == PendingAction::JoinGuest
        ? ServiceIdentityMode::Guest
        : ServiceIdentityMode::SignedIn;

    if (m_transport->state() != TransportState::Closed
        && m_serviceIdentityMode == requiredMode) {
        return true;
    }

    if (m_transport->state() != TransportState::Closed) {
        m_identity->closeService();
        m_serviceIdentityMode = ServiceIdentityMode::None;
    }

    setPhase(QStringLiteral("connecting"));
    m_openingService = true;
    const IdentityActionResult result =
        requiredMode == ServiceIdentityMode::Guest
        ? m_identity->openGuestService(m_serviceUrl)
        : m_identity->openSignedInService(m_serviceUrl);
    m_openingService = false;

    if (!result.ok()) {
        setError(
            identityActionErrorName(result.error),
            result.detail.isEmpty()
                ? defaultIdentityErrorText(result.error)
                : result.detail);
        setPhase(QStringLiteral("error"));
        return false;
    }

    m_serviceIdentityMode = requiredMode;
    return true;
}

bool UiController::dispatchPendingAction()
{
    if (!m_identity
        || !m_transport
        || m_transport->state() != TransportState::Connected
        || m_pendingAction == PendingAction::None) {
        return false;
    }

    IdentityActionResult result;
    switch (m_pendingAction) {
    case PendingAction::Create:
        result = m_identity->createRoom(m_pendingSource);
        break;
    case PendingAction::JoinSignedIn:
        result = m_identity->joinSignedIn(m_pendingRoomId);
        break;
    case PendingAction::JoinGuest:
        result = m_identity->joinGuest(
            m_pendingRoomId, m_pendingGuestDisplayName);
        break;
    case PendingAction::None:
        return false;
    }

    if (!result.ok()) {
        setError(
            identityActionErrorName(result.error),
            result.detail.isEmpty()
                ? defaultIdentityErrorText(result.error)
                : result.detail);
        setPhase(QStringLiteral("error"));
        resetPendingAction();
        return false;
    }

    resetPendingAction();
    setPhase(QStringLiteral("establishing"));
    return true;
}

bool UiController::setSharedControl(bool enabled)
{
    clearFeedback();
    if (!m_service || !canToggleControlMode()) {
        setError(
            QStringLiteral("hostRequired"),
            QStringLiteral("Only the host can change room controls."));
        return false;
    }

    const ControlMode requested =
        enabled ? ControlMode::SharedControl : ControlMode::HostControl;
    if (!m_service->setControlMode(requested)) {
        setError(
            QStringLiteral("serviceRejected"),
            QStringLiteral("Could not change Watch Party controls."));
        return false;
    }
    return true;
}

bool UiController::inviteExactUsername(const QString& exactUsername)
{
    clearFeedback();
    if (!m_identity || !canInvite()) {
        setError(
            QStringLiteral("hostRequired"),
            QStringLiteral("Only the signed-in host can invite by username."));
        return false;
    }

    const QString target = trimmedSingleLine(exactUsername, 128);
    if (target.isEmpty()) {
        setError(
            QStringLiteral("invalidInviteTarget"),
            QStringLiteral("Enter an exact username."));
        return false;
    }

    m_inviteBusy = true;
    Q_EMIT stateChanged();

    QPointer<UiController> guard(this);
    const IdentityActionResult result =
        m_identity->inviteExactUsername(
            target,
            [guard](const InviteDeliveryResult& delivery) {
                if (!guard)
                    return;
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, delivery] {
                        if (!guard)
                            return;
                        guard->m_inviteBusy = false;
                        Q_EMIT guard->stateChanged();
                        if (delivery.ok()) {
                            guard->setNotice(
                                QStringLiteral("Invite handed to the account service."));
                        } else {
                            guard->setError(
                                QStringLiteral("inviteRejected"),
                                delivery.detail.isEmpty()
                                    ? QStringLiteral("The invite could not be delivered.")
                                    : delivery.detail);
                        }
                    },
                    Qt::QueuedConnection);
            });

    if (!result.ok()) {
        m_inviteBusy = false;
        Q_EMIT stateChanged();
        setError(
            identityActionErrorName(result.error),
            result.detail.isEmpty()
                ? defaultIdentityErrorText(result.error)
                : result.detail);
        return false;
    }

    return true;
}

bool UiController::removeParticipant(const QString& participantId)
{
    clearFeedback();
    if (!m_service || !m_inRoom || !m_localIsHost) {
        setError(
            QStringLiteral("hostRequired"),
            QStringLiteral("Only the host can remove participants."));
        return false;
    }

    const QString target = participantId.trimmed();
    if (target.isEmpty() || target == m_localParticipantId) {
        setError(
            QStringLiteral("invalidParticipant"),
            QStringLiteral("Choose another participant."));
        return false;
    }

    if (!m_service->removeParticipant(target)) {
        setError(
            QStringLiteral("serviceRejected"),
            QStringLiteral("Could not remove that participant."));
        return false;
    }
    return true;
}

bool UiController::sendChat(const QString& message)
{
    clearFeedback();
    if (!m_service || !canChat())
        return false;

    const QString text = message.trimmed().left(kMaxChatLength);
    if (text.isEmpty())
        return false;

    if (!m_service->sendChat(text)) {
        setError(
            QStringLiteral("serviceRejected"),
            QStringLiteral("Message could not be sent."));
        return false;
    }
    return true;
}

bool UiController::sendReaction(const QString& reaction)
{
    clearFeedback();
    if (!m_service || !canChat())
        return false;

    const QString value =
        trimmedSingleLine(reaction, kMaxReactionLength);
    if (value.isEmpty())
        return false;

    if (!m_service->sendReaction(value)) {
        setError(
            QStringLiteral("serviceRejected"),
            QStringLiteral("Reaction could not be sent."));
        return false;
    }
    return true;
}

bool UiController::catchUp()
{
    clearFeedback();
    if (!m_inRoom || !m_playerSync)
        return false;

    if (!m_playerSync->catchUp(QDateTime::currentMSecsSinceEpoch())) {
        setError(
            QStringLiteral("catchUpUnavailable"),
            QStringLiteral("There is no room position to catch up to yet."));
        return false;
    }
    return true;
}

bool UiController::leaveParty()
{
    clearFeedback();
    if (!m_service || !m_inRoom)
        return false;

    if (!m_service->leaveRoom()) {
        setError(
            QStringLiteral("serviceRejected"),
            QStringLiteral("Could not leave the Watch Party."));
        return false;
    }

    if (m_playerSync)
        m_playerSync->deactivate();
    clearRoomPresentation();
    setPhase(QStringLiteral("idle"));
    setNotice(QStringLiteral("Left Watch Party."));
    return true;
}

bool UiController::endParty()
{
    clearFeedback();
    if (!m_service || !canEnd()) {
        setError(
            QStringLiteral("hostRequired"),
            QStringLiteral("Only the host can end the Watch Party."));
        return false;
    }

    if (!m_service->endRoom()) {
        setError(
            QStringLiteral("serviceRejected"),
            QStringLiteral("Could not end the Watch Party."));
        return false;
    }

    setPhase(QStringLiteral("ending"));
    return true;
}

void UiController::setLocalSourceReady(bool exactRoomSourceReady)
{
    const bool ready = m_inRoom && exactRoomSourceReady;
    if (m_localSourceReady == ready)
        return;

    m_localSourceReady = ready;
    updatePlayerSyncActivation();
    Q_EMIT roomChanged();
}

void UiController::updateLocalParticipantState(bool ready,
                                               const QString& syncStatus)
{
    ready = ready && m_localSourceReady;
    if (!m_service
        || !m_inRoom
        || m_service->transportState() != TransportState::Connected) {
        return;
    }

    SyncStatus parsed = SyncStatus::Unknown;
    if (!syncStatusFromName(syncStatus, &parsed))
        parsed = SyncStatus::Unknown;
    if (!ready)
        parsed = SyncStatus::Unknown;

    if (m_havePublishedParticipantState
        && m_lastPublishedReady == ready
        && m_lastPublishedSyncStatus == parsed) {
        return;
    }

    if (m_service->publishParticipantState(ready, parsed)) {
        m_havePublishedParticipantState = true;
        m_lastPublishedReady = ready;
        m_lastPublishedSyncStatus = parsed;
    }
}

void UiController::clearFeedback()
{
    const bool changed =
        !m_errorCategory.isEmpty()
        || !m_errorText.isEmpty()
        || !m_noticeText.isEmpty();
    m_errorCategory.clear();
    m_errorText.clear();
    m_noticeText.clear();
    if (changed) {
        Q_EMIT feedbackChanged();
        Q_EMIT observabilityChanged();
    }
}

void UiController::refreshIdentity()
{
    Q_EMIT identityChanged();
    Q_EMIT roomChanged();
}

void UiController::handleAccountIdentityChanged()
{
    Q_EMIT identityChanged();

    if (m_serviceIdentityMode != ServiceIdentityMode::SignedIn)
        return;

    if (m_inRoom && m_service)
        m_service->leaveRoom();
    if (m_playerSync)
        m_playerSync->deactivate();
    if (m_identity)
        m_identity->closeService();
    m_serviceIdentityMode = ServiceIdentityMode::None;
    resetPendingAction();
    clearRoomPresentation();
    setPhase(QStringLiteral("idle"));
}

QVariantMap UiController::diagnosticSnapshot() const
{
    QVariantMap result;
    result.insert(QStringLiteral("protocolVersion"), kProtocolVersion);
    result.insert(QStringLiteral("roomState"), roomState());
    result.insert(QStringLiteral("transportState"), transportState());
    result.insert(QStringLiteral("errorCategory"), m_errorCategory);
    result.insert(QStringLiteral("participantCount"), participantCount());
    result.insert(
        QStringLiteral("bufferingParticipantCount"),
        bufferingParticipantCount());
    result.insert(QStringLiteral("hostIdentityKind"), hostIdentityKind());
    result.insert(QStringLiteral("hostGraceActive"), m_hostGraceActive);
    result.insert(QStringLiteral("controlMode"), m_controlMode);
    result.insert(QStringLiteral("localSyncStatus"), localSyncStatus());
    result.insert(QStringLiteral("localSourceReady"), m_localSourceReady);
    result.insert(QStringLiteral("inRoom"), m_inRoom);
    result.insert(QStringLiteral("localIsHost"), m_localIsHost);
    return result;
}

void UiController::onSessionEstablished(const QString& roomId,
                                        const QString& participantId)
{
    m_roomId = roomId;
    m_localParticipantId = participantId;
    setPhase(QStringLiteral("synchronizing"));
    Q_EMIT roomChanged();
}

void UiController::onSnapshot(const RoomSnapshot& snapshot)
{
    const bool wasInRoom = m_inRoom;
    applySnapshotPresentation(snapshot);
    updatePlayerSyncActivation();
    setPhase(QStringLiteral("active"));
    if (!wasInRoom && m_inRoom)
        Q_EMIT roomActivated(m_roomId);
}

void UiController::onTimeline(const TimelineState& timeline)
{
    if (!m_inRoom || !m_playerSync || !m_playerSync->active())
        return;

    m_playerSync->applyAuthoritativeTimeline(
        timeline.playing,
        static_cast<double>(timeline.positionMs) / 1000.0,
        timeline.revision,
        QDateTime::currentMSecsSinceEpoch());
}

void UiController::onParticipant(const ParticipantState&)
{
    refreshSnapshotPresentation();
}

void UiController::onHostChanged(const QString&)
{
    refreshSnapshotPresentation();
}

void UiController::onChat(const ChatEvent& event)
{
    if (!m_inRoom)
        return;
    m_chatMessages.append(chatToVariant(event));
    Q_EMIT chatChanged();
}

void UiController::onReaction(const ReactionEvent& event)
{
    if (!m_inRoom)
        return;
    m_reactions.append(reactionToVariant(event));
    Q_EMIT reactionsChanged();
}

void UiController::onRoomEnded()
{
    if (m_playerSync)
        m_playerSync->deactivate();
    clearRoomPresentation();
    setPhase(QStringLiteral("idle"));
    setNotice(QStringLiteral("Watch Party ended."));
}

void UiController::onServiceError(const RoomServiceError& error)
{
    setError(
        roomServiceErrorCodeName(error.code),
        error.detail.isEmpty()
            ? defaultRoomServiceErrorText(error.code)
            : error.detail);

    if (!m_service || !m_service->hasSession()) {
        if (m_playerSync)
            m_playerSync->deactivate();
        clearRoomPresentation();
        setPhase(QStringLiteral("error"));
    }
}

void UiController::onTransportState(TransportState state)
{
    switch (state) {
    case TransportState::Closed:
        m_serviceIdentityMode = ServiceIdentityMode::None;
        if (m_inRoom)
            setPhase(QStringLiteral("error"));
        break;
    case TransportState::Connecting:
        setPhase(QStringLiteral("connecting"));
        break;
    case TransportState::Connected:
        if (m_pendingAction != PendingAction::None && !m_openingService)
            dispatchPendingAction();
        break;
    case TransportState::WaitingToReconnect:
    case TransportState::Reconnecting:
        if (m_inRoom)
            setPhase(QStringLiteral("reconnecting"));
        break;
    }

    Q_EMIT observabilityChanged();
}

void UiController::onTimelineCommandRequested(const QString& type,
                                              bool hasPosition,
                                              double positionSeconds)
{
    if (!m_service || !m_inRoom)
        return;

    TimelineCommand command;
    if (type == QStringLiteral("play")) {
        command = hasPosition && finiteNonNegative(positionSeconds)
            ? TimelineCommand::playAt(
                  qRound64(positionSeconds * 1000.0))
            : TimelineCommand::play();
    } else if (type == QStringLiteral("pause")) {
        command = hasPosition && finiteNonNegative(positionSeconds)
            ? TimelineCommand::pauseAt(
                  qRound64(positionSeconds * 1000.0))
            : TimelineCommand::pause();
    } else if (type == QStringLiteral("seek")
               && hasPosition
               && finiteNonNegative(positionSeconds)) {
        command = TimelineCommand::seek(
            qRound64(positionSeconds * 1000.0));
    } else {
        return;
    }

    if (!m_service->sendTimelineCommand(command)) {
        setError(
            QStringLiteral("timelineRejected"),
            QStringLiteral("Room playback control was rejected."));
    }
}

void UiController::applySnapshotPresentation(const RoomSnapshot& snapshot)
{
    m_inRoom = true;
    m_roomId = snapshot.roomId;
    m_localParticipantId =
        m_service ? m_service->participantId() : m_localParticipantId;
    m_localIsHost =
        !m_localParticipantId.isEmpty()
        && snapshot.hostParticipantId == m_localParticipantId;
    m_controlMode = controlModeName(snapshot.controlMode);
    m_hostGraceActive = snapshot.hostReconnectDeadlineMs >= 0;
    m_roomSource = sourceDescriptorToVariant(snapshot.source);

    QVariantList participantRows;
    participantRows.reserve(snapshot.participants.size());
    for (const ParticipantState& participant : snapshot.participants)
        participantRows.append(participantToVariant(participant));
    m_participants = participantRows;

    Q_EMIT roomChanged();
    Q_EMIT participantsChanged();
    Q_EMIT stateChanged();
    Q_EMIT observabilityChanged();
}

void UiController::refreshSnapshotPresentation()
{
    if (!m_service || !m_service->hasSnapshot())
        return;

    const RoomSnapshot snapshot = m_service->snapshot();
    applySnapshotPresentation(snapshot);
    if (m_playerSync)
        m_playerSync->updateAuthority(
            snapshot.hostParticipantId,
            controlModeName(snapshot.controlMode));
}

void UiController::updatePlayerSyncActivation()
{
    if (!m_playerSync)
        return;

    if (!m_inRoom
        || !m_localSourceReady
        || m_localParticipantId.isEmpty()
        || !m_service
        || !m_service->hasSnapshot()) {
        if (m_playerSync->active())
            m_playerSync->deactivate();
        return;
    }

    const RoomSnapshot snapshot = m_service->snapshot();
    if (!m_playerSync->active()) {
        m_playerSync->activate(
            m_localParticipantId,
            snapshot.hostParticipantId,
            controlModeName(snapshot.controlMode));
    } else {
        m_playerSync->updateAuthority(
            snapshot.hostParticipantId,
            controlModeName(snapshot.controlMode));
    }

    m_playerSync->applyAuthoritativeTimeline(
        snapshot.timeline.playing,
        static_cast<double>(snapshot.timeline.positionMs) / 1000.0,
        snapshot.timeline.revision,
        QDateTime::currentMSecsSinceEpoch());
}

void UiController::clearRoomPresentation()
{
    const bool hadParticipants = !m_participants.isEmpty();
    const bool hadChat = !m_chatMessages.isEmpty();
    const bool hadReactions = !m_reactions.isEmpty();

    m_inRoom = false;
    m_roomId.clear();
    m_localParticipantId.clear();
    m_localIsHost = false;
    m_controlMode = QStringLiteral("host");
    m_hostGraceActive = false;
    m_roomSource.clear();
    m_localSourceReady = false;
    m_participants.clear();
    m_chatMessages.clear();
    m_reactions.clear();

    m_havePublishedParticipantState = false;
    m_lastPublishedReady = false;
    m_lastPublishedSyncStatus = SyncStatus::Unknown;

    Q_EMIT roomChanged();
    if (hadParticipants)
        Q_EMIT participantsChanged();
    if (hadChat)
        Q_EMIT chatChanged();
    if (hadReactions)
        Q_EMIT reactionsChanged();
    Q_EMIT stateChanged();
    Q_EMIT observabilityChanged();
}

void UiController::setPhase(const QString& phase)
{
    if (m_phase == phase)
        return;
    m_phase = phase;
    Q_EMIT stateChanged();
    Q_EMIT observabilityChanged();
}

void UiController::setError(const QString& category, const QString& text)
{
    const bool changed =
        m_errorCategory != category
        || m_errorText != text
        || !m_noticeText.isEmpty();
    m_errorCategory = category;
    m_errorText = text;
    m_noticeText.clear();
    if (changed) {
        Q_EMIT feedbackChanged();
        Q_EMIT observabilityChanged();
    }
}

void UiController::setNotice(const QString& text)
{
    const bool changed =
        m_noticeText != text
        || !m_errorCategory.isEmpty()
        || !m_errorText.isEmpty();
    m_noticeText = text;
    m_errorCategory.clear();
    m_errorText.clear();
    if (changed)
        Q_EMIT feedbackChanged();
}

void UiController::resetPendingAction()
{
    if (m_pendingAction == PendingAction::None)
        return;

    m_pendingAction = PendingAction::None;
    m_pendingSource = {};
    m_pendingRoomId.clear();
    m_pendingGuestDisplayName.clear();
    Q_EMIT stateChanged();
}

QVariantMap UiController::sourceDescriptorToVariant(
    const SourceDescriptor& source)
{
    const SourceDescriptor normalized = source.normalized();
    if (!normalized.isValid())
        return {};

    QVariantMap result;
    result.insert(
        QStringLiteral("kind"),
        sourceKindName(normalized.kind));
    if (normalized.kind == SourceKind::Torrent) {
        result.insert(QStringLiteral("infoHash"), normalized.infoHash);
        result.insert(QStringLiteral("fileIdx"), normalized.fileIdx);
    } else if (normalized.kind == SourceKind::Debrid) {
        result.insert(QStringLiteral("providerId"), normalized.providerId);
        result.insert(
            QStringLiteral("providerSourceId"),
            normalized.providerSourceId);
    }
    return result;
}

QVariantMap UiController::participantToVariant(
    const ParticipantState& participant) const
{
    QVariantMap result;
    result.insert(
        QStringLiteral("participantId"),
        participant.identity.participantId);
    result.insert(
        QStringLiteral("displayName"),
        participant.identity.displayName);
    result.insert(
        QStringLiteral("identityKind"),
        identityKindName(participant.identity.kind));
    result.insert(QStringLiteral("host"), participant.host);
    result.insert(QStringLiteral("connected"), participant.connected);
    result.insert(QStringLiteral("ready"), participant.ready);
    result.insert(
        QStringLiteral("syncStatus"),
        syncStatusName(participant.syncStatus));
    result.insert(
        QStringLiteral("local"),
        participant.identity.participantId == m_localParticipantId);
    return result;
}

QVariantMap UiController::chatToVariant(const ChatEvent& event)
{
    QVariantMap result;
    result.insert(
        QStringLiteral("sequence"),
        QVariant::fromValue<qulonglong>(event.sequence));
    result.insert(QStringLiteral("participantId"), event.participantId);
    result.insert(QStringLiteral("displayName"), event.displayName);
    result.insert(QStringLiteral("message"), event.message);
    return result;
}

QVariantMap UiController::reactionToVariant(const ReactionEvent& event)
{
    QVariantMap result;
    result.insert(
        QStringLiteral("sequence"),
        QVariant::fromValue<qulonglong>(event.sequence));
    result.insert(QStringLiteral("participantId"), event.participantId);
    result.insert(QStringLiteral("displayName"), event.displayName);
    result.insert(QStringLiteral("reaction"), event.reaction);
    return result;
}

QString UiController::trimmedRoomId(const QString& roomId)
{
    return trimmedSingleLine(roomId, kMaxRoomIdLength);
}

QString UiController::trimmedSingleLine(const QString& value, int maxLength)
{
    QString result = value.trimmed();
    result.replace(QLatin1Char('\r'), QLatin1Char(' '));
    result.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return result.left(maxLength).trimmed();
}

} // namespace Colosseum::WatchParty
