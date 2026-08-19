#include "watchparty/FakeWatchPartyTransport.h"
#include "watchparty/WatchPartyProtocol.h"
#include "watchparty/WatchPartyRoomController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

namespace WatchParty = Colosseum::WatchParty;

namespace {

WatchParty::ParticipantIdentity signedIn(const QString& id, const QString& name)
{
    return WatchParty::ParticipantIdentity{
        id,
        name,
        WatchParty::IdentityKind::SignedIn
    };
}

WatchParty::ParticipantIdentity guest(const QString& id, const QString& name)
{
    return WatchParty::ParticipantIdentity{
        id,
        name,
        WatchParty::IdentityKind::Guest
    };
}

WatchParty::SourceDescriptor testSource()
{
    return WatchParty::SourceDescriptor::torrent(
        QStringLiteral("0123456789abcdef0123456789abcdef01234567"), 7);
}

void createRoom(WatchParty::RoomController* room)
{
    QVERIFY(room);

    const WatchParty::Result result =
        room->create(QStringLiteral("room-opaque-1"),
                     signedIn(QStringLiteral("host"), QStringLiteral("Host")),
                     testSource());
    QCOMPARE(result.error, WatchParty::RoomError::None);
}

} // namespace

class tst_watchparty_room final : public QObject
{
    Q_OBJECT

private slots:
    void create_requires_signed_in_host_and_defaults_to_host_control();
    void join_enforces_capacity_and_identity_uniqueness();
    void host_control_rejects_participant_timeline_commands();
    void shared_control_allows_timeline_but_not_administration();
    void host_can_remove_non_host_participant();
    void readiness_and_sync_status_are_participant_local();
    void explicit_leave_and_end_destroy_ephemeral_room_state();
    void host_reconnect_within_grace_keeps_authority();
    void host_transfer_uses_earliest_connected_signed_in_participant();
    void guest_never_inherits_and_room_ends_without_eligible_successor();
    void protocol_round_trips_room_and_ephemeral_events();
    void protocol_rejects_version_mismatch_and_invalid_guest_host();
    void fake_transport_records_and_injects_synchronously();
};

void tst_watchparty_room::create_requires_signed_in_host_and_defaults_to_host_control()
{
    WatchParty::RoomController room;

    const WatchParty::Result guestHost =
        room.create(QStringLiteral("room-a"),
                    guest(QStringLiteral("guest-host"), QStringLiteral("Guest Host")),
                    testSource());
    QCOMPARE(guestHost.error, WatchParty::RoomError::HostMustBeSignedIn);
    QVERIFY(!room.active());

    const WatchParty::Result created =
        room.create(QStringLiteral("  room-a  "),
                    signedIn(QStringLiteral(" host "), QStringLiteral(" Host ")),
                    testSource());
    QCOMPARE(created.error, WatchParty::RoomError::None);
    QVERIFY(room.active());
    QCOMPARE(room.roomId(), QStringLiteral("room-a"));
    QCOMPARE(room.hostParticipantId(), QStringLiteral("host"));
    QCOMPARE(room.controlMode(), WatchParty::ControlMode::HostControl);
    QCOMPARE(room.participants().size(), 1);
    QVERIFY(room.participants().first().host);
    QCOMPARE(room.participants().first().identity.kind,
             WatchParty::IdentityKind::SignedIn);
    QCOMPARE(room.timeline().playing, false);
    QCOMPARE(room.timeline().positionMs, qint64(0));
    QCOMPARE(room.timeline().revision, quint64(0));
}

void tst_watchparty_room::join_enforces_capacity_and_identity_uniqueness()
{
    WatchParty::RoomController room;
    createRoom(&room);

    QCOMPARE(room.join(guest(QStringLiteral("p1"), QStringLiteral("One"))).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.join(guest(QStringLiteral(" p1 "), QStringLiteral("Duplicate"))).error,
             WatchParty::RoomError::AlreadyJoined);

    for (int i = 2; i <= 11; ++i) {
        const QString id = QStringLiteral("p%1").arg(i);
        QCOMPARE(room.join(guest(id, id)).error, WatchParty::RoomError::None);
    }

    QCOMPARE(room.participants().size(), WatchParty::kMaxParticipants);
    QCOMPARE(room.join(guest(QStringLiteral("p12"), QStringLiteral("Too Many"))).error,
             WatchParty::RoomError::RoomFull);
    QCOMPARE(room.participants().size(), WatchParty::kMaxParticipants);
}

