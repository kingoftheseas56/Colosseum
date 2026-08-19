// tst_activity_projector — Slice D1 golden-parity test for the native ActivityProjector
// (native/account/ActivityProjector.h), the C++ port of the "Your Colosseum" activity
// oracle (Preflight-Architect activity-engine reference, activity-reference.js).
//
// Three layers of proof:
//   1. Golden fixture parity — the SAME JSON fixtures the JS oracle's own test suite
//      (test-activity-reference.js) asserts against, loaded verbatim from
//      tests/auto/activity/fixtures/ and compared field-for-field, including array
//      ordering, via a generic recursive comparator (compareJson below) that reports
//      the exact differing path on mismatch rather than a giant unified diff.
//   2. The invalid-cases fixture, proving every listed malformed event is rejected
//      with the exact error message text the JS oracle raises.
//   3. Hand-written natives for parity requirements no fixture expresses on its own:
//      determinism under repeated/shuffled-order projection, and a handful of
//      validation/semantic edge cases the invalid-cases fixture doesn't carry
//      (duplicate pageKey inside one event, out-of-range rateMilli/utcOffsetMinutes,
//      exact-duplicate-eventId idempotency, per-event UTC offset independence, and
//      lifetime completion dedupe against a later re-completion).

#include "account/ActivityProjector.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QtTest>

#include <algorithm>
#include <random>
#include <vector>

namespace {

QString fixturesDir() { return QStringLiteral(ACTIVITY_FIXTURES_DIR); }

QJsonDocument loadJsonFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("tst_activity_projector: cannot open fixture %s", qPrintable(path));
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qFatal("tst_activity_projector: malformed fixture %s: %s",
               qPrintable(path), qPrintable(error.errorString()));
    }
    return doc;
}

// Generic field-for-field JSON comparator. Recurses through objects/arrays and
// reports every mismatch with a dotted/bracketed path (e.g.
// "$.highlights[1].world") so a single wrong field is immediately obvious
// instead of drowning in a giant unified diff.
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

void assertProjectsTo(const QJsonArray &events, const QString &monthKey, const QJsonObject &expected) {
    QJsonObject actual;
    try {
        actual = ActivityProjector::projectMonth(events, monthKey);
    } catch (const ActivityProjector::ValidationError &error) {
        QFAIL(qPrintable(QStringLiteral("unexpected ValidationError: %1").arg(error.what())));
    }

    QStringList diffs;
    compareJson(actual, expected, QStringLiteral("$"), diffs);
    if (!diffs.isEmpty())
        QFAIL(qPrintable(diffs.join(QStringLiteral("\n"))));
}

// UTC epoch ms for a LOCAL wall-clock timestamp at the given offset — i.e. the
// same construction test-activity-reference.js's localMs() helper uses
// (Date.UTC(...) - offset*60000), so fixtures authored here and the JS oracle's
// own inline (non-fixture) tests describe timestamps the same way. Independent
// of ActivityProjector's internal civil-calendar math (QDateTime is a separate
// implementation) — which keeps this a real cross-check rather than a tautology.
qint64 localMs(int year, int month, int day, int hour, int minute, int second, qint64 offsetMinutes = 330) {
    QDateTime dt(QDate(year, month, day), QTime(hour, minute, second), Qt::UTC);
    return dt.toMSecsSinceEpoch() - offsetMinutes * 60000;
}

QJsonObject baseEvent(const QString &type, const QString &eventId, const QString &sessionId,
                      const QString &world, const QString &kind, const QString &titleKey,
                      const QString &itemKey, const QString &title, qint64 utcOffsetMinutes) {
    QJsonObject obj;
    obj.insert("v", 1);
    obj.insert("type", type);
    obj.insert("eventId", eventId);
    obj.insert("sessionId", sessionId);
    obj.insert("world", world);
    obj.insert("kind", kind);
    obj.insert("titleKey", titleKey);
    obj.insert("itemKey", itemKey);
    obj.insert("title", title);
    obj.insert("itemLabel", "");
    obj.insert("cover", "");
    obj.insert("utcOffsetMinutes", utcOffsetMinutes);
    obj.insert("syncable", true);
    obj.insert("source", "test");
    return obj;
}

