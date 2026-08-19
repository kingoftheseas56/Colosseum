#include "watchparty/WatchPartyProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

#include <limits>

namespace Colosseum::WatchParty {
namespace {

bool fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return false;
}

bool exactKeys(const QJsonObject& object,
               const QSet<QString>& allowed,
               const QString& context,
               QString* error)
{
    for (const QString& key : object.keys()) {
        if (!allowed.contains(key)) {
            return fail(
                error,
                QStringLiteral("unknown %1 key '%2'").arg(context, key));
        }
    }

    for (const QString& required : allowed) {
        if (!object.contains(required)) {
            return fail(
                error,
                QStringLiteral("%1 is missing required key '%2'")
                    .arg(context, required));
        }
    }
    return true;
}

bool exactKeysWithOptional(const QJsonObject& object,
                           const QSet<QString>& required,
                           const QSet<QString>& optional,
                           const QString& context,
                           QString* error)
{
    const QSet<QString> allowed = required | optional;
    for (const QString& key : object.keys()) {
        if (!allowed.contains(key)) {
            return fail(
                error,
                QStringLiteral("unknown %1 key '%2'").arg(context, key));
        }
    }

    for (const QString& key : required) {
        if (!object.contains(key)) {
            return fail(
                error,
                QStringLiteral("%1 is missing required key '%2'")
                    .arg(context, key));
        }
    }
    return true;
}

bool requireEmptyPayload(const QJsonObject& object,
                         const QString& context,
                         QString* error)
{
    if (!object.isEmpty())
        return fail(error, context + QStringLiteral(" payload must be empty"));
    return true;
}

bool readString(const QJsonObject& object,
                const QString& key,
                QString* value,
                QString* error,
                bool allowEmpty = false)
{
    const QJsonValue raw = object.value(key);
    if (!raw.isString())
        return fail(error, key + QStringLiteral(" must be a string"));

    const QString parsed = raw.toString();
    if (!allowEmpty && parsed.trimmed().isEmpty())
        return fail(error, key + QStringLiteral(" must not be empty"));

    *value = parsed;
    return true;
}

bool readBool(const QJsonObject& object,
              const QString& key,
              bool* value,
              QString* error)
{
    const QJsonValue raw = object.value(key);
    if (!raw.isBool())
        return fail(error, key + QStringLiteral(" must be a boolean"));

    *value = raw.toBool();
    return true;
}

bool readNonNegativeInteger(const QJsonObject& object,
                            const QString& key,
                            qint64* value,
                            QString* error)
{
    const QJsonValue raw = object.value(key);
    if (!raw.isDouble())
        return fail(error, key + QStringLiteral(" must be an integer"));

    const qint64 parsed = raw.toInteger(-1);
    if (parsed < 0 || static_cast<double>(parsed) != raw.toDouble())
        return fail(error, key + QStringLiteral(" must be a non-negative integer"));

    *value = parsed;
    return true;
}

bool readSequence(const QJsonObject& object, quint64* value, QString* error)
{
    qint64 parsed = 0;
    if (!readNonNegativeInteger(
            object, QStringLiteral("sequence"), &parsed, error)) {
        return false;
    }

    *value = static_cast<quint64>(parsed);
    return true;
}

bool readParticipantIdentityFields(const QJsonObject& object,
                                   ParticipantIdentity* identity,
                                   QString* error)
{
    ParticipantIdentity parsed;
    if (!readString(object,
                    QStringLiteral("participantId"),
                    &parsed.participantId,
                    error)) {
        return false;
    }
    if (!readString(object,
                    QStringLiteral("displayName"),
                    &parsed.displayName,
                    error)) {
        return false;
    }

    QString kindName;
    if (!readString(object, QStringLiteral("identityKind"), &kindName, error))
        return false;
    if (!identityKindFromName(kindName, &parsed.kind)) {
        return fail(
            error,
            QStringLiteral("unknown identityKind '%1'").arg(kindName));
    }

    parsed.participantId = parsed.participantId.trimmed();
    parsed.displayName = parsed.displayName.trimmed();
    if (!parsed.isValid())
        return fail(error, QStringLiteral("invalid participant identity"));

    *identity = parsed;
    return true;
}

bool requireRoom(const ProtocolMessage& message, QString* error)
{
    if (message.roomId.trimmed().isEmpty())
        return fail(error, QStringLiteral("roomId must not be empty"));
    return true;
}

bool requireSender(const ProtocolMessage& message, QString* error)
{
    if (message.senderId.trimmed().isEmpty())
        return fail(error, QStringLiteral("senderId must not be empty"));
    return true;
}

bool requireNoRoom(const ProtocolMessage& message, QString* error)
{
    if (!message.roomId.isEmpty())
        return fail(error, QStringLiteral("roomId must be empty"));
    return true;
}

bool requireNoSender(const ProtocolMessage& message, QString* error)
{
    if (!message.senderId.isEmpty())
        return fail(error, QStringLiteral("senderId must be empty"));
    return true;
}

bool validateClientMessage(const ProtocolMessage& message, QString* error)
{
    switch (message.type) {
    case MessageType::CreateRoom: {
        if (!requireNoRoom(message, error) || !requireNoSender(message, error))
            return false;
        if (!exactKeys(
                message.payload,
                {QStringLiteral("source")},
                QStringLiteral("createRoom payload"),
                error)) {
            return false;
        }

        const QJsonValue source = message.payload.value(QStringLiteral("source"));
        if (!source.isObject())
            return fail(error, QStringLiteral("source must be an object"));

        SourceDescriptor descriptor;
        return sourceDescriptorFromJson(source.toObject(), &descriptor, error);
    }

    case MessageType::JoinRoom: {
        if (!requireRoom(message, error) || !requireNoSender(message, error))
            return false;

        QString kindName;
        if (!readString(
                message.payload,
                QStringLiteral("identityKind"),
                &kindName,
                error)) {
            return false;
        }

        IdentityKind kind;
        if (!identityKindFromName(kindName, &kind)) {
            return fail(
                error,
                QStringLiteral("unknown identityKind '%1'").arg(kindName));
        }

        if (kind == IdentityKind::SignedIn) {
            return exactKeys(
                message.payload,
                {QStringLiteral("identityKind")},
                QStringLiteral("signed-in join payload"),
                error);
        }

        if (!exactKeys(
                message.payload,
                {QStringLiteral("identityKind"), QStringLiteral("displayName")},
                QStringLiteral("guest join payload"),
                error)) {
            return false;
        }

        QString displayName;
        return readString(
            message.payload,
            QStringLiteral("displayName"),
            &displayName,
            error);
    }

    case MessageType::ReconnectRoom: {
        if (!requireRoom(message, error) || !requireNoSender(message, error))
            return false;
        if (!exactKeys(
                message.payload,
                {QStringLiteral("reconnectToken")},
                QStringLiteral("reconnectRoom payload"),
                error)) {
            return false;
        }

        QString reconnectToken;
        return readString(
            message.payload,
            QStringLiteral("reconnectToken"),
            &reconnectToken,
            error);
    }

    case MessageType::LeaveRoom:
        return requireRoom(message, error)
            && requireSender(message, error)
            && requireEmptyPayload(
                message.payload, QStringLiteral("leaveRoom"), error);

    case MessageType::TimelineCommand: {
        if (!requireRoom(message, error) || !requireSender(message, error))
            return false;
        TimelineCommand command;
        return timelineCommandFromJson(message.payload, &command, error);
    }

    case MessageType::SetControlMode: {
        if (!requireRoom(message, error) || !requireSender(message, error))
            return false;
        if (!exactKeys(
                message.payload,
                {QStringLiteral("controlMode")},
                QStringLiteral("setControlMode payload"),
                error)) {
            return false;
        }

        QString modeName;
        if (!readString(
                message.payload,
                QStringLiteral("controlMode"),
                &modeName,
                error)) {
            return false;
        }

        ControlMode mode;
        if (!controlModeFromName(modeName, &mode)) {
            return fail(
                error,
                QStringLiteral("unknown controlMode '%1'").arg(modeName));
        }
        return true;
    }

    case MessageType::ParticipantState: {
        if (!requireRoom(message, error) || !requireSender(message, error))
            return false;
        if (!exactKeys(
                message.payload,
                {QStringLiteral("ready"), QStringLiteral("syncStatus")},
                QStringLiteral("participantState request payload"),
                error)) {
            return false;
        }

        bool ready = false;
        if (!readBool(message.payload, QStringLiteral("ready"), &ready, error))
            return false;

        QString statusName;
        if (!readString(
                message.payload,
                QStringLiteral("syncStatus"),
                &statusName,
                error)) {
            return false;
        }

        SyncStatus status;
        if (!syncStatusFromName(statusName, &status)) {
            return fail(
                error,
                QStringLiteral("unknown syncStatus '%1'").arg(statusName));
        }

        if (!ready && status != SyncStatus::Unknown) {
            return fail(
                error,
                QStringLiteral(
                    "a non-ready participant must have unknown syncStatus"));
        }
        return true;
    }

    case MessageType::RemoveParticipant: {
        if (!requireRoom(message, error) || !requireSender(message, error))
            return false;
        if (!exactKeys(
                message.payload,
                {QStringLiteral("participantId")},
                QStringLiteral("removeParticipant payload"),
                error)) {
            return false;
        }

        QString participantId;
        return readString(
            message.payload,
            QStringLiteral("participantId"),
            &participantId,
            error);
    }

    case MessageType::Chat: {
        if (!requireRoom(message, error) || !requireSender(message, error))
            return false;
        if (!exactKeys(
                message.payload,
                {QStringLiteral("message")},
                QStringLiteral("chat request payload"),
                error)) {
            return false;
        }

        QString text;
        return readString(
            message.payload, QStringLiteral("message"), &text, error);
    }

    case MessageType::Reaction: {
        if (!requireRoom(message, error) || !requireSender(message, error))
            return false;
        if (!exactKeys(
                message.payload,
                {QStringLiteral("reaction")},
                QStringLiteral("reaction request payload"),
                error)) {
            return false;
        }

        QString reaction;
        return readString(
            message.payload,
            QStringLiteral("reaction"),
            &reaction,
            error);
    }

    case MessageType::EndRoom:
        return requireRoom(message, error)
            && requireSender(message, error)
            && requireEmptyPayload(
                message.payload, QStringLiteral("endRoom"), error);

    case MessageType::SessionEstablished:
    case MessageType::RoomSnapshot:
    case MessageType::TimelineState:
    case MessageType::HostChanged:
    case MessageType::RoomEnded:
    case MessageType::Error:
        return fail(
            error,
            QStringLiteral("message type '%1' is server-to-client only")
                .arg(messageTypeName(message.type)));
    }

    return fail(error, QStringLiteral("unhandled client message type"));
}

bool validateServerMessage(const ProtocolMessage& message, QString* error)
{
    switch (message.type) {
    case MessageType::SessionEstablished: {
        if (!requireRoom(message, error) || !requireNoSender(message, error))
            return false;
        SessionEstablished session;
        return sessionEstablishedFromJson(message.payload, &session, error);
    }

    case MessageType::RoomSnapshot: {
        if (!requireRoom(message, error) || !requireNoSender(message, error))
            return false;

        RoomSnapshot snapshot;
        if (!roomSnapshotFromJson(message.payload, &snapshot, error))
            return false;
        if (snapshot.roomId != message.roomId.trimmed()) {
            return fail(
                error,
                QStringLiteral(
                    "roomSnapshot roomId does not match envelope roomId"));
        }
        return true;
    }

    case MessageType::TimelineState: {
        if (!requireRoom(message, error) || !requireNoSender(message, error))
            return false;
        TimelineState timeline;
        return timelineStateFromJson(message.payload, &timeline, error);
    }

    case MessageType::ParticipantState: {
        if (!requireRoom(message, error) || !requireNoSender(message, error))
            return false;
        ParticipantState participant;
        return participantStateFromJson(message.payload, &participant, error);
    }

    case MessageType::HostChanged: {
        if (!requireRoom(message, error) || !requireNoSender(message, error))
            return false;
        if (!exactKeys(
                message.payload,
                {QStringLiteral("hostParticipantId")},
                QStringLiteral("hostChanged payload"),
                error)) {
            return false;
        }

        QString hostParticipantId;
        return readString(
            message.payload,
            QStringLiteral("hostParticipantId"),
            &hostParticipantId,
            error);
    }

    case MessageType::Chat: {
        if (!requireRoom(message, error) || !requireNoSender(message, error))
            return false;
        ChatEvent event;
        return chatEventFromJson(message.payload, &event, error);
    }

    case MessageType::Reaction: {
        if (!requireRoom(message, error) || !requireNoSender(message, error))
            return false;
        ReactionEvent event;
        return reactionEventFromJson(message.payload, &event, error);
    }

    case MessageType::RoomEnded:
        return requireRoom(message, error)
            && requireNoSender(message, error)
            && requireEmptyPayload(
                message.payload, QStringLiteral("roomEnded"), error);

    case MessageType::Error: {
        if (!requireNoSender(message, error))
            return false;
        if (!exactKeys(
                message.payload,
                {QStringLiteral("code"), QStringLiteral("message")},
                QStringLiteral("error payload"),
                error)) {
            return false;
        }

        QString code;
        QString text;
        return readString(
                   message.payload, QStringLiteral("code"), &code, error)
            && readString(
                   message.payload,
                   QStringLiteral("message"),
                   &text,
                   error,
                   true);
    }

    case MessageType::CreateRoom:
    case MessageType::JoinRoom:
    case MessageType::ReconnectRoom:
    case MessageType::LeaveRoom:
    case MessageType::TimelineCommand:
    case MessageType::SetControlMode:
    case MessageType::RemoveParticipant:
    case MessageType::EndRoom:
        return fail(
            error,
            QStringLiteral("message type '%1' is client-to-server only")
                .arg(messageTypeName(message.type)));
    }

    return fail(error, QStringLiteral("unhandled server message type"));
}

} // namespace

