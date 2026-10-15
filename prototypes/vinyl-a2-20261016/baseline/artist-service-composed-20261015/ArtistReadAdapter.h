#pragma once

#include "../artist-live-prototypes-20261015/artwork/ArtistArtworkResolver.h"
#include "../artist-live-prototypes-20261015/bridge/ArtistServiceBridge.h"
#include "../artist-live-prototypes-20261015/catalogue/ArtistCatalogueQuery.h"
#include "../artist-live-prototypes-20261015/media/ArtistMediaQuery.h"
#include "IArtistSource.h"

#include <QHash>
#include <QString>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace vinyl::artist {

// Concrete IArtistReadPort implementation composing ArtistCatalogueQuery,
// ArtistMediaQuery, and ArtistArtworkResolver over an injectable IArtistSource.
class ArtistReadAdapter final : public IArtistReadPort {
public:
    explicit ArtistReadAdapter(IArtistSource &source, int discographyPageSize = 2);
    ~ArtistReadAdapter() override;

    ArtistReadAdapter(const ArtistReadAdapter &) = delete;
    ArtistReadAdapter &operator=(const ArtistReadAdapter &) = delete;

    void setDiscographyPageSize(int pageSize) noexcept;
    int discographyPageSize() const noexcept { return m_discographyPageSize; }

    void request(const ReadRequest &req, std::function<void(ReadReply)> complete) override;
    void cancel(quint64 token) override;

private:
    class CatalogueProviderBridge;
    class MediaSourceBridge;
    class MediaMappingBridge;

    struct InFlightEntry {
        Channel channel = Channel::Artist;
        std::uint64_t subTicket = 0;
        bool continuation = false;
    };

    void handleArtistRequest(const ReadRequest &req, std::function<void(ReadReply)> complete);
    void handleDiscographyRequest(const ReadRequest &req, std::function<void(ReadReply)> complete);
    void handleMediaRequest(const ReadRequest &req, std::function<void(ReadReply)> complete);
    void handleArtworkRequest(const ReadRequest &req, std::function<void(ReadReply)> complete);

    void issueDiscographyPage(
        const ReadRequest &req,
        std::uint64_t sessionVersion,
        const SourceStamp &sessionSource,
        std::shared_ptr<std::function<void(ReadReply)>> callback);

    static bool parseDiscographyFilter(
        const QString &filterText,
        ::vinyl::catalogue::artist_proof::DiscographyOptions &outOptions);

    IArtistSource &m_source;
    int m_discographyPageSize = 2;
    std::uint64_t m_nextSubTicket = 1;

    std::unique_ptr<CatalogueProviderBridge> m_catalogueProvider;
    std::unique_ptr<MediaSourceBridge> m_mediaSourceBridge;
    std::unique_ptr<MediaMappingBridge> m_mediaMappingBridge;

    ::vinyl::catalogue::artist_proof::ArtistCatalogueQuery m_artistQuery;
    ::vinyl::catalogue::artist_proof::ArtistCatalogueQuery m_discographyQuery;
    ::vinyl::artist_media::ArtistMediaQuery m_mediaQuery;
    ::vinyl::artwork::ArtistArtworkResolver m_artworkResolver;

    std::optional<std::uint64_t> m_discographySessionVersion;
    QString m_discographyArtistMbid;
    QString m_discographyFilter;
    SourceStamp m_discographySourceStamp;

    struct LifetimeGuard {};
    std::shared_ptr<LifetimeGuard> m_lifetime;
    QHash<quint64, InFlightEntry> m_inFlight;
};

} // namespace vinyl::artist
