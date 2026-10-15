#include "watchparty/FakeWatchPartyTransport.h"
#include "watchparty/WatchPartyIdentity.h"
#include "watchparty/WatchPartyProtocol.h"
#include "watchparty/WatchPartyRoomController.h"
#include "watchparty/WatchPartyRoomServiceClient.h"

#include <QJsonObject>
#include <QtTest>

#include <optional>
#include <utility>

namespace WatchParty = Colosseum::WatchParty;

namespace {

WatchParty::SourceDescriptor source()
{
    return WatchParty::SourceDescriptor::torrent(
        QStringLiteral("0123456789abcdef0123456789abcdef01234567"),
        7);
}

WatchParty::ParticipantState participant(
    const QString& id,
    const QString& displayName,
    WatchParty::IdentityKind kind,
    quint64 joinOrder,
    bool host)
{
    WatchParty::ParticipantState state;
    state.identity =
        WatchParty::ParticipantIdentity{id, displayName, kind};
    state.joinOrder = joinOrder;
    state.host = host;
    state.connected = true;
    state.ready = true;
    state.syncStatus = WatchParty::SyncStatus::InSync;
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
    session.reconnectToken =
        QStringLiteral("private-room-reconnect-token");

    return serverMessage(
        WatchParty::MessageType::SessionEstablished,
        roomId,
        WatchParty::sessionEstablishedToJson(session),
        sequence);
}

WatchParty::ProtocolMessage roomSnapshot(
    const QString& roomId,
    const QString& localParticipantId,
    bool localIsHost,
    WatchParty::IdentityKind localKind,
    qint64 sequence)
{
    WatchParty::RoomSnapshot snapshot;
    snapshot.roomId = roomId;
    snapshot.source = source();
    snapshot.controlMode = WatchParty::ControlMode::HostControl;
    snapshot.timeline.playing = true;
    snapshot.timeline.positionMs = 9'000;
    snapshot.timeline.revision = 2;

    if (localIsHost) {
        snapshot.hostParticipantId = localParticipantId;
        snapshot.participants.append(
            participant(
                localParticipantId,
                QStringLiteral("SignedInHost"),
                localKind,
                1,
                true));
    } else {
        snapshot.hostParticipantId = QStringLiteral("host");
        snapshot.participants.append(
            participant(
                QStringLiteral("host"),
                QStringLiteral("Host"),
                WatchParty::IdentityKind::SignedIn,
                1,
                true));
        snapshot.participants.append(
            participant(
                localParticipantId,
                QStringLiteral("Participant"),
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

class FakeAccountBridge final
    : public WatchParty::IWatchPartyAccountBridge
{
public:
    struct InviteCall {
        QString roomId;
        QString exactUsername;
    };

    std::optional<WatchParty::SignedInAccountIdentity>
    currentSignedInIdentity() const override
    {
        return identity;
    }

    void inviteExactUsername(
        const QString& roomId,
        const QString& exactUsername,
        InviteCompletion completion) override
    {
        inviteCalls.append(InviteCall{roomId, exactUsername});
        if (completion)
            completion(nextInviteResult);
    }

    void signIn(
        const QString& username,
        const QByteArray& bearerToken)
    {
        identity =
            WatchParty::SignedInAccountIdentity{
                username,
                bearerToken
            };
    }

    void signOut()
    {
        identity.reset();
    }

    std::optional<WatchParty::SignedInAccountIdentity> identity;
    QList<InviteCall> inviteCalls;
    WatchParty::InviteDeliveryResult nextInviteResult{
        WatchParty::InviteDeliveryStatus::AcceptedForDelivery,
        {}
    };
};

void establishHostSession(
    WatchParty::FakeTransport* transport,
    const QString& roomId,
    const QString& participantId)
{
    QVERIFY(transport);
    QVERIFY(
        transport->injectIncoming(
            sessionEstablished(
                roomId,
                participantId,
                10)));
    QVERIFY(
        transport->injectIncoming(
            roomSnapshot(
                roomId,
                participantId,
                true,
                WatchParty::IdentityKind::SignedIn,
                11)));
}

} // namespace

class tst_watchparty_identity final : public QObject
{
    Q_OBJECT

private slots:
    void signed_in_create_uses_account_bearer_without_username_payload();
    void guest_create_is_rejected_before_any_room_command();
    void signed_in_join_uses_account_identity_without_client_username();
    void accountless_guest_join_uses_no_bearer();
    void sign_out_blocks_new_signed_in_lifecycle_actions();
    void exact_username_invite_is_host_only_and_not_normalized();
    void non_host_cannot_dispatch_account_invite();
    void guest_never_inherits_host_authority();
    void missing_account_binding_keeps_accountless_join_available();
};

void tst_watchparty_identity::
signed_in_create_uses_account_bearer_without_username_payload()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient service(&transport);
    FakeAccountBridge account;
    account.signIn(
        QStringLiteral("Founder"),
        QByteArrayLiteral("account-secret"));
    WatchParty::IdentityCoordinator identity(&service, &account);

    QCOMPARE(
        identity.openSignedInService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))).error,
        WatchParty::IdentityActionError::None);
    QCOMPARE(
        transport.lastOpenOptions().bearerToken,
        QByteArrayLiteral("account-secret"));
    QCOMPARE(identity.signedInUsername(), QStringLiteral("Founder"));

