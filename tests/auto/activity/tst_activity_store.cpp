// tst_activity_store — Slice D2 test for the native ActivityStore
// (native/account/ActivityStore.h), the profile-owned SQLite durable ledger
// writer sitting in front of the already-golden-proven ActivityProjector
// (native/account/ActivityProjector.h, tst_activity_projector.cpp).
//
// This test does NOT re-prove projector aggregation semantics — that is
// tst_activity_projector's job and it must stay the sole authority for
// highlight/recent-activity/dedupe math. What this file proves is the SQLite
// store's own contract (CPP-PORT-CONTRACT.md §4/§5/§9 Lane C/§25):
//   - eventId idempotency (exact duplicate succeeds silently; conflicting
//     payload is rejected, never last-write-wins);
//   - malformed facts are rejected BEFORE any database mutation, reusing
//     ActivityProjector's own validation (no second validator to drift);
//   - revision()/changed() only advance on a real accepted mutation;
//   - projectMonth() delegates to the golden-proven projector over the full
//     persisted ledger (one smoke comparison against the same august-mixed
//     fixture pair tst_activity_projector already proves field-for-field);
//   - earliestActivityMonth()/hasFixedCoverage() read back correctly;
//   - clearAll() is transactional and signals correctly;
//   - data survives a close+reopen at the same path, deterministically;
//   - an unhealthy database (unwritable/corrupt path) fails every operation
//     closed — never a fabricated empty-but-successful result.

#include "account/ActivityStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <memory>

namespace {

QString fixturesDir() { return QStringLiteral(ACTIVITY_FIXTURES_DIR); }

QJsonDocument loadJsonFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("tst_activity_store: cannot open fixture %s", qPrintable(path));
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
        qFatal("tst_activity_store: malformed fixture %s: %s",
               qPrintable(path), qPrintable(error.errorString()));
    return doc;
}

// Generic field-for-field JSON comparator — same shape as
// tst_activity_projector.cpp's, duplicated here deliberately: it is test
// infrastructure (a diagnostic diff walker), not projector logic, and this
// file must stay independently readable without cross-including a sibling
// test's internals.
void compareJson(const QJsonValue &actual, const QJsonValue &expected,
                  const QString &path, QStringList &diffs) {
    if (actual.type() != expected.type()) {
        diffs << QStringLiteral("%1: type mismatch (expected type %2, got type %3)")
            .arg(path).arg(int(expected.type())).arg(int(actual.type()));
        return;
    }
    switch (expected.type()) {
    case QJsonValue::Object: {
        const QJsonObject a = actual.toObject();
        const QJsonObject e = expected.toObject();
        QStringList keys = e.keys();
        for (const QString &k : a.keys()) {
            if (!keys.contains(k))
                keys << k;
        }
        std::sort(keys.begin(), keys.end());
        for (const QString &key : keys) {
            const QString childPath = path + QLatin1Char('.') + key;
            if (!e.contains(key)) {
                diffs << QStringLiteral("%1: unexpected field present").arg(childPath);
                continue;
            }
            if (!a.contains(key)) {
                diffs << QStringLiteral("%1: missing field").arg(childPath);
                continue;
            }
            compareJson(a.value(key), e.value(key), childPath, diffs);
        }
        break;
    }
    case QJsonValue::Array: {
        const QJsonArray a = actual.toArray();
        const QJsonArray e = expected.toArray();
        if (a.size() != e.size()) {
            diffs << QStringLiteral("%1: array length mismatch (expected %2, got %3)")
                .arg(path).arg(e.size()).arg(a.size());
        }
        const int n = std::min(a.size(), e.size());
        for (int i = 0; i < n; ++i)
            compareJson(a.at(i), e.at(i), QStringLiteral("%1[%2]").arg(path).arg(i), diffs);
        break;
    }
    case QJsonValue::Double:
        if (actual.toDouble() != expected.toDouble()) {
            diffs << QStringLiteral("%1: expected %2, got %3")
                .arg(path).arg(expected.toDouble()).arg(actual.toDouble());
        }
        break;
    case QJsonValue::String:
        if (actual.toString() != expected.toString()) {
            diffs << QStringLiteral("%1: expected \"%2\", got \"%3\"")
                .arg(path, expected.toString(), actual.toString());
        }
        break;
    case QJsonValue::Bool:
        if (actual.toBool() != expected.toBool()) {
            diffs << QStringLiteral("%1: expected %2, got %3")
                .arg(path).arg(expected.toBool() ? "true" : "false")
                .arg(actual.toBool() ? "true" : "false");
        }
        break;
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        break;
    }
}

