#pragma once

#include "watchparty/WatchPartyTypes.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace Colosseum::WatchParty {

// Transport-level hard ceiling. The server contract uses the same value. This
// protects the desktop client before JSON parsing and is intentionally far above
// normal room-control/chat traffic without becoming an unbounded allocation.
constexpr qsizetype kMaxWireMessageBytes = 64 * 1024;

enum class MessageType {
    CreateRoom,
    JoinRoom,
    ReconnectRoom,
    LeaveRoom,
    SessionEstablished,
    RoomSnapshot,
    TimelineCommand,
    TimelineState,
    SetControlMode,
    ParticipantState,
    RemoveParticipant,
    HostChanged,
    Chat,
    Reaction,
    EndRoom,
    RoomEnded,
    Error
};

enum class MessageDirection {
    ClientToServer,
    ServerToClient
};

enum class DecodeError {
    None,
    MessageTooLarge,
    MalformedJson,
    InvalidEnvelope,
    UnsupportedVersion,
    UnknownMessageType
};

struct ProtocolMessage {
    int version = kProtocolVersion;
    MessageType type = MessageType::Error;
    QString roomId;
    QString senderId;
    qint64 sequence = 0;
    QJsonObject payload;
};

struct DecodeResult {
    bool ok = false;
    ProtocolMessage message;
    QString error;
    DecodeError errorCode = DecodeError::None;
};

struct ValidationResult {
    bool ok = false;
    QString error;
};

struct SessionEstablished {
    QString participantId;
    QString reconnectToken;
};

QString messageTypeName(MessageType type);
bool messageTypeFromName(const QString& name, MessageType* type);

QByteArray encodeMessage(const ProtocolMessage& message);
DecodeResult decodeMessage(const QByteArray& bytes);
ValidationResult validateMessage(const ProtocolMessage& message,
                                 MessageDirection direction);

QJsonObject sourceDescriptorToJson(const SourceDescriptor& source);
bool sourceDescriptorFromJson(const QJsonObject& object,
                              SourceDescriptor* source,
                              QString* error = nullptr);

QJsonObject participantIdentityToJson(const ParticipantIdentity& identity);
bool participantIdentityFromJson(const QJsonObject& object,
                                 ParticipantIdentity* identity,
                                 QString* error = nullptr);

QJsonObject participantStateToJson(const ParticipantState& participant);
bool participantStateFromJson(const QJsonObject& object,
                              ParticipantState* participant,
                              QString* error = nullptr);

QJsonObject timelineStateToJson(const TimelineState& timeline);
bool timelineStateFromJson(const QJsonObject& object,
                           TimelineState* timeline,
                           QString* error = nullptr);

QJsonObject timelineCommandToJson(const TimelineCommand& command);
bool timelineCommandFromJson(const QJsonObject& object,
                             TimelineCommand* command,
                             QString* error = nullptr);

QJsonObject chatEventToJson(const ChatEvent& event);
bool chatEventFromJson(const QJsonObject& object,
                       ChatEvent* event,
                       QString* error = nullptr);

QJsonObject reactionEventToJson(const ReactionEvent& event);
bool reactionEventFromJson(const QJsonObject& object,
                           ReactionEvent* event,
                           QString* error = nullptr);

QJsonObject roomSnapshotToJson(const RoomSnapshot& snapshot);
bool roomSnapshotFromJson(const QJsonObject& object,
                          RoomSnapshot* snapshot,
                          QString* error = nullptr);

QJsonObject sessionEstablishedToJson(const SessionEstablished& session);
bool sessionEstablishedFromJson(const QJsonObject& object,
                                SessionEstablished* session,
                                QString* error = nullptr);

} // namespace Colosseum::WatchParty
