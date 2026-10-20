#include "QueueSessionCoordinator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QVector>

#include <cstdlib>
#include <optional>
#include <variant>

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
T mustDomain(const vinyl::DomainResult<T> &result)
{
    require(result.ok(), "fixture domain value should parse");
    return *result.value;
}

template <typename T>
T mustQueue(const vinyl::QueueResult<T> &result)
{
    require(result.ok(), "fixture queue value should construct");
    return *result.value;
}

vinyl::QueueOccurrenceId qid(const char *text)
{
    return mustQueue(
        vinyl::QueueOccurrenceId::parse(QString::fromLatin1(text)));
}

vinyl::RecordingMbid recording(const char *text)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(text)));
}

vinyl::QueueOccurrence occurrence(
    const char *id,
    const vinyl::RecordingMbid &recordingMbid)
{
    return mustQueue(
        vinyl::QueueOccurrence::create(qid(id), recordingMbid));
}

vinyl::PlaybackLoadInput input(const char *name)
{
    auto made = vinyl::PlaybackLoadInput::create(
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

    void add(const char *id, const char *resource = nullptr)
    {
        entries.append(Entry{
            qid(id),
            input(resource ? resource : id)});
    }

    std::optional<vinyl::PlaybackLoadInput> inputFor(
        const vinyl::QueueOccurrence &occurrence) override
    {
        requests.append(occurrence.occurrenceId());
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
};

class FakeEngine final : public vinyl::PlaybackEngineCommands
{
public:
    void load(const vinyl::PlaybackWorkToken &token,
              const vinyl::PlaybackLoadInput &loadInput) override
    {
        commands.append(EngineCommand{
            CommandKind::Load,
            token,
            loadInput.resource()});
    }

    void play(const vinyl::PlaybackWorkToken &token) override
    {
        commands.append(EngineCommand{CommandKind::Play, token, {}});
    }

    void pause(const vinyl::PlaybackWorkToken &token) override
    {
        commands.append(EngineCommand{CommandKind::Pause, token, {}});
    }

    void seek(const vinyl::PlaybackSeekRequest &request) override
    {
        commands.append(EngineCommand{
            CommandKind::Seek,
            request.workToken(),
            {}});
    }

    void stop(const vinyl::PlaybackWorkToken &token) override
    {
        commands.append(EngineCommand{CommandKind::Stop, token, {}});
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

    QString lastLoadResource() const
    {
        for (auto it = commands.crbegin(); it != commands.crend(); ++it) {
            if (it->kind == CommandKind::Load)
                return it->resource;
        }
        return {};
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
            bool withInput = true)
{
    require(
        fixture.queue.append(occurrence(id, recordingMbid)).ok,
        "fixture append should succeed");
    if (withInput)
        fixture.inputs.add(id);
}

vinyl::PlaybackWorkToken token(const Fixture &fixture)
{
    require(
        fixture.session.activeWorkToken().has_value(),
        "fixture should have active work token");
    return *fixture.session.activeWorkToken();
}

void load(Fixture &fixture)
{
    require(
        fixture.coordinator.engineLoaded(token(fixture)).ok,
        "current load callback should succeed");
}

void requireActive(const Fixture &fixture, const char *id)
{
    require(
        fixture.queue.activeOccurrenceId().has_value()
            && fixture.queue.activeOccurrenceId()->value()
                == QString::fromLatin1(id),
        "BA1 active occurrence should match expected ID");
}

void requireInvariant(const Fixture &fixture)
{
    if (!fixture.session.activeOccurrence().has_value())
        return;

    require(
        fixture.queue.activeOccurrenceId().has_value(),
        "BA2 binding requires BA1 active occurrence");
    require(
        fixture.session.activeOccurrence()->occurrenceId()
            == *fixture.queue.activeOccurrenceId(),
        "BA2 bound occurrence must equal BA1 active occurrence");
}

bool hasEndCause(const vinyl::QueueSessionStatus &status,
                 const vinyl::QueueOccurrenceId &id,
                 vinyl::OccurrenceEndCause cause)
{
    for (const auto &fact : status.transitionFacts) {
        if (const auto *ended =
                std::get_if<vinyl::OccurrenceEnded>(&fact)) {
            if (ended->occurrenceId == id && ended->cause == cause)
                return true;
        }
    }
    return false;
}

bool hasStarted(const vinyl::QueueSessionStatus &status,
                const vinyl::QueueOccurrenceId &id)
{
    for (const auto &fact : status.transitionFacts) {
        if (const auto *started =
                std::get_if<vinyl::OccurrenceStarted>(&fact)) {
            if (started->occurrenceId == id)
                return true;
        }
    }
    return false;
}

bool hasAwaiting(const vinyl::QueueSessionStatus &status,
                 const vinyl::QueueOccurrenceId &id,
                 bool playWhenReady)
{
    for (const auto &fact : status.transitionFacts) {
        if (const auto *awaiting =
                std::get_if<vinyl::AwaitingInput>(&fact)) {
            if (awaiting->occurrenceId == id
                && awaiting->playWhenReady == playWhenReady) {
                return true;
            }
        }
    }
    return false;
}

bool hasExhausted(const vinyl::QueueSessionStatus &status)
{
    for (const auto &fact : status.transitionFacts) {
        if (std::holds_alternative<vinyl::QueueExhausted>(fact))
            return true;
    }
    return false;
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

    {
        Fixture f;
        append(f, "r-a", songA);
        append(f, "r-b", songB);
        require(f.coordinator.start().ok, "restart fixture should start");
        load(f);
        const PlaybackWorkToken original = token(f);
        const quint64 generation = original.generation();

        requireInvariant(f);

        const QueueSessionStatus replaced =
            f.coordinator.restartActive(
                qid("r-a"),
                input("replacement-a"));
        require(replaced.ok && replaced.playbackChanged,
                "correct active restart should succeed");
        require(hasEndCause(
                    replaced,
                    qid("r-a"),
                    OccurrenceEndCause::Replaced),
                "same-occurrence replacement should expose replaced cause");
        require(hasStarted(replaced, qid("r-a")),
                "replacement should expose fresh start fact");
        require(
            token(f).occurrenceId() == qid("r-a")
                && token(f).generation() == generation + 1,
            "replacement should retain occurrence and mint fresh BA2 generation");
        require(
            f.engine.lastLoadResource() == QStringLiteral("fixture:///replacement-a"),
            "replacement should load supplied transport");
        require(!f.coordinator.pendingInput().has_value(),
                "successful replacement should clear pending input");
        requireInvariant(f);

        require(!f.coordinator.engineLoaded(original).ok,
                "old generation callback must be stale after replacement");
        requireInvariant(f);

        const int loads = f.engine.count(CommandKind::Load);
        const QueueSessionStatus mismatch =
            f.coordinator.restartActive(
                qid("r-b"),
                input("stale-r-b"));
        require(
            !mismatch.ok
                && mismatch.error.code
                    == QueueSessionErrorCode::ActiveOccurrenceMismatch,
            "mismatched replacement should fail closed");
        require(f.engine.count(CommandKind::Load) == loads,
                "mismatched replacement must not load transport");
        requireInvariant(f);
    }

    {
        Fixture f;
        append(f, "s-a", songA);
        append(f, "s-b", songB);
        require(f.coordinator.start().ok, "skip fixture should start");
        load(f);
        require(
            f.coordinator.awaitInput(qid("s-a"), true).ok,
            "delayed replacement should arm");
        requireInvariant(f);

        const QueueSessionStatus skipped = f.coordinator.skip();
        require(skipped.ok, "skip while replacement pending should succeed");
        requireActive(f, "s-b");
        require(hasEndCause(
                    skipped,
                    qid("s-a"),
                    OccurrenceEndCause::UserSkip),
                "skip should expose user_skip cause");
        require(!f.coordinator.pendingInput().has_value(),
                "skip should clear old pending replacement");
        requireInvariant(f);

        const int loads = f.engine.count(CommandKind::Load);
        const QueueSessionStatus late =
            f.coordinator.restartActive(
                qid("s-a"),
                input("late-s-a"));
        require(
            !late.ok
                && late.error.code
                    == QueueSessionErrorCode::ActiveOccurrenceMismatch,
            "late replacement after skip should fail closed");
        require(f.engine.count(CommandKind::Load) == loads,
                "late skipped replacement must not issue load");
        requireInvariant(f);
    }

    {
        Fixture f;
        append(f, "m-a", songA);
        append(f, "m-b", songB, false);
        require(f.coordinator.start().ok, "remove fixture should start");
        load(f);
        require(
            f.coordinator.awaitInput(qid("m-a"), true).ok,
            "active removal fixture should arm pending");
        requireInvariant(f);

        const QueueSessionStatus removed =
            f.coordinator.remove(qid("m-a"));
        require(removed.ok, "active removal while awaiting should succeed");
        requireActive(f, "m-b");
        require(hasEndCause(
                    removed,
                    qid("m-a"),
                    OccurrenceEndCause::Removed),
                "active removal should expose removed cause");
        require(hasAwaiting(removed, qid("m-b"), true),
                "successor without source should expose awaiting input");
        require(
            f.coordinator.pendingInput().has_value()
                && f.coordinator.pendingInput()->occurrenceId == qid("m-b")
                && f.coordinator.pendingInput()->playWhenReady,
            "active removal should carry play intent to exact successor");
        require(f.session.state() == PlaybackState::Idle,
                "awaiting successor should not invent BA2 state");
        requireInvariant(f);
    }

    {
        Fixture f;
        append(f, "t-a", songA);
        require(f.coordinator.start().ok, "stop fixture should start");
        load(f);
        require(
            f.coordinator.awaitInput(qid("t-a"), true).ok,
            "stop fixture should arm pending");
        requireInvariant(f);

        const QueueSessionStatus stopped = f.coordinator.stop();
        require(stopped.ok, "stop while awaiting should succeed");
        require(hasEndCause(
                    stopped,
                    qid("t-a"),
                    OccurrenceEndCause::Stopped),
                "stop should expose stopped cause");
        require(!f.coordinator.pendingInput().has_value(),
                "stop should clear pending input");
        require(f.session.state() == PlaybackState::Idle,
                "stop should clear BA2 binding");
        requireActive(f, "t-a");
        requireInvariant(f);

        const QueueSessionStatus late =
            f.coordinator.restartActive(
                qid("t-a"),
                input("late-after-stop"));
        require(
            !late.ok
                && late.error.code
                    == QueueSessionErrorCode::ReplacementNotArmed,
            "late result after stop must not resurrect playback");
        requireInvariant(f);
    }

    {
        Fixture f;
        append(f, "p-a", songA, false);
        const QueueSessionStatus waiting = f.coordinator.start(false);
        require(waiting.ok, "missing source should become pending");
        require(hasAwaiting(waiting, qid("p-a"), false),
                "pending fact should preserve false playWhenReady");
        require(
            f.coordinator.pendingInput().has_value()
                && !f.coordinator.pendingInput()->playWhenReady,
            "pending state should preserve paused intent");
        requireInvariant(f);

        f.inputs.add("p-a", "provider-ready");
        const QueueSessionStatus resumed =
            f.coordinator.restartActive(
                qid("p-a"),
                input("provider-ready"));
        require(resumed.ok, "pending active occurrence should accept transport");
        require(hasStarted(resumed, qid("p-a")),
                "resolved pending occurrence should expose started fact");
        load(f);
        require(f.session.state() == PlaybackState::Paused,
                "pending false playWhenReady must survive replacement");
        requireInvariant(f);
    }

    {
        Fixture f;
        append(f, "e-a", songA);
        append(f, "e-b", songB);
        require(f.coordinator.repeatMode() == CoordinatorRepeatMode::Off,
                "repeat should default Off");
        require(f.coordinator.start().ok, "EOF fixture should start");
        load(f);
        const PlaybackWorkToken a = token(f);

        const QueueSessionStatus firstEnd = f.coordinator.engineEnded(a);
        require(firstEnd.ok, "first EOF should advance");
        require(hasEndCause(firstEnd, qid("e-a"), OccurrenceEndCause::Eof),
                "natural EOF should expose eof cause");
        requireActive(f, "e-b");
        requireInvariant(f);

        const int afterFirstIndex = f.queue.activeIndex();
        require(!f.coordinator.engineEnded(a).ok,
                "double EOF from old token should be stale");
        require(f.queue.activeIndex() == afterFirstIndex,
                "double EOF must not advance twice");
        requireInvariant(f);

        load(f);
        const PlaybackWorkToken b = token(f);
        const int loadsBeforeTail = f.engine.count(CommandKind::Load);
        const QueueSessionStatus tailEnd = f.coordinator.engineEnded(b);
        require(tailEnd.ok, "tail EOF should succeed");
        require(!f.queue.activeOccurrenceId().has_value(),
                "repeat Off must not wrap");
        require(f.session.state() == PlaybackState::Idle,
                "queue exhaustion should clear BA2 binding");
        require(hasExhausted(tailEnd),
                "tail EOF should expose QueueExhausted");
        require(f.engine.count(CommandKind::Load) == loadsBeforeTail,
                "repeat Off must not issue wrap load");
        requireInvariant(f);

        require(!f.coordinator.engineEnded(b).ok,
                "duplicate tail EOF should be stale after exhaustion");
        requireInvariant(f);
    }

    {
        Fixture f;
        append(f, "q-a", songA);
        append(f, "q-b", songB);
        f.coordinator.setRepeatMode(CoordinatorRepeatMode::Queue);
        require(
            coordinatorRepeatModeName(f.coordinator.repeatMode())
                == QStringLiteral("queue"),
            "repeat Queue should expose durable-compatible name");
        require(f.coordinator.start().ok, "repeat fixture should start");
        load(f);
        require(f.coordinator.engineEnded(token(f)).ok,
                "repeat fixture first EOF should advance");
        requireActive(f, "q-b");
        load(f);
        const QueueSessionStatus wrapped =
            f.coordinator.engineEnded(token(f));
        require(wrapped.ok, "repeat Queue tail EOF should wrap");
        requireActive(f, "q-a");
        require(!hasExhausted(wrapped),
                "repeat Queue wrap must not report exhaustion");
        requireInvariant(f);
    }

    {
        Fixture f;
        append(f, "one", songA);
        f.coordinator.setRepeatMode(CoordinatorRepeatMode::Queue);
        require(f.coordinator.start().ok, "single repeat should start");
        load(f);
        const PlaybackWorkToken first = token(f);
        const QueueSessionStatus wrapped =
            f.coordinator.engineEnded(first);
        require(wrapped.ok, "single-item repeat should wrap");
        requireActive(f, "one");
        require(
            token(f).occurrenceId() == first.occurrenceId()
                && token(f).generation() == first.generation() + 1,
            "single-item repeat must mint fresh BA2 generation");
        require(!f.coordinator.engineLoaded(first).ok,
                "pre-wrap token must be stale after same-ID repeat");
        requireInvariant(f);
    }

    {
        Fixture f;
        append(f, "f-a", songA);
        require(f.coordinator.start().ok, "failure fixture should start");
        load(f);
        const PlaybackWorkToken current = token(f);
        const QueueSessionStatus failed =
            f.coordinator.engineFailed(
                current,
                PlaybackFailure{
                    QStringLiteral("decoder"),
                    QStringLiteral("fixture failure")});
        require(failed.ok, "current playback failure should be accepted");
        require(hasEndCause(
                    failed,
                    qid("f-a"),
                    OccurrenceEndCause::Failed),
                "playback failure should expose failed cause");
        require(f.session.state() == PlaybackState::Failed,
                "failure fact should not invent fallback policy");
        requireInvariant(f);
    }

    require(
        occurrenceEndCauseName(OccurrenceEndCause::UserSkip)
            == QStringLiteral("user_skip"),
        "transition cause names should be stable");
    require(
        coordinatorRepeatModeName(CoordinatorRepeatMode::Off)
            == QStringLiteral("off"),
        "repeat Off should expose durable-compatible name");

    qInfo().noquote()
        << "VINYL_W4B_ADVERSARIAL_OK checks=" << g_checks;
    return 0;
}
