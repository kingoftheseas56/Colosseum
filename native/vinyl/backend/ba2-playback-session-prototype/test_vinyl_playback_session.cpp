#include "VinylPlaybackSession.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <limits>
#include <optional>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;

    qCritical().noquote() << "FAIL:" << message;
    std::exit(1);
}

template <typename T>
T mustQueue(const vinyl::QueueResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustDomain(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustSession(const vinyl::PlaybackSessionResult<T> &result,
              const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::QueueOccurrenceId occurrenceId(const char *value)
{
    return mustQueue(
        vinyl::QueueOccurrenceId::parse(QString::fromLatin1(value)),
        "fixture occurrence ID should parse");
}

vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
        "fixture Recording MBID should parse");
}

vinyl::QueueOccurrence occurrence(const char *id,
                                  const vinyl::RecordingMbid &recordingMbid)
{
    return mustQueue(
        vinyl::QueueOccurrence::create(
            occurrenceId(id),
            recordingMbid),
        "fixture queue occurrence should construct");
}

void requireState(const vinyl::PlaybackSession &session,
                  vinyl::PlaybackState expected,
                  const char *message)
{
    require(session.state() == expected, message);
}

void requireActiveId(const vinyl::PlaybackSession &session,
                     const char *expected)
{
    require(session.activeOccurrence().has_value(),
            "session should retain an active occurrence");
    require(session.activeOccurrence()->occurrenceId().value()
                == QString::fromLatin1(expected),
            "session active occurrence should match expected identity");
}

void requireStale(const vinyl::PlaybackMutationStatus &status,
                  const char *message)
{
    require(!status.ok, message);
    require(status.error.code
                == vinyl::PlaybackSessionErrorCode::StaleAsyncWork,
            "stale work should use StaleAsyncWork");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    const RecordingMbid a =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid b =
        recording("22222222-2222-4222-8222-222222222222");

    PlaybackSession session;
    requireState(session, PlaybackState::Idle,
                 "new session should start idle");
    require(!session.activeOccurrence().has_value(),
            "idle session should have no active occurrence");
    require(!session.activeWorkToken().has_value(),
            "idle session should have no work token");
    require(session.generation() == 0,
            "generation should begin at zero before first lifecycle");

    const PlaybackMutationStatus idlePlay = session.play();
    require(!idlePlay.ok,
            "play without an active occurrence should fail");
    require(idlePlay.error.code
                == PlaybackSessionErrorCode::NoActiveOccurrence,
            "idle play should identify missing active occurrence");

    const auto idleSeek = session.requestSeek(5.0);
    require(!idleSeek.ok(),
            "seek without active occurrence should fail");
    require(idleSeek.error.code
                == PlaybackSessionErrorCode::NoActiveOccurrence,
            "idle seek should identify missing active occurrence");

    const PlaybackWorkToken token1 = mustSession(
        session.beginOccurrence(occurrence("a-1", a), false),
        "first occurrence should begin");
    requireState(session, PlaybackState::Loading,
                 "begin should enter loading");
    requireActiveId(session, "a-1");
    require(token1.occurrenceId().value() == QStringLiteral("a-1"),
            "work token should carry exact queue occurrence identity");
    require(token1.generation() == 1 && session.generation() == 1,
            "first lifecycle should use generation one");
    require(!session.playWhenReady(),
            "start-paused begin should not request playback");

    const PlaybackMutationStatus pauseLoading = session.pause();
    require(pauseLoading.ok && !pauseLoading.changed,
            "pause while already loading-paused should be a no-op");
    const PlaybackMutationStatus playLoading = session.play();
    require(playLoading.ok && playLoading.changed,
            "play while loading should update desired state");
    require(session.playWhenReady(),
            "play while loading should set play-when-ready");
    const PlaybackMutationStatus pauseLoadingAgain = session.pause();
    require(pauseLoadingAgain.ok && pauseLoadingAgain.changed,
            "pause while loading should update desired state");
    require(!session.playWhenReady(),
            "pause while loading should clear play-when-ready");

    require(session.markLoaded(token1).ok,
            "matching load completion should be accepted");
    requireState(session, PlaybackState::Paused,
                 "load should honor paused desired state");
    const PlaybackMutationStatus duplicateLoaded =
        session.markLoaded(token1);
    require(duplicateLoaded.ok && !duplicateLoaded.changed,
            "duplicate matching load completion should be harmless");

    require(session.play().ok,
            "play should transition paused session");
    requireState(session, PlaybackState::Playing,
                 "play should enter playing");
    const PlaybackMutationStatus playNoop = session.play();
    require(playNoop.ok && !playNoop.changed,
            "play while playing should be idempotent");

    require(session.observePosition(token1, 12.5).ok,
            "matching position observation should be accepted");
    require(session.positionSeconds().has_value()
                && *session.positionSeconds() == 12.5,
            "position observation should be retained");
    require(session.observeDuration(token1, 180.0).ok,
            "matching duration observation should be accepted");
    require(session.durationSeconds().has_value()
                && *session.durationSeconds() == 180.0,
            "duration observation should be retained");

    const PlaybackMutationStatus negativePosition =
        session.observePosition(token1, -1.0);
    require(!negativePosition.ok,
            "negative position should fail");
    require(negativePosition.error.code
                == PlaybackSessionErrorCode::InvalidTimeValue,
            "negative position should use typed time error");
    require(*session.positionSeconds() == 12.5,
            "invalid position must not mutate prior observation");

    const PlaybackMutationStatus nanDuration =
        session.observeDuration(
            token1,
            std::numeric_limits<double>::quiet_NaN());
    require(!nanDuration.ok,
            "non-finite duration should fail");
    require(*session.durationSeconds() == 180.0,
            "invalid duration must not mutate prior observation");

    require(session.observePause(token1, true).ok,
            "matching pause observation should be accepted");
    requireState(session, PlaybackState::Paused,
                 "pause observation should enter paused");
    require(session.observePause(token1, false).ok,
            "matching resume observation should be accepted");
    requireState(session, PlaybackState::Playing,
                 "resume observation should enter playing");

    const PlaybackSeekRequest seek1 = mustSession(
        session.requestSeek(30.0),
        "first seek should be created");
    require(seek1.sequence() == 1,
            "first seek sequence should start at one");
    require(seek1.workToken() == token1,
            "seek should inherit exact active work token");

    const PlaybackSeekRequest seek2 = mustSession(
        session.requestSeek(60.0),
        "second seek should supersede first");
    require(seek2.sequence() == 2,
            "second seek should increment sequence");
    require(session.pendingSeek().has_value()
                && session.pendingSeek()->sequence() == 2,
            "latest seek should be the only pending seek");

    const PlaybackMutationStatus oldSeekApplied =
        session.markSeekApplied(seek1, 30.0);
    require(!oldSeekApplied.ok,
            "superseded seek completion should fail closed");
    require(oldSeekApplied.error.code
                == PlaybackSessionErrorCode::SupersededSeek,
            "superseded seek should use typed error");
    require(*session.positionSeconds() == 12.5,
            "superseded seek must not mutate position");
    require(session.pendingSeek().has_value()
                && session.pendingSeek()->sequence() == 2,
            "superseded completion must not clear newer seek");

    require(session.markSeekApplied(seek2, 59.75).ok,
            "current seek completion should be accepted");
    require(session.positionSeconds().has_value()
                && *session.positionSeconds() == 59.75,
            "current seek completion should publish observed position");
    require(!session.pendingSeek().has_value(),
            "applied current seek should clear pending state");

    const PlaybackMutationStatus duplicateSeekApplied =
        session.markSeekApplied(seek2, 60.0);
    require(!duplicateSeekApplied.ok,
            "duplicate seek completion should not apply twice");
    require(duplicateSeekApplied.error.code
                == PlaybackSessionErrorCode::SupersededSeek,
            "completed seek should no longer be current");

    const auto invalidSeek = session.requestSeek(
        std::numeric_limits<double>::infinity());
    require(!invalidSeek.ok(),
            "non-finite seek target should fail");
    require(invalidSeek.error.code
                == PlaybackSessionErrorCode::InvalidTimeValue,
            "invalid seek should use typed time error");

    const PlaybackWorkToken token2 = mustSession(
        session.beginOccurrence(occurrence("a-2", a), true),
        "second duplicate recording occurrence should begin");
    requireState(session, PlaybackState::Loading,
                 "new occurrence should reset to loading");
    requireActiveId(session, "a-2");
    require(token2.generation() == 2,
            "new occurrence should advance generation");
    require(token2.occurrenceId() != token1.occurrenceId(),
            "duplicate recording occurrences should keep distinct identities");
    require(session.activeOccurrence()->recordingMbid() == a,
            "duplicate occurrence should preserve same Recording MBID");
    require(!session.positionSeconds().has_value()
                && !session.durationSeconds().has_value(),
            "new lifecycle should clear prior timing observations");
    require(!session.pendingSeek().has_value()
                && !session.failure().has_value(),
            "new lifecycle should clear prior transient state");

    requireStale(session.markLoaded(token1),
                 "old load completion must be stale");
    requireStale(session.observePause(token1, true),
                 "old pause callback must be stale");
    requireStale(session.observePosition(token1, 999.0),
                 "old position callback must be stale");
    requireStale(session.observeDuration(token1, 999.0),
                 "old duration callback must be stale");
    requireStale(session.markEnded(token1),
                 "old EOF must be stale");
    requireStale(
        session.markFailed(
            token1,
            PlaybackFailure{QStringLiteral("old_error"),
                            QStringLiteral("from old occurrence")}),
        "old playback error must be stale");
    requireStale(session.markSeekApplied(seek2, 999.0),
                 "old seek completion must be stale");

    requireState(session, PlaybackState::Loading,
                 "stale callbacks must not move newer lifecycle");
    requireActiveId(session, "a-2");
    require(session.playWhenReady(),
            "stale pause callback must not alter newer desired state");
    require(!session.positionSeconds().has_value()
                && !session.durationSeconds().has_value()
                && !session.failure().has_value(),
            "stale callbacks must not leak old observations into new occurrence");

    require(session.markLoaded(token2).ok,
            "current second lifecycle should load");
    requireState(session, PlaybackState::Playing,
                 "play-when-ready load should enter playing");

    const PlaybackWorkToken token3 = mustSession(
        session.beginOccurrence(occurrence("a-2", a), true),
        "same occurrence restart should begin a new generation");
    require(token3.occurrenceId() == token2.occurrenceId(),
            "restart may retain exact queue occurrence identity");
    require(token3.generation() == token2.generation() + 1,
            "same occurrence restart must still advance generation");
    requireStale(session.markLoaded(token2),
                 "old generation of same occurrence must be stale");
    requireState(session, PlaybackState::Loading,
                 "same-occurrence stale callback must not complete new load");

    require(session.markLoaded(token3).ok,
            "new generation load should be accepted");
    const PlaybackMutationStatus invalidFailure =
        session.markFailed(
            token3,
            PlaybackFailure{QStringLiteral(" bad "),
                            QStringLiteral("invalid code")});
    require(!invalidFailure.ok,
            "invalid failure envelope should fail");
    require(invalidFailure.error.code
                == PlaybackSessionErrorCode::InvalidFailure,
            "invalid failure envelope should use typed error");
    requireState(session, PlaybackState::Playing,
                 "invalid failure must not terminate session");

    require(session.markFailed(
                token3,
                PlaybackFailure{QStringLiteral("unknown"),
                                QStringLiteral("coarse engine failure")})
                .ok,
            "matching typed failure should terminate current lifecycle");
    requireState(session, PlaybackState::Failed,
                 "failure should enter failed state");
    require(session.failure().has_value()
                && session.failure()->code == QStringLiteral("unknown"),
            "failure should preserve adapter-provided coarse code");
    require(session.failure()->detail
                == QStringLiteral("coarse engine failure"),
            "failure should preserve detail without inventing classification");

    const PlaybackMutationStatus eofAfterFailure =
        session.markEnded(token3);
    require(!eofAfterFailure.ok,
            "EOF must not replace terminal failure state");
    requireState(session, PlaybackState::Failed,
                 "failure should remain terminal");
    require(!session.play().ok,
            "play should not revive failed lifecycle");

    const PlaybackMutationStatus stopFailed = session.stop();
    require(stopFailed.ok && stopFailed.changed,
            "stop should clear failed lifecycle");
    requireState(session, PlaybackState::Idle,
                 "stop should return to idle");
    require(!session.activeOccurrence().has_value()
                && !session.activeWorkToken().has_value(),
            "stop should clear lifecycle binding");
    require(!session.failure().has_value(),
            "stop should clear failure state");
    requireStale(session.markLoaded(token3),
                 "callback after stop must be stale");

    const PlaybackMutationStatus stopAgain = session.stop();
    require(stopAgain.ok && !stopAgain.changed,
            "stop while idle should be idempotent");

    PlaybackSession ended;
    const PlaybackWorkToken endedToken = mustSession(
        ended.beginOccurrence(occurrence("b-1", b), true),
        "ended fixture should begin");
    require(ended.markLoaded(endedToken).ok,
            "ended fixture should load");
    const PlaybackSeekRequest pendingAtEof = mustSession(
        ended.requestSeek(25.0),
        "ended fixture should have pending seek");
    require(ended.pendingSeek().has_value(),
            "seek should be pending before EOF");
    require(ended.markEnded(endedToken).ok,
            "matching EOF should end lifecycle");
    requireState(ended, PlaybackState::Ended,
                 "EOF should enter ended state");
    requireActiveId(ended, "b-1");
    require(ended.activeWorkToken().has_value(),
            "ended state should retain occurrence binding for coordination");
    require(!ended.pendingSeek().has_value(),
            "EOF should cancel pending seek");
    require(ended.markEnded(endedToken).ok
                && !ended.markEnded(endedToken).changed,
            "duplicate EOF should be harmless");
    require(!ended.play().ok,
            "play should not implicitly restart ended occurrence");
    require(!ended.requestSeek(10.0).ok(),
            "seek should not revive ended lifecycle");
    require(!ended.markSeekApplied(pendingAtEof, 25.0).ok,
            "seek completion after EOF should be rejected");

    VinylQueue queue;
    require(queue.append(occurrence("q-a", a)).ok,
            "queue fixture append a");
    require(queue.append(occurrence("q-b", b)).ok,
            "queue fixture append b");
    require(queue.activate(occurrenceId("q-a")).ok,
            "queue fixture activate a");

    PlaybackSession isolated;
    const PlaybackWorkToken queueToken = mustSession(
        isolated.beginOccurrence(*queue.activeOccurrence(), true),
        "session should consume BA1 active occurrence snapshot");
    require(isolated.markLoaded(queueToken).ok,
            "queue-backed fixture should load");
    require(isolated.stop().ok,
            "session stop should succeed");
    require(queue.size() == 2,
            "BA2 stop must not mutate BA1 queue order");
    require(queue.activeOccurrenceId().has_value()
                && queue.activeOccurrenceId()->value()
                    == QStringLiteral("q-a"),
            "BA2 stop must not change BA1 active occurrence");
    require(queue.items().at(1).occurrenceId().value()
                == QStringLiteral("q-b"),
            "BA2 lifecycle must not consume or reorder next queue item");

    PlaybackSession eofIsolation;
    const PlaybackWorkToken eofToken = mustSession(
        eofIsolation.beginOccurrence(*queue.activeOccurrence(), true),
        "EOF isolation fixture should begin");
    require(eofIsolation.markLoaded(eofToken).ok,
            "EOF isolation fixture should load");
    require(eofIsolation.markEnded(eofToken).ok,
            "EOF isolation fixture should end");
    require(queue.activeOccurrenceId().has_value()
                && queue.activeOccurrenceId()->value()
                    == QStringLiteral("q-a"),
            "BA2-A EOF must not advance BA1 queue");
    require(queue.size() == 2,
            "BA2-A EOF must not remove queue occurrences");

    require(playbackStateName(PlaybackState::Loading)
                == QStringLiteral("loading"),
            "playback states should expose stable snake_case names");
    require(playbackSessionErrorCodeName(
                PlaybackSessionErrorCode::StaleAsyncWork)
                == QStringLiteral("stale_async_work"),
            "session errors should expose stable snake_case names");
    require(playbackSessionErrorCodeName(
                PlaybackSessionErrorCode::SupersededSeek)
                == QStringLiteral("superseded_seek"),
            "seek supersession should expose stable typed name");

    qInfo().noquote()
        << "VINYL_BA2A_SESSION_OK checks=" << g_checks;
    return 0;
}
