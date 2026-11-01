#include "CueWholeAlbum.h"
#include "ResolvedPlaybackSource.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

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
T mustBb2(const vinyl::ExactTrackContractResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustCue(const vinyl::CueWholeAlbumResult<T> &result, const char *message)
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
T mustBb4(const vinyl::MpvPlaybackAdapterResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::SourceCandidateKey candidateKey()
{
    return vinyl::SourceCandidateKey(
        mustSource(
            vinyl::SourceProviderId::parse(QStringLiteral("cue.local")),
            "provider should parse"),
        mustSource(
            vinyl::SourceCandidateId::parse(QStringLiteral("whole-album-fixture")),
            "candidate should parse"));
}

QString fixtureCue()
{
    return QStringLiteral(
        "PERFORMER \"Fixture Artist\"\n"
        "TITLE \"Fixture Album\"\n"
        "FILE \"fixture-album.flac\" WAVE\n"
        "  TRACK 01 AUDIO\n"
        "    TITLE \"Prelude\"\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    TITLE \"First Movement\"\n"
        "    INDEX 01 00:10:74\n"
        "  TRACK 03 AUDIO\n"
        "    TITLE \"Finale\"\n"
        "    INDEX 01 01:00:00\n");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "W4-A CUE handoff must run against Qt 6.11.1");

    const WholeAlbumSourceId sourceId = mustCue(
        WholeAlbumSourceId::parse(QStringLiteral("whole-source-42")),
        "whole source should parse");
    const CueVirtualAlbum album = mustCue(
        CueWholeAlbum::normalize(
            sourceId,
            CueInputOrigin::ExternalSidecar,
            fixtureCue(),
            120000),
        "CUE should normalize");

    const RecordingMbid recording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "recording should parse");
    const ReleaseGroupMbid releaseGroup = mustDomain(
        ReleaseGroupMbid::parse(
            QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "release group should parse");
    const ReleaseMbid release = mustDomain(
        ReleaseMbid::parse(
            QStringLiteral("44444444-4444-4444-8444-444444444444")),
        "release should parse");
    const ReleaseTrackMbid releaseTrack = mustDomain(
        ReleaseTrackMbid::parse(
            QStringLiteral("55555555-5555-4555-8555-555555555555")),
        "release track should parse");
    const ReleaseTrackCoordinate coordinate = mustDomain(
        ReleaseTrackCoordinate::create(
            releaseGroup,
            release,
            releaseTrack,
            recording,
            1,
            2),
        "release-track coordinate should construct");

    TrackTextEvidence expectedText;
    expectedText.title = QStringLiteral("First Movement");
    expectedText.artist = QStringLiteral("Fixture Artist");
    expectedText.album = QStringLiteral("Fixture Album");

    const RequestedTrackIdentity request = mustBb2(
        RequestedTrackIdentity::create(
            recording,
            coordinate,
            expectedText),
        "requested track identity should construct");

    CueBb2ManifestContext context;
    context.mediumPosition = 1;
    const ExactVirtualTrackSlice slice = mustCue(
        CueWholeAlbum::resolveExactSlice(
            album,
            context,
            request),
        "BB3 exact slice should resolve");

    require(slice.startMs == 10987
                && slice.endMs.has_value()
                && *slice.endMs == 60000,
            "fixture exact slice must expose deterministic CUE bounds");

    const QString backingFile =
        QDir::root().filePath(QStringLiteral("vinyl-fixture/whole-album.flac"));

    const ResolvedTrackSource source = mustBb4(
        ResolvedTrackSource::createForExactVirtualSlice(
            slice,
            candidateKey(),
            SourceKind::LocalFile,
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::LocalPath,
            backingFile,
            {},
            qint64{0}),
        "BB3 exact CUE slice should become a bounded resolved track source");

    const PlaybackLoadInput input = mustBb4(
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(
            source,
            slice.recordingMbid),
        "resolved exact CUE slice should reach PlaybackLoadInput");

    require(input.playbackWindow().has_value(),
            "BB4 handoff must retain the exact virtual slice window");
    require(input.playbackWindow()->startMs() == slice.startMs
                && input.playbackWindow()->endMs() == slice.endMs,
            "BB3 bounds must survive BB4 without reinterpretation");

    const PlaybackTimeTransform sourceTime = source.timeTransform();
    require(sourceTime.trackPositionForPhysical(slice.startMs).has_value()
                && *sourceTime.trackPositionForPhysical(slice.startMs) == 0,
            "CUE physical start must become track-relative zero");
    require(sourceTime.trackPositionForPhysical(*slice.endMs).has_value()
                && *sourceTime.trackPositionForPhysical(*slice.endMs)
                    == *slice.endMs - slice.startMs,
            "CUE physical end must become logical track duration");
    require(sourceTime.recordingPositionForTrack(2500).has_value()
                && *sourceTime.recordingPositionForTrack(2500) == 2500,
            "zero recording offset should preserve recording-relative resume");
    require(sourceTime.trackPositionForRecording(2500).has_value()
                && *sourceTime.trackPositionForRecording(2500) == 2500,
            "known recording alignment should invert for source switching");

    qInfo().noquote()
        << "VINYL_W4A_CUE_HANDOFF_OK checks=" << g_checks
        << "startMs=" << slice.startMs
        << "endMs=" << *slice.endMs;
    return 0;
}
