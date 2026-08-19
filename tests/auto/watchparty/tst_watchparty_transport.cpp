#include "watchparty/FakeWatchPartyTransport.h"
#include "watchparty/WatchPartyProtocol.h"
#include "watchparty/WatchPartyRoomServiceClient.h"
#include "watchparty/WebSocketWatchPartyTransport.h"

#include <QJsonDocument>
#include <QNetworkRequest>
#include <QtTest>

#include <memory>
#include <utility>

namespace WatchParty = Colosseum::WatchParty;

namespace {

class FakeSocket final : public WatchParty::IWebSocket
{
public:
    void setConnectedHandler(ConnectedHandler handler) override
    {
        connectedHandler = std::move(handler);
    }

    void setDisconnectedHandler(DisconnectedHandler handler) override
    {
        disconnectedHandler = std::move(handler);
    }

    void setTextHandler(TextHandler handler) override
    {
        textHandler = std::move(handler);
    }

    void setBinaryHandler(BinaryHandler handler) override
    {
        binaryHandler = std::move(handler);
    }

    void setErrorHandler(ErrorHandler handler) override
    {
        errorHandler = std::move(handler);
    }

    void setMaxAllowedIncomingMessageSize(quint64 bytes) override
    {
        maxIncomingBytes = bytes;
    }

    void open(const QNetworkRequest& request) override
    {
        openRequests.append(request);
        closed = false;
    }

    qint64 sendTextMessage(const QString& message) override
    {
        if (failSend)
            return -1;
        sentText.append(message);
        return message.toUtf8().size();
    }

    void close() override
    {
        ++closeCount;
        closed = true;
    }

    void connectNow()
    {
        if (connectedHandler)
            connectedHandler();
    }

    void disconnectNow()
    {
        if (disconnectedHandler)
            disconnectedHandler();
    }

    void injectText(const QString& text)
    {
        if (textHandler)
            textHandler(text);
    }

    void injectBinary(const QByteArray& bytes)
    {
        if (binaryHandler)
            binaryHandler(bytes);
    }

    void injectError(const QString& detail)
    {
        if (errorHandler)
            errorHandler(detail);
    }

    ConnectedHandler connectedHandler;
    DisconnectedHandler disconnectedHandler;
    TextHandler textHandler;
    BinaryHandler binaryHandler;
    ErrorHandler errorHandler;

    QList<QNetworkRequest> openRequests;
    QList<QString> sentText;
    quint64 maxIncomingBytes = 0;
    int closeCount = 0;
    bool closed = false;
    bool failSend = false;
};

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
    state.identity = WatchParty::ParticipantIdentity{id, displayName, kind};
    state.joinOrder = joinOrder;
    state.host = host;
    state.connected = true;
    state.ready = true;
    state.syncStatus = WatchParty::SyncStatus::InSync;
    return state;
}

WatchParty::RoomSnapshot snapshotFor(
    const QString& roomId,
    const QString& localParticipantId,
    bool localIsHost = true)
{
    WatchParty::RoomSnapshot snapshot;
    snapshot.roomId = roomId;
    snapshot.source = source();
    snapshot.controlMode = WatchParty::ControlMode::HostControl;
    snapshot.timeline.playing = true;
    snapshot.timeline.positionMs = 12'000;
    snapshot.timeline.revision = 5;
    snapshot.hostReconnectDeadlineMs = -1;

    if (localIsHost) {
        snapshot.hostParticipantId = localParticipantId;
        snapshot.participants.append(
            participant(
                localParticipantId,
                QStringLiteral("Host"),
                WatchParty::IdentityKind::SignedIn,
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
                QStringLiteral("next"),
                QStringLiteral("Next"),
                WatchParty::IdentityKind::SignedIn,
                2,
                false));
        snapshot.participants.append(
            participant(
                localParticipantId,
                QStringLiteral("Guest"),
                WatchParty::IdentityKind::Guest,
                3,
                false));
    }

    return snapshot;
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
    const QString& reconnectToken,
    qint64 sequence)
{
    WatchParty::SessionEstablished session;
    session.participantId = participantId;
    session.reconnectToken = reconnectToken;
    return serverMessage(
        WatchParty::MessageType::SessionEstablished,
        roomId,
        WatchParty::sessionEstablishedToJson(session),
        sequence);
}

