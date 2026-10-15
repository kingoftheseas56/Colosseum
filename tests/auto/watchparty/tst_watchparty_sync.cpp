// Watch Party Slice 3 — deterministic Player 1 synchronization policy tests.
//
// The controller has no mpv/network dependency. FakePlayer is the narrow shipped-player
// seam: a cached position/pause/readiness observation in, seek/pause requests out.
// Explicit integer clock values make drift/cooldown behavior deterministic.

#include "watchparty/WatchPartyPlayerSync.h"

#include <QSignalSpy>
#include <QtTest>

using Colosseum::WatchParty::PlayerSyncController;

namespace {

struct FakePlayer {
    explicit FakePlayer(PlayerSyncController& controller)
        : sync(controller)
    {
        QObject::connect(&sync, &PlayerSyncController::seekRequested,
                         &sync, [this](double requestedPosition) {
            ++seekRequestCount;
            lastSeekPosition = requestedPosition;
        });
        QObject::connect(&sync, &PlayerSyncController::pauseRequested,
                         &sync, [this](bool requestedPaused) {
            ++pauseRequestCount;
            lastPause = requestedPaused;
        });
    }

    void observe(qint64 nowMs)
    {
        sync.observePlayer(position, paused, ready, buffering, seeking, nowMs);
    }

    PlayerSyncController& sync;
    double position = 0.0;
    bool paused = true;
    bool ready = false;
    bool buffering = false;
    bool seeking = false;

    int seekRequestCount = 0;
    double lastSeekPosition = -1.0;
    int pauseRequestCount = 0;
    bool lastPause = true;
};

} // namespace

class tst_watchparty_sync : public QObject
{
    Q_OBJECT

private slots:
    void hostControlRejectsParticipantTimelineIntent();
    void sharedControlAllowsParticipantTimelineIntent();
    void remoteApplicationNeverEchoesAsLocalCommand();
    void lateJoinAppliesAuthoritativePositionAndPlayState();
    void bufferingDoesNotPauseRoomAndCatchesUpWhenViable();
    void smallDriftDoesNotCauseMicroSeek();
    void largeDriftCorrectionIsCooldownBounded();
    void catchUpRetriesPendingDesyncImmediately();
    void localInterruptionDoesNotMutateRoomAndRestoreReconciles();
    void staleAuthoritativeRevisionIsIgnored();
};

void tst_watchparty_sync::hostControlRejectsParticipantTimelineIntent()
{
    PlayerSyncController sync;
    QSignalSpy outbound(&sync, &PlayerSyncController::timelineCommandRequested);
    QSignalSpy rejected(&sync, &PlayerSyncController::controlRejected);

    QVERIFY(sync.activate(QStringLiteral("guest-1"),
                          QStringLiteral("host-1"),
                          QStringLiteral("host")));
    QVERIFY(!sync.canControlTimeline());

    QVERIFY(!sync.requestLocalPlayback(true, 12.0));
    QVERIFY(!sync.requestLocalSeek(18.0));
    QCOMPARE(outbound.count(), 0);
    QCOMPARE(rejected.count(), 2);
    QCOMPARE(rejected.at(0).at(0).toString(),
             QStringLiteral("timeline_control_not_authorized"));
}

void tst_watchparty_sync::sharedControlAllowsParticipantTimelineIntent()
{
    PlayerSyncController sync;
    QSignalSpy outbound(&sync, &PlayerSyncController::timelineCommandRequested);

    QVERIFY(sync.activate(QStringLiteral("guest-1"),
                          QStringLiteral("host-1"),
                          QStringLiteral("shared")));
    QVERIFY(sync.canControlTimeline());

    QVERIFY(sync.requestLocalPlayback(true, 12.25));
    QVERIFY(sync.requestLocalSeek(22.5));

    QCOMPARE(outbound.count(), 2);
    QCOMPARE(outbound.at(0).at(0).toString(), QStringLiteral("play"));
    QCOMPARE(outbound.at(0).at(1).toBool(), true);
    QCOMPARE(outbound.at(0).at(2).toDouble(), 12.25);
    QCOMPARE(outbound.at(1).at(0).toString(), QStringLiteral("seek"));
    QCOMPARE(outbound.at(1).at(2).toDouble(), 22.5);
}

