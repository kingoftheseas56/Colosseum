#pragma once

#include "../../bg1-catalogue-client-prototype/VinylCatalogueTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace vinyl::catalogue::artist_proof {

// BA0 MBIDs are authoritative. Source browse IDs are *mappings*, never MBIDs.
struct SourceRef {
    ProviderItemRef item;
    QString provenance;
};

struct SourcedCredit {
    ArtistCredit canonical;
    std::optional<SourceRef> source;
};

struct SourcedRecording {
    Recording canonical;
    std::optional<SourceRef> source;
    QVector<SourcedCredit> credits;
    QVector<SourceRef> additionalSources; // retain alternate provider mappings
};

struct ArtistRow {
    Artist canonical;
    std::optional<SourceRef> source;
};

struct GroupRow {
    ReleaseGroup canonical;
    QVector<Release> editions;
    QVector<SourceRef> sources;
    QVector<SourcedCredit> credits;
    QVector<SourcedRecording> recordings;
};

enum class ReleaseStatus { Official, Promotion, Bootleg, PseudoRelease, Other };
enum class DiscographySort { OldestFirst, NewestFirst, TitleAscending, TitleDescending };

struct DiscographyOptions {
    ReleaseGroupFilter groups;
    QVector<ReleaseStatus> statuses;
    DiscographySort sort = DiscographySort::NewestFirst;
};

struct ArtistSelector {
    std::optional<ArtistMbid> mbid;
    QString exactName;
    static ArtistSelector byId(ArtistMbid id);
    static ArtistSelector byExactName(QString text);
};

struct ArtistEnvelope {
    QString corpusVersion;
    QVector<ArtistRow> candidates;
    std::optional<ArtistMbid> redirectedTo;
};

struct GroupsEnvelope {
    QString corpusVersion;
    QVector<GroupRow> rows;
};

using ArtistProviderResult = CatalogueResult<ArtistEnvelope>;
using GroupsProviderResult = CatalogueResult<GroupsEnvelope>;

// Missing live YTM artist adapter seam: an adopter MUST implement both methods
// using canonical MBID mapping/provenance, not a name-derived synthetic UUID.
// Name resolution must enumerate all exact-name/alias candidates (fail closed on
// collisions). The immutable revision must fence both calls. No network here.
class IArtistCatalogueProvider {
public:
    virtual ~IArtistCatalogueProvider() = default;
    virtual void lookup(const ArtistSelector &selector, std::uint64_t ticket,
                        std::function<void(ArtistProviderResult)> completion) = 0;
    virtual void releaseGroups(const ArtistMbid &artist, std::uint64_t ticket,
                               std::function<void(GroupsProviderResult)> completion) = 0;
    virtual void cancel(std::uint64_t ticket) noexcept = 0;
};

enum class ArtistState {
    Found, NotFound, Redirected, Unavailable, Ambiguous, InvalidQuery
};

struct ArtistResolution {
    ArtistState state = ArtistState::Unavailable;
    std::uint64_t version = 0;
    std::optional<Artist> artist;
    std::optional<ArtistMbid> redirectedTo;
    std::optional<SourceRef> source;
    QString corpusVersion;
    QString detail;
};

struct DiscographyPage {
    Page<GroupRow> page;
    QString corpusVersion;
    std::uint64_t sessionVersion = 0;
};
using DiscographyResult = CatalogueResult<DiscographyPage>;

// One view-scoped, latest-request-wins query session. Provider completions
// must be serialized to the caller's thread; no QObject or QML dependency.
class ArtistCatalogueQuery {
public:
    explicit ArtistCatalogueQuery(IArtistCatalogueProvider &provider);
    ~ArtistCatalogueQuery();
    ArtistCatalogueQuery(const ArtistCatalogueQuery &) = delete;
    ArtistCatalogueQuery &operator=(const ArtistCatalogueQuery &) = delete;

    std::uint64_t begin(ArtistSelector selector, DiscographyOptions options,
                        std::function<void(ArtistResolution)> completion);
    // nullopt => obsolete session, callback deliberately NOT invoked.
    std::optional<std::uint64_t> page(
        std::uint64_t version, PageRequest request,
        std::function<void(DiscographyResult)> completion);
    void cancel() noexcept;
    std::uint64_t version() const noexcept;

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace vinyl::catalogue::artist_proof
