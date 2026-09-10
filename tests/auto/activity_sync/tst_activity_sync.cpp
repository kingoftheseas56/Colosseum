#include "account/ActivityStore.h"
#include "account/ActivitySyncAdapter.h"

#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QVariantList>
#include <QtTest>

#include <utility>

namespace {
QVariantMap readingDeltaFact(const QString &eventId) {
    QVariantMap fact;
    fact.insert(QStringLiteral("eventId"), eventId);
    fact.insert(QStringLiteral("sessionId"), QStringLiteral("session-reading"));
    fact.insert(QStringLiteral("world"), QStringLiteral("tankoban"));
    fact.insert(QStringLiteral("kind"), QStringLiteral("manga_chapter"));
    fact.insert(QStringLiteral("titleKey"), QStringLiteral("manga:fixture"));
    fact.insert(QStringLiteral("itemKey"), QStringLiteral("chapter:1"));
    fact.insert(QStringLiteral("title"), QStringLiteral("Fixture Chapter"));
    fact.insert(QStringLiteral("itemLabel"), QString());
    fact.insert(QStringLiteral("cover"), QString());
    fact.insert(QStringLiteral("utcOffsetMinutes"), 0);
    fact.insert(QStringLiteral("syncable"), true);
    fact.insert(QStringLiteral("source"), QStringLiteral("test"));
    fact.insert(QStringLiteral("atMs"), qint64(2000));
    fact.insert(QStringLiteral("readingForm"), QStringLiteral("fixed"));
    fact.insert(QStringLiteral("pageKeys"),
                QVariantList{QStringLiteral("p1"), QStringLiteral("p2")});
    fact.insert(QStringLiteral("progressMicros"), qint64(500000));
    return fact;
}

QVariantMap completionFactHelper(const QString &eventId) {
    QVariantMap fact;
    fact.insert(QStringLiteral("eventId"), eventId);
    fact.insert(QStringLiteral("sessionId"), QStringLiteral("session-completion"));
    fact.insert(QStringLiteral("world"), QStringLiteral("theatre"));
    fact.insert(QStringLiteral("kind"), QStringLiteral("movie"));
    fact.insert(QStringLiteral("titleKey"), QStringLiteral("movie:complete"));
    fact.insert(QStringLiteral("itemKey"), QStringLiteral("movie:complete"));
    fact.insert(QStringLiteral("title"), QStringLiteral("Fixture Completion"));
    fact.insert(QStringLiteral("itemLabel"), QString());
    fact.insert(QStringLiteral("cover"), QString());
    fact.insert(QStringLiteral("utcOffsetMinutes"), 0);
    fact.insert(QStringLiteral("syncable"), true);
    fact.insert(QStringLiteral("source"), QStringLiteral("test"));
    fact.insert(QStringLiteral("atMs"), qint64(3000));
    fact.insert(QStringLiteral("reason"), QStringLiteral("eof"));
    return fact;
}

QVariantMap playbackFact(
    const QString &eventId,
    bool syncable = true) {
    QVariantMap fact;
    fact.insert(QStringLiteral("eventId"), eventId);
    fact.insert(QStringLiteral("sessionId"), QStringLiteral("session-1"));
    fact.insert(QStringLiteral("world"), QStringLiteral("theatre"));
    fact.insert(QStringLiteral("kind"), QStringLiteral("movie"));
    fact.insert(QStringLiteral("titleKey"), QStringLiteral("movie:fixture"));
    fact.insert(QStringLiteral("itemKey"), QStringLiteral("movie:fixture"));
    fact.insert(QStringLiteral("title"), QStringLiteral("Fixture Movie"));
    fact.insert(QStringLiteral("itemLabel"), QString());
    fact.insert(QStringLiteral("cover"), QStringLiteral("C:/machine-local/cover.jpg"));
    fact.insert(QStringLiteral("utcOffsetMinutes"), 0);
    fact.insert(QStringLiteral("syncable"), syncable);
    fact.insert(QStringLiteral("source"), QStringLiteral("test"));
    fact.insert(QStringLiteral("startAtMs"), qint64(1000));
    fact.insert(QStringLiteral("endAtMs"), qint64(6000));
    fact.insert(QStringLiteral("activeMs"), qint64(5000));
    fact.insert(QStringLiteral("rateMilli"), qint64(1000));
    return fact;
}

SyncAdapterRecord exportOnlyRecord(
    ActivitySyncAdapter &adapter) {
    SyncAdapterExport snapshot;
    QString error;
    if (!adapter.exportSnapshot(&snapshot, &error))
        qFatal("activity export failed: %s", qPrintable(error));
    if (snapshot.records.size() != 1)
        qFatal("expected exactly one activity record");
    return snapshot.records.constFirst();
}
} // namespace

