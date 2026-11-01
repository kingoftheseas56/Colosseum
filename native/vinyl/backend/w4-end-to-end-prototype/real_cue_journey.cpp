#include "ColosseumMpvBridge.h"
#include "CueWholeAlbum.h"
#include "MpvSidecarReference.h"
#include "QueueSessionCoordinator.h"
#include "ResolvedPlaybackSource.h"
#include "W4TestFixtures.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <QFileInfo>
#include <QString>

#include <mpv/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <variant>

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
T mustDomain(const vinyl::DomainResult<T> &r, const char *m)
{
    require(r.ok(), m);
    return *r.value;
}

template <typename T>
T mustQueue(const vinyl::QueueResult<T> &r, const char *m)
{
    require(r.ok(), m);
    return *r.value;
}

template <typename T>
T mustCandidate(const vinyl::SourceCandidateResult<T> &r, const char *m)
{
    require(r.ok(), m);
    return *r.value;
}

template <typename T>
T mustCue(const vinyl::CueWholeAlbumResult<T> &r, const char *m)
{
    require(r.ok(), m);
    return *r.value;
}

template <typename T>
T mustMpv(const vinyl::MpvPlaybackAdapterResult<T> &r, const char *m)
{
    require(r.ok(), m);
    return *r.value;
}

vinyl::RecordingMbid recording(const char *v)
{
    return mustDomain(vinyl::RecordingMbid::parse(QString::fromLatin1(v)),
                      "recording should parse");
}

vinyl::QueueOccurrenceId occurrenceId(const char *v)
{
    return mustQueue(vinyl::QueueOccurrenceId::parse(QString::fromLatin1(v)),
                     "occurrence id should parse");
}

vinyl::QueueOccurrence occurrence(
    const char *id, const vinyl::RecordingMbid &recordingMbid)
{
    return mustQueue(
        vinyl::QueueOccurrence::create(
            occurrenceId(id), recordingMbid),
        "occurrence should construct");
}

vinyl::SourceCandidateKey sourceKey()
{
    return vinyl::SourceCandidateKey(
        mustCandidate(
            vinyl::SourceProviderId::parse(
                QStringLiteral("w4h.local.cue")),
            "provider id should parse"),
        mustCandidate(
            vinyl::SourceCandidateId::parse(
                QStringLiteral("shared-physical-file")),
            "candidate id should parse"));
}

qint64 fixtureDurationMs(const QString &fixture)
{
    mpv_handle *client = mpv_create();
    require(client != nullptr, "duration mpv_create should succeed");
    require(mpv_set_option_string(client, "vo", "null") >= 0,
            "duration vo=null should configure");
    require(mpv_set_option_string(client, "ao", "null") >= 0,
            "duration ao=null should configure");
    require(mpv_initialize(client) >= 0,
            "duration mpv should initialize");

    const QByteArray path = QFileInfo(fixture).absoluteFilePath().toUtf8();
    const char *args[] = {"loadfile", path.constData(), "replace", nullptr};
    require(mpv_command(client, args) >= 0,
            "duration loadfile should succeed");

    bool loaded = false;
    double remaining = 5.0;
    while (remaining > 0.0 && !loaded) {
        const double slice = std::min(0.1, remaining);
        remaining -= slice;
        mpv_event *event = mpv_wait_event(client, slice);
        if (event && event->event_id == MPV_EVENT_FILE_LOADED)
            loaded = true;
    }
    require(loaded, "fixture should load for duration query");

    double duration = 0.0;
    require(mpv_get_property(
                client, "duration", MPV_FORMAT_DOUBLE, &duration) >= 0
                && std::isfinite(duration) && duration > 1.2,
            "fixture should be longer than 1.2 seconds");
    mpv_terminate_destroy(client);
    const auto ms = vinyl::millisecondsFromMpvSeconds(duration);
    require(ms.has_value(), "duration should convert to milliseconds");
    return *ms;
}

