#include "ColosseumMpvBridge.h"
#include "MpvSidecarReference.h"

#include <QCoreApplication>
#include <QDebug>

#include <cmath>
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
T mustLoad(const vinyl::PlaybackLoadInputResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustSession(const vinyl::PlaybackSessionResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
        "fixture recording should parse");
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
        "fixture occurrence should construct");
}

enum class PlayerCommandKind {
    LoadPlain,
    LoadWithHeaders,
    SetPaused,
    SeekExact,
    Stop,
};

struct PlayerCommand final {
    PlayerCommandKind kind;
    vinyl::PlaybackWorkToken token;
    QString resource;
    QVector<vinyl::PlaybackRequestHeader> headers;
    QString perFileOptions;
    std::optional<bool> paused;
    std::optional<vinyl::PlaybackSeekCommand> seekCommand;
    std::optional<double> physicalSeekSeconds;

    PlayerCommand(PlayerCommandKind commandKind,
                  vinyl::PlaybackWorkToken workToken)
        : kind(commandKind)
        , token(std::move(workToken))
    {
    }
};

class FakeColosseumPlayer final : public vinyl::ColosseumMpvPlayerPort
{
public:
    void loadFile(const vinyl::PlaybackWorkToken &token,
                  const QString &resource,
                  const QString &perFileOptions) override
    {
        PlayerCommand command(PlayerCommandKind::LoadPlain, token);
        command.resource = resource;
        command.perFileOptions = perFileOptions;
        commands.push_back(std::move(command));
    }

    void loadFileWithHeaders(
        const vinyl::PlaybackWorkToken &token,
        const QString &resource,
        const QVector<vinyl::PlaybackRequestHeader> &headers,
        const QString &perFileOptions) override
    {
        PlayerCommand command(PlayerCommandKind::LoadWithHeaders, token);
        command.resource = resource;
        command.headers = headers;
        command.perFileOptions = perFileOptions;
        commands.push_back(std::move(command));
    }

    void setPaused(const vinyl::PlaybackWorkToken &token,
                   bool paused) override
    {
        PlayerCommand command(PlayerCommandKind::SetPaused, token);
        command.paused = paused;
        commands.push_back(std::move(command));
    }

    void seekExact(const vinyl::PlaybackSeekCommand &command,
                   double physicalSeconds) override
    {
        PlayerCommand recorded(
            PlayerCommandKind::SeekExact,
            command.workToken());
        recorded.seekCommand = command;
        recorded.physicalSeekSeconds = physicalSeconds;
        commands.push_back(std::move(recorded));
    }

    void stop(const vinyl::PlaybackWorkToken &token) override
    {
        commands.push_back(PlayerCommand(PlayerCommandKind::Stop, token));
        if (stopHook)
            stopHook(token);
    }

    QVector<PlayerCommand> commands;
    std::function<void(const vinyl::PlaybackWorkToken &)> stopHook;
};

void requireCommand(const FakeColosseumPlayer &player,
                    int index,
                    PlayerCommandKind kind,
                    const vinyl::PlaybackWorkToken &token,
                    const char *message)
{
    require(index >= 0 && index < player.commands.size(), message);
    require(player.commands.at(index).kind == kind,
            "player command kind should match");
    require(player.commands.at(index).token == token,
            "player command must retain the exact work token");
}

