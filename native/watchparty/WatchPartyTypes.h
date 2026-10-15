#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

namespace Colosseum::WatchParty {

// Slice 4 makes the protocol direction-aware and adds the private session-
// establishment/reconnect credential handshake required for a production
// service boundary. This is a wire-incompatible refinement of Slice 2, so the
// cumulative unadopted reference bundle advances the protocol version.
constexpr int kProtocolVersion = 3;
constexpr int kMaxParticipants = 12;

enum class IdentityKind {
    SignedIn,
    Guest
};

enum class ControlMode {
    HostControl,
    SharedControl
};

enum class SyncStatus {
    Unknown,
    InSync,
    Desynced,
    Buffering
};

enum class TimelineCommandType {
    Play,
    Pause,
    Seek
};

enum class RoomError {
    None,
    RoomNotActive,
    RoomAlreadyActive,
    InvalidRoomId,
    InvalidIdentity,
    InvalidSource,
    HostMustBeSignedIn,
    RoomFull,
    AlreadyJoined,
    NotParticipant,
    ParticipantDisconnected,
    NotHost,
    NotAuthorized,
    HostMustEndRoom,
    CannotRemoveHost,
    InvalidTimelineCommand,
    InvalidParticipantState,
    InvalidEvent,
    InvalidGracePeriod
};

struct Result {
    RoomError error = RoomError::None;
    QString detail;

    bool ok() const { return error == RoomError::None; }

    static Result success() { return {}; }
    static Result failure(RoomError error, const QString& detail = QString())
    {
        return Result{error, detail};
    }
};

struct ParticipantIdentity {
    QString participantId;
    QString displayName;
    IdentityKind kind = IdentityKind::Guest;

    bool isValid() const
    {
        return !participantId.trimmed().isEmpty() && !displayName.trimmed().isEmpty();
    }
};

struct ParticipantState {
    ParticipantIdentity identity;
    quint64 joinOrder = 0;
    bool host = false;
    bool connected = true;
    bool ready = false;
    SyncStatus syncStatus = SyncStatus::Unknown;
};

enum class SourceKind {
    Unknown,
    Torrent,
    Debrid
};

struct SourceDescriptor {
    SourceKind kind = SourceKind::Unknown;

    // Torrent authority: the exact BitTorrent content identity plus selected file.
    QString infoHash;
    int fileIdx = 0;

    // Debrid authority is deliberately provider-owned. providerSourceId is a
    // non-secret, non-URL source identifier produced only by a provider adapter
    // whose portability/credential semantics have been verified. It is NEVER a
    // resolved download URL, bearer token, cookie, request header, or credential.
    QString providerId;
    QString providerSourceId;

    static SourceDescriptor torrent(const QString& infoHash, int fileIdx);
    static SourceDescriptor debrid(const QString& providerId,
                                   const QString& providerSourceId);

    SourceDescriptor normalized() const;
    bool isValid() const;
};

struct TimelineState {
    bool playing = false;
    qint64 positionMs = 0;
    quint64 revision = 0;
};

struct TimelineCommand {
    TimelineCommandType type = TimelineCommandType::Pause;
    bool hasPosition = false;
    qint64 positionMs = 0;

    static TimelineCommand play()
    {
        return TimelineCommand{TimelineCommandType::Play, false, 0};
    }

    static TimelineCommand playAt(qint64 positionMs)
    {
        return TimelineCommand{TimelineCommandType::Play, true, positionMs};
    }

    static TimelineCommand pause()
    {
        return TimelineCommand{TimelineCommandType::Pause, false, 0};
    }

    static TimelineCommand pauseAt(qint64 positionMs)
    {
        return TimelineCommand{TimelineCommandType::Pause, true, positionMs};
    }

    static TimelineCommand seek(qint64 positionMs)
    {
        return TimelineCommand{TimelineCommandType::Seek, true, positionMs};
    }
};

struct ChatEvent {
    quint64 sequence = 0;
    QString participantId;
    QString displayName;
    QString message;
};

struct ReactionEvent {
    quint64 sequence = 0;
    QString participantId;
    QString displayName;
    QString reaction;
};

struct RoomSnapshot {
    QString roomId;
    QString hostParticipantId;
    SourceDescriptor source;
    ControlMode controlMode = ControlMode::HostControl;
    TimelineState timeline;
    QList<ParticipantState> participants;
    qint64 hostReconnectDeadlineMs = -1;
};

QString identityKindName(IdentityKind kind);
bool identityKindFromName(const QString& name, IdentityKind* kind);

QString controlModeName(ControlMode mode);
bool controlModeFromName(const QString& name, ControlMode* mode);

QString syncStatusName(SyncStatus status);
bool syncStatusFromName(const QString& name, SyncStatus* status);

QString timelineCommandTypeName(TimelineCommandType type);
bool timelineCommandTypeFromName(const QString& name, TimelineCommandType* type);

QString sourceKindName(SourceKind kind);
bool sourceKindFromName(const QString& name, SourceKind* kind);

QString roomErrorName(RoomError error);

} // namespace Colosseum::WatchParty