class tst_activity_sync final : public QObject {
    Q_OBJECT

private slots:
    void identityAndImmutablePolicy();
    void localSignalsOnlyForSyncableAppends();
    void exportUsesLowercaseEventKeyAndPortablePayload();
    void remotePutIsIdempotentAndDoesNotEcho();
    void rejectsDeleteMalformedKeyIdentitySchemaAndPayloadBeforeMutation();
    void preflightMatchesPortableOwnerSchema();
    void preflightKeepsIntegerBoundariesExact();
    void sharedFixtureMatchesQtProjection();
    void twoDevicesUnionFactsIntoIdenticalProjections();
};

void tst_activity_sync::identityAndImmutablePolicy() {
    ActivityStore store;
    ActivitySyncAdapter adapter(&store);

    QCOMPARE(adapter.categoryId(), QStringLiteral("activity_fact"));
    QCOMPARE(adapter.schemaVersion(), 1);
    QCOMPARE(adapter.revision(), quint64(0));
    QVERIFY(!adapter.missingRecordsAreDeletes());
}

void tst_activity_sync::localSignalsOnlyForSyncableAppends() {
    ActivityStore store;
    ActivitySyncAdapter adapter(&store);
    QSignalSpy spy(&adapter, &SyncAdapter::localMutationAvailable);

    QVERIFY(store.recordPlaybackDelta(
        playbackFact(QStringLiteral("11111111-1111-4111-8111-111111111111"), true)));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(adapter.revision(), quint64(1));

    QVERIFY(store.recordPlaybackDelta(
        playbackFact(QStringLiteral("22222222-2222-4222-8222-222222222222"), false)));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(adapter.revision(), quint64(1));

    QVERIFY(store.clearAll());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(adapter.revision(), quint64(1));
}