QJsonObject playbackEvent(const QString &eventId, const QString &sessionId, const QString &world,
                          const QString &kind, const QString &titleKey, const QString &itemKey,
                          const QString &title, qint64 startAtMs, qint64 activeMs,
                          qint64 rateMilli = 1000, qint64 utcOffsetMinutes = 330) {
    QJsonObject obj = baseEvent(QStringLiteral("playback_delta"), eventId, sessionId, world, kind,
                                titleKey, itemKey, title, utcOffsetMinutes);
    obj.insert("startAtMs", startAtMs);
    obj.insert("endAtMs", startAtMs + activeMs);
    obj.insert("activeMs", activeMs);
    obj.insert("rateMilli", rateMilli);
    return obj;
}

QJsonObject readingEvent(const QString &eventId, const QString &sessionId, const QString &world,
                         const QString &kind, const QString &titleKey, const QString &itemKey,
                         const QString &title, qint64 atMs, const QString &readingForm,
                         const QJsonArray &pageKeys, qint64 progressMicros,
                         qint64 utcOffsetMinutes = 330) {
    QJsonObject obj = baseEvent(QStringLiteral("reading_delta"), eventId, sessionId, world, kind,
                                titleKey, itemKey, title, utcOffsetMinutes);
    obj.insert("atMs", atMs);
    obj.insert("readingForm", readingForm);
    obj.insert("pageKeys", pageKeys);
    obj.insert("progressMicros", progressMicros);
    return obj;
}

QJsonObject completionEvent(const QString &eventId, const QString &sessionId, const QString &world,
                            const QString &kind, const QString &titleKey, const QString &itemKey,
                            const QString &title, qint64 atMs, const QString &reason,
                            qint64 utcOffsetMinutes = 330) {
    QJsonObject obj = baseEvent(QStringLiteral("media_completed"), eventId, sessionId, world, kind,
                                titleKey, itemKey, title, utcOffsetMinutes);
    obj.insert("atMs", atMs);
    obj.insert("reason", reason);
    return obj;
}

// A ledger with NO exact-timestamp ties anywhere (every eventTime distinct,
// five distinct titles) — the shape the JS oracle's own tie-break code paths
// (">=" latest-wins updates) are provably order-independent for, so any
// permutation of this array must project identically.
QJsonArray tieFreeLedger() {
    QJsonArray events;
    events.append(playbackEvent("tf-alpha-play", "tf-s1", "theatre", "movie", "movie:alpha",
                                "movie:alpha", "Alpha", localMs(2026, 8, 3, 10, 0, 0), 15000));
    events.append(readingEvent("tf-beta-read", "tf-s2", "tankoban", "manga_chapter", "manga:beta",
                               "chapter:1", "Beta", localMs(2026, 8, 4, 9, 0, 0), "fixed",
                               QJsonArray{"p1", "p2"}, 0));
    events.append(readingEvent("tf-gamma-read", "tf-s3", "biblio", "book", "book:gamma",
                               "book:gamma", "Gamma", localMs(2026, 8, 5, 11, 0, 0), "reflowable",
                               QJsonArray{}, 5000));
    events.append(completionEvent("tf-alpha-done", "tf-s4", "theatre", "movie", "movie:alpha",
                                  "movie:alpha", "Alpha", localMs(2026, 8, 6, 12, 0, 0), "eof"));
    events.append(playbackEvent("tf-delta-play", "tf-s5", "biblio", "audiobook", "book:delta",
                                "audio:delta", "Delta", localMs(2026, 8, 7, 8, 0, 0), 12000));
    events.append(readingEvent("tf-epsilon-read", "tf-s6", "tankoban", "comic_issue",
                               "comic:epsilon", "issue:1", "Epsilon",
                               localMs(2026, 8, 8, 7, 0, 0), "fixed", QJsonArray{"q1"}, 0));
    return events;
}

} // namespace

class tst_activity_projector : public QObject {
    Q_OBJECT

private slots:
    // 1. Golden fixture parity.
    void goldenPair_data();
    void goldenPair();
    void goldenCoverage_data();
    void goldenCoverage();

    // 2. Invalid-cases fixture.
    void invalidCases_data();
    void invalidCases();

