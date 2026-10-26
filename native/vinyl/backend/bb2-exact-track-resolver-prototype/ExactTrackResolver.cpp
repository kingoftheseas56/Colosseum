#include "ExactTrackResolver.h"

#include <QChar>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cstdlib>

namespace vinyl {

namespace {

bool hasInvalidOptionalText(const std::optional<QString> &value)
{
    return value.has_value() && value->trimmed().isEmpty();
}

bool hasInvalidTextEvidence(const TrackTextEvidence &evidence)
{
    return hasInvalidOptionalText(evidence.title)
        || hasInvalidOptionalText(evidence.artist)
        || hasInvalidOptionalText(evidence.album)
        || hasInvalidOptionalText(evidence.filename);
}

bool containsControl(const QString &text)
{
    for (const QChar character : text) {
        const auto category = character.category();
        if (category == QChar::Other_Control
            || category == QChar::Other_Format
            || category == QChar::Other_Surrogate) {
            return true;
        }
    }
    return false;
}

bool positionsValid(const CandidateCanonicalEvidence &evidence)
{
    return (!evidence.mediumPosition || *evidence.mediumPosition > 0)
        && (!evidence.trackPosition || *evidence.trackPosition > 0);
}

void addReason(QVector<ExactTrackReasonCode> &reasons,
               ExactTrackReasonCode reason)
{
    if (!reasons.contains(reason))
        reasons.push_back(reason);
}

struct FilenameEvidence final {
    std::optional<QString> title;
    std::optional<int> mediumPosition;
    std::optional<int> trackPosition;
};

FilenameEvidence filenameEvidence(const QString &filename)
{
    FilenameEvidence evidence;
    const QString stem = QFileInfo(filename).completeBaseName().trimmed();
    if (stem.isEmpty())
        return evidence;

    static const QRegularExpression cdTrackPattern(
        QStringLiteral(R"(^\s*cd\s*(\d{1,2})\s+(\d{1,3})\s*[-._)]?\s*(.+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression discTrackPattern(
        QStringLiteral(R"(^\s*(\d{1,2})\s*[-.]\s*(\d{1,3})\s*[-._)]?\s*(.+)$)"));
    static const QRegularExpression trackPattern(
        QStringLiteral(R"(^\s*(\d{1,3})\s*[-._)]\s*(.+)$)"));

    auto match = cdTrackPattern.match(stem);
    if (match.hasMatch()) {
        evidence.mediumPosition = match.captured(1).toInt();
        evidence.trackPosition = match.captured(2).toInt();
        evidence.title = normalizeTrackText(match.captured(3));
        return evidence;
    }

    match = discTrackPattern.match(stem);
    if (match.hasMatch()) {
        evidence.mediumPosition = match.captured(1).toInt();
        evidence.trackPosition = match.captured(2).toInt();
        evidence.title = normalizeTrackText(match.captured(3));
        return evidence;
    }

    match = trackPattern.match(stem);
    if (match.hasMatch()) {
        evidence.trackPosition = match.captured(1).toInt();
        evidence.title = normalizeTrackText(match.captured(2));
        return evidence;
    }

    evidence.title = normalizeTrackText(stem);
    return evidence;
}

bool normalizedExact(const std::optional<QString> &expected,
                     const std::optional<QString> &candidate)
{
    return expected.has_value()
        && candidate.has_value()
        && *expected == *candidate;
}

ExactTrackCandidateAssessment assessCandidate(
    const RequestedTrackIdentity &request,
    const CandidateTrackEntry &entry,
    const ExactTrackResolverPolicy &policy)
{
    ExactTrackCandidateAssessment assessment{
        entry.id(),
        ExactTrackOutcome::InsufficientEvidence,
        ExactEvidenceStrength::None,
        {},
    };

    const auto &candidate = entry.canonicalEvidence();
    const auto &coordinate = request.releaseTrackCoordinate();
    bool conflict = false;

    if (candidate.recordingMbid) {
        if (*candidate.recordingMbid == request.recordingMbid()) {
            addReason(assessment.reasons,
                      ExactTrackReasonCode::RecordingMbidExact);
        } else {
            addReason(assessment.reasons,
                      ExactTrackReasonCode::RecordingMbidConflict);
            conflict = true;
        }
    }

    if (coordinate) {
        if (candidate.releaseTrackMbid && coordinate->releaseTrackMbid()) {
            if (*candidate.releaseTrackMbid == *coordinate->releaseTrackMbid()) {
                addReason(assessment.reasons,
                          ExactTrackReasonCode::ReleaseTrackMbidExact);
            } else {
                addReason(assessment.reasons,
                          ExactTrackReasonCode::ReleaseTrackMbidConflict);
                conflict = true;
            }
        }

        if (candidate.releaseGroupMbid) {
            if (*candidate.releaseGroupMbid == coordinate->releaseGroupMbid()) {
                addReason(assessment.reasons,
                          ExactTrackReasonCode::ReleaseGroupMbidExact);
            } else {
                addReason(assessment.reasons,
                          ExactTrackReasonCode::ReleaseGroupMbidConflict);
                conflict = true;
            }
        }

        if (candidate.releaseMbid) {
            if (*candidate.releaseMbid == coordinate->releaseMbid()) {
                addReason(assessment.reasons,
                          ExactTrackReasonCode::ReleaseMbidExact);
            } else {
                addReason(assessment.reasons,
                          ExactTrackReasonCode::ReleaseMbidConflict);
                conflict = true;
            }
        }

        if (candidate.mediumPosition) {
            if (*candidate.mediumPosition == coordinate->mediumPosition()) {
                addReason(assessment.reasons,
                          ExactTrackReasonCode::MediumPositionExact);
            } else {
                addReason(assessment.reasons,
                          ExactTrackReasonCode::MediumPositionConflict);
                conflict = true;
            }
        }

        if (candidate.trackPosition) {
            if (*candidate.trackPosition == coordinate->trackPosition()) {
                addReason(assessment.reasons,
                          ExactTrackReasonCode::TrackPositionExact);
            } else {
                addReason(assessment.reasons,
                          ExactTrackReasonCode::TrackPositionConflict);
                conflict = true;
            }
        }
    }

    if (conflict) {
        assessment.outcome = ExactTrackOutcome::Rejected;
        return assessment;
    }

    if (coordinate && coordinate->releaseTrackMbid()
        && candidate.releaseTrackMbid
        && *candidate.releaseTrackMbid == *coordinate->releaseTrackMbid()) {
        assessment.outcome = ExactTrackOutcome::Exact;
        assessment.strength = ExactEvidenceStrength::ReleaseTrackMbid;
        return assessment;
    }

    if (candidate.recordingMbid
        && *candidate.recordingMbid == request.recordingMbid()) {
        assessment.outcome = ExactTrackOutcome::Exact;
        assessment.strength = ExactEvidenceStrength::RecordingMbid;
        return assessment;
    }

    if (coordinate
        && candidate.releaseMbid
        && candidate.mediumPosition
        && candidate.trackPosition
        && *candidate.releaseMbid == coordinate->releaseMbid()
        && *candidate.mediumPosition == coordinate->mediumPosition()
        && *candidate.trackPosition == coordinate->trackPosition()) {
        addReason(assessment.reasons,
                  ExactTrackReasonCode::ReleaseCoordinateExact);
        assessment.outcome = ExactTrackOutcome::Exact;
        assessment.strength = ExactEvidenceStrength::ReleaseCoordinate;
        return assessment;
    }

    const auto &expectedText = request.normalizedExpectedText();
    const auto &candidateText = entry.normalizedTextEvidence();

    if (!expectedText.title || !expectedText.artist || !expectedText.album) {
        addReason(assessment.reasons,
                  ExactTrackReasonCode::StructuredEvidenceMissing);
        return assessment;
    }

    bool titleExact = false;
    if (normalizedExact(expectedText.title, candidateText.title)) {
        addReason(assessment.reasons,
                  ExactTrackReasonCode::TitleExact);
        titleExact = true;
    } else if (normalizedExact(expectedText.title,
                               candidateText.filenameTitle)) {
        addReason(assessment.reasons,
                  ExactTrackReasonCode::FilenameTitleExact);
        titleExact = true;
    } else if (candidateText.title || candidateText.filenameTitle) {
        addReason(assessment.reasons,
                  ExactTrackReasonCode::TitleMismatch);
    }

    if (!titleExact) {
        addReason(assessment.reasons,
                  ExactTrackReasonCode::StructuredEvidenceMissing);
        return assessment;
    }

    if (!normalizedExact(expectedText.artist, candidateText.artist)) {
        if (candidateText.artist) {
            addReason(assessment.reasons,
                      ExactTrackReasonCode::ArtistMismatch);
        }
        addReason(assessment.reasons,
                  ExactTrackReasonCode::StructuredEvidenceMissing);
        return assessment;
    }
    addReason(assessment.reasons, ExactTrackReasonCode::ArtistExact);

    if (!normalizedExact(expectedText.album, candidateText.album)) {
        if (candidateText.album) {
            addReason(assessment.reasons,
                      ExactTrackReasonCode::AlbumMismatch);
        }
        addReason(assessment.reasons,
                  ExactTrackReasonCode::StructuredEvidenceMissing);
        return assessment;
    }
    addReason(assessment.reasons, ExactTrackReasonCode::AlbumExact);

    if (!coordinate) {
        addReason(assessment.reasons,
                  ExactTrackReasonCode::StructuredEvidenceMissing);
        return assessment;
    }

    bool trackPositionExact =
        candidate.trackPosition
        && *candidate.trackPosition == coordinate->trackPosition();
    if (!candidate.trackPosition
        && candidateText.filenameTrackPosition) {
        if (*candidateText.filenameTrackPosition
            != coordinate->trackPosition()) {
            addReason(assessment.reasons,
                      ExactTrackReasonCode::FilenameTrackPositionMismatch);
            addReason(assessment.reasons,
                      ExactTrackReasonCode::StructuredEvidenceMissing);
            return assessment;
        }
        addReason(assessment.reasons,
                  ExactTrackReasonCode::FilenameTrackPositionExact);
        trackPositionExact = true;
    }

    if (!trackPositionExact) {
        addReason(assessment.reasons,
                  ExactTrackReasonCode::StructuredEvidenceMissing);
        return assessment;
    }

    if (!candidate.mediumPosition
        && candidateText.filenameMediumPosition) {
        if (*candidateText.filenameMediumPosition
            != coordinate->mediumPosition()) {
            addReason(assessment.reasons,
                      ExactTrackReasonCode::FilenameMediumPositionMismatch);
            addReason(assessment.reasons,
                      ExactTrackReasonCode::StructuredEvidenceMissing);
            return assessment;
        }
        addReason(assessment.reasons,
                  ExactTrackReasonCode::FilenameMediumPositionExact);
    } else if (coordinate->mediumPosition() > 1
               && !candidate.mediumPosition) {
        addReason(assessment.reasons,
                  ExactTrackReasonCode::StructuredEvidenceMissing);
        return assessment;
    }

    if (request.expectedDurationMs() && entry.measuredDurationMs()) {
        const qint64 difference =
            std::llabs(*request.expectedDurationMs()
                       - *entry.measuredDurationMs());
        const qint64 tolerance = std::max<qint64>(
            0,
            policy.durationToleranceMs);
        if (difference > tolerance) {
            addReason(assessment.reasons,
                      ExactTrackReasonCode::DurationMismatch);
            assessment.outcome = ExactTrackOutcome::Rejected;
            return assessment;
        }
        addReason(assessment.reasons,
                  ExactTrackReasonCode::DurationWithinTolerance);
    }

    addReason(assessment.reasons,
              ExactTrackReasonCode::StructuredMetadataExact);
    assessment.outcome = ExactTrackOutcome::Exact;
    assessment.strength = ExactEvidenceStrength::StructuredMetadata;
    return assessment;
}

} // namespace

QString exactTrackContractErrorCodeName(ExactTrackContractErrorCode code)
{
    switch (code) {
    case ExactTrackContractErrorCode::None:
        return QStringLiteral("none");
    case ExactTrackContractErrorCode::InvalidManifestEntryId:
        return QStringLiteral("invalid_manifest_entry_id");
    case ExactTrackContractErrorCode::InvalidTextEvidence:
        return QStringLiteral("invalid_text_evidence");
    case ExactTrackContractErrorCode::InvalidDuration:
        return QStringLiteral("invalid_duration");
    case ExactTrackContractErrorCode::InvalidCandidateCoordinate:
        return QStringLiteral("invalid_candidate_coordinate");
    case ExactTrackContractErrorCode::InvalidRequestCoordinate:
        return QStringLiteral("invalid_request_coordinate");
    case ExactTrackContractErrorCode::DuplicateManifestEntryId:
        return QStringLiteral("duplicate_manifest_entry_id");
    }
    return QStringLiteral("unknown");
}

ExactTrackContractResult<ManifestEntryId> ManifestEntryId::parse(
    const QString &text)
{
    if (text.isEmpty()
        || text.size() > 1024
        || text.trimmed() != text
        || containsControl(text)) {
        return ExactTrackContractResult<ManifestEntryId>::failure(
            ExactTrackContractErrorCode::InvalidManifestEntryId,
            QStringLiteral("manifestEntryId"),
            QStringLiteral("Manifest entry identity must be non-empty, bounded, trimmed, and free of control characters."));
    }

    return ExactTrackContractResult<ManifestEntryId>::success(
        ManifestEntryId(text));
}

QString normalizeTrackText(const QString &text)
{
    const QString folded = text.normalized(QString::NormalizationForm_KC)
                               .toCaseFolded()
                               .trimmed();

    QString output;
    output.reserve(folded.size());
    bool previousWasSpace = false;

    for (const QChar character : folded) {
        const bool keep = character.isLetterOrNumber();
        if (keep) {
            output.append(character);
            previousWasSpace = false;
            continue;
        }

        if (!previousWasSpace && !output.isEmpty()) {
            output.append(QChar(u' '));
            previousWasSpace = true;
        }
    }

    return output.trimmed();
}

NormalizedTextEvidence normalizeTextEvidence(
    const TrackTextEvidence &evidence)
{
    NormalizedTextEvidence normalized;

    if (evidence.title)
        normalized.title = normalizeTrackText(*evidence.title);
    if (evidence.artist)
        normalized.artist = normalizeTrackText(*evidence.artist);
    if (evidence.album)
        normalized.album = normalizeTrackText(*evidence.album);
    if (evidence.filename) {
        const auto filename = filenameEvidence(*evidence.filename);
        normalized.filenameTitle = filename.title;
        normalized.filenameMediumPosition = filename.mediumPosition;
        normalized.filenameTrackPosition = filename.trackPosition;
    }

    return normalized;
}

ExactTrackContractResult<RequestedTrackIdentity> RequestedTrackIdentity::create(
    RecordingMbid recordingMbid,
    std::optional<ReleaseTrackCoordinate> releaseTrackCoordinate,
    TrackTextEvidence expectedText,
    std::optional<qint64> expectedDurationMs)
{
    if (releaseTrackCoordinate
        && releaseTrackCoordinate->recordingMbid() != recordingMbid) {
        return ExactTrackContractResult<RequestedTrackIdentity>::failure(
            ExactTrackContractErrorCode::InvalidRequestCoordinate,
            QStringLiteral("releaseTrackCoordinate.recordingMbid"),
            QStringLiteral("Release-track context must retain the requested Recording MBID."));
    }

    if (hasInvalidTextEvidence(expectedText)) {
        return ExactTrackContractResult<RequestedTrackIdentity>::failure(
            ExactTrackContractErrorCode::InvalidTextEvidence,
            QStringLiteral("expectedText"),
            QStringLiteral("Known expected text evidence must not be blank."));
    }

    if (expectedDurationMs && *expectedDurationMs <= 0) {
        return ExactTrackContractResult<RequestedTrackIdentity>::failure(
            ExactTrackContractErrorCode::InvalidDuration,
            QStringLiteral("expectedDurationMs"),
            QStringLiteral("Known expected duration must be positive."));
    }

    auto normalized = normalizeTextEvidence(expectedText);

    return ExactTrackContractResult<RequestedTrackIdentity>::success(
        RequestedTrackIdentity(
            std::move(recordingMbid),
            std::move(releaseTrackCoordinate),
            std::move(expectedText),
            std::move(normalized),
            expectedDurationMs));
}

ExactTrackContractResult<CandidateTrackEntry> CandidateTrackEntry::create(
    ManifestEntryId id,
    CandidateCanonicalEvidence canonicalEvidence,
    TrackTextEvidence textEvidence,
    std::optional<qint64> measuredDurationMs)
{
    if (!positionsValid(canonicalEvidence)) {
        return ExactTrackContractResult<CandidateTrackEntry>::failure(
            ExactTrackContractErrorCode::InvalidCandidateCoordinate,
            QStringLiteral("canonicalEvidence"),
            QStringLiteral("Known medium and track positions must be positive and one-based."));
    }

    if (hasInvalidTextEvidence(textEvidence)) {
        return ExactTrackContractResult<CandidateTrackEntry>::failure(
            ExactTrackContractErrorCode::InvalidTextEvidence,
            QStringLiteral("textEvidence"),
            QStringLiteral("Known candidate text evidence must not be blank."));
    }

    if (measuredDurationMs && *measuredDurationMs <= 0) {
        return ExactTrackContractResult<CandidateTrackEntry>::failure(
            ExactTrackContractErrorCode::InvalidDuration,
            QStringLiteral("measuredDurationMs"),
            QStringLiteral("Known measured duration must be positive."));
    }

    auto normalized = normalizeTextEvidence(textEvidence);

    return ExactTrackContractResult<CandidateTrackEntry>::success(
        CandidateTrackEntry(
            std::move(id),
            std::move(canonicalEvidence),
            std::move(textEvidence),
            std::move(normalized),
            measuredDurationMs));
}

ExactTrackContractResult<AlbumSourceManifest> AlbumSourceManifest::create(
    std::optional<SourceCandidateKey> sourceCandidateKey,
    QVector<CandidateTrackEntry> entries)
{
    std::sort(
        entries.begin(),
        entries.end(),
        [](const CandidateTrackEntry &left,
           const CandidateTrackEntry &right) {
            return left.id().value() < right.id().value();
        });

    for (qsizetype index = 1; index < entries.size(); ++index) {
        if (entries.at(index - 1).id() == entries.at(index).id()) {
            return ExactTrackContractResult<AlbumSourceManifest>::failure(
                ExactTrackContractErrorCode::DuplicateManifestEntryId,
                QStringLiteral("entries"),
                QStringLiteral("Manifest entry identity must be unique inside one manifest."));
        }
    }

    return ExactTrackContractResult<AlbumSourceManifest>::success(
        AlbumSourceManifest(
            std::move(sourceCandidateKey),
            std::move(entries)));
}

QString exactTrackOutcomeName(ExactTrackOutcome outcome)
{
    switch (outcome) {
    case ExactTrackOutcome::Exact:
        return QStringLiteral("exact");
    case ExactTrackOutcome::Rejected:
        return QStringLiteral("rejected");
    case ExactTrackOutcome::Ambiguous:
        return QStringLiteral("ambiguous");
    case ExactTrackOutcome::InsufficientEvidence:
        return QStringLiteral("insufficient_evidence");
    }
    return QStringLiteral("unknown");
}

QString exactEvidenceStrengthName(ExactEvidenceStrength strength)
{
    switch (strength) {
    case ExactEvidenceStrength::None:
        return QStringLiteral("none");
    case ExactEvidenceStrength::StructuredMetadata:
        return QStringLiteral("structured_metadata");
    case ExactEvidenceStrength::ReleaseCoordinate:
        return QStringLiteral("release_coordinate");
    case ExactEvidenceStrength::RecordingMbid:
        return QStringLiteral("recording_mbid");
    case ExactEvidenceStrength::ReleaseTrackMbid:
        return QStringLiteral("release_track_mbid");
    }
    return QStringLiteral("unknown");
}

QString exactTrackReasonCodeName(ExactTrackReasonCode reason)
{
    switch (reason) {
    case ExactTrackReasonCode::RecordingMbidExact:
        return QStringLiteral("recording_mbid_exact");
    case ExactTrackReasonCode::RecordingMbidConflict:
        return QStringLiteral("recording_mbid_conflict");
    case ExactTrackReasonCode::ReleaseTrackMbidExact:
        return QStringLiteral("release_track_mbid_exact");
    case ExactTrackReasonCode::ReleaseTrackMbidConflict:
        return QStringLiteral("release_track_mbid_conflict");
    case ExactTrackReasonCode::ReleaseGroupMbidExact:
        return QStringLiteral("release_group_mbid_exact");
    case ExactTrackReasonCode::ReleaseGroupMbidConflict:
        return QStringLiteral("release_group_mbid_conflict");
    case ExactTrackReasonCode::ReleaseMbidExact:
        return QStringLiteral("release_mbid_exact");
    case ExactTrackReasonCode::ReleaseMbidConflict:
        return QStringLiteral("release_mbid_conflict");
    case ExactTrackReasonCode::MediumPositionExact:
        return QStringLiteral("medium_position_exact");
    case ExactTrackReasonCode::MediumPositionConflict:
        return QStringLiteral("medium_position_conflict");
    case ExactTrackReasonCode::TrackPositionExact:
        return QStringLiteral("track_position_exact");
    case ExactTrackReasonCode::TrackPositionConflict:
        return QStringLiteral("track_position_conflict");
    case ExactTrackReasonCode::ReleaseCoordinateExact:
        return QStringLiteral("release_coordinate_exact");
    case ExactTrackReasonCode::TitleExact:
        return QStringLiteral("title_exact");
    case ExactTrackReasonCode::TitleMismatch:
        return QStringLiteral("title_mismatch");
    case ExactTrackReasonCode::ArtistExact:
        return QStringLiteral("artist_exact");
    case ExactTrackReasonCode::ArtistMismatch:
        return QStringLiteral("artist_mismatch");
    case ExactTrackReasonCode::AlbumExact:
        return QStringLiteral("album_exact");
    case ExactTrackReasonCode::AlbumMismatch:
        return QStringLiteral("album_mismatch");
    case ExactTrackReasonCode::FilenameTitleExact:
        return QStringLiteral("filename_title_exact");
    case ExactTrackReasonCode::FilenameMediumPositionExact:
        return QStringLiteral("filename_medium_position_exact");
    case ExactTrackReasonCode::FilenameMediumPositionMismatch:
        return QStringLiteral("filename_medium_position_mismatch");
    case ExactTrackReasonCode::FilenameTrackPositionExact:
        return QStringLiteral("filename_track_position_exact");
    case ExactTrackReasonCode::FilenameTrackPositionMismatch:
        return QStringLiteral("filename_track_position_mismatch");
    case ExactTrackReasonCode::DurationWithinTolerance:
        return QStringLiteral("duration_within_tolerance");
    case ExactTrackReasonCode::DurationMismatch:
        return QStringLiteral("duration_mismatch");
    case ExactTrackReasonCode::StructuredMetadataExact:
        return QStringLiteral("structured_metadata_exact");
    case ExactTrackReasonCode::StructuredEvidenceMissing:
        return QStringLiteral("structured_evidence_missing");
    case ExactTrackReasonCode::CanonicalEvidenceMissing:
        return QStringLiteral("canonical_evidence_missing");
    case ExactTrackReasonCode::EqualStrongestCandidates:
        return QStringLiteral("equal_strongest_candidates");
    case ExactTrackReasonCode::EmptyManifest:
        return QStringLiteral("empty_manifest");
    case ExactTrackReasonCode::NoCandidateReachedExactGate:
        return QStringLiteral("no_candidate_reached_exact_gate");
    }
    return QStringLiteral("unknown");
}

ExactTrackResolution ExactTrackResolver::resolve(
    const RequestedTrackIdentity &request,
    const AlbumSourceManifest &manifest,
    const ExactTrackResolverPolicy &policy)
{
    ExactTrackResolution result;
    result.assessments.reserve(manifest.entries().size());

    if (manifest.entries().isEmpty()) {
        result.reasons.push_back(ExactTrackReasonCode::EmptyManifest);
        return result;
    }

    for (const auto &entry : manifest.entries())
        result.assessments.push_back(
            assessCandidate(request, entry, policy));

    ExactEvidenceStrength strongest = ExactEvidenceStrength::None;
    for (const auto &assessment : result.assessments) {
        if (assessment.outcome != ExactTrackOutcome::Exact)
            continue;
        if (static_cast<int>(assessment.strength)
            > static_cast<int>(strongest)) {
            strongest = assessment.strength;
        }
    }

    QVector<ManifestEntryId> strongestEntries;
    for (const auto &assessment : result.assessments) {
        if (assessment.outcome == ExactTrackOutcome::Exact
            && assessment.strength == strongest) {
            strongestEntries.push_back(assessment.entryId);
        }
    }

    if (strongest != ExactEvidenceStrength::None
        && strongestEntries.size() == 1) {
        result.outcome = ExactTrackOutcome::Exact;
        result.selectedEntryId = strongestEntries.front();
        result.strength = strongest;
        return result;
    }

    if (strongest != ExactEvidenceStrength::None
        && strongestEntries.size() > 1) {
        result.outcome = ExactTrackOutcome::Ambiguous;
        result.strength = strongest;
        result.competingEntryIds = std::move(strongestEntries);
        result.reasons.push_back(
            ExactTrackReasonCode::EqualStrongestCandidates);
        return result;
    }

    const bool anyInsufficient = std::any_of(
        result.assessments.cbegin(),
        result.assessments.cend(),
        [](const ExactTrackCandidateAssessment &assessment) {
            return assessment.outcome
                == ExactTrackOutcome::InsufficientEvidence;
        });

    if (!anyInsufficient) {
        result.outcome = ExactTrackOutcome::Rejected;
        return result;
    }

    result.reasons.push_back(
        ExactTrackReasonCode::NoCandidateReachedExactGate);
    return result;
}

} // namespace vinyl
