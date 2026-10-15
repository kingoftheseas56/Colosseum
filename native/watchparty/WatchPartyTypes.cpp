#include "watchparty/WatchPartyTypes.h"

#include <QRegularExpression>

namespace Colosseum::WatchParty {
namespace {

bool validInfoHash(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})$"));
    return pattern.match(value).hasMatch();
}

bool validProviderId(const QString& value)
{
    // Provider identity is a stable adapter key, never a URL or credential.
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return pattern.match(value).hasMatch();
}

bool validProviderSourceId(const QString& value)
{
    // Keep the wire grammar intentionally narrower than URLs/query strings.
    // A provider adapter still owns the stronger semantic guarantee that this
    // identifier is non-secret and independently resolvable by each participant.
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,255}$"));
    return pattern.match(value).hasMatch();
}

} // namespace

SourceDescriptor SourceDescriptor::torrent(const QString& infoHash, int fileIdx)
{
    SourceDescriptor descriptor;
    descriptor.kind = SourceKind::Torrent;
    descriptor.infoHash = infoHash;
    descriptor.fileIdx = fileIdx;
    return descriptor.normalized();
}

SourceDescriptor SourceDescriptor::debrid(const QString& providerId,
                                          const QString& providerSourceId)
{
    SourceDescriptor descriptor;
    descriptor.kind = SourceKind::Debrid;
    descriptor.providerId = providerId;
    descriptor.providerSourceId = providerSourceId;
    return descriptor.normalized();
}

SourceDescriptor SourceDescriptor::normalized() const
{
    SourceDescriptor descriptor = *this;
    descriptor.infoHash = descriptor.infoHash.trimmed().toLower();
    descriptor.providerId = descriptor.providerId.trimmed();
    descriptor.providerSourceId = descriptor.providerSourceId.trimmed();
    return descriptor;
}

bool SourceDescriptor::isValid() const
{
    const SourceDescriptor descriptor = normalized();
    switch (descriptor.kind) {
    case SourceKind::Torrent:
        return validInfoHash(descriptor.infoHash)
            && descriptor.fileIdx >= 0
            && descriptor.providerId.isEmpty()
            && descriptor.providerSourceId.isEmpty();
    case SourceKind::Debrid:
        return descriptor.infoHash.isEmpty()
            && descriptor.fileIdx == 0
            && validProviderId(descriptor.providerId)
            && validProviderSourceId(descriptor.providerSourceId);
    case SourceKind::Unknown:
        break;
    }
    return false;
}

QString identityKindName(IdentityKind kind)
{
    switch (kind) {
    case IdentityKind::SignedIn:
        return QStringLiteral("signedIn");
    case IdentityKind::Guest:
        return QStringLiteral("guest");
    }
    return QStringLiteral("guest");
}

bool identityKindFromName(const QString& name, IdentityKind* kind)
{
    if (!kind)
        return false;
    if (name == QStringLiteral("signedIn")) {
        *kind = IdentityKind::SignedIn;
        return true;
    }
    if (name == QStringLiteral("guest")) {
        *kind = IdentityKind::Guest;
        return true;
    }
    return false;
}

QString controlModeName(ControlMode mode)
{
    switch (mode) {
    case ControlMode::HostControl:
        return QStringLiteral("host");
    case ControlMode::SharedControl:
        return QStringLiteral("shared");
    }
    return QStringLiteral("host");
}

bool controlModeFromName(const QString& name, ControlMode* mode)
{
    if (!mode)
        return false;
    if (name == QStringLiteral("host")) {
        *mode = ControlMode::HostControl;
        return true;
    }
    if (name == QStringLiteral("shared")) {
        *mode = ControlMode::SharedControl;
        return true;
    }
    return false;
}

QString syncStatusName(SyncStatus status)
{
    switch (status) {
    case SyncStatus::Unknown:
        return QStringLiteral("unknown");
    case SyncStatus::InSync:
        return QStringLiteral("inSync");
    case SyncStatus::Desynced:
        return QStringLiteral("desynced");
    case SyncStatus::Buffering:
        return QStringLiteral("buffering");
    }
    return QStringLiteral("unknown");
}

