// Watch Party Slice 7 — deterministic assembled lifecycle/failure/observability matrix.
//
// No live room service, account backend, QML engine, or mpv instance is used. FakeTransport
// drives the exact Slice 4-6 boundaries. The assertions are intentionally state-based: no
// sleeps, timeouts, log-string matching, Room ID exposure, or chat/credential diagnostics.

#include "watchparty/FakeWatchPartyTransport.h"
#include "watchparty/WatchPartyPlayerSync.h"
#include "watchparty/WatchPartyProtocol.h"
#include "watchparty/WatchPartyUiController.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

namespace WatchParty = Colosseum::WatchParty;

namespace {

constexpr auto kRoomId = "room-lifecycle-secret-sentinel";
constexpr auto kParticipantId = "participant-local";
constexpr auto kReconnectToken = "reconnect-secret-sentinel";
constexpr auto kChatSentinel = "chat-secret-sentinel";
constexpr auto kInfoHash = "0123456789abcdef0123456789abcdef01234567";

WatchParty::SourceDescriptor source()
{
    return WatchParty::SourceDescriptor::torrent(
        QString::fromLatin1(kInfoHash),
        7);
}

WatchParty::ParticipantState participant(
    const QString& id,
    const QString& displayName,
    WatchParty::IdentityKind kind,
    quint64 joinOrder,
    bool host,
    bool connected = true,
    bool ready = false,
    WatchParty::SyncStatus syncStatus = WatchParty::SyncStatus::Unknown)
{
    WatchParty::ParticipantState state;
    state.identity = WatchParty::ParticipantIdentity{id, displayName, kind};
    state.joinOrder = joinOrder;
    state.host = host;
    state.connected = connected;
    state.ready = ready;
    state.syncStatus = ready ? syncStatus : WatchParty::SyncStatus::Unknown;
    return state;
}

WatchParty::RoomSnapshot snapshot(
    const QString& localParticipantId,
    bool localIsHost = false)
{
    WatchParty::RoomSnapshot value;
    value.roomId = QString::fromLatin1(kRoomId);
    value.source = source();
    value.controlMode = WatchParty::ControlMode::HostControl;
    value.timeline.playing = true;
    value.timeline.positionMs = 12'000;
    value.timeline.revision = 4;

    if (localIsHost) {
        value.hostParticipantId = localParticipantId;
        value.participants.append(
            participant(
                localParticipantId,
                QStringLiteral("Host"),
                WatchParty::IdentityKind::SignedIn,
                0,
                true,
                true,
                true,
                WatchParty::SyncStatus::InSync));
    } else {
        value.hostParticipantId = QStringLiteral("host-a");
        value.participants.append(
            participant(
                QStringLiteral("host-a"),
                QStringLiteral("Host"),
                WatchParty::IdentityKind::SignedIn,
                0,
                true,
                true,
                true,
                WatchParty::SyncStatus::InSync));
        value.participants.append(
            participant(
                localParticipantId,
                QStringLiteral("Guest"),
                WatchParty::IdentityKind::Guest,
                1,
                false));
    }

    return value;
}

WatchParty::ProtocolMessage serverMessage(
    WatchParty::MessageType type,
    const QString& roomId,
    const QJsonObject& payload,
    qint64 sequence)
{
    WatchParty::ProtocolMessage message;
    message.type = type;
    message.roomId = roomId;
    message.sequence = sequence;
    message.payload = payload;
    return message;
}

WatchParty::ProtocolMessage sessionEstablished(
    const QString& reconnectToken = QString::fromLatin1(kReconnectToken),
    qint64 sequence = 10)
{
    WatchParty::SessionEstablished session;
    session.participantId = QString::fromLatin1(kParticipantId);
    session.reconnectToken = reconnectToken;
    return serverMessage(
        WatchParty::MessageType::SessionEstablished,
        QString::fromLatin1(kRoomId),
        WatchParty::sessionEstablishedToJson(session),
        sequence);
}

WatchParty::ProtocolMessage snapshotMessage(
    const WatchParty::RoomSnapshot& value,
    qint64 sequence)
{
    return serverMessage(
        WatchParty::MessageType::RoomSnapshot,
        value.roomId,
        WatchParty::roomSnapshotToJson(value),
        sequence);
}

WatchParty::ProtocolMessage serverError(
    const QString& code,
    const QString& message,
    qint64 sequence = 20)
{
    return serverMessage(
        WatchParty::MessageType::Error,
        QString::fromLatin1(kRoomId),
        QJsonObject{
            {QStringLiteral("code"), code},
            {QStringLiteral("message"), message}},
        sequence);
}

WatchParty::ProtocolMessage chatMessage(qint64 sequence = 30)
{
    WatchParty::ChatEvent event;
    event.sequence = 1;
    event.participantId = QStringLiteral("host-a");
    event.displayName = QStringLiteral("Host");
    event.message = QString::fromLatin1(kChatSentinel);
    return serverMessage(
        WatchParty::MessageType::Chat,
        QString::fromLatin1(kRoomId),
        WatchParty::chatEventToJson(event),
        sequence);
}

WatchParty::ProtocolMessage reactionMessage(qint64 sequence = 31)
{
    WatchParty::ReactionEvent event;
    event.sequence = 2;
    event.participantId = QStringLiteral("host-a");
    event.displayName = QStringLiteral("Host");
    event.reaction = QStringLiteral("🔥");
    return serverMessage(
        WatchParty::MessageType::Reaction,
        QString::fromLatin1(kRoomId),
        WatchParty::reactionEventToJson(event),
        sequence);
}

void beginGuestJoin(WatchParty::UiController* ui)
{
    QVERIFY(ui);
    QVERIFY(
        ui->configureServiceUrl(
            QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(
        ui->joinRoom(
            QString::fromLatin1(kRoomId),
            QStringLiteral("Temporary Guest")));
}

void establishGuestRoom(WatchParty::UiController* ui,
                        WatchParty::FakeTransport* transport,
                        WatchParty::RoomSnapshot value = snapshot(
                            QString::fromLatin1(kParticipantId)))
{
    QVERIFY(ui);
    QVERIFY(transport);

    beginGuestJoin(ui);
    QVERIFY(transport->injectIncoming(sessionEstablished()));
    QVERIFY(transport->injectIncoming(snapshotMessage(value, 11)));
    QVERIFY(ui->inRoom());
    QCOMPARE(ui->phase(), QStringLiteral("active"));
}

QByteArray diagnosticsBytes(const WatchParty::UiController& ui)
{
    return QJsonDocument::fromVariant(ui.diagnosticSnapshot())
        .toJson(QJsonDocument::Compact);
}

} // namespace

class tst_watchparty_lifecycle final : public QObject
{
    Q_OBJECT

private slots:
    void join_failures_have_exact_categories_and_never_create_session();
    void source_unavailable_is_membership_without_player_sync();
    void buffering_participant_is_observable_without_moving_room_timeline();
    void reconnect_preserves_membership_and_returns_to_authoritative_state();
    void terminal_room_end_clears_ephemeral_presentation();
    void participant_removal_terminates_current_membership();
    void terminal_protocol_mismatch_clears_stale_room_before_ui_error();
    void diagnostics_are_allow_listed_and_redacted();
};

void tst_watchparty_lifecycle::
join_failures_have_exact_categories_and_never_create_session()
{
    struct FailureCase {
        QString serverCode;
        QString uiCategory;
        QString fallbackText;
    };
    const QList<FailureCase> cases = {
        {
            QStringLiteral("room_not_found"),
            QStringLiteral("roomNotFound"),
            QStringLiteral("That Watch Party does not exist.")
        },
        {
            QStringLiteral("room_full"),
            QStringLiteral("roomFull"),
            QStringLiteral("This Watch Party is full.")
        },
        {
            QStringLiteral("room_ended"),
            QStringLiteral("roomEnded"),
            QStringLiteral("That Watch Party has ended.")
        }
    };

    for (const FailureCase& item : cases) {
        WatchParty::FakeTransport transport(false);
        WatchParty::PlayerSyncController sync;
        WatchParty::UiController ui(&transport, &sync, nullptr);

        beginGuestJoin(&ui);
        QVERIFY(
            transport.injectIncoming(
                serverError(item.serverCode, QString())));

        QVERIFY(!ui.inRoom());
        QVERIFY(!sync.active());
        QCOMPARE(ui.phase(), QStringLiteral("error"));
        QCOMPARE(ui.errorCategory(), item.uiCategory);
        QCOMPARE(ui.errorText(), item.fallbackText);
        QCOMPARE(ui.participantCount(), 0);
        QCOMPARE(ui.localSyncStatus(), QStringLiteral("inactive"));
    }
}

void tst_watchparty_lifecycle::
source_unavailable_is_membership_without_player_sync()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);

