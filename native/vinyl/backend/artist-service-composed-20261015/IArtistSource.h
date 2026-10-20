#pragma once

#include "../artist-live-prototypes-20261015/artwork/ArtistArtworkResolver.h"
#include "../artist-live-prototypes-20261015/catalogue/ArtistCatalogueQuery.h"
#include "../artist-live-prototypes-20261015/media/ArtistMediaQuery.h"

#include <QString>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace vinyl::artist {

struct ArtworkSourcePayload {
    bool available = true;
    QString authority = QStringLiteral("artwork-provider");
    QString sourceItemId;
    QString revision = QStringLiteral("artwork-rev-1");
    QString artistName;
    std::vector<::vinyl::artwork::PhotoCandidate> fetched;
    std::vector<::vinyl::artwork::CachedPhoto> cached;
    std::optional<::vinyl::artwork::LocalOverride> userOverride;
    ::vinyl::artwork::Scope scope = ::vinyl::artwork::Scope::Release;
    std::int64_t nowEpoch = 1000;
    std::size_t maxSlides = 8;
};

// Injectable provider/data-source seam beneath ArtistReadAdapter and RecordingPlaybackPort.
// A1's provider feed adapter plugs into this interface later without changing bridge or adapter code.
// Implementations must not invoke network calls or provider.py inside offline tests.
class IArtistSource {
public:
    virtual ~IArtistSource() = default;

    // Catalogue lookup & release-group discography (consumed via ArtistCatalogueQuery)
    virtual void lookupArtist(
        const ::vinyl::catalogue::artist_proof::ArtistSelector &selector,
        std::uint64_t ticket,
        std::function<void(::vinyl::catalogue::artist_proof::ArtistProviderResult)> completion) = 0;

    virtual void fetchReleaseGroups(
        const ::vinyl::ArtistMbid &artist,
        std::uint64_t ticket,
        std::function<void(::vinyl::catalogue::artist_proof::GroupsProviderResult)> completion) = 0;

    virtual void cancelCatalogue(std::uint64_t ticket) noexcept = 0;

    // Media rows & BF2 trusted relationship lookup (consumed via ArtistMediaQuery)
    virtual void fetchMedia(
        const ::vinyl::ArtistMbid &artist,
        std::uint64_t ticket,
        std::function<void(::vinyl::artist_media::SourceBatch)> completion) = 0;

    virtual std::optional<::vinyl::artist_media::VerifiedMediaMapping> lookupMediaMapping(
        const ::vinyl::ArtistMbid &artist,
        const ::vinyl::ProviderItemRef &providerItem) const = 0;

    virtual void cancelMedia(std::uint64_t ticket) noexcept = 0;

    // Artwork candidates & cache attestations (consumed via ArtistArtworkResolver)
    virtual void fetchArtwork(
        const ::vinyl::ArtistMbid &artist,
        std::uint64_t ticket,
        std::function<void(ArtworkSourcePayload)> completion) = 0;

    virtual void cancelArtwork(std::uint64_t ticket) noexcept = 0;

    // Authoritative edition/group and recording/edition link verification for RecordingPlaybackPort
    virtual bool hasReleaseInGroup(
        const ::vinyl::ArtistMbid &artist,
        const ::vinyl::ReleaseGroupMbid &group,
        const ::vinyl::ReleaseMbid &release) const = 0;

    // True only for one authoritative recording occurrence in this concrete edition.
    // Missing tracklists and repeated occurrences require explicit disambiguation.
    virtual bool hasRecordingInRelease(
        const ::vinyl::ArtistMbid &artist,
        const ::vinyl::ReleaseGroupMbid &group,
        const ::vinyl::ReleaseMbid &release,
        const ::vinyl::RecordingMbid &recording) const = 0;
};

} // namespace vinyl::artist