WatchParty::ProtocolMessage snapshotMessage(
    const WatchParty::RoomSnapshot& snapshot,
    qint64 sequence)
{
    return serverMessage(
        WatchParty::MessageType::RoomSnapshot,
        snapshot.roomId,
        WatchParty::roomSnapshotToJson(snapshot),
        sequence);
}

} // namespace

class tst_watchparty_transport final : public QObject
{
    Q_OBJECT

private slots:
    void websocket_requires_wss_and_keeps_bearer_out_of_wire();
    void websocket_wrong_version_is_terminal_protocol_failure();
    void websocket_binary_message_is_terminal_protocol_failure();
    void websocket_reconnect_is_bounded_and_retryable();
    void websocket_outbound_rate_is_bounded();
    void websocket_inbound_rate_and_wire_size_are_bounded();
    void protocol_rejects_malformed_authoritative_snapshot();
    void service_create_reconnect_and_token_rotation();
    void service_reconnect_rejects_identity_change();
    void service_signed_in_join_uses_authenticated_identity_only();
    void service_guest_join_and_host_transfer();
    void service_guest_cannot_inherit_host_authority();
    void service_rejects_conflicting_authoritative_timeline();
};

void tst_watchparty_transport::
websocket_requires_wss_and_keeps_bearer_out_of_wire()
{
    auto socket = std::make_unique<FakeSocket>();
    FakeSocket* rawSocket = socket.get();
    WatchParty::WebSocketTransport transport(std::move(socket));

    WatchParty::TransportError lastError;
    transport.setErrorHandler(
        [&](const WatchParty::TransportError& error) {
            lastError = error;
        });

    WatchParty::TransportOpenOptions insecure;
    insecure.serviceUrl =
        QUrl(QStringLiteral("ws://party.example.test/v1"));
    QVERIFY(!transport.open(insecure));
    QCOMPARE(
        lastError.code,
        WatchParty::TransportErrorCode::InvalidConfiguration);
    QVERIFY(rawSocket->openRequests.isEmpty());

    WatchParty::TransportOpenOptions options;
    options.serviceUrl =
        QUrl(QStringLiteral("wss://party.example.test/v1"));
    options.bearerToken = QByteArrayLiteral("signed-in-secret");

    QVERIFY(transport.open(options));
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::Connecting);
    QCOMPARE(rawSocket->openRequests.size(), 1);
    QCOMPARE(
        rawSocket->maxIncomingBytes,
        static_cast<quint64>(WatchParty::kMaxWireMessageBytes));
    QCOMPARE(
        rawSocket->openRequests.first().rawHeader(
            QByteArrayLiteral("Authorization")),
        QByteArrayLiteral("Bearer signed-in-secret"));
    QCOMPARE(
        rawSocket->openRequests.first().rawHeader(
            QByteArrayLiteral(
                "X-Colosseum-Watch-Party-Protocol")),
        QByteArray::number(WatchParty::kProtocolVersion));

    rawSocket->connectNow();
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::Connected);

    WatchParty::ProtocolMessage create;
    create.type = WatchParty::MessageType::CreateRoom;
    create.sequence = 1;
    create.payload = QJsonObject{
        {
            QStringLiteral("source"),
            WatchParty::sourceDescriptorToJson(source())
        }
    };

    QVERIFY(transport.send(create));
    QCOMPARE(rawSocket->sentText.size(), 1);
    QVERIFY(
        !rawSocket->sentText.first().contains(
            QStringLiteral("signed-in-secret")));

    const WatchParty::DecodeResult decoded =
        WatchParty::decodeMessage(
            rawSocket->sentText.first().toUtf8());
    QVERIFY(decoded.ok);
    QCOMPARE(
        decoded.message.type,
        WatchParty::MessageType::CreateRoom);
}

