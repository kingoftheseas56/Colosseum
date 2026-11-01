#include "QueueSessionCoordinator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QVector>

#include <cstdlib>
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

vinyl::PlaybackLoadInput inputForName(const char *name)
{
    const auto made = vinyl::PlaybackLoadInput::create(
        QStringLiteral("fixture:///") + QString::fromLatin1(name));
    require(made.ok(), "fixture playback input should construct");
    return *made.value;
}

class FakeInputProvider final : public vinyl::PlaybackLoadInputProvider
{
public:
    struct Entry final {
        vinyl::QueueOccurrenceId id;
        vinyl::PlaybackLoadInput input;
    };

    void add(const vinyl::QueueOccurrenceId &id,
             vinyl::PlaybackLoadInput input)
    {
        entries.push_back(Entry{id, std::move(input)});
    }

    void add(const char *id)
    {
        add(occurrenceId(id), inputForName(id));
    }

    std::optional<vinyl::PlaybackLoadInput> inputFor(
        const vinyl::QueueOccurrence &occurrence) override
    {
        requests.push_back(occurrence.occurrenceId());
        for (const Entry &entry : entries) {
            if (entry.id == occurrence.occurrenceId())
                return entry.input;
        }
        return std::nullopt;
    }

    QVector<Entry> entries;
    QVector<vinyl::QueueOccurrenceId> requests;
};

enum class CommandKind {
    Load,
    Play,
    Pause,
    Seek,
    Stop,
};

struct EngineCommand final {
    CommandKind kind;
    vinyl::PlaybackWorkToken token;
    QString resource;

    EngineCommand(CommandKind kindValue,
                  vinyl::PlaybackWorkToken tokenValue,
                  QString resourceValue = QString())
        : kind(kindValue)
        , token(std::move(tokenValue))
        , resource(std::move(resourceValue))
    {
    }
};

class FakeEngine final : public vinyl::PlaybackEngineCommands
{
public:
    void load(const vinyl::PlaybackWorkToken &token,
              const vinyl::PlaybackLoadInput &input) override
    {
        commands.push_back(
            EngineCommand(CommandKind::Load, token, input.resource()));
    }

    void play(const vinyl::PlaybackWorkToken &token) override
    {
        commands.push_back(EngineCommand(CommandKind::Play, token));
    }

    void pause(const vinyl::PlaybackWorkToken &token) override
    {
        commands.push_back(EngineCommand(CommandKind::Pause, token));
    }

    void seek(const vinyl::PlaybackSeekRequest &request) override
    {
        commands.push_back(
            EngineCommand(CommandKind::Seek, request.workToken()));
    }

    void stop(const vinyl::PlaybackWorkToken &token) override
    {
        commands.push_back(EngineCommand(CommandKind::Stop, token));
    }

    int count(CommandKind kind) const
    {
        int total = 0;
        for (const EngineCommand &command : commands) {
            if (command.kind == kind)
                ++total;
        }
        return total;
    }

    QVector<EngineCommand> commands;
};

struct Fixture final {
    vinyl::VinylQueue queue;
    vinyl::PlaybackSession session;
    FakeEngine engine;
    vinyl::PlaybackEngineAdapter adapter;
    FakeInputProvider inputs;
    vinyl::QueueSessionCoordinator coordinator;

    Fixture()
        : adapter(session, engine)
        , coordinator(queue, session, adapter, inputs)
    {
    }
};

void append(Fixture &fixture,
            const char *id,
            const vinyl::RecordingMbid &recordingMbid,
            bool addInput = true)
{
    require(fixture.queue.append(occurrence(id, recordingMbid)).ok,
            "fixture queue append should succeed");
    if (addInput)
        fixture.inputs.add(id);
}

vinyl::PlaybackWorkToken activeToken(const Fixture &fixture)
{
    require(fixture.session.activeWorkToken().has_value(),
            "fixture should have active playback token");
    return *fixture.session.activeWorkToken();
}

