#include "ArtistReadAdapter.h"

#include <QStringList>
#include <algorithm>
#include <utility>

namespace vinyl::artist {
namespace {

using ::vinyl::catalogue::CatalogueErrorCode;
using ::vinyl::catalogue::PageCursor;
using ::vinyl::catalogue::PageRequest;
using ::vinyl::catalogue::ReleaseGroupPrimaryType;
using ::vinyl::catalogue::artist_proof::ArtistResolution;
using ::vinyl::catalogue::artist_proof::ArtistSelector;
using ::vinyl::catalogue::artist_proof::ArtistState;
using ::vinyl::catalogue::artist_proof::DiscographyOptions;
using ::vinyl::catalogue::artist_proof::DiscographyResult;
using ::vinyl::catalogue::artist_proof::DiscographySort;
using ::vinyl::catalogue::artist_proof::GroupRow;
using ::vinyl::catalogue::artist_proof::ReleaseStatus;

ReadReply makeBaseReply(const ReadRequest &req, State state = State::Error) {
    ReadReply r;
    r.channel = req.channel;
    r.token = req.token;
    r.profileScopeId = req.profileScopeId;
    r.artistMbid = req.artistMbid.toLower();
    r.cursor = req.cursor;
    r.filter = req.filter;
    r.state = state;
    return r;
}

QString primaryTypeName(ReleaseGroupPrimaryType type) {
    switch (type) {
    case ReleaseGroupPrimaryType::Album: return QStringLiteral("Album");
    case ReleaseGroupPrimaryType::Single: return QStringLiteral("Single");
    case ReleaseGroupPrimaryType::EP: return QStringLiteral("EP");
    case ReleaseGroupPrimaryType::Broadcast: return QStringLiteral("Broadcast");
    case ReleaseGroupPrimaryType::Other: return QStringLiteral("Other");
    }
    return QStringLiteral("Other");
}

QString mediaKindName(::vinyl::RelatedVideoRelationshipKind kind) {
    using K = ::vinyl::RelatedVideoRelationshipKind;
    switch (kind) {
    case K::OfficialMusicVideo: return QStringLiteral("OfficialMusicVideo");
    case K::OfficialAudioVisualizer: return QStringLiteral("OfficialAudioVisualizer");
    case K::LivePerformance: return QStringLiteral("LivePerformance");
    case K::LyricVideo: return QStringLiteral("LyricVideo");
    case K::AlternatePerformance: return QStringLiteral("AlternatePerformance");
    case K::OtherUnknown: return QStringLiteral("OtherUnknown");
    }
    return QStringLiteral("OtherUnknown");
}

QString artworkModeName(::vinyl::artwork::DisplayMode mode) {
    using M = ::vinyl::artwork::DisplayMode;
    switch (mode) {
    case M::ArtistComposition: return QStringLiteral("composition");
    case M::StaticPhoto: return QStringLiteral("static");
    case M::Crossfade: return QStringLiteral("crossfade");
    case M::Rotating: return QStringLiteral("rotating");
    }
    return QStringLiteral("composition");
}

QVariantMap serializeHeroFrame(const ::vinyl::artwork::HeroFrame &frame) {
    return QVariantMap{
        {QStringLiteral("displayRef"), QString::fromStdString(frame.displayRef)},
        {QStringLiteral("provider"), QString::fromStdString(frame.photo.provider)},
        {QStringLiteral("providerAssetId"), QString::fromStdString(frame.photo.providerAssetId)},
        {QStringLiteral("sourceUrl"), QString::fromStdString(frame.photo.sourceUrl)},
        {QStringLiteral("width"), frame.photo.width},
        {QStringLiteral("height"), frame.photo.height},
        {QStringLiteral("fromVerifiedCache"), frame.fromVerifiedCache},
        {QStringLiteral("localOnly"), frame.localOnly},
        {QStringLiteral("releaseCleared"), frame.releaseCleared},
        {QStringLiteral("licenseId"), QString::fromStdString(frame.photo.rights.licenseId)},
        {QStringLiteral("author"), QString::fromStdString(frame.photo.rights.author)},
        {QStringLiteral("attribution"), QString::fromStdString(frame.photo.rights.attribution)}
    };
}

QVariantMap serializeGroupRow(const GroupRow &row) {
    QVariantList editions;
    for (const auto &ed : row.editions) {
        editions.append(QVariantMap{
            {QStringLiteral("releaseMbid"), ed.mbid.toString().toLower()},
            {QStringLiteral("releaseGroupMbid"), ed.releaseGroupMbid.toString().toLower()},
            {QStringLiteral("title"), ed.title},
            {QStringLiteral("status"), ed.status},
            {QStringLiteral("country"), ed.country},
            {QStringLiteral("releaseDate"), ed.releaseDate.toString(Qt::ISODate)},
            {QStringLiteral("barcode"), ed.barcode}
        });
    }

    QVariantList recordings;
    for (const auto &rec : row.recordings) {
        QVariantMap recMap{
            {QStringLiteral("recordingMbid"), rec.canonical.mbid.toString().toLower()},
            {QStringLiteral("title"), rec.canonical.title},
            {QStringLiteral("lengthMs"),
             rec.canonical.lengthMs.has_value()
                 ? QVariant::fromValue<qlonglong>(*rec.canonical.lengthMs)
                 : QVariant()}
        };
        if (rec.source) {
            recMap.insert(QStringLiteral("sourceProvider"), rec.source->item.providerId().value());
            recMap.insert(QStringLiteral("sourceItemId"), rec.source->item.itemId().value());
            recMap.insert(QStringLiteral("sourceProvenance"), rec.source->provenance);
        }
        recordings.append(std::move(recMap));
    }

    QVariantList sources;
    for (const auto &src : row.sources) {
        sources.append(QVariantMap{
            {QStringLiteral("provider"), src.item.providerId().value()},
            {QStringLiteral("itemId"), src.item.itemId().value()},
            {QStringLiteral("provenance"), src.provenance}
        });
    }

    return QVariantMap{
        {QStringLiteral("releaseGroupMbid"), row.canonical.mbid.toString().toLower()},
        {QStringLiteral("title"), row.canonical.title},
        {QStringLiteral("primaryType"), primaryTypeName(row.canonical.primaryType)},
        {QStringLiteral("secondaryTypes"), row.canonical.secondaryTypes},
        {QStringLiteral("firstReleaseYear"),
         row.canonical.firstReleaseYear.has_value()
             ? QVariant(*row.canonical.firstReleaseYear)
             : QVariant()},
        {QStringLiteral("editions"), editions},
        {QStringLiteral("recordings"), recordings},
        {QStringLiteral("sources"), sources}
    };
}

QVariantMap serializeMediaItem(const ::vinyl::artist_media::ArtistMediaItem &item) {
    QVariantMap m{
        {QStringLiteral("stableKey"), item.stableKey},
        {QStringLiteral("artistMbid"), item.artist.toString().toLower()},
        {QStringLiteral("recordingMbid"),
         item.recording ? item.recording->toString().toLower() : QString()},
        {QStringLiteral("provider"), item.provider.value()},
        {QStringLiteral("providerItemId"), item.providerItem.value()},
        {QStringLiteral("kind"), mediaKindName(item.kind)},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("channel"), item.channel},
        {QStringLiteral("sourceLabel"), item.sourceLabel},
        {QStringLiteral("snapshotId"), item.snapshotId},
        {QStringLiteral("mappingProvenance"), item.mappingProvenance},
        {QStringLiteral("directSwitchEligible"), item.directSwitchEligible}
    };
    if (item.durationMs) {
        m.insert(QStringLiteral("durationMs"), QVariant::fromValue<qlonglong>(*item.durationMs));
    }
    if (item.validUntilEpochMs) {
        m.insert(QStringLiteral("validUntilEpochMs"),
                 QVariant::fromValue<qlonglong>(*item.validUntilEpochMs));
    }
    if (item.videoThumbnail) {
        m.insert(QStringLiteral("thumbnail"), QVariantMap{
            {QStringLiteral("imageProvider"), item.videoThumbnail->imageProvider.value()},
            {QStringLiteral("imageAsset"), item.videoThumbnail->imageAsset.value()},
            {QStringLiteral("httpsLocator"), item.videoThumbnail->httpsLocator}
        });
    }
    return m;
}

} // namespace