void tst_watchparty_transport::
websocket_wrong_version_is_terminal_protocol_failure()
{
    auto socket = std::make_unique<FakeSocket>();
    FakeSocket* rawSocket = socket.get();
    WatchParty::WebSocketTransport transport(std::move(socket));

    WatchParty::TransportError lastError;
    transport.setErrorHandler(
        [&](const WatchParty::TransportError& error) {
            lastError = error;
        });

    WatchParty::TransportOpenOptions options;
    options.serviceUrl =
        QUrl(QStringLiteral("wss://party.example.test/v1"));
    QVERIFY(transport.open(options));
    rawSocket->connectNow();

    WatchParty::ProtocolMessage ended =
        serverMessage(
            WatchParty::MessageType::RoomEnded,
            QStringLiteral("room-a"),
            {},
            8);
    QJsonObject object =
        QJsonDocument::fromJson(
            WatchParty::encodeMessage(ended))
            .object();
    object.insert(
        QStringLiteral("version"),
        WatchParty::kProtocolVersion + 1);

    rawSocket->injectText(
        QString::fromUtf8(
            QJsonDocument(object).toJson(
                QJsonDocument::Compact)));

    QCOMPARE(
        lastError.code,
        WatchParty::TransportErrorCode::ProtocolVersionMismatch);
    QVERIFY(lastError.terminal);
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::Closed);
    QVERIFY(rawSocket->closeCount >= 1);
    QVERIFY(!transport.retryNow());
}

void tst_watchparty_transport::
websocket_binary_message_is_terminal_protocol_failure()
{
    auto socket = std::make_unique<FakeSocket>();
    FakeSocket* rawSocket = socket.get();
    WatchParty::WebSocketTransport transport(std::move(socket));

    WatchParty::TransportError lastError;
    transport.setErrorHandler(
        [&](const WatchParty::TransportError& error) {
            lastError = error;
        });

    WatchParty::TransportOpenOptions options;
    options.serviceUrl =
        QUrl(QStringLiteral("wss://party.example.test/v1"));
    QVERIFY(transport.open(options));
    rawSocket->connectNow();

    rawSocket->injectBinary(QByteArrayLiteral("not-json"));

    QCOMPARE(
        lastError.code,
        WatchParty::TransportErrorCode::ProtocolRejected);
    QVERIFY(lastError.terminal);
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::Closed);
}