QString messageTypeName(MessageType type)
{
    switch (type) {
    case MessageType::CreateRoom:
        return QStringLiteral("createRoom");
    case MessageType::JoinRoom:
        return QStringLiteral("joinRoom");
    case MessageType::ReconnectRoom:
        return QStringLiteral("reconnectRoom");
    case MessageType::LeaveRoom:
        return QStringLiteral("leaveRoom");
    case MessageType::SessionEstablished:
        return QStringLiteral("sessionEstablished");
    case MessageType::RoomSnapshot:
        return QStringLiteral("roomSnapshot");
    case MessageType::TimelineCommand:
        return QStringLiteral("timelineCommand");
    case MessageType::TimelineState:
        return QStringLiteral("timelineState");
    case MessageType::SetControlMode:
        return QStringLiteral("setControlMode");
    case MessageType::ParticipantState:
        return QStringLiteral("participantState");
    case MessageType::RemoveParticipant:
        return QStringLiteral("removeParticipant");
    case MessageType::HostChanged:
        return QStringLiteral("hostChanged");
    case MessageType::Chat:
        return QStringLiteral("chat");
    case MessageType::Reaction:
        return QStringLiteral("reaction");
    case MessageType::EndRoom:
        return QStringLiteral("endRoom");
    case MessageType::RoomEnded:
        return QStringLiteral("roomEnded");
    case MessageType::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

bool messageTypeFromName(const QString& name, MessageType* type)
{
    if (!type)
        return false;

    struct Entry {
        const char* name;
        MessageType type;
    };

    static constexpr Entry entries[] = {
        {"createRoom", MessageType::CreateRoom},
        {"joinRoom", MessageType::JoinRoom},
        {"reconnectRoom", MessageType::ReconnectRoom},
        {"leaveRoom", MessageType::LeaveRoom},
        {"sessionEstablished", MessageType::SessionEstablished},
        {"roomSnapshot", MessageType::RoomSnapshot},
        {"timelineCommand", MessageType::TimelineCommand},
        {"timelineState", MessageType::TimelineState},
        {"setControlMode", MessageType::SetControlMode},
        {"participantState", MessageType::ParticipantState},
        {"removeParticipant", MessageType::RemoveParticipant},
        {"hostChanged", MessageType::HostChanged},
        {"chat", MessageType::Chat},
        {"reaction", MessageType::Reaction},
        {"endRoom", MessageType::EndRoom},
        {"roomEnded", MessageType::RoomEnded},
        {"error", MessageType::Error},
    };

    for (const Entry& entry : entries) {
        if (name == QLatin1String(entry.name)) {
            *type = entry.type;
            return true;
        }
    }
    return false;
}

QByteArray encodeMessage(const ProtocolMessage& message)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), message.version);
    object.insert(QStringLiteral("type"), messageTypeName(message.type));
    object.insert(QStringLiteral("roomId"), message.roomId);
    object.insert(QStringLiteral("senderId"), message.senderId);
    object.insert(QStringLiteral("sequence"), message.sequence);
    object.insert(QStringLiteral("payload"), message.payload);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

