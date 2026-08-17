// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/SyncProtocol.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

class CaptureAccountTransport final
    : public AccountTransport {
public:
    using AccountTransport::AccountTransport;

    void send(
        quint64 requestId,
        const AccountTransportRequest &request) override {
        lastRequestId = requestId;
        lastRequest = request;
        ++sendCount;
    }

    quint64 lastRequestId = 0;
    AccountTransportRequest lastRequest;
    int sendCount = 0;
};

class tst_sync_protocol : public QObject {
    Q_OBJECT

private slots:
    void hlcOrdersPhysicalCounterThenDevice();
    void putMutationRoundTrips();
    void deleteMutationRoundTripsWithoutPayload();
    void deleteMutationRejectsOrdinaryPayload();
    void invalidRecordKeysAreRejected();
    void pushResultParsesClockSkewCurrentMetadata();
    void pullEntryParsesJournalEnvelope();
    void fullResponseEnvelopesParseServiceTimeAndAscendingCursorOrder();
    void recordKeyLimitUsesUtf8Bytes();
    void accountClientPushUsesAuthenticatedEndpoint();
    void accountClientPullUsesAuthenticatedCursorEndpoint();
};

void tst_sync_protocol::
hlcOrdersPhysicalCounterThenDevice() {
    SyncWireHlc a{
        1000,
        1,
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111")};
    SyncWireHlc b{
        1001,
        0,
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111")};
    QVERIFY(compareSyncWireHlc(a, b) < 0);

    b.physicalMs = 1000;
    b.counter = 2;
    QVERIFY(compareSyncWireHlc(a, b) < 0);

    b.counter = 1;
    b.deviceId =
        QStringLiteral(
            "22222222-2222-4222-8222-222222222222");
    QVERIFY(compareSyncWireHlc(a, b) < 0);
    QVERIFY(syncWireHlcGreater(b, a));
}

void tst_sync_protocol::
putMutationRoundTrips() {
    SyncWireMutation source;
    source.mutationId =
        QStringLiteral(
            "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    source.deviceId =
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111");
    source.category =
        QStringLiteral("collection");
    source.recordKey =
        QStringLiteral("manga/item-1");
    source.schemaVersion = 1;
    source.hlc = SyncWireHlc{
        2000,
        3,
        source.deviceId};
    source.operation =
        SyncWireOperation::Put;
    source.payload =
        QJsonObject{
            {
                QStringLiteral("logicalId"),
                QStringLiteral("item-1")
            },
            {
                QStringLiteral("value"),
                QStringLiteral("saved")
            }
        };

    const QJsonObject encoded =
        syncWireMutationToJson(source);
    const auto decoded =
        syncWireMutationFromJson(encoded);

    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->mutationId, source.mutationId);
    QCOMPARE(decoded->deviceId, source.deviceId);
    QCOMPARE(decoded->category, source.category);
    QCOMPARE(decoded->recordKey, source.recordKey);
    QCOMPARE(decoded->schemaVersion, 1);
    QCOMPARE(decoded->hlc.physicalMs, qint64(2000));
    QCOMPARE(decoded->hlc.counter, quint64(3));
    QCOMPARE(
        decoded->operation,
        SyncWireOperation::Put);
    QCOMPARE(decoded->payload, source.payload);
}

void tst_sync_protocol::
deleteMutationRoundTripsWithoutPayload() {
    SyncWireMutation source;
    source.mutationId =
        QStringLiteral(
            "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    source.deviceId =
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111");
    source.category =
        QStringLiteral("collection");
    source.recordKey =
        QStringLiteral("manga/item-1");
    source.schemaVersion = 1;
    source.hlc = SyncWireHlc{
        3000,
        0,
        source.deviceId};
    source.operation =
        SyncWireOperation::Delete;

    const QJsonObject encoded =
        syncWireMutationToJson(source);
    QVERIFY(
        !encoded.contains(
            QStringLiteral("payload")));

    const auto decoded =
        syncWireMutationFromJson(encoded);
    QVERIFY(decoded.has_value());
    QCOMPARE(
        decoded->operation,
        SyncWireOperation::Delete);
    QVERIFY(decoded->payload.isUndefined());
}

void tst_sync_protocol::
deleteMutationRejectsOrdinaryPayload() {
    QJsonObject encoded;
    encoded.insert(
        QStringLiteral("mutation_id"),
        QStringLiteral(
            "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"));
    encoded.insert(
        QStringLiteral("device_id"),
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111"));
    encoded.insert(
        QStringLiteral("category"),
        QStringLiteral("collection"));
    encoded.insert(
        QStringLiteral("record_key"),
        QStringLiteral("manga/item-1"));
    encoded.insert(
        QStringLiteral("schema_version"),
        1);
    encoded.insert(
        QStringLiteral("hlc_physical_ms"),
        QStringLiteral("3000"));
    encoded.insert(
        QStringLiteral("hlc_counter"),
        QStringLiteral("0"));
    encoded.insert(
        QStringLiteral("operation"),
        QStringLiteral("delete"));
    encoded.insert(
        QStringLiteral("payload"),
        QJsonObject{
            {
                QStringLiteral("value"),
                QStringLiteral("must-not-exist")
            }
        });

    QVERIFY(
        !syncWireMutationFromJson(
             encoded)
             .has_value());
}

void tst_sync_protocol::
invalidRecordKeysAreRejected() {
    const QStringList invalid = {
        QString(),
        QStringLiteral("/absolute"),
        QStringLiteral("\\machine"),
        QStringLiteral("a//b"),
        QStringLiteral("a/../b"),
        QStringLiteral(" a/b"),
        QStringLiteral("a\\b")
    };

    for (const QString &recordKey : invalid) {
        QVERIFY2(
            !isValidSyncWireRecordKey(
                recordKey),
            qPrintable(recordKey));
    }

    QVERIFY(
        isValidSyncWireRecordKey(
            QStringLiteral(
                "world/media/item-1")));
}

void tst_sync_protocol::
pushResultParsesClockSkewCurrentMetadata() {
    QJsonObject current;
    current.insert(
        QStringLiteral("mutation_id"),
        QStringLiteral(
            "cccccccc-cccc-4ccc-8ccc-cccccccccccc"));
    current.insert(
        QStringLiteral("device_id"),
        QStringLiteral(
            "22222222-2222-4222-8222-222222222222"));
    current.insert(
        QStringLiteral("schema_version"),
        1);
    current.insert(
        QStringLiteral("hlc_physical_ms"),
        QStringLiteral("4000"));
    current.insert(
        QStringLiteral("hlc_counter"),
        QStringLiteral("2"));
    current.insert(
        QStringLiteral("operation"),
        QStringLiteral("put"));
    current.insert(
        QStringLiteral("server_seq"),
        QStringLiteral("19"));

    QJsonObject result;
    result.insert(
        QStringLiteral("mutation_id"),
        QStringLiteral(
            "dddddddd-dddd-4ddd-8ddd-dddddddddddd"));
    result.insert(
        QStringLiteral("accepted"),
        false);
    result.insert(
        QStringLiteral("code"),
        QStringLiteral("clock_skew"));
    result.insert(
        QStringLiteral("message"),
        QStringLiteral("fixture"));
    result.insert(
        QStringLiteral("current"),
        current);

    const auto parsed =
        syncWirePushResultFromJson(result);
    QVERIFY(parsed.has_value());
    QVERIFY(!parsed->accepted);
    QCOMPARE(
        parsed->code,
        QStringLiteral("clock_skew"));
    QVERIFY(parsed->current.has_value());
    QCOMPARE(
        parsed->current->serverSeq,
        quint64(19));
    QCOMPARE(
        parsed->current->hlc.counter,
        quint64(2));
}

void tst_sync_protocol::
pullEntryParsesJournalEnvelope() {
    SyncWireMutation mutation;
    mutation.mutationId =
        QStringLiteral(
            "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee");
    mutation.deviceId =
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111");
    mutation.category =
        QStringLiteral("collection");
    mutation.recordKey =
        QStringLiteral("manga/item-1");
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{
        5000,
        1,
        mutation.deviceId};
    mutation.operation =
        SyncWireOperation::Put;
    mutation.payload =
        QJsonObject{
            {
                QStringLiteral("value"),
                QStringLiteral("fixture")
            }
        };

    QJsonObject entry;
    entry.insert(
        QStringLiteral("server_seq"),
        QStringLiteral("27"));
    entry.insert(
        QStringLiteral("won"),
        true);
    entry.insert(
        QStringLiteral("mutation"),
        syncWireMutationToJson(
            mutation));

    const auto parsed =
        syncWirePullEntryFromJson(entry);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->serverSeq, quint64(27));
    QVERIFY(parsed->won);
    QCOMPARE(
        parsed->mutation.recordKey,
        mutation.recordKey);
}

void tst_sync_protocol::
fullResponseEnvelopesParseServiceTimeAndAscendingCursorOrder() {
    QJsonObject pushResult;
    pushResult.insert(
        QStringLiteral("mutation_id"),
        QStringLiteral(
            "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
    pushResult.insert(
        QStringLiteral("accepted"),
        true);
    pushResult.insert(
        QStringLiteral("server_seq"),
        QStringLiteral("7"));
    pushResult.insert(
        QStringLiteral("won"),
        true);

    QJsonObject push;
    push.insert(
        QStringLiteral("server_time_ms"),
        QStringLiteral("6000"));
    push.insert(
        QStringLiteral("results"),
        QJsonArray{pushResult});

    const auto parsedPush =
        syncWirePushResponseFromJson(push);
    QVERIFY(parsedPush.has_value());
    QCOMPARE(parsedPush->serverTimeMs, qint64(6000));
    QCOMPARE(parsedPush->results.size(), 1);

    SyncWireMutation mutation;
    mutation.mutationId =
        QStringLiteral(
            "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    mutation.deviceId =
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111");
    mutation.category =
        QStringLiteral("collection");
    mutation.recordKey =
        QStringLiteral("manga/item-1");
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{
        6000,
        0,
        mutation.deviceId};
    mutation.operation =
        SyncWireOperation::Delete;

    QJsonObject first;
    first.insert(
        QStringLiteral("server_seq"),
        QStringLiteral("8"));
    first.insert(
        QStringLiteral("won"),
        true);
    first.insert(
        QStringLiteral("mutation"),
        syncWireMutationToJson(
            mutation));

    mutation.mutationId =
        QStringLiteral(
            "cccccccc-cccc-4ccc-8ccc-cccccccccccc");
    mutation.hlc.counter = 1;

    QJsonObject second;
    second.insert(
        QStringLiteral("server_seq"),
        QStringLiteral("9"));
    second.insert(
        QStringLiteral("won"),
        false);
    second.insert(
        QStringLiteral("mutation"),
        syncWireMutationToJson(
            mutation));

    QJsonObject pull;
    pull.insert(
        QStringLiteral("server_time_ms"),
        QStringLiteral("6001"));
    pull.insert(
        QStringLiteral("entries"),
        QJsonArray{first, second});
    pull.insert(
        QStringLiteral("has_more"),
        true);

    const auto parsedPull =
        syncWirePullResponseFromJson(pull);
    QVERIFY(parsedPull.has_value());
    QCOMPARE(parsedPull->serverTimeMs, qint64(6001));
    QCOMPARE(parsedPull->entries.size(), 2);
    QVERIFY(parsedPull->hasMore);

    QJsonObject descending = pull;
    descending.insert(
        QStringLiteral("entries"),
        QJsonArray{second, first});
    QVERIFY(
        !syncWirePullResponseFromJson(
             descending)
             .has_value());
}

void tst_sync_protocol::
recordKeyLimitUsesUtf8Bytes() {
    const QString ascii(
        512,
        QLatin1Char('a'));
    QVERIFY(
        isValidSyncWireRecordKey(
            ascii));

    const QString unicode(
        257,
        QChar(0x00e9));
    QVERIFY(
        unicode.size() <= 512);
    QVERIFY(
        unicode.toUtf8().size() > 512);
    QVERIFY(
        !isValidSyncWireRecordKey(
            unicode));
}

void tst_sync_protocol::
accountClientPushUsesAuthenticatedEndpoint() {
    CaptureAccountTransport transport;
    AccountClient client(&transport);
    client.setAccessToken(
        QByteArrayLiteral("fixture-access"));

    QJsonArray mutations;
    mutations.append(
        QJsonObject{
            {
                QStringLiteral("mutation_id"),
                QStringLiteral(
                    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa")
            }
        });

    const quint64 requestId =
        client.pushSync(mutations);

    QCOMPARE(transport.sendCount, 1);
    QCOMPARE(
        transport.lastRequestId,
        requestId);
    QCOMPARE(
        transport.lastRequest.method,
        QByteArrayLiteral("POST"));
    QCOMPARE(
        transport.lastRequest.path,
        QStringLiteral("/v1/sync/push"));
    QCOMPARE(
        transport.lastRequest.bearerToken,
        QByteArrayLiteral("fixture-access"));
    QCOMPARE(
        transport.lastRequest.body
            .value(
                QStringLiteral("mutations"))
            .toArray()
            .size(),
        1);
}

void tst_sync_protocol::
accountClientPullUsesAuthenticatedCursorEndpoint() {
    CaptureAccountTransport transport;
    AccountClient client(&transport);
    client.setAccessToken(
        QByteArrayLiteral("fixture-access"));

    const quint64 requestId =
        client.pullSync(42);

    QCOMPARE(transport.sendCount, 1);
    QCOMPARE(
        transport.lastRequestId,
        requestId);
    QCOMPARE(
        transport.lastRequest.method,
        QByteArrayLiteral("GET"));
    QCOMPARE(
        transport.lastRequest.path,
        QStringLiteral(
            "/v1/sync/pull?after=42"));
    QCOMPARE(
        transport.lastRequest.bearerToken,
        QByteArrayLiteral("fixture-access"));
    QVERIFY(
        transport.lastRequest.body.isEmpty());
}

QTEST_MAIN(tst_sync_protocol)
#include "tst_sync_protocol.moc"
