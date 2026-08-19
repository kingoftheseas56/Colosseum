// tst_activity_playback_tracker — Slice D3 test for the native
// ActivityPlaybackTracker (native/account/ActivityPlaybackTracker.h), the
// transient reusable sampler that converts wall-clock/media-position samples
// into qualified playback_delta facts (CPP-PORT-CONTRACT.md §8, §22).
//
// This test does NOT re-prove ActivityStore/ActivityProjector persistence or
// aggregation semantics — that is tst_activity_store's and
// tst_activity_projector's job. What this file proves is the TRACKER's own
// sampling/gate/coalescing/completion contract, end to end through a REAL
// ActivityStore (so every emitted fact is proven to pass the same
// validation/idempotency path a live player would exercise) using fully
// injected monotonic/wall/UTC-offset clocks for determinism:
//   - the sampling law's reject-and-reset conditions (pause, stall, >30s gap,
//     rewind, forward-seek-without-discontinuity-call);
//   - rate at 2x still yields wall-clock-equal watched time;
//   - the 10-second activation gate (buffer/discard-below/persist-original-
//     timestamps-on-crossing);
//   - event coalescing and forced splits on rate/UTC-offset change, each
//     split event carrying its own correct rateMilli/utcOffsetMinutes;
//   - guarded 90% completion (movie/episode only, reset by discontinuity(),
//     never for audiobooks) and unconditional naturalEof() -> reason "eof";
//   - identity/session change mid-stream resets tracker state independently
//     per session (no gate/baseline leakage across the switch).
//
// Aggregate assertions (total watchSeconds/activeDays) go through
// ActivityStore::projectMonth() with an in-memory store — the same golden-
// proven ActivityProjector path. Assertions needing to see INDIVIDUAL
// persisted rows (distinct rateMilli/utcOffsetMinutes per split event, or
// original vs. flush-time timestamps) use a file-backed store and a second,
// independent read-only QSqlDatabase connection opened after the store that
// wrote it has been destroyed — ActivityStore itself intentionally exposes no
// raw-row read API.

#include "account/ActivityPlaybackTracker.h"
#include "account/ActivityStore.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QVector>
#include <QtTest>

#include <memory>

namespace {

qint64 localMs(int year, int month, int day, int hour, int minute, int second,
               qint64 offsetMinutes = 330) {
    QDateTime dt(QDate(year, month, day), QTime(hour, minute, second), Qt::UTC);
    return dt.toMSecsSinceEpoch() - offsetMinutes * 60000;
}

QVariantMap identity(const QString &world, const QString &kind, const QString &titleKey,
                      const QString &itemKey, const QString &title) {
    QVariantMap m;
    m.insert(QStringLiteral("world"), world);
    m.insert(QStringLiteral("kind"), kind);
    m.insert(QStringLiteral("titleKey"), titleKey);
    m.insert(QStringLiteral("itemKey"), itemKey);
    m.insert(QStringLiteral("title"), title);
    m.insert(QStringLiteral("itemLabel"), QString());
    m.insert(QStringLiteral("cover"), QString());
    m.insert(QStringLiteral("syncable"), true);
    m.insert(QStringLiteral("source"), QStringLiteral("test"));
    return m;
}

// Deterministic monotonic/wall/UTC-offset clock triple, advanced explicitly by
// each test — the tracker never touches the system clock through this seam.
class FakeClock {
public:
    FakeClock(qint64 startWallMs, int offsetMinutes)
        : m_monotonicMs(0), m_wallMs(startWallMs), m_offsetMinutes(offsetMinutes) {}

    void advance(qint64 deltaMs) {
        m_monotonicMs += deltaMs;
        m_wallMs += deltaMs;
    }

    void setOffsetMinutes(int minutes) { m_offsetMinutes = minutes; }