    // 3. Hand-written natives.
    void determinismRepeatedProjection();
    void determinismShuffledOrderWhenTieFree();
    void rejectsInvalidMonthKey_data();
    void rejectsInvalidMonthKey();
    void rejectsActiveMsMismatch();
    void rejectsDuplicatePageKeyInEvent();
    void rejectsRateMilliNotPositive();
    void rejectsUtcOffsetOutOfRange();
    void exactDuplicateEventIsIdempotent();
    void utcOffsetIsAppliedPerEventIndependently();
    void lifetimeCompletionDedupeIgnoresLaterReCompletion();
};

void tst_activity_projector::goldenPair_data() {
    QTest::addColumn<QString>("eventsPath");
    QTest::addColumn<QString>("expectedPath");

    const QString dir = fixturesDir();
    QTest::newRow("august-mixed")
        << dir + "/august-mixed-events.json" << dir + "/august-mixed-expected.json";
    QTest::newRow("boundary-dedupe")
        << dir + "/boundary-dedupe-events.json" << dir + "/boundary-dedupe-expected.json";
}

void tst_activity_projector::goldenPair() {
    QFETCH(QString, eventsPath);
    QFETCH(QString, expectedPath);

    const QJsonArray events = loadJsonFile(eventsPath).array();
    const QJsonObject expected = loadJsonFile(expectedPath).object();
    assertProjectsTo(events, expected.value(QStringLiteral("month")).toString(), expected);
}

void tst_activity_projector::goldenCoverage_data() {
    QTest::addColumn<QString>("path");

    const QDir dir(fixturesDir() + "/coverage");
    const QStringList files = dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        if (name == QLatin1String("invalid-cases.json"))
            continue; // handled by invalidCases() — those fixtures expect a thrown error, not a projection
        QTest::newRow(qPrintable(name)) << dir.filePath(name);
    }
}

void tst_activity_projector::goldenCoverage() {
    QFETCH(QString, path);

    const QJsonObject fixture = loadJsonFile(path).object();
    const QJsonArray events = fixture.value(QStringLiteral("events")).toArray();
    const QString month = fixture.value(QStringLiteral("month")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    assertProjectsTo(events, month, expected);
}

void tst_activity_projector::invalidCases_data() {
    QTest::addColumn<QString>("eventsJson");
    QTest::addColumn<QString>("expectedError");

    const QJsonArray cases = loadJsonFile(fixturesDir() + "/coverage/invalid-cases.json").array();
    for (const QJsonValue &caseValue : cases) {
        const QJsonObject fixture = caseValue.toObject();
        QJsonArray events;
        if (fixture.contains(QStringLiteral("events")))
            events = fixture.value(QStringLiteral("events")).toArray();
        else
            events.append(fixture.value(QStringLiteral("event")));

        const QString eventsJson =
            QString::fromUtf8(QJsonDocument(events).toJson(QJsonDocument::Compact));
        QTest::newRow(qPrintable(fixture.value(QStringLiteral("name")).toString()))
            << eventsJson << fixture.value(QStringLiteral("expectedError")).toString();
    }
}

void tst_activity_projector::invalidCases() {
    QFETCH(QString, eventsJson);
    QFETCH(QString, expectedError);

    const QJsonArray events = QJsonDocument::fromJson(eventsJson.toUtf8()).array();

    bool threw = false;
    try {
        ActivityProjector::projectMonth(events, QStringLiteral("2026-08"));
    } catch (const ActivityProjector::ValidationError &error) {
        threw = true;
        const QString message = QString::fromUtf8(error.what());
        QVERIFY2(message.contains(expectedError),
                 qPrintable(QStringLiteral("expected error containing \"%1\", got \"%2\"")
                     .arg(expectedError, message)));
    }
    QVERIFY2(threw, "expected ActivityProjector::ValidationError to be thrown");
}

// --- Determinism (CPP-PORT-CONTRACT §21/§24: output must not depend on hash-
// map iteration order, container internals, or input array order when the
// oracle's own semantics don't depend on it). ---------------------------------

void tst_activity_projector::determinismRepeatedProjection() {
    const QJsonArray events = loadJsonFile(fixturesDir() + "/august-mixed-events.json").array();
    const QJsonObject first = ActivityProjector::projectMonth(events, QStringLiteral("2026-08"));
    const QByteArray firstJson = QJsonDocument(first).toJson(QJsonDocument::Compact);

    for (int i = 0; i < 2; ++i) {
        const QJsonObject again = ActivityProjector::projectMonth(events, QStringLiteral("2026-08"));
        QCOMPARE(QJsonDocument(again).toJson(QJsonDocument::Compact), firstJson);
    }
}

void tst_activity_projector::determinismShuffledOrderWhenTieFree() {
    const QJsonArray events = tieFreeLedger();
    const QJsonObject baseline = ActivityProjector::projectMonth(events, QStringLiteral("2026-08"));
    const QByteArray baselineJson = QJsonDocument(baseline).toJson(QJsonDocument::Compact);

    std::vector<QJsonValue> pool;
    pool.reserve(events.size());
    for (const QJsonValue &value : events)
        pool.push_back(value);

    std::mt19937 rng(20260819u); // fixed seed: reproducible, not a live-clock dependency
    for (int trial = 0; trial < 6; ++trial) {
        std::shuffle(pool.begin(), pool.end(), rng);
        QJsonArray shuffled;
        for (const QJsonValue &value : pool)
            shuffled.append(value);

        const QJsonObject actual = ActivityProjector::projectMonth(shuffled, QStringLiteral("2026-08"));
        QCOMPARE(QJsonDocument(actual).toJson(QJsonDocument::Compact), baselineJson);
    }
}

// --- Validation edge cases the invalid-cases fixture doesn't carry -----------

void tst_activity_projector::rejectsInvalidMonthKey_data() {
    QTest::addColumn<QString>("monthKey");
    QTest::newRow("month-13") << QStringLiteral("2026-13");
    QTest::newRow("month-00") << QStringLiteral("2026-00");
    QTest::newRow("two-digit-year") << QStringLiteral("26-08");
    QTest::newRow("empty") << QString();
    QTest::newRow("no-dash") << QStringLiteral("202608");
}

void tst_activity_projector::rejectsInvalidMonthKey() {
    QFETCH(QString, monthKey);
    bool threw = false;
    try {
        ActivityProjector::projectMonth(QJsonArray(), monthKey);
    } catch (const ActivityProjector::ValidationError &error) {
        threw = true;
        QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("invalid month key")));
    }
    QVERIFY(threw);
}

