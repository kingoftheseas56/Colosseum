#include "CueWholeAlbum.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <iostream>
#include <type_traits>

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
T mustBb2(const vinyl::ExactTrackContractResult<T> &result,
          const char *message)
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

    static_assert(!std::is_same<VirtualTrackId, RecordingMbid>::value,
                  "BB3 virtual identity must remain distinct from Recording MBID");
    static_assert(!std::is_same<VirtualTrackId, ReleaseTrackMbid>::value,
                  "BB3 virtual identity must remain distinct from Release Track MBID");
    static_assert(!std::is_convertible<WholeAlbumSourceId, RecordingMbid>::value,
                  "physical source correlation must not become canonical identity");

    const auto sourceId = mustCue(
        WholeAlbumSourceId::parse(QStringLiteral("whole-source-42")),
        "whole-album source ID should parse");

    const auto album = mustCue(
        CueWholeAlbum::normalize(
            sourceId,
            CueInputOrigin::ExternalSidecar,
            fixtureCue(),
            120000),
        "valid CUE should normalize");

    require(album.tracks.size() == 3,
            "one physical source should expose three virtual tracks");
    require(album.sourceId == sourceId,
            "all virtual tracks should remain scoped to one physical source");
    require(album.tracks[1].startMs == 10987,
            "00:10:74 must use standard 75-fps conversion and round to 10987 ms");
    require(album.tracks[0].endMs == album.tracks[1].startMs,
            "first virtual track should end at adjacent track start");
    require(album.tracks[1].endMs == album.tracks[2].startMs,
            "second virtual track should end at adjacent track start");
    require(album.tracks[0].endKind == VirtualTrackEndKind::NextTrackStart,
            "adjacent track end kind should be explicit");
    require(album.tracks[2].endMs && *album.tracks[2].endMs == 120000,
            "final known track should end at backing duration");
    require(album.tracks[2].endKind == VirtualTrackEndKind::BackingDuration,
            "final known end kind should be explicit");

    const auto openAlbum = mustCue(
        CueWholeAlbum::normalize(
            sourceId,
            CueInputOrigin::EmbeddedMetadata,
            fixtureCue()),
        "CUE without known backing duration should normalize");
    require(openAlbum.tracks[1].endMs == openAlbum.tracks[2].startMs,
            "adjacent boundaries remain known without total duration");
    require(!openAlbum.tracks[2].endMs,
            "final virtual track should remain open when backing duration is unknown");
    require(openAlbum.tracks[2].endKind == VirtualTrackEndKind::OpenEnded,
            "unknown final bound should be explicit rather than invented");
    require(openAlbum.origin == CueInputOrigin::EmbeddedMetadata,
            "embedded and sidecar inputs should retain provenance only");

    const auto invalidFrame = CueWholeAlbum::normalize(
        sourceId,
        CueInputOrigin::ExternalSidecar,
        QStringLiteral(
            "FILE \"a.flac\" WAVE\n"
            "TRACK 01 AUDIO\n"
            "INDEX 01 00:00:75\n"),
        1000);
    require(!invalidFrame.ok()
                && invalidFrame.error.code == CueWholeAlbumErrorCode::InvalidCueFrame,
            "frame 75 must fail closed");

    const auto backwards = CueWholeAlbum::normalize(
        sourceId,
        CueInputOrigin::ExternalSidecar,
        QStringLiteral(
            "FILE \"a.flac\" WAVE\n"
            "TRACK 01 AUDIO\nINDEX 01 00:02:00\n"
            "TRACK 02 AUDIO\nINDEX 01 00:01:00\n"),
        10000);
    require(!backwards.ok()
                && backwards.error.code == CueWholeAlbumErrorCode::NonIncreasingStart,
            "backwards virtual ranges must fail closed");

    const auto overlapping = CueWholeAlbum::normalize(
        sourceId,
        CueInputOrigin::ExternalSidecar,
        QStringLiteral(
            "FILE \"a.flac\" WAVE\n"
            "TRACK 01 AUDIO\nINDEX 01 00:01:00\n"
            "TRACK 02 AUDIO\nINDEX 01 00:01:00\n"),
        10000);
    require(!overlapping.ok()
                && overlapping.error.code == CueWholeAlbumErrorCode::NonIncreasingStart,
            "zero-length or overlapping adjacent ranges must fail closed");

    const auto multipleFiles = CueWholeAlbum::normalize(
        sourceId,
        CueInputOrigin::ExternalSidecar,
        QStringLiteral(
            "FILE \"a.flac\" WAVE\n"
            "TRACK 01 AUDIO\nINDEX 01 00:00:00\n"
            "FILE \"b.flac\" WAVE\n"
            "TRACK 02 AUDIO\nINDEX 01 00:10:00\n"),
        20000);
    require(!multipleFiles.ok()
                && multipleFiles.error.code == CueWholeAlbumErrorCode::MultipleFileSections,
            "single-source BB3 normalization must reject multiple FILE sections");

    const auto missingIndex = CueWholeAlbum::normalize(
        sourceId,
        CueInputOrigin::ExternalSidecar,
        QStringLiteral("FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\n"),
        1000);
    require(!missingIndex.ok()
                && missingIndex.error.code == CueWholeAlbumErrorCode::MissingIndex01,
            "every audio track must carry INDEX 01");

    const auto beyondDuration = CueWholeAlbum::normalize(
        sourceId,
        CueInputOrigin::ExternalSidecar,
        QStringLiteral(
            "FILE \"a.flac\" WAVE\n"
            "TRACK 01 AUDIO\nINDEX 01 00:10:00\n"),
        10000);
    require(!beyondDuration.ok()
                && beyondDuration.error.code == CueWholeAlbumErrorCode::StartBeyondBackingDuration,
            "a track starting at backing-file end must fail closed");

    const auto recording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "recording MBID should parse");
    const auto releaseGroup = mustDomain(
        ReleaseGroupMbid::parse(
            QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "release-group MBID should parse");
    const auto release = mustDomain(
        ReleaseMbid::parse(
            QStringLiteral("44444444-4444-4444-8444-444444444444")),
        "release MBID should parse");
    const auto releaseTrack = mustDomain(
        ReleaseTrackMbid::parse(
            QStringLiteral("55555555-5555-4555-8555-555555555555")),
        "release-track MBID should parse");
    const auto coordinate = mustDomain(
        ReleaseTrackCoordinate::create(
            releaseGroup,
            release,
            releaseTrack,
            recording,
            1,
            2),
        "release-track coordinate should parse");

    TrackTextEvidence expectedText;
    expectedText.title = QStringLiteral("First Movement");
    expectedText.artist = QStringLiteral("Fixture Artist");
    expectedText.album = QStringLiteral("Fixture Album");
    const auto request = mustBb2(
        RequestedTrackIdentity::create(
            recording,
            coordinate,
            expectedText),
        "BB2 request should be valid");

    CueBb2ManifestContext manifestContext;
    manifestContext.mediumPosition = 1;
    const auto manifest = mustCue(
        CueWholeAlbum::toBb2Manifest(album, manifestContext),
        "CUE album should bridge to a BB2 manifest");
    require(manifest.entries().size() == 3,
            "BB2 manifest should expose each virtual occurrence separately");
    for (const auto &entry : manifest.entries()) {
        require(!entry.canonicalEvidence().recordingMbid,
                "CUE metadata must not manufacture Recording MBIDs");
        require(!entry.canonicalEvidence().releaseTrackMbid,
                "CUE metadata must not manufacture Release Track MBIDs");
    }

    const auto resolution = ExactTrackResolver::resolve(request, manifest);
    require(resolution.outcome == ExactTrackOutcome::Exact,
            "BB2 should remain the authority that selects the exact virtual occurrence");
    require(resolution.strength == ExactEvidenceStrength::StructuredMetadata,
            "CUE text/position should remain supporting structured evidence");
    require(resolution.selectedEntryId
                && resolution.selectedEntryId->value() == album.tracks[1].id.value(),
            "BB2 should select the requested virtual track occurrence");

    const auto bound = mustCue(
        CueWholeAlbum::resolveExactSlice(album, manifestContext, request),
        "an exact BB2 result should bind to a playable virtual slice");
    require(bound.recordingMbid == recording,
            "bound slice must retain the requested canonical Recording MBID");
    require(bound.releaseTrackMbid && *bound.releaseTrackMbid == releaseTrack,
            "bound slice must retain requested Release Track identity separately");
    require(bound.sourceId == sourceId,
            "bound slice must retain the physical source correlation separately");
    require(bound.virtualTrackId == album.tracks[1].id,
            "bound slice should keep lane-local virtual identity separately");
    require(bound.startMs == 10987 && bound.endMs && *bound.endMs == 60000,
            "bound slice should carry deterministic CUE time bounds");

    TrackTextEvidence wrongText = expectedText;
    wrongText.title = QStringLiteral("First Movement (Live)");
    const auto wrongRequest = mustBb2(
        RequestedTrackIdentity::create(
            recording,
            coordinate,
            wrongText),
        "wrong-title request should still be a valid BB2 request");
    const auto wrongResolution = ExactTrackResolver::resolve(wrongRequest, manifest);
    require(wrongResolution.outcome != ExactTrackOutcome::Exact,
            "BB3 must not bypass a BB2 exact-match failure");
    const auto rejectedBind = CueWholeAlbum::resolveExactSlice(
        album,
        manifestContext,
        wrongRequest);
    require(!rejectedBind.ok()
                && rejectedBind.error.code == CueWholeAlbumErrorCode::ExactResolutionRequired,
            "non-exact BB2 results must fail closed at the BB3 binding boundary");

    const auto otherSourceId = mustCue(
        WholeAlbumSourceId::parse(QStringLiteral("whole-source-99")),
        "second whole-album source ID should parse");
    const auto otherAlbum = mustCue(
        CueWholeAlbum::normalize(
            otherSourceId,
            CueInputOrigin::ExternalSidecar,
            fixtureCue(),
            120000),
        "second source should normalize independently");
    require(!(otherAlbum.tracks[1].id == album.tracks[1].id),
            "virtual track IDs must be namespaced by physical source identity");
    const auto otherManifest = mustCue(
        CueWholeAlbum::toBb2Manifest(otherAlbum, manifestContext),
        "second CUE album should bridge to BB2 independently");
    const auto otherResolution = ExactTrackResolver::resolve(request, otherManifest);
    require(otherResolution.outcome == ExactTrackOutcome::Exact
                && otherResolution.selectedEntryId
                && otherResolution.selectedEntryId->value()
                    == otherAlbum.tracks[1].id.value(),
            "BB2 should resolve the second source with its own virtual occurrence ID");

    require(cueWholeAlbumErrorCodeName(CueWholeAlbumErrorCode::InvalidCueFrame)
                == QStringLiteral("invalid_cue_frame"),
            "BB3 error codes should expose stable snake_case names");
    require(virtualTrackEndKindName(VirtualTrackEndKind::OpenEnded)
                == QStringLiteral("open_ended"),
            "BB3 end-boundary kinds should expose stable names");

    std::cout << "VINYL_BB3_CUE_WHOLE_ALBUM_OK checks="
              << g_checks << std::endl;
    return 0;
}