void tst_watchparty_transport::
websocket_reconnect_is_bounded_and_retryable()
{
    QCOMPARE(
        WatchParty::WebSocketTransport::reconnectDelayMs(0),
        500);
    QCOMPARE(
        WatchParty::WebSocketTransport::reconnectDelayMs(1),
        1'000);
    QCOMPARE(
        WatchParty::WebSocketTransport::reconnectDelayMs(2),
        2'000);
    QCOMPARE(
        WatchParty::WebSocketTransport::reconnectDelayMs(3),
        5'000);
    QCOMPARE(
        WatchParty::WebSocketTransport::reconnectDelayMs(4),
        10'000);
    QCOMPARE(
        WatchParty::WebSocketTransport::reconnectDelayMs(50),
        10'000);

    auto socket = std::make_unique<FakeSocket>();
    FakeSocket* rawSocket = socket.get();
    WatchParty::WebSocketTransport transport(std::move(socket));

    WatchParty::TransportOpenOptions options;
    options.serviceUrl =
        QUrl(QStringLiteral("wss://party.example.test/v1"));
    QVERIFY(transport.open(options));
    rawSocket->connectNow();

    rawSocket->disconnectNow();
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::WaitingToReconnect);

    QVERIFY(transport.retryNow());
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::Reconnecting);
    QCOMPARE(rawSocket->openRequests.size(), 2);

    rawSocket->connectNow();
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::Connected);
}

void tst_watchparty_transport::
websocket_outbound_rate_is_bounded()
{
    auto socket = std::make_unique<FakeSocket>();
    FakeSocket* rawSocket = socket.get();
    WatchParty::WebSocketTransport transport(std::move(socket));

    WatchParty::TransportError lastError;
    transport.setErrorHandler(
        [&](const WatchParty::TransportError& error) {
            lastError = error;
        });

    WatchParty::TransportOpenOptions options;
    options.serviceUrl =
        QUrl(QStringLiteral("wss://party.example.test/v1"));
    QVERIFY(transport.open(options));
    rawSocket->connectNow();

    for (int i = 0;
         i < WatchParty::WebSocketTransport::kMaxMessagesPerRateWindow;
         ++i) {
        WatchParty::ProtocolMessage chat;
        chat.type = WatchParty::MessageType::Chat;
        chat.roomId = QStringLiteral("room-a");
        chat.senderId = QStringLiteral("participant-a");
        chat.sequence = i + 1;
        chat.payload = QJsonObject{
            {
                QStringLiteral("message"),
                QStringLiteral("message %1").arg(i)
            }
        };
        QVERIFY(transport.send(chat));
    }

    WatchParty::ProtocolMessage overflow;
    overflow.type = WatchParty::MessageType::Chat;
    overflow.roomId = QStringLiteral("room-a");
    overflow.senderId = QStringLiteral("participant-a");
    overflow.sequence = 999;
    overflow.payload = QJsonObject{
        {
            QStringLiteral("message"),
            QStringLiteral("one too many")
        }
    };

    QVERIFY(!transport.send(overflow));
    QCOMPARE(
        lastError.code,
        WatchParty::TransportErrorCode::RateLimited);
    QCOMPARE(
        rawSocket->sentText.size(),
        WatchParty::WebSocketTransport::kMaxMessagesPerRateWindow);
}

void tst_watchparty_transport::
websocket_inbound_rate_and_wire_size_are_bounded()
{
    auto socket = std::make_unique<FakeSocket>();
    FakeSocket* rawSocket = socket.get();
    WatchParty::WebSocketTransport transport(std::move(socket));

    WatchParty::TransportError lastError;
    transport.setErrorHandler(
        [&](const WatchParty::TransportError& error) {
            lastError = error;
        });

    WatchParty::TransportOpenOptions options;
    options.serviceUrl =
        QUrl(QStringLiteral("wss://party.example.test/v1"));
    QVERIFY(transport.open(options));
    rawSocket->connectNow();

    WatchParty::ProtocolMessage oversized;
    oversized.type = WatchParty::MessageType::Chat;
    oversized.roomId = QStringLiteral("room-a");
    oversized.senderId = QStringLiteral("participant-a");
    oversized.sequence = 1;
    oversized.payload = QJsonObject{
        {
            QStringLiteral("message"),
            QString(WatchParty::kMaxWireMessageBytes, QLatin1Char('x'))
        }
    };

    QVERIFY(!transport.send(oversized));
    QCOMPARE(
        lastError.code,
        WatchParty::TransportErrorCode::MessageTooLarge);
    QCOMPARE(rawSocket->sentText.size(), 0);

    for (int i = 0;
         i < WatchParty::WebSocketTransport::kMaxMessagesPerRateWindow;
         ++i) {
        const WatchParty::ProtocolMessage ended =
            serverMessage(
                WatchParty::MessageType::RoomEnded,
                QStringLiteral("room-a"),
                {},
                i + 1);
        rawSocket->injectText(
            QString::fromUtf8(
                WatchParty::encodeMessage(ended)));
        QCOMPARE(
            transport.state(),
            WatchParty::TransportState::Connected);
    }

    const WatchParty::ProtocolMessage overflow =
        serverMessage(
            WatchParty::MessageType::RoomEnded,
            QStringLiteral("room-a"),
            {},
            999);
    rawSocket->injectText(
        QString::fromUtf8(
            WatchParty::encodeMessage(overflow)));

    QCOMPARE(
        lastError.code,
        WatchParty::TransportErrorCode::RateLimited);
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::Closed);
    QVERIFY(rawSocket->closeCount >= 1);
}

void tst_watchparty_transport::
protocol_rejects_malformed_authoritative_snapshot()
{
    WatchParty::RoomSnapshot snapshot =
        snapshotFor(
            QStringLiteral("room-a"),
            QStringLiteral("host"));

    QJsonObject payload =
        WatchParty::roomSnapshotToJson(snapshot);

    QJsonObject sourceObject =
        payload.value(QStringLiteral("source")).toObject();
    sourceObject.insert(
        QStringLiteral("headers"),
        QStringLiteral("Authorization: secret"));
    payload.insert(QStringLiteral("source"), sourceObject);

    WatchParty::ProtocolMessage message =
        serverMessage(
            WatchParty::MessageType::RoomSnapshot,
            QStringLiteral("room-a"),
            payload,
            2);

    const WatchParty::ValidationResult secretField =
        WatchParty::validateMessage(
            message,
            WatchParty::MessageDirection::ServerToClient);
    QVERIFY(!secretField.ok);
    QVERIFY(
        secretField.error.contains(
            QStringLiteral("unknown torrent source key")));

    WatchParty::RoomSnapshot tooMany =
        snapshotFor(
            QStringLiteral("room-a"),
            QStringLiteral("host"));
    for (int i = tooMany.participants.size();
         i <= WatchParty::kMaxParticipants;
         ++i) {
        tooMany.participants.append(
            participant(
                QStringLiteral("p%1").arg(i),
                QStringLiteral("Participant %1").arg(i),
                WatchParty::IdentityKind::SignedIn,
                static_cast<quint64>(i + 1),
                false));
    }

    message.payload =
        WatchParty::roomSnapshotToJson(tooMany);
    const WatchParty::ValidationResult capacity =
        WatchParty::validateMessage(
            message,
            WatchParty::MessageDirection::ServerToClient);
    QVERIFY(!capacity.ok);
    QVERIFY(
        capacity.error.contains(
            QStringLiteral("participants must contain")));
}

void tst_watchparty_transport::
service_create_reconnect_and_token_rotation()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient client(&transport);

    QVERIFY(
        client.openService(
            QUrl(QStringLiteral("wss://party.example.test/v1")),
            QByteArrayLiteral("account-token")));
    QCOMPARE(
        client.transportState(),
        WatchParty::TransportState::Connected);

    QVERIFY(client.createRoom(source()));
    QCOMPARE(transport.sentMessages().size(), 1);
    QCOMPARE(
        transport.sentMessages().last().type,
        WatchParty::MessageType::CreateRoom);

    QVERIFY(
        transport.injectIncoming(
            sessionEstablished(
                QStringLiteral("room-new"),
                QStringLiteral("host-id"),
                QStringLiteral("resume-1"),
                1)));

    const WatchParty::RoomSnapshot snapshot =
        snapshotFor(
            QStringLiteral("room-new"),
            QStringLiteral("host-id"));
    QVERIFY(
        transport.injectIncoming(
            snapshotMessage(snapshot, 2)));
    QVERIFY(client.hasSession());
    QVERIFY(client.hasSnapshot());

    transport.clearSentMessages();
    transport.simulateDisconnect();
    QCOMPARE(
        client.transportState(),
        WatchParty::TransportState::WaitingToReconnect);

    QVERIFY(client.retryTransportNow());
    QCOMPARE(
        client.transportState(),
        WatchParty::TransportState::Connected);
    QCOMPARE(transport.sentMessages().size(), 1);
    QCOMPARE(
        transport.sentMessages().last().type,
        WatchParty::MessageType::ReconnectRoom);
    QCOMPARE(
        transport.sentMessages().last().payload.value(
            QStringLiteral("reconnectToken")).toString(),
        QStringLiteral("resume-1"));

    // Successful reconnect rotates the private room credential. The new token
    // remains transport/session-private and never enters RoomSnapshot.
    QVERIFY(
        transport.injectIncoming(
            sessionEstablished(
                QStringLiteral("room-new"),
                QStringLiteral("host-id"),
                QStringLiteral("resume-2"),
                3)));

    transport.clearSentMessages();
    transport.simulateDisconnect();
    QVERIFY(client.retryTransportNow());
    QCOMPARE(
        transport.sentMessages().last().payload.value(
            QStringLiteral("reconnectToken")).toString(),
        QStringLiteral("resume-2"));
}