class ArtistReadAdapter::CatalogueProviderBridge final
    : public ::vinyl::catalogue::artist_proof::IArtistCatalogueProvider {
public:
    explicit CatalogueProviderBridge(IArtistSource &source) : m_source(source) {}

    void lookup(
        const ArtistSelector &selector,
        std::uint64_t ticket,
        std::function<void(::vinyl::catalogue::artist_proof::ArtistProviderResult)> completion) override {
        m_source.lookupArtist(selector, ticket, std::move(completion));
    }

    void releaseGroups(
        const ::vinyl::ArtistMbid &artist,
        std::uint64_t ticket,
        std::function<void(::vinyl::catalogue::artist_proof::GroupsProviderResult)> completion) override {
        m_source.fetchReleaseGroups(artist, ticket, std::move(completion));
    }

    void cancel(std::uint64_t ticket) noexcept override {
        m_source.cancelCatalogue(ticket);
    }

private:
    IArtistSource &m_source;
};

class ArtistReadAdapter::MediaSourceBridge final : public ::vinyl::artist_media::ArtistMediaSource {
public:
    explicit MediaSourceBridge(IArtistSource &source) : m_source(source) {}

    void fetch(const ::vinyl::ArtistMbid &artist, Reply reply) override {
        const std::uint64_t ticket = ++m_lastTicket;
        m_source.fetchMedia(artist, ticket, std::move(reply));
    }

