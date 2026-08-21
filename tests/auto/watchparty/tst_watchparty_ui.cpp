// Watch Party Slice 6 — deterministic QML-facing lifecycle/readiness controller tests.
//
// No live WebSocket, account backend, QML engine, or player is constructed. FakeTransport and
// FakeAccountBridge drive the exact seams exposed by Slices 4-5; PlayerSyncController proves that
// room membership cannot apply authoritative playback until Player 1 confirms the exact room
// source is locally ready.

#include "watchparty/FakeWatchPartyTransport.h"
#include "watchparty/WatchPartyIdentity.h"
#include "watchparty/WatchPartyPlayerSync.h"
#include "watchparty/WatchPartyProtocol.h"
#include "watchparty/WatchPartyUiController.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include <optional>

namespace WatchParty = Colosseum::WatchParty;

namespace {

WatchParty::SourceDescriptor torrentSource()
{
    return WatchParty::SourceDescriptor::torrent(
        QStringLiteral("0123456789abcdef0123456789abcdef01234567"),
        7);
}

QVariantMap eligibleTorrentCandidate()
{
    return {
        {QStringLiteral("infoHash"),
         QStringLiteral("0123456789abcdef0123456789abcdef01234567")},
        {QStringLiteral("fileIdx"), 7},
        {QStringLiteral("addonId"), QStringLiteral("com.example.torrent")}
    };
}

QVariantMap unsupportedDirectCandidate()
{
    return {
        {QStringLiteral("infoHash"),
         QStringLiteral("0123456789abcdef0123456789abcdef01234567")},
        {QStringLiteral("fileIdx"), 7},
        {QStringLiteral("url"), QStringLiteral("https://cdn.example.test/video.mkv")},
        {QStringLiteral("addonId"), QStringLiteral("com.example.direct")}
    };
}

QVariantMap forgedEligibleWrapper()
{
    return {
        {QStringLiteral("eligible"), true},
        {QStringLiteral("eligibility"), QStringLiteral("torrent")},
        {QStringLiteral("descriptor"),
         QVariantMap{
             {QStringLiteral("kind"), QStringLiteral("torrent")},
             {QStringLiteral("infoHash"),
              QStringLiteral("0123456789abcdef0123456789abcdef01234567")},
             {QStringLiteral("fileIdx"), 7}}}
    };
}

WatchParty::ParticipantState participant(
    const QString& id,
    const QString& displayName,
    WatchParty::IdentityKind kind,
    quint64 joinOrder,
    bool host)
{
    WatchParty::ParticipantState state;
    state.identity = WatchParty::ParticipantIdentity{id, displayName, kind};
    state.joinOrder = joinOrder;
    state.host = host;
    state.connected = true;
    state.ready = host;
    state.syncStatus =
        host ? WatchParty::SyncStatus::InSync
             : WatchParty::SyncStatus::Unknown;
    return state;
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
    const QString& roomId,
    const QString& participantId,
    qint64 sequence)
{
    WatchParty::SessionEstablished session;
    session.participantId = participantId;
    session.reconnectToken = QStringLiteral("private-reconnect-token");
    return serverMessage(
        WatchParty::MessageType::SessionEstablished,
        roomId,
        WatchParty::sessionEstablishedToJson(session),
        sequence);
}

WatchParty::ProtocolMessage snapshotMessage(
    const QString& roomId,
    const QString& localParticipantId,
    bool localIsHost,
    WatchParty::IdentityKind localKind,
    qint64 sequence)
{
    WatchParty::RoomSnapshot snapshot;
    snapshot.roomId = roomId;
    snapshot.source = torrentSource();
    snapshot.controlMode = WatchParty::ControlMode::HostControl;
    snapshot.timeline.playing = true;
    snapshot.timeline.positionMs = 15'000;
    snapshot.timeline.revision = 4;

    if (localIsHost) {
        snapshot.hostParticipantId = localParticipantId;
        snapshot.participants.append(
            participant(
                localParticipantId,
                QStringLiteral("Host"),
                localKind,
                1,
                true));
    } else {
        snapshot.hostParticipantId = QStringLiteral("participant-host");
        snapshot.participants.append(
            participant(
                QStringLiteral("participant-host"),
                QStringLiteral("Host"),
                WatchParty::IdentityKind::SignedIn,
                1,
                true));
        snapshot.participants.append(
            participant(
                localParticipantId,
                QStringLiteral("Guest"),
                localKind,
                2,
                false));
    }

    return serverMessage(
        WatchParty::MessageType::RoomSnapshot,
        roomId,
        WatchParty::roomSnapshotToJson(snapshot),
        sequence);
}

class FakeAccountBridge final : public WatchParty::IWatchPartyAccountBridge
{
public:
    std::optional<WatchParty::SignedInAccountIdentity>
    currentSignedInIdentity() const override
    {
        return identity;
    }

    bool exactUsernameInviteAvailable() const override { return inviteAvailable; }