    ActivityPlaybackTracker::MonotonicClockFn monotonicFn() {
        return [this]() { return m_monotonicMs; };
    }
    ActivityPlaybackTracker::WallClockFn wallFn() {
        return [this]() { return m_wallMs; };
    }
    ActivityPlaybackTracker::UtcOffsetFn offsetFn() {
        return [this]() { return m_offsetMinutes; };
    }

private:
    qint64 m_monotonicMs;
    qint64 m_wallMs;
    int m_offsetMinutes;
};

void wireClock(ActivityPlaybackTracker &tracker, FakeClock &clock) {
    tracker.setMonotonicClock(clock.monotonicFn());
    tracker.setWallClock(clock.wallFn());
    tracker.setUtcOffsetProvider(clock.offsetFn());
}

// One persisted row, read back directly from the SQLite file — used only by
// tests that need to see individual events rather than aggregate projections.
struct RawEvent {
    QString type;
    QString sessionId;
    QString kind;
    QString reason;
    qint64 rateMilli = 0;
    qint64 utcOffsetMinutes = 0;
    qint64 startAtMs = 0;
    qint64 endAtMs = 0;
    qint64 activeMs = 0;
};

QVector<RawEvent> readRawEvents(const QString &dbPath) {
    QVector<RawEvent> out;
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("tracker_inspect"));
        db.setDatabaseName(dbPath);
        if (!db.open())
            return out;

        QSqlQuery query(db);
        query.exec(QStringLiteral(
            "SELECT type, session_id, kind, rate_milli, utc_offset_minutes, "
            "start_at_ms, end_at_ms, active_ms, completion_reason "
            "FROM events ORDER BY COALESCE(start_at_ms, at_ms)"));
        while (query.next()) {
            RawEvent e;
            e.type = query.value(0).toString();
            e.sessionId = query.value(1).toString();
            e.kind = query.value(2).toString();
            e.rateMilli = query.value(3).toLongLong();
            e.utcOffsetMinutes = query.value(4).toLongLong();
            e.startAtMs = query.value(5).toLongLong();
            e.endAtMs = query.value(6).toLongLong();
            e.activeMs = query.value(7).toLongLong();
            e.reason = query.value(8).toString();
            out.append(e);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("tracker_inspect"));
    return out;
}

} // namespace

class tst_activity_playback_tracker : public QObject {
    Q_OBJECT

private slots:
    void twentyMinutesNormalPlaybackTotalsTwelveHundredSeconds();
    void pauseAddsZero();
    void tenMinuteForwardSeekAddsZero();
    void thirtyMinutesAtDoubleSpeedTotalsEighteenHundredSeconds();
    void rewindThenReplayCountsReplay();
    void stallWithNoAdvancementAddsZero();
    void gapOverThirtySecondsDiscarded();
    void subTenSecondSessionDiscardsAll();
    void gateCrossingPersistsOriginalBufferedTimestamps();
    void speedChangeSplitsEventsWithCorrectRatePerEvent();
    void utcOffsetChangeSplitsAndDiscardsBridge();
    void guarded90CrossingEmitsCompletionOnce();
    void discontinuityToNinetyFivePercentDoesNotTriggerCompletion();
    void naturalEofEmitsEofRegardlessOfKind();
    void audiobookNeverEmitsGuarded90();
    void identityChangeMidSessionResetsGateIndependently();
};

void tst_activity_playback_tracker::twentyMinutesNormalPlaybackTotalsTwelveHundredSeconds() {
    ActivityStore store; // in-memory
    QSignalSpy errorSpy(&store, &ActivityStore::integrityError);

    ActivityPlaybackTracker tracker;
    FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
    wireClock(tracker, clock);
    tracker.setSink(&store);

    tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                            QStringLiteral("theatre:x"), QStringLiteral("movie:x"),
                            QStringLiteral("X")),
                  QStringLiteral("session-20min"));

    // Player 1's real five-second cadence — establish the baseline, then 240
    // ticks of 5s real time / 5s content advance at 1x = 20 real minutes.
    tracker.sample(0, 7200000, 1000, true);
    qint64 position = 0;
    for (int i = 0; i < 240; ++i) {
        clock.advance(5000);
        position += 5000;
        tracker.sample(position, 7200000, 1000, true);
    }
    tracker.endSession();

    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 1200);
    QCOMPARE(projection.value(QStringLiteral("activeDays")).toInt(), 1);
    QCOMPARE(errorSpy.count(), 0); // every emitted fact passed validation
}