    std::uint64_t lastTicket() const noexcept { return m_lastTicket; }

private:
    IArtistSource &m_source;
    std::uint64_t m_lastTicket = 0;
};

class ArtistReadAdapter::MediaMappingBridge final
    : public ::vinyl::artist_media::ArtistMediaMappingPort {
public:
    explicit MediaMappingBridge(const IArtistSource &source) : m_source(source) {}

    std::optional<::vinyl::artist_media::VerifiedMediaMapping> lookup(
        const ::vinyl::ArtistMbid &artist,
        const ::vinyl::ProviderItemRef &providerItem) const override {
        return m_source.lookupMediaMapping(artist, providerItem);
    }

private:
    const IArtistSource &m_source;
};

ArtistReadAdapter::ArtistReadAdapter(IArtistSource &source, int discographyPageSize)
    : m_source(source),
      m_discographyPageSize(std::clamp(discographyPageSize, 1, 100)),
      m_catalogueProvider(std::make_unique<CatalogueProviderBridge>(source)),
      m_mediaSourceBridge(std::make_unique<MediaSourceBridge>(source)),
      m_mediaMappingBridge(std::make_unique<MediaMappingBridge>(source)),
      m_artistQuery(*m_catalogueProvider),
      m_discographyQuery(*m_catalogueProvider),
      m_mediaQuery(*m_mediaSourceBridge, *m_mediaMappingBridge),
      m_lifetime(std::make_shared<LifetimeGuard>()) {}

ArtistReadAdapter::~ArtistReadAdapter() = default;

void ArtistReadAdapter::setDiscographyPageSize(int pageSize) noexcept {
    m_discographyPageSize = std::clamp(pageSize, 1, 100);
}

