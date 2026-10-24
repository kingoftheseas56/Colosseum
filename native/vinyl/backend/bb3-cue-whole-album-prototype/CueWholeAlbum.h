#pragma once

#include "ExactTrackResolver.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class CueWholeAlbumErrorCode {
    None,
    InvalidSourceId,
    EmptyCue,
    MissingFile,
    MultipleFileSections,
    InvalidFileDirective,
    TrackBeforeFile,
    InvalidTrackDirective,
    InvalidMetadataDirective,
    NonAudioTrack,
    InvalidTrackNumber,
    InvalidTrackOrder,
    NoAudioTracks,
    IndexBeforeTrack,
    InvalidIndexDirective,
    DuplicateIndex01,
    MissingIndex01,
    InvalidCueSeconds,
    InvalidCueFrame,
    CueTimeOverflow,
    NonIncreasingStart,
    InvalidBackingDuration,
    StartBeyondBackingDuration,
    InvalidMediumPosition,
    Bb2BridgeFailure,
    ExactResolutionRequired,
    ExactResolutionEntryMissing,
};

struct CueWholeAlbumError final {
    CueWholeAlbumErrorCode code = CueWholeAlbumErrorCode::None;
    int line = 0;
    QString detail;
};

QString cueWholeAlbumErrorCodeName(CueWholeAlbumErrorCode code);

template <typename T>
struct CueWholeAlbumResult final {
    std::optional<T> value;
    CueWholeAlbumError error;

    bool ok() const noexcept { return value.has_value(); }

    static CueWholeAlbumResult success(T value)
    {
        CueWholeAlbumResult out;
        out.value = std::move(value);
        return out;
    }

    static CueWholeAlbumResult failure(CueWholeAlbumErrorCode code,
                                       int line = 0,
                                       QString detail = QString())
    {
        CueWholeAlbumResult out;
        out.error = CueWholeAlbumError{code, line, std::move(detail)};
        return out;
    }
};

class WholeAlbumSourceId final
{
public:
    static CueWholeAlbumResult<WholeAlbumSourceId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const WholeAlbumSourceId &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    explicit WholeAlbumSourceId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class VirtualTrackId final
{
public:
    static CueWholeAlbumResult<VirtualTrackId> fromSourceAndCueTrack(
        const WholeAlbumSourceId &sourceId,
        int trackNumber);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const VirtualTrackId &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    explicit VirtualTrackId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

enum class CueInputOrigin {
    ExternalSidecar,
    EmbeddedMetadata,
};

QString cueInputOriginName(CueInputOrigin origin);

enum class VirtualTrackEndKind {
    NextTrackStart,
    BackingDuration,
    OpenEnded,
};

QString virtualTrackEndKindName(VirtualTrackEndKind kind);

struct CueVirtualTrack final {
    VirtualTrackId id;
    int cueTrackNumber = 0;
    QString title;
    QString performer;
    qint64 startMs = 0;
    std::optional<qint64> endMs;
    VirtualTrackEndKind endKind = VirtualTrackEndKind::OpenEnded;
};

struct CueVirtualAlbum final {
    WholeAlbumSourceId sourceId;
    CueInputOrigin origin = CueInputOrigin::ExternalSidecar;
    QString declaredFileName;
    QString albumTitle;
    QString albumPerformer;
    std::optional<qint64> backingDurationMs;
    QVector<CueVirtualTrack> tracks;
};

struct CueBb2ManifestContext final {
    std::optional<SourceCandidateKey> sourceCandidateKey;
    std::optional<int> mediumPosition;
};

struct ExactVirtualTrackSlice final {
    RecordingMbid recordingMbid;
    std::optional<ReleaseTrackMbid> releaseTrackMbid;
    WholeAlbumSourceId sourceId;
    VirtualTrackId virtualTrackId;
    int cueTrackNumber = 0;
    qint64 startMs = 0;
    std::optional<qint64> endMs;
    VirtualTrackEndKind endKind = VirtualTrackEndKind::OpenEnded;
    ExactEvidenceStrength exactEvidenceStrength = ExactEvidenceStrength::None;
};

class CueWholeAlbum final
{
public:
    static CueWholeAlbumResult<CueVirtualAlbum> normalize(
        WholeAlbumSourceId sourceId,
        CueInputOrigin origin,
        const QString &cueText,
        std::optional<qint64> backingDurationMs = std::nullopt);

    static CueWholeAlbumResult<AlbumSourceManifest> toBb2Manifest(
        const CueVirtualAlbum &album,
        const CueBb2ManifestContext &context = {});

    static CueWholeAlbumResult<ExactVirtualTrackSlice> resolveExactSlice(
        const CueVirtualAlbum &album,
        const CueBb2ManifestContext &context,
        const RequestedTrackIdentity &request,
        const ExactTrackResolverPolicy &policy = {});
};

} // namespace vinyl
