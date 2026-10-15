#include "watchparty/WatchPartyRoomServiceClient.h"

#include "watchparty/WatchPartyProtocol.h"

#include <QJsonObject>

#include <limits>
#include <utility>

namespace Colosseum::WatchParty {

namespace {

RoomServiceErrorCode classifyTransportError(const TransportError& error)
{
    switch (error.code) {
    case TransportErrorCode::ProtocolVersionMismatch:
        return RoomServiceErrorCode::ProtocolVersionMismatch;
    case TransportErrorCode::ProtocolRejected:
        return RoomServiceErrorCode::ProtocolFailure;
    case TransportErrorCode::RateLimited:
        return RoomServiceErrorCode::RateLimited;
    case TransportErrorCode::MessageTooLarge:
        return RoomServiceErrorCode::InvalidMessage;
    case TransportErrorCode::None:
    case TransportErrorCode::InvalidConfiguration:
    case TransportErrorCode::NotConnected:
    case TransportErrorCode::SocketError:
    case TransportErrorCode::SendFailed:
        return RoomServiceErrorCode::TransportFailure;
    }
    return RoomServiceErrorCode::TransportFailure;
}

RoomServiceErrorCode classifyServerError(const QString& rawCode)
{
    const QString code = rawCode.trimmed();
    if (code == QStringLiteral("unauthenticated"))
        return RoomServiceErrorCode::Unauthenticated;
    if (code == QStringLiteral("room_not_found"))
        return RoomServiceErrorCode::RoomNotFound;
    if (code == QStringLiteral("room_full"))
        return RoomServiceErrorCode::RoomFull;
    if (code == QStringLiteral("room_ended"))
        return RoomServiceErrorCode::RoomEnded;
    if (code == QStringLiteral("participant_removed"))
        return RoomServiceErrorCode::ParticipantRemoved;
    if (code == QStringLiteral("not_authorized"))
        return RoomServiceErrorCode::NotAuthorized;
    if (code == QStringLiteral("invalid_source"))
        return RoomServiceErrorCode::InvalidSource;
    if (code == QStringLiteral("invalid_message"))
        return RoomServiceErrorCode::InvalidMessage;
    if (code == QStringLiteral("protocol_version_mismatch"))
        return RoomServiceErrorCode::ProtocolVersionMismatch;
    if (code == QStringLiteral("rate_limited"))
        return RoomServiceErrorCode::RateLimited;
    return RoomServiceErrorCode::ServerRejected;
}

bool terminatesRoomSession(RoomServiceErrorCode code)
{
    switch (code) {
    case RoomServiceErrorCode::Unauthenticated:
    case RoomServiceErrorCode::RoomNotFound:
    case RoomServiceErrorCode::RoomEnded:
    case RoomServiceErrorCode::ParticipantRemoved:
    case RoomServiceErrorCode::ProtocolVersionMismatch:
        return true;
    case RoomServiceErrorCode::None:
    case RoomServiceErrorCode::InvalidRequest:
    case RoomServiceErrorCode::NotConnected:
    case RoomServiceErrorCode::NoSession:
    case RoomServiceErrorCode::ProtocolFailure:
    case RoomServiceErrorCode::TransportFailure:
    case RoomServiceErrorCode::RoomFull:
    case RoomServiceErrorCode::NotAuthorized:
    case RoomServiceErrorCode::InvalidSource:
    case RoomServiceErrorCode::InvalidMessage:
    case RoomServiceErrorCode::RateLimited:
    case RoomServiceErrorCode::ServerRejected:
        return false;
    }
    return false;
}

} // namespace

RoomServiceClient::RoomServiceClient(ITransport* transport)
    : m_transport(transport)
{
    if (!m_transport)
        return;

    m_transport->setReceiveHandler(
        [this](const ProtocolMessage& message) {
            onMessage(message);
        });
    m_transport->setStateHandler(
        [this](TransportState state) {
            onTransportState(state);
        });
    m_transport->setErrorHandler(
        [this](const TransportError& error) {
            onTransportError(error);
        });
}

RoomServiceClient::~RoomServiceClient()
{
    if (!m_transport)
        return;

    m_transport->setReceiveHandler({});
    m_transport->setStateHandler({});
    m_transport->setErrorHandler({});
}

void RoomServiceClient::setSessionHandler(SessionHandler handler)
{
    m_sessionHandler = std::move(handler);
}

void RoomServiceClient::setSnapshotHandler(SnapshotHandler handler)
{
    m_snapshotHandler = std::move(handler);
}

void RoomServiceClient::setTimelineHandler(TimelineHandler handler)
{
    m_timelineHandler = std::move(handler);
}

void RoomServiceClient::setParticipantHandler(ParticipantHandler handler)
{
    m_participantHandler = std::move(handler);
}

void RoomServiceClient::setHostChangedHandler(HostChangedHandler handler)
{
    m_hostChangedHandler = std::move(handler);
}

void RoomServiceClient::setChatHandler(ChatHandler handler)
{
    m_chatHandler = std::move(handler);
}

void RoomServiceClient::setReactionHandler(ReactionHandler handler)
{
    m_reactionHandler = std::move(handler);
}

void RoomServiceClient::setRoomEndedHandler(RoomEndedHandler handler)
{
    m_roomEndedHandler = std::move(handler);
}

void RoomServiceClient::setErrorHandler(ErrorHandler handler)
{
    m_errorHandler = std::move(handler);
}

void RoomServiceClient::setTransportStateHandler(
    TransportStateHandler handler)
{
    m_transportStateHandler = std::move(handler);
}

bool RoomServiceClient::openService(
    const QUrl& serviceUrl,
    const QByteArray& bearerToken)
{
    if (!m_transport) {
        reportError(
            RoomServiceErrorCode::TransportFailure,
            QStringLiteral("Watch Party transport is unavailable"));
        return false;
    }

    if (m_transport->state() != TransportState::Closed) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral(
                "Watch Party service is already open or reconnecting"));
        return false;
    }

    if (hasSession() || m_pendingOperation != PendingOperation::None) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral(
                "close the current Watch Party session before opening a new service"));
        return false;
    }

    TransportOpenOptions options;
    options.serviceUrl = serviceUrl;
    options.bearerToken = bearerToken;

    m_signedInCredentialPresent = !bearerToken.isEmpty();
    if (!m_transport->open(options)) {
        m_signedInCredentialPresent = false;
        return false;
    }

    return true;
}