bool ArtistReadAdapter::parseDiscographyFilter(
    const QString &filterText,
    DiscographyOptions &outOptions) {
    outOptions = DiscographyOptions{};
    const QString norm = filterText.trimmed().toLower();
    if (norm.isEmpty() || norm == QStringLiteral("all")) {
        return true;
    }
    if (norm == QStringLiteral("album") || norm == QStringLiteral("albums")) {
        outOptions.groups.primaryTypes = {ReleaseGroupPrimaryType::Album};
        return true;
    }
    if (norm == QStringLiteral("single") || norm == QStringLiteral("singles")) {
        outOptions.groups.primaryTypes = {ReleaseGroupPrimaryType::Single};
        return true;
    }
    if (norm == QStringLiteral("ep") || norm == QStringLiteral("eps")) {
        outOptions.groups.primaryTypes = {ReleaseGroupPrimaryType::EP};
        return true;
    }
    if (norm == QStringLiteral("single-ep") || norm == QStringLiteral("singles-eps")) {
        outOptions.groups.primaryTypes = {
            ReleaseGroupPrimaryType::Single,
            ReleaseGroupPrimaryType::EP
        };
        return true;
    }
    if (norm == QStringLiteral("broadcast")) {
        outOptions.groups.primaryTypes = {ReleaseGroupPrimaryType::Broadcast};
        return true;
    }
    if (norm == QStringLiteral("live")) {
        outOptions.groups.secondaryTypes = {QStringLiteral("Live")};
        return true;
    }
    if (norm == QStringLiteral("compilation") || norm == QStringLiteral("compilations")) {
        outOptions.groups.secondaryTypes = {QStringLiteral("Compilation")};
        return true;
    }
    if (norm == QStringLiteral("official")) {
        outOptions.statuses = {ReleaseStatus::Official};
        return true;
    }
    if (norm == QStringLiteral("promotion")) {
        outOptions.statuses = {ReleaseStatus::Promotion};
        return true;
    }
    if (norm == QStringLiteral("bootleg")) {
        outOptions.statuses = {ReleaseStatus::Bootleg};
        return true;
    }
    if (norm == QStringLiteral("oldest")) {
        outOptions.sort = DiscographySort::OldestFirst;
        return true;
    }
    if (norm == QStringLiteral("title") || norm == QStringLiteral("title-asc")) {
        outOptions.sort = DiscographySort::TitleAscending;
        return true;
    }
    if (norm == QStringLiteral("title-desc")) {
        outOptions.sort = DiscographySort::TitleDescending;
        return true;
    }
    return false;
}

void ArtistReadAdapter::request(const ReadRequest &req, std::function<void(ReadReply)> complete) {
    switch (req.channel) {
    case Channel::Artist:
        handleArtistRequest(req, std::move(complete));
        return;
    case Channel::Discography:
        handleDiscographyRequest(req, std::move(complete));
        return;
    case Channel::Media:
        handleMediaRequest(req, std::move(complete));
        return;
    case Channel::Artwork:
        handleArtworkRequest(req, std::move(complete));
        return;
    }
}

void ArtistReadAdapter::cancel(quint64 token) {
    const auto it = m_inFlight.constFind(token);
    if (it == m_inFlight.cend()) return;
    const InFlightEntry entry = it.value();
    m_inFlight.remove(token);

    switch (entry.channel) {
    case Channel::Artist:
        m_artistQuery.cancel();
        break;
    case Channel::Discography:
        if (!entry.continuation) {
            m_discographyQuery.cancel();
            m_discographySessionVersion.reset();
            m_discographyArtistMbid.clear();
            m_discographyFilter.clear();
            m_discographySourceStamp = {};
        } else if (entry.subTicket != 0) {
            m_source.cancelCatalogue(entry.subTicket);
        }
        break;
    case Channel::Media:
        if (entry.subTicket != 0) {
            m_mediaQuery.cancel(static_cast<quint64>(entry.subTicket));
            m_source.cancelMedia(entry.subTicket);
        }
        break;
    case Channel::Artwork:
        if (entry.subTicket != 0) {
            m_source.cancelArtwork(entry.subTicket);
        }
        break;
    }
}