void tst_activity_playback_tracker::pauseAddsZero() {
    ActivityStore store;
    ActivityPlaybackTracker tracker;
    FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
    wireClock(tracker, clock);
    tracker.setSink(&store);

    tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                            QStringLiteral("theatre:p"), QStringLiteral("movie:p"),
                            QStringLiteral("P")),
                  QStringLiteral("session-pause"));

    tracker.sample(0, 600000, 1000, true);
    clock.advance(5000);
    tracker.sample(5000, 600000, 1000, true); // qualifies 5000ms

    // Paused for 3s wall time — position frozen, consuming=false.
    clock.advance(3000);
    tracker.sample(5000, 600000, 1000, false); // rejected, adds zero

    clock.advance(5000);
    tracker.sample(10000, 600000, 1000, true); // qualifies another 5000ms
    tracker.endSession();

    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 10); // not 13
}

void tst_activity_playback_tracker::tenMinuteForwardSeekAddsZero() {
    ActivityStore store;
    ActivityPlaybackTracker tracker;
    FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
    wireClock(tracker, clock);
    tracker.setSink(&store);

    tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("episode"),
                            QStringLiteral("theatre:s"), QStringLiteral("ep:1"),
                            QStringLiteral("S")),
                  QStringLiteral("session-seek"));

    tracker.sample(0, 3600000, 1000, true);
    clock.advance(5000);
    tracker.sample(5000, 3600000, 1000, true); // qualifies 5000ms

    // A 10-minute forward seek — the host calls discontinuity() with the
    // post-jump position; the bridge must be discarded entirely.
    tracker.discontinuity(605000, 3600000, 1000);

    clock.advance(5000);
    tracker.sample(610000, 3600000, 1000, true); // qualifies 5000ms from new baseline
    tracker.endSession();

    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 10); // not ~615
}

void tst_activity_playback_tracker::thirtyMinutesAtDoubleSpeedTotalsEighteenHundredSeconds() {
    ActivityStore store;
    ActivityPlaybackTracker tracker;
    FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
    wireClock(tracker, clock);
    tracker.setSink(&store);

    tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                            QStringLiteral("theatre:2x"), QStringLiteral("movie:2x"),
                            QStringLiteral("Speed")),
                  QStringLiteral("session-2x"));

    tracker.sample(0, 10800000, 2000, true);
    qint64 position = 0;
    // 360 ticks of 5s real time; content advances 2x -> 10s of position per tick.
    for (int i = 0; i < 360; ++i) {
        clock.advance(5000);
        position += 10000;
        tracker.sample(position, 10800000, 2000, true);
    }
    tracker.endSession();

    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 1800); // 30 real minutes
}

void tst_activity_playback_tracker::rewindThenReplayCountsReplay() {
    ActivityStore store;
    ActivityPlaybackTracker tracker;
    FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
    wireClock(tracker, clock);
    tracker.setSink(&store);

    tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                            QStringLiteral("theatre:r"), QStringLiteral("movie:r"),
                            QStringLiteral("R")),
                  QStringLiteral("session-rewind"));

    tracker.sample(1000, 600000, 1000, true);
    clock.advance(5000);
    tracker.sample(6000, 600000, 1000, true); // qualifies 5000ms

    // Rewind: position moves backward — "no advancement", rejected.
    clock.advance(3000);
    tracker.sample(3000, 600000, 1000, true); // adds zero, baseline resets to 3000

    // Replay forward from the rewound point.
    clock.advance(5000);
    tracker.sample(8000, 600000, 1000, true); // qualifies 5000ms
    tracker.endSession();

    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 10); // 5s + 5s replay, not 14s
}