void tst_activity_sync::exportUsesLowercaseEventKeyAndPortablePayload() {
    ActivityStore store;
    ActivitySyncAdapter adapter(&store);
    QVERIFY(store.recordPlaybackDelta(
        playbackFact(QStringLiteral("AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA"), true)));

    const SyncAdapterRecord record = exportOnlyRecord(adapter);
    QCOMPARE(record.recordKey, QStringLiteral("activity/aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
    QVERIFY(record.payload.isObject());
    const QJsonObject payload = record.payload.toObject();
    QCOMPARE(payload.value(QStringLiteral("eventId")).toString(),
             QStringLiteral("AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA"));
    QCOMPARE(payload.value(QStringLiteral("syncable")).toBool(), true);
    QCOMPARE(payload.value(QStringLiteral("cover")).toString(), QString());
}

void tst_activity_sync::remotePutIsIdempotentAndDoesNotEcho() {
    ActivityStore source;
    ActivitySyncAdapter sourceAdapter(&source);
    QVERIFY(source.recordPlaybackDelta(
        playbackFact(QStringLiteral("BBBBBBBB-BBBB-4BBB-8BBB-BBBBBBBBBBBB"), true)));
    const SyncAdapterRecord record = exportOnlyRecord(sourceAdapter);

    ActivityStore target;
    ActivitySyncAdapter targetAdapter(&target);
    QSignalSpy spy(&targetAdapter, &SyncAdapter::localMutationAvailable);
    QString error;
    QVERIFY2(targetAdapter.applyRemote(
        record.recordKey,
        SyncWireOperation::Put,
        record.payload,
        1,
        &error), qPrintable(error));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(target.portableSyncFacts().size(), 1);

    error.clear();
    QVERIFY2(targetAdapter.applyRemote(
        record.recordKey,
        SyncWireOperation::Put,
        record.payload,
        1,
        &error), qPrintable(error));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(target.portableSyncFacts().size(), 1);
}

void tst_activity_sync::rejectsDeleteMalformedKeyIdentitySchemaAndPayloadBeforeMutation() {
    ActivityStore source;
    ActivitySyncAdapter sourceAdapter(&source);
    QVERIFY(source.recordPlaybackDelta(
        playbackFact(QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"), true)));
    const SyncAdapterRecord record = exportOnlyRecord(sourceAdapter);

    ActivityStore target;
    ActivitySyncAdapter adapter(&target);
    QString error;

    QVERIFY(!adapter.applyRemote(
        record.recordKey, SyncWireOperation::Delete, QJsonValue(), 1, &error));
    QCOMPARE(target.portableSyncFacts().size(), 0);

    error.clear();
    QVERIFY(!adapter.applyRemote(
        QStringLiteral("activity/not-a-uuid"), SyncWireOperation::Put,
        record.payload, 1, &error));
    QCOMPARE(target.portableSyncFacts().size(), 0);

    error.clear();
    QVERIFY(!adapter.applyRemote(
        QStringLiteral("activity/AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA"), SyncWireOperation::Put,
        record.payload, 1, &error));
    QCOMPARE(target.portableSyncFacts().size(), 0);

    error.clear();
    QVERIFY(!adapter.applyRemote(
        QStringLiteral("activity/bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"), SyncWireOperation::Put,
        record.payload, 1, &error));
    QCOMPARE(target.portableSyncFacts().size(), 0);

    error.clear();
    QVERIFY(!adapter.applyRemote(
        record.recordKey, SyncWireOperation::Put, record.payload, 2, &error));
    QCOMPARE(target.portableSyncFacts().size(), 0);

    QJsonObject malformed = record.payload.toObject();
    malformed.remove(QStringLiteral("title"));
    error.clear();
    QVERIFY(!adapter.applyRemote(
        record.recordKey, SyncWireOperation::Put, malformed, 1, &error));
    QCOMPARE(target.portableSyncFacts().size(), 0);
}

void tst_activity_sync::preflightMatchesPortableOwnerSchema() {
    ActivityStore store;
    ActivitySyncAdapter adapter(&store);
    const QString eventId = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const QString recordKey = QStringLiteral("activity/") + eventId;
    QJsonObject payload = QJsonObject::fromVariantMap(
        playbackFact(eventId));
    payload.insert(QStringLiteral("v"), 1);
    payload.insert(QStringLiteral("type"), QStringLiteral("playback_delta"));

    SyncAdapterValidationError validation;
    QVERIFY2(adapter.validateRemote(
                 recordKey, SyncWireOperation::Put, payload, 1, &validation),
             qPrintable(validation.detail));

    payload.insert(QStringLiteral("syncable"), false);
    validation = {};
    QVERIFY(!adapter.validateRemote(
        recordKey, SyncWireOperation::Put, payload, 1, &validation));
    QCOMPARE(validation.code, QStringLiteral("activity_not_syncable"));

    payload = QJsonObject::fromVariantMap(playbackFact(eventId));
    payload.insert(QStringLiteral("v"), 1);
    payload.insert(QStringLiteral("type"), QStringLiteral("playback_delta"));
    payload.insert(QStringLiteral("atMs"), 1000);
    validation = {};
    QVERIFY(!adapter.validateRemote(
        recordKey, SyncWireOperation::Put, payload, 1, &validation));
    QCOMPARE(validation.code, QStringLiteral("payload_invalid"));
    QCOMPARE(validation.fieldPath, QStringLiteral("atMs"));

    payload = QJsonObject::fromVariantMap(playbackFact(eventId));
    payload.insert(QStringLiteral("v"), 1);
    payload.insert(QStringLiteral("type"), QStringLiteral("playback_delta"));
    payload.insert(QStringLiteral("futureField"), QStringLiteral("must reject"));
    validation = {};
    QVERIFY(!adapter.validateRemote(
        recordKey, SyncWireOperation::Put, payload, 1, &validation));
    QCOMPARE(validation.code, QStringLiteral("payload_invalid"));
    QCOMPARE(validation.fieldPath, QStringLiteral("futureField"));

    payload = QJsonObject::fromVariantMap(playbackFact(eventId));
    payload.insert(QStringLiteral("v"), 1);
    payload.insert(QStringLiteral("type"), QStringLiteral("playback_delta"));
    payload.remove(QStringLiteral("itemLabel"));
    validation = {};
    QVERIFY(!adapter.validateRemote(
        recordKey, SyncWireOperation::Put, payload, 1, &validation));
    QCOMPARE(validation.code, QStringLiteral("payload_invalid"));
    QCOMPARE(validation.fieldPath, QStringLiteral("itemLabel"));
}

void tst_activity_sync::preflightKeepsIntegerBoundariesExact() {
    ActivityStore store;
    ActivitySyncAdapter adapter(&store);
    const QString recordKey = QStringLiteral(
        "activity/aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const auto payloadFor = [](const QByteArray &atMs) {
        const QByteArray json = QByteArrayLiteral(
            "{\"v\":1,\"type\":\"media_completed\","
            "\"eventId\":\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\","
            "\"sessionId\":\"session\",\"world\":\"theatre\","
            "\"kind\":\"movie\",\"titleKey\":\"movie\","
            "\"itemKey\":\"movie\",\"title\":\"Movie\","
            "\"itemLabel\":\"\",\"cover\":\"\",\"source\":\"test\","
            "\"utcOffsetMinutes\":0,\"syncable\":true,"
            "\"atMs\":") + atMs + QByteArrayLiteral(
            ",\"reason\":\"eof\"}");
        return QJsonDocument::fromJson(json).object();
    };

    for (const QByteArray &literal : {
             QByteArrayLiteral("-8640000000000000"),
             QByteArrayLiteral("8640000000000000")}) {
        SyncAdapterValidationError validation;
        QVERIFY2(adapter.validateRemote(
                     recordKey, SyncWireOperation::Put,
                     payloadFor(literal), 1, &validation),
                 qPrintable(validation.detail));
    }

    for (const QByteArray &literal : {
             QByteArrayLiteral("-9223372036854775808"),
             QByteArrayLiteral("9223372036854775807"),
             QByteArrayLiteral("9223372036854775808"),
             QByteArrayLiteral("1.5")}) {
        SyncAdapterValidationError validation;
        const bool accepted = adapter.validateRemote(
            recordKey, SyncWireOperation::Put,
            payloadFor(literal), 1, &validation);
        QVERIFY2(!accepted, literal.constData());
        QCOMPARE(validation.code, QStringLiteral("payload_invalid"));
    }

    const auto playbackPayloadFor = [](
        const QByteArray &startAtMs,
        const QByteArray &endAtMs,
        const QByteArray &activeMs) {
        const QByteArray json = QByteArrayLiteral(
            "{\"v\":1,\"type\":\"playback_delta\","
            "\"eventId\":\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\","
            "\"sessionId\":\"session\",\"world\":\"theatre\","
            "\"kind\":\"movie\",\"titleKey\":\"movie\","
            "\"itemKey\":\"movie\",\"title\":\"Movie\","
            "\"itemLabel\":\"\",\"cover\":\"\",\"source\":\"test\","
            "\"utcOffsetMinutes\":0,\"syncable\":true,"
            "\"startAtMs\":") + startAtMs
            + QByteArrayLiteral(
                ",\"endAtMs\":") + endAtMs
            + QByteArrayLiteral(
                ",\"activeMs\":") + activeMs
            + QByteArrayLiteral(
                ",\"rateMilli\":1000}");
        return QJsonDocument::fromJson(json).object();
    };

    for (const auto &bounds : {
             std::pair<QByteArray, QByteArray>{
                 QByteArrayLiteral("-8640000000000000"),
                 QByteArrayLiteral("-8639999999999999")},
             std::pair<QByteArray, QByteArray>{
                 QByteArrayLiteral("8639999999999999"),
                 QByteArrayLiteral("8640000000000000")}}) {
        const QJsonObject payload = playbackPayloadFor(
            bounds.first, bounds.second, QByteArrayLiteral("1"));
        SyncAdapterValidationError validation;
        QVERIFY2(adapter.validateRemote(
                     recordKey, SyncWireOperation::Put,
                     payload, 1, &validation),
                 qPrintable(validation.detail));
    }

    const QJsonObject overflowingInterval = playbackPayloadFor(
        QByteArrayLiteral("-9223372036854775808"),
        QByteArrayLiteral("9223372036854775807"),
        QByteArrayLiteral("1"));
    SyncAdapterValidationError validation;
    QVERIFY(!adapter.validateRemote(
        recordKey, SyncWireOperation::Put,
        overflowingInterval, 1, &validation));
    QCOMPARE(validation.code, QStringLiteral("payload_invalid"));

    ActivityStore ownerStore;
    ActivitySyncAdapter ownerAdapter(&ownerStore);
    QSignalSpy ownerIntegrity(&ownerStore, &ActivityStore::integrityError);
    QString ownerError;
    const bool ownerApplied = ownerAdapter.applyRemote(
        recordKey, SyncWireOperation::Put,
        playbackPayloadFor(
            QByteArrayLiteral("8639999999999999"),
            QByteArrayLiteral("8640000000000000"),
            QByteArrayLiteral("1")),
        1, &ownerError);
    if (!ownerApplied && ownerIntegrity.count() > 0)
        qDebug() << ownerIntegrity.last().at(0)
                 << ownerIntegrity.last().at(1);
    QVERIFY2(ownerApplied, qPrintable(ownerError));
}

void tst_activity_sync::sharedFixtureMatchesQtProjection() {
#ifndef COLOSSEUM_SHARED_ACTIVITY_FIXTURE_PATH
#error "The shared Activity sync fixture path must be provided by CMake."
#endif
    QFile file(QStringLiteral(COLOSSEUM_SHARED_ACTIVITY_FIXTURE_PATH));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError,
             qPrintable(parseError.errorString()));
    QVERIFY(document.isObject());
    const QJsonObject fixture = document.object();
    QCOMPARE(fixture.value(QStringLiteral("category")).toString(),
             QStringLiteral("activity_fact"));
    QCOMPARE(fixture.value(QStringLiteral("schema_version")).toInt(), 1);

    const QString eventId = QStringLiteral(
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    ActivityStore store;
    ActivitySyncAdapter adapter(&store);
    QVERIFY(store.recordPlaybackDelta(playbackFact(eventId)));
    const SyncAdapterRecord projected = exportOnlyRecord(adapter);

    QCOMPARE(projected.recordKey,
             fixture.value(QStringLiteral("record_key")).toString());
    QCOMPARE(projected.payload,
             fixture.value(QStringLiteral("payload")));

    SyncAdapterValidationError validation;
    QVERIFY2(adapter.validateRemote(
                 projected.recordKey,
                 SyncWireOperation::Put,
                 projected.payload,
                 fixture.value(QStringLiteral("schema_version")).toInt(),
                 &validation),
             qPrintable(validation.detail));
}

// Wave 3 Lead final proof: two devices that each hold part of the same
// Activity history — including one fact recorded independently on both —
// exchange their portable exports through the sync adapter seam and
// reconstruct byte-identical "Your Colosseum" projections, with the shared
// fact totalled exactly once. The server-side halves of the same proof
// (cross-device duplicate stays one row; unified pull returns it once at its
// original server_seq) are covered by the N-10/N-11 account-service tests.
void tst_activity_sync::twoDevicesUnionFactsIntoIdenticalProjections() {
    ActivityStore deviceA;
    ActivitySyncAdapter adapterA(&deviceA);
    ActivityStore deviceB;
    ActivitySyncAdapter adapterB(&deviceB);

    const QString sharedEventId =
        QStringLiteral("42424242-4242-4242-8242-424242424242");
    QVERIFY(deviceA.recordPlaybackDelta(playbackFact(sharedEventId)));
    QVERIFY(deviceB.recordPlaybackDelta(playbackFact(sharedEventId)));
    QVERIFY(deviceA.recordReadingDelta(readingDeltaFact(
        QStringLiteral("43434343-4343-4343-8343-434343434343"))));
    QVERIFY(deviceB.recordCompletion(completionFactHelper(
        QStringLiteral("44444444-4444-4444-8444-444444444444"))));

    QString error;
    SyncAdapterExport exportA;
    QVERIFY2(adapterA.exportSnapshot(&exportA, &error), qPrintable(error));
    SyncAdapterExport exportB;
    QVERIFY2(adapterB.exportSnapshot(&exportB, &error), qPrintable(error));
    QCOMPARE(exportA.records.size(), 2);
    QCOMPARE(exportB.records.size(), 2);

    for (const SyncAdapterRecord &record : std::as_const(exportA.records)) {
        error.clear();
        QVERIFY2(adapterB.applyRemote(
                     record.recordKey, SyncWireOperation::Put,
                     record.payload, 1, &error),
                 qPrintable(error));
    }
    for (const SyncAdapterRecord &record : std::as_const(exportB.records)) {
        error.clear();
        QVERIFY2(adapterA.applyRemote(
                     record.recordKey, SyncWireOperation::Put,
                     record.payload, 1, &error),
                 qPrintable(error));
    }

    QCOMPARE(deviceA.portableSyncFacts().size(), 3);
    QCOMPARE(deviceB.portableSyncFacts().size(), 3);

    const QVariantMap projectionA =
        deviceA.projectMonth(QStringLiteral("1970-01"));
    const QVariantMap projectionB =
        deviceB.projectMonth(QStringLiteral("1970-01"));
    QCOMPARE(projectionA, projectionB);

    // The shared playback fact (5000 ms) must be totalled exactly once — a
    // doubled watch time here is the classic duplicate-union failure.
    QCOMPARE(projectionA.value(QStringLiteral("watchSeconds")).toLongLong(),
             qint64(5));
    QCOMPARE(projectionB.value(QStringLiteral("watchSeconds")).toLongLong(),
             qint64(5));
    QCOMPARE(projectionA.value(QStringLiteral("pagesRead")).toLongLong(),
             qint64(2));
    QCOMPARE(projectionA.value(QStringLiteral("completedCount")).toLongLong(),
             qint64(1));
    QCOMPARE(projectionA.value(QStringLiteral("activeDays")).toLongLong(),
             qint64(1));
}

QTEST_MAIN(tst_activity_sync)
#include "tst_activity_sync.moc"
