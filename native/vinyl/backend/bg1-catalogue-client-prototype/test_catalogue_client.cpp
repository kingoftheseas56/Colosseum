#include "FakeVinylCatalogueClient.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>

#include <cstdlib>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;

    qCritical().noquote() << "FAIL:" << message;
    std::exit(1);
}

template <typename Id>
Id mustId(const char *text)
{
    const auto parsed = Id::parse(QString::fromLatin1(text));
    require(parsed.ok(), "fixture MBID must parse");
    return *parsed.value;
}

QString readUtf8(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text),
            "public contract source should be readable");
    return QString::fromUtf8(file.readAll());
}

void provePublicSurfaceHasNoImplementationLeaks()
{
    const QString root = QStringLiteral(VINYL_BG1_SOURCE_DIR);
    const QString publicSurface =
        readUtf8(root + QStringLiteral("/VinylCatalogueClient.h"))
        + readUtf8(root + QStringLiteral("/VinylCatalogueTypes.h"));

    const QStringList implementationTerms{
        QStringLiteral("QNetwork"),
        QStringLiteral("QUrl"),
        QStringLiteral("http"),
        QStringLiteral("HTTP"),
        QStringLiteral("Cloudflare"),
        QStringLiteral("Worker"),
        QStringLiteral("R2"),
        QStringLiteral("PostgreSQL"),
        QStringLiteral("SQLite"),
        QStringLiteral("QSql"),
    };
    for (const QString &term : implementationTerms) {
        require(!publicSurface.contains(term),
                "storage/transport implementation must not leak into public BG1 contract");
    }

    const QStringList presentationTerms{
        QStringLiteral("QML"),
        QStringLiteral("QVariant"),
        QStringLiteral("Discover"),
        QStringLiteral("carousel"),
        QStringLiteral("rail"),
        QStringLiteral("mock"),
    };
    for (const QString &term : presentationTerms) {
        require(!publicSurface.contains(term, Qt::CaseInsensitive),
                "current presentation/mock concepts must not leak into BG1 contract");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;
    using namespace vinyl::catalogue;

    FakeCatalogueClient fake;
    ICatalogueClient &client = fake;

    const auto artistA =
        mustId<ArtistMbid>("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const auto artistB =
        mustId<ArtistMbid>("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    const auto rg1 =
        mustId<ReleaseGroupMbid>("11111111-1111-4111-8111-111111111111");
    const auto rg2 =
        mustId<ReleaseGroupMbid>("22222222-2222-4222-8222-222222222222");
    const auto rg3 =
        mustId<ReleaseGroupMbid>("33333333-3333-4333-8333-333333333333");
    const auto rg4 =
        mustId<ReleaseGroupMbid>("44444444-4444-4444-8444-444444444444");
    const auto rel1 =
        mustId<ReleaseMbid>("51111111-1111-4111-8111-111111111111");
    const auto rel2 =
        mustId<ReleaseMbid>("52222222-2222-4222-8222-222222222222");
    const auto rec1 =
        mustId<RecordingMbid>("61111111-1111-4111-8111-111111111111");
    const auto rec2 =
        mustId<RecordingMbid>("62222222-2222-4222-8222-222222222222");
    const auto rt1 =
        mustId<ReleaseTrackMbid>("71111111-1111-4111-8111-111111111111");

    const auto artist = client.artistById(artistA);
    require(artist.ok(), "artist lookup by ArtistMbid should succeed");
    require(artist.value->mbid == artistA,
            "artist lookup must preserve canonical ArtistMbid");
    require(artist.value->name == QStringLiteral("宇多田ヒカル"),
            "non-Latin canonical artist metadata must be preserved");
    require(artist.value->aliases.size() == 2,
            "artist aliases must remain available as metadata");
    require(artist.value->aliases.at(0).name == QStringLiteral("Hikaru Utada"),
            "Latin alias must be preserved separately");

    const auto releaseGroupLookup = client.releaseGroupById(rg1);
    require(releaseGroupLookup.ok() && releaseGroupLookup.value->mbid == rg1,
            "release-group lookup by ReleaseGroupMbid should succeed");
    const auto releaseLookup = client.releaseById(rel1);
    require(releaseLookup.ok() && releaseLookup.value->mbid == rel1,
            "concrete release lookup by ReleaseMbid should succeed");
    const auto recordingLookup = client.recordingById(rec2);
    require(recordingLookup.ok() && recordingLookup.value->mbid == rec2,
            "recording lookup by RecordingMbid should succeed");

    const ArtistDiscographyQuery discographyQuery{};
    const auto page1 = client.artistDiscography(
        artistA, discographyQuery, PageRequest{2, std::nullopt});
    require(page1.ok(), "artist discography first page should succeed");
    require(page1.value->items.size() == 2,
            "artist discography page should honor limit");
    require(page1.value->items.at(0).mbid == rg1
                && page1.value->items.at(1).mbid == rg2,
            "discography ordering must be deterministic");
    require(page1.value->nextCursor.has_value(),
            "first discography page should expose an opaque continuation cursor");

    const auto page1Again = client.artistDiscography(
        artistA, discographyQuery, PageRequest{2, std::nullopt});
    require(page1Again.ok(), "repeated first page should succeed");
    require(page1Again.value->items.at(0).mbid == page1.value->items.at(0).mbid
                && page1Again.value->items.at(1).mbid == page1.value->items.at(1).mbid,
            "repeated query must produce the same ordered first page");
    require(page1Again.value->nextCursor == page1.value->nextCursor,
            "repeated query must produce the same continuation cursor");

    const auto page2 = client.artistDiscography(
        artistA,
        discographyQuery,
        PageRequest{2, page1.value->nextCursor});
    require(page2.ok(), "artist discography continuation should succeed");
    require(page2.value->items.size() == 2,
            "second discography page should contain remaining release groups");
    require(page2.value->items.at(0).mbid == rg3
                && page2.value->items.at(1).mbid == rg4,
            "second discography page should remain in canonical order");
    require(!page2.value->nextCursor.has_value(),
            "last discography page should not invent another cursor");

    ArtistDiscographyQuery epOnly;
    epOnly.releaseGroups.primaryTypes = {ReleaseGroupPrimaryType::EP};
    const auto epPage = client.artistDiscography(
        artistA, epOnly, PageRequest{10, std::nullopt});
    require(epPage.ok() && epPage.value->items.size() == 1
                && epPage.value->items.at(0).mbid == rg4,
            "discography filters must use neutral release-group metadata");

    const auto wrongQueryCursor = client.artistDiscography(
        artistA, epOnly, PageRequest{2, page1.value->nextCursor});
    require(!wrongQueryCursor.ok(),
            "cursor from one query must not be valid for a different query");
    require(wrongQueryCursor.error.code == CatalogueErrorCode::InvalidPage,
            "wrong-query cursor should return typed InvalidPage");

    const auto versions = client.releaseGroupReleases(
        rg1, ReleaseVersionQuery{}, PageRequest{10, std::nullopt});
    require(versions.ok(), "release group versions lookup should succeed");
    require(versions.value->items.size() == 2,
            "one logical release group should expose both concrete editions");
    require(versions.value->items.at(0).releaseGroupMbid == rg1
                && versions.value->items.at(1).releaseGroupMbid == rg1,
            "concrete releases must retain their logical release-group owner");
    require(versions.value->items.at(0).mbid == rel1
                && versions.value->items.at(1).mbid == rel2,
            "release group versions should preserve distinct ReleaseMbid values");

    ReleaseVersionQuery usOnly;
    usOnly.countries = {QStringLiteral("US")};
    const auto usVersions = client.releaseGroupReleases(
        rg1, usOnly, PageRequest{10, std::nullopt});
    require(usVersions.ok() && usVersions.value->items.size() == 1
                && usVersions.value->items.at(0).mbid == rel2,
            "version filters must not alter logical release-group identity");

    const auto tracklist = client.releaseTracklist(rel1);
    require(tracklist.ok(), "release tracklist lookup should succeed");
    require(tracklist.value->releaseMbid == rel1,
            "tracklist must remain bound to one concrete release");
    require(tracklist.value->media.size() == 2,
            "release must preserve multiple media/discs");
    require(tracklist.value->media.at(0).tracks.size() == 2,
            "release medium must preserve ordered tracks");

    const ReleaseTrack &firstTrack = tracklist.value->media.at(0).tracks.at(0);
    require(firstTrack.releaseTrackMbid.has_value(),
            "release track identity should be preserved when MusicBrainz has it");
    require(*firstTrack.releaseTrackMbid == rt1,
            "release-track MBID must remain its own identity");
    require(firstTrack.recordingMbid == rec1,
            "release track must retain canonical RecordingMbid separately");

    const ReleaseTrack &secondTrack = tracklist.value->media.at(0).tracks.at(1);
    require(!secondTrack.releaseTrackMbid.has_value(),
            "release-track MBID must remain explicitly optional");
    require(secondTrack.recordingMbid == rec2,
            "missing release-track identity must never erase RecordingMbid");

    const auto blueSearch = client.search(
        SearchQuery{QStringLiteral("Blue"), {}, {}},
        PageRequest{20, std::nullopt});
    require(blueSearch.ok(), "cross-entity search should succeed");
    require(blueSearch.value->items.size() == 3,
            "fixture Blue search should produce one hit per public search kind");
    require(blueSearch.value->items.at(0).kind == SearchEntityKind::Artist,
            "search hit must explicitly identify Artist kind");
    require(blueSearch.value->items.at(1).kind == SearchEntityKind::ReleaseGroup,
            "search hit must explicitly identify ReleaseGroup kind");
    require(blueSearch.value->items.at(2).kind == SearchEntityKind::Recording,
            "search hit must explicitly identify Recording kind");
    require(blueSearch.value->items.at(0).entity
                == CanonicalEntityRef::from(artistB),
            "search hit must return canonical entity identity");
    require(blueSearch.value->items.at(1).entity
                == CanonicalEntityRef::from(rg2),
            "release-group alias search must preserve ReleaseGroupMbid");
    require(blueSearch.value->items.at(1).canonicalText
                == QStringLiteral("青いアルバム")
                && blueSearch.value->items.at(1).matchedText
                == QStringLiteral("Blue Album"),
            "release-group alias must not overwrite canonical-script metadata");

    const auto aliasSearch = client.search(
        SearchQuery{QStringLiteral("Hikaru"),
                    {SearchEntityKind::Artist},
                    {}},
        PageRequest{20, std::nullopt});
    require(aliasSearch.ok() && aliasSearch.value->items.size() == 1,
            "artist alias should participate in search");
    require(aliasSearch.value->items.at(0).entity
                == CanonicalEntityRef::from(artistA),
            "alias match must resolve to canonical ArtistMbid");
    require(aliasSearch.value->items.at(0).canonicalText
                == QStringLiteral("宇多田ヒカル"),
            "alias match must not replace canonical non-Latin metadata");
    require(aliasSearch.value->items.at(0).matchedText
                == QStringLiteral("Hikaru Utada"),
            "search result should expose which alternate text matched");

    const auto recordingAliasSearch = client.search(
        SearchQuery{QStringLiteral("Hikari"),
                    {SearchEntityKind::Recording},
                    {}},
        PageRequest{20, std::nullopt});
    require(recordingAliasSearch.ok()
                && recordingAliasSearch.value->items.size() == 1,
            "recording alias should participate in search");
    require(recordingAliasSearch.value->items.at(0).entity
                == CanonicalEntityRef::from(rec2),
            "recording alias must resolve to canonical RecordingMbid");
    require(recordingAliasSearch.value->items.at(0).canonicalText
                == QStringLiteral("光"),
            "alternate script must not replace canonical recording identity/metadata");

    const auto appearances = client.recordingAppearances(
        rec2, PageRequest{10, std::nullopt});
    require(appearances.ok() && appearances.value->items.size() == 2,
            "recording appearances should enumerate concrete release occurrences");
    require(!appearances.value->items.at(0).releaseTrackMbid.has_value(),
            "appearance can preserve absent ReleaseTrackMbid without inventing one");
    require(appearances.value->items.at(1).releaseTrackMbid.has_value(),
            "appearance should preserve ReleaseTrackMbid when present");

    const auto relationships = client.recordingRelationships(rec2);
    require(relationships.ok() && relationships.value->size() == 1,
            "recording relationship seam should expose future-compatible relations");
    require(relationships.value->at(0).target.kind()
                == CanonicalEntityKind::Work,
            "Work may be represented as relationship without becoming catalogue UI");
    require(relationships.value->at(0).relationType
                == QStringLiteral("performance_of"),
            "relationship type should remain explicit");

    const auto oldRec2 = mustId<RecordingMbid>(
        "69999999-9999-4999-8999-999999999999");
    const auto redirectedRecording = client.recordingById(oldRec2);
    require(!redirectedRecording.ok() && redirectedRecording.redirected(),
            "merged Recording MBID must produce Redirected rather than NotFound");
    require(redirectedRecording.redirectedTo.has_value()
                && *redirectedRecording.redirectedTo == rec2,
            "Recording redirect must expose its canonical target");
    require(redirectedRecording.error.code == CatalogueErrorCode::None,
            "redirect is a first-class lookup outcome, not a catalogue failure");

    const auto oldRg1 = mustId<ReleaseGroupMbid>(
        "19999999-9999-4999-8999-999999999999");
    const auto redirectedRepresentative = client.representativeRelease(oldRg1);
    require(redirectedRepresentative.redirected()
                && redirectedRepresentative.redirectedTo.has_value()
                && *redirectedRepresentative.redirectedTo == rg1,
            "representative release lookup must surface a release-group redirect");

    const auto representative = client.representativeRelease(rg1);
    require(representative.ok(),
            "release group with canonical mapping should resolve a representative release");
    require(representative.value->mbid == rel1
                && representative.value->releaseGroupMbid == rg1,
            "representative release must stay a concrete Release under the requested Release Group");
    const auto noRepresentative = client.representativeRelease(rg2);
    require(noRepresentative.failed()
                && noRepresentative.error.code == CatalogueErrorCode::NotFound,
            "release group without canonical representative mapping must fail closed instead of guessing an edition");

    const auto missingRec = mustId<RecordingMbid>(
        "6fffffff-ffff-4fff-8fff-ffffffffffff");
    const QVector<CanonicalEntityRef> batchIds{
        CanonicalEntityRef::from(rec1),
        CanonicalEntityRef::from(oldRec2),
        CanonicalEntityRef::from(missingRec),
    };
    const auto batch = client.lookupCanonicalBatch(batchIds);
    require(batch.ok() && batch.value->size() == 3,
            "canonical batch lookup should preserve one result per requested identity");
    require(batch.value->at(0).requested == batchIds.at(0)
                && batch.value->at(0).state == CanonicalBatchLookupState::Found,
            "batch lookup must preserve deterministic request order for found identities");
    require(batch.value->at(0).displaySnapshot.has_value()
                && batch.value->at(0).displaySnapshot->primaryText
                    == QStringLiteral("Blue Song")
                && batch.value->at(0).displaySnapshot->artistCredit
                    == QStringList{QStringLiteral("Blue Ensemble")},
            "found batch item should carry non-authoritative last-known display text");
    require(batch.value->at(1).requested == batchIds.at(1)
                && batch.value->at(1).state == CanonicalBatchLookupState::Redirected
                && batch.value->at(1).redirectedTo.has_value()
                && *batch.value->at(1).redirectedTo == CanonicalEntityRef::from(rec2),
            "batch lookup must retain redirect target without silently following it");
    require(batch.value->at(2).requested == batchIds.at(2)
                && batch.value->at(2).state == CanonicalBatchLookupState::NotFound
                && !batch.value->at(2).displaySnapshot.has_value(),
            "batch lookup must distinguish NotFound from Redirected and Found");
    require(canonicalBatchLookupStateName(CanonicalBatchLookupState::Redirected)
                == QStringLiteral("redirected"),
            "batch lookup states should expose stable names");

    const auto unknown = mustId<ArtistMbid>(
        "ffffffff-ffff-4fff-8fff-ffffffffffff");
    const auto missing = client.artistById(unknown);
    require(!missing.ok(), "unknown canonical entity should fail");
    require(missing.error.code == CatalogueErrorCode::NotFound,
            "unknown canonical entity should return typed NotFound");

    const auto badSearch = client.search(
        SearchQuery{QStringLiteral("   "), {}, {}},
        PageRequest{20, std::nullopt});
    require(!badSearch.ok()
                && badSearch.error.code == CatalogueErrorCode::InvalidQuery,
            "blank search should return typed InvalidQuery");

    const auto badLimit = client.artistDiscography(
        artistA, discographyQuery, PageRequest{0, std::nullopt});
    require(!badLimit.ok()
                && badLimit.error.code == CatalogueErrorCode::InvalidQuery,
            "invalid page limit should return typed InvalidQuery");

    fake.setAvailable(false);
    const auto unavailableArtist = client.artistById(artistA);
    require(!unavailableArtist.ok()
                && unavailableArtist.error.code == CatalogueErrorCode::Unavailable,
            "service outage should return typed Unavailable");
    fake.setAvailable(true);

    require(catalogueErrorCodeName(CatalogueErrorCode::NotFound)
                == QStringLiteral("not_found"),
            "catalogue errors should have stable snake_case names");
    require(searchEntityKindName(SearchEntityKind::ReleaseGroup)
                == QStringLiteral("release_group"),
            "search entity kinds should have stable names");

    provePublicSurfaceHasNoImplementationLeaks();

    qInfo().noquote() << "VINYL_BG1_CATALOGUE_OK checks=" << g_checks;
    return 0;
}