QString cueClock(qint64 frames)
{
    const qint64 minutes = frames / (75 * 60);
    const qint64 seconds = (frames / 75) % 60;
    const qint64 ff = frames % 75;
    return QStringLiteral("%1:%2:%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

class CueInputProvider final : public vinyl::PlaybackLoadInputProvider
{
public:
    CueInputProvider(vinyl::QueueOccurrenceId firstId,
                     vinyl::PlaybackLoadInput first,
                     vinyl::QueueOccurrenceId secondId,
                     vinyl::PlaybackLoadInput second)
        : m_firstId(std::move(firstId))
        , m_first(std::move(first))
        , m_secondId(std::move(secondId))
        , m_second(std::move(second))
    {
    }

    std::optional<vinyl::PlaybackLoadInput> inputFor(
        const vinyl::QueueOccurrence &occurrence) override
    {
        if (occurrence.occurrenceId() == m_firstId)
            return m_first;
        if (occurrence.occurrenceId() == m_secondId)
            return m_second;
        return std::nullopt;
    }

private:
    vinyl::QueueOccurrenceId m_firstId;
    vinyl::PlaybackLoadInput m_first;
    vinyl::QueueOccurrenceId m_secondId;
    vinyl::PlaybackLoadInput m_second;
};

class RealMpvEngine final : public vinyl::PlaybackEngineCommands
{
public:
    struct LoadObservation final {
        vinyl::PlaybackWorkToken token;
        qint64 playlistEntryId = -1;
        qint64 physicalPositionMs = -1;
        qint64 trackPositionMs = -1;
        qint64 logicalDurationMs = -1;
    };

    RealMpvEngine()
    {
        m_clock.start();
        m_owner = mpv_create();
        require(m_owner != nullptr, "real mpv owner should create");
        require(mpv_set_option_string(m_owner, "vo", "null") >= 0,
                "real mpv vo=null should configure");
        require(mpv_set_option_string(m_owner, "ao", "null") >= 0,
                "real mpv ao=null should configure");
        require(mpv_set_option_string(m_owner, "idle", "yes") >= 0,
                "real mpv idle=yes should configure");
        require(mpv_initialize(m_owner) >= 0,
                "real mpv owner should initialize");
        m_sidecar = mpv_create_weak_client(m_owner, "vinyl-w4h-real");
        require(m_sidecar != nullptr,
                "real mpv weak sidecar should attach");
        for (const mpv_event_id event : {
                 MPV_EVENT_START_FILE,
                 MPV_EVENT_FILE_LOADED,
                 MPV_EVENT_END_FILE,
                 MPV_EVENT_SEEK,
                 MPV_EVENT_PLAYBACK_RESTART}) {
            require(mpv_request_event(m_sidecar, event, 1) >= 0,
                    "sidecar event request should succeed");
        }
    }

    ~RealMpvEngine() override
    {
        if (m_sidecar)
            mpv_destroy(m_sidecar);
        if (m_owner)
            mpv_terminate_destroy(m_owner);
    }

    void bind(vinyl::PlaybackEngineAdapter *adapter,
              vinyl::QueueSessionCoordinator *coordinator)
    {
        m_adapter = adapter;
        m_coordinator = coordinator;
    }

    void load(const vinyl::PlaybackWorkToken &token,
              const vinyl::PlaybackLoadInput &input) override
    {
        m_currentToken = token;
        m_transform = vinyl::PlaybackTimeTransform(input.playbackWindow());
        m_currentDurationMs =
            input.playbackWindow().has_value()
            ? input.playbackWindow()->durationMs()
            : std::nullopt;
        m_seekState.bindWork(token);

        const QByteArray resource =
            QFileInfo(input.resource()).absoluteFilePath().toUtf8();
        const QString optionText =
            vinyl::mpvPerFilePlaybackWindowOptions(input.playbackWindow());
        const QByteArray options = optionText.toUtf8();

        int rc = 0;
        if (optionText.isEmpty()) {
            const char *args[] = {
                "loadfile", resource.constData(), "replace", nullptr};
            rc = mpv_command(m_owner, args);
        } else {
            const char *args[] = {
                "loadfile",
                resource.constData(),
                "replace",
                "-1",
                options.constData(),
                nullptr,
            };
            rc = mpv_command(m_owner, args);
        }
        require(rc >= 0, "real mpv loadfile should succeed");

        const auto entryId = vinyl::currentMpvPlaylistEntryId(m_sidecar);
        require(entryId.has_value(),
                "real mpv load should expose concrete playlist-entry-id");
        require(m_entryLedger.bind(*entryId, token),
                "playlist-entry-id should bind exact PlaybackWorkToken");
    }

    void play(const vinyl::PlaybackWorkToken &token) override
    {
        if (!owns(token))
            return;
        setPaused(false);
    }

    void pause(const vinyl::PlaybackWorkToken &token) override
    {
        if (!owns(token))
            return;
        setPaused(true);
    }

    void seek(const vinyl::PlaybackSeekCommand &command) override
    {
        if (!owns(command.workToken()) || !m_transform.has_value()) {
            if (m_adapter) m_adapter->engineSeekFailed(command.request(), QStringLiteral("seek_transform_unavailable"));
            return;
        }

        const auto submitted = m_seekState.submit(command, m_clock.elapsed());
        if (submitted.disposition == vinyl::MpvSeekSubmitDisposition::RejectedStale) {
            if (m_adapter) m_adapter->engineSeekFailed(command.request(), QStringLiteral("seek_quarantined"));
            return;
        }
        if (submitted.dispatch.has_value())
            dispatchPhysicalSeek(*submitted.dispatch);
    }

    void seek(const vinyl::PlaybackSeekRequest &request) override
    {
        const auto targetMs =
            vinyl::millisecondsFromMpvSeconds(request.targetSeconds());
        require(targetMs.has_value(),
                "compatibility seek should convert to integer milliseconds");
        seek(vinyl::PlaybackSeekCommand(request, *targetMs));
    }

    void stop(const vinyl::PlaybackWorkToken &token) override
    {
        if (!owns(token))
            return;
        m_seekState.clear();
        const char *args[] = {"stop", nullptr};
        require(mpv_command(m_owner, args) >= 0,
                "real mpv stop should succeed");
    }

    bool pumpOne(double timeoutSeconds = 0.05)
    {
        if (auto expired = m_seekState.expire(m_clock.elapsed())) {
            if (m_adapter) m_adapter->engineSeekFailed(expired->request(), QStringLiteral("seek_timeout"));
            if (m_coordinator) m_coordinator->engineFailed(expired->workToken(), {QStringLiteral("seek_timeout"), {}});
            stop(expired->workToken());
        }
        sampleCurrentPosition();

        mpv_event *event = mpv_wait_event(m_sidecar, timeoutSeconds);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            sampleCurrentPosition();
            return false;
        }

        if (qEnvironmentVariableIsSet("VINYL_M0R_SEEK_TRACE") &&
            (event->event_id == MPV_EVENT_SEEK || event->event_id == MPV_EVENT_PLAYBACK_RESTART))
            qInfo() << "M0R raw event" << mpv_event_name(event->event_id)
                    << "physicalMs" << physicalPositionMs().value_or(-1)
                    << "inFlight" << m_seekState.hasPhysicalSeekInFlight();
        switch (event->event_id) {
        case MPV_EVENT_FILE_LOADED:
            handleLoaded();
            break;

        case MPV_EVENT_SEEK:
            if (m_currentToken.has_value())
                m_seekState.observeSeekStart(*m_currentToken);
            break;

        case MPV_EVENT_PLAYBACK_RESTART:
            finishSeekIfReady();
            break;

        case MPV_EVENT_END_FILE:
            handleEnd(event);
            break;

        default:
            break;
        }

        sampleCurrentPosition();
        return true;
    }

    bool pumpUntil(const std::function<bool()> &predicate,
                   double timeoutSeconds)
    {
        const int steps =
            std::max(1, static_cast<int>(std::ceil(timeoutSeconds / 0.03)));
        for (int i = 0; i < steps; ++i) {
            if (predicate())
                return true;
            pumpOne(0.03);
        }
        return predicate();
    }

    const QVector<LoadObservation> &loads() const noexcept
    {
        return m_loads;
    }

    const QVector<vinyl::QueueSessionStatus> &endStatuses() const noexcept
    {
        return m_endStatuses;
    }

    std::optional<qint64> lastSeekPhysicalMs() const noexcept
    {
        return m_lastSeekPhysicalMs;
    }

    std::optional<qint64> lastSeekTrackMs() const noexcept
    {
        return m_lastSeekTrackMs;
    }

    std::optional<qint64> lastTrackFor(
        const vinyl::QueueOccurrenceId &id) const
    {
        for (auto it = m_lastPositions.crbegin();
             it != m_lastPositions.crend(); ++it) {
            if (it->first == id)
                return it->second;
        }
        return std::nullopt;
    }

private:
    bool owns(const vinyl::PlaybackWorkToken &token) const noexcept
    {
        return m_currentToken.has_value() && *m_currentToken == token;
    }

    void setPaused(bool paused)
    {
        int flag = paused ? 1 : 0;
        require(mpv_set_property(
                    m_owner, "pause", MPV_FORMAT_FLAG, &flag) >= 0,
                "real mpv pause property should update");
    }

    std::optional<qint64> physicalPositionMs() const
    {
        double seconds = 0.0;
        if (mpv_get_property(
                m_sidecar, "time-pos", MPV_FORMAT_DOUBLE, &seconds) < 0) {
            return std::nullopt;
        }
        return vinyl::millisecondsFromMpvSeconds(seconds);
    }

    void sampleCurrentPosition()
    {
        if (!m_currentToken.has_value() || !m_transform.has_value())
            return;
        const auto physical = physicalPositionMs();
        if (!physical.has_value())
            return;
        const auto track =
            m_transform->trackPositionForPhysical(*physical);
        if (!track.has_value())
            return;

        m_lastPositions.append(
            {m_currentToken->occurrenceId(), *track});
        if (m_adapter)
            require(m_adapter->enginePositionObserved(
                        *m_currentToken, *track).ok,
                    "real logical position should reach BA2");
    }

    void handleLoaded()
    {
        require(m_adapter != nullptr,
                "real engine should be bound before FILE_LOADED");
        const auto entryId = vinyl::currentMpvPlaylistEntryId(m_sidecar);
        require(entryId.has_value(),
                "FILE_LOADED should expose current playlist-entry-id");
        const auto token = m_entryLedger.tokenFor(*entryId);
        require(token.has_value(),
                "FILE_LOADED should attribute through playlist-entry ledger");
        require(m_currentToken.has_value() && *m_currentToken == *token,
                "FILE_LOADED attribution should match current live work");

        require(m_adapter->engineLoaded(*token).ok,
                "real FILE_LOADED should settle BA2");

        const auto physical = physicalPositionMs();
        require(physical.has_value(),
                "real FILE_LOADED should expose physical position");
        require(m_transform.has_value(),
                "real FILE_LOADED should have playback transform");
        const auto track =
            m_transform->trackPositionForPhysical(*physical);
        require(track.has_value(),
                "physical start should map into logical track time");

        qint64 logicalDuration = -1;
        if (m_currentDurationMs.has_value()) {
            logicalDuration = *m_currentDurationMs;
            require(m_adapter->engineDurationObserved(
                        *token, logicalDuration).ok,
                    "logical slice duration should reach BA2");
        }

        m_loads.append({
            *token,
            *entryId,
            *physical,
            *track,
            logicalDuration,
        });
    }

    void dispatchPhysicalSeek(
        const vinyl::PlaybackSeekCommand &command)
    {
        require(m_transform.has_value(),
                "real seek should have playback transform");
        const auto physical =
            m_transform->physicalPositionForTrack(command.targetMs());
        require(physical.has_value(),
                "logical seek target should map to physical source time");
        m_lastSeekPhysicalMs = *physical;

        const QByteArray target = QByteArray::number(
            vinyl::mpvSecondsFromMilliseconds(*physical), 'f', 3);
        const char *args[] = {
            "seek",
            target.constData(),
            "absolute",
            "exact",
            nullptr,
        };
        if (mpv_command(m_owner, args) < 0) {
            if (auto failed = m_seekState.failActive(command)) {
                if (m_adapter) m_adapter->engineSeekFailed(failed->request(), QStringLiteral("seek_command_failed"));
                if (m_coordinator) m_coordinator->engineFailed(command.workToken(), {QStringLiteral("seek_command_failed"), {}});
                stop(command.workToken());
            }
        }
    }

    void finishSeekIfReady()
    {
        if (!m_currentToken.has_value() || !m_adapter)
            return;
        const auto physical = physicalPositionMs();
        if (!physical.has_value() || !m_transform.has_value()) return;
        const auto track = m_transform->trackPositionForPhysical(*physical);
        if (!track.has_value()) return;
        auto advance = m_seekState.observePlaybackRestart(*m_currentToken, *track, m_clock.elapsed());
        if (!advance.has_value()) return;
        m_lastSeekTrackMs = *track;
        if (qEnvironmentVariableIsSet("VINYL_M0R_SEEK_TRACE"))
            qInfo() << "M0R seek completion" << "targetMs" << advance->completed.targetMs()
                    << "physicalMs" << *physical << "trackMs" << *track;

        const auto applied = m_adapter->engineSeekApplied(advance->completed, *track);
        require(applied.ok || applied.error.code == vinyl::PlaybackSessionErrorCode::SupersededSeek,
                "physical seek completion should apply or preserve the latest queued request");
        if (advance->nextPhysical.has_value())
            dispatchPhysicalSeek(*advance->nextPhysical);
    }

    void handleEnd(mpv_event *event)
    {
        require(event && event->data,
                "END_FILE should contain event data");
        const auto *end =
            static_cast<const mpv_event_end_file *>(event->data);
        const auto token =
            m_entryLedger.tokenFor(end->playlist_entry_id);
        require(token.has_value(),
                "END_FILE should attribute through playlist-entry ledger");

        sampleCurrentPosition();
        m_entryLedger.forget(end->playlist_entry_id);
        m_seekState.cancelWork(*token);

        require(m_coordinator != nullptr,
                "real engine should be bound to coordinator before EOF");
        if (end->error >= 0
            && end->reason == MPV_END_FILE_REASON_EOF) {
            m_endStatuses.append(
                m_coordinator->engineEnded(*token));
        } else {
            m_endStatuses.append(
                m_coordinator->engineFailed(
                    *token,
                    {QStringLiteral("mpv_end_file"),
                     QString::fromUtf8(mpv_error_string(end->error))}));
        }
    }

    QElapsedTimer m_clock;
    mpv_handle *m_owner = nullptr;
    mpv_handle *m_sidecar = nullptr;
    vinyl::PlaybackEngineAdapter *m_adapter = nullptr;
    vinyl::QueueSessionCoordinator *m_coordinator = nullptr;
    std::optional<vinyl::PlaybackWorkToken> m_currentToken;
    std::optional<vinyl::PlaybackTimeTransform> m_transform;
    std::optional<qint64> m_currentDurationMs;
    vinyl::MpvPlaylistEntryWorkLedger m_entryLedger;
    vinyl::SerializedMpvSeekState m_seekState;
    QVector<LoadObservation> m_loads;
    QVector<vinyl::QueueSessionStatus> m_endStatuses;
    QVector<std::pair<vinyl::QueueOccurrenceId, qint64>> m_lastPositions;
    std::optional<qint64> m_lastSeekPhysicalMs;
    std::optional<qint64> m_lastSeekTrackMs;
};

template <typename T>
T mustExact(const vinyl::ExactTrackContractResult<T> &r, const char *m)
{
    require(r.ok(), m);
    return *r.value;
}

vinyl::ReleaseTrackCoordinate coordinate(
    const vinyl::ReleaseGroupMbid &group,
    const vinyl::ReleaseMbid &release,
    const vinyl::ReleaseTrackMbid &track,
    const vinyl::RecordingMbid &recordingMbid,
    int position)
{
    return mustDomain(
        vinyl::ReleaseTrackCoordinate::create(
            group, release, track, recordingMbid, 1, position),
        "release-track coordinate should construct");
}

vinyl::RequestedTrackIdentity requestFor(
    const vinyl::RecordingMbid &recordingMbid,
    const vinyl::ReleaseTrackCoordinate &coord,
    const QString &title)
{
    vinyl::TrackTextEvidence text;
    text.title = title;
    text.artist = QStringLiteral("W4-H Artist");
    text.album = QStringLiteral("W4-H CUE Album");
    return mustExact(
        vinyl::RequestedTrackIdentity::create(
            recordingMbid, coord, text),
        "CUE exact request should construct");
}

vinyl::ResolvedTrackSource resolvedLocal(
    const vinyl::SourceResolutionRequest &request,
    const QString &path,
    qint64 resolvedAtMs,
    std::optional<qint64> expiresAtMs = std::nullopt)
{
    return mustMpv(
        vinyl::ResolvedTrackSource::create(
            request.recordingMbid(),
            request.candidateKey(),
            request.sourceKind(),
            vinyl::SourceResolutionPath::Direct,
            vinyl::ResolvedPlaybackResourceKind::LocalPath,
            path,
            {},
            std::nullopt,
            std::nullopt,
            resolvedAtMs,
            expiresAtMs),
        "local resolved source should construct");
}

bool hasEndCause(
    const vinyl::QueueSessionStatus &status,
    vinyl::OccurrenceEndCause cause)
{
    for (const auto &fact : status.transitionFacts) {
        if (const auto *ended = std::get_if<vinyl::OccurrenceEnded>(&fact)) {
            if (ended->cause == cause)
                return true;
        }
    }
    return false;
}

void verifyRealFallbackJourney(
    const QString &fixture,
    bool negativeWithoutFallback)
{
    using namespace vinyl;

    const ProfileScopeId profile = w4h::profile("w4h-real-libmpv-fallback");
    const RecordingMbid song =
        recording("33333333-3333-4333-8333-333333333333");
    const QueueOccurrence q1 = occurrence("real-fallback-occ", song);

    VinylQueue queue;
    require(queue.append(q1).ok
                && queue.activate(q1.occurrenceId()).ok,
            "real fallback queue should construct");

    PlaybackSession playback;
    RealMpvEngine engine;
    PlaybackEngineAdapter adapter(playback, engine);
    w4h::EmptyInputProvider inputProvider;
    QueueSessionCoordinator coordinator(
        queue, playback, adapter, inputProvider);
    engine.bind(&adapter, &coordinator);

    ReferenceSourceKindScorer scorer;
    SourceProfileMemory sourceMemory(profile);
    SourceResolutionSession sources(
        queue,
        playback,
        coordinator,
        SourceSelectionPolicy::vinylReference(),
        scorer,
        sourceMemory, profile);

    ListenEventLedger ledger(profile);
    ListenEventRecorder recorder(ledger);

    auto requireActiveInvariant = [&](const char *message) {
        const auto active = queue.activeOccurrenceId();
        const auto &bound = playback.activeOccurrence();
        require(!bound.has_value()
                    || (active.has_value()
                        && bound->occurrenceId() == *active),
                message);
    };

    require(coordinator.start(true).ok
                && coordinator.pendingInput().has_value(),
            "real fallback journey should enter AwaitingInput");
    requireActiveInvariant(
        "real fallback AwaitingInput must preserve active occurrence");

    const SelectableSourceCandidate missingCandidate =
        w4h::exactAudio(
            song,
            "w4h.real.missing",
            "missing-local",
            1000,
            true,
            SourceKind::LocalFile);
    const SelectableSourceCandidate fallbackCandidate =
        w4h::exactAudio(
            song,
            "w4h.real.fixture",
            "fixture-local",
            900,
            false,
            SourceKind::LocalFile);

    QVector<SelectableSourceCandidate> candidates{missingCandidate};
    if (!negativeWithoutFallback)
        candidates.append(fallbackCandidate);

    const SourceResolutionRequest firstRequest = w4h::mustResolution(
        sources.beginForActive(candidates, true),
        "real fallback BB5 request should begin");
    require(firstRequest.candidateKey()
                == missingCandidate.candidateKey(),
            "real fallback first request must choose missing local source");

    const QString missingPath =
        fixture + QStringLiteral(".w4h-definitely-missing");
    const SourceResolutionOutcome firstAccepted =
        sources.acceptResolved(
            firstRequest,
            resolvedLocal(firstRequest, missingPath, 1000, 10000),
            1500);
    require(firstAccepted.action
                == SourceResolutionAction::PlaybackRestarted,
            "missing source must reach real mpv load through BB5");
    requireActiveInvariant(
        "real missing-source restart must preserve active occurrence");

    require(playback.activeWorkToken().has_value(),
            "real missing-source load should mint a work token");
    const PlaybackWorkToken failedToken =
        *playback.activeWorkToken();
    const PlayInstanceId failedPlay =
        w4h::playInstance("real-fallback-failed-play");
    const ListenSourceTraceId failedTrace =
        w4h::sourceTrace("real-missing-local");
    const ListenSourceTraceId fallbackTrace =
        w4h::sourceTrace("real-fixture-local");
    const ListenContext failedContext = w4h::mustListen(
        ListenContext::create(
            profile,
            w4h::listenSession("real-fallback-session"),
            q1,
            failedToken,
            failedPlay),
        "real failed-source BA4 context should construct");
    require(recorder.start(
                failedContext,
                10000,
                0,
                0,
                1000,
                failedTrace).ok,
            "BA4 failed-source context should start");

    require(engine.pumpUntil(
                [&]() {
                    return !engine.endStatuses().isEmpty();
                },
                5.0),
            "real mpv missing local path must emit END_FILE failure");
    require(engine.endStatuses().constFirst().ok
                && hasEndCause(
                    engine.endStatuses().constFirst(),
                    OccurrenceEndCause::Failed),
            "real mpv load error must reach coordinator engineFailed");
    requireActiveInvariant(
        "real mpv engineFailed must preserve active occurrence");

    require(recorder.failure(
                failedToken, 11000, 1000, 0, 1000).ok,
            "BA4 must record real mpv failure");
    requireActiveInvariant(
        "BA4 failure recording must not disturb active occurrence");

    const SourceResolutionOutcome fallback =
        sources.playbackFailed(failedToken);
    require(fallback.action == SourceResolutionAction::NeedResolution
                && fallback.nextRequest.has_value(),
            "real mpv failure must cause BB5 to choose a fallback source");
    requireActiveInvariant(
        "BB5 fallback selection must preserve active occurrence");

    require(fallback.nextRequest->candidateKey()
                == fallbackCandidate.candidateKey(),
            "real mpv failure must select the fixture fallback candidate");
    require(recorder.fallbackSourceChangedAfterFailure(
                failedTrace,
                fallbackTrace,
                12000,
                1001,
                0,
                1000).ok,
            "BA4 must record source change immediately after failure");

    const SourceResolutionOutcome fallbackAccepted =
        sources.acceptResolved(
            *fallback.nextRequest,
            resolvedLocal(
                *fallback.nextRequest,
                fixture,
                2000,
                10000),
            2500);
    require(fallbackAccepted.action
                == SourceResolutionAction::PlaybackRestarted,
            "fixture fallback must restart playback through BB5");
    requireActiveInvariant(
        "fixture fallback restart must preserve active occurrence");

    require(playback.activeWorkToken().has_value(),
            "fixture fallback should mint a replacement work token");
    const PlaybackWorkToken fallbackToken =
        *playback.activeWorkToken();
    const ListenContext fallbackContext = w4h::mustListen(
        ListenContext::create(
            profile,
            w4h::listenSession("real-fallback-session"),
            q1,
            fallbackToken,
            w4h::playInstance("real-fallback-playing"),
            failedPlay),
        "real fallback continuation context should construct");
    require(recorder.start(
                fallbackContext,
                12001,
                1002,
                0,
                1000,
                fallbackTrace).ok,
            "BA4 fallback continuation should start");

    require(engine.pumpUntil(
                [&]() {
                    return playback.state() == PlaybackState::Playing
                        && !engine.loads().isEmpty();
                },
                5.0),
            "fixture fallback must reach real FILE_LOADED and Playing");
    requireActiveInvariant(
        "real fixture Playing state must preserve active occurrence");

    int failureIndex = -1;
    int sourceChangeIndex = -1;
    for (int i = 0; i < ledger.events().size(); ++i) {
        if (ledger.events().at(i).kind() == ListenEventKind::Failure)
            failureIndex = i;
        if (ledger.events().at(i).kind() == ListenEventKind::SourceChange)
            sourceChangeIndex = i;
    }
    require(failureIndex >= 0
                && sourceChangeIndex == failureIndex + 1,
            "BA4 must hold Failure immediately followed by SourceChange");
    require(ledger.events().at(sourceChangeIndex).sourceBefore().has_value()
                && *ledger.events().at(sourceChangeIndex).sourceBefore()
                    == failedTrace
                && ledger.events().at(sourceChangeIndex).sourceAfter().has_value()
                && *ledger.events().at(sourceChangeIndex).sourceAfter()
                    == fallbackTrace,
            "BA4 source change must preserve failed and replacement provenance");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "W4-H real CUE journey must execute on Qt 6.11.1");
    const bool negativeWithoutFallback =
        argc == 3
        && QString::fromLocal8Bit(argv[2])
            == QStringLiteral("--negative-no-fallback");
    require(argc == 2 || negativeWithoutFallback,
            "W4-H real journey requires fixture and optional negative-control flag");

    const QString fixture =
        QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    require(QFileInfo(fixture).isFile(),
            "W4-H real CUE fixture should exist");

    verifyRealFallbackJourney(fixture, negativeWithoutFallback);

    const qint64 totalMs = fixtureDurationMs(fixture);
    qint64 splitFrames =
        std::max<qint64>(15, (totalMs / 2) * 75 / 1000);
    while (splitFrames > 15
           && (splitFrames * 1000 / 75) > totalMs - 300) {
        --splitFrames;
    }

    const QString cueText = QStringLiteral(
        "PERFORMER \"W4-H Artist\"\n"
        "TITLE \"W4-H CUE Album\"\n"
        "FILE \"shared-physical-source\" WAVE\n"
        "  TRACK 01 AUDIO\n"
        "    TITLE \"Slice A\"\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    TITLE \"Slice B\"\n"
        "    INDEX 01 %1\n")
        .arg(cueClock(splitFrames));

    const WholeAlbumSourceId wholeSource = mustCue(
        WholeAlbumSourceId::parse(
            QStringLiteral("w4h-whole-album-source")),
        "whole-album source should parse");
    const CueVirtualAlbum album = mustCue(
        CueWholeAlbum::normalize(
            wholeSource,
            CueInputOrigin::ExternalSidecar,
            cueText,
            totalMs),
        "two-slice CUE should normalize");
    require(album.tracks.size() == 2,
            "real CUE fixture should expose exactly two logical slices");
    require(album.tracks[0].startMs == 0
                && album.tracks[0].endMs.has_value()
                && album.tracks[1].startMs
                    == *album.tracks[0].endMs
                && album.tracks[1].endMs.has_value()
                && *album.tracks[1].endMs == totalMs,
            "CUE slices should form adjacent bounds over one physical source");

    const RecordingMbid recordingA =
        recording("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const RecordingMbid recordingB =
        recording("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    const ReleaseGroupMbid releaseGroup = mustDomain(
        ReleaseGroupMbid::parse(
            QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc")),
        "release group should parse");
    const ReleaseMbid release = mustDomain(
        ReleaseMbid::parse(
            QStringLiteral("dddddddd-dddd-4ddd-8ddd-dddddddddddd")),
        "release should parse");
    const ReleaseTrackMbid releaseTrackA = mustDomain(
        ReleaseTrackMbid::parse(
            QStringLiteral("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee")),
        "release track A should parse");
    const ReleaseTrackMbid releaseTrackB = mustDomain(
        ReleaseTrackMbid::parse(
            QStringLiteral("ffffffff-ffff-4fff-8fff-ffffffffffff")),
        "release track B should parse");

    const auto coordA = coordinate(
        releaseGroup, release, releaseTrackA, recordingA, 1);
    const auto coordB = coordinate(
        releaseGroup, release, releaseTrackB, recordingB, 2);
    const auto requestA =
        requestFor(recordingA, coordA, QStringLiteral("Slice A"));
    const auto requestB =
        requestFor(recordingB, coordB, QStringLiteral("Slice B"));

    CueBb2ManifestContext manifestContext;
    manifestContext.sourceCandidateKey = sourceKey();
    manifestContext.mediumPosition = 1;
    const ExactVirtualTrackSlice sliceA = mustCue(
        CueWholeAlbum::resolveExactSlice(
            album, manifestContext, requestA),
        "Slice A should resolve exactly through BB2/BB3");
    const ExactVirtualTrackSlice sliceB = mustCue(
        CueWholeAlbum::resolveExactSlice(
            album, manifestContext, requestB),
        "Slice B should resolve exactly through BB2/BB3");
    require(sliceA.recordingMbid == recordingA
                && sliceB.recordingMbid == recordingB
                && !(sliceA.virtualTrackId == sliceB.virtualTrackId),
            "logical CUE slices should retain separate Recording and virtual identities");

    const ResolvedTrackSource sourceA = mustMpv(
        ResolvedTrackSource::createForExactVirtualSlice(
            sliceA,
            sourceKey(),
            SourceKind::LocalFile,
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::LocalPath,
            fixture,
            {},
            0),
        "Slice A resolved transport should construct");
    const ResolvedTrackSource sourceB = mustMpv(
        ResolvedTrackSource::createForExactVirtualSlice(
            sliceB,
            sourceKey(),
            SourceKind::LocalFile,
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::LocalPath,
            fixture,
            {},
            0),
        "Slice B resolved transport should construct");
    const PlaybackLoadInput inputA = mustMpv(
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(
            sourceA, recordingA),
        "Slice A should become PlaybackLoadInput");
    const PlaybackLoadInput inputB = mustMpv(
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(
            sourceB, recordingB),
        "Slice B should become PlaybackLoadInput");

    require(inputA.resource() == inputB.resource()
                && inputA.playbackWindow().has_value()
                && inputB.playbackWindow().has_value(),
            "two logical tracks should share one physical source with distinct windows");

    const QueueOccurrence q1 =
        occurrence("cue-occ-a", recordingA);
    const QueueOccurrence q2 =
        occurrence("cue-occ-b", recordingB);
    VinylQueue queue;
    require(queue.append(q1).ok && queue.append(q2).ok,
            "real CUE queue should append two logical occurrences");

    PlaybackSession playback;
    RealMpvEngine engine;
    PlaybackEngineAdapter adapter(playback, engine);
    CueInputProvider provider(
        q1.occurrenceId(), inputA,
        q2.occurrenceId(), inputB);
    QueueSessionCoordinator coordinator(
        queue, playback, adapter, provider);
    engine.bind(&adapter, &coordinator);

    const QueueSessionStatus started = coordinator.start(true);
    require(started.ok,
            "real CUE journey should start first logical slice");
    require(engine.pumpUntil(
                [&]() {
                    return engine.loads().size() >= 1
                        && playback.state() == PlaybackState::Playing;
                },
                5.0),
            "Slice A should reach real FILE_LOADED and Playing");

    const auto firstLoad = engine.loads().at(0);
    require(firstLoad.token.occurrenceId() == q1.occurrenceId(),
            "real load attribution should bind Slice A occurrence");
    require(firstLoad.trackPositionMs >= 0
                && firstLoad.trackPositionMs <= 300,
            "Slice A should begin at logical track-relative zero");
    require(firstLoad.logicalDurationMs
                == *inputA.playbackWindow()->durationMs(),
            "Slice A duration should be logical CUE slice duration");

    const qint64 seekTarget =
        std::max<qint64>(
            50,
            *inputA.playbackWindow()->durationMs() / 2);
    require(adapter.seek(seekTarget / 2).ok(),
            "real first seek should dispatch before the latest queued seek");
    const auto seekResult = adapter.seek(seekTarget);
    require(seekResult.ok(),
            "real logical seek should be accepted");
    require(engine.pumpUntil(
                [&]() {
                    return !playback.pendingSeek().has_value()
                        && engine.lastSeekTrackMs().has_value();
                },
                4.0),
            "real logical seek should complete through single-flight machinery");
    require(engine.lastSeekPhysicalMs().has_value()
                && engine.lastSeekTrackMs().has_value(),
            "real seek should expose physical and logical observations");
    require(std::llabs(
                *engine.lastSeekTrackMs() - seekTarget) <= 300,
            "real seek completion should report logical target time");
    require(std::llabs(
                *engine.lastSeekPhysicalMs()
                - (sliceA.startMs + seekTarget)) <= 5,
            "real seek dispatch should offset into physical source time");

    require(engine.pumpUntil(
                [&]() {
                    return engine.loads().size() >= 2
                        && queue.activeOccurrenceId().has_value()
                        && *queue.activeOccurrenceId()
                            == q2.occurrenceId();
                },
                12.0),
            "logical EOF of Slice A should advance queue to Slice B");

    require(!engine.endStatuses().isEmpty()
                && engine.endStatuses().at(0).ok,
            "Slice A real EOF should reach coordinator cleanly");
    bool firstEofCause = false;
    for (const auto &fact :
         engine.endStatuses().at(0).transitionFacts) {
        if (const auto *ended =
                std::get_if<OccurrenceEnded>(&fact)) {
            if (ended->occurrenceId == q1.occurrenceId()
                && ended->cause == OccurrenceEndCause::Eof) {
                firstEofCause = true;
            }
        }
    }
    require(firstEofCause,
            "Slice A physical bound should surface as logical EOF cause");

    const auto secondLoad = engine.loads().at(1);
    require(secondLoad.token.occurrenceId() == q2.occurrenceId()
                && secondLoad.token.occurrenceId()
                    != firstLoad.token.occurrenceId(),
            "Slice B should remain a distinct QueueOccurrence");
    require(q1.recordingMbid() != q2.recordingMbid(),
            "Slice B should remain a distinct Recording identity");
    require(secondLoad.physicalPositionMs
                >= sliceB.startMs - 300,
            "Slice B should begin at its physical CUE offset");
    require(secondLoad.trackPositionMs >= 0
                && secondLoad.trackPositionMs <= 300,
            "Slice B user-facing position should restart at logical zero");
    require(secondLoad.logicalDurationMs
                == *inputB.playbackWindow()->durationMs(),
            "Slice B duration should be logical CUE slice duration");
    require(secondLoad.physicalPositionMs > secondLoad.trackPositionMs + 200,
            "physical-file offset must not leak into logical domain position");
    require(adapter.positionMs().has_value()
                && *adapter.positionMs() <= 300,
            "BA2 user-facing position should remain track-relative on Slice B");

    require(engine.pumpUntil(
                [&]() {
                    return engine.endStatuses().size() >= 2
                        && !queue.activeOccurrenceId().has_value();
                },
                12.0),
            "Slice B should reach real logical EOF and exhaust queue");
    require(engine.endStatuses().at(1).ok,
            "Slice B real EOF should reach coordinator cleanly");

    const auto lastA =
        engine.lastTrackFor(q1.occurrenceId());
    const auto lastB =
        engine.lastTrackFor(q2.occurrenceId());
    require(lastA.has_value() && lastB.has_value(),
            "both real CUE slices should expose track-relative progress");
    require(*lastA <= *inputA.playbackWindow()->durationMs()
                && *lastB <= *inputB.playbackWindow()->durationMs(),
            "physical file time must never escape as logical progress");

    qInfo().noquote()
        << "VINYL_W4_H_REAL_CUE_OK checks=" << g_checks
        << "fixtureDurationMs=" << totalMs
        << "sliceA=" << sliceA.startMs << "-" << *sliceA.endMs
        << "sliceB=" << sliceB.startMs << "-" << *sliceB.endMs
        << "seekLogicalMs=" << seekTarget
        << "seekPhysicalMs=" << *engine.lastSeekPhysicalMs();
    return 0;
}