qint64 localMs(int year, int month, int day, int hour, int minute, int second, qint64 offsetMinutes = 330) {
    QDateTime dt(QDate(year, month, day), QTime(hour, minute, second), Qt::UTC);
    return dt.toMSecsSinceEpoch() - offsetMinutes * 60000;
}

QVariantMap baseFact(const QString &sessionId, const QString &world, const QString &kind,
                      const QString &titleKey, const QString &itemKey, const QString &title,
                      qint64 utcOffsetMinutes = 330, const QString &eventId = QString()) {
    QVariantMap m;
    if (!eventId.isEmpty())
        m.insert(QStringLiteral("eventId"), eventId);
    m.insert(QStringLiteral("sessionId"), sessionId);
    m.insert(QStringLiteral("world"), world);
    m.insert(QStringLiteral("kind"), kind);
    m.insert(QStringLiteral("titleKey"), titleKey);
    m.insert(QStringLiteral("itemKey"), itemKey);
    m.insert(QStringLiteral("title"), title);
    m.insert(QStringLiteral("itemLabel"), QString());
    m.insert(QStringLiteral("cover"), QString());
    m.insert(QStringLiteral("utcOffsetMinutes"), utcOffsetMinutes);
    m.insert(QStringLiteral("syncable"), true);
    m.insert(QStringLiteral("source"), QStringLiteral("test"));
    return m;
}

QVariantMap playbackFact(const QString &sessionId, const QString &world, const QString &kind,
                          const QString &titleKey, const QString &itemKey, const QString &title,
                          qint64 startAtMs, qint64 activeMs, qint64 rateMilli = 1000,
                          qint64 utcOffsetMinutes = 330, const QString &eventId = QString()) {
    QVariantMap m = baseFact(sessionId, world, kind, titleKey, itemKey, title, utcOffsetMinutes, eventId);
    m.insert(QStringLiteral("startAtMs"), startAtMs);
    m.insert(QStringLiteral("endAtMs"), startAtMs + activeMs);
    m.insert(QStringLiteral("activeMs"), activeMs);
    m.insert(QStringLiteral("rateMilli"), rateMilli);
    return m;
}

QVariantMap readingFact(const QString &sessionId, const QString &world, const QString &kind,
                         const QString &titleKey, const QString &itemKey, const QString &title,
                         qint64 atMs, const QString &readingForm, const QStringList &pageKeys,
                         qint64 progressMicros, qint64 utcOffsetMinutes = 330,
                         const QString &eventId = QString()) {
    QVariantMap m = baseFact(sessionId, world, kind, titleKey, itemKey, title, utcOffsetMinutes, eventId);
    m.insert(QStringLiteral("atMs"), atMs);
    m.insert(QStringLiteral("readingForm"), readingForm);
    m.insert(QStringLiteral("pageKeys"), pageKeys);
    m.insert(QStringLiteral("progressMicros"), progressMicros);
    return m;
}

QVariantMap completionFact(const QString &sessionId, const QString &world, const QString &kind,
                            const QString &titleKey, const QString &itemKey, const QString &title,
                            qint64 atMs, const QString &reason, qint64 utcOffsetMinutes = 330,
                            const QString &eventId = QString()) {
    QVariantMap m = baseFact(sessionId, world, kind, titleKey, itemKey, title, utcOffsetMinutes, eventId);
    m.insert(QStringLiteral("atMs"), atMs);
    m.insert(QStringLiteral("reason"), reason);
    return m;
}

// Records every event from a golden fixture's raw JSON array (as authored for
// ActivityProjector's own fixtures — same shape, "type" decides the sink)
// through the store's public record*() seam, exactly as a real caller would.
void recordAllFrom(ActivityStore &store, const QJsonArray &events) {
    for (const QJsonValue &value : events) {
        const QJsonObject obj = value.toObject();
        const QString type = obj.value(QStringLiteral("type")).toString();
        const QVariantMap fact = obj.toVariantMap();
        bool ok = false;
        if (type == QLatin1String("playback_delta"))
            ok = store.recordPlaybackDelta(fact);
        else if (type == QLatin1String("reading_delta"))
            ok = store.recordReadingDelta(fact);
        else if (type == QLatin1String("media_completed"))
            ok = store.recordCompletion(fact);
        QVERIFY2(ok, qPrintable(QStringLiteral("failed to record fixture event %1")
            .arg(obj.value(QStringLiteral("eventId")).toString())));
    }
}

} // namespace

