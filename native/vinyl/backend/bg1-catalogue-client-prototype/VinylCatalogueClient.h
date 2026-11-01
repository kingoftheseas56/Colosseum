#pragma once

#include "VinylCatalogueTypes.h"

namespace vinyl::catalogue {

class ICatalogueClient
{
public:
    virtual ~ICatalogueClient() = default;

    virtual CatalogueLookupResult<Artist, ArtistMbid> artistById(const ArtistMbid &id) const = 0;
    virtual CatalogueLookupResult<ReleaseGroup, ReleaseGroupMbid> releaseGroupById(const ReleaseGroupMbid &id) const = 0;
    virtual CatalogueLookupResult<Release, ReleaseMbid> releaseById(const ReleaseMbid &id) const = 0;
    virtual CatalogueLookupResult<Recording, RecordingMbid> recordingById(const RecordingMbid &id) const = 0;

    virtual CatalogueResult<QVector<CanonicalBatchLookupItem>> lookupCanonicalBatch(
        const QVector<CanonicalEntityRef> &ids) const = 0;

    virtual CatalogueLookupResult<Release, ReleaseGroupMbid> representativeRelease(
        const ReleaseGroupMbid &releaseGroup) const = 0;

    virtual CatalogueResult<Page<ReleaseGroup>> artistDiscography(
        const ArtistMbid &artist,
        const ArtistDiscographyQuery &query,
        const PageRequest &page) const = 0;

    virtual CatalogueResult<Page<Release>> releaseGroupReleases(
        const ReleaseGroupMbid &releaseGroup,
        const ReleaseVersionQuery &query,
        const PageRequest &page) const = 0;

    virtual CatalogueResult<ReleaseTracklist> releaseTracklist(
        const ReleaseMbid &release) const = 0;

    virtual CatalogueResult<Page<RecordingAppearance>> recordingAppearances(
        const RecordingMbid &recording,
        const PageRequest &page) const = 0;

    virtual CatalogueResult<QVector<EntityRelationship>> recordingRelationships(
        const RecordingMbid &recording) const = 0;

    virtual CatalogueResult<Page<SearchHit>> search(
        const SearchQuery &query,
        const PageRequest &page) const = 0;
};

} // namespace vinyl::catalogue