void RoomServiceClient::closeService()
{
    if (m_transport)
        m_transport->close();

    m_signedInCredentialPresent = false;
    m_pendingOperation = PendingOperation::None;
    m_requestedRoomId.clear();
    clearSession();
}

bool RoomServiceClient::retryTransportNow()
{
    if (!m_transport)
        return false;
    return m_transport->retryNow();
}

TransportState RoomServiceClient::transportState() const
{
    return m_transport
        ? m_transport->state()
        : TransportState::Closed;
}

bool RoomServiceClient::createRoom(const SourceDescriptor& source)
{
    if (!requireConnected(QStringLiteral("create room"))
        || !requireNoSession(QStringLiteral("create room"))) {
        return false;
    }

    if (!m_signedInCredentialPresent) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral("only an authenticated identity can create a room"));
        return false;
    }

    const SourceDescriptor descriptor = source.normalized();
    if (!descriptor.isValid()) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral("room source descriptor is invalid"));
        return false;
    }

    ProtocolMessage message;
    message.version = kProtocolVersion;
    message.type = MessageType::CreateRoom;
    message.sequence = nextSequence();
    message.payload = QJsonObject{
        {
            QStringLiteral("source"),
            sourceDescriptorToJson(descriptor)
        }
    };

    if (!m_transport->send(message))
        return false;

    m_pendingOperation = PendingOperation::Create;
    m_requestedRoomId.clear();
    return true;
}

bool RoomServiceClient::joinSignedIn(const QString& roomId)
{
    if (!requireConnected(QStringLiteral("join room"))
        || !requireNoSession(QStringLiteral("join room"))) {
        return false;
    }

    if (!m_signedInCredentialPresent) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral(
                "signed-in join requires an authenticated identity"));
        return false;
    }

    const QString cleanedRoomId = roomId.trimmed();
    if (cleanedRoomId.isEmpty()) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral("room ID must not be empty"));
        return false;
    }

    ProtocolMessage message;
    message.version = kProtocolVersion;
    message.type = MessageType::JoinRoom;
    message.roomId = cleanedRoomId;
    message.sequence = nextSequence();
    message.payload = QJsonObject{
        {
            QStringLiteral("identityKind"),
            identityKindName(IdentityKind::SignedIn)
        }
    };

    if (!m_transport->send(message))
        return false;

    m_pendingOperation = PendingOperation::Join;
    m_requestedRoomId = cleanedRoomId;
    return true;
}