void tst_activity_playback_tracker::stallWithNoAdvancementAddsZero() {
    ActivityStore store;
    ActivityPlaybackTracker tracker;
    FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
    wireClock(tracker, clock);
    tracker.setSink(&store);

    tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                            QStringLiteral("theatre:st"), QStringLiteral("movie:st"),
                            QStringLiteral("Stall")),
                  QStringLiteral("session-stall"));

    tracker.sample(0, 600000, 1000, true);
    clock.advance(5000);
    tracker.sample(5000, 600000, 1000, true); // qualifies 5000ms

    // Buffering stall: consuming stays true but position does not move.
    clock.advance(4000);
    tracker.sample(5000, 600000, 1000, true); // rejected (no advancement), adds zero

    clock.advance(5000);
    tracker.sample(10000, 600000, 1000, true); // qualifies another 5000ms
    tracker.endSession();

    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 10); // not 14
}

void tst_activity_playback_tracker::gapOverThirtySecondsDiscarded() {
    ActivityStore store;
    ActivityPlaybackTracker tracker;
    FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
    wireClock(tracker, clock);
    tracker.setSink(&store);

    tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                            QStringLiteral("theatre:gap"), QStringLiteral("movie:gap"),
                            QStringLiteral("Gap")),
                  QStringLiteral("session-gap"));

    tracker.sample(0, 600000, 1000, true);
    clock.advance(5000);
    tracker.sample(5000, 600000, 1000, true); // qualifies 5000ms (kept)

    // A >30s observation gap (e.g. app suspended) — even though position also
    // advanced by the same wall amount, the whole interval is discarded.
    clock.advance(35000);
    tracker.sample(40000, 600000, 1000, true); // rejected: wallMs (35000) > 30000

    clock.advance(5000);
    tracker.sample(45000, 600000, 1000, true); // qualifies another 5000ms
    tracker.endSession();

    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 10); // not 45
}

void tst_activity_playback_tracker::subTenSecondSessionDiscardsAll() {
    ActivityStore store;
    ActivityPlaybackTracker tracker;
    FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
    wireClock(tracker, clock);
    tracker.setSink(&store);

    tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                            QStringLiteral("theatre:short"), QStringLiteral("movie:short"),
                            QStringLiteral("Short")),
                  QStringLiteral("session-short"));

    tracker.sample(0, 600000, 1000, true);
    clock.advance(4000);
    tracker.sample(4000, 600000, 1000, true); // qualifies 4000ms — never crosses the 10s gate
    tracker.endSession();

    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 0);
    QCOMPARE(projection.value(QStringLiteral("activeDays")).toInt(), 0);
    QCOMPARE(store.earliestActivityMonth(), QString());
}

