// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/SyncAdapterRegistry.h"
#include "account/TankobanReaderPreferencesCodec.h"
#include "account/TankobanReaderPreferencesOwner.h"
#include "account/TankobanReaderPreferencesSyncAdapter.h"

#include <QHash>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

class FakeTankobanPreferencesOwner final
    : public TankobanReaderPreferencesOwner {
    Q_OBJECT

public:
    explicit FakeTankobanPreferencesOwner(
        QObject *parent = nullptr)
        : TankobanReaderPreferencesOwner(
              parent) {}

    quint64 revision() const override {
        return m_revision;
    }

    QStringList tankobanSeriesIds()
        const override {
        QStringList ids =
            m_records.keys();
        ids.sort();
        return ids;
    }

    QVariantMap rawRecord(
        const QString &seriesId)
        const override {
        return m_records.value(
            seriesId);
    }

    bool applySyncedRawRecord(
        const QString &seriesId,
        const QVariantMap &rawRecord,
        QString *error = nullptr) override {
        Q_UNUSED(error);

        if (m_records.value(seriesId)
            == rawRecord) {
            return true;
        }

        m_records.insert(
            seriesId,
            rawRecord);
        ++m_revision;
        ++remoteApplyCalls;

        emit syncedRecordApplied(
            seriesId);
        return true;
    }

    bool removeSyncedRawRecord(
        const QString &seriesId,
        QString *error = nullptr) override {
        Q_UNUSED(error);

        if (!m_records.contains(
                seriesId)) {
            return true;
        }

        m_records.remove(seriesId);
        ++m_revision;
        ++remoteRemoveCalls;

        emit syncedRecordApplied(
            seriesId);
        return true;
    }

    void seed(
        const QString &seriesId,
        const QVariantMap &rawRecord) {
        m_records.insert(
            seriesId,
            rawRecord);
    }

    void putLocal(
        const QString &seriesId,
        const QVariantMap &rawRecord) {
        if (m_records.value(seriesId)
            == rawRecord) {
            return;
        }

        m_records.insert(
            seriesId,
            rawRecord);
        ++m_revision;

        emit localMutationAvailable(
            m_revision);
    }

    int remoteApplyCalls = 0;
    int remoteRemoveCalls = 0;

private:
    quint64 m_revision = 0;
    QHash<QString, QVariantMap>
        m_records;
};

namespace {
QVariantMap approvedRaw(
    const QString &layout =
        QStringLiteral("paired_pages"),
    const QString &order =
        QStringLiteral("rtl")) {
    QVariantMap render;
    render.insert(
        QStringLiteral("brightness"),
        10);
    render.insert(
        QStringLiteral("contrast"),
        -5);
    render.insert(
        QStringLiteral("gamma"),
        120);
    render.insert(
        QStringLiteral("rotation"),
        90);
    render.insert(
        QStringLiteral("autoCrop"),
        true);
    render.insert(
        QStringLiteral("nightFilter"),
        true);

    QVariantMap raw;
    raw.insert(
        QStringLiteral("layout"),
        layout);
    raw.insert(
        QStringLiteral("order"),
        order);
    raw.insert(
        QStringLiteral("zoomPercent"),
        140.0);
    raw.insert(
        QStringLiteral("stripWidthPct"),
        82.0);
    raw.insert(
        QStringLiteral("stripGap"),
        8.0);
    raw.insert(
        QStringLiteral("autoScrollSpeed"),
        1.5);
    raw.insert(
        QStringLiteral("renderProfile"),
        render);
    return raw;
}

QJsonObject canonicalPayload(
    const QString &seriesId,
    const QVariantMap &raw =
        approvedRaw()) {
    QJsonObject payload;
    QString error;

    if (!TankobanReaderPreferencesCodec::
            canonicalPayload(
                seriesId,
                raw,
                &payload,
                &error)) {
        qFatal(
            "Test payload canonicalization failed.");
    }

    return payload;
}
}

