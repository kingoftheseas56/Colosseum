#pragma once

// Arc 62 artist-media-only reference adapter: BA0/BF2 real types; BF3 intent, no player.
#include "RelatedVideoContract.h"

#include <QVector>
#include <QString>
#include <QtGlobal>
#include <functional>
#include <memory>
#include <optional>

namespace vinyl::artist_media {

enum class QueryStatus { Ready, Empty, Unavailable, Invalid };
enum class ActionChoice { OpenMedia, RequestAudioVideoSwitch };
enum class ActionFailure { None, Stale, NotReady, NotFound, Invalid, ArtistMismatch,
                           RecordingMismatch, NotEligible, Expired };

struct VideoThumbnailProvenance {
    ProviderId imageProvider;
    ProviderItemId imageAsset;
    QString httpsLocator; // Optional display-only locator, never playback transport.
};

struct ProviderVideoRow {
    ProviderId provider;
    ProviderItemId providerItem;
    QString title;
    QString channel;
    std::optional<VideoThumbnailProvenance> thumbnail;
    std::optional<qint64> durationMs;
    std::optional<qint64> validUntilEpochMs;
    RelatedVideoDiscoveryMethod discovery = RelatedVideoDiscoveryMethod::ProviderSearch;
    // Deliberately untrusted; it MUST NOT confer officiality or relationship.
    std::optional<RelatedVideoRelationshipKind> sourceClaim;
};

struct SourceBatch {
    ArtistMbid artist;
    QueryStatus status = QueryStatus::Ready;
    QString sourceLabel;
    QString snapshotId;
    QVector<ProviderVideoRow> rows;
};

// Deliberately independent of provider search rows. The adopter must provide
// a *trusted*, previously verified exact artist/provider/recording relationship.
struct VerifiedMediaMapping {
    ArtistMbid artist;
    std::optional<RecordingMbid> recording;
    ProviderId provider;
    ProviderItemId providerItem;
    RelatedVideoRelationshipKind kind;
    QVector<RelationshipEvidence> evidence;
    QString mappingProvenance;
};

class ArtistMediaMappingPort {
public:
    virtual ~ArtistMediaMappingPort() = default;
    virtual std::optional<VerifiedMediaMapping> lookup(
        const ArtistMbid &artist, const ProviderItemRef &providerItem) const = 0;
};

class ArtistMediaSource {
public:
    using Reply = std::function<void(SourceBatch)>;
    virtual ~ArtistMediaSource() = default;
    virtual void fetch(const ArtistMbid &artist, Reply reply) = 0;
};

struct ArtistMediaItem {
    ArtistMbid artist;
    std::optional<RecordingMbid> recording;
    ProviderId provider;
    ProviderItemId providerItem;
    RelatedVideoRelationshipKind kind;
    QString stableKey;
    QString title;
    QString channel;
    QString sourceLabel;
    QString snapshotId;
    QString mappingProvenance;
    std::optional<VideoThumbnailProvenance> videoThumbnail;
    std::optional<qint64> durationMs;
    std::optional<qint64> validUntilEpochMs;
    std::optional<RelatedVideoCandidate> bf2Candidate; // only when Recording is known
    bool directSwitchEligible = false;
};

struct ArtistMediaResult {
    quint64 ticket = 0;
    QueryStatus status = QueryStatus::Invalid;
    QVector<ArtistMediaItem> videos;
    QVector<ArtistMediaItem> live;
    QString errorCode; // Stable, safe, non-provider-specific diagnostic.
};

struct MediaActionIntent {
    ActionChoice choice;
    ArtistMbid artist;
    std::optional<RecordingMbid> recording;
    ProviderId provider;
    ProviderItemId providerItem;
    QString mediaKey;
    std::optional<RelatedVideoCandidate> bf2Candidate; // passed to BF3 only for switch
    // No resolved URL, auth headers, player command, or trust in a source claim.
};

struct ActionResult {
    std::optional<MediaActionIntent> intent;
    ActionFailure failure = ActionFailure::None;
    bool ok() const noexcept { return intent.has_value(); }
};

class ArtistMediaQuery final {
public:
    using ResultCallback = std::function<void(const ArtistMediaResult &)>;
    ArtistMediaQuery(ArtistMediaSource &source, const ArtistMediaMappingPort &mappings);
    ~ArtistMediaQuery();
    ArtistMediaQuery(const ArtistMediaQuery &) = delete;
    ArtistMediaQuery &operator=(const ArtistMediaQuery &) = delete;

    quint64 start(const QString &artistMbid, ResultCallback done);
    void cancel(quint64 ticket);
    ActionResult select(quint64 ticket, const QString &mediaKey,
                        const QString &expectedArtistMbid, ActionChoice choice,
                        const std::optional<QString> &expectedRecordingMbid,
                        qint64 nowEpochMs) const;
private:
    struct State;
    std::shared_ptr<State> m_state;
    ArtistMediaSource &m_source;
};

const char *queryStatusName(QueryStatus status);
const char *actionFailureName(ActionFailure failure);

} // namespace vinyl::artist_media