void tst_activity_playback_tracker::gateCrossingPersistsOriginalBufferedTimestamps() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("activity.sqlite"));

    {
        ActivityStore store(dbPath);
        QVERIFY(store.healthy());
        QSignalSpy errorSpy(&store, &ActivityStore::integrityError);

        ActivityPlaybackTracker tracker;
        // Start just before local midnight on the 15th so the first buffered
        // chunk is unambiguously an Aug-15 fact.
        FakeClock clock(localMs(2026, 8, 15, 23, 55, 0), 330);
        wireClock(tracker, clock);
        tracker.setSink(&store);

        tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                                QStringLiteral("theatre:gate"), QStringLiteral("movie:gate"),
                                QStringLiteral("Gate")),
                      QStringLiteral("session-gate"));

        tracker.sample(0, 600000, 1000, true);
        clock.advance(6000);
        tracker.sample(6000, 600000, 1000, true); // qualifies 6000ms — below the 10s gate

        // Force-close that first chunk with a brief pause, still on Aug 15.
        clock.advance(1000);
        tracker.sample(6000, 600000, 1000, false); // rejected -> closes+buffers the 6000ms chunk

        // Jump forward over an hour, well past local midnight into Aug 16,
        // before the gate actually crosses.
        clock.advance(3600000);
        tracker.sample(6000, 600000, 1000, false); // still paused — just re-anchors "now"

        clock.advance(5000);
        tracker.sample(11000, 600000, 1000, true); // qualifies 5000ms on Aug 16
        tracker.endSession(); // closes the second chunk: 6000+5000=11000 crosses the gate

        QCOMPARE(errorSpy.count(), 0);
    }

    const QVector<RawEvent> events = readRawEvents(dbPath);
    QCOMPARE(events.size(), 2);
    // Original timestamps preserved: the first chunk still reads as an Aug-15
    // fact even though it was only actually written to disk after the Aug-16
    // jump crossed the activation gate.
    qint64 totalActive = 0;
    for (const RawEvent &e : events)
        totalActive += e.activeMs;
    QCOMPARE(totalActive, qint64(11000));

    ActivityStore reopened(dbPath);
    const QVariantMap projection = reopened.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 11);
    QCOMPARE(projection.value(QStringLiteral("activeDays")).toInt(), 2); // Aug 15 AND Aug 16
}

void tst_activity_playback_tracker::speedChangeSplitsEventsWithCorrectRatePerEvent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("activity.sqlite"));

    {
        ActivityStore store(dbPath);
        QVERIFY(store.healthy());

        ActivityPlaybackTracker tracker;
        FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
        wireClock(tracker, clock);
        tracker.setSink(&store);

        tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                                QStringLiteral("theatre:speed"), QStringLiteral("movie:speed"),
                                QStringLiteral("SpeedSplit")),
                      QStringLiteral("session-speedsplit"));

        tracker.sample(0, 600000, 1000, true);
        clock.advance(5000);
        tracker.sample(5000, 600000, 1000, true); // qualifies 5000ms at 1x

        // Speed change to 2x — flushes the pending 1x time, resets baseline.
        tracker.sample(5000, 600000, 2000, true);

        clock.advance(5000);
        tracker.sample(15000, 600000, 2000, true); // qualifies 5000ms at 2x
        tracker.endSession(); // 5000+5000=10000 crosses the gate, both flushed
    }

    const QVector<RawEvent> events = readRawEvents(dbPath);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).rateMilli, qint64(1000));
    QCOMPARE(events.at(0).activeMs, qint64(5000));
    QCOMPARE(events.at(1).rateMilli, qint64(2000));
    QCOMPARE(events.at(1).activeMs, qint64(5000));
}

void tst_activity_playback_tracker::utcOffsetChangeSplitsAndDiscardsBridge() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("activity.sqlite"));

    {
        ActivityStore store(dbPath);
        QVERIFY(store.healthy());

        ActivityPlaybackTracker tracker;
        FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
        wireClock(tracker, clock);
        tracker.setSink(&store);

        tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                                QStringLiteral("theatre:tz"), QStringLiteral("movie:tz"),
                                QStringLiteral("Timezone")),
                      QStringLiteral("session-tz"));

        tracker.sample(0, 600000, 1000, true);
        clock.advance(5000);
        tracker.sample(5000, 600000, 1000, true); // qualifies 5000ms at offset 330

        // Local UTC offset changes (e.g. system timezone change mid-flight).
        // The bridge sample below must be discarded entirely, not counted at
        // either offset.
        clock.setOffsetMinutes(60);
        clock.advance(2000);
        tracker.sample(7000, 600000, 1000, true); // detected as offset change -> flush+reset, no qualified time this call

        clock.advance(5000);
        tracker.sample(12000, 600000, 1000, true); // qualifies 5000ms at offset 60
        tracker.endSession(); // 5000+5000=10000 crosses the gate
    }

    const QVector<RawEvent> events = readRawEvents(dbPath);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).utcOffsetMinutes, qint64(330));
    QCOMPARE(events.at(0).activeMs, qint64(5000));
    QCOMPARE(events.at(1).utcOffsetMinutes, qint64(60));
    QCOMPARE(events.at(1).activeMs, qint64(5000));

    qint64 totalActive = 0;
    for (const RawEvent &e : events)
        totalActive += e.activeMs;
    QCOMPARE(totalActive, qint64(10000)); // the 2000ms/2000-position bridge never counted
}