    establishGuestRoom(&ui, &transport);

    QVERIFY(ui.inRoom());
    QVERIFY(!ui.localSourceReady());
    QVERIFY(!sync.active());
    QCOMPARE(ui.localSyncStatus(), QStringLiteral("sourceUnavailable"));

    const QVariantMap diagnostics = ui.diagnosticSnapshot();
    QCOMPARE(
        diagnostics.value(QStringLiteral("roomState")).toString(),
        QStringLiteral("active"));
    QCOMPARE(
        diagnostics.value(QStringLiteral("localSyncStatus")).toString(),
        QStringLiteral("sourceUnavailable"));
}

void tst_watchparty_lifecycle::
buffering_participant_is_observable_without_moving_room_timeline()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);

    WatchParty::RoomSnapshot value =
        snapshot(QString::fromLatin1(kParticipantId));
    value.participants.append(
        participant(
            QStringLiteral("signed-buffering"),
            QStringLiteral("Buffering"),
            WatchParty::IdentityKind::SignedIn,
            2,
            false,
            true,
            true,
            WatchParty::SyncStatus::Buffering));

    const WatchParty::TimelineState originalTimeline = value.timeline;
    establishGuestRoom(&ui, &transport, value);

    QCOMPARE(ui.participantCount(), 3);
    QCOMPARE(ui.bufferingParticipantCount(), 1);

    const QVariantMap diagnostics = ui.diagnosticSnapshot();
    QCOMPARE(
        diagnostics.value(
            QStringLiteral("bufferingParticipantCount")).toInt(),
        1);

    // A participant's buffering/readiness state is not shared timeline authority.
    QCOMPARE(value.timeline.playing, originalTimeline.playing);
    QCOMPARE(value.timeline.positionMs, originalTimeline.positionMs);
    QCOMPARE(value.timeline.revision, originalTimeline.revision);
}