void tst_activity_projector::rejectsActiveMsMismatch() {
    QJsonObject event = playbackEvent("bad-active", "s1", "theatre", "movie", "movie:x", "movie:x",
                                      "X", localMs(2026, 8, 15, 10, 0, 0), 10000);
    event["activeMs"] = 9999; // no longer endAtMs - startAtMs
    QJsonArray events{event};

    QVERIFY_EXCEPTION_THROWN(
        ActivityProjector::projectMonth(events, QStringLiteral("2026-08")),
        ActivityProjector::ValidationError);
}

void tst_activity_projector::rejectsDuplicatePageKeyInEvent() {
    const QJsonArray events{readingEvent("bad-pagekeys", "s1", "tankoban", "manga_chapter",
                                         "manga:x", "chapter:1", "X",
                                         localMs(2026, 8, 15, 10, 0, 0), "fixed",
                                         QJsonArray{"p1", "p1"}, 0)};

    bool threw = false;
    try {
        ActivityProjector::projectMonth(events, QStringLiteral("2026-08"));
    } catch (const ActivityProjector::ValidationError &error) {
        threw = true;
        QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("duplicate pageKey in event")));
    }
    QVERIFY(threw);
}

void tst_activity_projector::rejectsRateMilliNotPositive() {
    QJsonObject event = playbackEvent("bad-rate", "s1", "theatre", "movie", "movie:x", "movie:x",
                                      "X", localMs(2026, 8, 15, 10, 0, 0), 10000);
    event["rateMilli"] = 0;
    const QJsonArray events{event};

    bool threw = false;
    try {
        ActivityProjector::projectMonth(events, QStringLiteral("2026-08"));
    } catch (const ActivityProjector::ValidationError &error) {
        threw = true;
        QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("invalid rateMilli")));
    }
    QVERIFY(threw);
}