class tst_activity_store : public QObject {
    Q_OBJECT

private slots:
    void exactDuplicateEventIsIdempotent();
    void conflictingDuplicateEventIsRejected();
    void malformedFactRejectedBeforeMutation();
    void revisionAndChangedSemantics();
    void projectMonthGoldenSmoke();
    void earliestActivityMonthTracksLedger();
    void hasFixedCoveragePositiveAcrossSessions();
    void hasFixedCoverageNegativeMissingPage();
    void hasFixedCoverageVacuousWhenNoneRequired();
    void clearAllIsTransactionalAndSignals();
    void restartPersistsAndProjectsDeterministically();
    void portableSyncFactsExportOnlySyncableSortedAndDeterministic();
    void portableSyncFactsSanitizeMachineLocalCover();
    void applySyncedPortableFactImportsAndProjects();
    void applySyncedPortableFactIdempotentAgainstRicherLocalPresentation();
    void applySyncedPortableFactConflictDoesNotMutate();
    void applySyncedPortableFactRejectsMalformedBeforeMutation();
    void unhealthyDatabasePath_data();
    void unhealthyDatabasePath();
};

void tst_activity_store::exactDuplicateEventIsIdempotent() {
    ActivityStore store; // default in-memory
    const QVariantMap fact = playbackFact(QStringLiteral("s1"), QStringLiteral("theatre"),
        QStringLiteral("movie"), QStringLiteral("movie:x"), QStringLiteral("movie:x"),
        QStringLiteral("X"), localMs(2026, 8, 15, 10, 0, 0), 20000, 1000, 330,
        QStringLiteral("dupe-1"));

    QSignalSpy changedSpy(&store, &ActivityStore::changed);
    QVERIFY(store.recordPlaybackDelta(fact));
    QCOMPARE(store.revision(), quint64(1));
    QCOMPARE(changedSpy.count(), 1);

    // Exact same payload again — idempotent success, no second row, no bump.
    QVERIFY(store.recordPlaybackDelta(fact));
    QCOMPARE(store.revision(), quint64(1));
    QCOMPARE(changedSpy.count(), 1);

    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 20);
}

void tst_activity_store::conflictingDuplicateEventIsRejected() {
    ActivityStore store;
    const QVariantMap first = playbackFact(QStringLiteral("s1"), QStringLiteral("theatre"),
        QStringLiteral("movie"), QStringLiteral("movie:x"), QStringLiteral("movie:x"),
        QStringLiteral("X"), localMs(2026, 8, 15, 10, 0, 0), 20000, 1000, 330,
        QStringLiteral("conflict-1"));
    QVERIFY(store.recordPlaybackDelta(first));
    QCOMPARE(store.revision(), quint64(1));

    QSignalSpy errorSpy(&store, &ActivityStore::integrityError);
    QSignalSpy changedSpy(&store, &ActivityStore::changed);

    // Same eventId, different activeMs — a conflicting payload.
    QVariantMap conflicting = first;
    conflicting[QStringLiteral("activeMs")] = 25000;
    conflicting[QStringLiteral("endAtMs")] =
        conflicting[QStringLiteral("startAtMs")].toLongLong() + 25000;

    QVERIFY(!store.recordPlaybackDelta(conflicting));
    QCOMPARE(store.revision(), quint64(1)); // unchanged
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).toString(), QStringLiteral("event_conflict"));

    // The original event's data must be intact (never last-write-wins).
    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 20);
}

void tst_activity_store::malformedFactRejectedBeforeMutation() {
    ActivityStore store;
    QVariantMap fact = playbackFact(QStringLiteral("s1"), QStringLiteral("theatre"),
        QStringLiteral("movie"), QStringLiteral("movie:x"), QStringLiteral("movie:x"),
        QStringLiteral("X"), localMs(2026, 8, 15, 10, 0, 0), 20000);
    fact.remove(QStringLiteral("title")); // required field missing

    QSignalSpy errorSpy(&store, &ActivityStore::integrityError);
    QSignalSpy changedSpy(&store, &ActivityStore::changed);

    QVERIFY(!store.recordPlaybackDelta(fact));
    QCOMPARE(store.revision(), quint64(0));
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).toString(), QStringLiteral("invalid_event"));
    QVERIFY(errorSpy.at(0).at(1).toString().contains(QStringLiteral("invalid title")));

    // Nothing was ever mutated: the ledger is still empty.
    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 0);
    QCOMPARE(projection.value(QStringLiteral("activeDays")).toInt(), 0);
}

