#include "account/ActivityStore.h"
#include "account/ConsumptionHistoryBridge.h"
#include "account/HistoryStore.h"
#include "ProgressStore.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {
QVariantMap fact(const QString &id, const QString &type, qint64 at, bool syncable = true) {
    QVariantMap m{{QStringLiteral("eventId"), id}, {QStringLiteral("sessionId"), QStringLiteral("s")},
        {QStringLiteral("world"), QStringLiteral("theatre")}, {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("titleKey"), QStringLiteral("movie:t")}, {QStringLiteral("itemKey"), QStringLiteral("movie:i")},
        {QStringLiteral("title"), QStringLiteral("Movie")}, {QStringLiteral("itemLabel"), QString()},
        {QStringLiteral("cover"), QString()}, {QStringLiteral("utcOffsetMinutes"), 330},
        {QStringLiteral("syncable"), syncable}, {QStringLiteral("source"), QStringLiteral("test")}};
    m.insert(QStringLiteral("atMs"), at);
    if (type == QLatin1String("playback_delta")) {
        m.insert(QStringLiteral("startAtMs"), at);
        m.insert(QStringLiteral("endAtMs"), at + 1000);
        m.insert(QStringLiteral("activeMs"), 1000);
        m.insert(QStringLiteral("rateMilli"), 1000);
    } else if (type == QLatin1String("reading_delta")) {
        m.insert(QStringLiteral("world"), QStringLiteral("tankoban"));
        m.insert(QStringLiteral("kind"), QStringLiteral("tankoban_volume"));
        m.insert(QStringLiteral("titleKey"), QStringLiteral("tankoban:t"));
        m.insert(QStringLiteral("itemKey"), QStringLiteral("tankoban:i"));
        m.insert(QStringLiteral("title"), QStringLiteral("Tankoban"));
        m.insert(QStringLiteral("readingForm"), QStringLiteral("fixed"));
        m.insert(QStringLiteral("pageKeys"), QVariantList{QStringLiteral("p1")});
        m.insert(QStringLiteral("progressMicros"), 500000);
    } else {
        m.insert(QStringLiteral("reason"), QStringLiteral("eof"));
    }
    return m;
}
}

class tst_consumption_history final : public QObject {
    Q_OBJECT
private slots:
    void playbackDeltaProjectsFirstAndLastActivity();
    void readingDeltaProjectsActivity();
    void completionProjectsCompletedAt();
    void progressMovieThresholdMarksHistoryCompleted();
    void progressEpisodeThresholdMarksHistoryCompletedOnce();
    void exactDuplicateActivityDoesNotDoubleProject();
    void localOnlyFactDoesNotEnterPortableHistory();
    void existingActivityLedgerReplaysIntoEmptyHistory();
    void replayIsIdempotentAcrossRestart();
    void clearRemovesActivityBeforeHistoryAndDoesNotTouchProgress();
};

void tst_consumption_history::playbackDeltaProjectsFirstAndLastActivity() {
    QTemporaryDir d; QVERIFY(d.isValid()); ActivityStore a; HistoryStore h(d.filePath("history.ini")); ProgressStore p(d.filePath("progress.ini")); ConsumptionHistoryBridge b(&a, &p, &h);
    QVERIFY(a.recordPlaybackDelta(fact("p1", "playback_delta", 2000)));
    QVERIFY(a.recordPlaybackDelta(fact("p2", "playback_delta", 1000)));
    const auto projectionFacts = a.historyProjectionFacts();
    QCOMPARE(projectionFacts.size(), 2);
    QVERIFY(std::any_of(projectionFacts.cbegin(), projectionFacts.cend(), [](const QVariantMap &event) {
        return event.value(QStringLiteral("eventId")).toString() == QStringLiteral("p1");
    }));
    QVERIFY(std::any_of(projectionFacts.cbegin(), projectionFacts.cend(), [](const QVariantMap &event) {
        return event.value(QStringLiteral("eventId")).toString() == QStringLiteral("p2");
    }));
    const auto r = h.get("movie", "movie:i");
    QCOMPARE(r.value("firstActivityAt").toLongLong(), 1000LL);
    QCOMPARE(r.value("lastActivityAt").toLongLong(), 3000LL);
}

void tst_consumption_history::readingDeltaProjectsActivity() {
    QTemporaryDir d; QVERIFY(d.isValid()); ActivityStore a; HistoryStore h(d.filePath("history.ini")); ProgressStore p(d.filePath("progress.ini")); ConsumptionHistoryBridge b(&a, &p, &h);
    QVERIFY(a.recordReadingDelta(fact("r1", "reading_delta", 3000)));
    QCOMPARE(h.get("tankoban_volume", "tankoban:i").value("lastActivityAt").toLongLong(), 3000LL);
}

void tst_consumption_history::completionProjectsCompletedAt() {
    QTemporaryDir d; QVERIFY(d.isValid()); ActivityStore a; HistoryStore h(d.filePath("history.ini")); ProgressStore p(d.filePath("progress.ini")); ConsumptionHistoryBridge b(&a, &p, &h);
    QVERIFY(a.recordCompletion(fact("c1", "media_completed", 4000)));
    QVERIFY(h.completed("movie", "movie:i"));
    QCOMPARE(h.get("movie", "movie:i").value("completedAt").toLongLong(), 4000LL);
}