    void inviteExactUsername(
        const QString& roomId,
        const QString& exactUsername,
        InviteCompletion completion) override
    {
        lastInviteRoomId = roomId;
        lastInviteUsername = exactUsername;
        ++inviteCount;
        if (completion) {
            completion(
                WatchParty::InviteDeliveryResult{
                    WatchParty::InviteDeliveryStatus::AcceptedForDelivery,
                    {}});
        }
    }

    void signIn()
    {
        identity = WatchParty::SignedInAccountIdentity{
            QStringLiteral("SignedInHost"),
            QByteArrayLiteral("private-account-bearer")};
    }

    std::optional<WatchParty::SignedInAccountIdentity> identity;
    bool inviteAvailable = true;
    int inviteCount = 0;
    QString lastInviteRoomId;
    QString lastInviteUsername;
};

void establishRoom(
    WatchParty::FakeTransport* transport,
    const QString& participantId,
    bool localIsHost,
    WatchParty::IdentityKind localKind)
{
    QVERIFY(transport);
    QVERIFY(
        transport->injectIncoming(
            sessionEstablished(
                QStringLiteral("room-ui-test"),
                participantId,
                10)));
    QVERIFY(
        transport->injectIncoming(
            snapshotMessage(
                QStringLiteral("room-ui-test"),
                participantId,
                localIsHost,
                localKind,
                11)));
}

} // namespace

class tst_watchparty_ui final : public QObject
{
    Q_OBJECT

private slots:
    void unsupported_direct_source_cannot_start_room();
    void forged_qml_eligibility_cannot_start_room();
    void eligible_torrent_host_create_uses_existing_identity_transport_seam();
    void accountless_guest_join_requires_only_room_and_temporary_name();
    void room_membership_does_not_activate_player_sync_before_exact_source_ready();
    void source_becoming_unready_deactivates_player_sync();
    void exact_username_invite_remains_authoritative_host_only();
    void signed_in_session_account_identity_changed_closes_authenticated_session();
    void guest_session_account_identity_changed_preserves_guest_session();
};

void tst_watchparty_ui::unsupported_direct_source_cannot_start_room()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    FakeAccountBridge account;
    account.signIn();
    WatchParty::UiController ui(&transport, &sync, &account);

    QVERIFY(ui.configureServiceUrl(QUrl(QStringLiteral("wss://party.example.test/v1"))));

    // Non-vacuous negative control: the same signed-in/configured controller accepts the
    // eligible torrent in the next test. Weakening the source gate would open the transport
    // and emit CreateRoom here.
    QVERIFY(!ui.startParty(unsupportedDirectCandidate()));
    QCOMPARE(ui.errorCategory(), QStringLiteral("unsupportedSource"));
    QCOMPARE(transport.openCount(), 0);
    QVERIFY(transport.sentMessages().isEmpty());
}