class tst_tankoban_preferences_sync final
    : public QObject {
    Q_OBJECT

private slots:
    void legacyRecordCanonicalizesAndExcludesQuality();
    void qualityOnlyRecordDoesNotExport();
    void filesystemSeriesIdentityDoesNotExport();
    void adapterExportsCanonicalKeySchemaAndNoTimestamp();
    void remotePutPreservesQualityAndUnknownLocalFields();
    void remoteDeleteClearsSyncedFieldsButPreservesQuality();
    void remoteDeleteRemovesPurePreferenceRecord();
    void remoteApplyDoesNotEchoLocalMutation();
    void localMutationIsForwarded();
    void duplicateRemoteReplayIsIdempotent();
    void unknownPayloadFieldIsRejected();
    void payloadSeriesMustMatchRecordKey();
    void registryRejectsUntilProductionOwnerGateCloses();
};

void tst_tankoban_preferences_sync::
legacyRecordCanonicalizesAndExcludesQuality() {
    QVariantMap render;
    render.insert(
        QStringLiteral("brightness"),
        999);
    render.insert(
        QStringLiteral("contrast"),
        -999);
    render.insert(
        QStringLiteral("gamma"),
        999);
    render.insert(
        QStringLiteral("rotation"),
        450);
    render.insert(
        QStringLiteral("autoCrop"),
        true);
    render.insert(
        QStringLiteral("nightFilter"),
        true);
    render.insert(
        QStringLiteral("quality"),
        QStringLiteral("best"));

    QVariantMap raw;
    raw.insert(
        QStringLiteral("rm"),
        QStringLiteral("strip"));
    raw.insert(
        QStringLiteral("zoomPercent"),
        0);
    raw.insert(
        QStringLiteral("sw"),
        95);
    raw.insert(
        QStringLiteral("sg"),
        12);
    raw.insert(
        QStringLiteral("autoScrollSpeed"),
        9.0);
    raw.insert(
        QStringLiteral("renderProfile"),
        render);

    QJsonObject payload;
    QString error;
    QVERIFY2(
        TankobanReaderPreferencesCodec::
            canonicalPayload(
                QStringLiteral("series-1"),
                raw,
                &payload,
                &error),
        qPrintable(error));

    QCOMPARE(
        payload.value(
            QStringLiteral("layout"))
            .toString(),
        QStringLiteral("long_strip"));
    QCOMPARE(
        payload.value(
            QStringLiteral("order"))
            .toString(),
        QStringLiteral("rtl"));
    QCOMPARE(
        payload.value(
            QStringLiteral("zoomPercent"))
            .toDouble(),
        100.0);
    QCOMPARE(
        payload.value(
            QStringLiteral("stripWidthPct"))
            .toDouble(),
        95.0);
    QCOMPARE(
        payload.value(
            QStringLiteral("stripGap"))
            .toDouble(),
        12.0);
    QCOMPARE(
        payload.value(
            QStringLiteral("autoScrollSpeed"))
            .toDouble(),
        3.0);

    const QJsonObject normalizedRender =
        payload.value(
                QStringLiteral(
                    "renderProfile"))
            .toObject();

    QCOMPARE(
        normalizedRender.value(
            QStringLiteral("brightness"))
            .toInt(),
        100);
    QCOMPARE(
        normalizedRender.value(
            QStringLiteral("contrast"))
            .toInt(),
        -100);
    QCOMPARE(
        normalizedRender.value(
            QStringLiteral("gamma"))
            .toInt(),
        300);
    QCOMPARE(
        normalizedRender.value(
            QStringLiteral("rotation"))
            .toInt(),
        90);
    QVERIFY(
        normalizedRender.value(
            QStringLiteral("autoCrop"))
            .toBool());
    QVERIFY(
        normalizedRender.value(
            QStringLiteral("nightFilter"))
            .toBool());

    QVERIFY(
        !normalizedRender.contains(
            QStringLiteral("quality")));
    QCOMPARE(
        normalizedRender.size(),
        6);
}