void tst_watchparty_transport::
service_reconnect_rejects_identity_change()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient client(&transport);

    WatchParty::RoomServiceError lastError;
    client.setErrorHandler(
        [&](const WatchParty::RoomServiceError& error) {
            lastError = error;
        });

    QVERIFY(
        client.openService(
            QUrl(QStringLiteral("wss://party.example.test/v1")),
            QByteArrayLiteral("account-token")));
    QVERIFY(client.createRoom(source()));
    QVERIFY(
        transport.injectIncoming(
            sessionEstablished(
                QStringLiteral("room-a"),
                QStringLiteral("host-id"),
                QStringLiteral("resume-1"),
                1)));
    QVERIFY(
        transport.injectIncoming(
            snapshotMessage(
                snapshotFor(
                    QStringLiteral("room-a"),
                    QStringLiteral("host-id")),
                2)));

    transport.clearSentMessages();
    transport.simulateDisconnect();
    QVERIFY(client.retryTransportNow());
    QCOMPARE(
        transport.sentMessages().last().type,
        WatchParty::MessageType::ReconnectRoom);

    QVERIFY(
        transport.injectIncoming(
            sessionEstablished(
                QStringLiteral("room-a"),
                QStringLiteral("different-participant"),
                QStringLiteral("resume-2"),
                3)));

    QCOMPARE(
        lastError.code,
        WatchParty::RoomServiceErrorCode::ProtocolFailure);
    QVERIFY(!client.hasSession());
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::Closed);
}