bool RoomServiceClient::joinGuest(
    const QString& roomId,
    const QString& displayName)
{
    if (!requireConnected(QStringLiteral("join room"))
        || !requireNoSession(QStringLiteral("join room"))) {
        return false;
    }

    if (m_signedInCredentialPresent) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral(
                "authenticated sessions must join with signed-in identity"));
        return false;
    }

    const QString cleanedRoomId = roomId.trimmed();
    const QString cleanedName = displayName.trimmed();
    if (cleanedRoomId.isEmpty() || cleanedName.isEmpty()) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral(
                "guest join requires a room ID and temporary display name"));
        return false;
    }

    ProtocolMessage message;
    message.version = kProtocolVersion;
    message.type = MessageType::JoinRoom;
    message.roomId = cleanedRoomId;
    message.sequence = nextSequence();
    message.payload = QJsonObject{
        {
            QStringLiteral("identityKind"),
            identityKindName(IdentityKind::Guest)
        },
        {
            QStringLiteral("displayName"),
            cleanedName
        }
    };

    if (!m_transport->send(message))
        return false;

    m_pendingOperation = PendingOperation::Join;
    m_requestedRoomId = cleanedRoomId;
    return true;
}

bool RoomServiceClient::leaveRoom()
{
    if (!requireConnected(QStringLiteral("leave room"))
        || !requireSession(QStringLiteral("leave room"))) {
        return false;
    }

    const bool sent =
        sendSessionMessage(MessageType::LeaveRoom, {});
    if (sent)
        clearSession();
    return sent;
}

bool RoomServiceClient::sendTimelineCommand(
    const TimelineCommand& command)
{
    if (!requireConnected(QStringLiteral("control timeline"))
        || !requireSession(QStringLiteral("control timeline"))) {
        return false;
    }

    return sendSessionMessage(
        MessageType::TimelineCommand,
        timelineCommandToJson(command));
}

bool RoomServiceClient::setControlMode(ControlMode mode)
{
    if (!requireConnected(QStringLiteral("set control mode"))
        || !requireSession(QStringLiteral("set control mode"))) {
        return false;
    }

    return sendSessionMessage(
        MessageType::SetControlMode,
        QJsonObject{
            {
                QStringLiteral("controlMode"),
                controlModeName(mode)
            }
        });
}

bool RoomServiceClient::publishParticipantState(
    bool ready,
    SyncStatus status)
{
    if (!requireConnected(QStringLiteral("publish participant state"))
        || !requireSession(QStringLiteral("publish participant state"))) {
        return false;
    }

    if (!ready && status != SyncStatus::Unknown) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral(
                "non-ready participant must publish unknown sync status"));
        return false;
    }

    return sendSessionMessage(
        MessageType::ParticipantState,
        QJsonObject{
            {QStringLiteral("ready"), ready},
            {
                QStringLiteral("syncStatus"),
                syncStatusName(status)
            }
        });
}

bool RoomServiceClient::removeParticipant(const QString& participantId)
{
    if (!requireConnected(QStringLiteral("remove participant"))
        || !requireSession(QStringLiteral("remove participant"))) {
        return false;
    }

    const QString cleaned = participantId.trimmed();
    if (cleaned.isEmpty()) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral("participant ID must not be empty"));
        return false;
    }

    return sendSessionMessage(
        MessageType::RemoveParticipant,
        QJsonObject{
            {QStringLiteral("participantId"), cleaned}
        });
}

bool RoomServiceClient::sendChat(const QString& message)
{
    if (!requireConnected(QStringLiteral("send chat"))
        || !requireSession(QStringLiteral("send chat"))) {
        return false;
    }

    const QString cleaned = message.trimmed();
    if (cleaned.isEmpty()) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral("chat message must not be empty"));
        return false;
    }

    return sendSessionMessage(
        MessageType::Chat,
        QJsonObject{
            {QStringLiteral("message"), cleaned}
        });
}

bool RoomServiceClient::sendReaction(const QString& reaction)
{
    if (!requireConnected(QStringLiteral("send reaction"))
        || !requireSession(QStringLiteral("send reaction"))) {
        return false;
    }

    const QString cleaned = reaction.trimmed();
    if (cleaned.isEmpty()) {
        reportError(
            RoomServiceErrorCode::InvalidRequest,
            QStringLiteral("reaction must not be empty"));
        return false;
    }

    return sendSessionMessage(
        MessageType::Reaction,
        QJsonObject{
            {QStringLiteral("reaction"), cleaned}
        });
}