void tst_tankoban_preferences_sync::
qualityOnlyRecordDoesNotExport() {
    FakeTankobanPreferencesOwner owner;

    QVariantMap render;
    render.insert(
        QStringLiteral("quality"),
        QStringLiteral("best"));

    owner.seed(
        QStringLiteral("series-quality"),
        QVariantMap{
            {
                QStringLiteral(
                    "renderProfile"),
                render
            }
        });

    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        adapter.exportSnapshot(
            &snapshot,
            &error),
        qPrintable(error));

    QVERIFY(snapshot.records.isEmpty());
}

void tst_tankoban_preferences_sync::
filesystemSeriesIdentityDoesNotExport() {
    FakeTankobanPreferencesOwner owner;

    owner.seed(
        QStringLiteral(
            "C:\\Private\\volume.cbz"),
        approvedRaw());

    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        adapter.exportSnapshot(
            &snapshot,
            &error),
        qPrintable(error));

    QVERIFY(snapshot.records.isEmpty());

    QVERIFY(
        !TankobanReaderPreferencesCodec::
            isPortableSeriesId(
                QStringLiteral(
                    "../private/volume.cbz")));
}

void tst_tankoban_preferences_sync::
adapterExportsCanonicalKeySchemaAndNoTimestamp() {
    FakeTankobanPreferencesOwner owner;
    owner.seed(
        QStringLiteral("tankoban:α:42"),
        approvedRaw());

    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    QCOMPARE(
        adapter.categoryId(),
        QStringLiteral(
            "tankoban_reader_preferences"));
    QCOMPARE(adapter.schemaVersion(), 1);

    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        adapter.exportSnapshot(
            &snapshot,
            &error),
        qPrintable(error));

    QCOMPARE(snapshot.records.size(), 1);

    const SyncAdapterRecord record =
        snapshot.records.first();

    QCOMPARE(record.localOrderMs, qint64(-1));

    QString decoded;
    QVERIFY(
        TankobanReaderPreferencesCodec::
            decodeRecordKey(
                record.recordKey,
                &decoded));
    QCOMPARE(
        decoded,
        QStringLiteral("tankoban:α:42"));

    const QJsonObject payload =
        record.payload.toObject();
    QCOMPARE(
        payload.value(
            QStringLiteral("seriesId"))
            .toString(),
        decoded);
}

void tst_tankoban_preferences_sync::
remotePutPreservesQualityAndUnknownLocalFields() {
    FakeTankobanPreferencesOwner owner;

    QVariantMap localRender;
    localRender.insert(
        QStringLiteral("brightness"),
        -40);
    localRender.insert(
        QStringLiteral("quality"),
        QStringLiteral("best"));
    localRender.insert(
        QStringLiteral("futureLocalRender"),
        QStringLiteral("keep"));

    QVariantMap existing;
    existing.insert(
        QStringLiteral("rm"),
        QStringLiteral("comic"));
    existing.insert(
        QStringLiteral("sw"),
        60);
    existing.insert(
        QStringLiteral("localProbe"),
        QStringLiteral("keep"));
    existing.insert(
        QStringLiteral("renderProfile"),
        localRender);

    owner.seed(
        QStringLiteral("series-1"),
        existing);

    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    const QJsonObject payload =
        canonicalPayload(
            QStringLiteral("series-1"),
            approvedRaw(
                QStringLiteral("long_strip"),
                QStringLiteral("rtl")));

    QString error;
    QVERIFY2(
        adapter.applyRemote(
            TankobanReaderPreferencesCodec::
                recordKey(
                    QStringLiteral("series-1")),
            SyncWireOperation::Put,
            payload,
            1,
            &error),
        qPrintable(error));

    const QVariantMap stored =
        owner.rawRecord(
            QStringLiteral("series-1"));

    QCOMPARE(
        stored.value(
            QStringLiteral("layout"))
            .toString(),
        QStringLiteral("long_strip"));
    QVERIFY(
        !stored.contains(
            QStringLiteral("rm")));
    QVERIFY(
        !stored.contains(
            QStringLiteral("sw")));
    QCOMPARE(
        stored.value(
            QStringLiteral("localProbe"))
            .toString(),
        QStringLiteral("keep"));

    const QVariantMap render =
        stored.value(
                QStringLiteral(
                    "renderProfile"))
            .toMap();

    QCOMPARE(
        render.value(
            QStringLiteral("quality"))
            .toString(),
        QStringLiteral("best"));
    QCOMPARE(
        render.value(
            QStringLiteral(
                "futureLocalRender"))
            .toString(),
        QStringLiteral("keep"));
    QCOMPARE(
        render.value(
            QStringLiteral("brightness"))
            .toInt(),
        10);
}