void tst_activity_playback_tracker::guarded90CrossingEmitsCompletionOnce() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("activity.sqlite"));

    {
        ActivityStore store(dbPath);
        QVERIFY(store.healthy());

        ActivityPlaybackTracker tracker;
        FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
        wireClock(tracker, clock);
        tracker.setSink(&store);

        tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                                QStringLiteral("theatre:complete"), QStringLiteral("movie:complete"),
                                QStringLiteral("Complete")),
                      QStringLiteral("session-complete"));

        tracker.sample(0, 100000, 1000, true); // baseline fraction 0.0
        clock.advance(1000);
        tracker.sample(1000, 100000, 1000, true); // fraction .01 — no crossing
        clock.advance(1000);
        tracker.sample(89000, 100000, 1000, true); // fraction .89 — still below .9
        clock.advance(1000);
        tracker.sample(95000, 100000, 1000, true); // fraction .95 — crosses .9 -> emits once
        clock.advance(1000);
        tracker.sample(96000, 100000, 1000, true); // fraction .96 — already above, no re-emit
        tracker.naturalEof();
        tracker.endSession();
    }

    const QVector<RawEvent> events = readRawEvents(dbPath);
    int guardedCount = 0;
    int eofCount = 0;
    for (const RawEvent &e : events) {
        if (e.type != QLatin1String("media_completed"))
            continue;
        if (e.reason == QLatin1String("guarded_90_percent"))
            ++guardedCount;
        else if (e.reason == QLatin1String("eof"))
            ++eofCount;
    }
    QCOMPARE(guardedCount, 1);
    QCOMPARE(eofCount, 1); // naturalEof() always fires too, independent of the guarded rule
}

void tst_activity_playback_tracker::discontinuityToNinetyFivePercentDoesNotTriggerCompletion() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("activity.sqlite"));

    {
        ActivityStore store(dbPath);
        QVERIFY(store.healthy());

        ActivityPlaybackTracker tracker;
        FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
        wireClock(tracker, clock);
        tracker.setSink(&store);

        tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("episode"),
                                QStringLiteral("theatre:seek95"), QStringLiteral("ep:seek95"),
                                QStringLiteral("Seek95")),
                      QStringLiteral("session-seek95"));

        tracker.sample(0, 100000, 1000, true); // baseline fraction 0.0

        // Seek directly to 95% — discontinuity() resets the completion
        // baseline to the new fraction, so the "crossing" never happens.
        tracker.discontinuity(95000, 100000, 1000);

        clock.advance(1000);
        tracker.sample(96000, 100000, 1000, true); // fraction .96, baseline already .95
        tracker.endSession();
    }

    const QVector<RawEvent> events = readRawEvents(dbPath);
    int guardedCount = 0;
    for (const RawEvent &e : events) {
        if (e.type == QLatin1String("media_completed")
            && e.reason == QLatin1String("guarded_90_percent"))
            ++guardedCount;
    }
    QCOMPARE(guardedCount, 0);
}

