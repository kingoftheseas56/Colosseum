#include "PlaybackEngineAdapter.h"
#include "ColosseumMpvBridge.h"
#include <QElapsedTimer>
#include "MpvSidecarReference.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QString>

#include <mpv/client.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;

    std::fprintf(stderr, "FAIL: %s\n", message);
    std::fflush(stderr);
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
T mustLoad(const vinyl::PlaybackLoadInputResult<T> &result,
           const char *message)
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
        "fixture recording MBID should parse");
}

vinyl::QueueOccurrence occurrence(const char *id,
                                  const vinyl::RecordingMbid &recordingMbid)
{
    return mustQueue(
        vinyl::QueueOccurrence::create(
            occurrenceId(id),
            recordingMbid),
        "fixture occurrence should construct");
}

struct SeekCompletion final {
    quint64 sequence = 0;
    vinyl::PlaybackMutationStatus status;
    double observedPosition = 0.0;
};

class SerializedMpvSeekEngine final : public vinyl::PlaybackEngineCommands
{
public:
    SerializedMpvSeekEngine()
    {
        m_clock.start();
        m_owner = mpv_create();
        require(m_owner != nullptr,
                "owner mpv handle should be created");

        require(mpv_set_option_string(m_owner, "vo", "null") >= 0,
                "vo=null should configure");
        require(mpv_set_option_string(m_owner, "ao", "null") >= 0,
                "ao=null should configure");
        require(mpv_set_option_string(m_owner, "idle", "yes") >= 0,
                "idle=yes should configure");
        require(mpv_initialize(m_owner) >= 0,
                "owner mpv handle should initialize");

        int paused = 1;
        require(mpv_set_property(
                    m_owner,
                    "pause",
                    MPV_FORMAT_FLAG,
                    &paused) >= 0,
                "initial pause should configure");

        m_sidecar =
            mpv_create_weak_client(m_owner, "vinyl_ba2db3");
        require(m_sidecar != nullptr,
                "weak sidecar should attach to same mpv core");

        for (const mpv_event_id event : {
                 MPV_EVENT_START_FILE,
                 MPV_EVENT_FILE_LOADED,
                 MPV_EVENT_END_FILE,
                 MPV_EVENT_SEEK,
                 MPV_EVENT_PLAYBACK_RESTART}) {
            require(mpv_request_event(m_sidecar, event, 1) >= 0,
                    "weak client should request raw event");
        }
    }

    ~SerializedMpvSeekEngine() override
    {
        if (m_sidecar)
            mpv_destroy(m_sidecar);
        if (m_owner)
            mpv_terminate_destroy(m_owner);
    }

    void bind(vinyl::PlaybackEngineAdapter *adapter)
    {
        m_adapter = adapter;
    }

    void load(const vinyl::PlaybackWorkToken &token,
              const vinyl::PlaybackLoadInput &input) override
    {
        m_currentToken = token;
        m_seekState.bindWork(token);

        const QByteArray resource =
            QFileInfo(input.resource()).absoluteFilePath().toUtf8();
        const char *args[] = {
            "loadfile",
            resource.constData(),
            "replace",
            nullptr,
        };
        require(mpv_command(m_owner, args) >= 0,
                "physical load command should succeed");
        const auto entryId = vinyl::currentMpvPlaylistEntryId(m_sidecar);
        require(entryId.has_value() && m_entryLedger.bind(*entryId, token),
                "physical load should register exact EOF work attribution");
    }

    void play(const vinyl::PlaybackWorkToken &token) override
    {
        if (!owns(token))
            return;
        setPause(false);
    }

    void pause(const vinyl::PlaybackWorkToken &token) override
    {
        if (!owns(token))
            return;
        setPause(true);
    }

    void seek(const vinyl::PlaybackSeekRequest &request) override
    {
        if (!m_currentToken.has_value() || *m_currentToken != request.workToken()) {
            if (m_adapter) m_adapter->engineSeekFailed(request, QStringLiteral("seek_work_unavailable"));
            return;
        }

        const qint64 targetMs = static_cast<qint64>(
            std::llround(request.targetSeconds() * 1000.0));
        const auto submitted = m_seekState.submit(
            vinyl::PlaybackSeekCommand(request, targetMs), m_clock.elapsed());
        if (submitted.disposition == vinyl::MpvSeekSubmitDisposition::RejectedStale) {
            if (m_adapter) m_adapter->engineSeekFailed(request, QStringLiteral("seek_quarantined"));
            return;
        }
        if (submitted.dispatch.has_value())
            dispatchPhysicalSeek(*submitted.dispatch);
    }