void ArtistReadAdapter::handleArtistRequest(
    const ReadRequest &req,
    std::function<void(ReadReply)> complete) {
    const auto parsedMbid = ::vinyl::ArtistMbid::parse(req.artistMbid);
    if (!parsedMbid.ok()) {
        ReadReply bad = makeBaseReply(req, State::Error);
        bad.errorCode = QStringLiteral("invalid_query");
        bad.message = QStringLiteral("Malformed Artist MBID.");
        complete(std::move(bad));
        return;
    }

    m_inFlight.insert(req.token, InFlightEntry{Channel::Artist, 0, false});
    const std::weak_ptr<LifetimeGuard> weakLife = m_lifetime;
    auto sharedCb = std::make_shared<std::function<void(ReadReply)>>(std::move(complete));

    m_artistQuery.begin(
        ArtistSelector::byId(*parsedMbid.value),
        DiscographyOptions{},
        [this, weakLife, req, sharedCb](ArtistResolution res) {
            if (weakLife.expired()) return;
            if (!m_inFlight.contains(req.token)) return;
            m_inFlight.remove(req.token);

            ReadReply reply = makeBaseReply(req);
            switch (res.state) {
            case ArtistState::Found: {
                if (!res.artist) {
                    reply.state = State::Error;
                    reply.errorCode = QStringLiteral("invalid_payload");
                    reply.message = QStringLiteral("Resolved artist payload is missing.");
                    break;
                }
                reply.state = State::Ready;
                const QString sourceAuthority =
                    (res.source && !res.source->provenance.trimmed().isEmpty())
                        ? res.source->provenance
                        : QStringLiteral("musicbrainz-catalogue");
                const QString sourceItem =
                    res.source
                        ? res.source->item.itemId().value()
                        : (QStringLiteral("artist:") + res.artist->mbid.toString().toLower());
                reply.source = SourceStamp{sourceAuthority, sourceItem, res.corpusVersion};

                QStringList aliasNames;
                for (const auto &alias : res.artist->aliases) {
                    aliasNames.append(alias.name);
                }
                reply.entity = QVariantMap{
                    {QStringLiteral("artistMbid"), res.artist->mbid.toString().toLower()},
                    {QStringLiteral("name"), res.artist->name},
                    {QStringLiteral("sortName"), res.artist->sortName},
                    {QStringLiteral("disambiguation"), res.artist->disambiguation},
                    {QStringLiteral("aliases"), aliasNames}
                };
                if (res.source) {
                    reply.entity.insert(QStringLiteral("sourceProvider"),
                                        res.source->item.providerId().value());
                    reply.entity.insert(QStringLiteral("sourceItemId"),
                                        res.source->item.itemId().value());
                }
                break;
            }
            case ArtistState::Redirected:
                reply.state = State::Error;
                reply.errorCode = QStringLiteral("redirected");
                reply.message = res.redirectedTo
                    ? QStringLiteral("Artist MBID redirected to %1.").arg(res.redirectedTo->toString().toLower())
                    : QStringLiteral("Artist MBID redirected.");
                if (res.redirectedTo) {
                    reply.entity = QVariantMap{
                        {QStringLiteral("redirectedTo"), res.redirectedTo->toString().toLower()}
                    };
                }
                break;
            case ArtistState::NotFound:
                reply.state = State::NotFound;
                reply.errorCode = QStringLiteral("not_found");
                reply.message = res.detail.isEmpty()
                    ? QStringLiteral("Artist not found.")
                    : res.detail;
                break;
            case ArtistState::Unavailable:
                reply.state = State::Unavailable;
                reply.errorCode = QStringLiteral("unavailable");
                reply.message = res.detail.isEmpty()
                    ? QStringLiteral("Catalogue service unavailable.")
                    : res.detail;
                break;
            case ArtistState::Ambiguous:
                reply.state = State::Error;
                reply.errorCode = QStringLiteral("ambiguous");
                reply.message = res.detail.isEmpty()
                    ? QStringLiteral("Ambiguous artist identity.")
                    : res.detail;
                break;
            case ArtistState::InvalidQuery:
                reply.state = State::Error;
                reply.errorCode = QStringLiteral("invalid_query");
                reply.message = res.detail;
                break;
            }

            if (*sharedCb) (*sharedCb)(std::move(reply));
        });
}