bool syncStatusFromName(const QString& name, SyncStatus* status)
{
    if (!status)
        return false;
    if (name == QStringLiteral("unknown")) {
        *status = SyncStatus::Unknown;
        return true;
    }
    if (name == QStringLiteral("inSync")) {
        *status = SyncStatus::InSync;
        return true;
    }
    if (name == QStringLiteral("desynced")) {
        *status = SyncStatus::Desynced;
        return true;
    }
    if (name == QStringLiteral("buffering")) {
        *status = SyncStatus::Buffering;
        return true;
    }
    return false;
}

QString timelineCommandTypeName(TimelineCommandType type)
{
    switch (type) {
    case TimelineCommandType::Play:
        return QStringLiteral("play");
    case TimelineCommandType::Pause:
        return QStringLiteral("pause");
    case TimelineCommandType::Seek:
        return QStringLiteral("seek");
    }
    return QStringLiteral("pause");
}

bool timelineCommandTypeFromName(const QString& name, TimelineCommandType* type)
{
    if (!type)
        return false;
    if (name == QStringLiteral("play")) {
        *type = TimelineCommandType::Play;
        return true;
    }
    if (name == QStringLiteral("pause")) {
        *type = TimelineCommandType::Pause;
        return true;
    }
    if (name == QStringLiteral("seek")) {
        *type = TimelineCommandType::Seek;
        return true;
    }
    return false;
}

QString sourceKindName(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Torrent:
        return QStringLiteral("torrent");
    case SourceKind::Debrid:
        return QStringLiteral("debrid");
    case SourceKind::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

bool sourceKindFromName(const QString& name, SourceKind* kind)
{
    if (!kind)
        return false;
    if (name == QStringLiteral("torrent")) {
        *kind = SourceKind::Torrent;
        return true;
    }
    if (name == QStringLiteral("debrid")) {
        *kind = SourceKind::Debrid;
        return true;
    }
    return false;
}

QString roomErrorName(RoomError error)
{
    switch (error) {
    case RoomError::None:
        return QStringLiteral("none");
    case RoomError::RoomNotActive:
        return QStringLiteral("roomNotActive");
    case RoomError::RoomAlreadyActive:
        return QStringLiteral("roomAlreadyActive");
    case RoomError::InvalidRoomId:
        return QStringLiteral("invalidRoomId");
    case RoomError::InvalidIdentity:
        return QStringLiteral("invalidIdentity");
    case RoomError::InvalidSource:
        return QStringLiteral("invalidSource");
    case RoomError::HostMustBeSignedIn:
        return QStringLiteral("hostMustBeSignedIn");
    case RoomError::RoomFull:
        return QStringLiteral("roomFull");
    case RoomError::AlreadyJoined:
        return QStringLiteral("alreadyJoined");
    case RoomError::NotParticipant:
        return QStringLiteral("notParticipant");
    case RoomError::ParticipantDisconnected:
        return QStringLiteral("participantDisconnected");
    case RoomError::NotHost:
        return QStringLiteral("notHost");
    case RoomError::NotAuthorized:
        return QStringLiteral("notAuthorized");
    case RoomError::HostMustEndRoom:
        return QStringLiteral("hostMustEndRoom");
    case RoomError::CannotRemoveHost:
        return QStringLiteral("cannotRemoveHost");
    case RoomError::InvalidTimelineCommand:
        return QStringLiteral("invalidTimelineCommand");
    case RoomError::InvalidParticipantState:
        return QStringLiteral("invalidParticipantState");
    case RoomError::InvalidEvent:
        return QStringLiteral("invalidEvent");
    case RoomError::InvalidGracePeriod:
        return QStringLiteral("invalidGracePeriod");
    }
    return QStringLiteral("unknown");
}

} // namespace Colosseum::WatchParty