    void stop(const vinyl::PlaybackWorkToken &token) override
    {
        if (!owns(token))
            return;

        m_seekState.clear();

        const char *args[] = {"stop", nullptr};
        require(mpv_command(m_owner, args) >= 0,
                "physical stop should succeed");
    }

    bool pumpOne(double timeoutSeconds = 0.1)
    {
        if (auto expired = m_seekState.expire(m_clock.elapsed())) {
            if (m_adapter) {
                m_adapter->engineSeekFailed(expired->request(), QStringLiteral("seek_timeout"));
                m_adapter->engineFailed(expired->workToken(), {QStringLiteral("seek_timeout"), {}});
            }
            stop(expired->workToken());
        }
        mpv_event *event =
            mpv_wait_event(m_sidecar, timeoutSeconds);
        if (!event || event->event_id == MPV_EVENT_NONE)
            return false;

        switch (event->event_id) {
        case MPV_EVENT_FILE_LOADED:
            if (m_adapter && m_currentToken.has_value())
                m_adapter->engineLoaded(*m_currentToken);
            break;

        case MPV_EVENT_SEEK:
            if (m_currentToken.has_value())
                m_seekState.observeSeekStart(*m_currentToken);
            break;

        case MPV_EVENT_PLAYBACK_RESTART:
            finishActiveSeekIfReady();
            break;

        case MPV_EVENT_END_FILE: {
            const auto *end = static_cast<const mpv_event_end_file *>(event->data);
            if (!end) break;
            const auto token = m_entryLedger.tokenFor(end->playlist_entry_id);
            if (!token.has_value()) break;
            m_entryLedger.forget(end->playlist_entry_id);
            if (auto cancelled = m_seekState.cancelWork(*token))
                if (m_adapter) m_adapter->engineSeekCancelled(cancelled->request());
            if (m_adapter) {
                if (end->error >= 0 && end->reason == MPV_END_FILE_REASON_EOF)
                    m_adapter->engineEnded(*token);
                else
                    m_adapter->engineFailed(*token, {QStringLiteral("mpv_end_file"), {}});
            }
            break;
        }
        default:
            break;
        }

        return true;
    }

    bool pumpUntil(const std::function<bool()> &predicate,
                   double timeoutSeconds)
    {
        const int steps =
            static_cast<int>(std::ceil(timeoutSeconds / 0.05));
        for (int i = 0; i < steps; ++i) {
            if (predicate())
                return true;
            pumpOne(0.05);
        }
        return predicate();
    }

    std::optional<double> duration() const
    {
        double value = 0.0;
        if (mpv_get_property(
                m_sidecar,
                "duration",
                MPV_FORMAT_DOUBLE,
                &value) < 0) {
            return std::nullopt;
        }
        if (!std::isfinite(value) || value <= 0.0)
            return std::nullopt;
        return value;
    }

    std::optional<double> position() const
    {
        double value = 0.0;
        if (mpv_get_property(
                m_sidecar,
                "time-pos",
                MPV_FORMAT_DOUBLE,
                &value) < 0) {
            return std::nullopt;
        }
        if (!std::isfinite(value) || value < 0.0)
            return std::nullopt;
        return value;
    }

    const std::vector<quint64> &physicalSeekSequences() const
    {
        return m_physicalSeekSequences;
    }

    const std::vector<SeekCompletion> &completions() const
    {
        return m_completions;
    }

    bool hasPhysicalSeekInFlight() const noexcept
    {
        return m_seekState.hasPhysicalSeekInFlight();
    }

    bool hasQueuedSeek() const noexcept
    {
        return m_seekState.hasQueuedSeek();
    }

private:
    bool owns(const vinyl::PlaybackWorkToken &token) const
    {
        return m_currentToken.has_value()
            && *m_currentToken == token;
    }

