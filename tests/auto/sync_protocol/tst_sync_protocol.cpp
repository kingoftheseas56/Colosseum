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
    void pullEntryParsesCanonicalCompatibilityFlag();
    void fullResponseEnvelopesParseServiceTimeAndAscendingCursorOrder();
    void recordKeyLimitUsesUtf8Bytes();
    void accountClientPushUsesAuthenticatedEndpoint();
    void accountClientPullUsesAuthenticatedCursorEndpoint();
    void attachmentResponseParsesLifecycleFields();
    void attachmentResponseRejectsMalformedRequiredFields();
    void snapshotResponseParsesFixedCursorEntriesAndPageToken();
    void snapshotResponseRejectsMalformedRequiredFields();
    void accountClientBeginAttachmentPostsAuthenticatedEnvelope();
    void accountClientGetAttachmentEncodesPathSegment();
    void accountClientCommitAttachmentPostsToCommitEndpoint();
    void accountClientSnapshotAppendsEncodedAfterKeyOnlyWithToken();
    void accountClientPushIncludesAttachmentIdOnlyWhenAttached();
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
    QVERIFY(!parsed->canonical);
    QCOMPARE(
        parsed->mutation.recordKey,
        mutation.recordKey);
}

void tst_sync_protocol::
pullEntryParsesCanonicalCompatibilityFlag() {
    SyncWireMutation mutation;
    mutation.mutationId =
        QStringLiteral(
            "ffffffff-ffff-4fff-8fff-ffffffffffff");
    mutation.deviceId =
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111");
    mutation.category =
        QStringLiteral("collection");
    mutation.recordKey =
        QStringLiteral("manga/item-canonical");
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{
        5100,
        0,
        mutation.deviceId};
    mutation.operation =
        SyncWireOperation::Put;
    mutation.payload =
        QJsonObject{
            {
                QStringLiteral("value"),
                QStringLiteral("canonical")
            }
        };

    QJsonObject entry;
    entry.insert(
        QStringLiteral("server_seq"),
        QStringLiteral("28"));
    entry.insert(
        QStringLiteral("won"),
        false);
    entry.insert(
        QStringLiteral("canonical"),
        true);
    entry.insert(
        QStringLiteral("mutation"),
        syncWireMutationToJson(mutation));

    const auto parsed =
        syncWirePullEntryFromJson(entry);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->serverSeq, quint64(28));
    QVERIFY(!parsed->won);
    QVERIFY(parsed->canonical);
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