void tst_activity_store::revisionAndChangedSemantics() {
    ActivityStore store;
    QSignalSpy changedSpy(&store, &ActivityStore::changed);
    QCOMPARE(store.revision(), quint64(0));

    QVERIFY(store.recordReadingDelta(readingFact(
        QStringLiteral("s1"), QStringLiteral("tankoban"), QStringLiteral("manga_chapter"),
        QStringLiteral("manga:x"), QStringLiteral("chapter:1"), QStringLiteral("X"),
        localMs(2026, 8, 10, 9, 0, 0), QStringLiteral("fixed"),
        QStringList{QStringLiteral("p1")}, 0)));
    QCOMPARE(store.revision(), quint64(1));
    QCOMPARE(changedSpy.count(), 1);

    QVERIFY(store.recordCompletion(completionFact(
        QStringLiteral("s2"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:y"), QStringLiteral("movie:y"), QStringLiteral("Y"),
        localMs(2026, 8, 11, 9, 0, 0), QStringLiteral("eof"))));
    QCOMPARE(store.revision(), quint64(2));
    QCOMPARE(changedSpy.count(), 2);

    QVERIFY(store.clearAll());
    QCOMPARE(store.revision(), quint64(3));
    QCOMPARE(changedSpy.count(), 3);
}

void tst_activity_store::projectMonthGoldenSmoke() {
    ActivityStore store;
    const QJsonArray events = loadJsonFile(fixturesDir() + "/august-mixed-events.json").array();
    const QJsonObject expected = loadJsonFile(fixturesDir() + "/august-mixed-expected.json").object();

    recordAllFrom(store, events);

    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    const QJsonObject actual = QJsonObject::fromVariantMap(projection);

    QStringList diffs;
    compareJson(actual, expected, QStringLiteral("$"), diffs);
    if (!diffs.isEmpty())
        QFAIL(qPrintable(diffs.join(QStringLiteral("\n"))));
}

void tst_activity_store::earliestActivityMonthTracksLedger() {
    ActivityStore store;
    QCOMPARE(store.earliestActivityMonth(), QString()); // empty ledger

    QVERIFY(store.recordCompletion(completionFact(
        QStringLiteral("s1"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:x"), QStringLiteral("movie:x"), QStringLiteral("X"),
        localMs(2026, 9, 1, 12, 0, 0), QStringLiteral("eof"))));
    QCOMPARE(store.earliestActivityMonth(), QStringLiteral("2026-09"));

    // An earlier playback delta (by startAtMs) pulls the earliest month back.
    QVERIFY(store.recordPlaybackDelta(playbackFact(
        QStringLiteral("s2"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:y"), QStringLiteral("movie:y"), QStringLiteral("Y"),
        localMs(2026, 6, 1, 8, 0, 0), 10000)));
    QCOMPARE(store.earliestActivityMonth(), QStringLiteral("2026-06"));

    // A LATER event never moves it forward again.
    QVERIFY(store.recordCompletion(completionFact(
        QStringLiteral("s3"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:z"), QStringLiteral("movie:z"), QStringLiteral("Z"),
        localMs(2026, 10, 1, 12, 0, 0), QStringLiteral("eof"))));
    QCOMPARE(store.earliestActivityMonth(), QStringLiteral("2026-06"));
}

void tst_activity_store::hasFixedCoveragePositiveAcrossSessions() {
    ActivityStore store;
    QVERIFY(!store.hasFixedCoverage(QStringLiteral("manga_chapter"), QStringLiteral("chapter:1"),
                                     QVariantList{QStringLiteral("p1"), QStringLiteral("p2")}));

    QVERIFY(store.recordReadingDelta(readingFact(
        QStringLiteral("session-a"), QStringLiteral("tankoban"), QStringLiteral("manga_chapter"),
        QStringLiteral("manga:x"), QStringLiteral("chapter:1"), QStringLiteral("X"),
        localMs(2026, 8, 1, 9, 0, 0), QStringLiteral("fixed"),
        QStringList{QStringLiteral("p1")}, 0)));
    QVERIFY(!store.hasFixedCoverage(QStringLiteral("manga_chapter"), QStringLiteral("chapter:1"),
                                     QVariantList{QStringLiteral("p1"), QStringLiteral("p2")}));

    // A DIFFERENT, later session reads the remaining page — coverage is over
    // all retained history, not scoped to one session (§9 Lane C).
    QVERIFY(store.recordReadingDelta(readingFact(
        QStringLiteral("session-b"), QStringLiteral("tankoban"), QStringLiteral("manga_chapter"),
        QStringLiteral("manga:x"), QStringLiteral("chapter:1"), QStringLiteral("X"),
        localMs(2026, 8, 2, 9, 0, 0), QStringLiteral("fixed"),
        QStringList{QStringLiteral("p2")}, 0)));
    QVERIFY(store.hasFixedCoverage(QStringLiteral("manga_chapter"), QStringLiteral("chapter:1"),
                                    QVariantList{QStringLiteral("p1"), QStringLiteral("p2")}));
}

void tst_activity_store::hasFixedCoverageNegativeMissingPage() {
    ActivityStore store;
    QVERIFY(store.recordReadingDelta(readingFact(
        QStringLiteral("s1"), QStringLiteral("tankoban"), QStringLiteral("comic_issue"),
        QStringLiteral("comic:x"), QStringLiteral("issue:1"), QStringLiteral("X"),
        localMs(2026, 8, 1, 9, 0, 0), QStringLiteral("fixed"),
        QStringList{QStringLiteral("p1"), QStringLiteral("p2")}, 0)));

    // A different item's pages must never count toward this item's coverage.
    QVERIFY(!store.hasFixedCoverage(QStringLiteral("comic_issue"), QStringLiteral("issue:2"),
                                     QVariantList{QStringLiteral("p1")}));
    // Missing page p3 keeps coverage false even though p1/p2 are present.
    QVERIFY(!store.hasFixedCoverage(QStringLiteral("comic_issue"), QStringLiteral("issue:1"),
                                     QVariantList{QStringLiteral("p1"), QStringLiteral("p2"),
                                                  QStringLiteral("p3")}));
}

void tst_activity_store::hasFixedCoverageVacuousWhenNoneRequired() {
    ActivityStore store;
    QVERIFY(store.hasFixedCoverage(QStringLiteral("manga_chapter"), QStringLiteral("chapter:none"),
                                    QVariantList{}));
}

void tst_activity_store::clearAllIsTransactionalAndSignals() {
    ActivityStore store;
    QVERIFY(store.recordCompletion(completionFact(
        QStringLiteral("s1"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:x"), QStringLiteral("movie:x"), QStringLiteral("X"),
        localMs(2026, 8, 1, 12, 0, 0), QStringLiteral("eof"))));
    QCOMPARE(store.revision(), quint64(1));
    QCOMPARE(store.earliestActivityMonth(), QStringLiteral("2026-08"));

    QSignalSpy changedSpy(&store, &ActivityStore::changed);
    QVERIFY(store.clearAll());
    QCOMPARE(store.revision(), quint64(2));
    QCOMPARE(changedSpy.count(), 1);

    QCOMPARE(store.earliestActivityMonth(), QString());
    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("completedCount")).toInt(), 0);
    QCOMPARE(projection.value(QStringLiteral("activeDays")).toInt(), 0);
}

void tst_activity_store::restartPersistsAndProjectsDeterministically() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("activity.sqlite"));

    const QJsonArray events = loadJsonFile(fixturesDir() + "/august-mixed-events.json").array();

    QByteArray firstProjectionJson;
    {
        auto store = std::make_unique<ActivityStore>(dbPath);
        QVERIFY(store->healthy());
        recordAllFrom(*store, events);
        const QVariantMap projection = store->projectMonth(QStringLiteral("2026-08"));
        firstProjectionJson =
            QJsonDocument(QJsonObject::fromVariantMap(projection)).toJson(QJsonDocument::Compact);
    } // store destroyed here — connection closed, WAL checkpointed

    QVERIFY(QFile::exists(dbPath));

    auto reopened = std::make_unique<ActivityStore>(dbPath);
    QVERIFY(reopened->healthy());
    // A fresh instance's in-process revision counter restarts at 0 — it is
    // not persisted truth, only a same-process cache-invalidation clock.
    QCOMPARE(reopened->revision(), quint64(0));

    const QVariantMap reopenedProjection = reopened->projectMonth(QStringLiteral("2026-08"));
    const QByteArray reopenedProjectionJson =
        QJsonDocument(QJsonObject::fromVariantMap(reopenedProjection)).toJson(QJsonDocument::Compact);

    QCOMPARE(reopenedProjectionJson, firstProjectionJson);
    QCOMPARE(reopened->earliestActivityMonth(), QStringLiteral("2026-08"));

    // Determinism: projecting again over the reopened, unchanged ledger
    // yields byte-identical output.
    const QVariantMap again = reopened->projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(QJsonDocument(QJsonObject::fromVariantMap(again)).toJson(QJsonDocument::Compact),
             reopenedProjectionJson);
}

void tst_activity_store::portableSyncFactsExportOnlySyncableSortedAndDeterministic() {
    ActivityStore store;

    QVariantMap later = playbackFact(
        QStringLiteral("s-later"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:later"), QStringLiteral("movie:later"), QStringLiteral("Later"),
        localMs(2026, 8, 15, 12, 0, 0), 10000, 1000, 330,
        QStringLiteral("BBBBBBBB-BBBB-4BBB-8BBB-BBBBBBBBBBBB"));
    QVariantMap earlier = readingFact(
        QStringLiteral("s-earlier"), QStringLiteral("tankoban"), QStringLiteral("manga_chapter"),
        QStringLiteral("manga:earlier"), QStringLiteral("chapter:1"), QStringLiteral("Earlier"),
        localMs(2026, 8, 14, 12, 0, 0), QStringLiteral("fixed"),
        QStringList{QStringLiteral("p1")}, 0, 330,
        QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
    QVariantMap localOnly = completionFact(
        QStringLiteral("s-local"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:local"), QStringLiteral("movie:local"), QStringLiteral("Local"),
        localMs(2026, 8, 16, 12, 0, 0), QStringLiteral("eof"), 330,
        QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc"));
    localOnly[QStringLiteral("syncable")] = false;

    QVERIFY(store.recordPlaybackDelta(later));
    QVERIFY(store.recordReadingDelta(earlier));
    QVERIFY(store.recordCompletion(localOnly));

    QString error;
    const QList<QVariantMap> first = store.portableSyncFacts(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(first.size(), 2);
    QCOMPARE(first.at(0).value(QStringLiteral("eventId")).toString(),
             QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
    QCOMPARE(first.at(1).value(QStringLiteral("eventId")).toString(),
             QStringLiteral("BBBBBBBB-BBBB-4BBB-8BBB-BBBBBBBBBBBB"));
    QVERIFY(first.at(0).value(QStringLiteral("syncable")).toBool());
    QVERIFY(first.at(1).value(QStringLiteral("syncable")).toBool());

    QVariantList firstList;
    for (const QVariantMap &fact : first)
        firstList.append(fact);
    const QByteArray firstJson = QJsonDocument(QJsonArray::fromVariantList(firstList))
        .toJson(QJsonDocument::Compact);

    const QList<QVariantMap> second = store.portableSyncFacts(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVariantList secondList;
    for (const QVariantMap &fact : second)
        secondList.append(fact);
    QCOMPARE(QJsonDocument(QJsonArray::fromVariantList(secondList)).toJson(QJsonDocument::Compact),
             firstJson);
}

void tst_activity_store::portableSyncFactsSanitizeMachineLocalCover() {
    ActivityStore store;
    QVariantMap fact = playbackFact(
        QStringLiteral("s1"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:x"), QStringLiteral("movie:x"), QStringLiteral("X"),
        localMs(2026, 8, 15, 10, 0, 0), 20000, 1000, 330,
        QStringLiteral("11111111-1111-4111-8111-111111111111"));
    fact[QStringLiteral("cover")] = QStringLiteral("C:\\Users\\Suprabha\\Pictures\\cover.jpg");
    QVERIFY(store.recordPlaybackDelta(fact));

    QString error;
    const QList<QVariantMap> portable = store.portableSyncFacts(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(portable.size(), 1);
    QCOMPARE(portable.first().value(QStringLiteral("cover")).toString(), QString());
    QCOMPARE(portable.first().value(QStringLiteral("eventId")).toString(),
             QStringLiteral("11111111-1111-4111-8111-111111111111"));
    QCOMPARE(portable.first().value(QStringLiteral("titleKey")).toString(), QStringLiteral("movie:x"));
    QCOMPARE(portable.first().value(QStringLiteral("activeMs")).toLongLong(), qint64(20000));

    const QList<QVariantMap> local = store.historyProjectionFacts();
    QCOMPARE(local.size(), 1);
    QCOMPARE(local.first().value(QStringLiteral("cover")).toString(),
             QStringLiteral("C:\\Users\\Suprabha\\Pictures\\cover.jpg"));
}

void tst_activity_store::applySyncedPortableFactImportsAndProjects() {
    ActivityStore source;
    ActivityStore target;
    QVERIFY(source.recordReadingDelta(readingFact(
        QStringLiteral("s-read"), QStringLiteral("biblio"), QStringLiteral("book"),
        QStringLiteral("book:x"), QStringLiteral("book:x"), QStringLiteral("Book X"),
        localMs(2026, 8, 20, 18, 0, 0), QStringLiteral("reflowable"), QStringList{},
        420000, 330, QStringLiteral("22222222-2222-4222-8222-222222222222"))));
    QVERIFY(source.recordPlaybackDelta(playbackFact(
        QStringLiteral("s-play"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:y"), QStringLiteral("movie:y"), QStringLiteral("Movie Y"),
        localMs(2026, 8, 20, 19, 0, 0), 18000, 1250, 330,
        QStringLiteral("66666666-6666-4666-8666-666666666666"))));
    QVERIFY(source.recordCompletion(completionFact(
        QStringLiteral("s-complete"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:z"), QStringLiteral("movie:z"), QStringLiteral("Movie Z"),
        localMs(2026, 8, 20, 20, 0, 0), QStringLiteral("eof"), 330,
        QStringLiteral("77777777-7777-4777-8777-777777777777"))));

    QString error;
    const QList<QVariantMap> portable = source.portableSyncFacts(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(portable.size(), 3);
    for (const QVariantMap &portableFact : portable)
        QVERIFY2(target.applySyncedPortableFact(portableFact, &error), qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QJsonObject sourceProjection = QJsonObject::fromVariantMap(
        source.projectMonth(QStringLiteral("2026-08")));
    const QJsonObject targetProjection = QJsonObject::fromVariantMap(
        target.projectMonth(QStringLiteral("2026-08")));
    QStringList diffs;
    compareJson(targetProjection, sourceProjection, QStringLiteral("$"), diffs);
    if (!diffs.isEmpty())
        QFAIL(qPrintable(diffs.join(QStringLiteral("\n"))));

    const QList<QVariantMap> imported = target.portableSyncFacts(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(imported, portable);
    QCOMPARE(imported.at(2).value(QStringLiteral("reason")).toString(), QStringLiteral("eof"));
}

void tst_activity_store::applySyncedPortableFactIdempotentAgainstRicherLocalPresentation() {
    ActivityStore store;
    QVariantMap local = playbackFact(
        QStringLiteral("s1"), QStringLiteral("theatre"), QStringLiteral("episode"),
        QStringLiteral("series:x"), QStringLiteral("episode:x:1"), QStringLiteral("Series X"),
        localMs(2026, 8, 21, 20, 0, 0), 15000, 1000, 330,
        QStringLiteral("33333333-3333-4333-8333-333333333333"));
    local[QStringLiteral("cover")] = QStringLiteral("qrc:/covers/series-x.jpg");
    QVERIFY(store.recordPlaybackDelta(local));
    QCOMPARE(store.revision(), quint64(1));

    QString error;
    const QList<QVariantMap> portable = store.portableSyncFacts(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(portable.size(), 1);
    QCOMPARE(portable.first().value(QStringLiteral("cover")).toString(), QString());

    QSignalSpy changedSpy(&store, &ActivityStore::changed);
    QVERIFY2(store.applySyncedPortableFact(portable.first(), &error), qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(store.revision(), quint64(1));
    QCOMPARE(changedSpy.count(), 0);

    const QList<QVariantMap> localFacts = store.historyProjectionFacts();
    QCOMPARE(localFacts.size(), 1);
    QCOMPARE(localFacts.first().value(QStringLiteral("cover")).toString(),
             QStringLiteral("qrc:/covers/series-x.jpg"));
}

void tst_activity_store::applySyncedPortableFactConflictDoesNotMutate() {
    ActivityStore store;
    QVariantMap local = playbackFact(
        QStringLiteral("s1"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:x"), QStringLiteral("movie:x"), QStringLiteral("X"),
        localMs(2026, 8, 22, 20, 0, 0), 10000, 1000, 330,
        QStringLiteral("44444444-4444-4444-8444-444444444444"));
    local[QStringLiteral("cover")] = QStringLiteral("file:///C:/covers/x.jpg");
    QVERIFY(store.recordPlaybackDelta(local));

    QString error;
    const QList<QVariantMap> before = store.portableSyncFacts(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(before.size(), 1);

    QVariantMap conflicting = before.first();
    conflicting[QStringLiteral("activeMs")] = qint64(12000);
    conflicting[QStringLiteral("endAtMs")] =
        conflicting.value(QStringLiteral("startAtMs")).toLongLong() + 12000;

    QSignalSpy changedSpy(&store, &ActivityStore::changed);
    QVERIFY(!store.applySyncedPortableFact(conflicting, &error));
    QCOMPARE(error, QStringLiteral("activity_event_conflict"));
    QCOMPARE(store.revision(), quint64(1));
    QCOMPARE(changedSpy.count(), 0);

    QString afterError;
    const QList<QVariantMap> after = store.portableSyncFacts(&afterError);
    QVERIFY2(afterError.isEmpty(), qPrintable(afterError));
    QCOMPARE(after, before);
}

void tst_activity_store::applySyncedPortableFactRejectsMalformedBeforeMutation() {
    ActivityStore store;
    QVariantMap malformed = playbackFact(
        QStringLiteral("s1"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:x"), QStringLiteral("movie:x"), QStringLiteral("X"),
        localMs(2026, 8, 23, 20, 0, 0), 10000, 1000, 330,
        QStringLiteral("55555555-5555-4555-8555-555555555555"));
    malformed.remove(QStringLiteral("title"));
    malformed[QStringLiteral("v")] = 1;
    malformed[QStringLiteral("type")] = QStringLiteral("playback_delta");

    QSignalSpy changedSpy(&store, &ActivityStore::changed);
    QString error;
    QVERIFY(!store.applySyncedPortableFact(malformed, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.revision(), quint64(0));
    QCOMPARE(changedSpy.count(), 0);

    QString exportError;
    QVERIFY(store.portableSyncFacts(&exportError).isEmpty());
    QVERIFY2(exportError.isEmpty(), qPrintable(exportError));
    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 0);
}

void tst_activity_store::unhealthyDatabasePath_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("directory-as-file") << QStringLiteral("directory");
    QTest::newRow("corrupt-file") << QStringLiteral("corrupt");
}

void tst_activity_store::unhealthyDatabasePath() {
    QFETCH(QString, kind);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString path;
    if (kind == QLatin1String("directory")) {
        path = dir.filePath(QStringLiteral("as-a-directory.sqlite"));
        QVERIFY(QDir().mkpath(path));
    } else {
        path = dir.filePath(QStringLiteral("corrupt.sqlite"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArrayLiteral("not a sqlite database, just garbage bytes"));
        file.close();
    }

    ActivityStore store(path);
    QString error;
    QVERIFY(!store.healthy(&error));
    QVERIFY(!error.isEmpty());

    QSignalSpy errorSpy(&store, &ActivityStore::integrityError);

    QVERIFY(!store.recordPlaybackDelta(playbackFact(
        QStringLiteral("s1"), QStringLiteral("theatre"), QStringLiteral("movie"),
        QStringLiteral("movie:x"), QStringLiteral("movie:x"), QStringLiteral("X"),
        localMs(2026, 8, 1, 9, 0, 0), 10000)));
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).toString(), QStringLiteral("db_unhealthy"));

    // projectMonth() must return an EMPTY map, never a fabricated all-zero
    // "successful" projection (CPP-PORT-CONTRACT §25).
    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QVERIFY(projection.isEmpty());
    QCOMPARE(errorSpy.count(), 2);
    QCOMPARE(errorSpy.at(1).at(0).toString(), QStringLiteral("db_unhealthy"));

    QCOMPARE(store.earliestActivityMonth(), QString());
    QVERIFY(!store.hasFixedCoverage(QStringLiteral("manga_chapter"), QStringLiteral("chapter:1"),
                                     QVariantList{QStringLiteral("p1")}));
    QVERIFY(!store.clearAll());
}

QTEST_GUILESS_MAIN(tst_activity_store)
#include "tst_activity_store.moc"