    void setPause(bool paused)
    {
        int flag = paused ? 1 : 0;
        require(mpv_set_property(
                    m_owner,
                    "pause",
                    MPV_FORMAT_FLAG,
                    &flag) >= 0,
                "pause property write should succeed");
    }

    void dispatchPhysicalSeek(
        const vinyl::PlaybackSeekCommand &command)
    {
        m_physicalSeekSequences.push_back(command.sequence());

        const double targetSeconds =
            static_cast<double>(command.targetMs()) / 1000.0;
        const QByteArray target =
            QByteArray::number(targetSeconds, 'f', 3);
        const char *args[] = {
            "seek",
            target.constData(),
            "absolute",
            "exact",
            nullptr,
        };
        if (mpv_command(m_owner, args) < 0) {
            if (auto failed = m_seekState.failActive(command)) {
                if (m_adapter) {
                    m_adapter->engineSeekFailed(failed->request(), QStringLiteral("seek_command_failed"));
                    m_adapter->engineFailed(command.workToken(), {QStringLiteral("seek_command_failed"), {}});
                }
                stop(command.workToken());
            }
        }
    }

    void finishActiveSeekIfReady()
    {
        if (!m_adapter || !m_currentToken.has_value())
            return;

        const std::optional<double> observed = position();
        if (!observed.has_value()) return;
        auto advance = m_seekState.observePlaybackRestart(*m_currentToken,
            vinyl::millisecondsFromMpvSeconds(*observed), m_clock.elapsed());
        if (!advance.has_value()) return;

        SeekCompletion result;
        result.sequence = advance->completed.sequence();
        result.observedPosition = *observed;
        result.status = m_adapter->engineSeekApplied(
            advance->completed.request(),
            *observed);
        m_completions.push_back(result);

        if (advance->nextPhysical.has_value())
            dispatchPhysicalSeek(*advance->nextPhysical);
    }

    QElapsedTimer m_clock;
    mpv_handle *m_owner = nullptr;
    mpv_handle *m_sidecar = nullptr;
    vinyl::PlaybackEngineAdapter *m_adapter = nullptr;
    vinyl::MpvPlaylistEntryWorkLedger m_entryLedger;

    std::optional<vinyl::PlaybackWorkToken> m_currentToken;
    vinyl::SerializedMpvSeekState m_seekState;

    std::vector<quint64> m_physicalSeekSequences;
    std::vector<SeekCompletion> m_completions;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    require(argc >= 2,
            "probe requires an existing seekable media fixture");

    const QString media =
        QString::fromLocal8Bit(argv[1]);
    require(QFileInfo(media).isFile(),
            "seekable media fixture should exist");

    const RecordingMbid song =
        recording("11111111-1111-4111-8111-111111111111");
    const PlaybackLoadInput input = mustLoad(
        PlaybackLoadInput::create(media),
        "seek probe load input should construct");

    PlaybackSession session;
    SerializedMpvSeekEngine engine;
    PlaybackEngineAdapter adapter(session, engine);
    engine.bind(&adapter);

    const PlaybackWorkToken token = mustSession(
        adapter.beginOccurrence(
            occurrence("seek-runtime", song),
            input,
            false),
        "seek runtime occurrence should begin");

    require(engine.pumpUntil(
                [&]() {
                    return session.state()
                        == PlaybackState::Paused;
                },
                5.0),
            "FILE_LOADED should reach BA2-A and settle paused");

    std::optional<double> duration;
    require(engine.pumpUntil(
                [&]() {
                    duration = engine.duration();
                    return duration.has_value();
                },
                5.0),
            "seekable fixture should expose duration");

    const double total = *duration;
    const double target1 =
        qBound(0.0, total * 0.20, qMax(0.0, total - 0.05));
    const double target2 =
        qBound(0.0, total * 0.45, qMax(0.0, total - 0.04));
    const double target3 =
        qBound(0.0, total * 0.75, qMax(0.0, total - 0.03));

    const PlaybackSeekRequest seek1 = mustSession(
        adapter.seek(target1),
        "logical seek1 should be accepted");
    const PlaybackSeekRequest seek2 = mustSession(
        adapter.seek(target2),
        "logical seek2 should be accepted");
    const PlaybackSeekRequest seek3 = mustSession(
        adapter.seek(target3),
        "logical seek3 should be accepted");