    transport.clearSentMessages();
    QCOMPARE(
        identity.createRoom(source()).error,
        WatchParty::IdentityActionError::None);
    QCOMPARE(transport.sentMessages().size(), 1);

    const WatchParty::ProtocolMessage create =
        transport.sentMessages().first();
    QCOMPARE(create.type, WatchParty::MessageType::CreateRoom);
    QVERIFY(!create.payload.contains(QStringLiteral("username")));
    QVERIFY(!create.payload.contains(QStringLiteral("userId")));
    QVERIFY(
        !WatchParty::encodeMessage(create).contains(
            QByteArrayLiteral("account-secret")));
    QVERIFY(
        !WatchParty::encodeMessage(create).contains(
            QByteArrayLiteral("Founder")));
}

void tst_watchparty_identity::
guest_create_is_rejected_before_any_room_command()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient service(&transport);
    FakeAccountBridge account;
    account.signOut();
    WatchParty::IdentityCoordinator identity(&service, &account);

    QCOMPARE(
        identity.openGuestService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))).error,
        WatchParty::IdentityActionError::None);
    transport.clearSentMessages();

    // Negative control: weakening host eligibility to "any connected client"
    // would make this produce a createRoom protocol command and fail both checks.
    QCOMPARE(
        identity.createRoom(source()).error,
        WatchParty::IdentityActionError::SignedInRequired);
    QVERIFY(transport.sentMessages().isEmpty());
}

void tst_watchparty_identity::
signed_in_join_uses_account_identity_without_client_username()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient service(&transport);
    FakeAccountBridge account;
    account.signIn(
        QStringLiteral("ExactAccountName"),
        QByteArrayLiteral("signed-in-secret"));
    WatchParty::IdentityCoordinator identity(&service, &account);

    QCOMPARE(
        identity.openSignedInService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))).error,
        WatchParty::IdentityActionError::None);
    transport.clearSentMessages();

    QCOMPARE(
        identity.joinSignedIn(QStringLiteral("room-opaque")).error,
        WatchParty::IdentityActionError::None);
    QCOMPARE(transport.sentMessages().size(), 1);

    const WatchParty::ProtocolMessage join =
        transport.sentMessages().first();
    QCOMPARE(join.type, WatchParty::MessageType::JoinRoom);
    QCOMPARE(join.roomId, QStringLiteral("room-opaque"));
    QCOMPARE(
        join.payload.value(QStringLiteral("identityKind")).toString(),
        QStringLiteral("signedIn"));
    QVERIFY(!join.payload.contains(QStringLiteral("username")));
    QVERIFY(!join.payload.contains(QStringLiteral("userId")));
}

void tst_watchparty_identity::
accountless_guest_join_uses_no_bearer()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient service(&transport);
    FakeAccountBridge account;
    account.signOut();
    WatchParty::IdentityCoordinator identity(&service, &account);

    QCOMPARE(
        identity.openGuestService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))).error,
        WatchParty::IdentityActionError::None);
    QVERIFY(transport.lastOpenOptions().bearerToken.isEmpty());
    transport.clearSentMessages();

    QCOMPARE(
        identity.joinGuest(
            QStringLiteral("room-guest"),
            QStringLiteral("Temporary Guest")).error,
        WatchParty::IdentityActionError::None);
    QCOMPARE(transport.sentMessages().size(), 1);

    const WatchParty::ProtocolMessage join =
        transport.sentMessages().first();
    QCOMPARE(join.type, WatchParty::MessageType::JoinRoom);
    QCOMPARE(
        join.payload.value(QStringLiteral("identityKind")).toString(),
        QStringLiteral("guest"));
    QCOMPARE(
        join.payload.value(QStringLiteral("displayName")).toString(),
        QStringLiteral("Temporary Guest"));
    QVERIFY(!join.payload.contains(QStringLiteral("username")));
}

void tst_watchparty_identity::
sign_out_blocks_new_signed_in_lifecycle_actions()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient service(&transport);
    FakeAccountBridge account;
    account.signIn(
        QStringLiteral("AccountA"),
        QByteArrayLiteral("secret-a"));
    WatchParty::IdentityCoordinator identity(&service, &account);

    QCOMPARE(
        identity.openSignedInService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))).error,
        WatchParty::IdentityActionError::None);

    account.signOut();
    transport.clearSentMessages();

    QCOMPARE(
        identity.createRoom(source()).error,
        WatchParty::IdentityActionError::SignedInRequired);
    QVERIFY(transport.sentMessages().isEmpty());
}