void requireStale(const vinyl::PlaybackMutationStatus &status,
                  const char *message)
{
    require(!status.ok, message);
    require(status.error.code == vinyl::PlaybackSessionErrorCode::StaleAsyncWork,
            "old player callback should be rejected as stale async work");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "BA2-D harness must execute against Qt 6.11.1");

    const RecordingMbid a =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid b =
        recording("22222222-2222-4222-8222-222222222222");

    const PlaybackWindow boundedWindow = mustLoad(
        PlaybackWindow::create(10000, 70000),
        "bounded window should construct");
    require(mpvPerFilePlaybackWindowOptions(boundedWindow)
                == QStringLiteral("start=10.000,end=70.000"),
            "bounded window should map to deterministic mpv per-file options");

    const PlaybackWindow openWindow = mustLoad(
        PlaybackWindow::create(5000),
        "open window should construct");
    require(mpvPerFilePlaybackWindowOptions(openWindow)
                == QStringLiteral("start=5.000"),
            "open window should map only a physical start");
    require(mpvPerFilePlaybackWindowOptions(std::nullopt).isEmpty(),
            "absence of a playback window must not invent mpv options");

    const PlaybackLoadInput headerInput = mustLoad(
        PlaybackLoadInput::create(
            QStringLiteral("https://fixture.invalid/a.mp3"),
            {
                {QStringLiteral("Referer"), QStringLiteral("https://fixture.invalid/")},
                {QStringLiteral("Origin"), QStringLiteral("https://fixture.invalid")},
            },
            boundedWindow),
        "header-bearing bounded load input should construct");

    const PlaybackLoadInput plainInput = mustLoad(
        PlaybackLoadInput::create(
            QStringLiteral("file:///fixture/b.flac")),
        "plain load input should construct");

    PlaybackSession session;
    FakeColosseumPlayer player;
    ColosseumMpvTimeState timeState;
    ColosseumMpvCommands commands(player, timeState);
    PlaybackEngineAdapter adapter(session, commands);
    ColosseumMpvCallbacks callbacks(adapter, timeState);

    const PlaybackWorkToken token1 = mustSession(
        adapter.beginOccurrence(
            occurrence("occ-a", a),
            headerInput,
            true),
        "first bounded occurrence should begin");

    require(player.commands.size() == 1,
            "begin should issue exactly one concrete player load");
    requireCommand(
        player, 0, PlayerCommandKind::LoadWithHeaders, token1,
        "header load should use header-bearing path");
    require(player.commands.at(0).perFileOptions
                == QStringLiteral("start=10.000,end=70.000"),
            "physical playback bounds must reach concrete mpv load options");

    require(callbacks.loaded(token1).ok,
            "current loaded callback should be accepted");
    require(session.state() == PlaybackState::Playing,
            "play-when-ready load should enter Playing");

    require(callbacks.positionObserved(token1, 12.250).ok,
            "physical position observation should be accepted");
    require(adapter.positionMs().has_value()
                && *adapter.positionMs() == 2250,
            "position above bridge must be track-relative integer milliseconds");

    require(callbacks.durationObserved(token1, 180.0).ok,
            "physical duration observation should be accepted");
    require(adapter.durationMs().has_value()
                && *adapter.durationMs() == 60000,
            "bounded duration must equal end-start, not physical-file duration");

    require(callbacks.positionObserved(token1, 75.0).ok,
            "slight or late position beyond logical end should remain mappable");
    require(adapter.positionMs().has_value()
                && *adapter.positionMs() == 60000,
            "bounded position must clamp at the logical track end");

    const PlaybackSeekRequest seek1 = mustSession(
        adapter.seek(qint64{33000}),
        "integer-ms logical seek1 should be accepted");
    requireCommand(
        player, 2, PlayerCommandKind::SeekExact, token1,
        "seek should retain work token");
    require(player.commands.at(2).seekCommand.has_value()
                && player.commands.at(2).seekCommand->sequence()
                    == seek1.sequence()
                && player.commands.at(2).seekCommand->targetMs() == 33000,
            "concrete bridge must retain full seek identity plus ms target");
    require(player.commands.at(2).physicalSeekSeconds.has_value()
                && std::abs(*player.commands.at(2).physicalSeekSeconds - 43.0)
                    < 0.0001,
            "logical 33s seek in a 10s slice must target physical 43s");

    const PlaybackSeekRequest seek2 = mustSession(
        adapter.seek(qint64{44000}),
        "integer-ms logical seek2 should be accepted");
    requireCommand(
        player, 3, PlayerCommandKind::SeekExact, token1,
        "second seek should retain token");
    require(player.commands.at(3).physicalSeekSeconds.has_value()
                && std::abs(*player.commands.at(3).physicalSeekSeconds - 54.0)
                    < 0.0001,
            "logical 44s seek must be offset by physical playback-window start");

    const PlaybackMutationStatus oldSeek =
        callbacks.seekApplied(
            *player.commands.at(2).seekCommand,
            43.0);
    require(!oldSeek.ok
                && oldSeek.error.code
                    == PlaybackSessionErrorCode::SupersededSeek,
            "older same-generation seek completion should remain superseded");

    require(callbacks.seekApplied(
                *player.commands.at(3).seekCommand,
                53.750)
                .ok,
            "latest exact seek completion should be accepted");
    require(adapter.positionMs().has_value()
                && *adapter.positionMs() == 43750,
            "seek completion must report track-relative integer milliseconds");

    const PlaybackWorkToken token2 = mustSession(
        adapter.beginOccurrence(
            occurrence("occ-b", b),
            plainInput,
            false),
        "replacement unbounded occurrence should begin");
    require(token2 != token1,
            "replacement occurrence should use a new work token");
    requireCommand(
        player, 4, PlayerCommandKind::LoadPlain, token2,
        "header-free input should use plain load path");
    require(player.commands.at(4).perFileOptions.isEmpty(),
            "unbounded transport must not inherit old start/end options");

    requireStale(
        callbacks.positionObserved(token1, 999.0),
        "old physical position must remain stale after replacement");
    requireStale(
        callbacks.seekApplied(
            *player.commands.at(3).seekCommand,
            54.0),
        "old seek completion must remain stale after replacement");

    require(callbacks.loaded(token2).ok,
            "replacement loaded callback should be accepted");
    require(session.state() == PlaybackState::Paused,
            "playWhenReady=false should enter Paused");
    require(callbacks.positionObserved(token2, 12.250).ok,
            "unbounded physical position should be accepted");
    require(adapter.positionMs().has_value()
                && *adapter.positionMs() == 12250,
            "no-window position should remain same-timestamp in milliseconds");

    const PlaybackMutationStatus invalidTime =
        callbacks.positionObserved(token2, -1.0);
    require(!invalidTime.ok
                && invalidTime.error.code
                    == PlaybackSessionErrorCode::InvalidTimeValue,
            "invalid concrete mpv time must fail before reaching domain state");

    const PlaybackWorkToken token3 = mustSession(
        adapter.beginOccurrence(
            occurrence("occ-b", b),
            plainInput,
            true),
        "same occurrence retry should begin new generation");
    require(token3.occurrenceId() == token2.occurrenceId()
                && token3.generation() == token2.generation() + 1,
            "retry must preserve occurrence identity and advance generation");

    std::optional<PlaybackMutationStatus> synchronousStopCallback;
    player.stopHook = [&](const PlaybackWorkToken &token) {
        synchronousStopCallback = callbacks.ended(token);
    };

    require(adapter.stop().ok,
            "stop should succeed");
    require(session.state() == PlaybackState::Idle,
            "BA2-B must invalidate lifecycle before concrete stop");
    require(synchronousStopCallback.has_value(),
            "fake player should emit synchronous stop callback");
    requireStale(
        *synchronousStopCallback,
        "callback emitted during stop must already be stale");


    const QString repairCase = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (repairCase.isEmpty() || repairCase == "bounded-rejection") {
        const auto token = mustSession(adapter.beginOccurrence(occurrence("bounded", a), headerInput), "begin bounded");
        require(callbacks.loaded(token).ok, "load bounded");
        const int before = player.commands.size();
        require(!adapter.seek(qint64{61000}).ok() && !session.pendingSeek().has_value() && player.commands.size() == before,
            "M0R out-of-window seek must reject without stranded pending state");
    }
    if (repairCase.isEmpty() || repairCase == "missing-transform") {
        const auto token = mustSession(adapter.beginOccurrence(occurrence("missing-transform", a), headerInput), "begin missing transform");
        require(callbacks.loaded(token).ok, "load missing transform"); timeState.clearFor(token);
        require(!adapter.seek(qint64{1000}).ok() && !session.pendingSeek().has_value(), "M0R missing transform seek must reject");
        require(!callbacks.positionObserved(token, 10.0).ok && !adapter.positionMs().has_value(),
            "missing transform must not manufacture zero position");
        require(!callbacks.durationObserved(token, 10.0).ok && !adapter.durationMs().has_value(),
            "missing transform must not manufacture zero duration");
    }
    if (repairCase.isEmpty() || repairCase == "integer-boundary") {
        require(!millisecondsFromMpvSeconds(9223372036854776.0).has_value(), "M0R mpv seconds overflow must reject");
        require(!adapter.seek(9223372036854776.0).ok(), "M0R adapter seconds overflow must reject");
    }


    if (repairCase.isEmpty() || repairCase == "unobserved-seek") {
        const auto token = mustSession(adapter.beginOccurrence(occurrence("startup-seek", a), headerInput), "startup fixture");
        callbacks.loaded(token);
        const auto request = mustSession(adapter.seek(qint64{2000}), "startup seek request");
        SerializedMpvSeekState state; state.bindWork(token); state.submit(PlaybackSeekCommand(request, 2000));
        state.observeSeekStart(token);
        require(!state.observePlaybackRestart(token).has_value() && state.hasPhysicalSeekInFlight(),
            "M0R startup restart without observed target must not complete a user seek");
    }
    if (repairCase.isEmpty() || repairCase == "seek-sequence") {
        const auto token = mustSession(adapter.beginOccurrence(occurrence("seek-order", a), headerInput), "order fixture");
        callbacks.loaded(token);
        const auto first = mustSession(adapter.seek(qint64{1000}), "first request");
        const auto second = mustSession(adapter.seek(qint64{2000}), "second request");
        SerializedMpvSeekState state; state.bindWork(token); state.submit(PlaybackSeekCommand(second, 2000));
        require(state.submit(PlaybackSeekCommand(first, 1000)).disposition == MpvSeekSubmitDisposition::RejectedStale,
            "M0R older seek sequence must not displace newer physical work");
    }

    if (repairCase.isEmpty() || repairCase == "seek-recovery") {
        const auto token = mustSession(adapter.beginOccurrence(occurrence("recovery", a), headerInput), "recovery fixture");
        callbacks.loaded(token);
        const auto first = mustSession(adapter.seek(qint64{2000}), "recovery first");
        const auto latest = mustSession(adapter.seek(qint64{3000}), "recovery latest");
        const PlaybackSeekCommand physical(first, 2000), queued(latest, 3000);
        SerializedMpvSeekState state; state.bindWork(token); state.submit(physical, 100); state.submit(queued, 200);
        state.observeSeekStart(token);
        require(!state.observePlaybackRestart(token, qint64{0}, 300).has_value(), "unrelated restart outside target must not acknowledge");
        require(!state.failActive(queued).has_value(), "queued command cannot report a physical dispatch failure");
        auto staleFailure = adapter.engineSeekFailed(first, QStringLiteral("late_old_failure"));
        require(!staleFailure.ok && !staleFailure.changed && session.pendingSeek().has_value()
            && session.pendingSeek()->sequence() == latest.sequence(), "stale physical failure must preserve latest logical pending request");
        require(!state.expire(10099).has_value(), "deadline must not fire early");
        auto timeout = state.expire(10100);
        require(timeout.has_value() && timeout->sequence() == latest.sequence()
            && !state.hasPhysicalSeekInFlight() && !state.hasQueuedSeek() && !state.boundWork().has_value(),
            "timeout must retire physical and latest queued ownership");
        auto rejected = adapter.engineSeekFailed(timeout->request(), QStringLiteral("seek_timeout"));
        require(!rejected.ok && rejected.changed && rejected.error.code == PlaybackSessionErrorCode::SeekFailed
            && !session.pendingSeek().has_value(), "typed timeout failure must clear logical pending seek");
        require(!state.observePlaybackRestart(token, qint64{2000}, 10200).has_value(), "late restart after timeout must be ignored");
        state.bindWork(token);
        require(!state.boundWork().has_value(), "quarantined generation cannot be rearmed");
        auto fresh = [&]() {
            auto work = mustSession(adapter.beginOccurrence(occurrence("recovery", a), headerInput), "fresh recovery work");
            callbacks.loaded(work);
            auto request = mustSession(adapter.seek(qint64{3000}), "fresh recovery seek");
            state.bindWork(work);
            return std::make_pair(work, PlaybackSeekCommand(request, 3000));
        };
        auto retry = fresh(); state.submit(retry.second, 11000);
        auto failed = state.failActive(retry.second);
        require(failed.has_value() && !state.boundWork().has_value(), "physical command error must release all seek ownership");
        auto cancelledWork = fresh(); state.submit(cancelledWork.second, 12000);
        require(!state.cancelWork(token).has_value() && state.hasPhysicalSeekInFlight(), "old EOF must preserve newer seek ownership");
        require(state.cancelWork(cancelledWork.first).has_value() && !state.hasPhysicalSeekInFlight(), "EOF or cancellation must clear seek work");
        auto observed = fresh(); state.submit(observed.second, 13000); state.observeSeekStart(observed.first);
        require(!state.observePlaybackRestart(observed.first, qint64{1000}, 13010).has_value(), "out-of-target restart remains pending");
        auto completed = state.observePlaybackRestart(observed.first, qint64{3000}, 13020);
        require(completed.has_value() && completed->completed.sequence() == observed.second.sequence(), "only observed target acknowledges exact request");
        const auto cancelled = mustSession(adapter.seek(qint64{1000}), "logical cancellation fixture");
        require(adapter.engineSeekCancelled(cancelled).ok && !session.pendingSeek().has_value(), "logical cancellation must clear exact pending request");
    }
    qInfo().noquote()
        << "VINYL_W4A_MPV_BRIDGE_OK checks=" << g_checks
        << "qt=" << qVersion();

    return 0;
}