void ArtistReadAdapter::handleDiscographyRequest(
    const ReadRequest &req,
    std::function<void(ReadReply)> complete) {
    const auto parsedMbid = ::vinyl::ArtistMbid::parse(req.artistMbid);
    if (!parsedMbid.ok()) {
        ReadReply bad = makeBaseReply(req, State::Error);
        bad.errorCode = QStringLiteral("invalid_query");
        bad.message = QStringLiteral("Malformed Artist MBID.");
        complete(std::move(bad));
        return;
    }

    DiscographyOptions options;
    if (!parseDiscographyFilter(req.filter, options)) {
        ReadReply bad = makeBaseReply(req, State::Error);
        bad.errorCode = QStringLiteral("invalid_query");
        bad.message = QStringLiteral("Unsupported discography filter.");
        complete(std::move(bad));
        return;
    }

    const QString normArtist = req.artistMbid.toLower();
    const QString normFilter = req.filter.trimmed().isEmpty()
        ? QStringLiteral("all")
        : req.filter.trimmed().toLower();
    const bool isContinuation = !req.cursor.isEmpty();

    auto sharedCb = std::make_shared<std::function<void(ReadReply)>>(std::move(complete));
    m_inFlight.insert(req.token, InFlightEntry{Channel::Discography, 0, isContinuation});

    if (isContinuation &&
        m_discographySessionVersion.has_value() &&
        m_discographyArtistMbid == normArtist &&
        m_discographyFilter == normFilter) {
        issueDiscographyPage(req, *m_discographySessionVersion, m_discographySourceStamp, sharedCb);
        return;
    }

    const std::weak_ptr<LifetimeGuard> weakLife = m_lifetime;
    m_discographySessionVersion.reset();
    m_discographyArtistMbid = normArtist;
    m_discographyFilter = normFilter;
    m_discographySourceStamp = {};

    m_discographyQuery.begin(
        ArtistSelector::byId(*parsedMbid.value),
        options,
        [this, weakLife, req, sharedCb](ArtistResolution res) {
            if (weakLife.expired()) return;
            if (!m_inFlight.contains(req.token)) return;

            if (res.state != ArtistState::Found || !res.artist) {
                m_inFlight.remove(req.token);
                ReadReply reply = makeBaseReply(req);
                if (res.state == ArtistState::Redirected) {
                    reply.state = State::Error;
                    reply.errorCode = QStringLiteral("redirected");
                    reply.message = res.redirectedTo
                        ? QStringLiteral("Artist MBID redirected to %1.").arg(res.redirectedTo->toString().toLower())
                        : QStringLiteral("Artist MBID redirected.");
                } else if (res.state == ArtistState::NotFound) {
                    reply.state = State::NotFound;
                    reply.errorCode = QStringLiteral("not_found");
                    reply.message = res.detail;
                } else if (res.state == ArtistState::Unavailable) {
                    reply.state = State::Unavailable;
                    reply.errorCode = QStringLiteral("unavailable");
                    reply.message = res.detail;
                } else {
                    reply.state = State::Error;
                    reply.errorCode = QStringLiteral("invalid_query");
                    reply.message = res.detail;
                }
                if (*sharedCb) (*sharedCb)(std::move(reply));
                return;
            }

            const QString authority =
                (res.source && !res.source->provenance.trimmed().isEmpty())
                    ? res.source->provenance
                    : QStringLiteral("musicbrainz-catalogue");
            const QString itemId =
                res.source
                    ? res.source->item.itemId().value()
                    : (QStringLiteral("discography:") + res.artist->mbid.toString().toLower());

            m_discographySessionVersion = res.version;
            m_discographySourceStamp = SourceStamp{authority, itemId, res.corpusVersion};
            issueDiscographyPage(req, res.version, m_discographySourceStamp, sharedCb);
        });
}

void ArtistReadAdapter::issueDiscographyPage(
    const ReadRequest &req,
    std::uint64_t sessionVersion,
    const SourceStamp &sessionSource,
    std::shared_ptr<std::function<void(ReadReply)>> callback) {
    PageRequest pageReq;
    pageReq.limit = m_discographyPageSize;
    if (!req.cursor.isEmpty()) {
        pageReq.cursor = PageCursor(req.cursor);
    }

    const std::weak_ptr<LifetimeGuard> weakLife = m_lifetime;
    auto invokedSynchronously = std::make_shared<bool>(false);

    const auto pageTicket = m_discographyQuery.page(
        sessionVersion,
        pageReq,
        [this, weakLife, req, sessionSource, callback, invokedSynchronously](DiscographyResult res) {
            *invokedSynchronously = true;
            if (weakLife.expired()) return;
            if (!m_inFlight.contains(req.token)) return;
            m_inFlight.remove(req.token);

            ReadReply reply = makeBaseReply(req);
            if (!res.ok() || !res.value) {
                switch (res.error.code) {
                case CatalogueErrorCode::NotFound:
                    reply.state = State::NotFound;
                    reply.errorCode = QStringLiteral("not_found");
                    break;
                case CatalogueErrorCode::Unavailable:
                    reply.state = State::Unavailable;
                    reply.errorCode = QStringLiteral("unavailable");
                    break;
                case CatalogueErrorCode::InvalidPage:
                    reply.state = State::Error;
                    reply.errorCode = QStringLiteral("invalid_page");
                    break;
                case CatalogueErrorCode::InvalidQuery:
                case CatalogueErrorCode::None:
                    reply.state = State::Error;
                    reply.errorCode = QStringLiteral("invalid_query");
                    break;
                }
                reply.message = res.error.detail.isEmpty()
                    ? QStringLiteral("Discography request failed.")
                    : res.error.detail;
                if (*callback) (*callback)(std::move(reply));
                return;
            }

            const auto &discPage = *res.value;
            if (discPage.page.items.isEmpty()) {
                reply.state = State::Empty;
                if (*callback) (*callback)(std::move(reply));
                return;
            }

            reply.state = State::Ready;
            reply.source = SourceStamp{
                sessionSource.authority.isEmpty()
                    ? QStringLiteral("musicbrainz-catalogue")
                    : sessionSource.authority,
                sessionSource.itemId.isEmpty()
                    ? (QStringLiteral("discography:") + req.artistMbid.toLower())
                    : sessionSource.itemId,
                discPage.corpusVersion
            };
            for (const auto &groupRow : discPage.page.items) {
                reply.items.append(serializeGroupRow(groupRow));
            }
            if (discPage.page.nextCursor) {
                reply.nextCursor = discPage.page.nextCursor->token();
            }
            if (*callback) (*callback)(std::move(reply));
        });

    if (pageTicket && m_inFlight.contains(req.token)) {
        m_inFlight[req.token].subTicket = *pageTicket;
    } else if (!pageTicket && !*invokedSynchronously && m_inFlight.contains(req.token)) {
        m_inFlight.remove(req.token);
        ReadReply stale = makeBaseReply(req, State::Error);
        stale.errorCode = QStringLiteral("invalid_page");
        stale.message = QStringLiteral("Discography session is no longer active.");
        if (*callback) (*callback)(std::move(stale));
    }
}