void tst_watchparty_identity::
exact_username_invite_is_host_only_and_not_normalized()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient service(&transport);
    FakeAccountBridge account;
    account.signIn(
        QStringLiteral("HostAccount"),
        QByteArrayLiteral("host-secret"));
    WatchParty::IdentityCoordinator identity(&service, &account);

    QCOMPARE(
        identity.openSignedInService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))).error,
        WatchParty::IdentityActionError::None);
    QCOMPARE(
        identity.createRoom(source()).error,
        WatchParty::IdentityActionError::None);

    establishHostSession(
        &transport,
        QStringLiteral("room-host"),
        QStringLiteral("participant-host"));

    WatchParty::InviteDeliveryResult callbackResult;
    bool callbackCalled = false;
    const QString exactUsername =
        QStringLiteral("MiXeD_Case_User");

    QCOMPARE(
        identity.inviteExactUsername(
            exactUsername,
            [&](const WatchParty::InviteDeliveryResult& result) {
                callbackCalled = true;
                callbackResult = result;
            }).error,
        WatchParty::IdentityActionError::None);

    QCOMPARE(account.inviteCalls.size(), 1);
    QCOMPARE(
        account.inviteCalls.first().roomId,
        QStringLiteral("room-host"));
    QCOMPARE(
        account.inviteCalls.first().exactUsername,
        exactUsername);
    QVERIFY(callbackCalled);
    QVERIFY(callbackResult.ok());

    QCOMPARE(
        identity.inviteExactUsername(QStringLiteral("   ")).error,
        WatchParty::IdentityActionError::InvalidInviteTarget);
    QCOMPARE(account.inviteCalls.size(), 1);
}

void tst_watchparty_identity::
non_host_cannot_dispatch_account_invite()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient service(&transport);
    FakeAccountBridge account;
    account.signIn(
        QStringLiteral("ParticipantAccount"),
        QByteArrayLiteral("participant-secret"));
    WatchParty::IdentityCoordinator identity(&service, &account);

    QCOMPARE(
        identity.openSignedInService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))).error,
        WatchParty::IdentityActionError::None);
    QCOMPARE(
        identity.joinSignedIn(QStringLiteral("room-existing")).error,
        WatchParty::IdentityActionError::None);

    QVERIFY(
        transport.injectIncoming(
            sessionEstablished(
                QStringLiteral("room-existing"),
                QStringLiteral("participant-local"),
                20)));
    QVERIFY(
        transport.injectIncoming(
            roomSnapshot(
                QStringLiteral("room-existing"),
                QStringLiteral("participant-local"),
                false,
                WatchParty::IdentityKind::SignedIn,
                21)));

    QCOMPARE(
        identity.inviteExactUsername(
            QStringLiteral("TargetUser")).error,
        WatchParty::IdentityActionError::HostRequired);
    QVERIFY(account.inviteCalls.isEmpty());
}

void tst_watchparty_identity::
guest_never_inherits_host_authority()
{
    WatchParty::RoomController room;

    const WatchParty::ParticipantIdentity host{
        QStringLiteral("host"),
        QStringLiteral("Host"),
        WatchParty::IdentityKind::SignedIn
    };
    const WatchParty::ParticipantIdentity guest{
        QStringLiteral("guest"),
        QStringLiteral("Guest"),
        WatchParty::IdentityKind::Guest
    };
    const WatchParty::ParticipantIdentity successor{
        QStringLiteral("signed-in-successor"),
        QStringLiteral("Successor"),
        WatchParty::IdentityKind::SignedIn
    };

    QCOMPARE(
        room.create(
            QStringLiteral("room"),
            host,
            source()).error,
        WatchParty::RoomError::None);
    QCOMPARE(room.join(guest).error, WatchParty::RoomError::None);
    QCOMPARE(room.join(successor).error, WatchParty::RoomError::None);

    QCOMPARE(
        room.disconnect(
            QStringLiteral("host"),
            1'000,
            250).error,
        WatchParty::RoomError::None);
    QCOMPARE(
        room.advanceTime(1'250).error,
        WatchParty::RoomError::None);

    QCOMPARE(
        room.hostParticipantId(),
        QStringLiteral("signed-in-successor"));
    QVERIFY(!room.participant(QStringLiteral("guest"))->host);
    QCOMPARE(
        room.participant(
            QStringLiteral("signed-in-successor"))->identity.kind,
        WatchParty::IdentityKind::SignedIn);
}

void tst_watchparty_identity::
missing_account_binding_keeps_accountless_join_available()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient service(&transport);
    WatchParty::IdentityCoordinator identity(&service, nullptr);

    QCOMPARE(
        identity.openSignedInService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))).error,
        WatchParty::IdentityActionError::AccountBindingUnavailable);
    QCOMPARE(transport.openCount(), 0);

    QCOMPARE(
        identity.openGuestService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))).error,
        WatchParty::IdentityActionError::None);
    QVERIFY(transport.lastOpenOptions().bearerToken.isEmpty());

    transport.clearSentMessages();
    QCOMPARE(
        identity.joinGuest(
            QStringLiteral("room"),
            QStringLiteral("Guest")).error,
        WatchParty::IdentityActionError::None);
    QCOMPARE(transport.sentMessages().size(), 1);
}

QTEST_APPLESS_MAIN(tst_watchparty_identity)

#include "tst_watchparty_identity.moc"