DecodeResult decodeMessage(const QByteArray& bytes)
{
    if (bytes.size() > kMaxWireMessageBytes) {
        return DecodeResult{
            false,
            {},
            QStringLiteral("message exceeds %1-byte wire limit")
                .arg(kMaxWireMessageBytes),
            DecodeError::MessageTooLarge
        };
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return DecodeResult{
            false,
            {},
            QStringLiteral("invalid JSON object: %1").arg(parseError.errorString()),
            DecodeError::MalformedJson
        };
    }

    const QJsonObject object = document.object();
    QString envelopeError;
    if (!exactKeys(
            object,
            {
                QStringLiteral("version"),
                QStringLiteral("type"),
                QStringLiteral("roomId"),
                QStringLiteral("senderId"),
                QStringLiteral("sequence"),
                QStringLiteral("payload")
            },
            QStringLiteral("protocol envelope"),
            &envelopeError)) {
        return DecodeResult{
            false, {}, envelopeError, DecodeError::InvalidEnvelope
        };
    }

    const QJsonValue versionValue = object.value(QStringLiteral("version"));
    if (!versionValue.isDouble()) {
        return DecodeResult{
            false,
            {},
            QStringLiteral("version must be an integer"),
            DecodeError::InvalidEnvelope
        };
    }

    const qint64 version = versionValue.toInteger(-1);
    if (static_cast<double>(version) != versionValue.toDouble()) {
        return DecodeResult{
            false,
            {},
            QStringLiteral("version must be an integer"),
            DecodeError::InvalidEnvelope
        };
    }

    if (version != kProtocolVersion) {
        return DecodeResult{
            false,
            {},
            QStringLiteral("unsupported protocol version %1; expected %2")
                .arg(version)
                .arg(kProtocolVersion),
            DecodeError::UnsupportedVersion
        };
    }

    const QJsonValue typeValue = object.value(QStringLiteral("type"));
    if (!typeValue.isString()) {
        return DecodeResult{
            false,
            {},
            QStringLiteral("type must be a string"),
            DecodeError::InvalidEnvelope
        };
    }

    MessageType type;
    if (!messageTypeFromName(typeValue.toString(), &type)) {
        return DecodeResult{
            false,
            {},
            QStringLiteral("unknown message type '%1'").arg(typeValue.toString()),
            DecodeError::UnknownMessageType
        };
    }

    QString roomId;
    QString fieldError;
    if (!readString(
            object, QStringLiteral("roomId"), &roomId, &fieldError, true)) {
        return DecodeResult{
            false, {}, fieldError, DecodeError::InvalidEnvelope
        };
    }

    QString senderId;
    if (!readString(
            object, QStringLiteral("senderId"), &senderId, &fieldError, true)) {
        return DecodeResult{
            false, {}, fieldError, DecodeError::InvalidEnvelope
        };
    }

    qint64 sequence = 0;
    if (!readNonNegativeInteger(
            object, QStringLiteral("sequence"), &sequence, &fieldError)) {
        return DecodeResult{
            false, {}, fieldError, DecodeError::InvalidEnvelope
        };
    }

    const QJsonValue payloadValue = object.value(QStringLiteral("payload"));
    if (!payloadValue.isObject()) {
        return DecodeResult{
            false,
            {},
            QStringLiteral("payload must be an object"),
            DecodeError::InvalidEnvelope
        };
    }

    ProtocolMessage message;
    message.version = static_cast<int>(version);
    message.type = type;
    message.roomId = roomId.trimmed();
    message.senderId = senderId.trimmed();
    message.sequence = sequence;
    message.payload = payloadValue.toObject();
    return DecodeResult{true, message, QString(), DecodeError::None};
}

