#include "VinylListenEventLedger.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>

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
T mustDomain(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustQueue(const vinyl::QueueResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustPlayback(const vinyl::PlaybackSessionResult<T> &result,
               const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustListen(const vinyl::ListenLedgerResult<T> &result,
             const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
        "fixture Recording MBID should parse");
}

vinyl::QueueOccurrenceId occurrenceId(const char *value)
{
    return mustQueue(
        vinyl::QueueOccurrenceId::parse(QString::fromLatin1(value)),
        "fixture occurrence ID should parse");
}

vinyl::QueueOccurrence occurrence(
    const char *id,
    const vinyl::RecordingMbid &recordingMbid)
{
    return mustQueue(
        vinyl::QueueOccurrence::create(
            occurrenceId(id),
            recordingMbid),
        "fixture queue occurrence should construct");
}

vinyl::ProfileScopeId profile(const char *value)
{
    return mustDomain(
        vinyl::ProfileScopeId::parse(QString::fromLatin1(value)),
        "fixture profile ID should parse");
}

vinyl::PlayInstanceId playInstance(const char *value)
{
    return mustDomain(
        vinyl::PlayInstanceId::parse(QString::fromLatin1(value)),
        "fixture play instance ID should parse");
}

vinyl::ListenSessionId listenSession(const char *value)
{
    return mustListen(
        vinyl::ListenSessionId::parse(QString::fromLatin1(value)),
        "fixture listen session ID should parse");
}

vinyl::ListenSourceTraceId source(const char *value)
{
    return mustListen(
        vinyl::ListenSourceTraceId::parse(QString::fromLatin1(value)),
        "fixture source trace should parse");
}

int countKind(const vinyl::ListenEventLedger &ledger,
              vinyl::ListenEventKind kind)
{
    int count = 0;
    for (const vinyl::ListenEvent &event : ledger.events()) {
        if (event.kind() == kind)
            ++count;
    }
    return count;
}

void requireStatus(const vinyl::ListenLedgerMutationStatus &status,
                   const char *message)
{
    require(status.ok, message);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    const RecordingMbid recordingA =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid recordingB =
        recording("22222222-2222-4222-8222-222222222222");
    const ProfileScopeId profileA = profile("profile-a");
    const ProfileScopeId profileB = profile("profile-b");

    require(!ProfileScopeId::parse(QString()).ok(),
            "empty profile IDs must fail");
    require(!ListenSessionId::parse(QStringLiteral(" bad ")).ok(),
            "session IDs with surrounding whitespace must fail");
    require(!ListenSourceTraceId::parse(QStringLiteral("bad\nsource")).ok(),
            "source trace IDs with control characters must fail");

    PlaybackSession playback;

    const QueueOccurrence occA = occurrence("occ-a", recordingA);
    const PlaybackWorkToken tokenA = mustPlayback(
        playback.beginOccurrence(occA),
        "occurrence A should begin");
    const ListenContext contextA = mustListen(
        ListenContext::create(
            profileA,
            listenSession("listen-a"),
            occA,
            tokenA,
            playInstance("play-a")),
        "listen context A should bind the exact occurrence and generation");

    const QueueOccurrence occB = occurrence("occ-b", recordingA);
    const auto mismatched = ListenContext::create(
        profileA,
        listenSession("bad-context"),
        occB,
        tokenA,
        playInstance("play-mismatch"));
    require(!mismatched.ok(),
            "a token must not be paired with another queue occurrence");
    require(mismatched.error.code == ListenLedgerErrorCode::ContextMismatch,
            "occurrence/token mismatch must be typed");

    ListenEventLedger ledger(profileA);
    ListenEventRecorder recorder(ledger);
    const ListenSourceTraceId sourceOne = source("source-trace-1");
    const ListenSourceTraceId sourceTwo = source("source-trace-2");

    requireStatus(
        recorder.start(contextA, 1000, 1000, 0, 1000, sourceOne),
        "start should append the first factual lifecycle event");
    require(ledger.size() == 1,
            "start should create exactly one ledger row");
    require(ledger.events().constFirst().kind() == ListenEventKind::Start,
            "first row should be start");
    require(ledger.events().constFirst().context().recordingMbid()
                == recordingA,
            "event identity should preserve Recording MBID");
    require(ledger.events().constFirst().context().occurrenceId()
                == occA.occurrenceId(),
            "event identity should preserve QueueOccurrenceId");
    require(ledger.events().constFirst().context().sessionId()
                == listenSession("listen-a"),
            "event identity should preserve listen-session context");
    require(ledger.events().constFirst().context().workToken()
                == tokenA,
            "event identity should preserve BA2 generation fencing");

    requireStatus(
        recorder.sample(tokenA, 2000, 2000, 1000, 1000),
        "first advancing sample should be accepted");
    requireStatus(
        recorder.sample(tokenA, 3000, 3000, 2000, 1000),
        "second advancing sample should be accepted");
    require(ledger.totalListeningMs() == 2000,
            "qualified advancing samples should add wall listening time");

    requireStatus(
        recorder.pause(tokenA, 4000, 4000, 3000, 1000),
        "pause should close the current qualified interval");
    require(ledger.totalListeningMs() == 3000,
            "time up to a factual pause should be counted once");
    require(!recorder.consuming(),
            "pause should stop BA4 accounting without changing BA2");

    const int pausedSize = ledger.size();
    requireStatus(
        recorder.sample(tokenA, 5000, 5000, 4000, 1000),
        "paused sample should be harmless");
    requireStatus(
        recorder.sample(tokenA, 6000, 6000, 5000, 1000),
        "repeated paused sample should be harmless");
    require(ledger.size() == pausedSize,
            "paused samples must not manufacture ledger events");
    require(ledger.totalListeningMs() == 3000,
            "paused samples must not manufacture listening time");

    requireStatus(
        recorder.resume(tokenA, 7000, 7000, 5000, 1000),
        "resume should append a factual lifecycle event");
    requireStatus(
        recorder.sample(tokenA, 8000, 8000, 6000, 1000),
        "post-resume sample should count from the resume baseline");
    require(ledger.totalListeningMs() == 4000,
            "resume should restart accounting only from its own baseline");

    requireStatus(
        recorder.seek(tokenA, 9000, 9000, 7000, 30000, 1000),
        "seek should record from/to positions and reset continuity");
    require(ledger.totalListeningMs() == 5000,
            "only the pre-seek advancing interval should count");
    requireStatus(
        recorder.sample(tokenA, 10000, 10000, 31000, 1000),
        "post-seek advancement should count from the seek target");
    require(ledger.totalListeningMs() == 6000,
            "seek jump itself must never become listening time");

    requireStatus(
        recorder.sourceChanged(
            tokenA,
            sourceTwo,
            11000,
            11000,
            32000,
            1000),
        "source change should be a factual discontinuity");
    require(ledger.totalListeningMs() == 7000,
            "time before source change should remain countable");
    const int afterSourceChange = ledger.size();
    const auto duplicateSourceChange = recorder.sourceChanged(
        tokenA,
        sourceTwo,
        11500,
        11500,
        32500,
        1000);
    require(duplicateSourceChange.ok && !duplicateSourceChange.changed,
            "repeating the same source trace should be a no-op");
    require(ledger.size() == afterSourceChange,
            "same-source no-op must not append a fake source change");
    requireStatus(
        recorder.sample(tokenA, 12000, 12000, 33000, 1000),
        "source-change baseline should permit later continuous time");
    require(ledger.totalListeningMs() == 8000,
            "source transition bridge must not be double-counted");

    const PlaybackWorkToken tokenB = mustPlayback(
        playback.beginOccurrence(occB),
        "duplicate-recording occurrence B should begin independently");
    require(tokenB.occurrenceId() != tokenA.occurrenceId(),
            "fixture should prove duplicate Recording MBIDs use distinct occurrences");
    const ListenContext contextB = mustListen(
        ListenContext::create(
            profileA,
            listenSession("listen-b"),
            occB,
            tokenB,
            playInstance("play-b")),
        "listen context B should construct");
    requireStatus(
        recorder.start(contextB, 13000, 13000, 0, 1000, sourceOne),
        "new occurrence should replace BA4 observation context without inventing a stop");

    const int beforeStale = ledger.size();
    const qint64 beforeStaleMs = ledger.totalListeningMs();
    const auto staleOldSample =
        recorder.sample(tokenA, 14000, 14000, 34000, 1000);
    require(!staleOldSample.ok,
            "old occurrence callback must be rejected");
    require(staleOldSample.error.code == ListenLedgerErrorCode::StaleContext,
            "old occurrence rejection should be typed stale_context");
    require(ledger.size() == beforeStale,
            "stale callback must append nothing");
    require(ledger.totalListeningMs() == beforeStaleMs,
            "stale callback must contribute zero listening time");
    const auto staleOldFailure =
        recorder.failure(tokenA, 14500, 14500, 34000, 1000);
    require(!staleOldFailure.ok
                && staleOldFailure.error.code
                    == ListenLedgerErrorCode::StaleContext,
            "stale failure callback must be rejected by the full work token");
    require(ledger.size() == beforeStale,
            "stale failure callback must append nothing");

    requireStatus(
        recorder.sample(tokenB, 14000, 14000, 1000, 1000),
        "new occurrence should accept only its exact token");
    requireStatus(
        recorder.skip(tokenB, 15000, 15000, 2000, 1000),
        "skip should close the active listen context");
    require(ledger.totalListeningMs() == 10000,
            "new occurrence time should account independently");
    const auto afterSkip =
        recorder.sample(tokenB, 16000, 16000, 3000, 1000);
    require(!afterSkip.ok
                && afterSkip.error.code
                    == ListenLedgerErrorCode::NoActiveContext,
            "terminal event should close BA4 context");

    const QueueOccurrence occC = occurrence("occ-c", recordingB);
    const PlaybackWorkToken tokenC = mustPlayback(
        playback.beginOccurrence(occC),
        "occurrence C should begin");
    const ListenContext contextC = mustListen(
        ListenContext::create(
            profileA,
            listenSession("listen-c"),
            occC,
            tokenC,
            playInstance("play-c")),
        "context C should construct");
    requireStatus(
        recorder.start(contextC, 16000, 16000, 0, 1000),
        "context C should start");
    requireStatus(
        recorder.sample(tokenC, 17000, 17000, 1000, 1000),
        "context C sample should count");
    requireStatus(
        recorder.stop(tokenC, 18000, 18000, 2000, 1000),
        "stop should be distinguishable from skip");
    require(ledger.totalListeningMs() == 12000,
            "stop should close and count only continuous time");

    const QueueOccurrence occD = occurrence("occ-d", recordingB);
    const PlaybackWorkToken tokenD = mustPlayback(
        playback.beginOccurrence(occD),
        "occurrence D should begin");
    const ListenContext contextD = mustListen(
        ListenContext::create(
            profileA,
            listenSession("listen-d"),
            occD,
            tokenD,
            playInstance("play-d")),
        "context D should construct");
    requireStatus(
        recorder.start(contextD, 19000, 19000, 0, 1000),
        "context D should start");
    requireStatus(
        recorder.complete(tokenD, 20000, 20000, 1000, 1000),
        "completion should be distinguishable from stop and skip");
    require(ledger.totalListeningMs() == 13000,
            "completion should count only its final continuous interval");

    const QueueOccurrence occForeign =
        occurrence("occ-foreign", recordingA);
    const PlaybackWorkToken tokenForeign = mustPlayback(
        playback.beginOccurrence(occForeign),
        "foreign-profile fixture occurrence should begin");
    const ListenContext foreignContext = mustListen(
        ListenContext::create(
            profileB,
            listenSession("foreign-session"),
            occForeign,
            tokenForeign,
            playInstance("play-foreign")),
        "foreign-profile context should be structurally valid");
    const int beforeForeign = ledger.size();
    const auto foreignStart =
        recorder.start(foreignContext, 20500, 20500, 0, 1000);
    require(!foreignStart.ok
                && foreignStart.error.code
                    == ListenLedgerErrorCode::ProfileMismatch,
            "profile A ledger must reject profile B events");
    require(ledger.size() == beforeForeign,
            "cross-profile rejection must mutate nothing");

    const QueueOccurrence occRate = occurrence("occ-rate", recordingA);
    const PlaybackWorkToken tokenRate = mustPlayback(
        playback.beginOccurrence(occRate),
        "rate fixture occurrence should begin");
    const ListenContext rateContext = mustListen(
        ListenContext::create(
            profileA,
            listenSession("listen-rate"),
            occRate,
            tokenRate,
            playInstance("play-rate")),
        "rate context should construct");
    requireStatus(
        recorder.start(rateContext, 21000, 21000, 0, 1000),
        "rate context should start");
    requireStatus(
        recorder.sample(tokenRate, 22000, 22000, 1000, 1000),
        "normal-rate sample should count");
    require(ledger.totalListeningMs() == 14000,
            "normal rate interval should count one second");

    const qint64 beforeRateTransition = ledger.totalListeningMs();
    requireStatus(
        recorder.sample(tokenRate, 23000, 23000, 3000, 2000),
        "rate change should reset the baseline safely");
    require(ledger.totalListeningMs() == beforeRateTransition,
            "rate-change bridge must not be counted");
    requireStatus(
        recorder.sample(tokenRate, 24000, 24000, 5000, 2000),
        "stable new rate should resume accounting");
    require(ledger.totalListeningMs() == 15000,
            "2x media advance over one wall second should count one wall second");

    const qint64 beforeGap = ledger.totalListeningMs();
    requireStatus(
        recorder.sample(tokenRate, 60000, 60000, 6000, 2000),
        "oversized sampling gap should reset rather than fabricate time");
    require(ledger.totalListeningMs() == beforeGap,
            "gap over 30 seconds must contribute zero time");
    requireStatus(
        recorder.sample(tokenRate, 61000, 61000, 8000, 2000),
        "accounting should resume after the gap reset");
    require(ledger.totalListeningMs() == 16000,
            "post-gap stable interval should count normally");

    requireStatus(
        recorder.seek(tokenRate, 62000, 62000, 10000, 5000, 2000),
        "rewind seek should be an explicit discontinuity");
    require(ledger.totalListeningMs() == 17000,
            "pre-rewind interval should count but rewind distance should not");
    requireStatus(
        recorder.sample(tokenRate, 63000, 63000, 7000, 2000),
        "post-rewind playback should count from the target");
    requireStatus(
        recorder.stop(tokenRate, 64000, 64000, 9000, 2000),
        "final stop should close rate fixture");
    require(ledger.totalListeningMs() == 19000,
            "deterministic accounting total should match qualified spans only");

    const QueueOccurrence occFailure =
        occurrence("occ-failure", recordingB);
    const PlaybackWorkToken tokenFailure = mustPlayback(
        playback.beginOccurrence(occFailure),
        "failure fixture occurrence should begin");
    const ListenContext failureContext = mustListen(
        ListenContext::create(
            profileA,
            listenSession("listen-failure"),
            occFailure,
            tokenFailure,
            playInstance("play-failure")),
        "failure fixture context should construct");
    requireStatus(
        recorder.start(failureContext, 65000, 65000, 0, 1000),
        "failure fixture should start");
    requireStatus(
        recorder.sample(tokenFailure, 66000, 66000, 1000, 1000),
        "failure fixture should accumulate qualified time");
    requireStatus(
        recorder.failure(tokenFailure, 67000, 67000, 2000, 1000),
        "playback failure should be a distinct factual terminal event");
    require(ledger.totalListeningMs() == 21000,
            "failure should close and count only its final continuous interval");

    require(countKind(ledger, ListenEventKind::Start) > 0,
            "ledger should distinguish start facts");
    require(countKind(ledger, ListenEventKind::Pause) > 0,
            "ledger should distinguish pause facts");
    require(countKind(ledger, ListenEventKind::Resume) > 0,
            "ledger should distinguish resume facts");
    require(countKind(ledger, ListenEventKind::Seek) > 0,
            "ledger should distinguish seek facts");
    require(countKind(ledger, ListenEventKind::Skip) > 0,
            "ledger should distinguish skip facts");
    require(countKind(ledger, ListenEventKind::Stop) > 0,
            "ledger should distinguish stop facts");
    require(countKind(ledger, ListenEventKind::Failure) > 0,
            "ledger should distinguish playback-failure facts");
    require(countKind(ledger, ListenEventKind::Completion) > 0,
            "ledger should distinguish completion facts");
    require(countKind(ledger, ListenEventKind::SourceChange) > 0,
            "ledger should distinguish source-change facts");
    require(countKind(ledger, ListenEventKind::ListeningSpan) > 0,
            "ledger should retain explicit qualified listening-time facts");

    bool foundSeek = false;
    bool foundSourceChange = false;
    quint64 expectedSequence = 1;
    for (const ListenEvent &event : ledger.events()) {
        require(event.sequence() == expectedSequence++,
                "append-only ledger sequence must be strictly contiguous");
        require(event.context().profileId() == profileA,
                "every row in profile A ledger must retain profile A scope");

        if (event.kind() == ListenEventKind::Seek) {
            foundSeek = true;
            require(event.fromPositionMs().has_value()
                        && event.toPositionMs().has_value(),
                    "seek event must preserve both sides of the discontinuity");
        }
        if (event.kind() == ListenEventKind::SourceChange) {
            foundSourceChange = true;
            require(event.sourceBefore().has_value()
                        && event.sourceAfter().has_value(),
                    "source-change event must preserve provenance before and after");
            require(event.sourceBefore()->value()
                        != event.sourceAfter()->value(),
                    "source-change event must describe an actual change");
        }
    }
    require(foundSeek, "seek evidence should be inspectable");
    require(foundSourceChange, "source-change evidence should be inspectable");

    const QueueOccurrence occSame = occurrence("occ-same", recordingA);
    const PlaybackWorkToken firstSame = mustPlayback(
        playback.beginOccurrence(occSame),
        "same-occurrence first generation should begin");
    const ListenContext firstSameContext = mustListen(
        ListenContext::create(
            profileA,
            listenSession("same-generation-session"),
            occSame,
            firstSame,
            playInstance("play-same-1")),
        "first same-occurrence context should construct");
    requireStatus(
        recorder.start(firstSameContext, 65000, 65000, 0, 1000),
        "first same-occurrence generation should start");

    const PlaybackWorkToken secondSame = mustPlayback(
        playback.beginOccurrence(occSame),
        "same occurrence retry should receive another BA2 generation");
    require(secondSame.occurrenceId() == firstSame.occurrenceId(),
            "same-occurrence retry should keep occurrence identity");
    require(secondSame.generation() != firstSame.generation(),
            "same-occurrence retry must have a fresh generation");
    const ListenContext secondSameContext = mustListen(
        ListenContext::create(
            profileA,
            listenSession("same-generation-session"),
            occSame,
            secondSame,
            playInstance("play-same-2"),
            playInstance("play-same-1")),
        "second same-occurrence context should construct");
    requireStatus(
        recorder.start(secondSameContext, 66000, 66000, 0, 1000),
        "new generation should replace old BA4 observation context");
    const auto staleGeneration =
        recorder.sample(firstSame, 67000, 67000, 1000, 1000);
    require(!staleGeneration.ok
                && staleGeneration.error.code
                    == ListenLedgerErrorCode::StaleContext,
            "old generation of the same occurrence must also be stale");
    requireStatus(
        recorder.stop(secondSame, 67000, 67000, 1000, 1000),
        "new generation should remain valid after stale callback rejection");

    const QueueOccurrence occClock =
        occurrence("occ-clock-step", recordingB);
    const PlaybackWorkToken tokenClock = mustPlayback(
        playback.beginOccurrence(occClock),
        "clock-step occurrence should begin");
    const ListenContext clockContext = mustListen(
        ListenContext::create(
            profileA,
            listenSession("clock-step-session"),
            occClock,
            tokenClock,
            playInstance("play-clock")),
        "clock-step listen context should construct");
    ListenEventLedger clockLedger(profileA);
    ListenEventRecorder clockRecorder(clockLedger);
    requireStatus(
        clockRecorder.start(clockContext, 100000, 1000, 0, 1000),
        "clock-step fixture should start");
    requireStatus(
        clockRecorder.sample(tokenClock, 99000, 2000, 1000, 1000),
        "backward wall-clock step must not reject forward monotonic listening");
    requireStatus(
        clockRecorder.sample(tokenClock, 98000, 3000, 2000, 1000),
        "subsequent backward wall time must remain descriptive");
    requireStatus(
        clockRecorder.stop(tokenClock, 97000, 4000, 3000, 1000),
        "terminal fact must survive a backward wall-clock sequence");
    require(clockLedger.totalListeningMs() == 3000,
            "qualified accounting must use monotonic elapsed time, not wall time");
    require(countKind(clockLedger, ListenEventKind::ListeningSpan) == 1,
            "continuous qualified samples must coalesce into one durable span");
    require(clockLedger.events().at(1).activeMs() == std::optional<qint64>(3000),
            "the coalesced span must retain the full continuous qualified duration");
    require(clockLedger.events().at(1).observedAtMs() == 97000,
            "coalesced span may end at a wall time lower than its start");
    require(clockLedger.events().at(1).monotonicElapsedMs() == 4000,
            "coalesced span must retain the host monotonic endpoint");

    qInfo().noquote()
        << "PASS: BA4 listen-event ledger contract"
        << g_checks << "checks";
    return 0;
}