namespace {
QJsonObject attachmentMutationJson(
    const QString &mutationId,
    const QString &recordKey,
    qint64 physicalMs) {
    SyncWireMutation mutation;
    mutation.mutationId = mutationId;
    mutation.deviceId =
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111");
    mutation.category =
        QStringLiteral("collection");
    mutation.recordKey = recordKey;
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{
        physicalMs,
        0,
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
    return syncWireMutationToJson(mutation);
}

QJsonObject snapshotEntryJson(
    quint64 serverSeq,
    const QString &recordKey,
    const QString &mutationId,
    bool canonical) {
    QJsonObject entry;
    entry.insert(
        QStringLiteral("server_seq"),
        QString::number(serverSeq));
    entry.insert(
        QStringLiteral("won"),
        true);
    entry.insert(
        QStringLiteral("canonical"),
        canonical);
    entry.insert(
        QStringLiteral("mutation"),
        attachmentMutationJson(
            mutationId,
            recordKey,
            8000));
    return entry;
}
}

void tst_sync_protocol::
attachmentResponseParsesLifecycleFields() {
    QJsonObject attachment;
    attachment.insert(
        QStringLiteral("attachment_id"),
        QStringLiteral(
            "12345678-1234-4123-8123-123456789ABC"));
    attachment.insert(
        QStringLiteral("device_id"),
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111"));
    attachment.insert(
        QStringLiteral("baseline_server_seq"),
        QStringLiteral("41"));
    attachment.insert(
        QStringLiteral("state"),
        QStringLiteral("uploaded"));

    const auto parsed =
        syncWireAttachmentResponseFromJson(
            attachment);
    QVERIFY(parsed.has_value());
    QCOMPARE(
        parsed->attachmentId,
        QStringLiteral(
            "12345678-1234-4123-8123-123456789abc"));
    QCOMPARE(
        parsed->deviceId,
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111"));
    QCOMPARE(
        parsed->baselineServerSeq,
        quint64(41));
    QCOMPARE(
        parsed->state,
        SyncWireAttachmentState::Uploaded);

    const QStringList states = {
        QStringLiteral("open"),
        QStringLiteral("uploaded"),
        QStringLiteral("committed"),
        QStringLiteral("aborted")
    };
    for (const QString &name : states) {
        const auto state =
            syncWireAttachmentStateFromName(
                name);
        QVERIFY2(
            state.has_value(),
            qPrintable(name));
        QCOMPARE(
            syncWireAttachmentStateName(
                *state),
            name);
    }
}

void tst_sync_protocol::
attachmentResponseRejectsMalformedRequiredFields() {
    QJsonObject valid;
    valid.insert(
        QStringLiteral("attachment_id"),
        QStringLiteral(
            "12345678-1234-4123-8123-123456789abc"));
    valid.insert(
        QStringLiteral("device_id"),
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111"));
    valid.insert(
        QStringLiteral("baseline_server_seq"),
        QStringLiteral("41"));
    valid.insert(
        QStringLiteral("state"),
        QStringLiteral("open"));

    QVERIFY(
        syncWireAttachmentResponseFromJson(
             valid)
            .has_value());

    QJsonObject missingDevice = valid;
    missingDevice.remove(
        QStringLiteral("device_id"));
    QVERIFY(
        !syncWireAttachmentResponseFromJson(
             missingDevice)
            .has_value());

    QJsonObject missingBaseline = valid;
    missingBaseline.remove(
        QStringLiteral(
            "baseline_server_seq"));
    QVERIFY(
        !syncWireAttachmentResponseFromJson(
             missingBaseline)
            .has_value());

    QJsonObject negativeBaseline = valid;
    negativeBaseline.insert(
        QStringLiteral(
            "baseline_server_seq"),
        QStringLiteral("-1"));
    QVERIFY(
        !syncWireAttachmentResponseFromJson(
             negativeBaseline)
            .has_value());

    QJsonObject unknownState = valid;
    unknownState.insert(
        QStringLiteral("state"),
        QStringLiteral("pending"));
    QVERIFY(
        !syncWireAttachmentResponseFromJson(
             unknownState)
            .has_value());

    QJsonObject badAttachmentId = valid;
    badAttachmentId.insert(
        QStringLiteral("attachment_id"),
        QStringLiteral("not-a-uuid"));
    QVERIFY(
        !syncWireAttachmentResponseFromJson(
             badAttachmentId)
            .has_value());
}

void tst_sync_protocol::
snapshotResponseParsesFixedCursorEntriesAndPageToken() {
    // Snapshot pages sort by (category, record_key) under one frozen
    // cursor, so server_seq ordering across entries is intentionally
    // NOT ascending — unlike the ordinary pull journal envelope.
    QJsonObject snapshot;
    snapshot.insert(
        QStringLiteral("server_time_ms"),
        QStringLiteral("7000"));
    snapshot.insert(
        QStringLiteral("cursor"),
        QStringLiteral("41"));
    snapshot.insert(
        QStringLiteral("entries"),
        QJsonArray{
            snapshotEntryJson(
                40,
                QStringLiteral(
                    "collection/manga-a"),
                QStringLiteral(
                    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
                true),
            snapshotEntryJson(
                33,
                QStringLiteral(
                    "collection/manga-b"),
                QStringLiteral(
                    "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"),
                true)});
    snapshot.insert(
        QStringLiteral("next_page_token"),
        QStringLiteral("opaque-token-2"));
    snapshot.insert(
        QStringLiteral("has_more"),
        true);

    const auto parsed =
        syncWireSnapshotResponseFromJson(
            snapshot);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->serverTimeMs, qint64(7000));
    QCOMPARE(parsed->cursor, quint64(41));
    QCOMPARE(parsed->entries.size(), 2);
    QCOMPARE(
        parsed->entries.at(0).serverSeq,
        quint64(40));
    QVERIFY(parsed->entries.at(0).canonical);
    QCOMPARE(
        parsed->entries.at(1).serverSeq,
        quint64(33));
    QVERIFY(parsed->entries.at(1).canonical);
    QCOMPARE(
        parsed->entries.at(1).mutation.recordKey,
        QStringLiteral("collection/manga-b"));
    QVERIFY(parsed->hasMore);
    QCOMPARE(
        parsed->nextPageToken,
        QStringLiteral("opaque-token-2"));

    QJsonObject finalPage = snapshot;
    finalPage.insert(
        QStringLiteral("entries"),
        QJsonArray{
            snapshotEntryJson(
                12,
                QStringLiteral(
                    "collection/manga-c"),
                QStringLiteral(
                    "cccccccc-cccc-4ccc-8ccc-cccccccccccc"),
                true)});
    finalPage.insert(
        QStringLiteral("next_page_token"),
        QString());
    finalPage.insert(
        QStringLiteral("has_more"),
        false);
    const auto parsedFinal =
        syncWireSnapshotResponseFromJson(
            finalPage);
    QVERIFY(parsedFinal.has_value());
    QVERIFY(!parsedFinal->hasMore);
    QVERIFY(parsedFinal->nextPageToken.isEmpty());
}

void tst_sync_protocol::
snapshotResponseRejectsMalformedRequiredFields() {
    const QJsonArray validEntries{
        snapshotEntryJson(
            40,
            QStringLiteral(
                "collection/manga-a"),
            QStringLiteral(
                "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
            true)};

    QJsonObject valid;
    valid.insert(
        QStringLiteral("server_time_ms"),
        QStringLiteral("7000"));
    valid.insert(
        QStringLiteral("cursor"),
        QStringLiteral("41"));
    valid.insert(
        QStringLiteral("entries"),
        validEntries);
    valid.insert(
        QStringLiteral("has_more"),
        false);

    QVERIFY(
        syncWireSnapshotResponseFromJson(
             valid)
            .has_value());

    QJsonObject missingCursor = valid;
    missingCursor.remove(
        QStringLiteral("cursor"));
    QVERIFY(
        !syncWireSnapshotResponseFromJson(
             missingCursor)
            .has_value());

    QJsonObject malformedCursor = valid;
    malformedCursor.insert(
        QStringLiteral("cursor"),
        QStringLiteral("bogus"));
    QVERIFY(
        !syncWireSnapshotResponseFromJson(
             malformedCursor)
            .has_value());

    QJsonObject missingServerTime = valid;
    missingServerTime.remove(
        QStringLiteral("server_time_ms"));
    QVERIFY(
        !syncWireSnapshotResponseFromJson(
             missingServerTime)
            .has_value());

    QJsonObject entriesNotArray = valid;
    entriesNotArray.insert(
        QStringLiteral("entries"),
        QJsonObject{
            {
                QStringLiteral("value"),
                QStringLiteral("fixture")
            }
        });
    QVERIFY(
        !syncWireSnapshotResponseFromJson(
             entriesNotArray)
            .has_value());

    // Entries reuse the ordinary pull-entry validation: a missing
    // mutation envelope must fail the whole snapshot.
    QJsonObject entryWithoutMutation;
    entryWithoutMutation.insert(
        QStringLiteral("server_seq"),
        QStringLiteral("40"));
    entryWithoutMutation.insert(
        QStringLiteral("won"),
        true);
    QJsonObject malformedEntry = valid;
    malformedEntry.insert(
        QStringLiteral("entries"),
        QJsonArray{entryWithoutMutation});
    QVERIFY(
        !syncWireSnapshotResponseFromJson(
             malformedEntry)
            .has_value());

    // The fixed cursor bounds every entry: a row beyond it would be
    // missed by the ordinary pull that resumes after the cursor.
    QJsonObject entryBeyondCursor = valid;
    entryBeyondCursor.insert(
        QStringLiteral("entries"),
        QJsonArray{
            snapshotEntryJson(
                50,
                QStringLiteral(
                    "collection/manga-a"),
                QStringLiteral(
                    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
                true)});
    QVERIFY(
        !syncWireSnapshotResponseFromJson(
             entryBeyondCursor)
            .has_value());

    // has_more without a usable continuation token cannot be resumed.
    QJsonObject danglingToken = valid;
    danglingToken.insert(
        QStringLiteral("has_more"),
        true);
    QVERIFY(
        !syncWireSnapshotResponseFromJson(
             danglingToken)
            .has_value());
}

void tst_sync_protocol::
accountClientBeginAttachmentPostsAuthenticatedEnvelope() {
    CaptureAccountTransport transport;
    AccountClient client(&transport);
    client.setAccessToken(
        QByteArrayLiteral("fixture-access"));

    const quint64 requestId =
        client.beginProfileAttachment(
            QStringLiteral(
                "12345678-1234-4123-8123-123456789abc"),
            QStringLiteral("legacy_local"),
            QStringLiteral(
                "sha256:0123456789abcdef"));

    QCOMPARE(transport.sendCount, 1);
    QCOMPARE(
        transport.lastRequestId,
        requestId);
    QCOMPARE(
        transport.lastRequest.method,
        QByteArrayLiteral("POST"));
    QCOMPARE(
        transport.lastRequest.path,
        QStringLiteral(
            "/v1/profile/attachments"));
    QCOMPARE(
        transport.lastRequest.bearerToken,
        QByteArrayLiteral("fixture-access"));
    QCOMPARE(
        transport.lastRequest.body
            .value(
                QStringLiteral(
                    "attachment_id"))
            .toString(),
        QStringLiteral(
            "12345678-1234-4123-8123-123456789abc"));
    QCOMPARE(
        transport.lastRequest.body
            .value(
                QStringLiteral(
                    "source_kind"))
            .toString(),
        QStringLiteral("legacy_local"));
    QCOMPARE(
        transport.lastRequest.body
            .value(
                QStringLiteral(
                    "source_semantic_digest"))
            .toString(),
        QStringLiteral(
            "sha256:0123456789abcdef"));
}

void tst_sync_protocol::
accountClientGetAttachmentEncodesPathSegment() {
    CaptureAccountTransport transport;
    AccountClient client(&transport);
    client.setAccessToken(
        QByteArrayLiteral("fixture-access"));

    const quint64 requestId =
        client.getProfileAttachment(
            QStringLiteral("att id/1"));

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
            "/v1/profile/attachments/att%20id%2F1"));
    QCOMPARE(
        transport.lastRequest.bearerToken,
        QByteArrayLiteral("fixture-access"));
    QVERIFY(
        transport.lastRequest.body.isEmpty());
}

void tst_sync_protocol::
accountClientCommitAttachmentPostsToCommitEndpoint() {
    CaptureAccountTransport transport;
    AccountClient client(&transport);
    client.setAccessToken(
        QByteArrayLiteral("fixture-access"));

    const quint64 requestId =
        client.commitProfileAttachment(
            QStringLiteral(
                "12345678-1234-4123-8123-123456789abc"));

    QCOMPARE(transport.sendCount, 1);
    QCOMPARE(
        transport.lastRequestId,
        requestId);
    QCOMPARE(
        transport.lastRequest.method,
        QByteArrayLiteral("POST"));
    QCOMPARE(
        transport.lastRequest.path,
        QStringLiteral(
            "/v1/profile/attachments/"
            "12345678-1234-4123-8123-123456789abc/"
            "commit"));
    QCOMPARE(
        transport.lastRequest.bearerToken,
        QByteArrayLiteral("fixture-access"));
    QVERIFY(
        transport.lastRequest.body.isEmpty());
}

void tst_sync_protocol::
accountClientSnapshotAppendsEncodedAfterKeyOnlyWithToken() {
    CaptureAccountTransport transport;
    AccountClient client(&transport);
    client.setAccessToken(
        QByteArrayLiteral("fixture-access"));

    const quint64 firstRequestId =
        client.pullSyncSnapshot();

    QCOMPARE(transport.sendCount, 1);
    QCOMPARE(
        transport.lastRequestId,
        firstRequestId);
    QCOMPARE(
        transport.lastRequest.method,
        QByteArrayLiteral("GET"));
    QCOMPARE(
        transport.lastRequest.path,
        QStringLiteral("/v1/sync/snapshot"));
    QCOMPARE(
        transport.lastRequest.bearerToken,
        QByteArrayLiteral("fixture-access"));
    QVERIFY(
        transport.lastRequest.body.isEmpty());

    const quint64 nextRequestId =
        client.pullSyncSnapshot(
            QStringLiteral("page two/3"));

    QCOMPARE(transport.sendCount, 2);
    QCOMPARE(
        transport.lastRequestId,
        nextRequestId);
    QCOMPARE(
        transport.lastRequest.method,
        QByteArrayLiteral("GET"));
    QCOMPARE(
        transport.lastRequest.path,
        QStringLiteral(
            "/v1/sync/snapshot"
            "?after_key=page%20two%2F3"));
}

void tst_sync_protocol::
accountClientPushIncludesAttachmentIdOnlyWhenAttached() {
    CaptureAccountTransport transport;
    AccountClient client(&transport);
    client.setAccessToken(
        QByteArrayLiteral("fixture-access"));

    SyncWireMutation mutation;
    mutation.mutationId =
        QStringLiteral(
            "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    mutation.deviceId =
        QStringLiteral(
            "11111111-1111-4111-8111-111111111111");
    mutation.category =
        QStringLiteral("collection");
    mutation.recordKey =
        QStringLiteral("manga/item-1");
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{
        9000,
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
    const QJsonArray mutations{
        syncWireMutationToJson(mutation)};

    client.pushSync(mutations);
    QCOMPARE(transport.sendCount, 1);
    QCOMPARE(
        transport.lastRequest.path,
        QStringLiteral("/v1/sync/push"));
    QVERIFY(
        !transport.lastRequest.body.contains(
            QStringLiteral(
                "attachment_id")));
    QCOMPARE(
        transport.lastRequest.body
            .value(
                QStringLiteral("mutations"))
            .toArray(),
        mutations);

    client.pushSync(
        mutations,
        QStringLiteral(
            "12345678-1234-4123-8123-123456789abc"));
    QCOMPARE(transport.sendCount, 2);
    QCOMPARE(
        transport.lastRequest.path,
        QStringLiteral("/v1/sync/push"));
    QCOMPARE(
        transport.lastRequest.body
            .value(
                QStringLiteral(
                    "attachment_id"))
            .toString(),
        QStringLiteral(
            "12345678-1234-4123-8123-123456789abc"));
    QCOMPARE(
        transport.lastRequest.body
            .value(
                QStringLiteral("mutations"))
            .toArray(),
        mutations);
}

QTEST_MAIN(tst_sync_protocol)
#include "tst_sync_protocol.moc"
