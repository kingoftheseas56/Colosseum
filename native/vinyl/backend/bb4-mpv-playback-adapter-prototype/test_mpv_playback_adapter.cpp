#include "ResolvedPlaybackSource.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QUrl>

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
T mustSource(const vinyl::SourceCandidateResult<T> &result, const char *message)
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
T mustBb4(const vinyl::MpvPlaybackAdapterResult<T> &result, const char *message)
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
        "fixture Recording MBID should parse");
}

vinyl::SourceCandidateKey candidateKey(const char *provider, const char *candidate)
{
    return vinyl::SourceCandidateKey(
        mustSource(
            vinyl::SourceProviderId::parse(QString::fromLatin1(provider)),
            "fixture source provider should parse"),
        mustSource(
            vinyl::SourceCandidateId::parse(QString::fromLatin1(candidate)),
            "fixture source candidate should parse"));
}

vinyl::QueueOccurrence occurrence(
    const char *id,
    const vinyl::RecordingMbid &recordingMbid)
{
    return mustQueue(
        vinyl::QueueOccurrence::create(
            mustQueue(
                vinyl::QueueOccurrenceId::parse(QString::fromLatin1(id)),
                "fixture queue occurrence ID should parse"),
            recordingMbid),
        "fixture queue occurrence should construct");
}

struct RecordedLoad final {
    vinyl::PlaybackWorkToken token;
    vinyl::PlaybackLoadInput input;
};

class FakePlaybackEngine final : public vinyl::PlaybackEngineCommands
{
public:
    void load(const vinyl::PlaybackWorkToken &token,
              const vinyl::PlaybackLoadInput &input) override
    {
        loads.push_back(RecordedLoad{token, input});
    }

    void play(const vinyl::PlaybackWorkToken &) override {}
    void pause(const vinyl::PlaybackWorkToken &) override {}

    void seek(const vinyl::PlaybackSeekRequest &request) override
    {
        seeks.push_back(request);
    }

    void stop(const vinyl::PlaybackWorkToken &) override {}