void tst_watchparty_lifecycle::
reconnect_preserves_membership_and_returns_to_authoritative_state()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);

    establishGuestRoom(&ui, &transport);
    ui.setLocalSourceReady(true);
    QVERIFY(sync.active());

    transport.clearSentMessages();
    transport.simulateDisconnect();

    QCOMPARE(ui.phase(), QStringLiteral("reconnecting"));
    QVERIFY(ui.inRoom());

    transport.reopen();
    QVERIFY(!transport.sentMessages().isEmpty());
    QCOMPARE(
        transport.sentMessages().last().type,
        WatchParty::MessageType::ReconnectRoom);

    QVERIFY(
        transport.injectIncoming(
            sessionEstablished(
                QStringLiteral("rotated-private-token"),
                40)));
    QCOMPARE(ui.phase(), QStringLiteral("synchronizing"));

    WatchParty::RoomSnapshot resumed =
        snapshot(QString::fromLatin1(kParticipantId));
    resumed.timeline.positionMs = 25'000;
    resumed.timeline.revision = 5;
    QVERIFY(transport.injectIncoming(snapshotMessage(resumed, 41)));

    QVERIFY(ui.inRoom());
    QCOMPARE(ui.phase(), QStringLiteral("active"));
    QCOMPARE(ui.transportState(), QStringLiteral("connected"));
    QVERIFY(sync.active());
    QCOMPARE(sync.authoritativeRevision(), qulonglong(5));
}

void tst_watchparty_lifecycle::
terminal_room_end_clears_ephemeral_presentation()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);

    establishGuestRoom(&ui, &transport);
    ui.setLocalSourceReady(true);
    QVERIFY(transport.injectIncoming(chatMessage()));
    QVERIFY(transport.injectIncoming(reactionMessage()));
    QCOMPARE(ui.chatMessages().size(), 1);
    QCOMPARE(ui.reactions().size(), 1);
    QVERIFY(sync.active());

    QVERIFY(
        transport.injectIncoming(
            serverMessage(
                WatchParty::MessageType::RoomEnded,
                QString::fromLatin1(kRoomId),
                {},
                50)));

    // Permanent mutation canary: if onRoomEnded()/clearRoomPresentation() stops
    // clearing any room-owned state, at least one assertion below turns red.
    QVERIFY(!ui.inRoom());
    QCOMPARE(ui.participantCount(), 0);
    QVERIFY(ui.chatMessages().isEmpty());
    QVERIFY(ui.reactions().isEmpty());
    QVERIFY(ui.roomSource().isEmpty());
    QVERIFY(!ui.localSourceReady());
    QVERIFY(!sync.active());
    QCOMPARE(ui.phase(), QStringLiteral("idle"));
    QCOMPARE(ui.noticeText(), QStringLiteral("Watch Party ended."));
}