void tst_tankoban_preferences_sync::
remoteDeleteClearsSyncedFieldsButPreservesQuality() {
    FakeTankobanPreferencesOwner owner;

    QVariantMap raw =
        approvedRaw();

    QVariantMap render =
        raw.value(
                QStringLiteral(
                    "renderProfile"))
            .toMap();
    render.insert(
        QStringLiteral("quality"),
        QStringLiteral("fast"));
    render.insert(
        QStringLiteral("futureLocalRender"),
        7);
    raw.insert(
        QStringLiteral("renderProfile"),
        render);
    raw.insert(
        QStringLiteral("localProbe"),
        QStringLiteral("keep"));
    raw.insert(
        QStringLiteral("readingMode"),
        QStringLiteral("manga"));

    owner.seed(
        QStringLiteral("series-1"),
        raw);

    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    QString error;
    QVERIFY2(
        adapter.applyRemote(
            TankobanReaderPreferencesCodec::
                recordKey(
                    QStringLiteral("series-1")),
            SyncWireOperation::Delete,
            QJsonValue(),
            1,
            &error),
        qPrintable(error));

    const QVariantMap stored =
        owner.rawRecord(
            QStringLiteral("series-1"));

    QVERIFY(
        !stored.contains(
            QStringLiteral("layout")));
    QVERIFY(
        !stored.contains(
            QStringLiteral("readingMode")));
    QCOMPARE(
        stored.value(
            QStringLiteral("localProbe"))
            .toString(),
        QStringLiteral("keep"));

    const QVariantMap remainingRender =
        stored.value(
                QStringLiteral(
                    "renderProfile"))
            .toMap();
    QCOMPARE(
        remainingRender.value(
            QStringLiteral("quality"))
            .toString(),
        QStringLiteral("fast"));
    QCOMPARE(
        remainingRender.value(
            QStringLiteral(
                "futureLocalRender"))
            .toInt(),
        7);
    QVERIFY(
        !remainingRender.contains(
            QStringLiteral("brightness")));

    QCOMPARE(owner.remoteRemoveCalls, 0);
    QCOMPARE(owner.remoteApplyCalls, 1);
}

void tst_tankoban_preferences_sync::
remoteDeleteRemovesPurePreferenceRecord() {
    FakeTankobanPreferencesOwner owner;
    owner.seed(
        QStringLiteral("series-1"),
        approvedRaw());

    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    QString error;
    QVERIFY2(
        adapter.applyRemote(
            TankobanReaderPreferencesCodec::
                recordKey(
                    QStringLiteral("series-1")),
            SyncWireOperation::Delete,
            QJsonValue(),
            1,
            &error),
        qPrintable(error));

    QVERIFY(
        owner.rawRecord(
            QStringLiteral("series-1"))
            .isEmpty());
    QCOMPARE(owner.remoteRemoveCalls, 1);
}

void tst_tankoban_preferences_sync::
remoteApplyDoesNotEchoLocalMutation() {
    FakeTankobanPreferencesOwner owner;
    owner.seed(
        QStringLiteral("series-1"),
        approvedRaw());

    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    QSignalSpy localSpy(
        &adapter,
        &SyncAdapter::
            localMutationAvailable);
    QSignalSpy importedSpy(
        &owner,
        &TankobanReaderPreferencesOwner::
            syncedRecordApplied);

    const QJsonObject payload =
        canonicalPayload(
            QStringLiteral("series-1"),
            approvedRaw(
                QStringLiteral("long_strip"),
                QStringLiteral("ltr")));

    QString error;
    QVERIFY2(
        adapter.applyRemote(
            TankobanReaderPreferencesCodec::
                recordKey(
                    QStringLiteral("series-1")),
            SyncWireOperation::Put,
            payload,
            1,
            &error),
        qPrintable(error));

    QCOMPARE(localSpy.count(), 0);
    QCOMPARE(importedSpy.count(), 1);
}