    QVector<RecordedLoad> loads;
    QVector<vinyl::PlaybackSeekRequest> seeks;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "BB4 harness must execute against Qt 6.11.1");

    const RecordingMbid a =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid b =
        recording("22222222-2222-4222-8222-222222222222");

    const QString localPath =
        QDir::root().filePath(QStringLiteral("vinyl-fixture/Track 01.flac"));
    const QString fileUrl =
        QUrl::fromLocalFile(localPath).toString(QUrl::FullyEncoded);
    const QString httpUrl =
        QStringLiteral("https://audio.example.invalid/Track%2001.flac");

    const ResolvedTrackSource local = mustBb4(
        ResolvedTrackSource::create(
            a,
            candidateKey("local.library", "local-track-a"),
            SourceKind::LocalFile,
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::LocalPath,
            localPath),
        "local-path source should construct");
    const PlaybackLoadInput localInput = mustBb4(
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(local, a),
        "local-path source should map to PlaybackLoadInput");
    require(localInput.resource() == localPath,
            "local path must reach PlaybackLoadInput unchanged");
    require(localInput.requestHeaders().isEmpty(),
            "local path must not invent request headers");

    const auto playbackWindowResult =
        PlaybackWindow::create(10987, 60000);
    require(playbackWindowResult.ok(),
            "BB3-style window should construct");
    const PlaybackWindow playbackWindow =
        *playbackWindowResult.value;

    const ResolvedTrackSource timedLocal = mustBb4(
        ResolvedTrackSource::create(
            a,
            candidateKey("local.library", "timed-local-a"),
            SourceKind::LocalFile,
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::LocalPath,
            localPath,
            {},
            playbackWindow,
            -1000,
            100000,
            200000),
        "timed resolved audio source should construct");
    require(timedLocal.playbackWindow().has_value()
                && *timedLocal.playbackWindow() == playbackWindow,
            "resolved source must retain physical playback bounds");
    require(timedLocal.recordingTimeOffsetMs().has_value()
                && *timedLocal.recordingTimeOffsetMs() == -1000,
            "resolved source must retain known recording-time alignment");
    require(timedLocal.resolvedAtEpochMs().has_value()
                && *timedLocal.resolvedAtEpochMs() == 100000
                && timedLocal.expiresAtEpochMs().has_value()
                && *timedLocal.expiresAtEpochMs() == 200000,
            "resolved audio transport should retain explicit freshness metadata");
    require(!timedLocal.isExpiredAt(199999)
                && timedLocal.isExpiredAt(200000),
            "audio transport expiry should use the same boundary law as video");

    const PlaybackLoadInput timedInput = mustBb4(
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(
            timedLocal,
            a),
        "timed audio source should map to PlaybackLoadInput");
    require(timedInput.playbackWindow().has_value()
                && *timedInput.playbackWindow() == playbackWindow,
            "BB4 must not drop playback bounds at BA2 handoff");
    require(!PlaybackTimeTransform(playbackWindow)
                 .recordingPositionForTrack(1000)
                 .has_value(),
            "PlaybackLoadInput alone must not manufacture recording alignment");
    require(timedLocal.timeTransform()
                .recordingPositionForTrack(1000)
                .has_value()
                && *timedLocal.timeTransform()
                        .recordingPositionForTrack(1000)
                    == 0,
            "known source offset should map track time to recording time");

    const auto missingResolutionTime =
        ResolvedTrackSource::create(
            a,
            candidateKey("http.source", "bad-expiry-no-resolution"),
            SourceKind::Http,
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::HttpUrl,
            httpUrl,
            {},
            std::nullopt,
            std::nullopt,
            std::nullopt,
            200000);
    require(!missingResolutionTime.ok()
                && missingResolutionTime.error.code
                    == MpvPlaybackAdapterErrorCode::InvalidTimingMetadata,
            "known expiry without known resolution time must fail closed");

    const ResolvedTrackSource file = mustBb4(
        ResolvedTrackSource::create(
            a,
            candidateKey("local.library", "file-url-a"),
            SourceKind::LocalFile,
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::FileUrl,
            fileUrl),
        "file-URL source should construct");
    const PlaybackLoadInput fileInput = mustBb4(
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(file, a),
        "file-URL source should map to PlaybackLoadInput");
    require(fileInput.resource() == fileUrl,
            "file URL must reach PlaybackLoadInput unchanged");

    const QVector<PlaybackRequestHeader> headers = {
        {QStringLiteral("Referer"), QStringLiteral("https://example.invalid/")},
        {QStringLiteral("Origin"), QStringLiteral("https://example.invalid")},
    };
    const ResolvedTrackSource http = mustBb4(
        ResolvedTrackSource::create(
            a,
            candidateKey("http.source", "http-a"),
            SourceKind::Http,
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::HttpUrl,
            httpUrl,
            headers),
        "HTTP source should construct");
    const PlaybackLoadInput httpInput = mustBb4(
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(http, a),
        "HTTP source should map to PlaybackLoadInput");
    require(httpInput.resource() == httpUrl,
            "HTTP URL must reach PlaybackLoadInput unchanged");
    require(httpInput.requestHeaders() == headers,
            "validated transport headers must be preserved exactly");

    const ResolvedTrackSource external = mustBb4(
        ResolvedTrackSource::create(
            a,
            candidateKey("resolver.source", "resolver-a"),
            SourceKind::ExternalResolver,
            SourceResolutionPath::ExternalResolver,
            ResolvedPlaybackResourceKind::HttpUrl,
            httpUrl,
            headers),
        "external-resolver source should remain expressible");
    const PlaybackLoadInput externalInput = mustBb4(
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(external, a),
        "external-resolver output should map to PlaybackLoadInput");
    require(external.resolutionPath() == SourceResolutionPath::ExternalResolver,
            "resolved source should retain external-resolver provenance");
    require(external.sourceKind() == SourceKind::ExternalResolver,
            "BB1 source kind should remain provenance on resolved source");
    require(externalInput.resource() == httpInput.resource()
                && externalInput.requestHeaders() == httpInput.requestHeaders(),
            "provider/resolver provenance must not change equal transport material");
    require(external.recordingMbid() == a,
            "canonical RecordingMbid must remain on resolved source");

    const auto wrongRecording =
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(http, b);
    require(!wrongRecording.ok()
                && wrongRecording.error.code
                    == MpvPlaybackAdapterErrorCode::RecordingMismatch,
            "resolved source must fail closed before transport when RecordingMbid mismatches");

    const ResolvedTrackSource localB = mustBb4(
        ResolvedTrackSource::create(
            b,
            candidateKey("local.library", "local-track-b"),
            SourceKind::LocalFile,
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::LocalPath,
            localPath),
        "replacement local source for recording B should construct");
    const PlaybackLoadInput localInputB = mustBb4(
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(localB, b),
        "recording B source should map only against recording B");

    const auto relativePath = ResolvedTrackSource::create(
        a,
        candidateKey("local.library", "relative"),
        SourceKind::LocalFile,
        SourceResolutionPath::Direct,
        ResolvedPlaybackResourceKind::LocalPath,
        QStringLiteral("relative/track.flac"));
    require(!relativePath.ok()
                && relativePath.error.code
                    == MpvPlaybackAdapterErrorCode::InvalidResourceShape,
            "relative local path should fail closed");

    const auto wrongHttpScheme = ResolvedTrackSource::create(
        a,
        candidateKey("http.source", "ftp"),
        SourceKind::Http,
        SourceResolutionPath::Direct,
        ResolvedPlaybackResourceKind::HttpUrl,
        QStringLiteral("ftp://example.invalid/track.flac"));
    require(!wrongHttpScheme.ok()
                && wrongHttpScheme.error.code
                    == MpvPlaybackAdapterErrorCode::InvalidResourceShape,
            "non-HTTP URL should not masquerade as an HTTP resource");

    const auto localHeaders = ResolvedTrackSource::create(
        a,
        candidateKey("local.library", "headers"),
        SourceKind::LocalFile,
        SourceResolutionPath::Direct,
        ResolvedPlaybackResourceKind::LocalPath,
        localPath,
        headers);
    require(!localHeaders.ok()
                && localHeaders.error.code
                    == MpvPlaybackAdapterErrorCode::HeadersRequireHttpResource,
            "headers on local resources should fail closed");

    const auto injectedHeader = ResolvedTrackSource::create(
        a,
        candidateKey("http.source", "bad-header"),
        SourceKind::Http,
        SourceResolutionPath::Direct,
        ResolvedPlaybackResourceKind::HttpUrl,
        httpUrl,
        {{QStringLiteral("Referer"), QStringLiteral("good\r\nInjected: bad")}});
    require(!injectedHeader.ok()
                && injectedHeader.error.code
                    == MpvPlaybackAdapterErrorCode::PlaybackLoadInputRejected,
            "unsafe header should be rejected by the BA2 load-input boundary");
    require(injectedHeader.error.loadInputError.has_value()
                && *injectedHeader.error.loadInputError
                    == PlaybackLoadInputErrorCode::InvalidHeader,
            "BB4 should preserve the BA2 typed header-validation failure");

    PlaybackSession session;
    FakePlaybackEngine engine;
    PlaybackEngineAdapter playback(session, engine);

    const PlaybackWorkToken token1 = mustSession(
        playback.beginOccurrence(
            occurrence("bb4-occ-a", a),
            httpInput,
            true),
        "mapped source should enter BA2 only through beginOccurrence");
    require(engine.loads.size() == 1 && engine.loads.at(0).token == token1,
            "BA2 should issue the mapped load under its exact work token");
    require(engine.loads.at(0).input.resource() == httpUrl
                && engine.loads.at(0).input.requestHeaders() == headers,
            "tokenized BA2 load should preserve BB4 transport mapping");

    require(playback.engineLoaded(token1).ok,
            "first mapped source should accept matching load callback");
    const PlaybackSeekRequest seek1 = mustSession(
        playback.seek(12.5),
        "seek should be created by BA2 after BB4 mapping");
    require(engine.seeks.size() == 1
                && engine.seeks.at(0).workToken() == token1
                && engine.seeks.at(0).sequence() == seek1.sequence(),
            "BB4 mapping must not bypass BA2 seek request identity");

    const PlaybackWorkToken token2 = mustSession(
        playback.beginOccurrence(
            occurrence("bb4-occ-b", b),
            localInputB,
            true),
        "replacement mapped source should begin through BA2");
    require(token2 != token1,
            "replacement load must receive a new BA2 work token");
    require(engine.loads.size() == 2 && engine.loads.at(1).token == token2,
            "replacement source should be loaded only under the new token");

    const PlaybackMutationStatus staleLoad = playback.engineLoaded(token1);
    require(!staleLoad.ok
                && staleLoad.error.code == PlaybackSessionErrorCode::StaleAsyncWork,
            "old load callback must remain stale after BB4 source replacement");
    const PlaybackMutationStatus staleSeek =
        playback.engineSeekApplied(seek1, 12.5);
    require(!staleSeek.ok
                && staleSeek.error.code == PlaybackSessionErrorCode::StaleAsyncWork,
            "old seek completion must remain stale after source replacement");

    require(playback.engineLoaded(token2).ok,
            "current replacement load should be accepted");
    const int loadCountBeforeFailure = engine.loads.size();
    require(playback.engineFailed(
                token2,
                PlaybackFailure{
                    QStringLiteral("mpv_error"),
                    QStringLiteral("fixture failure")})
                .ok,
            "current playback failure should flow into BA2");
    require(session.state() == PlaybackState::Failed,
            "current playback failure should terminate the BA2 lifecycle");
    require(engine.loads.size() == loadCountBeforeFailure,
            "playback failure must not silently trigger source re-ranking or reload");

    qInfo().noquote()
        << "VINYL_BB4_MPV_PLAYBACK_ADAPTER_OK checks=" << g_checks
        << "qt=" << qVersion();
    return 0;
}