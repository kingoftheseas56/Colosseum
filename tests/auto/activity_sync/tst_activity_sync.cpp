#include "account/ActivityStore.h"
#include "account/ActivitySyncAdapter.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

namespace {
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

QTEST_MAIN(tst_activity_sync)
#include "tst_activity_sync.moc"
