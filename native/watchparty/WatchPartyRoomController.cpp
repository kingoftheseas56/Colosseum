#include "watchparty/WatchPartyRoomController.h"

#include <limits>

namespace Colosseum::WatchParty {

const ParticipantState* RoomController::participant(const QString& participantId) const
{
    for (const ParticipantState& participant : m_participants) {
        if (participant.identity.participantId == participantId)
            return &participant;
    }
    return nullptr;
}

RoomSnapshot RoomController::snapshot() const
{
    RoomSnapshot value;
    value.roomId = m_roomId;
    value.hostParticipantId = m_hostParticipantId;
    value.source = m_source;
    value.controlMode = m_controlMode;
    value.timeline = m_timeline;
    value.participants = m_participants;
    value.hostReconnectDeadlineMs = m_hostReconnectDeadlineMs;
    return value;
}

Result RoomController::create(const QString& roomId,
                              const ParticipantIdentity& host,
                              const SourceDescriptor& source)
{
    if (m_active)
        return Result::failure(RoomError::RoomAlreadyActive);

    const QString cleanedRoomId = roomId.trimmed();
    const ParticipantIdentity cleanedHost = cleanedIdentity(host);
    const SourceDescriptor normalizedSource = cleanedSource(source);

    if (cleanedRoomId.isEmpty())
        return Result::failure(RoomError::InvalidRoomId);
    if (!cleanedHost.isValid())
        return Result::failure(RoomError::InvalidIdentity);
    if (cleanedHost.kind != IdentityKind::SignedIn)
        return Result::failure(RoomError::HostMustBeSignedIn);
    if (!normalizedSource.isValid())
        return Result::failure(RoomError::InvalidSource);

    clear();

    m_active = true;
    m_roomId = cleanedRoomId;
    m_source = normalizedSource;
    m_controlMode = ControlMode::HostControl;

    ParticipantState hostState;
    hostState.identity = cleanedHost;
    hostState.joinOrder = 0;
    hostState.host = true;
    hostState.connected = true;

    m_hostParticipantId = hostState.identity.participantId;
    m_participants.append(hostState);
    return Result::success();
}

Result RoomController::join(const ParticipantIdentity& identity)
{
    if (!m_active)
        return Result::failure(RoomError::RoomNotActive);

    const ParticipantIdentity cleaned = cleanedIdentity(identity);
    if (!cleaned.isValid())
        return Result::failure(RoomError::InvalidIdentity);
    if (participant(cleaned.participantId))
        return Result::failure(RoomError::AlreadyJoined);
    if (m_participants.size() >= kMaxParticipants)
        return Result::failure(RoomError::RoomFull);

    ParticipantState state;
    state.identity = cleaned;
    state.joinOrder = m_nextJoinOrder++;
    m_participants.append(state);
    return Result::success();
}

Result RoomController::leave(const QString& participantId)
{
    ParticipantState* state = nullptr;
    const Result membership = requireConnectedParticipant(participantId, &state);
    if (!membership.ok())
        return membership;

    if (state->host)
        return Result::failure(RoomError::HostMustEndRoom);

    eraseParticipant(participantId);
    if (m_participants.isEmpty())
        clear();
    return Result::success();
}

Result RoomController::removeParticipant(const QString& actorId,
                                         const QString& participantId)
{
    const Result authority = requireHost(actorId);
    if (!authority.ok())
        return authority;

    const ParticipantState* target = participant(participantId);
    if (!target)
        return Result::failure(RoomError::NotParticipant);
    if (target->host)
        return Result::failure(RoomError::CannotRemoveHost);

    eraseParticipant(participantId);
    return Result::success();
}

Result RoomController::endRoom(const QString& actorId)
{
    const Result authority = requireHost(actorId);
    if (!authority.ok())
        return authority;

    clear();
    return Result::success();
}

Result RoomController::setControlMode(const QString& actorId, ControlMode mode)
{
    const Result authority = requireHost(actorId);
    if (!authority.ok())
        return authority;

    m_controlMode = mode;
    return Result::success();
}

bool RoomController::canControlTimeline(const QString& participantId) const
{
    const ParticipantState* state = participant(participantId);
    if (!m_active || !state || !state->connected)
        return false;

    return state->host || m_controlMode == ControlMode::SharedControl;
}

Result RoomController::applyTimelineCommand(const QString& actorId,
                                            const TimelineCommand& command)
{
    const Result membership = requireConnectedParticipant(actorId);
    if (!membership.ok())
        return membership;
    if (!canControlTimeline(actorId))
        return Result::failure(RoomError::NotAuthorized);
    if (command.hasPosition && command.positionMs < 0)
        return Result::failure(RoomError::InvalidTimelineCommand);
    if (command.type == TimelineCommandType::Seek && !command.hasPosition)
        return Result::failure(RoomError::InvalidTimelineCommand);

    switch (command.type) {
    case TimelineCommandType::Play:
        m_timeline.playing = true;
        if (command.hasPosition)
            m_timeline.positionMs = command.positionMs;
        break;
    case TimelineCommandType::Pause:
        m_timeline.playing = false;
        if (command.hasPosition)
            m_timeline.positionMs = command.positionMs;
        break;
    case TimelineCommandType::Seek:
        m_timeline.positionMs = command.positionMs;
        break;
    default:
        return Result::failure(RoomError::InvalidTimelineCommand);
    }

    ++m_timeline.revision;
    return Result::success();
}

Result RoomController::setReady(const QString& participantId, bool ready)
{
    ParticipantState* state = nullptr;
    const Result membership = requireConnectedParticipant(participantId, &state);
    if (!membership.ok())
        return membership;

    state->ready = ready;
    if (!ready)
        state->syncStatus = SyncStatus::Unknown;
    return Result::success();
}

Result RoomController::setSyncStatus(const QString& participantId, SyncStatus status)
{
    ParticipantState* state = nullptr;
    const Result membership = requireConnectedParticipant(participantId, &state);
    if (!membership.ok())
        return membership;
    if (!state->ready && status != SyncStatus::Unknown)
        return Result::failure(RoomError::InvalidParticipantState);

    state->syncStatus = status;
    return Result::success();
}

Result RoomController::addChat(const QString& participantId, const QString& message)
{
    ParticipantState* state = nullptr;
    const Result membership = requireConnectedParticipant(participantId, &state);
    if (!membership.ok())
        return membership;

    const QString cleaned = message.trimmed();
    if (cleaned.isEmpty())
        return Result::failure(RoomError::InvalidEvent);

    ChatEvent event;
    event.sequence = m_nextEventSequence++;
    event.participantId = state->identity.participantId;
    event.displayName = state->identity.displayName;
    event.message = cleaned;
    m_chatEvents.append(event);
    return Result::success();
}

Result RoomController::addReaction(const QString& participantId,
                                   const QString& reaction)
{
    ParticipantState* state = nullptr;
    const Result membership = requireConnectedParticipant(participantId, &state);
    if (!membership.ok())
        return membership;

    const QString cleaned = reaction.trimmed();
    if (cleaned.isEmpty())
        return Result::failure(RoomError::InvalidEvent);

    ReactionEvent event;
    event.sequence = m_nextEventSequence++;
    event.participantId = state->identity.participantId;
    event.displayName = state->identity.displayName;
    event.reaction = cleaned;
    m_reactionEvents.append(event);
    return Result::success();
}

Result RoomController::disconnect(const QString& participantId,
                                  qint64 nowMs,
                                  qint64 hostGraceMs)
{
    if (!m_active)
        return Result::failure(RoomError::RoomNotActive);
    if (nowMs < 0 || hostGraceMs < 0
        || hostGraceMs > std::numeric_limits<qint64>::max() - nowMs) {
        return Result::failure(RoomError::InvalidGracePeriod);
    }

    ParticipantState* state = mutableParticipant(participantId);
    if (!state)
        return Result::failure(RoomError::NotParticipant);
    if (!state->connected)
        return Result::success();

    state->connected = false;
    state->ready = false;
    state->syncStatus = SyncStatus::Unknown;

    if (state->host) {
        m_hostReconnectDeadlineMs = nowMs + hostGraceMs;
        if (hostGraceMs == 0)
            return advanceTime(nowMs);
    }

    return Result::success();
}

Result RoomController::reconnect(const QString& participantId, qint64 nowMs)
{
    if (nowMs < 0)
        return Result::failure(RoomError::InvalidGracePeriod);

    const Result timeResult = advanceTime(nowMs);
    if (!timeResult.ok())
        return timeResult;
    if (!m_active)
        return Result::failure(RoomError::RoomNotActive);

    ParticipantState* state = mutableParticipant(participantId);
    if (!state)
        return Result::failure(RoomError::NotParticipant);

    state->connected = true;
    if (state->host)
        m_hostReconnectDeadlineMs = -1;
    return Result::success();
}

Result RoomController::advanceTime(qint64 nowMs)
{
    if (!m_active)
        return Result::failure(RoomError::RoomNotActive);
    if (nowMs < 0)
        return Result::failure(RoomError::InvalidGracePeriod);
    if (m_hostReconnectDeadlineMs < 0 || nowMs < m_hostReconnectDeadlineMs)
        return Result::success();

    ParticipantState* host = mutableParticipant(m_hostParticipantId);
    if (host && host->connected) {
        m_hostReconnectDeadlineMs = -1;
        return Result::success();
    }

    const QString previousHostId = m_hostParticipantId;
    eraseParticipant(previousHostId);

    ParticipantState* successor = nullptr;
    for (ParticipantState& candidate : m_participants) {
        if (!candidate.connected
            || candidate.identity.kind != IdentityKind::SignedIn) {
            continue;
        }

        if (!successor || candidate.joinOrder < successor->joinOrder)
            successor = &candidate;
    }

    if (!successor) {
        clear();
        return Result::success();
    }

    successor->host = true;
    m_hostParticipantId = successor->identity.participantId;
    m_hostReconnectDeadlineMs = -1;
    return Result::success();
}

ParticipantState* RoomController::mutableParticipant(const QString& participantId)
{
    for (ParticipantState& participant : m_participants) {
        if (participant.identity.participantId == participantId)
            return &participant;
    }
    return nullptr;
}

Result RoomController::requireConnectedParticipant(
    const QString& participantId,
    ParticipantState** participantOut)
{
    if (!m_active)
        return Result::failure(RoomError::RoomNotActive);

    ParticipantState* state = mutableParticipant(participantId);
    if (!state)
        return Result::failure(RoomError::NotParticipant);
    if (!state->connected)
        return Result::failure(RoomError::ParticipantDisconnected);

    if (participantOut)
        *participantOut = state;
    return Result::success();
}

Result RoomController::requireHost(const QString& participantId,
                                   ParticipantState** participantOut)
{
    ParticipantState* state = nullptr;
    const Result membership =
        requireConnectedParticipant(participantId, &state);
    if (!membership.ok())
        return membership;
    if (!state->host)
        return Result::failure(RoomError::NotHost);

    if (participantOut)
        *participantOut = state;
    return Result::success();
}

ParticipantIdentity RoomController::cleanedIdentity(
    const ParticipantIdentity& identity)
{
    ParticipantIdentity cleaned = identity;
    cleaned.participantId = cleaned.participantId.trimmed();
    cleaned.displayName = cleaned.displayName.trimmed();
    return cleaned;
}

SourceDescriptor RoomController::cleanedSource(const SourceDescriptor& source)
{
    return source.normalized();
}

void RoomController::eraseParticipant(const QString& participantId)
{
    for (int i = 0; i < m_participants.size(); ++i) {
        if (m_participants.at(i).identity.participantId == participantId) {
            m_participants.removeAt(i);
            return;
        }
    }
}

void RoomController::clear()
{
    m_active = false;
    m_roomId.clear();
    m_hostParticipantId.clear();
    m_source = {};
    m_controlMode = ControlMode::HostControl;
    m_timeline = {};
    m_participants.clear();
    m_chatEvents.clear();
    m_reactionEvents.clear();
    m_hostReconnectDeadlineMs = -1;
    m_nextJoinOrder = 1;
    m_nextEventSequence = 1;
}

} // namespace Colosseum::WatchParty