void tst_watchparty_sync::remoteApplicationNeverEchoesAsLocalCommand()
{
    PlayerSyncController sync;
    FakePlayer player(sync);
    QSignalSpy outbound(&sync, &PlayerSyncController::timelineCommandRequested);

    // Shared mode is intentional: if remote application were accidentally routed
    // through requestLocalSeek(), authorization would allow it and this test would
    // expose the echo instead of hiding it behind host-only rejection.
    QVERIFY(sync.activate(QStringLiteral("guest-1"),
                          QStringLiteral("host-1"),
                          QStringLiteral("shared")));

    player.ready = true;
    player.paused = false;
    player.position = 5.0;
    player.observe(1'000);

    QVERIFY(sync.applyAuthoritativeTimeline(true, 20.0, 1, 1'000));
    QCOMPARE(player.seekRequestCount, 1);
    QCOMPARE(player.lastSeekPosition, 20.0);
    QCOMPARE(outbound.count(), 0);

    // Simulate Player 1 applying the authoritative seek, then feeding its ordinary
    // property observation back. Observation is one-way and must stay silent.
    player.position = 20.0;
    player.observe(1'010);
    QCOMPARE(outbound.count(), 0);

    // Negative-control canary: the spy is live and the same participant is allowed
    // to emit an EXPLICIT local seek. If remote application were mutated into this
    // path, the zero-count assertions above would fail.
    QVERIFY(sync.requestLocalSeek(25.0));
    QCOMPARE(outbound.count(), 1);
    QCOMPARE(outbound.at(0).at(0).toString(), QStringLiteral("seek"));
}

void tst_watchparty_sync::lateJoinAppliesAuthoritativePositionAndPlayState()
{
    PlayerSyncController sync;
    FakePlayer player(sync);
    QSignalSpy outbound(&sync, &PlayerSyncController::timelineCommandRequested);

    QVERIFY(sync.activate(QStringLiteral("guest-1"),
                          QStringLiteral("host-1"),
                          QStringLiteral("host")));

    player.ready = true;
    player.paused = true;
    player.position = 0.0;
    player.observe(1'000);

    QVERIFY(sync.applyAuthoritativeTimeline(true, 42.0, 7, 1'000));

    QCOMPARE(player.pauseRequestCount, 1);
    QCOMPARE(player.lastPause, false);
    QCOMPARE(player.seekRequestCount, 1);
    QCOMPARE(player.lastSeekPosition, 42.0);
    QCOMPARE(outbound.count(), 0);
    QCOMPARE(sync.syncStatus(), QStringLiteral("desynced"));
}

void tst_watchparty_sync::bufferingDoesNotPauseRoomAndCatchesUpWhenViable()
{
    PlayerSyncController sync;
    FakePlayer player(sync);
    QSignalSpy outbound(&sync, &PlayerSyncController::timelineCommandRequested);

    QVERIFY(sync.activate(QStringLiteral("guest-1"),
                          QStringLiteral("host-1"),
                          QStringLiteral("host")));

    player.ready = true;
    player.paused = false;
    player.position = 10.0;
    player.buffering = true;
    player.observe(1'000);

    QVERIFY(sync.applyAuthoritativeTimeline(true, 10.0, 1, 1'000));
    player.observe(4'000);

    QCOMPARE(sync.syncStatus(), QStringLiteral("buffering"));
    QCOMPARE(player.pauseRequestCount, 0);
    QCOMPARE(player.seekRequestCount, 0);
    QCOMPARE(outbound.count(), 0);

    // Three room-playing seconds elapsed while this participant remained at 10s.
    // Becoming viable again performs a local catch-up; it never pauses the room.
    player.buffering = false;
    player.observe(4'000);

    QCOMPARE(player.seekRequestCount, 1);
    QCOMPARE(player.lastSeekPosition, 13.0);
    QCOMPARE(outbound.count(), 0);
}

void tst_watchparty_sync::smallDriftDoesNotCauseMicroSeek()
{
    PlayerSyncController sync;
    FakePlayer player(sync);

    QVERIFY(sync.activate(QStringLiteral("host-1"),
                          QStringLiteral("host-1"),
                          QStringLiteral("host")));

    player.ready = true;
    player.paused = false;
    player.position = 10.6;
    player.observe(1'000);

    QVERIFY(sync.applyAuthoritativeTimeline(true, 10.0, 1, 1'000));

    QCOMPARE(player.seekRequestCount, 0);
    QCOMPARE(sync.syncStatus(), QStringLiteral("inSync"));
    QVERIFY(qAbs(sync.driftSeconds() - 0.6) < 0.001);
}

void tst_watchparty_sync::largeDriftCorrectionIsCooldownBounded()
{
    PlayerSyncController sync;
    FakePlayer player(sync);

    QVERIFY(sync.activate(QStringLiteral("host-1"),
                          QStringLiteral("host-1"),
                          QStringLiteral("host")));

    player.ready = true;
    player.paused = false;
    player.position = 8.0;
    player.observe(1'000);

    QVERIFY(sync.applyAuthoritativeTimeline(true, 10.0, 1, 1'000));
    QCOMPARE(player.seekRequestCount, 1);
    QCOMPARE(player.lastSeekPosition, 10.0);

    // The player has not settled yet. A normal position sample inside the 2s retry
    // cooldown must not produce a seek storm.
    player.observe(1'500);
    QCOMPARE(player.seekRequestCount, 1);

    // Once cooldown expires, a still-broken player may be corrected again using
    // the extrapolated authoritative timeline.
    player.observe(3'001);
    QCOMPARE(player.seekRequestCount, 2);
    QVERIFY(qAbs(player.lastSeekPosition - 12.001) < 0.001);
}

void tst_watchparty_sync::catchUpRetriesPendingDesyncImmediately()
{
    PlayerSyncController sync;
    FakePlayer player(sync);

    QVERIFY(sync.activate(QStringLiteral("host-1"),
                          QStringLiteral("host-1"),
                          QStringLiteral("host")));

    player.ready = true;
    player.paused = false;
    player.position = 8.0;
    player.observe(1'000);

    QVERIFY(sync.applyAuthoritativeTimeline(true, 10.0, 1, 1'000));
    QCOMPARE(player.seekRequestCount, 1);

    // A normal observation inside cooldown cannot retry.
    player.observe(1'100);
    QCOMPARE(player.seekRequestCount, 1);
    QVERIFY(sync.catchUpAvailable());

    // The explicit Catch Up action is allowed to retry immediately and uses the
    // current extrapolated room position rather than the stale original target.
    QVERIFY(sync.catchUp(1'100));
    QCOMPARE(player.seekRequestCount, 2);
    QVERIFY(qAbs(player.lastSeekPosition - 10.1) < 0.001);
}

void tst_watchparty_sync::localInterruptionDoesNotMutateRoomAndRestoreReconciles()
{
    PlayerSyncController sync;
    FakePlayer player(sync);
    QSignalSpy outbound(&sync, &PlayerSyncController::timelineCommandRequested);

    QVERIFY(sync.activate(QStringLiteral("guest-1"),
                          QStringLiteral("host-1"),
                          QStringLiteral("host")));

    player.ready = false; // minimize/resume-overlay/startup-style local interruption
    player.paused = true;
    player.position = 30.0;
    player.observe(1'000);

    QVERIFY(sync.applyAuthoritativeTimeline(true, 30.0, 1, 1'000));
    player.observe(4'000);
    QCOMPARE(player.seekRequestCount, 0);
    QCOMPARE(player.pauseRequestCount, 0);
    QCOMPARE(outbound.count(), 0);

    player.ready = true;
    player.observe(4'000);

    QCOMPARE(player.pauseRequestCount, 1);
    QCOMPARE(player.lastPause, false);
    QCOMPARE(player.seekRequestCount, 1);
    QCOMPARE(player.lastSeekPosition, 33.0);
    QCOMPARE(outbound.count(), 0);
}

void tst_watchparty_sync::staleAuthoritativeRevisionIsIgnored()
{
    PlayerSyncController sync;
    FakePlayer player(sync);

    QVERIFY(sync.activate(QStringLiteral("host-1"),
                          QStringLiteral("host-1"),
                          QStringLiteral("host")));

    player.ready = true;
    player.paused = true;
    player.position = 5.0;
    player.observe(1'000);

    QVERIFY(sync.applyAuthoritativeTimeline(false, 5.0, 10, 1'000));
    QVERIFY(!sync.applyAuthoritativeTimeline(true, 99.0, 9, 1'100));

    QCOMPARE(sync.authoritativeRevision(), qulonglong(10));
    QCOMPARE(sync.authoritativePlaying(), false);
    QCOMPARE(sync.authoritativePositionSeconds(), 5.0);
    QCOMPARE(player.seekRequestCount, 0);
    QCOMPARE(player.pauseRequestCount, 0);
}

QTEST_APPLESS_MAIN(tst_watchparty_sync)
#include "tst_watchparty_sync.moc"
