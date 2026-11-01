#pragma once

#include "SourceCandidateContract.h"
#include "VinylDomainTypes.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class ExactTrackContractErrorCode {
    None,
    InvalidManifestEntryId,
    InvalidTextEvidence,
    InvalidDuration,
    InvalidCandidateCoordinate,
    InvalidRequestCoordinate,
    DuplicateManifestEntryId,
};

struct ExactTrackContractError final {
    ExactTrackContractErrorCode code = ExactTrackContractErrorCode::None;
    QString field;
    QString detail;
};

QString exactTrackContractErrorCodeName(ExactTrackContractErrorCode code);

template <typename T>
struct ExactTrackContractResult final {
    std::optional<T> value;
    ExactTrackContractError error;

    bool ok() const noexcept { return value.has_value(); }

    static ExactTrackContractResult success(T value)
    {
        ExactTrackContractResult out;
        out.value = std::move(value);
        return out;
    }

    static ExactTrackContractResult failure(ExactTrackContractErrorCode code,
                                            QString field,
                                            QString detail = QString())
    {
        ExactTrackContractResult out;
        out.error = ExactTrackContractError{
            code,
            std::move(field),
            std::move(detail),
        };
        return out;
    }
};

class ManifestEntryId final
{
public:
    static ExactTrackContractResult<ManifestEntryId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const ManifestEntryId &other) const noexcept
    {
        return m_value == other.m_value;
    }

    bool operator!=(const ManifestEntryId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit ManifestEntryId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

struct TrackTextEvidence final {
    std::optional<QString> title;
    std::optional<QString> artist;
    std::optional<QString> album;
    std::optional<QString> filename;
};

struct NormalizedTextEvidence final {
    std::optional<QString> title;
    std::optional<QString> artist;
    std::optional<QString> album;
    std::optional<QString> filenameTitle;
    std::optional<int> filenameMediumPosition;
    std::optional<int> filenameTrackPosition;
};

QString normalizeTrackText(const QString &text);
NormalizedTextEvidence normalizeTextEvidence(const TrackTextEvidence &evidence);

struct CandidateCanonicalEvidence final {
    std::optional<RecordingMbid> recordingMbid;
    std::optional<ReleaseTrackMbid> releaseTrackMbid;
    std::optional<ReleaseGroupMbid> releaseGroupMbid;
    std::optional<ReleaseMbid> releaseMbid;
    std::optional<int> mediumPosition;
    std::optional<int> trackPosition;
};

class RequestedTrackIdentity final
{
public:
    static ExactTrackContractResult<RequestedTrackIdentity> create(
        RecordingMbid recordingMbid,
        std::optional<ReleaseTrackCoordinate> releaseTrackCoordinate,
        TrackTextEvidence expectedText = {},
        std::optional<qint64> expectedDurationMs = std::nullopt);

    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const std::optional<ReleaseTrackCoordinate> &releaseTrackCoordinate() const noexcept
    {
        return m_releaseTrackCoordinate;
    }
    const TrackTextEvidence &expectedText() const noexcept { return m_expectedText; }
    const NormalizedTextEvidence &normalizedExpectedText() const noexcept
    {
        return m_normalizedExpectedText;
    }
    const std::optional<qint64> &expectedDurationMs() const noexcept
    {
        return m_expectedDurationMs;
    }

private:
    RequestedTrackIdentity(RecordingMbid recordingMbid,
                           std::optional<ReleaseTrackCoordinate> releaseTrackCoordinate,
                           TrackTextEvidence expectedText,
                           NormalizedTextEvidence normalizedExpectedText,
                           std::optional<qint64> expectedDurationMs)
        : m_recordingMbid(std::move(recordingMbid))
        , m_releaseTrackCoordinate(std::move(releaseTrackCoordinate))
        , m_expectedText(std::move(expectedText))
        , m_normalizedExpectedText(std::move(normalizedExpectedText))
        , m_expectedDurationMs(expectedDurationMs)
    {
    }

    RecordingMbid m_recordingMbid;
    std::optional<ReleaseTrackCoordinate> m_releaseTrackCoordinate;
    TrackTextEvidence m_expectedText;
    NormalizedTextEvidence m_normalizedExpectedText;
    std::optional<qint64> m_expectedDurationMs;
};

class CandidateTrackEntry final
{
public:
    static ExactTrackContractResult<CandidateTrackEntry> create(
        ManifestEntryId id,
        CandidateCanonicalEvidence canonicalEvidence = {},
        TrackTextEvidence textEvidence = {},
        std::optional<qint64> measuredDurationMs = std::nullopt);

    const ManifestEntryId &id() const noexcept { return m_id; }
    const CandidateCanonicalEvidence &canonicalEvidence() const noexcept
    {
        return m_canonicalEvidence;
    }
    const TrackTextEvidence &textEvidence() const noexcept { return m_textEvidence; }
    const NormalizedTextEvidence &normalizedTextEvidence() const noexcept
    {
        return m_normalizedTextEvidence;
    }
    const std::optional<qint64> &measuredDurationMs() const noexcept
    {
        return m_measuredDurationMs;
    }

private:
    CandidateTrackEntry(ManifestEntryId id,
                        CandidateCanonicalEvidence canonicalEvidence,
                        TrackTextEvidence textEvidence,
                        NormalizedTextEvidence normalizedTextEvidence,
                        std::optional<qint64> measuredDurationMs)
        : m_id(std::move(id))
        , m_canonicalEvidence(std::move(canonicalEvidence))
        , m_textEvidence(std::move(textEvidence))
        , m_normalizedTextEvidence(std::move(normalizedTextEvidence))
        , m_measuredDurationMs(measuredDurationMs)
    {
    }

    ManifestEntryId m_id;
    CandidateCanonicalEvidence m_canonicalEvidence;
    TrackTextEvidence m_textEvidence;
    NormalizedTextEvidence m_normalizedTextEvidence;
    std::optional<qint64> m_measuredDurationMs;
};

class AlbumSourceManifest final
{
public:
    static ExactTrackContractResult<AlbumSourceManifest> create(
        std::optional<SourceCandidateKey> sourceCandidateKey,
        QVector<CandidateTrackEntry> entries);

    const std::optional<SourceCandidateKey> &sourceCandidateKey() const noexcept
    {
        return m_sourceCandidateKey;
    }
    const QVector<CandidateTrackEntry> &entries() const noexcept { return m_entries; }

private:
    AlbumSourceManifest(std::optional<SourceCandidateKey> sourceCandidateKey,
                        QVector<CandidateTrackEntry> entries)
        : m_sourceCandidateKey(std::move(sourceCandidateKey))
        , m_entries(std::move(entries))
    {
    }

    std::optional<SourceCandidateKey> m_sourceCandidateKey;
    QVector<CandidateTrackEntry> m_entries;
};

struct ExactTrackResolverPolicy final {
    qint64 durationToleranceMs = 2000;
};

enum class ExactTrackOutcome {
    Exact,
    Rejected,
    Ambiguous,
    InsufficientEvidence,
};

QString exactTrackOutcomeName(ExactTrackOutcome outcome);

enum class ExactEvidenceStrength {
    None = 0,
    StructuredMetadata = 100,
    ReleaseCoordinate = 200,
    RecordingMbid = 300,
    ReleaseTrackMbid = 400,
};

QString exactEvidenceStrengthName(ExactEvidenceStrength strength);

enum class ExactTrackReasonCode {
    RecordingMbidExact,
    RecordingMbidConflict,
    ReleaseTrackMbidExact,
    ReleaseTrackMbidConflict,
    ReleaseGroupMbidExact,
    ReleaseGroupMbidConflict,
    ReleaseMbidExact,
    ReleaseMbidConflict,
    MediumPositionExact,
    MediumPositionConflict,
    TrackPositionExact,
    TrackPositionConflict,
    ReleaseCoordinateExact,
    TitleExact,
    TitleMismatch,
    ArtistExact,
    ArtistMismatch,
    AlbumExact,
    AlbumMismatch,
    FilenameTitleExact,
    FilenameMediumPositionExact,
    FilenameMediumPositionMismatch,
    FilenameTrackPositionExact,
    FilenameTrackPositionMismatch,
    DurationWithinTolerance,
    DurationMismatch,
    StructuredMetadataExact,
    StructuredEvidenceMissing,
    CanonicalEvidenceMissing,
    EqualStrongestCandidates,
    EmptyManifest,
    NoCandidateReachedExactGate,
};

QString exactTrackReasonCodeName(ExactTrackReasonCode reason);

struct ExactTrackCandidateAssessment final {
    ManifestEntryId entryId;
    ExactTrackOutcome outcome = ExactTrackOutcome::InsufficientEvidence;
    ExactEvidenceStrength strength = ExactEvidenceStrength::None;
    QVector<ExactTrackReasonCode> reasons;
};

struct ExactTrackResolution final {
    ExactTrackOutcome outcome = ExactTrackOutcome::InsufficientEvidence;
    std::optional<ManifestEntryId> selectedEntryId;
    ExactEvidenceStrength strength = ExactEvidenceStrength::None;
    QVector<ManifestEntryId> competingEntryIds;
    QVector<ExactTrackReasonCode> reasons;
    QVector<ExactTrackCandidateAssessment> assessments;
};

class ExactTrackResolver final
{
public:
    static ExactTrackResolution resolve(
        const RequestedTrackIdentity &request,
        const AlbumSourceManifest &manifest,
        const ExactTrackResolverPolicy &policy = {});
};

} // namespace vinyl
