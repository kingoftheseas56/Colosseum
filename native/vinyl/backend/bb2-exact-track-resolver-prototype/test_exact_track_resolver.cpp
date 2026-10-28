#include "ExactTrackResolver.h"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include <cstdlib>
#include <type_traits>
#include <utility>

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
T mustBb2(const vinyl::ExactTrackContractResult<T> &result,
          const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T, typename = void>
struct HasResolvedUrl : std::false_type {
};

template <typename T>
struct HasResolvedUrl<T, std::void_t<decltype(std::declval<T>().resolvedUrl())>>
    : std::true_type {
};

vinyl::CandidateTrackEntry makeEntry(
    const QString &id,
    vinyl::CandidateCanonicalEvidence canonical = {},
    vinyl::TrackTextEvidence text = {},
    std::optional<qint64> durationMs = std::nullopt)
{
    return mustBb2(
        vinyl::CandidateTrackEntry::create(
            mustBb2(vinyl::ManifestEntryId::parse(id),
                    "manifest entry id should parse"),
            std::move(canonical),
            std::move(text),
            durationMs),
        "candidate track entry should be valid");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    static_assert(!std::is_same<RecordingMbid, ReleaseTrackMbid>::value,
                  "Recording and Release Track identity must remain distinct");
    static_assert(!std::is_convertible<SourceCandidateId, RecordingMbid>::value,
                  "BB1 candidate identity must never become canonical Recording identity");
    static_assert(!HasResolvedUrl<ExactTrackResolution>::value,
                  "BB2 must not require or own a resolved playback URL");

    const auto recording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "recording MBID should parse");
    const auto otherRecording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "other recording MBID should parse");
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
        "release-track coordinate should be valid");

    TrackTextEvidence expectedText;
    expectedText.title = QStringLiteral("  The   Song  ");
    expectedText.artist = QStringLiteral("THE ARTIST");
    expectedText.album = QStringLiteral("Album: Name");

    const auto request = mustBb2(
        RequestedTrackIdentity::create(
            recording,
            coordinate,
            expectedText,
            201000),
        "requested track identity should be valid");

    require(request.recordingMbid() == recording,
            "request must retain canonical Recording MBID");
    require(request.normalizedExpectedText().title
                == std::optional<QString>{QStringLiteral("the song")},
            "title evidence should normalize deterministically");
    require(request.normalizedExpectedText().artist
                == std::optional<QString>{QStringLiteral("the artist")},
            "artist evidence should normalize deterministically");
    require(request.normalizedExpectedText().album
                == std::optional<QString>{QStringLiteral("album name")},
            "album evidence should normalize deterministically");

    const ExactTrackResolverPolicy defaultPolicy;
    require(defaultPolicy.durationToleranceMs == 2000,
            "BB2 should freeze the initial duration neutral band at 2000 ms");

    CandidateCanonicalEvidence coordinateOnly;
    coordinateOnly.releaseMbid = release;
    coordinateOnly.mediumPosition = 1;
    coordinateOnly.trackPosition = 2;

    CandidateCanonicalEvidence recordingExact;
    recordingExact.recordingMbid = recording;

    CandidateCanonicalEvidence releaseTrackExact;
    releaseTrackExact.releaseTrackMbid = releaseTrack;

    const auto coordinateEntry = makeEntry(
        QStringLiteral("coordinate-only"),
        coordinateOnly);
    const auto recordingEntry = makeEntry(
        QStringLiteral("recording-exact"),
        recordingExact);
    const auto releaseTrackEntry = makeEntry(
        QStringLiteral("release-track-exact"),
        releaseTrackExact);

    const auto forwardCanonicalManifest = mustBb2(
        AlbumSourceManifest::create(
            std::nullopt,
            QVector<CandidateTrackEntry>{
                coordinateEntry,
                recordingEntry,
                releaseTrackEntry,
            }),
        "forward canonical manifest should be valid");
    const auto reverseCanonicalManifest = mustBb2(
        AlbumSourceManifest::create(
            std::nullopt,
            QVector<CandidateTrackEntry>{
                releaseTrackEntry,
                recordingEntry,
                coordinateEntry,
            }),
        "reverse canonical manifest should be valid");

    const auto forwardCanonical = ExactTrackResolver::resolve(
        request,
        forwardCanonicalManifest);
    const auto reverseCanonical = ExactTrackResolver::resolve(
        request,
        reverseCanonicalManifest);

    require(forwardCanonical.outcome == ExactTrackOutcome::Exact,
            "exact canonical evidence should resolve");
    require(forwardCanonical.strength
                == ExactEvidenceStrength::ReleaseTrackMbid,
            "release-track MBID should outrank recording and coordinate evidence");
    require(forwardCanonical.selectedEntryId.has_value()
                && forwardCanonical.selectedEntryId->value()
                    == QStringLiteral("release-track-exact"),
            "strongest canonical candidate should be selected");
    require(reverseCanonical.selectedEntryId.has_value()
                && reverseCanonical.selectedEntryId->value()
                    == forwardCanonical.selectedEntryId->value(),
            "manifest ordering must not change canonical exact resolution");

    const auto coordinateManifest = mustBb2(
        AlbumSourceManifest::create(
            std::nullopt,
            QVector<CandidateTrackEntry>{coordinateEntry}),
        "coordinate manifest should be valid");
    const auto coordinateResolution = ExactTrackResolver::resolve(
        request,
        coordinateManifest);

    require(coordinateResolution.outcome == ExactTrackOutcome::Exact,
            "exact release/medium/track coordinates should resolve");
    require(coordinateResolution.strength
                == ExactEvidenceStrength::ReleaseCoordinate,
            "coordinate-only resolution should expose release-coordinate strength");
    require(coordinateResolution.selectedEntryId.has_value()
                && coordinateResolution.selectedEntryId->value()
                    == QStringLiteral("coordinate-only"),
            "exact coordinate should select the expected occurrence");

    const QString sameUuid =
        QStringLiteral("66666666-6666-4666-8666-666666666666");
    const auto sameRawRecording = mustDomain(
        RecordingMbid::parse(sameUuid),
        "same-raw Recording MBID should parse");
    const auto sameRawReleaseTrack = mustDomain(
        ReleaseTrackMbid::parse(sameUuid),
        "same-raw Release Track MBID should parse");
    require(sameRawRecording.toString() == sameRawReleaseTrack.toString(),
            "fixture should prove raw UUID spelling can be identical");
    require(!std::is_same<decltype(sameRawRecording),
                          decltype(sameRawReleaseTrack)>::value,
            "strong identity types must remain different even with same raw UUID");

    SourceProviderCapabilities capabilities;
    capabilities |= SourceProviderCapability::DiscoverCandidates;
    capabilities |= SourceProviderCapability::ResolveExactSource;
    capabilities |= SourceProviderCapability::HttpSources;

    const auto provider = mustSource(
        SourceProviderDescriptor::create(
            mustSource(
                SourceProviderId::parse(QStringLiteral("source.example")),
                "source provider id should parse"),
            capabilities),
        "source provider descriptor should be valid");
    const auto mbidLookingCandidateId = mustSource(
        SourceCandidateId::parse(otherRecording.toString()),
        "MBID-looking BB1 candidate id should remain valid opaque identity");
    const SourceCandidateKey sourceKey(
        provider.id(),
        mbidLookingCandidateId);

    const auto provenanceManifest = mustBb2(
        AlbumSourceManifest::create(
            sourceKey,
            QVector<CandidateTrackEntry>{recordingEntry}),
        "manifest with BB1 provenance should be valid");
    const auto provenanceResolution = ExactTrackResolver::resolve(
        request,
        provenanceManifest);

    require(provenanceManifest.sourceCandidateKey().has_value(),
            "BB1 source candidate provenance should be retained");
    require(provenanceManifest.sourceCandidateKey()->candidateId().value()
                == otherRecording.toString(),
            "MBID-looking candidate ID must remain opaque BB1 identity");
    require(provenanceResolution.outcome == ExactTrackOutcome::Exact
                && provenanceResolution.selectedEntryId.has_value()
                && provenanceResolution.selectedEntryId->value()
                    == QStringLiteral("recording-exact"),
            "BB1 candidate identity must not replace requested canonical identity");

    CandidateCanonicalEvidence canonicalConflict;
    canonicalConflict.recordingMbid = otherRecording;
    canonicalConflict.releaseGroupMbid = releaseGroup;
    canonicalConflict.releaseMbid = release;
    canonicalConflict.mediumPosition = 1;
    canonicalConflict.trackPosition = 2;

    TrackTextEvidence temptingText;
    temptingText.title = QStringLiteral("The Song");
    temptingText.artist = QStringLiteral("The Artist");
    temptingText.album = QStringLiteral("Album Name");

    const auto conflictManifest = mustBb2(
        AlbumSourceManifest::create(
            std::nullopt,
            QVector<CandidateTrackEntry>{
                makeEntry(
                    QStringLiteral("tempting-conflict"),
                    canonicalConflict,
                    temptingText,
                    201000),
            }),
        "conflict manifest should be valid");
    const auto conflictResolution = ExactTrackResolver::resolve(
        request,
        conflictManifest);

    require(conflictResolution.outcome == ExactTrackOutcome::Rejected,
            "canonical Recording mismatch must reject tempting supporting evidence");
    require(conflictResolution.assessments.size() == 1
                && conflictResolution.assessments.front().reasons.contains(
                    ExactTrackReasonCode::RecordingMbidConflict),
            "canonical rejection must expose a machine-readable reason");

    const auto emptyManifest = mustBb2(
        AlbumSourceManifest::create(std::nullopt, {}),
        "empty manifest should be representable");
    const auto emptyResolution = ExactTrackResolver::resolve(
        request,
        emptyManifest);
    require(emptyResolution.outcome
                == ExactTrackOutcome::InsufficientEvidence,
            "empty manifest must fail closed as insufficient evidence");
    require(emptyResolution.reasons.contains(
                ExactTrackReasonCode::EmptyManifest),
            "empty manifest should retain an explicit reason");

    const auto weakManifest = mustBb2(
        AlbumSourceManifest::create(
            std::nullopt,
            QVector<CandidateTrackEntry>{
                makeEntry(QStringLiteral("weak")),
            }),
        "weak manifest should be valid");
    const auto weakResolution = ExactTrackResolver::resolve(
        request,
        weakManifest);
    require(weakResolution.outcome
                == ExactTrackOutcome::InsufficientEvidence,
            "weak evidence must not be guessed into an exact match");

    TrackTextEvidence messyStructuredText;
    messyStructuredText.artist = QStringLiteral("  The   Artist ");
    messyStructuredText.album = QStringLiteral("Album---Name");
    messyStructuredText.filename = QStringLiteral("02 - THE_song!!.flac");

    const auto messyStructuredEntry = makeEntry(
        QStringLiteral("structured-messy"),
        {},
        messyStructuredText,
        202500);
    const auto messyStructuredManifest = mustBb2(
        AlbumSourceManifest::create(
            std::nullopt,
            QVector<CandidateTrackEntry>{messyStructuredEntry}),
        "messy structured manifest should be valid");
    const auto messyStructuredResolution = ExactTrackResolver::resolve(
        request,
        messyStructuredManifest);

    require(messyStructuredResolution.outcome == ExactTrackOutcome::Exact,
            "messy but sufficient filename and metadata evidence should resolve exactly");
    require(messyStructuredResolution.strength
                == ExactEvidenceStrength::StructuredMetadata,
            "messy structured evidence should resolve at structured strength");
    require(messyStructuredResolution.selectedEntryId.has_value()
                && messyStructuredResolution.selectedEntryId->value()
                    == QStringLiteral("structured-messy"),
            "structured evidence should select the expected manifest occurrence");
    require(messyStructuredResolution.assessments.front().reasons.contains(
                ExactTrackReasonCode::FilenameTitleExact),
            "filename-derived exact title evidence should be explicit");
    require(messyStructuredResolution.assessments.front().reasons.contains(
                ExactTrackReasonCode::FilenameTrackPositionExact),
            "filename-derived track position should be explicit");
    require(messyStructuredResolution.assessments.front().reasons.contains(
                ExactTrackReasonCode::DurationWithinTolerance),
            "duration inside the neutral band should be recorded as supporting evidence");

    const auto structuredTieManifest = mustBb2(
        AlbumSourceManifest::create(
            std::nullopt,
            QVector<CandidateTrackEntry>{
                makeEntry(
                    QStringLiteral("structured-a"),
                    {},
                    messyStructuredText,
                    200500),
                makeEntry(
                    QStringLiteral("structured-b"),
                    {},
                    messyStructuredText,
                    201500),
            }),
        "structured tie manifest should be valid");
    const auto structuredTieResolution = ExactTrackResolver::resolve(
        request,
        structuredTieManifest);

    require(structuredTieResolution.outcome == ExactTrackOutcome::Ambiguous,
            "two equally plausible structured tracks must remain ambiguous");
    require(structuredTieResolution.strength
                == ExactEvidenceStrength::StructuredMetadata,
            "structured ambiguity should retain the common evidence strength");
    require(structuredTieResolution.competingEntryIds.size() == 2,
            "structured ambiguity should retain both competing track occurrences");

    const auto durationMismatchManifest = mustBb2(
        AlbumSourceManifest::create(
            std::nullopt,
            QVector<CandidateTrackEntry>{
                makeEntry(
                    QStringLiteral("duration-mismatch"),
                    {},
                    messyStructuredText,
                    205001),
            }),
        "duration mismatch manifest should be valid");
    const auto durationMismatchResolution = ExactTrackResolver::resolve(
        request,
        durationMismatchManifest);

    require(durationMismatchResolution.outcome == ExactTrackOutcome::Rejected,
            "duration mismatch must reject an otherwise tempting structured title match");
    require(durationMismatchResolution.assessments.front().reasons.contains(
                ExactTrackReasonCode::DurationMismatch),
            "duration rejection should expose an explicit reason code");

    TrackTextEvidence wrongDiscText;
    wrongDiscText.artist = QStringLiteral("The Artist");
    wrongDiscText.album = QStringLiteral("Album Name");
    wrongDiscText.filename = QStringLiteral("CD2 02 - The Song.flac");

    const auto wrongDiscManifest = mustBb2(
        AlbumSourceManifest::create(
            std::nullopt,
            QVector<CandidateTrackEntry>{
                makeEntry(
                    QStringLiteral("wrong-disc-filename"),
                    {},
                    wrongDiscText,
                    201000),
            }),
        "wrong-disc filename manifest should be valid");
    const auto wrongDiscResolution = ExactTrackResolver::resolve(
        request,
        wrongDiscManifest);

    require(wrongDiscResolution.outcome
                == ExactTrackOutcome::InsufficientEvidence,
            "explicit wrong-disc filename evidence must fail closed");
    require(wrongDiscResolution.assessments.front().reasons.contains(
                ExactTrackReasonCode::FilenameMediumPositionMismatch),
            "wrong-disc filename should expose a typed mismatch reason");

    const auto providerB = mustSource(
        SourceProviderDescriptor::create(
            mustSource(
                SourceProviderId::parse(QStringLiteral("source.second")),
                "source provider B id should parse"),
            capabilities),
        "source provider B descriptor should be valid");
    const SourceCandidateKey sourceKeyB(
        providerB.id(),
        mustSource(
            SourceCandidateId::parse(QStringLiteral("second-candidate")),
            "provider B candidate id should parse"));

    const auto providerStructuredManifest = mustBb2(
        AlbumSourceManifest::create(
            sourceKey,
            QVector<CandidateTrackEntry>{messyStructuredEntry}),
        "provider structured manifest should be valid");
    const auto providerWeakManifest = mustBb2(
        AlbumSourceManifest::create(
            sourceKeyB,
            QVector<CandidateTrackEntry>{
                makeEntry(QStringLiteral("provider-weak")),
            }),
        "provider weak manifest should be valid");

    const auto summarizeProviderResults =
        [&](QVector<AlbumSourceManifest> manifests) {
            QStringList summaries;
            for (const auto &manifest : manifests) {
                require(manifest.sourceCandidateKey().has_value(),
                        "provider-order fixture must retain source candidate provenance");
                const auto resolution = ExactTrackResolver::resolve(
                    request,
                    manifest);
                const QString selected = resolution.selectedEntryId
                    ? resolution.selectedEntryId->value()
                    : QStringLiteral("-");
                summaries.push_back(
                    QStringLiteral("%1|%2|%3")
                        .arg(
                            manifest.sourceCandidateKey()->stableKey(),
                            exactTrackOutcomeName(resolution.outcome),
                            selected));
            }
            summaries.sort();
            return summaries;
        };

    const auto providerForward = summarizeProviderResults(
        QVector<AlbumSourceManifest>{
            providerStructuredManifest,
            providerWeakManifest,
        });
    const auto providerReverse = summarizeProviderResults(
        QVector<AlbumSourceManifest>{
            providerWeakManifest,
            providerStructuredManifest,
        });

    require(providerForward == providerReverse,
            "provider candidate ordering must not change any BB2 decision");
    require(providerForward.size() == 2,
            "provider-order fixture should preserve both provider-scoped decisions");

    const auto canonicalTieManifest = mustBb2(
        AlbumSourceManifest::create(
            std::nullopt,
            QVector<CandidateTrackEntry>{
                makeEntry(QStringLiteral("canonical-a"), recordingExact),
                makeEntry(QStringLiteral("canonical-b"), recordingExact),
            }),
        "canonical tie manifest should be valid");
    const auto canonicalTieResolution = ExactTrackResolver::resolve(
        request,
        canonicalTieManifest);

    require(canonicalTieResolution.outcome == ExactTrackOutcome::Ambiguous,
            "equal strongest canonical candidates must remain ambiguous");
    require(canonicalTieResolution.competingEntryIds.size() == 2,
            "ambiguous result should retain both competing occurrences");
    require(canonicalTieResolution.reasons.contains(
                ExactTrackReasonCode::EqualStrongestCandidates),
            "ambiguity should expose an explicit reason code");

    require(exactTrackOutcomeName(ExactTrackOutcome::InsufficientEvidence)
                == QStringLiteral("insufficient_evidence"),
            "outcomes should expose stable snake_case names");
    require(exactEvidenceStrengthName(
                ExactEvidenceStrength::ReleaseTrackMbid)
                == QStringLiteral("release_track_mbid"),
            "evidence strengths should expose stable names");
    require(exactTrackReasonCodeName(
                ExactTrackReasonCode::ReleaseCoordinateExact)
                == QStringLiteral("release_coordinate_exact"),
            "reason codes should expose stable names");
    require(exactTrackReasonCodeName(
                ExactTrackReasonCode::StructuredMetadataExact)
                == QStringLiteral("structured_metadata_exact"),
            "structured evidence reasons should expose stable names");
    require(exactTrackReasonCodeName(
                ExactTrackReasonCode::DurationMismatch)
                == QStringLiteral("duration_mismatch"),
            "duration rejection reasons should expose stable names");

    qInfo().noquote()
        << "VINYL_BB2_EXACT_TRACK_RESOLVER_OK checks=" << g_checks;
    return 0;
}