void tst_tankoban_preferences_sync::
localMutationIsForwarded() {
    FakeTankobanPreferencesOwner owner;
    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    QSignalSpy localSpy(
        &adapter,
        &SyncAdapter::
            localMutationAvailable);

    owner.putLocal(
        QStringLiteral("series-1"),
        approvedRaw());

    QCOMPARE(localSpy.count(), 1);
    QCOMPARE(
        localSpy.at(0).at(0)
            .toULongLong(),
        owner.revision());
}

void tst_tankoban_preferences_sync::
duplicateRemoteReplayIsIdempotent() {
    FakeTankobanPreferencesOwner owner;
    owner.seed(
        QStringLiteral("series-1"),
        approvedRaw());

    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    const QJsonObject payload =
        canonicalPayload(
            QStringLiteral("series-1"),
            approvedRaw(
                QStringLiteral("single_page"),
                QStringLiteral("rtl")));

    QString error;
    QVERIFY(
        adapter.applyRemote(
            TankobanReaderPreferencesCodec::
                recordKey(
                    QStringLiteral("series-1")),
            SyncWireOperation::Put,
            payload,
            1,
            &error));

    QCOMPARE(owner.remoteApplyCalls, 1);

    QVERIFY(
        adapter.applyRemote(
            TankobanReaderPreferencesCodec::
                recordKey(
                    QStringLiteral("series-1")),
            SyncWireOperation::Put,
            payload,
            1,
            &error));

    QCOMPARE(owner.remoteApplyCalls, 1);
}

void tst_tankoban_preferences_sync::
unknownPayloadFieldIsRejected() {
    FakeTankobanPreferencesOwner owner;
    owner.seed(
        QStringLiteral("series-1"),
        approvedRaw());

    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    QJsonObject payload =
        canonicalPayload(
            QStringLiteral("series-1"));
    payload.insert(
        QStringLiteral("quality"),
        QStringLiteral("best"));

    QString error;
    QVERIFY(
        !adapter.applyRemote(
            TankobanReaderPreferencesCodec::
                recordKey(
                    QStringLiteral("series-1")),
            SyncWireOperation::Put,
            payload,
            1,
            &error));

    QVERIFY(!error.isEmpty());
    QCOMPARE(owner.remoteApplyCalls, 0);
}

void tst_tankoban_preferences_sync::
payloadSeriesMustMatchRecordKey() {
    FakeTankobanPreferencesOwner owner;
    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);

    const QJsonObject payload =
        canonicalPayload(
            QStringLiteral("series-B"));

    QString error;
    QVERIFY(
        !adapter.applyRemote(
            TankobanReaderPreferencesCodec::
                recordKey(
                    QStringLiteral("series-A")),
            SyncWireOperation::Put,
            payload,
            1,
            &error));

    QVERIFY(!error.isEmpty());
    QCOMPARE(owner.remoteApplyCalls, 0);
}

void tst_tankoban_preferences_sync::
registryRejectsUntilProductionOwnerGateCloses() {
    FakeTankobanPreferencesOwner owner;
    TankobanReaderPreferencesSyncAdapter
        adapter(&owner);
    SyncAdapterRegistry registry;

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.registerAdapter(
            &adapter,
            &error));

    QCOMPARE(
        error.code,
        QStringLiteral(
            "category_not_exportable_yet"));
    QVERIFY(
        !registry.contains(
            QStringLiteral(
                "tankoban_reader_preferences")));
}

QTEST_MAIN(tst_tankoban_preferences_sync)
#include "tst_tankoban_preferences_sync.moc"
