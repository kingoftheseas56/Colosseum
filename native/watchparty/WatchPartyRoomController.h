#pragma once

#include "watchparty/WatchPartyTypes.h"

#include <QList>
#include <QString>

namespace Colosseum::WatchParty {

class RoomController
{
public:
    bool active() const { return m_active; }
    QString roomId() const { return m_roomId; }
    QString hostParticipantId() const { return m_hostParticipantId; }
    ControlMode controlMode() const { return m_controlMode; }
    TimelineState timeline() const { return m_timeline; }
    SourceDescriptor source() const { return m_source; }

    const QList<ParticipantState>& participants() const { return m_participants; }
    const QList<ChatEvent>& chatEvents() const { return m_chatEvents; }
    const QList<ReactionEvent>& reactionEvents() const { return m_reactionEvents; }

    qint64 hostReconnectDeadlineMs() const { return m_hostReconnectDeadlineMs; }

    const ParticipantState* participant(const QString& participantId) const;
    RoomSnapshot snapshot() const;

    Result create(const QString& roomId,
                  const ParticipantIdentity& host,
                  const SourceDescriptor& source);
    // This in-process model owns current room membership only. It does not
    // retain durable actor identity after removal, so join() must not be read as
    // the product's unresolved removed-participant fresh re-entry policy.
    Result join(const ParticipantIdentity& participant);
    Result leave(const QString& participantId);
    Result removeParticipant(const QString& actorId, const QString& participantId);
    Result endRoom(const QString& actorId);

    Result setControlMode(const QString& actorId, ControlMode mode);
    bool canControlTimeline(const QString& participantId) const;
    Result applyTimelineCommand(const QString& actorId,
                                const TimelineCommand& command);

    Result setReady(const QString& participantId, bool ready);
    Result setSyncStatus(const QString& participantId, SyncStatus status);

    Result addChat(const QString& participantId, const QString& message);
    Result addReaction(const QString& participantId, const QString& reaction);

    Result disconnect(const QString& participantId,
                      qint64 nowMs,
                      qint64 hostGraceMs);
    Result reconnect(const QString& participantId, qint64 nowMs);
    Result advanceTime(qint64 nowMs);

private:
    ParticipantState* mutableParticipant(const QString& participantId);
    Result requireConnectedParticipant(
        const QString& participantId,
        ParticipantState** participant = nullptr);
    Result requireHost(const QString& participantId,
                       ParticipantState** participant = nullptr);

    static ParticipantIdentity cleanedIdentity(const ParticipantIdentity& identity);
    static SourceDescriptor cleanedSource(const SourceDescriptor& source);

    void eraseParticipant(const QString& participantId);
    void clear();

    bool m_active = false;
    QString m_roomId;
    QString m_hostParticipantId;
    SourceDescriptor m_source;
    ControlMode m_controlMode = ControlMode::HostControl;
    TimelineState m_timeline;
    QList<ParticipantState> m_participants;
    QList<ChatEvent> m_chatEvents;
    QList<ReactionEvent> m_reactionEvents;
    qint64 m_hostReconnectDeadlineMs = -1;
    quint64 m_nextJoinOrder = 1;
    quint64 m_nextEventSequence = 1;
};

} // namespace Colosseum::WatchParty