bool RoomServiceClient::endRoom()
{
    if (!requireConnected(QStringLiteral("end room"))
        || !requireSession(QStringLiteral("end room"))) {
        return false;
    }

    return sendSessionMessage(MessageType::EndRoom, {});
}

void RoomServiceClient::onTransportState(TransportState state)
{
    if (m_transportStateHandler)
        m_transportStateHandler(state);

    if (state == TransportState::WaitingToReconnect
        && m_pendingOperation != PendingOperation::None
        && !hasSession()) {
        m_pendingOperation = PendingOperation::None;
        m_requestedRoomId.clear();
        reportError(
            RoomServiceErrorCode::TransportFailure,
            QStringLiteral(
                "connection was lost before room creation/join completed"));
        return;
    }

    if (state == TransportState::Connected
        && hasSession()
        && !m_reconnectToken.isEmpty()) {
        sendReconnect();
    }
}

void RoomServiceClient::onTransportError(const TransportError& error)
{
    const RoomServiceErrorCode code = classifyTransportError(error);

    // Terminal transport failures mean the current room credential/session can no
    // longer be trusted. Clear it before notifying presentation so observers
    // cannot mistake stale room/chat state for a recoverable connection.
    if (error.terminal)
        clearSession();

    reportError(
        code,
        QStringLiteral("%1: %2")
            .arg(
                transportErrorCodeName(error.code),
                error.detail));
}

void RoomServiceClient::onMessage(const ProtocolMessage& message)
{
    const ValidationResult validation =
        validateMessage(
            message,
            MessageDirection::ServerToClient);
    if (!validation.ok) {
        protocolFailure(validation.error);
        return;
    }

    switch (message.type) {
    case MessageType::SessionEstablished:
        applySessionEstablished(message);
        return;
    case MessageType::RoomSnapshot:
        applyRoomSnapshot(message);
        return;
    case MessageType::TimelineState:
        applyTimelineState(message);
        return;
    case MessageType::ParticipantState:
        applyParticipantState(message);
        return;
    case MessageType::HostChanged:
        applyHostChanged(message);
        return;
    case MessageType::Chat:
        applyChat(message);
        return;
    case MessageType::Reaction:
        applyReaction(message);
        return;
    case MessageType::RoomEnded:
        applyRoomEnded(message);
        return;
    case MessageType::Error:
        applyServerError(message);
        return;

    case MessageType::CreateRoom:
    case MessageType::JoinRoom:
    case MessageType::ReconnectRoom:
    case MessageType::LeaveRoom:
    case MessageType::TimelineCommand:
    case MessageType::SetControlMode:
    case MessageType::RemoveParticipant:
    case MessageType::EndRoom:
        protocolFailure(
            QStringLiteral("received a client-only protocol message"));
        return;
    }
}

bool RoomServiceClient::sendSessionMessage(
    MessageType type,
    const QJsonObject& payload)
{
    if (!m_transport)
        return false;

    const ProtocolMessage message =
        makeClientMessage(type, payload, true);
    return m_transport->send(message);
}

bool RoomServiceClient::sendReconnect()
{
    if (!m_transport
        || m_transport->state() != TransportState::Connected
        || m_roomId.isEmpty()
        || m_reconnectToken.isEmpty()) {
        return false;
    }

    ProtocolMessage message;
    message.version = kProtocolVersion;
    message.type = MessageType::ReconnectRoom;
    message.roomId = m_roomId;
    message.sequence = nextSequence();
    message.payload = QJsonObject{
        {
            QStringLiteral("reconnectToken"),
            m_reconnectToken
        }
    };

    if (!m_transport->send(message))
        return false;

    m_pendingOperation = PendingOperation::Reconnect;
    return true;
}