ValidationResult validateMessage(const ProtocolMessage& message,
                                 MessageDirection direction)
{
    if (message.version != kProtocolVersion) {
        return ValidationResult{
            false,
            QStringLiteral("message protocol version %1 does not match %2")
                .arg(message.version)
                .arg(kProtocolVersion)
        };
    }

    if (message.sequence < 0)
        return ValidationResult{false, QStringLiteral("sequence must be non-negative")};

    QString error;
    const bool ok = direction == MessageDirection::ClientToServer
        ? validateClientMessage(message, &error)
        : validateServerMessage(message, &error);
    return ValidationResult{ok, error};
}

QJsonObject sourceDescriptorToJson(const SourceDescriptor& source)
{
    const SourceDescriptor descriptor = source.normalized();
    if (!descriptor.isValid())
        return {};

    switch (descriptor.kind) {
    case SourceKind::Torrent:
        return QJsonObject{
            {QStringLiteral("kind"), sourceKindName(descriptor.kind)},
            {QStringLiteral("infoHash"), descriptor.infoHash},
            {QStringLiteral("fileIdx"), descriptor.fileIdx}
        };
    case SourceKind::Debrid:
        return QJsonObject{
            {QStringLiteral("kind"), sourceKindName(descriptor.kind)},
            {QStringLiteral("providerId"), descriptor.providerId},
            {QStringLiteral("providerSourceId"), descriptor.providerSourceId}
        };
    case SourceKind::Unknown:
        break;
    }
    return {};
}