void requireActiveQueue(const Fixture &fixture, const char *id)
{
    require(fixture.queue.activeOccurrenceId().has_value(),
            "queue should have active occurrence");
    require(fixture.queue.activeOccurrenceId()->value()
                == QString::fromLatin1(id),
            "queue active occurrence should match expected ID");
}

void requireActiveSession(const Fixture &fixture, const char *id)
{
    require(fixture.session.activeOccurrence().has_value(),
            "session should have active occurrence");
    require(fixture.session.activeOccurrence()->occurrenceId().value()
                == QString::fromLatin1(id),
            "session active occurrence should match expected ID");
}

void loadCurrent(Fixture &fixture)
{
    const auto status =
        fixture.coordinator.engineLoaded(activeToken(fixture));
    require(status.ok, "current load callback should succeed");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    const RecordingMbid songA =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid songB =
        recording("22222222-2222-4222-8222-222222222222");
    const RecordingMbid songC =
        recording("33333333-3333-4333-8333-333333333333");

    {
        Fixture empty;
        const QueueSessionStatus status = empty.coordinator.start();
        require(!status.ok,
                "explicit start on empty queue should fail");
        require(status.error.code == QueueSessionErrorCode::EmptyQueue,
                "empty start should be typed");
    }

    {
        Fixture missing;
        append(missing, "missing-a", songA, false);
        const QueueSessionStatus status = missing.coordinator.start();
        require(status.ok && status.queueChanged && !status.playbackChanged,
                "explicit start without input should enter pending state");
        requireActiveQueue(missing, "missing-a");
        require(missing.session.state() == PlaybackState::Idle,
                "pending input must leave BA2 idle");
        require(!missing.session.activeOccurrence().has_value(),
                "pending input must not invent a BA2 binding");
        require(missing.coordinator.pendingInput().has_value(),
                "pending input should be retained by coordinator");
        require(missing.coordinator.pendingInput()->occurrenceId
                    == occurrenceId("missing-a")
                    && missing.coordinator.pendingInput()->playWhenReady,
                "pending input should preserve exact occurrence and intent");
        require(missing.engine.count(CommandKind::Load) == 0,
                "missing input must not issue engine load");
    }

    {
        Fixture f;
        append(f, "a", songA);
        append(f, "b", songB);
        append(f, "c", songC);

        const QueueSessionStatus started = f.coordinator.start();
        require(started.ok && started.queueChanged
                    && started.playbackChanged,
                "explicit start should activate and begin first item");
        requireActiveQueue(f, "a");
        requireActiveSession(f, "a");
        const PlaybackWorkToken tokenA = activeToken(f);
        require(tokenA.generation() == 1,
                "first explicit start should use generation one");
        require(f.engine.count(CommandKind::Load) == 1,
                "explicit start should issue one load");

        loadCurrent(f);
        require(f.session.state() == PlaybackState::Playing,
                "loaded explicit start should be playing");
        const quint64 stableGeneration = f.session.generation();
        const int stableLoads = f.engine.count(CommandKind::Load);
        const int stableStops = f.engine.count(CommandKind::Stop);

        require(f.coordinator.append(occurrence("d", songA)).ok,
                "append through coordinator should succeed");
        require(f.session.generation() == stableGeneration,
                "append must not restart active playback");
        require(f.engine.count(CommandKind::Load) == stableLoads
                    && f.engine.count(CommandKind::Stop) == stableStops,
                "append must not touch engine lifecycle");

        f.inputs.add("next");
        require(f.coordinator.playNext(
                    occurrence("next", songB)).ok,
                "Play Next should succeed");
        require(f.session.generation() == stableGeneration,
                "Play Next must not restart active playback");

        f.inputs.add("before");
        require(f.coordinator.insert(
                    0, occurrence("before", songC)).ok,
                "insert before active should succeed");
        requireActiveQueue(f, "a");
        requireActiveSession(f, "a");
        require(f.queue.activeIndex() > 0,
                "insert before active should change active index");
        require(f.session.generation() == stableGeneration,
                "active index movement must not restart playback");

        require(f.coordinator.move(
                    occurrenceId("a"),
                    f.queue.size() - 1).ok,
                "moving active occurrence should succeed");
        requireActiveQueue(f, "a");
        requireActiveSession(f, "a");
        require(f.session.generation() == stableGeneration,
                "moving active occurrence must not restart playback");

        require(f.coordinator.remove(
                    occurrenceId("before")).ok,
                "removing non-active item should succeed");
        require(f.session.generation() == stableGeneration,
                "non-active removal must not restart playback");

        require(f.coordinator.clearAfterCurrent().ok,
                "clear after current should succeed");
        requireActiveQueue(f, "a");
        requireActiveSession(f, "a");
        require(f.session.generation() == stableGeneration,
                "clear-after-current with surviving active must not restart playback");

        f.inputs.add("now");
        const QueueSessionStatus playNow =
            f.coordinator.playNow(occurrence("now", songA));
        require(playNow.ok && playNow.queueChanged
                    && playNow.playbackChanged,
                "Play Now should change queue and playback once");
        requireActiveQueue(f, "now");
        requireActiveSession(f, "now");
        const PlaybackWorkToken tokenNow = activeToken(f);
        require(tokenNow.occurrenceId() != tokenA.occurrenceId(),
                "duplicate Recording MBID must still use distinct occurrence ID");
        require(f.session.activeOccurrence()->recordingMbid() == songA,
                "duplicate occurrence may share Recording MBID");
        require(tokenNow.generation() == stableGeneration + 1,
                "Play Now should create exactly one new playback generation");
        require(f.engine.count(CommandKind::Stop) == stableStops + 1,
                "Play Now should stop old lifecycle exactly once");
        require(f.engine.count(CommandKind::Load) == stableLoads + 1,
                "Play Now should load new lifecycle exactly once");
        require(f.queue.find(occurrenceId("a")) != nullptr,
                "Play Now must preserve old active occurrence in queue");

        const QueueSessionStatus staleLoad =
            f.coordinator.engineLoaded(tokenA);
        require(!staleLoad.ok,
                "old load callback after Play Now should be stale");
        const QueueSessionStatus stalePosition =
            f.coordinator.enginePositionObserved(tokenA, 999.0);
        require(!stalePosition.ok,
                "old position callback after Play Now should be stale");
        const QueueSessionStatus staleError =
            f.coordinator.engineFailed(
                tokenA,
                PlaybackFailure{QStringLiteral("old"),
                                QStringLiteral("stale")});
        require(!staleError.ok,
                "old error callback after Play Now should be stale");
        requireActiveSession(f, "now");
        require(!f.session.positionSeconds().has_value(),
                "stale position must not leak into replacement lifecycle");
        require(!f.session.failure().has_value(),
                "stale error must not fail replacement lifecycle");
    }

    {
        Fixture middle;
        append(middle, "m-a", songA);
        append(middle, "m-b", songB);
        append(middle, "m-c", songC);
        require(middle.coordinator.activate(
                    occurrenceId("m-b")).ok,
                "activate middle item should begin playback");
        loadCurrent(middle);
        const PlaybackWorkToken old = activeToken(middle);
        const quint64 generation = middle.session.generation();
        const int loads = middle.engine.count(CommandKind::Load);

        const QueueSessionStatus removed =
            middle.coordinator.remove(occurrenceId("m-b"));
        require(removed.ok && removed.queueChanged
                    && removed.playbackChanged,
                "removing active middle should coordinate replacement");
        requireActiveQueue(middle, "m-c");
        requireActiveSession(middle, "m-c");
        require(middle.session.generation() == generation + 1,
                "active middle removal should start successor once");
        require(middle.engine.count(CommandKind::Load) == loads + 1,
                "active middle removal should load successor once");
        require(!middle.coordinator.engineLoaded(old).ok,
                "removed active occurrence callback should be stale");
    }

    {
        Fixture tail;
        append(tail, "t-a", songA);
        append(tail, "t-b", songB);
        require(tail.coordinator.activate(
                    occurrenceId("t-b")).ok,
                "activate tail should begin playback");
        loadCurrent(tail);
        const quint64 generation = tail.session.generation();

        require(tail.coordinator.remove(
                    occurrenceId("t-b")).ok,
                "removing active tail should succeed");
        requireActiveQueue(tail, "t-a");
        requireActiveSession(tail, "t-a");
        require(tail.session.generation() == generation + 1,
                "active tail removal should start predecessor once");
    }

    {
        Fixture finalItem;
        append(finalItem, "only", songA);
        require(finalItem.coordinator.start().ok,
                "single-item queue should start");
        loadCurrent(finalItem);
        const int loads = finalItem.engine.count(CommandKind::Load);

        require(finalItem.coordinator.remove(
                    occurrenceId("only")).ok,
                "removing final active item should succeed");
        require(finalItem.queue.empty(),
                "removing final active item should empty queue");
        require(!finalItem.queue.activeOccurrenceId().has_value(),
                "removing final active item should clear BA1 active");
        require(finalItem.session.state() == PlaybackState::Idle,
                "removing final active item should clear BA2 lifecycle");
        require(finalItem.engine.count(CommandKind::Load) == loads,
                "final removal must not invent replacement load");
    }

    {
        Fixture eof;
        append(eof, "e-a", songA);
        append(eof, "e-b", songB);
        require(eof.coordinator.start().ok,
                "EOF fixture should start first item");
        loadCurrent(eof);
        const PlaybackWorkToken tokenA = activeToken(eof);
        const int loadsBefore = eof.engine.count(CommandKind::Load);

        const QueueSessionStatus ended =
            eof.coordinator.engineEnded(tokenA);
        require(ended.ok && ended.queueChanged
                    && ended.playbackChanged,
                "accepted EOF should advance queue and playback once");
        requireActiveQueue(eof, "e-b");
        requireActiveSession(eof, "e-b");
        const PlaybackWorkToken tokenB = activeToken(eof);
        require(eof.engine.count(CommandKind::Load) == loadsBefore + 1,
                "accepted EOF should load successor once");

        const int queueIndexAfterFirstEof = eof.queue.activeIndex();
        const int loadsAfterFirstEof = eof.engine.count(CommandKind::Load);
        const QueueSessionStatus duplicateOld =
            eof.coordinator.engineEnded(tokenA);
        require(!duplicateOld.ok,
                "duplicate old EOF after transition should be stale");
        require(eof.queue.activeIndex() == queueIndexAfterFirstEof,
                "duplicate EOF must not advance queue twice");
        require(eof.engine.count(CommandKind::Load) == loadsAfterFirstEof,
                "duplicate EOF must not load another occurrence");

        loadCurrent(eof);
        require(eof.coordinator.stop().ok,
                "stop before same-occurrence restart should succeed");
        requireActiveQueue(eof, "e-b");
        require(eof.session.state() == PlaybackState::Idle,
                "stop should clear BA2 only");

        require(eof.coordinator.start().ok,
                "explicit start should restart retained active occurrence");
        const PlaybackWorkToken tokenB2 = activeToken(eof);
        require(tokenB2.occurrenceId() == tokenB.occurrenceId(),
                "restart should retain exact queue occurrence");
        require(tokenB2.generation() == tokenB.generation() + 1,
                "restart should create new generation");
        const int activeBeforeStaleEof = eof.queue.activeIndex();
        require(!eof.coordinator.engineEnded(tokenB).ok,
                "older generation EOF should be stale");
        require(eof.queue.activeIndex() == activeBeforeStaleEof,
                "stale old-generation EOF must not advance queue");

        loadCurrent(eof);
        require(eof.coordinator.engineEnded(tokenB2).ok,
                "final current EOF should succeed");
        require(!eof.queue.activeOccurrenceId().has_value(),
                "final EOF should exhaust queue without wrap");
        require(eof.session.state() == PlaybackState::Idle,
                "final EOF should clear playback binding after exhaustion");
        const int loadsAtExhaustion = eof.engine.count(CommandKind::Load);

        require(!eof.coordinator.engineEnded(tokenB2).ok,
                "duplicate final EOF should be stale after exhaustion");
        require(!eof.queue.activeOccurrenceId().has_value(),
                "duplicate final EOF must not reactivate item one");
        require(eof.engine.count(CommandKind::Load) == loadsAtExhaustion,
                "duplicate final EOF must not load item one");

        require(eof.coordinator.start().ok,
                "new explicit start may start exhausted queue again");
        requireActiveQueue(eof, "e-a");
        requireActiveSession(eof, "e-a");
        require(eof.engine.count(CommandKind::Load) == loadsAtExhaustion + 1,
                "explicit restart after exhaustion should issue one new load");
    }

    {
        Fixture stop;
        append(stop, "s-a", songA);
        append(stop, "s-b", songB);
        require(stop.coordinator.start().ok,
                "stop fixture should start");
        loadCurrent(stop);
        const PlaybackWorkToken firstToken = activeToken(stop);
        const QVector<QueueOccurrence> queueBefore = stop.queue.items();

        const QueueSessionStatus stopped = stop.coordinator.stop();
        require(stopped.ok && stopped.playbackChanged
                    && !stopped.queueChanged,
                "coordinator stop should change playback only");
        requireActiveQueue(stop, "s-a");
        require(stop.queue.size() == queueBefore.size(),
                "stop should preserve queue size");
        require(stop.queue.items().at(0).occurrenceId()
                    == queueBefore.at(0).occurrenceId()
                && stop.queue.items().at(1).occurrenceId()
                    == queueBefore.at(1).occurrenceId(),
                "stop should preserve queue order");
        require(stop.session.state() == PlaybackState::Idle,
                "stop should clear playback lifecycle");

        require(stop.coordinator.start().ok,
                "explicit start should resume retained active occurrence");
        const PlaybackWorkToken restarted = activeToken(stop);
        require(restarted.occurrenceId() == firstToken.occurrenceId(),
                "restart after stop should use same queue occurrence");
        require(restarted.generation() == firstToken.generation() + 1,
                "restart after stop should use fresh generation");
    }

    {
        Fixture missingTransition;
        append(missingTransition, "x-a", songA);
        append(missingTransition, "x-b", songB, false);
        require(missingTransition.coordinator.start().ok,
                "missing-transition fixture should start first");
        loadCurrent(missingTransition);

        const QueueSessionStatus removed =
            missingTransition.coordinator.remove(
                occurrenceId("x-a"));
        require(removed.ok && removed.queueChanged && removed.playbackChanged,
                "active transition without input should wait honestly");
        requireActiveQueue(missingTransition, "x-b");
        require(missingTransition.session.state() == PlaybackState::Idle,
                "missing replacement input should leave BA2 idle");
        require(!missingTransition.session.activeOccurrence().has_value(),
                "missing replacement input should not leave old BA2 binding");
        require(missingTransition.coordinator.pendingInput().has_value()
                    && missingTransition.coordinator.pendingInput()->occurrenceId
                        == occurrenceId("x-b")
                    && missingTransition.coordinator.pendingInput()->playWhenReady,
                "active transition should carry play intent into pending input");
    }

    require(queueSessionErrorCodeName(
                QueueSessionErrorCode::MissingPlaybackInput)
                == QStringLiteral("missing_playback_input"),
            "coordinator errors should expose stable snake_case names");

    qInfo().noquote()
        << "VINYL_BA2C_COORDINATION_OK checks=" << g_checks;
    return 0;
}