void tst_watchparty_transport::
service_signed_in_join_uses_authenticated_identity_only()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient client(&transport);

    QVERIFY(
        client.openService(
            QUrl(QStringLiteral("wss://party.example.test/v1")),
            QByteArrayLiteral("account-token")));
    QVERIFY(client.joinSignedIn(QStringLiteral("room-a")));

    QCOMPARE(transport.sentMessages().size(), 1);
    const WatchParty::ProtocolMessage join =
        transport.sentMessages().first();
    QCOMPARE(join.type, WatchParty::MessageType::JoinRoom);
    QCOMPARE(join.roomId, QStringLiteral("room-a"));
    QCOMPARE(
        join.payload.value(QStringLiteral("identityKind")).toString(),
        QStringLiteral("signedIn"));
    QCOMPARE(join.payload.keys().size(), 1);
    QVERIFY(join.senderId.isEmpty());
    QVERIFY(join.sequence > 0);
}

void tst_watchparty_transport::
service_guest_join_and_host_transfer()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient client(&transport);

    QString changedHost;
    client.setHostChangedHandler(
        [&](const QString& participantId) {
            changedHost = participantId;
        });

    QVERIFY(
        client.openService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(
        client.joinGuest(
            QStringLiteral("room-a"),
            QStringLiteral("Guest")));
    QCOMPARE(
        transport.sentMessages().last().type,
        WatchParty::MessageType::JoinRoom);
    QCOMPARE(
        transport.sentMessages().last().payload.value(
            QStringLiteral("identityKind")).toString(),
        QStringLiteral("guest"));

    QVERIFY(
        transport.injectIncoming(
            sessionEstablished(
                QStringLiteral("room-a"),
                QStringLiteral("guest-id"),
                QStringLiteral("guest-resume"),
                1)));

    WatchParty::RoomSnapshot snapshot =
        snapshotFor(
            QStringLiteral("room-a"),
            QStringLiteral("guest-id"),
            false);
    QVERIFY(
        transport.injectIncoming(
            snapshotMessage(snapshot, 2)));

    WatchParty::ProtocolMessage hostChanged =
        serverMessage(
            WatchParty::MessageType::HostChanged,
            QStringLiteral("room-a"),
            QJsonObject{
                {
                    QStringLiteral("hostParticipantId"),
                    QStringLiteral("next")
                }
            },
            3);
    QVERIFY(transport.injectIncoming(hostChanged));

    QCOMPARE(changedHost, QStringLiteral("next"));
    QCOMPARE(
        client.snapshot().hostParticipantId,
        QStringLiteral("next"));

    int hostCount = 0;
    for (const WatchParty::ParticipantState& row :
         client.snapshot().participants) {
        if (row.host)
            ++hostCount;
    }
    QCOMPARE(hostCount, 1);
}