bool sourceDescriptorFromJson(const QJsonObject& object,
                              SourceDescriptor* source,
                              QString* error)
{
    if (!source)
        return fail(error, QStringLiteral("source output is null"));

    QString kindName;
    if (!readString(object, QStringLiteral("kind"), &kindName, error))
        return false;

    SourceKind kind = SourceKind::Unknown;
    if (!sourceKindFromName(kindName, &kind)) {
        return fail(
            error,
            QStringLiteral("unknown source kind '%1'").arg(kindName));
    }

    SourceDescriptor parsed;
    if (kind == SourceKind::Torrent) {
        if (!exactKeys(
                object,
                {
                    QStringLiteral("kind"),
                    QStringLiteral("infoHash"),
                    QStringLiteral("fileIdx")
                },
                QStringLiteral("torrent source"),
                error)) {
            return false;
        }

        QString infoHash;
        qint64 fileIdx = 0;
        if (!readString(
                object, QStringLiteral("infoHash"), &infoHash, error)
            || !readNonNegativeInteger(
                object, QStringLiteral("fileIdx"), &fileIdx, error)) {
            return false;
        }

        if (fileIdx > std::numeric_limits<int>::max())
            return fail(error, QStringLiteral("fileIdx is out of range"));

        parsed = SourceDescriptor::torrent(
            infoHash, static_cast<int>(fileIdx));
    } else {
        if (!exactKeys(
                object,
                {
                    QStringLiteral("kind"),
                    QStringLiteral("providerId"),
                    QStringLiteral("providerSourceId")
                },
                QStringLiteral("debrid source"),
                error)) {
            return false;
        }

        QString providerId;
        QString providerSourceId;
        if (!readString(
                object, QStringLiteral("providerId"), &providerId, error)
            || !readString(
                object,
                QStringLiteral("providerSourceId"),
                &providerSourceId,
                error)) {
            return false;
        }

        parsed = SourceDescriptor::debrid(providerId, providerSourceId);
    }

    if (!parsed.isValid())
        return fail(error, QStringLiteral("invalid source descriptor"));

    *source = parsed;
    return true;
}

QJsonObject participantIdentityToJson(const ParticipantIdentity& identity)
{
    return QJsonObject{
        {QStringLiteral("participantId"), identity.participantId},
        {QStringLiteral("displayName"), identity.displayName},
        {QStringLiteral("identityKind"), identityKindName(identity.kind)}
    };
}