void tst_activity_projector::rejectsUtcOffsetOutOfRange() {
    QJsonObject event = playbackEvent("bad-offset", "s1", "theatre", "movie", "movie:x", "movie:x",
                                      "X", localMs(2026, 8, 15, 10, 0, 0), 10000, 1000, 841);
    const QJsonArray events{event};

    bool threw = false;
    try {
        ActivityProjector::projectMonth(events, QStringLiteral("2026-08"));
    } catch (const ActivityProjector::ValidationError &error) {
        threw = true;
        QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("invalid utcOffsetMinutes")));
    }
    QVERIFY(threw);
}

// Exact duplicate (same eventId, identical payload) must be idempotent: the
// second copy is silently ignored, not double-counted. SEMANTICS.md §2.
void tst_activity_projector::exactDuplicateEventIsIdempotent() {
    const QJsonObject event = playbackEvent("dupe-1", "s1", "theatre", "movie", "movie:x",
                                            "movie:x", "X", localMs(2026, 8, 15, 10, 0, 0), 20000);
    const QJsonArray events{event, event};

    const QJsonObject result = ActivityProjector::projectMonth(events, QStringLiteral("2026-08"));
    QCOMPARE(result.value(QStringLiteral("watchSeconds")).toInt(), 20);
}

// Two playback deltas at the SAME absolute instant but captured under
// different utcOffsetMinutes must localize independently — proving the
// projector reads each event's own offset rather than a shared/global one
// (CPP-PORT-CONTRACT §12: "the tracker must not create one event spanning an
// offset change", and month selection is per-event-offset, not machine-local).
void tst_activity_projector::utcOffsetIsAppliedPerEventIndependently() {
    // 2026-08-31T22:00:00Z:
    //   at +180 minutes offset -> local 2026-09-01T01:00 -> September.
    //   at -180 minutes offset -> local 2026-08-31T19:00 -> still August.
    const qint64 instant = QDateTime(QDate(2026, 8, 31), QTime(22, 0, 0), Qt::UTC).toMSecsSinceEpoch();

    const QJsonObject septemberLocal = playbackEvent(
        "offset-sep", "s-sep", "theatre", "movie", "movie:sep", "movie:sep", "Sep",
        instant, 10000, 1000, 180);
    const QJsonObject augustLocal = playbackEvent(
        "offset-aug", "s-aug", "theatre", "movie", "movie:aug", "movie:aug", "Aug",
        instant, 10000, 1000, -180);
    const QJsonArray events{septemberLocal, augustLocal};

    const QJsonObject august = ActivityProjector::projectMonth(events, QStringLiteral("2026-08"));
    QCOMPARE(august.value(QStringLiteral("watchSeconds")).toInt(), 10); // augustLocal only

    const QJsonObject september = ActivityProjector::projectMonth(events, QStringLiteral("2026-09"));
    QCOMPARE(september.value(QStringLiteral("watchSeconds")).toInt(), 10); // septemberLocal only
}

// Lifetime completion dedupe: a second completion of the same kind+itemKey in
// a LATER month must not increment that later month's completedCount, but it
// remains a real activity moment (active day) there. SEMANTICS.md §5/§6 — no
// coverage fixture carries a second completion event for the same item, so
// this is a native-only case.
void tst_activity_projector::lifetimeCompletionDedupeIgnoresLaterReCompletion() {
    const QJsonObject firstCompletion = completionEvent(
        "complete-first", "s1", "theatre", "movie", "movie:x", "movie:x", "X",
        localMs(2026, 8, 1, 12, 0, 0), "eof");
    const QJsonObject reCompletion = completionEvent(
        "complete-again", "s2", "theatre", "movie", "movie:x", "movie:x", "X",
        localMs(2026, 9, 1, 12, 0, 0), "eof");
    const QJsonArray events{firstCompletion, reCompletion};

    const QJsonObject august = ActivityProjector::projectMonth(events, QStringLiteral("2026-08"));
    QCOMPARE(august.value(QStringLiteral("completedCount")).toInt(), 1);

    const QJsonObject september = ActivityProjector::projectMonth(events, QStringLiteral("2026-09"));
    QCOMPARE(september.value(QStringLiteral("completedCount")).toInt(), 0);
    QCOMPARE(september.value(QStringLiteral("activeDays")).toInt(), 1); // re-completion still an active day
}

QTEST_GUILESS_MAIN(tst_activity_projector)
#include "tst_activity_projector.moc"