void tst_consumption_history::progressMovieThresholdMarksHistoryCompleted() {
    QTemporaryDir d; QVERIFY(d.isValid()); ActivityStore a; HistoryStore h(d.filePath("history.ini"));
    ProgressStore p(d.filePath("progress.ini")); ConsumptionHistoryBridge b(&a, &p, &h);
    p.record({{"kind", "video"}, {"id", "movie:threshold"}, {"progress", 0.42}});
    p.record({{"kind", "video"}, {"id", "movie:threshold"}, {"progress", 0.90}});
    QVERIFY(h.completed("movie", "movie:threshold"));
    QVERIFY(h.get("movie", "movie:threshold").value(QStringLiteral("completedAt")).toLongLong() > 0);
}

void tst_consumption_history::progressEpisodeThresholdMarksHistoryCompletedOnce() {
    QTemporaryDir d; QVERIFY(d.isValid()); ActivityStore a; HistoryStore h(d.filePath("history.ini"));
    ProgressStore p(d.filePath("progress.ini")); ConsumptionHistoryBridge b(&a, &p, &h);
    QSignalSpy spy(&h, &HistoryStore::syncDirty);
    p.record({{"kind", "video"}, {"id", "series:1:1"}, {"progress", 0.42}});
    p.record({{"kind", "video"}, {"id", "series:1:1"}, {"progress", 0.90}});
    p.record({{"kind", "video"}, {"id", "series:1:1"}, {"progress", 0.95}});
    QVERIFY(h.completed("episode", "series:1:1"));
    QCOMPARE(spy.count(), 1);
}

void tst_consumption_history::exactDuplicateActivityDoesNotDoubleProject() {
    QTemporaryDir d; QVERIFY(d.isValid()); ActivityStore a; HistoryStore h(d.filePath("history.ini")); ProgressStore p(d.filePath("progress.ini")); ConsumptionHistoryBridge b(&a, &p, &h);
    QSignalSpy spy(&h, &HistoryStore::syncDirty); const auto f = fact("same", "playback_delta", 1000);
    QVERIFY(a.recordPlaybackDelta(f)); QVERIFY(a.recordPlaybackDelta(f));
    QCOMPARE(h.revision(), 1); QCOMPARE(spy.count(), 1);
}

void tst_consumption_history::localOnlyFactDoesNotEnterPortableHistory() {
    QTemporaryDir d; QVERIFY(d.isValid()); ActivityStore a; HistoryStore h(d.filePath("history.ini")); ProgressStore p(d.filePath("progress.ini")); ConsumptionHistoryBridge b(&a, &p, &h);
    QVERIFY(a.recordPlaybackDelta(fact("local", "playback_delta", 1000, false)));
    QVERIFY(h.records().isEmpty());
}

void tst_consumption_history::existingActivityLedgerReplaysIntoEmptyHistory() {
    QTemporaryDir d; QVERIFY(d.isValid()); ActivityStore a(d.filePath("activity.sqlite"));
    QVERIFY(a.recordReadingDelta(fact("r1", "reading_delta", 7000)));
    HistoryStore h(d.filePath("history.ini")); ProgressStore p(d.filePath("progress.ini")); ConsumptionHistoryBridge b(&a, &p, &h);
    QVERIFY(b.replayExisting()); QVERIFY(!h.get("tankoban_volume", "tankoban:i").isEmpty());
}

void tst_consumption_history::replayIsIdempotentAcrossRestart() {
    QTemporaryDir d; QVERIFY(d.isValid()); ActivityStore a(d.filePath("activity.sqlite"));
    QVERIFY(a.recordCompletion(fact("c1", "media_completed", 8000)));
    HistoryStore h(d.filePath("history.ini")); ProgressStore p(d.filePath("progress.ini")); ConsumptionHistoryBridge b(&a, &p, &h);
    QVERIFY(b.replayExisting()); const int rev = h.revision();
    ConsumptionHistoryBridge b2(&a, &p, &h); QVERIFY(b2.replayExisting()); QCOMPARE(h.revision(), rev);
}

void tst_consumption_history::clearRemovesActivityBeforeHistoryAndDoesNotTouchProgress() {
    QTemporaryDir d; QVERIFY(d.isValid()); ActivityStore a; HistoryStore h(d.filePath("history.ini")); ProgressStore p(d.filePath("progress.ini"));
    p.record({{"id", "keep"}, {"kind", "movie"}, {"progress", 0.5}});
    ConsumptionHistoryBridge b(&a, &p, &h); QVERIFY(a.recordReadingDelta(fact("r1", "reading_delta", 9000)));
    QVERIFY(b.clearAll()); QVERIFY(a.historyProjectionFacts().isEmpty()); QVERIFY(h.records().isEmpty());
    const auto kept = p.get(QStringLiteral("movie"), QStringLiteral("keep"));
    QVERIFY(!kept.isEmpty());
    QCOMPARE(kept.value(QStringLiteral("id")).toString(), QStringLiteral("keep"));
    QCOMPARE(kept.value(QStringLiteral("kind")).toString(), QStringLiteral("movie"));
    QCOMPARE(kept.value(QStringLiteral("progress")).toDouble(), 0.5);
}

QTEST_MAIN(tst_consumption_history)
#include "tst_consumption_history.moc"