bool participantIdentityFromJson(const QJsonObject& object,
                                 ParticipantIdentity* identity,
                                 QString* error)
{
    if (!identity)
        return fail(error, QStringLiteral("identity output is null"));

    if (!exactKeys(
            object,
            {
                QStringLiteral("participantId"),
                QStringLiteral("displayName"),
                QStringLiteral("identityKind")
            },
            QStringLiteral("participant identity"),
            error)) {
        return false;
    }

    return readParticipantIdentityFields(object, identity, error);
}

QJsonObject participantStateToJson(const ParticipantState& participant)
{
    QJsonObject object = participantIdentityToJson(participant.identity);
    object.insert(
        QStringLiteral("joinOrder"),
        static_cast<qint64>(participant.joinOrder));
    object.insert(QStringLiteral("host"), participant.host);
    object.insert(QStringLiteral("connected"), participant.connected);
    object.insert(QStringLiteral("ready"), participant.ready);
    object.insert(
        QStringLiteral("syncStatus"),
        syncStatusName(participant.syncStatus));
    return object;
}

bool participantStateFromJson(const QJsonObject& object,
                              ParticipantState* participant,
                              QString* error)
{
    if (!participant)
        return fail(error, QStringLiteral("participant output is null"));

    if (!exactKeys(
            object,
            {
                QStringLiteral("participantId"),
                QStringLiteral("displayName"),
                QStringLiteral("identityKind"),
                QStringLiteral("joinOrder"),
                QStringLiteral("host"),
                QStringLiteral("connected"),
                QStringLiteral("ready"),
                QStringLiteral("syncStatus")
            },
            QStringLiteral("participant state"),
            error)) {
        return false;
    }

    ParticipantState parsed;
    if (!readParticipantIdentityFields(object, &parsed.identity, error))
        return false;

    qint64 joinOrder = 0;
    if (!readNonNegativeInteger(
            object, QStringLiteral("joinOrder"), &joinOrder, error)) {
        return false;
    }
    parsed.joinOrder = static_cast<quint64>(joinOrder);

    if (!readBool(object, QStringLiteral("host"), &parsed.host, error)
        || !readBool(
            object, QStringLiteral("connected"), &parsed.connected, error)
        || !readBool(object, QStringLiteral("ready"), &parsed.ready, error)) {
        return false;
    }

    QString statusName;
    if (!readString(
            object, QStringLiteral("syncStatus"), &statusName, error)) {
        return false;
    }
    if (!syncStatusFromName(statusName, &parsed.syncStatus)) {
        return fail(
            error,
            QStringLiteral("unknown syncStatus '%1'").arg(statusName));
    }

    if (!parsed.ready && parsed.syncStatus != SyncStatus::Unknown) {
        return fail(
            error,
            QStringLiteral(
                "a non-ready participant must have unknown syncStatus"));
    }

    *participant = parsed;
    return true;
}

QJsonObject timelineStateToJson(const TimelineState& timeline)
{
    QJsonObject object;
    object.insert(QStringLiteral("playing"), timeline.playing);
    object.insert(QStringLiteral("positionMs"), timeline.positionMs);
    object.insert(
        QStringLiteral("revision"),
        static_cast<qint64>(timeline.revision));
    return object;
}

bool timelineStateFromJson(const QJsonObject& object,
                           TimelineState* timeline,
                           QString* error)
{
    if (!timeline)
        return fail(error, QStringLiteral("timeline output is null"));

    if (!exactKeys(
            object,
            {
                QStringLiteral("playing"),
                QStringLiteral("positionMs"),
                QStringLiteral("revision")
            },
            QStringLiteral("timeline state"),
            error)) {
        return false;
    }

    bool playing = false;
    if (!readBool(object, QStringLiteral("playing"), &playing, error))
        return false;

    qint64 positionMs = 0;
    if (!readNonNegativeInteger(
            object, QStringLiteral("positionMs"), &positionMs, error)) {
        return false;
    }

    qint64 revision = 0;
    if (!readNonNegativeInteger(
            object, QStringLiteral("revision"), &revision, error)) {
        return false;
    }

    timeline->playing = playing;
    timeline->positionMs = positionMs;
    timeline->revision = static_cast<quint64>(revision);
    return true;
}

QJsonObject timelineCommandToJson(const TimelineCommand& command)
{
    QJsonObject object;
    object.insert(
        QStringLiteral("command"),
        timelineCommandTypeName(command.type));
    if (command.hasPosition)
        object.insert(QStringLiteral("positionMs"), command.positionMs);
    return object;
}

bool timelineCommandFromJson(const QJsonObject& object,
                             TimelineCommand* command,
                             QString* error)
{
    if (!command)
        return fail(error, QStringLiteral("command output is null"));

    if (!exactKeysWithOptional(
            object,
            {QStringLiteral("command")},
            {QStringLiteral("positionMs")},
            QStringLiteral("timeline command"),
            error)) {
        return false;
    }

    QString typeName;
    if (!readString(object, QStringLiteral("command"), &typeName, error))
        return false;

    TimelineCommand parsed;
    if (!timelineCommandTypeFromName(typeName, &parsed.type)) {
        return fail(
            error,
            QStringLiteral("unknown timeline command '%1'").arg(typeName));
    }

    const QJsonValue positionValue =
        object.value(QStringLiteral("positionMs"));
    if (!positionValue.isUndefined()) {
        qint64 positionMs = 0;
        if (!readNonNegativeInteger(
                object,
                QStringLiteral("positionMs"),
                &positionMs,
                error)) {
            return false;
        }
        parsed.hasPosition = true;
        parsed.positionMs = positionMs;
    }

    if (parsed.type == TimelineCommandType::Seek && !parsed.hasPosition)
        return fail(error, QStringLiteral("seek requires positionMs"));

    *command = parsed;
    return true;
}