void ArtistReadAdapter::handleMediaRequest(
    const ReadRequest &req,
    std::function<void(ReadReply)> complete) {
    const std::weak_ptr<LifetimeGuard> weakLife = m_lifetime;
    auto sharedCb = std::make_shared<std::function<void(ReadReply)>>(std::move(complete));

    m_inFlight.insert(req.token, InFlightEntry{Channel::Media, 0, false});
    const quint64 mediaTicket = m_mediaQuery.start(
        req.artistMbid,
        [this, weakLife, req, sharedCb](const ::vinyl::artist_media::ArtistMediaResult &res) {
            if (weakLife.expired()) return;
            if (!m_inFlight.contains(req.token)) return;
            m_inFlight.remove(req.token);

            ReadReply reply = makeBaseReply(req);
            using ::vinyl::artist_media::QueryStatus;
            switch (res.status) {
            case QueryStatus::Empty:
                reply.state = State::Empty;
                break;
            case QueryStatus::Unavailable:
                reply.state = State::Unavailable;
                reply.errorCode = res.errorCode.isEmpty()
                    ? QStringLiteral("unavailable")
                    : res.errorCode;
                reply.message = QStringLiteral("Artist media source unavailable.");
                break;
            case QueryStatus::Invalid:
                reply.state = State::Error;
                reply.errorCode = res.errorCode.isEmpty()
                    ? QStringLiteral("invalid_media")
                    : res.errorCode;
                reply.message = QStringLiteral("Artist media response failed validation.");
                break;
            case QueryStatus::Ready: {
                reply.state = State::Ready;
                QVariantList videos;
                for (const auto &v : res.videos) {
                    videos.append(serializeMediaItem(v));
                }
                QVariantList live;
                for (const auto &l : res.live) {
                    live.append(serializeMediaItem(l));
                }
                const auto &sample = !res.videos.isEmpty() ? res.videos.first() : res.live.first();
                reply.source = SourceStamp{
                    sample.sourceLabel.isEmpty()
                        ? QStringLiteral("artist-media")
                        : sample.sourceLabel,
                    sample.provider.value() + QLatin1Char(':') + sample.providerItem.value(),
                    sample.snapshotId
                };
                reply.entity = QVariantMap{
                    {QStringLiteral("artistMbid"), req.artistMbid.toLower()},
                    {QStringLiteral("videos"), videos},
                    {QStringLiteral("live"), live},
                    {QStringLiteral("videoCount"), videos.size()},
                    {QStringLiteral("liveCount"), live.size()}
                };
                break;
            }
            }
            if (*sharedCb) (*sharedCb)(std::move(reply));
        });

    if (m_inFlight.contains(req.token)) {
        m_inFlight[req.token].subTicket = mediaTicket;
    }
}

