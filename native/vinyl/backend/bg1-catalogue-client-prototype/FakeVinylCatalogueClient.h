#pragma once

#include "VinylCatalogueClient.h"

namespace vinyl::catalogue {

class FakeCatalogueClient final : public ICatalogueClient
{
public:
    FakeCatalogueClient();

    void setAvailable(bool available) noexcept { m_available = available; }

    CatalogueLookupResult<Artist, ArtistMbid> artistById(const ArtistMbid &id) const override;
    CatalogueLookupResult<ReleaseGroup, ReleaseGroupMbid> releaseGroupById(const ReleaseGroupMbid &id) const override;
    CatalogueLookupResult<Release, ReleaseMbid> releaseById(const ReleaseMbid &id) const override;
    CatalogueLookupResult<Recording, RecordingMbid> recordingById(const RecordingMbid &id) const override;

    CatalogueResult<QVector<CanonicalBatchLookupItem>> lookupCanonicalBatch(
        const QVector<CanonicalEntityRef> &ids) const override;

    CatalogueLookupResult<Release, ReleaseGroupMbid> representativeRelease(
        const ReleaseGroupMbid &releaseGroup) const override;

    CatalogueResult<Page<ReleaseGroup>> artistDiscography(
        const ArtistMbid &artist,
        const ArtistDiscographyQuery &query,
        const PageRequest &page) const override;

    CatalogueResult<Page<Release>> releaseGroupReleases(
        const ReleaseGroupMbid &releaseGroup,
        const ReleaseVersionQuery &query,
        const PageRequest &page) const override;

    CatalogueResult<ReleaseTracklist> releaseTracklist(
        const ReleaseMbid &release) const override;

    CatalogueResult<Page<RecordingAppearance>> recordingAppearances(
        const RecordingMbid &recording,
        const PageRequest &page) const override;

    CatalogueResult<QVector<EntityRelationship>> recordingRelationships(
        const RecordingMbid &recording) const override;

    CatalogueResult<Page<SearchHit>> search(
        const SearchQuery &query,
        const PageRequest &page) const override;

private:
    struct AppearanceSet final {
        RecordingMbid recordingMbid;
        QVector<RecordingAppearance> appearances;
    };

    struct RelationshipSet final {
        RecordingMbid recordingMbid;
        QVector<EntityRelationship> relationships;
    };

    bool m_available = true;
    QVector<Artist> m_artists;
    QVector<ReleaseGroup> m_releaseGroups;
    QVector<Release> m_releases;
    QVector<Recording> m_recordings;
    QVector<ReleaseTracklist> m_tracklists;
    QVector<AppearanceSet> m_appearanceSets;
    QVector<RelationshipSet> m_relationshipSets;
    QVector<std::pair<CanonicalEntityRef, CanonicalEntityRef>> m_redirects;
    QVector<std::pair<ReleaseGroupMbid, ReleaseMbid>> m_representativeReleases;
};

} // namespace vinyl::catalogue