QJsonObject chatEventToJson(const ChatEvent& event)
{
    QJsonObject object;
    object.insert(
        QStringLiteral("sequence"), static_cast<qint64>(event.sequence));
    object.insert(QStringLiteral("participantId"), event.participantId);
    object.insert(QStringLiteral("displayName"), event.displayName);
    object.insert(QStringLiteral("message"), event.message);
    return object;
}

bool chatEventFromJson(const QJsonObject& object,
                       ChatEvent* event,
                       QString* error)
{
    if (!event)
        return fail(error, QStringLiteral("chat event output is null"));

    if (!exactKeys(
            object,
            {
                QStringLiteral("sequence"),
                QStringLiteral("participantId"),
                QStringLiteral("displayName"),
                QStringLiteral("message")
            },
            QStringLiteral("chat event"),
            error)) {
        return false;
    }

    ChatEvent parsed;
    if (!readSequence(object, &parsed.sequence, error))
        return false;
    if (!readString(
            object,
            QStringLiteral("participantId"),
            &parsed.participantId,
            error)) {
        return false;
    }
    if (!readString(
            object,
            QStringLiteral("displayName"),
            &parsed.displayName,
            error)) {
        return false;
    }
    if (!readString(
            object, QStringLiteral("message"), &parsed.message, error)) {
        return false;
    }

    parsed.participantId = parsed.participantId.trimmed();
    parsed.displayName = parsed.displayName.trimmed();
    parsed.message = parsed.message.trimmed();
    *event = parsed;
    return true;
}

QJsonObject reactionEventToJson(const ReactionEvent& event)
{
    QJsonObject object;
    object.insert(
        QStringLiteral("sequence"), static_cast<qint64>(event.sequence));
    object.insert(QStringLiteral("participantId"), event.participantId);
    object.insert(QStringLiteral("displayName"), event.displayName);
    object.insert(QStringLiteral("reaction"), event.reaction);
    return object;
}

bool reactionEventFromJson(const QJsonObject& object,
                           ReactionEvent* event,
                           QString* error)
{
    if (!event)
        return fail(error, QStringLiteral("reaction event output is null"));

    if (!exactKeys(
            object,
            {
                QStringLiteral("sequence"),
                QStringLiteral("participantId"),
                QStringLiteral("displayName"),
                QStringLiteral("reaction")
            },
            QStringLiteral("reaction event"),
            error)) {
        return false;
    }

    ReactionEvent parsed;
    if (!readSequence(object, &parsed.sequence, error))
        return false;
    if (!readString(
            object,
            QStringLiteral("participantId"),
            &parsed.participantId,
            error)) {
        return false;
    }
    if (!readString(
            object,
            QStringLiteral("displayName"),
            &parsed.displayName,
            error)) {
        return false;
    }
    if (!readString(
            object,
            QStringLiteral("reaction"),
            &parsed.reaction,
            error)) {
        return false;
    }

    parsed.participantId = parsed.participantId.trimmed();
    parsed.displayName = parsed.displayName.trimmed();
    parsed.reaction = parsed.reaction.trimmed();
    *event = parsed;
    return true;
}

QJsonObject roomSnapshotToJson(const RoomSnapshot& snapshot)
{
    QJsonArray participants;
    for (const ParticipantState& participant : snapshot.participants)
        participants.append(participantStateToJson(participant));

    QJsonObject object;
    object.insert(QStringLiteral("roomId"), snapshot.roomId);
    object.insert(
        QStringLiteral("hostParticipantId"),
        snapshot.hostParticipantId);
    object.insert(
        QStringLiteral("source"),
        sourceDescriptorToJson(snapshot.source));
    object.insert(
        QStringLiteral("controlMode"),
        controlModeName(snapshot.controlMode));
    object.insert(
        QStringLiteral("timeline"),
        timelineStateToJson(snapshot.timeline));
    object.insert(QStringLiteral("participants"), participants);
    object.insert(
        QStringLiteral("hostReconnectDeadlineMs"),
        snapshot.hostReconnectDeadlineMs);
    return object;
}