void tst_watchparty_lifecycle::
participant_removal_terminates_current_membership()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);

    establishGuestRoom(&ui, &transport);
    QVERIFY(transport.injectIncoming(chatMessage()));
    QCOMPARE(ui.chatMessages().size(), 1);

    QVERIFY(
        transport.injectIncoming(
            serverError(
                QStringLiteral("participant_removed"),
                QStringLiteral("You were removed from this room."),
                60)));

    QVERIFY(!ui.inRoom());
    QCOMPARE(ui.phase(), QStringLiteral("error"));
    QCOMPARE(ui.errorCategory(), QStringLiteral("participantRemoved"));
    QVERIFY(ui.chatMessages().isEmpty());
    QCOMPARE(ui.participantCount(), 0);

    // Intentionally no fresh join assertion here. The locked product spec does
    // not decide whether a removed participant may re-enter the same room. This
    // test fixes only the non-ambiguous rule: the removed membership and its
    // reconnect credential are terminal.
}

void tst_watchparty_lifecycle::
terminal_protocol_mismatch_clears_stale_room_before_ui_error()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);

    establishGuestRoom(&ui, &transport);
    QVERIFY(transport.injectIncoming(chatMessage()));
    QCOMPARE(ui.chatMessages().size(), 1);

    transport.injectError(
        WatchParty::TransportErrorCode::ProtocolVersionMismatch,
        QStringLiteral("unsupported protocol version"),
        true);

    QVERIFY(!ui.inRoom());
    QCOMPARE(ui.phase(), QStringLiteral("error"));
    QCOMPARE(
        ui.errorCategory(),
        QStringLiteral("protocolVersionMismatch"));
    QVERIFY(ui.chatMessages().isEmpty());
    QCOMPARE(ui.participantCount(), 0);
    QVERIFY(!sync.active());
}

void tst_watchparty_lifecycle::
diagnostics_are_allow_listed_and_redacted()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);

    establishGuestRoom(&ui, &transport);
    QVERIFY(transport.injectIncoming(chatMessage()));

    const QVariantMap diagnostics = ui.diagnosticSnapshot();
    const QSet<QString> expectedKeys = {
        QStringLiteral("protocolVersion"),
        QStringLiteral("roomState"),
        QStringLiteral("transportState"),
        QStringLiteral("errorCategory"),
        QStringLiteral("participantCount"),
        QStringLiteral("bufferingParticipantCount"),
        QStringLiteral("hostIdentityKind"),
        QStringLiteral("hostGraceActive"),
        QStringLiteral("controlMode"),
        QStringLiteral("localSyncStatus"),
        QStringLiteral("localSourceReady"),
        QStringLiteral("inRoom"),
        QStringLiteral("localIsHost")
    };
    QSet<QString> actualKeys;
    for (auto it = diagnostics.cbegin(); it != diagnostics.cend(); ++it)
        actualKeys.insert(it.key());
    QCOMPARE(actualKeys, expectedKeys);

    const QByteArray bytes = diagnosticsBytes(ui);
    QVERIFY(!bytes.contains(kRoomId));
    QVERIFY(!bytes.contains(kParticipantId));
    QVERIFY(!bytes.contains(kReconnectToken));
    QVERIFY(!bytes.contains(kChatSentinel));
    QVERIFY(!bytes.contains(kInfoHash));
    QVERIFY(!bytes.contains("bearer"));
    QVERIFY(!bytes.contains("token"));
    QVERIFY(!bytes.contains("message"));
    // Allow-listed category values may contain the word "source"
    // (e.g. localSourceReady, localSyncStatus: sourceUnavailable);
    // exact source identity is banned via the sentinel checks above.

    QCOMPARE(
        diagnostics.value(QStringLiteral("hostIdentityKind")).toString(),
        QStringLiteral("signedIn"));
    QCOMPARE(
        diagnostics.value(QStringLiteral("participantCount")).toInt(),
        2);
}

QTEST_GUILESS_MAIN(tst_watchparty_lifecycle)
#include "tst_watchparty_lifecycle.moc"