    require(seek1.workToken() == token
                && seek2.workToken() == token
                && seek3.workToken() == token,
            "all logical seeks should retain exact work token");
    require(seek2.sequence() == seek1.sequence() + 1
                && seek3.sequence() == seek2.sequence() + 1,
            "logical seek requests should retain strict sequence identity");

    require(engine.physicalSeekSequences().size() == 1
                && engine.physicalSeekSequences().at(0)
                    == seek1.sequence(),
            "only seek1 should be physically dispatched before raw completion");
    require(engine.hasPhysicalSeekInFlight(),
            "seek1 should remain the one physical in-flight seek");
    require(engine.hasQueuedSeek(),
            "newest later logical seek should be retained as queued work");
    require(session.pendingSeek().has_value()
                && session.pendingSeek()->sequence()
                    == seek3.sequence(),
            "BA2-A should keep seek3 as logical pending truth");

    require(engine.pumpUntil(
                [&]() {
                    return engine.completions().size() >= 2
                        && !session.pendingSeek().has_value();
                },
                8.0),
            "raw SEEK/PLAYBACK_RESTART should complete serialized seeks");

    require(engine.physicalSeekSequences().size() == 2,
            "exactly two physical seeks should have run");
    require(engine.physicalSeekSequences().at(0) == seek1.sequence(),
            "first physical seek should be seek1");
    require(engine.physicalSeekSequences().at(1) == seek3.sequence(),
            "newest queued seek3 should run after seek1");
    for (const quint64 sequence : engine.physicalSeekSequences()) {
        require(sequence != seek2.sequence(),
                "superseded queued seek2 must never be physically dispatched");
    }

    require(engine.completions().size() == 2,
            "exactly the two physical seeks should produce completions");

    const SeekCompletion &completion1 =
        engine.completions().at(0);
    const SeekCompletion &completion3 =
        engine.completions().at(1);

    require(completion1.sequence == seek1.sequence(),
            "first PLAYBACK_RESTART must complete seek1 identity");
    require(!completion1.status.ok
                && completion1.status.error.code
                    == PlaybackSessionErrorCode::SupersededSeek,
            "seek1 completion should be rejected because logical seek3 superseded it");

    require(completion3.sequence == seek3.sequence(),
            "second PLAYBACK_RESTART must complete seek3 identity");
    require(completion3.status.ok,
            "latest physically completed seek3 should be accepted by BA2-A");
    require(!session.pendingSeek().has_value(),
            "accepted seek3 completion should clear logical pending seek");

    require(session.positionSeconds().has_value(),
            "accepted seek3 completion should publish observed position");

    const double acceptedPosition =
        *session.positionSeconds();
    const double finalError =
        std::abs(acceptedPosition - target3);
    require(finalError <= qMax(0.75, total * 0.10),
            "final observed position should correspond to latest seek3 target");

    const std::size_t completionsBeforeStop =
        engine.completions().size();

    mustSession(
        adapter.seek(target1),
        "stop fixture seek1 should begin");
    mustSession(
        adapter.seek(target2),
        "stop fixture seek2 should queue");
    require(engine.hasPhysicalSeekInFlight()
                && engine.hasQueuedSeek(),
            "stop fixture should own active and queued seek work");

    const PlaybackMutationStatus stopped = adapter.stop();
    require(stopped.ok && stopped.changed,
            "stop should invalidate BA2-A before physical stop");
    require(session.state() == PlaybackState::Idle,
            "stop should leave BA2-A idle");
    require(!engine.hasPhysicalSeekInFlight()
                && !engine.hasQueuedSeek(),
            "physical and queued seek ownership must clear before stop returns");

    for (int i = 0; i < 20; ++i)
        engine.pumpOne(0.02);

    require(engine.completions().size() == completionsBeforeStop,
            "late seek/restart events after stop must not manufacture completion");
    require(session.state() == PlaybackState::Idle,
            "late mpv events after stop must not revive lifecycle");

    qInfo().noquote()
        << "VINYL_BA2DB3_SEEK_OK checks=" << g_checks
        << "duration=" << total
        << "target3=" << target3
        << "observed=" << acceptedPosition;

    return 0;
}