bool RoomServiceClient::applySessionEstablished(
    const ProtocolMessage& message)
{
    SessionEstablished session;
    QString error;
    if (!sessionEstablishedFromJson(
            message.payload, &session, &error)) {
        protocolFailure(error);
        return false;
    }

    if (m_pendingOperation == PendingOperation::None) {
        protocolFailure(
            QStringLiteral(
                "unsolicited sessionEstablished message"));
        return false;
    }

    const QString incomingRoomId = message.roomId.trimmed();
    const QString incomingParticipantId =
        session.participantId.trimmed();
    if (incomingRoomId.isEmpty()) {
        protocolFailure(
            QStringLiteral(
                "sessionEstablished requires a non-empty room ID"));
        return false;
    }

    if (m_pendingOperation == PendingOperation::Join
        && incomingRoomId != m_requestedRoomId) {
        protocolFailure(
            QStringLiteral(
                "joined room does not match requested room ID"));
        return false;
    }

    if (m_pendingOperation == PendingOperation::Reconnect) {
        if (!hasSession()) {
            protocolFailure(
                QStringLiteral(
                    "reconnect acknowledgement arrived without an existing session"));
            return false;
        }
        if (incomingRoomId != m_roomId) {
            protocolFailure(
                QStringLiteral(
                    "reconnected session changed room identity"));
            return false;
        }
        if (incomingParticipantId != m_participantId) {
            protocolFailure(
                QStringLiteral(
                    "reconnected session changed participant identity"));
            return false;
        }
    } else if (hasSession()) {
        protocolFailure(
            QStringLiteral(
                "new session acknowledgement arrived while a room session already exists"));
        return false;
    }

    m_roomId = incomingRoomId;
    m_participantId = incomingParticipantId;
    m_reconnectToken = session.reconnectToken;
    m_pendingOperation = PendingOperation::None;
    m_requestedRoomId.clear();

    if (m_sessionHandler)
        m_sessionHandler(m_roomId, m_participantId);
    return true;
}

bool RoomServiceClient::applyRoomSnapshot(
    const ProtocolMessage& message)
{
    if (!requireSession(QStringLiteral("apply room snapshot")))
        return false;
    if (!requireMatchingRoom(message))
        return false;

    RoomSnapshot snapshot;
    QString error;
    if (!roomSnapshotFromJson(
            message.payload, &snapshot, &error)) {
        protocolFailure(error);
        return false;
    }

    bool localParticipantFound = false;
    for (const ParticipantState& participant : snapshot.participants) {
        if (participant.identity.participantId == m_participantId) {
            localParticipantFound = true;
            break;
        }
    }

    if (!localParticipantFound) {
        protocolFailure(
            QStringLiteral(
                "room snapshot does not contain the established participant"));
        return false;
    }

    m_snapshot = snapshot;
    m_hasSnapshot = true;

    if (m_snapshotHandler)
        m_snapshotHandler(m_snapshot);
    return true;
}

bool RoomServiceClient::applyTimelineState(
    const ProtocolMessage& message)
{
    if (!requireSession(QStringLiteral("apply timeline state"))
        || !requireMatchingRoom(message)) {
        return false;
    }

    if (!m_hasSnapshot) {
        protocolFailure(
            QStringLiteral(
                "timeline state arrived before initial room snapshot"));
        return false;
    }

    TimelineState timeline;
    QString error;
    if (!timelineStateFromJson(
            message.payload, &timeline, &error)) {
        protocolFailure(error);
        return false;
    }

    if (timeline.revision < m_snapshot.timeline.revision)
        return true;

    if (timeline.revision == m_snapshot.timeline.revision) {
        if (timeline.playing != m_snapshot.timeline.playing
            || timeline.positionMs != m_snapshot.timeline.positionMs) {
            protocolFailure(
                QStringLiteral(
                    "same timeline revision carried conflicting state"));
            return false;
        }
        return true;
    }

    m_snapshot.timeline = timeline;
    if (m_timelineHandler)
        m_timelineHandler(m_snapshot.timeline);
    return true;
}

bool RoomServiceClient::applyParticipantState(
    const ProtocolMessage& message)
{
    if (!requireSession(QStringLiteral("apply participant state"))
        || !requireMatchingRoom(message)) {
        return false;
    }

    if (!m_hasSnapshot) {
        protocolFailure(
            QStringLiteral(
                "participant state arrived before initial room snapshot"));
        return false;
    }

    ParticipantState participant;
    QString error;
    if (!participantStateFromJson(
            message.payload, &participant, &error)) {
        protocolFailure(error);
        return false;
    }

    for (ParticipantState& current : m_snapshot.participants) {
        if (current.identity.participantId
            != participant.identity.participantId) {
            continue;
        }

        if (participant.host != current.host) {
            protocolFailure(
                QStringLiteral(
                    "participantState cannot transfer host authority"));
            return false;
        }

        if (participant.joinOrder != current.joinOrder
            || participant.identity.kind != current.identity.kind) {
            protocolFailure(
                QStringLiteral(
                    "participantState changed immutable participant identity"));
            return false;
        }

        current = participant;
        if (m_participantHandler)
            m_participantHandler(current);
        return true;
    }

    protocolFailure(
        QStringLiteral(
            "participantState references unknown participant"));
    return false;
}