void tst_watchparty_room::host_control_rejects_participant_timeline_commands()
{
    WatchParty::RoomController room;
    createRoom(&room);
    QCOMPARE(room.join(guest(QStringLiteral("guest"), QStringLiteral("Guest"))).error,
             WatchParty::RoomError::None);

    // Mutation negative control: if Host Control is weakened to permit any connected
    // participant, both assertions below fail before player/network integration exists.
    QVERIFY(!room.canControlTimeline(QStringLiteral("guest")));
    const WatchParty::Result refused =
        room.applyTimelineCommand(QStringLiteral("guest"),
                                  WatchParty::TimelineCommand::playAt(5'000));
    QCOMPARE(refused.error, WatchParty::RoomError::NotAuthorized);
    QCOMPARE(room.timeline().playing, false);
    QCOMPARE(room.timeline().positionMs, qint64(0));
    QCOMPARE(room.timeline().revision, quint64(0));

    QVERIFY(room.canControlTimeline(QStringLiteral("host")));
    QCOMPARE(room.applyTimelineCommand(QStringLiteral("host"),
                                       WatchParty::TimelineCommand::playAt(5'000)).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.timeline().playing, true);
    QCOMPARE(room.timeline().positionMs, qint64(5'000));
    QCOMPARE(room.timeline().revision, quint64(1));
}

void tst_watchparty_room::shared_control_allows_timeline_but_not_administration()
{
    WatchParty::RoomController room;
    createRoom(&room);
    QCOMPARE(room.join(guest(QStringLiteral("guest"), QStringLiteral("Guest"))).error,
             WatchParty::RoomError::None);

    QCOMPARE(room.setControlMode(QStringLiteral("host"),
                                 WatchParty::ControlMode::SharedControl).error,
             WatchParty::RoomError::None);
    QVERIFY(room.canControlTimeline(QStringLiteral("guest")));

    QCOMPARE(room.applyTimelineCommand(QStringLiteral("guest"),
                                       WatchParty::TimelineCommand::seek(42'000)).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.timeline().positionMs, qint64(42'000));

    QCOMPARE(room.setControlMode(QStringLiteral("guest"),
                                 WatchParty::ControlMode::HostControl).error,
             WatchParty::RoomError::NotHost);
    QCOMPARE(room.removeParticipant(QStringLiteral("guest"),
                                    QStringLiteral("host")).error,
             WatchParty::RoomError::NotHost);
    QCOMPARE(room.endRoom(QStringLiteral("guest")).error,
             WatchParty::RoomError::NotHost);
    QCOMPARE(room.controlMode(), WatchParty::ControlMode::SharedControl);
    QVERIFY(room.active());
}

void tst_watchparty_room::host_can_remove_non_host_participant()
{
    WatchParty::RoomController room;
    createRoom(&room);
    QCOMPARE(room.join(guest(QStringLiteral("guest"), QStringLiteral("Guest"))).error,
             WatchParty::RoomError::None);

    QCOMPARE(room.removeParticipant(QStringLiteral("host"),
                                    QStringLiteral("guest")).error,
             WatchParty::RoomError::None);
    QVERIFY(!room.participant(QStringLiteral("guest")));
    QCOMPARE(room.participants().size(), 1);

    QCOMPARE(room.removeParticipant(QStringLiteral("host"),
                                    QStringLiteral("host")).error,
             WatchParty::RoomError::CannotRemoveHost);
    QVERIFY(room.active());
}

void tst_watchparty_room::readiness_and_sync_status_are_participant_local()
{
    WatchParty::RoomController room;
    createRoom(&room);
    QCOMPARE(room.join(guest(QStringLiteral("guest"), QStringLiteral("Guest"))).error,
             WatchParty::RoomError::None);

    QCOMPARE(room.setSyncStatus(QStringLiteral("guest"),
                                WatchParty::SyncStatus::Buffering).error,
             WatchParty::RoomError::InvalidParticipantState);
    QCOMPARE(room.setReady(QStringLiteral("guest"), true).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.setSyncStatus(QStringLiteral("guest"),
                                WatchParty::SyncStatus::Buffering).error,
             WatchParty::RoomError::None);

    const WatchParty::ParticipantState* state =
        room.participant(QStringLiteral("guest"));
    QVERIFY(state);
    QVERIFY(state->ready);
    QCOMPARE(state->syncStatus, WatchParty::SyncStatus::Buffering);

    QCOMPARE(room.setReady(QStringLiteral("guest"), false).error,
             WatchParty::RoomError::None);
    state = room.participant(QStringLiteral("guest"));
    QVERIFY(state);
    QVERIFY(!state->ready);
    QCOMPARE(state->syncStatus, WatchParty::SyncStatus::Unknown);

    QCOMPARE(room.timeline().revision, quint64(0));
    QCOMPARE(room.timeline().playing, false);
}

void tst_watchparty_room::explicit_leave_and_end_destroy_ephemeral_room_state()
{
    WatchParty::RoomController room;
    createRoom(&room);
    QCOMPARE(room.join(guest(QStringLiteral("guest"), QStringLiteral("Guest"))).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.addChat(QStringLiteral("guest"), QStringLiteral("hello")).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.addReaction(QStringLiteral("host"), QStringLiteral("fire")).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.applyTimelineCommand(QStringLiteral("host"),
                                       WatchParty::TimelineCommand::playAt(12'345)).error,
             WatchParty::RoomError::None);

    QCOMPARE(room.leave(QStringLiteral("guest")).error,
             WatchParty::RoomError::None);
    QVERIFY(!room.participant(QStringLiteral("guest")));
    QVERIFY(room.active());

    QCOMPARE(room.endRoom(QStringLiteral("host")).error,
             WatchParty::RoomError::None);
    QVERIFY(!room.active());
    QVERIFY(room.roomId().isEmpty());
    QVERIFY(room.participants().isEmpty());
    QVERIFY(room.chatEvents().isEmpty());
    QVERIFY(room.reactionEvents().isEmpty());
    QVERIFY(!room.source().isValid());
    QCOMPARE(room.timeline().positionMs, qint64(0));
    QCOMPARE(room.timeline().revision, quint64(0));

    createRoom(&room);
    QCOMPARE(room.leave(QStringLiteral("host")).error,
             WatchParty::RoomError::HostMustEndRoom);
    QVERIFY(room.active());
    QCOMPARE(room.endRoom(QStringLiteral("host")).error,
             WatchParty::RoomError::None);
    QVERIFY(!room.active());
}

void tst_watchparty_room::host_reconnect_within_grace_keeps_authority()
{
    WatchParty::RoomController room;
    createRoom(&room);
    QCOMPARE(room.join(signedIn(QStringLiteral("member"), QStringLiteral("Member"))).error,
             WatchParty::RoomError::None);

    QCOMPARE(room.disconnect(QStringLiteral("host"), 1'000, 500).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.hostReconnectDeadlineMs(), qint64(1'500));
    QVERIFY(!room.participant(QStringLiteral("host"))->connected);

    QCOMPARE(room.reconnect(QStringLiteral("host"), 1'499).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.hostParticipantId(), QStringLiteral("host"));
    QCOMPARE(room.hostReconnectDeadlineMs(), qint64(-1));
    QVERIFY(room.participant(QStringLiteral("host"))->connected);

    QCOMPARE(room.advanceTime(2'000).error, WatchParty::RoomError::None);
    QCOMPARE(room.hostParticipantId(), QStringLiteral("host"));
}

void tst_watchparty_room::host_transfer_uses_earliest_connected_signed_in_participant()
{
    WatchParty::RoomController room;
    createRoom(&room);
    QCOMPARE(room.join(signedIn(QStringLiteral("first"), QStringLiteral("First"))).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.join(guest(QStringLiteral("guest"), QStringLiteral("Guest"))).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.join(signedIn(QStringLiteral("second"), QStringLiteral("Second"))).error,
             WatchParty::RoomError::None);

    QCOMPARE(room.disconnect(QStringLiteral("first"), 900, 1'000).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.disconnect(QStringLiteral("host"), 1'000, 500).error,
             WatchParty::RoomError::None);

    QCOMPARE(room.advanceTime(1'499).error, WatchParty::RoomError::None);
    QCOMPARE(room.hostParticipantId(), QStringLiteral("host"));

    QCOMPARE(room.advanceTime(1'500).error, WatchParty::RoomError::None);
    QVERIFY(room.active());
    QCOMPARE(room.hostParticipantId(), QStringLiteral("second"));
    QVERIFY(!room.participant(QStringLiteral("host")));

    const WatchParty::ParticipantState* successor =
        room.participant(QStringLiteral("second"));
    QVERIFY(successor);
    QVERIFY(successor->host);
    QCOMPARE(successor->identity.kind, WatchParty::IdentityKind::SignedIn);

    const WatchParty::ParticipantState* guestState =
        room.participant(QStringLiteral("guest"));
    QVERIFY(guestState);
    QVERIFY(!guestState->host);
}

void tst_watchparty_room::guest_never_inherits_and_room_ends_without_eligible_successor()
{
    WatchParty::RoomController room;
    createRoom(&room);
    QCOMPARE(room.join(guest(QStringLiteral("guest"), QStringLiteral("Guest"))).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.addChat(QStringLiteral("guest"), QStringLiteral("still here")).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.addReaction(QStringLiteral("guest"), QStringLiteral("clap")).error,
             WatchParty::RoomError::None);

    QCOMPARE(room.disconnect(QStringLiteral("host"), 10'000, 250).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.advanceTime(10'250).error, WatchParty::RoomError::None);

    QVERIFY(!room.active());
    QVERIFY(room.roomId().isEmpty());
    QVERIFY(room.hostParticipantId().isEmpty());
    QVERIFY(room.participants().isEmpty());
    QVERIFY(room.chatEvents().isEmpty());
    QVERIFY(room.reactionEvents().isEmpty());
    QVERIFY(!room.source().isValid());
}

void tst_watchparty_room::protocol_round_trips_room_and_ephemeral_events()
{
    WatchParty::RoomController room;
    createRoom(&room);
    QCOMPARE(room.join(guest(QStringLiteral("guest"), QStringLiteral("Guest"))).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.setReady(QStringLiteral("guest"), true).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.setSyncStatus(QStringLiteral("guest"),
                                WatchParty::SyncStatus::InSync).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.applyTimelineCommand(QStringLiteral("host"),
                                       WatchParty::TimelineCommand::playAt(33'000)).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.addChat(QStringLiteral("guest"), QStringLiteral(" hello ")).error,
             WatchParty::RoomError::None);
    QCOMPARE(room.addReaction(QStringLiteral("host"), QStringLiteral(" fire ")).error,
             WatchParty::RoomError::None);

    const QJsonObject payload = WatchParty::roomSnapshotToJson(room.snapshot());

    WatchParty::RoomSnapshot parsedSnapshot;
    QString snapshotError;
    QVERIFY2(WatchParty::roomSnapshotFromJson(payload,
                                              &parsedSnapshot,
                                              &snapshotError),
             qPrintable(snapshotError));
    QCOMPARE(parsedSnapshot.roomId, room.roomId());
    QCOMPARE(parsedSnapshot.hostParticipantId, room.hostParticipantId());
    QCOMPARE(parsedSnapshot.controlMode, room.controlMode());
    QCOMPARE(parsedSnapshot.timeline.playing, true);
    QCOMPARE(parsedSnapshot.timeline.positionMs, qint64(33'000));
    QCOMPARE(parsedSnapshot.participants.size(), 2);
    QCOMPARE(parsedSnapshot.source.kind, WatchParty::SourceKind::Torrent);
    QCOMPARE(parsedSnapshot.source.infoHash,
             QStringLiteral("0123456789abcdef0123456789abcdef01234567"));
    QCOMPARE(parsedSnapshot.source.fileIdx, 7);

    WatchParty::ChatEvent parsedChat;
    QString chatError;
    QVERIFY2(WatchParty::chatEventFromJson(
                 WatchParty::chatEventToJson(room.chatEvents().first()),
                 &parsedChat,
                 &chatError),
             qPrintable(chatError));
    QCOMPARE(parsedChat.participantId, QStringLiteral("guest"));
    QCOMPARE(parsedChat.message, QStringLiteral("hello"));

    WatchParty::ReactionEvent parsedReaction;
    QString reactionError;
    QVERIFY2(WatchParty::reactionEventFromJson(
                 WatchParty::reactionEventToJson(room.reactionEvents().first()),
                 &parsedReaction,
                 &reactionError),
             qPrintable(reactionError));
    QCOMPARE(parsedReaction.participantId, QStringLiteral("host"));
    QCOMPARE(parsedReaction.reaction, QStringLiteral("fire"));

    const QJsonObject commandJson =
        WatchParty::timelineCommandToJson(WatchParty::TimelineCommand::seek(9'876));
    WatchParty::TimelineCommand parsedCommand;
    QString commandError;
    QVERIFY2(WatchParty::timelineCommandFromJson(commandJson,
                                                 &parsedCommand,
                                                 &commandError),
             qPrintable(commandError));
    QCOMPARE(parsedCommand.type, WatchParty::TimelineCommandType::Seek);
    QVERIFY(parsedCommand.hasPosition);
    QCOMPARE(parsedCommand.positionMs, qint64(9'876));

    WatchParty::ProtocolMessage outbound;
    outbound.type = WatchParty::MessageType::RoomSnapshot;
    outbound.roomId = room.roomId();
    outbound.senderId = QStringLiteral("authority");
    outbound.sequence = 7;
    outbound.payload = payload;

    const QByteArray encoded = WatchParty::encodeMessage(outbound);
    const WatchParty::DecodeResult decoded = WatchParty::decodeMessage(encoded);
    QVERIFY2(decoded.ok, qPrintable(decoded.error));
    QCOMPARE(decoded.message.version, WatchParty::kProtocolVersion);
    QCOMPARE(decoded.message.type, WatchParty::MessageType::RoomSnapshot);
    QCOMPARE(decoded.message.roomId, outbound.roomId);
    QCOMPARE(decoded.message.senderId, outbound.senderId);
    QCOMPARE(decoded.message.sequence, qint64(7));
    QCOMPARE(decoded.message.payload, payload);
}

void tst_watchparty_room::protocol_rejects_version_mismatch_and_invalid_guest_host()
{
    WatchParty::ProtocolMessage message;
    message.type = WatchParty::MessageType::JoinRoom;
    message.roomId = QStringLiteral("room-a");
    message.senderId = QStringLiteral("guest");
    message.sequence = 1;

    QJsonDocument wrongVersionDocument =
        QJsonDocument::fromJson(WatchParty::encodeMessage(message));
    QJsonObject wrongVersionObject = wrongVersionDocument.object();
    wrongVersionObject.insert(QStringLiteral("version"),
                              WatchParty::kProtocolVersion + 1);

    const WatchParty::DecodeResult wrongVersion =
        WatchParty::decodeMessage(
            QJsonDocument(wrongVersionObject).toJson(QJsonDocument::Compact));
    QVERIFY(!wrongVersion.ok);
    QVERIFY(wrongVersion.error.contains(
        QStringLiteral("unsupported protocol version")));

    WatchParty::RoomController room;
    createRoom(&room);
    QJsonObject snapshot = WatchParty::roomSnapshotToJson(room.snapshot());
    QJsonArray participants = snapshot.value(QStringLiteral("participants")).toArray();
    QJsonObject host = participants.first().toObject();
    host.insert(QStringLiteral("identityKind"), QStringLiteral("guest"));
    participants[0] = host;
    snapshot.insert(QStringLiteral("participants"), participants);

    WatchParty::RoomSnapshot parsed;
    QString error;
    QVERIFY(!WatchParty::roomSnapshotFromJson(snapshot, &parsed, &error));
    QVERIFY(error.contains(QStringLiteral("host must be signed in")));
}

void tst_watchparty_room::fake_transport_records_and_injects_synchronously()
{
    WatchParty::FakeTransport transport;

    WatchParty::ProtocolMessage outbound;
    outbound.type = WatchParty::MessageType::JoinRoom;
    outbound.roomId = QStringLiteral("room-a");
    outbound.senderId = QStringLiteral("guest");
    outbound.sequence = 1;

    QVERIFY(transport.send(outbound));
    QCOMPARE(transport.sentMessages().size(), 1);
    QCOMPARE(transport.sentMessages().first().type,
             WatchParty::MessageType::JoinRoom);

    int received = 0;
    WatchParty::ProtocolMessage inbound;
    transport.setReceiveHandler(
        [&](const WatchParty::ProtocolMessage& message) {
            ++received;
            inbound = message;
        });

    WatchParty::ProtocolMessage snapshot;
    snapshot.type = WatchParty::MessageType::RoomSnapshot;
    snapshot.roomId = QStringLiteral("room-a");
    snapshot.senderId = QStringLiteral("authority");
    snapshot.sequence = 2;

    QVERIFY(transport.injectIncoming(snapshot));
    QCOMPARE(received, 1);
    QCOMPARE(inbound.sequence, qint64(2));

    transport.close();
    QVERIFY(!transport.send(outbound));
    QVERIFY(!transport.injectIncoming(snapshot));
    QCOMPARE(received, 1);

    transport.reopen();
    QVERIFY(transport.send(outbound));
    QCOMPARE(transport.takeSentMessages().size(), 2);
    QVERIFY(transport.sentMessages().isEmpty());
}

QTEST_GUILESS_MAIN(tst_watchparty_room)
#include "tst_watchparty_room.moc"
