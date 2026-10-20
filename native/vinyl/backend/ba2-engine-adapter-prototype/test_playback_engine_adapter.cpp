#include "PlaybackEngineAdapter.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <functional>
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

template <typename T>
T mustLoad(const vinyl::PlaybackLoadInputResult<T> &result,
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

enum class CommandKind {
    Load,
    Play,
    Pause,
    Seek,
    Stop,
};

struct RecordedCommand final {
    CommandKind kind;
    vinyl::PlaybackWorkToken token;
    std::optional<vinyl::PlaybackLoadInput> loadInput;
    std::optional<vinyl::PlaybackSeekRequest> seekRequest;
    std::optional<qint64> seekTargetMs;

    RecordedCommand(CommandKind commandKind,
                    vinyl::PlaybackWorkToken workToken)
        : kind(commandKind)
        , token(std::move(workToken))
    {
    }
};

class FakePlaybackEngine final : public vinyl::PlaybackEngineCommands
{
public:
    void load(const vinyl::PlaybackWorkToken &token,
              const vinyl::PlaybackLoadInput &input) override
    {
        RecordedCommand command(CommandKind::Load, token);
        command.loadInput = input;
        commands.push_back(std::move(command));
    }

    void play(const vinyl::PlaybackWorkToken &token) override
    {
        commands.push_back(RecordedCommand(CommandKind::Play, token));
    }

    void pause(const vinyl::PlaybackWorkToken &token) override
    {
        commands.push_back(RecordedCommand(CommandKind::Pause, token));
    }

    void seek(const vinyl::PlaybackSeekCommand &seekCommand) override
    {
        RecordedCommand command(CommandKind::Seek, seekCommand.workToken());
        command.seekRequest = seekCommand.request();
        command.seekTargetMs = seekCommand.targetMs();
        commands.push_back(std::move(command));
    }

    void seek(const vinyl::PlaybackSeekRequest &request) override
    {
        RecordedCommand command(CommandKind::Seek, request.workToken());
        command.seekRequest = request;
        commands.push_back(std::move(command));
    }

    void stop(const vinyl::PlaybackWorkToken &token) override
    {
        commands.push_back(RecordedCommand(CommandKind::Stop, token));
        if (stopHook)
            stopHook(token);
    }

    QVector<RecordedCommand> commands;
    std::function<void(const vinyl::PlaybackWorkToken &)> stopHook;
};

void requireCommand(const FakePlaybackEngine &engine,
                    int index,
                    CommandKind kind,
                    const vinyl::PlaybackWorkToken &token,
                    const char *message)
{
    require(index >= 0 && index < engine.commands.size(), message);
    require(engine.commands.at(index).kind == kind,
            "engine command kind should match expected kind");
    require(engine.commands.at(index).token == token,
            "engine command should carry exact playback work token");
}

void requireStale(const vinyl::PlaybackMutationStatus &status,
                  const char *message)
{
    require(!status.ok, message);
    require(status.error.code
                == vinyl::PlaybackSessionErrorCode::StaleAsyncWork,
            "stale callback should use StaleAsyncWork");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    const auto emptyResource = PlaybackLoadInput::create(QString());
    require(!emptyResource.ok(),
            "empty playback resource should fail");
    require(emptyResource.error.code
                == PlaybackLoadInputErrorCode::InvalidResource,
            "empty resource should use InvalidResource");

    const auto controlResource =
        PlaybackLoadInput::create(QStringLiteral("bad\nresource"));
    require(!controlResource.ok(),
            "resource with control character should fail");

    const auto invalidHeaderName = PlaybackLoadInput::create(
        QStringLiteral("https://fixture.invalid/audio"),
        {{QStringLiteral("Bad Header"), QStringLiteral("value")}});
    require(!invalidHeaderName.ok(),
            "header name with spaces should fail");
    require(invalidHeaderName.error.code
                == PlaybackLoadInputErrorCode::InvalidHeader,
            "bad header name should use InvalidHeader");

    const auto invalidHeaderValue = PlaybackLoadInput::create(
        QStringLiteral("https://fixture.invalid/audio"),
        {{QStringLiteral("Referer"), QStringLiteral("x\r\ny")}});
    require(!invalidHeaderValue.ok(),
            "header value with line break should fail");

    QVector<PlaybackRequestHeader> tooManyHeaders;
    for (int i = 0; i < 65; ++i) {
        tooManyHeaders.push_back(
            {QStringLiteral("X-Test-%1").arg(i),
             QStringLiteral("value")});
    }
    const auto tooMany = PlaybackLoadInput::create(
        QStringLiteral("https://fixture.invalid/audio"),
        tooManyHeaders);
    require(!tooMany.ok(),
            "more than 64 headers should fail");
    require(tooMany.error.code
                == PlaybackLoadInputErrorCode::TooManyHeaders,
            "header-count failure should be typed");

    const auto negativeWindow = PlaybackWindow::create(-1, 1000);
    require(!negativeWindow.ok()
                && negativeWindow.error.code
                    == PlaybackLoadInputErrorCode::InvalidPlaybackWindow,
            "negative playback-window start must fail closed");
    const auto emptyWindow = PlaybackWindow::create(1000, 1000);
    require(!emptyWindow.ok()
                && emptyWindow.error.code
                    == PlaybackLoadInputErrorCode::InvalidPlaybackWindow,
            "non-increasing playback-window bounds must fail closed");

    const PlaybackWindow cueWindow = mustLoad(
        PlaybackWindow::create(10987, 60000),
        "valid CUE-derived playback window should construct");
    require(cueWindow.durationMs().has_value()
                && *cueWindow.durationMs() == 49013,
            "bounded window duration should be end-start in integer ms");

    const PlaybackTimeTransform physicalTransform(cueWindow);
    require(physicalTransform.physicalPositionForTrack(2500).has_value()
                && *physicalTransform.physicalPositionForTrack(2500) == 13487,
            "track-relative time should map onto physical slice start");
    require(physicalTransform.trackPositionForPhysical(13487).has_value()
                && *physicalTransform.trackPositionForPhysical(13487) == 2500,
            "physical media time should map back to track-relative time");
    require(physicalTransform.trackPositionForPhysical(999999).has_value()
                && *physicalTransform.trackPositionForPhysical(999999) == 49013,
            "physical position beyond a bounded slice should clamp at logical end");
    require(physicalTransform.trackDurationForPhysicalDuration(120000).has_value()
                && *physicalTransform.trackDurationForPhysicalDuration(120000)
                    == 49013,
            "bounded duration must ignore the backing file's full duration");
    require(!physicalTransform.recordingPositionForTrack(2500).has_value(),
            "unknown recording-time offset must remain unknown");

    const PlaybackTimeTransform alignedTransform(cueWindow, -1000);
    require(alignedTransform.recordingPositionForTrack(1000).has_value()
                && *alignedTransform.recordingPositionForTrack(1000) == 0,
            "known negative offset should align a source intro to recording zero");
    require(!alignedTransform.recordingPositionForTrack(500).has_value(),
            "pre-recording source time must not manufacture negative recording time");
    require(alignedTransform.trackPositionForRecording(2000).has_value()
                && *alignedTransform.trackPositionForRecording(2000) == 3000,
            "recording-relative resume should invert a known source offset");

    const PlaybackTimeTransform oldSource(cueWindow, qint64{0});
    const auto preservedRecordingMs =
        oldSource.recordingPositionForTrack(12000);
    require(preservedRecordingMs.has_value()
                && *preservedRecordingMs == 12000,
            "old source should expose a recording-relative resume point");
    require(alignedTransform
                .trackPositionForRecording(*preservedRecordingMs)
                .has_value()
                && *alignedTransform
                        .trackPositionForRecording(*preservedRecordingMs)
                    == 13000,
            "same-occurrence source switch should preserve recording-relative position across different known offsets");

    const PlaybackLoadInput inputA = mustLoad(
        PlaybackLoadInput::create(
            QStringLiteral("https://fixture.invalid/audio a.mp3"),
            {
                {QStringLiteral("Referer"),
                 QStringLiteral("https://fixture.invalid/")},
                {QStringLiteral("X-Test"), QStringLiteral("alpha")},
            },
            cueWindow),
        "valid playback load input should construct");
    require(inputA.resource()
                == QStringLiteral("https://fixture.invalid/audio a.mp3"),
            "resource should remain opaque and preserve spaces");
    require(inputA.requestHeaders().size() == 2,
            "request headers should be preserved");
    require(inputA.requestHeaders().at(0).name
                == QStringLiteral("Referer"),
            "header spelling should be preserved");
    require(inputA.playbackWindow().has_value()
                && *inputA.playbackWindow() == cueWindow,
            "PlaybackLoadInput must retain exact validated physical bounds");

    const PlaybackLoadInput inputB = mustLoad(
        PlaybackLoadInput::create(
            QStringLiteral("file:///fixture/audio-b.flac")),
        "second playback input should construct");

    const RecordingMbid a =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid b =
        recording("22222222-2222-4222-8222-222222222222");

    VinylQueue queue;
    require(queue.append(occurrence("q-a", a)).ok,
            "append queue occurrence a");
    require(queue.append(occurrence("q-b", b)).ok,
            "append queue occurrence b");
    require(queue.activate(occurrenceId("q-a")).ok,
            "activate queue occurrence a");

    PlaybackSession session;
    FakePlaybackEngine engine;
    PlaybackEngineAdapter adapter(session, engine);

    const PlaybackWorkToken token1 = mustSession(
        adapter.beginOccurrence(*queue.activeOccurrence(), inputA, false),
        "adapter should begin active occurrence");
    require(session.state() == PlaybackState::Loading,
            "adapter begin should keep BA2-A loading state");
    require(engine.commands.size() == 1,
            "begin should issue exactly one engine load");
    requireCommand(engine, 0, CommandKind::Load, token1,
                   "first command should be load");
    require(engine.commands.at(0).loadInput.has_value(),
            "load command should retain resolved input");
    require(engine.commands.at(0).loadInput->resource()
                == inputA.resource(),
            "load command should preserve resource");
    require(engine.commands.at(0).loadInput->requestHeaders()
                == inputA.requestHeaders(),
            "load command should preserve request headers");

    require(adapter.play().ok,
            "play intent while loading should succeed");
    require(session.playWhenReady(),
            "play while loading should update BA2-A intent");
    require(engine.commands.size() == 1,
            "loading play intent should not pretend engine is loaded");

    require(adapter.pause().ok,
            "pause intent while loading should succeed");
    require(!session.playWhenReady(),
            "pause while loading should update BA2-A intent");
    require(engine.commands.size() == 1,
            "loading pause intent should not dispatch early command");

    require(adapter.engineLoaded(token1).ok,
            "matching loaded callback should be accepted");
    require(session.state() == PlaybackState::Paused,
            "loaded callback should honor latest paused intent");
    require(engine.commands.size() == 2,
            "loaded callback should apply exactly one paused command");
    requireCommand(engine, 1, CommandKind::Pause, token1,
                   "loaded paused intent should dispatch pause");

    const int afterFirstLoaded = engine.commands.size();
    const PlaybackMutationStatus duplicateLoaded =
        adapter.engineLoaded(token1);
    require(duplicateLoaded.ok && !duplicateLoaded.changed,
            "duplicate loaded callback should be idempotent");
    require(engine.commands.size() == afterFirstLoaded,
            "duplicate loaded callback should not redispatch state");

    require(adapter.play().ok,
            "play after load should succeed");
    require(session.state() == PlaybackState::Playing,
            "play should update BA2-A state");
    requireCommand(engine, 2, CommandKind::Play, token1,
                   "play should dispatch token-scoped engine command");

    const int afterPlay = engine.commands.size();
    const PlaybackMutationStatus playAgain = adapter.play();
    require(playAgain.ok && !playAgain.changed,
            "repeated play should be idempotent");
    require(engine.commands.size() == afterPlay,
            "repeated play should not redispatch");

    require(adapter.pause().ok,
            "pause after load should succeed");
    require(session.state() == PlaybackState::Paused,
            "pause should update BA2-A state");
    requireCommand(engine, 3, CommandKind::Pause, token1,
                   "pause should dispatch token-scoped engine command");

    const PlaybackSeekRequest seek1 = mustSession(
        adapter.seek(qint64{15000}),
        "first integer-ms adapter seek should succeed");
    requireCommand(engine, 4, CommandKind::Seek, token1,
                   "seek should dispatch exact token");
    require(engine.commands.at(4).seekRequest.has_value()
                && engine.commands.at(4).seekRequest->sequence()
                    == seek1.sequence()
                && engine.commands.at(4).seekTargetMs.has_value()
                && *engine.commands.at(4).seekTargetMs == 15000,
            "engine should receive exact seek identity plus integer-ms target");

    const PlaybackSeekRequest seek2 = mustSession(
        adapter.seek(qint64{25000}),
        "second integer-ms adapter seek should succeed");
    requireCommand(engine, 5, CommandKind::Seek, token1,
                   "second seek should retain same lifecycle token");
    require(engine.commands.at(5).seekRequest.has_value()
                && engine.commands.at(5).seekRequest->sequence()
                    == seek2.sequence()
                && engine.commands.at(5).seekTargetMs.has_value()
                && *engine.commands.at(5).seekTargetMs == 25000,
            "second engine seek should retain newer sequence and ms target");

    const PlaybackMutationStatus oldSeek =
        adapter.engineSeekApplied(
            PlaybackSeekCommand(seek1, 15000),
            qint64{15000});
    require(!oldSeek.ok
                && oldSeek.error.code
                    == PlaybackSessionErrorCode::SupersededSeek,
            "older same-generation seek completion should fail");
    require(!session.positionSeconds().has_value(),
            "superseded seek must not publish position");

    require(adapter.engineSeekApplied(
                PlaybackSeekCommand(seek2, 25000),
                qint64{24750})
                .ok,
            "latest integer-ms seek completion should be accepted");
    require(adapter.positionMs().has_value()
                && *adapter.positionMs() == 24750,
            "accepted seek completion should publish integer-ms position");

    require(adapter.engineDurationObserved(token1, qint64{180000}).ok,
            "integer-ms duration observation should flow through adapter");
    require(adapter.durationMs().has_value()
                && *adapter.durationMs() == 180000,
            "adapter should preserve integer-ms duration observation");

    const PlaybackWorkToken token2 = mustSession(
        adapter.beginOccurrence(queue.items().at(1), inputB, true),
        "adapter should begin replacement occurrence");
    require(token2 != token1,
            "replacement occurrence should have a new work token");
    require(session.state() == PlaybackState::Loading,
            "replacement occurrence should reset to loading");
    requireCommand(engine, 6, CommandKind::Load, token2,
                   "replacement begin should load with new token");

    const int beforeStaleLoaded = engine.commands.size();
    requireStale(adapter.engineLoaded(token1),
                 "old load callback should be stale");
    require(engine.commands.size() == beforeStaleLoaded,
            "stale loaded callback must not dispatch play/pause");

    requireStale(adapter.enginePositionObserved(token1, 999.0),
                 "old position callback should be stale");
    requireStale(
        adapter.engineFailed(
            token1,
            PlaybackFailure{QStringLiteral("old_error"),
                            QStringLiteral("old generation")}),
        "old failure callback should be stale");
    requireStale(adapter.engineEnded(token1),
                 "old EOF callback should be stale");
    requireStale(adapter.engineSeekApplied(seek2, 999.0),
                 "old seek completion should be stale");

    require(session.state() == PlaybackState::Loading,
            "stale callbacks must not mutate newer loading lifecycle");
    require(!session.positionSeconds().has_value()
                && !session.failure().has_value(),
            "stale observations must not leak into newer lifecycle");

    require(adapter.engineLoaded(token2).ok,
            "current replacement load should be accepted");
    require(session.state() == PlaybackState::Playing,
            "play-when-ready replacement should enter playing");
    requireCommand(engine, 7, CommandKind::Play, token2,
                   "current loaded callback should dispatch play");

    require(adapter.enginePauseObserved(token2, true).ok,
            "current pause observation should be accepted");
    require(session.state() == PlaybackState::Paused,
            "pause observation should reconcile BA2-A state");
    require(adapter.enginePauseObserved(token2, false).ok,
            "current resume observation should be accepted");
    require(session.state() == PlaybackState::Playing,
            "resume observation should reconcile BA2-A state");

    require(adapter.engineFailed(
                token2,
                PlaybackFailure{QStringLiteral("unknown"),
                                QStringLiteral("fixture failure")})
                .ok,
            "current failure should terminate lifecycle");
    require(session.state() == PlaybackState::Failed,
            "current engine failure should reach BA2-A failed state");

    const PlaybackWorkToken token3 = mustSession(
        adapter.beginOccurrence(queue.items().at(1), inputB, true),
        "same occurrence retry should begin a new generation");
    require(token3.occurrenceId() == token2.occurrenceId(),
            "same occurrence retry should retain occurrence identity");
    require(token3.generation() == token2.generation() + 1,
            "same occurrence retry should advance generation");
    requireCommand(engine, 8, CommandKind::Load, token3,
                   "same occurrence retry should issue new load");

    requireStale(adapter.engineLoaded(token2),
                 "prior generation of same occurrence should be stale");
    require(adapter.engineLoaded(token3).ok,
            "new generation loaded callback should be accepted");
    requireCommand(engine, 9, CommandKind::Play, token3,
                   "new generation should dispatch its own play");

    std::optional<PlaybackMutationStatus> stopHookStatus;
    engine.stopHook = [&](const PlaybackWorkToken &token) {
        stopHookStatus = adapter.engineEnded(token);
    };

    require(adapter.stop().ok,
            "adapter stop should succeed");
    require(session.state() == PlaybackState::Idle,
            "adapter stop should clear BA2-A before engine stop");
    requireCommand(engine, 10, CommandKind::Stop, token3,
                   "stop should dispatch exact old work token");
    require(stopHookStatus.has_value(),
            "fake engine should synchronously emit stop hook");
    requireStale(*stopHookStatus,
                 "callback during engine stop should already be stale");

    require(queue.size() == 2,
            "adapter commands must not mutate BA1 queue order");
    require(queue.activeOccurrenceId().has_value()
                && queue.activeOccurrenceId()->value()
                    == QStringLiteral("q-a"),
            "adapter commands must not change BA1 active occurrence");

    engine.stopHook = {};
    const PlaybackWorkToken token4 = mustSession(
        adapter.beginOccurrence(*queue.activeOccurrence(), inputA, true),
        "EOF fixture should begin");
    require(adapter.engineLoaded(token4).ok,
            "EOF fixture should load");
    const int beforeEof = engine.commands.size();
    require(adapter.engineEnded(token4).ok,
            "current EOF should reach BA2-A");
    require(session.state() == PlaybackState::Ended,
            "current EOF should terminate lifecycle as Ended");
    require(engine.commands.size() == beforeEof,
            "EOF should not invent an engine command or queue advance");
    require(queue.activeOccurrenceId().has_value()
                && queue.activeOccurrenceId()->value()
                    == QStringLiteral("q-a"),
            "EOF must leave BA1 active occurrence untouched");

    require(playbackLoadInputErrorCodeName(
                PlaybackLoadInputErrorCode::InvalidHeader)
                == QStringLiteral("invalid_header"),
            "load input errors should expose stable snake_case names");

    qInfo().noquote()
        << "VINYL_BA2B_ADAPTER_OK checks=" << g_checks;
    return 0;
}