void tst_activity_playback_tracker::naturalEofEmitsEofRegardlessOfKind() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("activity.sqlite"));

    {
        ActivityStore store(dbPath);
        QVERIFY(store.healthy());

        ActivityPlaybackTracker tracker;
        FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
        wireClock(tracker, clock);
        tracker.setSink(&store);

        tracker.begin(identity(QStringLiteral("biblio"), QStringLiteral("audiobook"),
                                QStringLiteral("biblio:book1"), QStringLiteral("pair:book1"),
                                QStringLiteral("Book1")),
                      QStringLiteral("session-audiobook-eof"));

        tracker.sample(0, 50000, 1000, true);
        clock.advance(5000);
        tracker.sample(5000, 50000, 1000, true);
        tracker.naturalEof();
        tracker.endSession();
    }

    const QVector<RawEvent> events = readRawEvents(dbPath);
    int eofCount = 0;
    for (const RawEvent &e : events) {
        if (e.type == QLatin1String("media_completed") && e.reason == QLatin1String("eof"))
            ++eofCount;
    }
    QCOMPARE(eofCount, 1);
}

void tst_activity_playback_tracker::audiobookNeverEmitsGuarded90() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("activity.sqlite"));

    {
        ActivityStore store(dbPath);
        QVERIFY(store.healthy());

        ActivityPlaybackTracker tracker;
        FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
        wireClock(tracker, clock);
        tracker.setSink(&store);

        tracker.begin(identity(QStringLiteral("biblio"), QStringLiteral("audiobook"),
                                QStringLiteral("biblio:book2"), QStringLiteral("pair:book2"),
                                QStringLiteral("Book2")),
                      QStringLiteral("session-audiobook-90"));

        tracker.sample(0, 100000, 1000, true); // fraction 0.0
        clock.advance(1000);
        tracker.sample(89000, 100000, 1000, true); // fraction .89
        clock.advance(1000);
        tracker.sample(95000, 100000, 1000, true); // fraction .95 — would cross for video, must not for audiobook
        tracker.endSession();
    }

    const QVector<RawEvent> events = readRawEvents(dbPath);
    for (const RawEvent &e : events)
        QVERIFY(e.type != QLatin1String("media_completed")
                || e.reason != QLatin1String("guarded_90_percent"));
}

void tst_activity_playback_tracker::identityChangeMidSessionResetsGateIndependently() {
    ActivityStore store; // in-memory
    ActivityPlaybackTracker tracker;
    FakeClock clock(localMs(2026, 8, 15, 10, 0, 0), 330);
    wireClock(tracker, clock);
    tracker.setSink(&store);

    tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                            QStringLiteral("theatre:a"), QStringLiteral("movie:a"),
                            QStringLiteral("A")),
                  QStringLiteral("session-a"));
    tracker.sample(0, 600000, 1000, true);
    clock.advance(8000);
    tracker.sample(8000, 600000, 1000, true); // qualifies 8000ms for A — still below A's own gate

    // Identity/session change mid-stream, WITHOUT an explicit endSession()
    // first — begin() must end session A under its own gate rules (discard,
    // since 8000ms never reached 10000ms) before starting session B fresh.
    tracker.begin(identity(QStringLiteral("theatre"), QStringLiteral("movie"),
                            QStringLiteral("theatre:b"), QStringLiteral("movie:b"),
                            QStringLiteral("B")),
                  QStringLiteral("session-b"));
    tracker.sample(0, 600000, 1000, true);
    clock.advance(3000);
    tracker.sample(3000, 600000, 1000, true); // qualifies 3000ms for B — also below B's own gate
    tracker.endSession();

    // If A's gate state had leaked into B (e.g. gateAccumulatedMs not reset),
    // A's 8000ms + B's 3000ms = 11000ms would have wrongly crossed the gate
    // and persisted B's chunk. Correctly, EACH session's activity independently
    // stayed under its own 10s gate and was discarded.
    const QVariantMap projection = store.projectMonth(QStringLiteral("2026-08"));
    QCOMPARE(projection.value(QStringLiteral("watchSeconds")).toInt(), 0);
    QCOMPARE(store.earliestActivityMonth(), QString());
}

QTEST_GUILESS_MAIN(tst_activity_playback_tracker)
#include "tst_activity_playback_tracker.moc"