void ArtistReadAdapter::handleArtworkRequest(
    const ReadRequest &req,
    std::function<void(ReadReply)> complete) {
    const auto parsedMbid = ::vinyl::ArtistMbid::parse(req.artistMbid);
    if (!parsedMbid.ok()) {
        ReadReply bad = makeBaseReply(req, State::Error);
        bad.errorCode = QStringLiteral("invalid_query");
        bad.message = QStringLiteral("Malformed Artist MBID.");
        complete(std::move(bad));
        return;
    }

    const std::uint64_t subTicket = m_nextSubTicket++;
    m_inFlight.insert(req.token, InFlightEntry{Channel::Artwork, subTicket, false});
    const std::weak_ptr<LifetimeGuard> weakLife = m_lifetime;
    auto sharedCb = std::make_shared<std::function<void(ReadReply)>>(std::move(complete));

    m_source.fetchArtwork(
        *parsedMbid.value,
        subTicket,
        [this, weakLife, req, sharedCb](ArtworkSourcePayload payload) {
            if (weakLife.expired()) return;
            if (!m_inFlight.contains(req.token)) return;
            m_inFlight.remove(req.token);

            ReadReply reply = makeBaseReply(req);
            if (!payload.available && payload.cached.empty()) {
                reply.state = State::Unavailable;
                reply.errorCode = QStringLiteral("unavailable");
                reply.message = QStringLiteral("Artwork provider is unavailable.");
                if (*sharedCb) (*sharedCb)(std::move(reply));
                return;
            }

            ::vinyl::artwork::Request artReq;
            artReq.artistMbid = req.artistMbid.toLower().toStdString();
            artReq.artistName = payload.artistName.toStdString();
            artReq.fetched = std::move(payload.fetched);
            artReq.cached = std::move(payload.cached);
            artReq.userOverride = std::move(payload.userOverride);
            artReq.scope = payload.scope;
            artReq.providerAvailable = payload.available;
            artReq.nowEpoch = payload.nowEpoch;
            artReq.maxSlides = payload.maxSlides;

            const ::vinyl::artwork::Decision decision = m_artworkResolver.resolve(artReq);

            QVariantList slides;
            for (const auto &frame : decision.slides) {
                slides.append(serializeHeroFrame(frame));
            }
            QStringList rejections;
            for (const auto &reason : decision.rejectionReasons) {
                rejections.append(QString::fromStdString(reason));
            }

            QVariantMap fallbackMap{
                {QStringLiteral("artistMbid"),
                 QString::fromStdString(decision.fallback.artistMbid).toLower()},
                {QStringLiteral("name"), QString::fromStdString(decision.fallback.name)},
                {QStringLiteral("monogram"), QString::fromStdString(decision.fallback.monogram)},
                {QStringLiteral("layoutSeed"),
                 QVariant::fromValue<qulonglong>(decision.fallback.layoutSeed)},
                {QStringLiteral("hasPortrait"), decision.fallback.eligiblePortrait.has_value()}
            };
            if (decision.fallback.eligiblePortrait) {
                fallbackMap.insert(
                    QStringLiteral("portrait"),
                    serializeHeroFrame(*decision.fallback.eligiblePortrait));
            }

            reply.state = State::Ready;
            reply.source = SourceStamp{
                payload.authority.trimmed().isEmpty()
                    ? QStringLiteral("artwork-resolver")
                    : payload.authority,
                payload.sourceItemId.trimmed().isEmpty()
                    ? (QStringLiteral("artwork:") + req.artistMbid.toLower())
                    : payload.sourceItemId,
                payload.revision
            };
            reply.entity = QVariantMap{
                {QStringLiteral("artistMbid"), req.artistMbid.toLower()},
                {QStringLiteral("mode"), artworkModeName(decision.mode)},
                {QStringLiteral("slides"), slides},
                {QStringLiteral("slideCount"), slides.size()},
                {QStringLiteral("releaseCleared"), decision.releaseCleared},
                {QStringLiteral("fallback"), fallbackMap},
                {QStringLiteral("rejectionReasons"), rejections}
            };
            if (*sharedCb) (*sharedCb)(std::move(reply));
        });
}

} // namespace vinyl::artist