bool RoomServiceClient::applyHostChanged(
    const ProtocolMessage& message)
{
    if (!requireSession(QStringLiteral("apply host transfer"))
        || !requireMatchingRoom(message)) {
        return false;
    }

    if (!m_hasSnapshot) {
        protocolFailure(
            QStringLiteral(
                "host change arrived before initial room snapshot"));
        return false;
    }

    const QString newHostId =
        message.payload.value(
            QStringLiteral("hostParticipantId"))
            .toString()
            .trimmed();

    ParticipantState* newHost = nullptr;
    for (ParticipantState& participant : m_snapshot.participants) {
        if (participant.identity.participantId == newHostId) {
            newHost = &participant;
            break;
        }
    }

    if (!newHost
        || newHost->identity.kind != IdentityKind::SignedIn
        || !newHost->connected) {
        protocolFailure(
            QStringLiteral(
                "hostChanged selected an ineligible participant"));
        return false;
    }

    for (ParticipantState& participant : m_snapshot.participants)
        participant.host = false;

    newHost->host = true;
    m_snapshot.hostParticipantId = newHostId;
    m_snapshot.hostReconnectDeadlineMs = -1;

    if (m_hostChangedHandler)
        m_hostChangedHandler(newHostId);
    return true;
}

bool RoomServiceClient::applyChat(
    const ProtocolMessage& message)
{
    if (!requireSession(QStringLiteral("apply chat"))
        || !requireMatchingRoom(message)) {
        return false;
    }

    ChatEvent event;
    QString error;
    if (!chatEventFromJson(message.payload, &event, &error)) {
        protocolFailure(error);
        return false;
    }

    if (m_chatHandler)
        m_chatHandler(event);
    return true;
}

bool RoomServiceClient::applyReaction(
    const ProtocolMessage& message)
{
    if (!requireSession(QStringLiteral("apply reaction"))
        || !requireMatchingRoom(message)) {
        return false;
    }

    ReactionEvent event;
    QString error;
    if (!reactionEventFromJson(
            message.payload, &event, &error)) {
        protocolFailure(error);
        return false;
    }

    if (m_reactionHandler)
        m_reactionHandler(event);
    return true;
}

bool RoomServiceClient::applyRoomEnded(
    const ProtocolMessage& message)
{
    if (!requireSession(QStringLiteral("end room"))
        || !requireMatchingRoom(message)) {
        return false;
    }

    clearSession();
    if (m_roomEndedHandler)
        m_roomEndedHandler();
    return true;
}

bool RoomServiceClient::applyServerError(
    const ProtocolMessage& message)
{
    const QString rawCode =
        message.payload.value(QStringLiteral("code")).toString().trimmed();
    const QString text =
        message.payload.value(QStringLiteral("message")).toString().trimmed();
    const RoomServiceErrorCode code = classifyServerError(rawCode);

    const PendingOperation pending = m_pendingOperation;
    if (pending != PendingOperation::None) {
        m_pendingOperation = PendingOperation::None;
        m_requestedRoomId.clear();
    }

    if (pending == PendingOperation::Reconnect
        || terminatesRoomSession(code)) {
        clearSession();
    }

    if (code == RoomServiceErrorCode::ProtocolVersionMismatch
        && m_transport) {
        m_transport->close();
    }

    // The typed enum carries machine meaning. Keep human detail optional so
    // presentation can provide a deterministic category-specific fallback
    // instead of leaking raw server machine codes into the UI.
    reportError(code, text);
    return true;
}

bool RoomServiceClient::requireConnected(const QString& action)
{
    if (m_transport
        && m_transport->state() == TransportState::Connected) {
        return true;
    }

    reportError(
        RoomServiceErrorCode::NotConnected,
        QStringLiteral("cannot %1 while transport is disconnected")
            .arg(action));
    return false;
}