bool roomSnapshotFromJson(const QJsonObject& object,
                          RoomSnapshot* snapshot,
                          QString* error)
{
    if (!snapshot)
        return fail(error, QStringLiteral("snapshot output is null"));

    if (!exactKeys(
            object,
            {
                QStringLiteral("roomId"),
                QStringLiteral("hostParticipantId"),
                QStringLiteral("source"),
                QStringLiteral("controlMode"),
                QStringLiteral("timeline"),
                QStringLiteral("participants"),
                QStringLiteral("hostReconnectDeadlineMs")
            },
            QStringLiteral("room snapshot"),
            error)) {
        return false;
    }

    RoomSnapshot parsed;
    if (!readString(
            object, QStringLiteral("roomId"), &parsed.roomId, error)) {
        return false;
    }
    if (!readString(
            object,
            QStringLiteral("hostParticipantId"),
            &parsed.hostParticipantId,
            error)) {
        return false;
    }

    const QJsonValue source = object.value(QStringLiteral("source"));
    if (!source.isObject())
        return fail(error, QStringLiteral("source must be an object"));
    if (!sourceDescriptorFromJson(source.toObject(), &parsed.source, error))
        return false;

    QString modeName;
    if (!readString(
            object, QStringLiteral("controlMode"), &modeName, error)) {
        return false;
    }
    if (!controlModeFromName(modeName, &parsed.controlMode)) {
        return fail(
            error,
            QStringLiteral("unknown controlMode '%1'").arg(modeName));
    }

    const QJsonValue timeline = object.value(QStringLiteral("timeline"));
    if (!timeline.isObject())
        return fail(error, QStringLiteral("timeline must be an object"));
    if (!timelineStateFromJson(timeline.toObject(), &parsed.timeline, error))
        return false;

    const QJsonValue participants =
        object.value(QStringLiteral("participants"));
    if (!participants.isArray())
        return fail(error, QStringLiteral("participants must be an array"));

    const QJsonArray rows = participants.toArray();
    if (rows.isEmpty() || rows.size() > kMaxParticipants) {
        return fail(
            error,
            QStringLiteral("participants must contain 1..%1 rows")
                .arg(kMaxParticipants));
    }

    int hostCount = 0;
    QSet<QString> participantIds;
    QSet<quint64> joinOrders;
    for (const QJsonValue& row : rows) {
        if (!row.isObject()) {
            return fail(
                error,
                QStringLiteral("participant row must be an object"));
        }

        ParticipantState participant;
        if (!participantStateFromJson(row.toObject(), &participant, error))
            return false;

        if (participantIds.contains(
                participant.identity.participantId)) {
            return fail(
                error,
                QStringLiteral("participantId values must be unique"));
        }
        participantIds.insert(participant.identity.participantId);

        if (joinOrders.contains(participant.joinOrder)) {
            return fail(
                error,
                QStringLiteral("joinOrder values must be unique"));
        }
        joinOrders.insert(participant.joinOrder);

        if (participant.host) {
            ++hostCount;
            if (participant.identity.participantId
                != parsed.hostParticipantId) {
                return fail(
                    error,
                    QStringLiteral(
                        "host row does not match hostParticipantId"));
            }
            if (participant.identity.kind != IdentityKind::SignedIn)
                return fail(error, QStringLiteral("host must be signed in"));
        }

        parsed.participants.append(participant);
    }

    if (hostCount != 1) {
        return fail(
            error,
            QStringLiteral("snapshot must contain exactly one host"));
    }

    const QJsonValue deadline =
        object.value(QStringLiteral("hostReconnectDeadlineMs"));
    if (!deadline.isDouble()) {
        return fail(
            error,
            QStringLiteral(
                "hostReconnectDeadlineMs must be an integer"));
    }

    parsed.hostReconnectDeadlineMs = deadline.toInteger(-2);
    if (static_cast<double>(parsed.hostReconnectDeadlineMs)
            != deadline.toDouble()
        || parsed.hostReconnectDeadlineMs < -1) {
        return fail(
            error,
            QStringLiteral(
                "hostReconnectDeadlineMs must be -1 or a non-negative integer"));
    }

    parsed.roomId = parsed.roomId.trimmed();
    parsed.hostParticipantId = parsed.hostParticipantId.trimmed();
    *snapshot = parsed;
    return true;
}

QJsonObject sessionEstablishedToJson(const SessionEstablished& session)
{
    return QJsonObject{
        {QStringLiteral("participantId"), session.participantId},
        {QStringLiteral("reconnectToken"), session.reconnectToken}
    };
}

bool sessionEstablishedFromJson(const QJsonObject& object,
                                SessionEstablished* session,
                                QString* error)
{
    if (!session)
        return fail(error, QStringLiteral("session output is null"));

    if (!exactKeys(
            object,
            {
                QStringLiteral("participantId"),
                QStringLiteral("reconnectToken")
            },
            QStringLiteral("sessionEstablished payload"),
            error)) {
        return false;
    }

    SessionEstablished parsed;
    if (!readString(
            object,
            QStringLiteral("participantId"),
            &parsed.participantId,
            error)
        || !readString(
            object,
            QStringLiteral("reconnectToken"),
            &parsed.reconnectToken,
            error)) {
        return false;
    }

    parsed.participantId = parsed.participantId.trimmed();
    parsed.reconnectToken = parsed.reconnectToken.trimmed();
    *session = parsed;
    return true;
}

} // namespace Colosseum::WatchParty