void tst_watchparty_ui::forged_qml_eligibility_cannot_start_room()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    FakeAccountBridge account;
    account.signIn();
    WatchParty::UiController ui(&transport, &sync, &account);

    QVERIFY(ui.configureServiceUrl(QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(!ui.startParty(forgedEligibleWrapper()));
    QCOMPARE(ui.errorCategory(), QStringLiteral("unsupportedSource"));
    QCOMPARE(transport.openCount(), 0);
    QVERIFY(transport.sentMessages().isEmpty());
}

void tst_watchparty_ui::
eligible_torrent_host_create_uses_existing_identity_transport_seam()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    FakeAccountBridge account;
    account.signIn();
    WatchParty::UiController ui(&transport, &sync, &account);

    QVERIFY(ui.configureServiceUrl(QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(ui.startParty(eligibleTorrentCandidate()));

    QCOMPARE(transport.openCount(), 1);
    QCOMPARE(
        transport.lastOpenOptions().bearerToken,
        QByteArrayLiteral("private-account-bearer"));
    QCOMPARE(transport.sentMessages().size(), 1);
    QCOMPARE(
        transport.sentMessages().first().type,
        WatchParty::MessageType::CreateRoom);
    QVERIFY(
        !WatchParty::encodeMessage(transport.sentMessages().first())
             .contains(QByteArrayLiteral("private-account-bearer")));
    QCOMPARE(ui.phase(), QStringLiteral("establishing"));
}

void tst_watchparty_ui::
accountless_guest_join_requires_only_room_and_temporary_name()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);

    QVERIFY(ui.configureServiceUrl(QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(
        ui.joinRoom(
            QStringLiteral("room-ui-test"),
            QStringLiteral("Temporary Guest")));

    QCOMPARE(transport.openCount(), 1);
    QVERIFY(transport.lastOpenOptions().bearerToken.isEmpty());
    QCOMPARE(transport.sentMessages().size(), 1);
    const WatchParty::ProtocolMessage join =
        transport.sentMessages().first();
    QCOMPARE(join.type, WatchParty::MessageType::JoinRoom);
    QCOMPARE(join.roomId, QStringLiteral("room-ui-test"));
    QCOMPARE(
        join.payload.value(QStringLiteral("identityKind")).toString(),
        QStringLiteral("guest"));
    QCOMPARE(
        join.payload.value(QStringLiteral("displayName")).toString(),
        QStringLiteral("Temporary Guest"));
}

void tst_watchparty_ui::
room_membership_does_not_activate_player_sync_before_exact_source_ready()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);
    QSignalSpy roomActivated(&ui, &WatchParty::UiController::roomActivated);

    QVERIFY(ui.configureServiceUrl(QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(
        ui.joinRoom(
            QStringLiteral("room-ui-test"),
            QStringLiteral("Temporary Guest")));

    establishRoom(
        &transport,
        QStringLiteral("participant-guest"),
        false,
        WatchParty::IdentityKind::Guest);

    QVERIFY(ui.inRoom());
    QCOMPARE(ui.roomId(), QStringLiteral("room-ui-test"));
    QCOMPARE(roomActivated.count(), 1);
    QVERIFY(!ui.localSourceReady());
    QVERIFY(!sync.active());

    // This is the Slice 6 safety boundary: membership alone must never let an
    // authoritative timeline touch unrelated Player 1 media.
    ui.setLocalSourceReady(true);
    QVERIFY(ui.localSourceReady());
    QVERIFY(sync.active());
    QVERIFY(!sync.canControlTimeline());
}

void tst_watchparty_ui::source_becoming_unready_deactivates_player_sync()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);

    QVERIFY(ui.configureServiceUrl(QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(
        ui.joinRoom(
            QStringLiteral("room-ui-test"),
            QStringLiteral("Temporary Guest")));
    establishRoom(
        &transport,
        QStringLiteral("participant-guest"),
        false,
        WatchParty::IdentityKind::Guest);

    ui.setLocalSourceReady(true);
    QVERIFY(sync.active());

    ui.setLocalSourceReady(false);
    QVERIFY(!ui.localSourceReady());
    QVERIFY(!sync.active());
}

void tst_watchparty_ui::
exact_username_invite_remains_authoritative_host_only()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    FakeAccountBridge account;
    account.signIn();
    WatchParty::UiController ui(&transport, &sync, &account);

    QVERIFY(ui.configureServiceUrl(QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(ui.startParty(eligibleTorrentCandidate()));
    establishRoom(
        &transport,
        QStringLiteral("participant-host"),
        true,
        WatchParty::IdentityKind::SignedIn);

    QVERIFY(ui.localIsHost());
    QVERIFY(ui.canInvite());

    const QString exact = QStringLiteral("MiXeD_Case_User");
    QVERIFY(ui.inviteExactUsername(exact));
    QCOMPARE(account.inviteCount, 1);
    QCOMPARE(account.lastInviteRoomId, QStringLiteral("room-ui-test"));
    QCOMPARE(account.lastInviteUsername, exact);

    // Completion is deliberately queued back onto UiController's thread.
    QTRY_VERIFY(!ui.inviteBusy());
}

void tst_watchparty_ui::
signed_in_session_account_identity_changed_closes_authenticated_session()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    FakeAccountBridge account;
    account.signIn();
    WatchParty::UiController ui(&transport, &sync, &account);
    QSignalSpy identityChanged(&ui, &WatchParty::UiController::identityChanged);

    QVERIFY(ui.configureServiceUrl(QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(ui.startParty(eligibleTorrentCandidate()));
    establishRoom(
        &transport,
        QStringLiteral("participant-host"),
        true,
        WatchParty::IdentityKind::SignedIn);

    QVERIFY(ui.inRoom());
    QCOMPARE(ui.phase(), QStringLiteral("active"));

    ui.handleAccountIdentityChanged();

    // The signed-in session must not survive an account identity change: the
    // room is torn down, playback sync is deactivated, and the transport the
    // previous identity opened is closed so a new identity can never reuse it.
    QVERIFY(!ui.inRoom());
    QCOMPARE(ui.phase(), QStringLiteral("idle"));
    QVERIFY(!sync.active());
    QCOMPARE(transport.state(), WatchParty::TransportState::Closed);
    QVERIFY(identityChanged.count() >= 1);
}

void tst_watchparty_ui::
guest_session_account_identity_changed_preserves_guest_session()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::PlayerSyncController sync;
    WatchParty::UiController ui(&transport, &sync, nullptr);
    QSignalSpy identityChanged(&ui, &WatchParty::UiController::identityChanged);

    QVERIFY(ui.configureServiceUrl(QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(
        ui.joinRoom(
            QStringLiteral("room-ui-test"),
            QStringLiteral("Temporary Guest")));
    establishRoom(
        &transport,
        QStringLiteral("participant-guest"),
        false,
        WatchParty::IdentityKind::Guest);

    QVERIFY(ui.inRoom());
    const QString phaseBefore = ui.phase();

    ui.handleAccountIdentityChanged();

    // A guest session is not account-bound: an account identity change must
    // leave it untouched, even though identityChanged is still emitted.
    QVERIFY(ui.inRoom());
    QCOMPARE(ui.phase(), phaseBefore);
    QCOMPARE(transport.state(), WatchParty::TransportState::Connected);
    QVERIFY(identityChanged.count() >= 1);
}

QTEST_GUILESS_MAIN(tst_watchparty_ui)
#include "tst_watchparty_ui.moc"
