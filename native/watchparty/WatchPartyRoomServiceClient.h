#pragma once

#include "watchparty/WatchPartyTransport.h"
#include "watchparty/WatchPartyTypes.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <functional>

namespace Colosseum::WatchParty {

enum class RoomServiceErrorCode {
    None,
    InvalidRequest,
    NotConnected,
    NoSession,
    ProtocolFailure,
    ProtocolVersionMismatch,
    TransportFailure,
    Unauthenticated,
    RoomNotFound,
    RoomFull,
    RoomEnded,
    ParticipantRemoved,
    NotAuthorized,
    InvalidSource,
    InvalidMessage,
    RateLimited,
    ServerRejected
};

struct RoomServiceError {
    RoomServiceErrorCode code = RoomServiceErrorCode::None;
    QString detail;
};

class RoomServiceClient
{
public:
    using SessionHandler =
        std::function<void(const QString& roomId,
                           const QString& participantId)>;
    using SnapshotHandler =
        std::function<void(const RoomSnapshot&)>;
    using TimelineHandler =
        std::function<void(const TimelineState&)>;
    using ParticipantHandler =
        std::function<void(const ParticipantState&)>;
    using HostChangedHandler =
        std::function<void(const QString& hostParticipantId)>;
    using ChatHandler =
        std::function<void(const ChatEvent&)>;
    using ReactionHandler =
        std::function<void(const ReactionEvent&)>;
    using RoomEndedHandler = std::function<void()>;
    using ErrorHandler = std::function<void(const RoomServiceError&)>;
    using TransportStateHandler = std::function<void(TransportState)>;

    explicit RoomServiceClient(ITransport* transport);
    ~RoomServiceClient();

    RoomServiceClient(const RoomServiceClient&) = delete;
    RoomServiceClient& operator=(const RoomServiceClient&) = delete;

    void setSessionHandler(SessionHandler handler);
    void setSnapshotHandler(SnapshotHandler handler);
    void setTimelineHandler(TimelineHandler handler);
    void setParticipantHandler(ParticipantHandler handler);
    void setHostChangedHandler(HostChangedHandler handler);
    void setChatHandler(ChatHandler handler);
    void setReactionHandler(ReactionHandler handler);
    void setRoomEndedHandler(RoomEndedHandler handler);
    void setErrorHandler(ErrorHandler handler);
    void setTransportStateHandler(TransportStateHandler handler);

    bool openService(const QUrl& serviceUrl,
                     const QByteArray& bearerToken = QByteArray());
    void closeService();
    bool retryTransportNow();

    TransportState transportState() const;
    bool signedInCredentialPresent() const
    {
        return m_signedInCredentialPresent;
    }

    bool hasSession() const { return !m_roomId.isEmpty(); }
    QString roomId() const { return m_roomId; }
    QString participantId() const { return m_participantId; }
    bool hasSnapshot() const { return m_hasSnapshot; }
    RoomSnapshot snapshot() const { return m_snapshot; }

    // The account subsystem supplies the bearer token to openService(). It does
    // not supply a username/id here; the service derives signed-in identity from
    // that authentication boundary.
    bool createRoom(const SourceDescriptor& source);
    bool joinSignedIn(const QString& roomId);

    // Guest identity is room-local. The service assigns the opaque participantId.
    bool joinGuest(const QString& roomId, const QString& displayName);

    bool leaveRoom();
    bool sendTimelineCommand(const TimelineCommand& command);
    bool setControlMode(ControlMode mode);
    bool publishParticipantState(bool ready, SyncStatus status);
    bool removeParticipant(const QString& participantId);
    bool sendChat(const QString& message);
    bool sendReaction(const QString& reaction);
    bool endRoom();

private:
    enum class PendingOperation {
        None,
        Create,
        Join,
        Reconnect
    };

    void onTransportState(TransportState state);
    void onTransportError(const TransportError& error);
    void onMessage(const ProtocolMessage& message);

    bool sendSessionMessage(MessageType type, const QJsonObject& payload);
    bool sendReconnect();

    bool applySessionEstablished(const ProtocolMessage& message);
    bool applyRoomSnapshot(const ProtocolMessage& message);
    bool applyTimelineState(const ProtocolMessage& message);
    bool applyParticipantState(const ProtocolMessage& message);
    bool applyHostChanged(const ProtocolMessage& message);
    bool applyChat(const ProtocolMessage& message);
    bool applyReaction(const ProtocolMessage& message);
    bool applyRoomEnded(const ProtocolMessage& message);
    bool applyServerError(const ProtocolMessage& message);

    bool requireConnected(const QString& action);
    bool requireSession(const QString& action);
    bool requireNoSession(const QString& action);
    bool requireMatchingRoom(const ProtocolMessage& message);

    ProtocolMessage makeClientMessage(MessageType type,
                                      const QJsonObject& payload,
                                      bool includeSession);
    qint64 nextSequence();

    void protocolFailure(const QString& detail);
    void reportError(RoomServiceErrorCode code, const QString& detail);
    void clearSession();

    ITransport* m_transport = nullptr;
    bool m_signedInCredentialPresent = false;
    PendingOperation m_pendingOperation = PendingOperation::None;

    QString m_requestedRoomId;
    QString m_roomId;
    QString m_participantId;
    QString m_reconnectToken;
    RoomSnapshot m_snapshot;
    bool m_hasSnapshot = false;
    qint64 m_nextSequence = 1;

    SessionHandler m_sessionHandler;
    SnapshotHandler m_snapshotHandler;
    TimelineHandler m_timelineHandler;
    ParticipantHandler m_participantHandler;
    HostChangedHandler m_hostChangedHandler;
    ChatHandler m_chatHandler;
    ReactionHandler m_reactionHandler;
    RoomEndedHandler m_roomEndedHandler;
    ErrorHandler m_errorHandler;
    TransportStateHandler m_transportStateHandler;
};

QString roomServiceErrorCodeName(RoomServiceErrorCode code);

} // namespace Colosseum::WatchParty