void tst_watchparty_transport::
service_guest_cannot_inherit_host_authority()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient client(&transport);

    WatchParty::RoomServiceError lastError;
    client.setErrorHandler(
        [&](const WatchParty::RoomServiceError& error) {
            lastError = error;
        });

    QVERIFY(
        client.openService(
            QUrl(QStringLiteral("wss://party.example.test/v1"))));
    QVERIFY(
        client.joinGuest(
            QStringLiteral("room-a"),
            QStringLiteral("Guest")));
    QVERIFY(
        transport.injectIncoming(
            sessionEstablished(
                QStringLiteral("room-a"),
                QStringLiteral("guest-id"),
                QStringLiteral("guest-resume"),
                1)));
    QVERIFY(
        transport.injectIncoming(
            snapshotMessage(
                snapshotFor(
                    QStringLiteral("room-a"),
                    QStringLiteral("guest-id"),
                    false),
                2)));

    WatchParty::ProtocolMessage invalidTransfer =
        serverMessage(
            WatchParty::MessageType::HostChanged,
            QStringLiteral("room-a"),
            QJsonObject{
                {
                    QStringLiteral("hostParticipantId"),
                    QStringLiteral("guest-id")
                }
            },
            3);

    QVERIFY(transport.injectIncoming(invalidTransfer));
    QCOMPARE(
        lastError.code,
        WatchParty::RoomServiceErrorCode::ProtocolFailure);
    QVERIFY(!client.hasSession());
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::Closed);
}

void tst_watchparty_transport::
service_rejects_conflicting_authoritative_timeline()
{
    WatchParty::FakeTransport transport(false);
    WatchParty::RoomServiceClient client(&transport);

    WatchParty::RoomServiceError lastError;
    client.setErrorHandler(
        [&](const WatchParty::RoomServiceError& error) {
            lastError = error;
        });

    QVERIFY(
        client.openService(
            QUrl(QStringLiteral("wss://party.example.test/v1")),
            QByteArrayLiteral("account-token")));
    QVERIFY(client.createRoom(source()));
    QVERIFY(
        transport.injectIncoming(
            sessionEstablished(
                QStringLiteral("room-a"),
                QStringLiteral("host-id"),
                QStringLiteral("resume"),
                1)));
    QVERIFY(
        transport.injectIncoming(
            snapshotMessage(
                snapshotFor(
                    QStringLiteral("room-a"),
                    QStringLiteral("host-id")),
                2)));

    WatchParty::TimelineState conflicting;
    conflicting.playing = false;
    conflicting.positionMs = 99'000;
    conflicting.revision = 5;

    QVERIFY(
        transport.injectIncoming(
            serverMessage(
                WatchParty::MessageType::TimelineState,
                QStringLiteral("room-a"),
                WatchParty::timelineStateToJson(conflicting),
                3)));

    QCOMPARE(
        lastError.code,
        WatchParty::RoomServiceErrorCode::ProtocolFailure);
    QVERIFY(!client.hasSession());
    QCOMPARE(
        transport.state(),
        WatchParty::TransportState::Closed);
}

QTEST_GUILESS_MAIN(tst_watchparty_transport)
#include "tst_watchparty_transport.moc"