bool RoomServiceClient::requireSession(const QString& action)
{
    if (hasSession()
        && !m_participantId.isEmpty()
        && !m_reconnectToken.isEmpty()) {
        return true;
    }

    reportError(
        RoomServiceErrorCode::NoSession,
        QStringLiteral("cannot %1 without an established room session")
            .arg(action));
    return false;
}

bool RoomServiceClient::requireNoSession(const QString& action)
{
    if (!hasSession()
        && m_pendingOperation == PendingOperation::None) {
        return true;
    }

    reportError(
        RoomServiceErrorCode::InvalidRequest,
        QStringLiteral("cannot %1 while a room session/request is active")
            .arg(action));
    return false;
}

bool RoomServiceClient::requireMatchingRoom(
    const ProtocolMessage& message)
{
    if (message.roomId == m_roomId)
        return true;

    protocolFailure(
        QStringLiteral(
            "server message room ID does not match established session"));
    return false;
}

ProtocolMessage RoomServiceClient::makeClientMessage(
    MessageType type,
    const QJsonObject& payload,
    bool includeSession)
{
    ProtocolMessage message;
    message.version = kProtocolVersion;
    message.type = type;
    message.payload = payload;

    if (includeSession) {
        message.roomId = m_roomId;
        message.senderId = m_participantId;
    }

    message.sequence = nextSequence();
    return message;
}

qint64 RoomServiceClient::nextSequence()
{
    const qint64 sequence = m_nextSequence;
    if (m_nextSequence == std::numeric_limits<qint64>::max())
        m_nextSequence = 1;
    else
        ++m_nextSequence;
    return sequence;
}

void RoomServiceClient::protocolFailure(const QString& detail)
{
    // Clear ephemeral session state before the error callback. UiController uses
    // hasSession() to decide whether this is recoverable; notifying first leaves
    // stale participants/chat visible after a terminal protocol violation.
    m_pendingOperation = PendingOperation::None;
    m_requestedRoomId.clear();
    clearSession();

    reportError(RoomServiceErrorCode::ProtocolFailure, detail);
    if (m_transport)
        m_transport->close();
}

void RoomServiceClient::reportError(
    RoomServiceErrorCode code,
    const QString& detail)
{
    if (m_errorHandler)
        m_errorHandler(RoomServiceError{code, detail});
}

void RoomServiceClient::clearSession()
{
    m_roomId.clear();
    m_participantId.clear();
    m_reconnectToken.clear();
    m_snapshot = {};
    m_hasSnapshot = false;
}

QString roomServiceErrorCodeName(RoomServiceErrorCode code)
{
    switch (code) {
    case RoomServiceErrorCode::None:
        return QStringLiteral("none");
    case RoomServiceErrorCode::InvalidRequest:
        return QStringLiteral("invalidRequest");
    case RoomServiceErrorCode::NotConnected:
        return QStringLiteral("notConnected");
    case RoomServiceErrorCode::NoSession:
        return QStringLiteral("noSession");
    case RoomServiceErrorCode::ProtocolFailure:
        return QStringLiteral("protocolFailure");
    case RoomServiceErrorCode::ProtocolVersionMismatch:
        return QStringLiteral("protocolVersionMismatch");
    case RoomServiceErrorCode::TransportFailure:
        return QStringLiteral("transportFailure");
    case RoomServiceErrorCode::Unauthenticated:
        return QStringLiteral("unauthenticated");
    case RoomServiceErrorCode::RoomNotFound:
        return QStringLiteral("roomNotFound");
    case RoomServiceErrorCode::RoomFull:
        return QStringLiteral("roomFull");
    case RoomServiceErrorCode::RoomEnded:
        return QStringLiteral("roomEnded");
    case RoomServiceErrorCode::ParticipantRemoved:
        return QStringLiteral("participantRemoved");
    case RoomServiceErrorCode::NotAuthorized:
        return QStringLiteral("notAuthorized");
    case RoomServiceErrorCode::InvalidSource:
        return QStringLiteral("invalidSource");
    case RoomServiceErrorCode::InvalidMessage:
        return QStringLiteral("invalidMessage");
    case RoomServiceErrorCode::RateLimited:
        return QStringLiteral("rateLimited");
    case RoomServiceErrorCode::ServerRejected:
        return QStringLiteral("serverRejected");
    }
    return QStringLiteral("unknown");
}

} // namespace Colosseum::WatchParty
