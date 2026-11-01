#include "ArtistServiceFactory.h"
#include "FakeArtistSource.h"

#include <QDate>
#include <QSignalSpy>
#include <QTest>
#include <cstdlib>
#include <utility>

using namespace vinyl::artist;

namespace {

const QString ARTIST_A_TEXT = QStringLiteral("b10bbbfc-cf9e-42e0-be17-e2c3e1d2600d");
const QString ARTIST_NO_ART_TEXT = QStringLiteral("535f74d5-8328-40ff-9cea-568ef916b3c8");
const QString REDIRECT_FROM_TEXT = QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc");

const QString GROUP_1_TEXT = QStringLiteral("11111111-1111-4111-8111-111111111111");
const QString GROUP_2_TEXT = QStringLiteral("22222222-2222-4222-8222-222222222222");
const QString GROUP_3_TEXT = QStringLiteral("33333333-3333-4333-8333-333333333333");
const QString GROUP_4_TEXT = QStringLiteral("44444444-4444-4444-8444-444444444444");

const QString RELEASE_1_TEXT = QStringLiteral("51111111-1111-4111-8111-111111111111");
const QString RELEASE_1_ALT_TEXT = QStringLiteral("51111112-1111-4111-8111-111111111111");
const QString RELEASE_2_TEXT = QStringLiteral("52222222-2222-4222-8222-222222222222");
const QString RELEASE_3_TEXT = QStringLiteral("53333333-3333-4333-8333-333333333333");
const QString RELEASE_4_TEXT = QStringLiteral("54444444-4444-4444-8444-444444444444");

const QString RECORDING_1_TEXT = QStringLiteral("61111111-1111-4111-8111-111111111111");
const QString RECORDING_2_TEXT = QStringLiteral("62222222-2222-4222-8222-222222222222");

const QString PROFILE_1 = QStringLiteral("profile-alpha");
const QString PROFILE_2 = QStringLiteral("profile-beta");

template<class T>
T mustParse(const QString &text) {
    const auto parsed = T::parse(text);
    if (!parsed.ok()) std::abort();
    return *parsed.value;
}

::vinyl::catalogue::artist_proof::SourceRef makeSourceRef(
    const char *providerText,
    const char *itemText,
    const char *provenanceText) {
    const auto p = mustParse<::vinyl::ProviderId>(QString::fromLatin1(providerText));
    const auto i = mustParse<::vinyl::ProviderItemId>(QString::fromLatin1(itemText));
    return {::vinyl::ProviderItemRef(p, i), QString::fromLatin1(provenanceText)};
}

::vinyl::artwork::PhotoCandidate makeHeroPhoto(
    const std::string &artistMbid,
    const std::string &assetId,
    const std::string &phashHex) {
    ::vinyl::artwork::PhotoCandidate c;
    c.artistMbid = artistMbid;
    c.provider = "SyntheticRightsFixture";
    c.providerAssetId = assetId;
    c.sourceUrl = "https://photos.example.test/" + assetId + ".jpg";
    c.kind = ::vinyl::artwork::PhotoKind::Hero;
    c.width = 1920;
    c.height = 1080;
    c.canonicalArtistConfirmed = true;
    c.actualPhotograph = true;
    c.cleanImage = true;
    c.subjectCropSafe = true;
    c.humanReviewedAsDistinctInArtistSet = true;
    c.phash64 = "phash64:" + phashHex;
    c.rights.status = ::vinyl::artwork::RightsStatus::VerifiedPermitted;
    c.rights.licenseId = "CC-BY-4.0-SYNTHETIC";
    c.rights.evidenceUrl = "https://rights.example.test/license/" + assetId;
    c.rights.author = "Synthetic Photographer";
    c.rights.attribution = "Photo by Synthetic Photographer";
    c.rights.artistHeroPermitted = true;
    c.rights.localCachePermitted = true;
    c.rights.validUntilEpoch = 500000;
    c.observedAtEpoch = 1000;
    return c;
}

::vinyl::catalogue::artist_proof::GroupRow buildGroupRow(
    const ::vinyl::ArtistMbid &artistMbid,
    const QString &artistName,
    const ::vinyl::ReleaseGroupMbid &groupMbid,
    const ::vinyl::ReleaseMbid &releaseMbid,
    const QString &title,
    int year,
    ::vinyl::catalogue::ReleaseGroupPrimaryType primaryType,
    const char *browseId,
    const ::vinyl::RecordingMbid &recordingMbid,
    const QString &recordingTitle) {
    using namespace ::vinyl::catalogue;
    using namespace ::vinyl::catalogue::artist_proof;

    const ArtistCredit credit{artistMbid, artistName};
    ReleaseGroup rg{groupMbid, title, {}, {credit}, primaryType, {}, year};
    Release rel{
        releaseMbid,
        groupMbid,
        title,
        {credit},
        QStringLiteral("Official"),
        QStringLiteral("US"),
        QDate(year, 1, 1),
        QString()
    };
    const SourceRef groupSrc = makeSourceRef("youtube-music", browseId, "mb-mapped-release-group");
    const SourcedCredit sourcedCredit{
        credit,
        makeSourceRef("youtube-music", "UC_artist_A", "mb-mapped-artist")
    };
    const SourcedRecording sourcedRec{
        Recording{recordingMbid, recordingTitle, {credit}, 215000, {}},
        makeSourceRef("youtube-music", "vid_rec_1", "mb-mapped-recording"),
        {sourcedCredit},
        {}
    };
    return GroupRow{rg, {rel}, {groupSrc}, {sourcedCredit}, {sourcedRec}};
}

void populateStandardSource(FakeArtistSource &source) {
    using namespace ::vinyl::catalogue;
    using namespace ::vinyl::catalogue::artist_proof;

    const auto artistA = mustParse<::vinyl::ArtistMbid>(ARTIST_A_TEXT);
    const auto artistNoArt = mustParse<::vinyl::ArtistMbid>(ARTIST_NO_ART_TEXT);
    const auto redirectFrom = mustParse<::vinyl::ArtistMbid>(REDIRECT_FROM_TEXT);

    const auto g1 = mustParse<::vinyl::ReleaseGroupMbid>(GROUP_1_TEXT);
    const auto g2 = mustParse<::vinyl::ReleaseGroupMbid>(GROUP_2_TEXT);
    const auto g3 = mustParse<::vinyl::ReleaseGroupMbid>(GROUP_3_TEXT);
    const auto g4 = mustParse<::vinyl::ReleaseGroupMbid>(GROUP_4_TEXT);

    const auto r1 = mustParse<::vinyl::ReleaseMbid>(RELEASE_1_TEXT);
    const auto r1Alt = mustParse<::vinyl::ReleaseMbid>(RELEASE_1_ALT_TEXT);
    const auto r2 = mustParse<::vinyl::ReleaseMbid>(RELEASE_2_TEXT);
    const auto r3 = mustParse<::vinyl::ReleaseMbid>(RELEASE_3_TEXT);
    const auto r4 = mustParse<::vinyl::ReleaseMbid>(RELEASE_4_TEXT);

    const auto rec1 = mustParse<::vinyl::RecordingMbid>(RECORDING_1_TEXT);
    const auto rec2 = mustParse<::vinyl::RecordingMbid>(RECORDING_2_TEXT);

    source.addArtist(ArtistRow{
        Artist{
            artistA,
            QStringLiteral("Lifafa"),
            QStringLiteral("Lifafa"),
            QStringLiteral("New Delhi electronic artist"),
            {Alias{QStringLiteral("Suryakant Sawhney"), {}, {}, {}, false}}
        },
        makeSourceRef("youtube-music", "UC_lifafa_channel", "mb-canonical-artist")
    });

    source.addArtist(ArtistRow{
        Artist{
            artistNoArt,
            QStringLiteral("Easy Wanderlings"),
            QStringLiteral("Easy Wanderlings"),
            QStringLiteral("Pune indie collective"),
            {}
        },
        makeSourceRef("youtube-music", "UC_easy_wanderlings", "mb-canonical-artist")
    });

    source.addRedirect(redirectFrom, artistA);

    GroupRow row1 = buildGroupRow(
        artistA, QStringLiteral("Lifafa"), g1, r1, QStringLiteral("Superpower 2020"), 2021,
        ReleaseGroupPrimaryType::Album, "MPREb_g1", rec1, QStringLiteral("Nikamma"));
    // Add a second edition on row1 that only contains rec2 (not rec1) so we can test both
    // edition->group mismatch and recording->edition mismatch.
    const ArtistCredit creditA{artistA, QStringLiteral("Lifafa")};
    row1.editions.push_back(Release{
        r1Alt, g1, QStringLiteral("Superpower 2020 (Bonus Edition)"), {creditA},
        QStringLiteral("Promotion"), QStringLiteral("IN"), QDate(2021, 6, 1), QString()
    });
    row1.recordings.push_back(SourcedRecording{
        Recording{rec2, QStringLiteral("Ek Nagma"), {creditA}, 240000, {}},
        makeSourceRef("youtube-music", "vid_rec_2", "mb-mapped-recording"),
        {{creditA, makeSourceRef("youtube-music", "UC_lifafa_channel", "mb-mapped-artist")}},
        {}
    });
    source.setEditionRecordings(r1, {rec1});
    source.setEditionRecordings(r1Alt, {rec2});

    GroupRow row2 = buildGroupRow(
        artistA, QStringLiteral("Lifafa"), g2, r2, QStringLiteral("Jaago"), 2019,
        ReleaseGroupPrimaryType::Album, "MPREb_g2", rec2, QStringLiteral("Jaago"));
    GroupRow row3 = buildGroupRow(
        artistA, QStringLiteral("Lifafa"), g3, r3, QStringLiteral("In Hi Ko"), 2017,
        ReleaseGroupPrimaryType::EP, "MPREb_g3", rec1, QStringLiteral("In Hi Ko"));
    GroupRow row4 = buildGroupRow(
        artistA, QStringLiteral("Lifafa"), g4, r4, QStringLiteral(" Wahin Ka Wahin"), 2023,
        ReleaseGroupPrimaryType::Single, "MPREb_g4", rec1, QStringLiteral("Wahin Ka Wahin"));

    source.setReleaseGroups(artistA, {row1, row2, row3, row4});

    // Media batch + trusted BF2 mappings for artistA
    const auto ytmProvider = mustParse<::vinyl::ProviderId>(QStringLiteral("youtube-music"));
    const auto videoItem = mustParse<::vinyl::ProviderItemId>(QStringLiteral("yt_official_mv_1"));
    const auto liveItem = mustParse<::vinyl::ProviderItemId>(QStringLiteral("yt_live_perf_1"));

    ::vinyl::artist_media::ProviderVideoRow officialVideoRow{
        ytmProvider,
        videoItem,
        QStringLiteral("Nikamma (Official Video)"),
        QStringLiteral("Lifafa Official"),
        ::vinyl::artist_media::VideoThumbnailProvenance{
            mustParse<::vinyl::ProviderId>(QStringLiteral("yt-img")),
            mustParse<::vinyl::ProviderItemId>(QStringLiteral("thumb_nikamma")),
            QStringLiteral("https://images.example.test/thumb_nikamma.jpg")
        },
        215000,
        900000,
        ::vinyl::RelatedVideoDiscoveryMethod::ProviderStructuredRelation,
        std::nullopt
    };

    ::vinyl::artist_media::ProviderVideoRow liveVideoRow{
        ytmProvider,
        liveItem,
        QStringLiteral("Lifafa Live at Boxout"),
        QStringLiteral("Lifafa Official"),
        std::nullopt,
        1800000,
        900000,
        ::vinyl::RelatedVideoDiscoveryMethod::CuratedMapping,
        std::nullopt
    };

    source.setMediaBatch(artistA, ::vinyl::artist_media::SourceBatch{
        artistA,
        ::vinyl::artist_media::QueryStatus::Ready,
        QStringLiteral("youtube-music-media"),
        QStringLiteral("media-snapshot-2026-10-15"),
        {officialVideoRow, liveVideoRow}
    });

    source.addMediaMapping(::vinyl::artist_media::VerifiedMediaMapping{
        artistA,
        rec1,
        ytmProvider,
        videoItem,
        ::vinyl::RelatedVideoRelationshipKind::OfficialMusicVideo,
        {
            {::vinyl::RelationshipEvidenceKind::CanonicalMapping,
             ::vinyl::RelationshipEvidenceStrength::Strong,
             QStringLiteral("MusicBrainz URL relation")},
            {::vinyl::RelationshipEvidenceKind::ProviderOfficialDesignation,
             ::vinyl::RelationshipEvidenceStrength::Strong,
             QStringLiteral("Verified OAC official music video")}
        },
        QStringLiteral("mb+ytm-verified-map:v1")
    });

    source.addMediaMapping(::vinyl::artist_media::VerifiedMediaMapping{
        artistA,
        std::nullopt,
        ytmProvider,
        liveItem,
        ::vinyl::RelatedVideoRelationshipKind::LivePerformance,
        {
            {::vinyl::RelationshipEvidenceKind::CuratedMapping,
             ::vinyl::RelationshipEvidenceStrength::Strong,
             QStringLiteral("Curated live session")}
        },
        QStringLiteral("curated-live-map:v1")
    });

    // Artwork payload with 2 verified distinct hero photos for artistA (Crossfade mode)
    ArtworkSourcePayload artA;
    artA.available = true;
    artA.authority = QStringLiteral("theaudiodb+commons");
    artA.sourceItemId = QStringLiteral("artwork:lifafa");
    artA.revision = QStringLiteral("artwork-rev-2026-10-15");
    artA.artistName = QStringLiteral("Lifafa");
    artA.nowEpoch = 1000;
    artA.fetched = {
        makeHeroPhoto(ARTIST_A_TEXT.toStdString(), "hero-01", "0123456789abcdef"),
        makeHeroPhoto(ARTIST_A_TEXT.toStdString(), "hero-02", "fedcba9876543210")
    };
    source.setArtworkPayload(artistA, std::move(artA));

    // Artwork payload with ZERO photos for artistNoArt (deterministic fallback composition)
    ArtworkSourcePayload artEmpty;
    artEmpty.available = true;
    artEmpty.authority = QStringLiteral("theaudiodb+commons");
    artEmpty.sourceItemId = QStringLiteral("artwork:easy-wanderlings");
    artEmpty.revision = QStringLiteral("artwork-rev-2026-10-15");
    artEmpty.artistName = QStringLiteral("Easy Wanderlings");
    artEmpty.nowEpoch = 1000;
    source.setArtworkPayload(artistNoArt, std::move(artEmpty));
}

// A request-scoped transport double: cancellation really suppresses delivery.
class CancellingSource final : public IArtistSource {
public:
    FakeArtistSource data;
    QSet<quint64> cancelledCatalogue;
    QSet<quint64> activeMedia;
    void lookupArtist(const ::vinyl::catalogue::artist_proof::ArtistSelector &selector,
                      std::uint64_t ticket,
                      std::function<void(::vinyl::catalogue::artist_proof::ArtistProviderResult)> done) override {
        data.lookupArtist(selector, ticket, [this, ticket, done](auto r) {
            if (!cancelledCatalogue.contains(ticket)) done(std::move(r));
        });
    }
    void fetchReleaseGroups(const ::vinyl::ArtistMbid &artist, std::uint64_t ticket,
                            std::function<void(::vinyl::catalogue::artist_proof::GroupsProviderResult)> done) override {
        data.fetchReleaseGroups(artist, ticket, [this, ticket, done](auto r) {
            if (!cancelledCatalogue.contains(ticket)) done(std::move(r));
        });
    }
    void cancelCatalogue(std::uint64_t ticket) noexcept override { cancelledCatalogue.insert(ticket); }
    void fetchMedia(const ::vinyl::ArtistMbid &artist, std::uint64_t ticket,
                    std::function<void(::vinyl::artist_media::SourceBatch)> done) override {
        activeMedia.insert(ticket);
        data.fetchMedia(artist, ticket, [this, ticket, done](auto r) {
            if (activeMedia.remove(ticket)) done(std::move(r));
        });
    }
    void cancelMedia(std::uint64_t ticket) noexcept override { activeMedia.remove(ticket); }
    std::optional<::vinyl::artist_media::VerifiedMediaMapping> lookupMediaMapping(
        const ::vinyl::ArtistMbid &a, const ::vinyl::ProviderItemRef &p) const override {
        return data.lookupMediaMapping(a, p);
    }
    void fetchArtwork(const ::vinyl::ArtistMbid &a, std::uint64_t t,
                      std::function<void(ArtworkSourcePayload)> done) override {
        data.fetchArtwork(a, t, std::move(done));
    }
    void cancelArtwork(std::uint64_t t) noexcept override { data.cancelArtwork(t); }
    bool hasReleaseInGroup(const ::vinyl::ArtistMbid &a, const ::vinyl::ReleaseGroupMbid &g,
                           const ::vinyl::ReleaseMbid &r) const override {
        return data.hasReleaseInGroup(a, g, r);
    }
    bool hasRecordingInRelease(const ::vinyl::ArtistMbid &a, const ::vinyl::ReleaseGroupMbid &g,
                               const ::vinyl::ReleaseMbid &r, const ::vinyl::RecordingMbid &rec) const override {
        return data.hasRecordingInRelease(a, g, r, rec);
    }
};

} // namespace

class ArtistServiceComposedTests : public QObject {
    Q_OBJECT
private slots:
    void catalogueCancellationDoesNotCancelAnotherLane();
    void mediaCancellationUsesTransportIdentityAfterGenerationGap();
    void selectionRequiresEditionRecordingMapping();
    void selectionRejectsRepeatedRecordingOccurrences();
    void offlineSourceAndIntegerLengthProjection();
    void occurrenceProjectionPreservesEditionCoordinates();
    void enterArtistAndLoadAllLanesToReady();
    void redirectedMbidReturnsTypedRedirectedError();
    void invalidCursorIsRejected();
    void switchingProfileClearsSavedState();
    void lateReplyAfterLeaveArtistIsDroppedByGenerationFence();
    void togglingSavedRoundTripsAndSurvivesReload();
    void selectRecordingEmitsPlayRequestAndRefusesMismatchedEdition();
    void artistWithNoArtworkReturnsDeterministicFallbackNotFakeImage();
};

void ArtistServiceComposedTests::enterArtistAndLoadAllLanesToReady() {
    FakeArtistSource source;
    populateStandardSource(source);

    auto service = createComposedArtistService(source, /*discographyPageSize=*/2);
    ArtistServiceBridge &bridge = service->bridge();

    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QVERIFY(bridge.active());
    QCOMPARE(bridge.profileScopeId(), PROFILE_1);
    QCOMPARE(bridge.artistMbid(), ARTIST_A_TEXT);
    QCOMPARE(bridge.savedState(), State::Ready);
    QVERIFY(!bridge.saved());

    // 1. Artist lane -> Ready
    QVERIFY(bridge.requestArtist(ARTIST_A_TEXT));
    QCOMPARE(bridge.artistState(), State::Ready);
    QCOMPARE(bridge.artist().value(QStringLiteral("artistMbid")).toString(), ARTIST_A_TEXT);
    QCOMPARE(bridge.artist().value(QStringLiteral("name")).toString(), QStringLiteral("Lifafa"));

    // 2. Discography Page 1 (2 items) -> Ready + opaque nextCursor
    QVERIFY(bridge.requestDiscography({}, QStringLiteral("all")));
    QCOMPARE(bridge.discographyState(), State::Ready);
    QCOMPARE(bridge.discography().size(), 2);
    const QString page1Cursor = bridge.nextCursor();
    QVERIFY(!page1Cursor.isEmpty());
    QVERIFY(page1Cursor.startsWith(QStringLiteral("ac1.")));

    // 3. Discography Page 2 (remaining 2 items) -> Ready + nextCursor empty
    QVERIFY(bridge.requestDiscography(page1Cursor, QStringLiteral("all")));
    QCOMPARE(bridge.discographyState(), State::Ready);
    QCOMPARE(bridge.discography().size(), 4);
    QVERIFY(bridge.nextCursor().isEmpty());

    // 4. Media lane -> Ready (1 official video + 1 live performance)
    QVERIFY(bridge.requestMedia());
    QCOMPARE(bridge.mediaState(), State::Ready);
    QCOMPARE(bridge.media().value(QStringLiteral("videoCount")).toInt(), 1);
    QCOMPARE(bridge.media().value(QStringLiteral("liveCount")).toInt(), 1);

    // 5. Artwork lane -> Ready (2 hero slides -> crossfade mode)
    QVERIFY(bridge.requestArtwork());
    QCOMPARE(bridge.artworkState(), State::Ready);
    QCOMPARE(bridge.artwork().value(QStringLiteral("mode")).toString(), QStringLiteral("crossfade"));
    QCOMPARE(bridge.artwork().value(QStringLiteral("slides")).toList().size(), 2);
}

void ArtistServiceComposedTests::redirectedMbidReturnsTypedRedirectedError() {
    FakeArtistSource source;
    populateStandardSource(source);

    auto service = createComposedArtistService(source);
    ArtistServiceBridge &bridge = service->bridge();

    QVERIFY(bridge.enterArtist(PROFILE_1, REDIRECT_FROM_TEXT));
    QVERIFY(bridge.requestArtist(REDIRECT_FROM_TEXT));
    QCOMPARE(bridge.artistState(), State::Error);
    QCOMPARE(bridge.result(Channel::Artist).value(QStringLiteral("errorCode")).toString(),
             QStringLiteral("redirected"));
    QVERIFY(bridge.result(Channel::Artist).value(QStringLiteral("message")).toString()
                .contains(ARTIST_A_TEXT));

    QVERIFY(bridge.requestDiscography({}, QStringLiteral("all")));
    QCOMPARE(bridge.discographyState(), State::Error);
    QCOMPARE(bridge.result(Channel::Discography).value(QStringLiteral("errorCode")).toString(),
             QStringLiteral("redirected"));
}

void ArtistServiceComposedTests::invalidCursorIsRejected() {
    FakeArtistSource source;
    populateStandardSource(source);

    auto service = createComposedArtistService(source, /*discographyPageSize=*/2);
    ArtistServiceBridge &bridge = service->bridge();
    QSignalSpy rejectedSpy(&bridge, &ArtistServiceBridge::rejected);

    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QVERIFY(bridge.requestDiscography({}, QStringLiteral("all")));
    QCOMPARE(bridge.discographyState(), State::Ready);
    QCOMPARE(bridge.discography().size(), 2);
    const QString validCursor = bridge.nextCursor();
    QVERIFY(!validCursor.isEmpty());

    // Bridge-level rejection of an invented/tampered cursor string
    QVERIFY(!bridge.requestDiscography(QStringLiteral("ac1.invalid.cursor"), QStringLiteral("all")));
    QCOMPARE(rejectedSpy.count(), 1);
    QCOMPARE(rejectedSpy.last().at(1).toString(), QStringLiteral("invalid_page"));
    QCOMPARE(bridge.discographyState(), State::Ready);
    QCOMPARE(bridge.discography().size(), 2);

    // Adapter/catalogue-level rejection when the cursor's bound corpus revision no longer matches
    source.setCorpusVersion(QStringLiteral("mb-corpus-mutated-2026-10-16"));
    QVERIFY(bridge.requestDiscography(validCursor, QStringLiteral("all")));
    QCOMPARE(bridge.discographyState(), State::Error);
    QCOMPARE(bridge.result(Channel::Discography).value(QStringLiteral("errorCode")).toString(),
             QStringLiteral("invalid_page"));

    // Also verify direct read-port rejection of a corrupted cursor token
    ReadRequest directReq;
    directReq.channel = Channel::Discography;
    directReq.token = 999;
    directReq.generation = 1;
    directReq.profileScopeId = PROFILE_1;
    directReq.artistMbid = ARTIST_A_TEXT;
    directReq.cursor = QStringLiteral("ac1.tampered_payload.0000000000000000");
    directReq.filter = QStringLiteral("all");
    std::optional<ReadReply> directReply;
    service->readAdapter().request(directReq, [&](ReadReply r) { directReply = std::move(r); });
    QVERIFY(directReply.has_value());
    QCOMPARE(directReply->state, State::Error);
    QCOMPARE(directReply->errorCode, QStringLiteral("invalid_page"));
}

void ArtistServiceComposedTests::switchingProfileClearsSavedState() {
    FakeArtistSource source;
    populateStandardSource(source);
    InMemoryPersonalBytesPort personalStore;

    auto service = createComposedArtistService(source, personalStore);
    ArtistServiceBridge &bridge = service->bridge();

    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QCOMPARE(bridge.savedState(), State::Ready);
    QVERIFY(!bridge.saved());
    QVERIFY(bridge.toggleSavedArtist());
    QCOMPARE(bridge.savedState(), State::Ready);
    QVERIFY(bridge.saved());

    // Switching to PROFILE_2 clears active context and saved state immediately
    QVERIFY(bridge.setProfile(PROFILE_2));
    QVERIFY(!bridge.active());
    QCOMPARE(bridge.profileScopeId(), PROFILE_2);
    QCOMPARE(bridge.savedState(), State::Idle);
    QVERIFY(!bridge.saved());

    // Entering the same artist under PROFILE_2 reflects PROFILE_2's unsaved state
    QVERIFY(bridge.enterArtist(PROFILE_2, ARTIST_A_TEXT));
    QCOMPARE(bridge.savedState(), State::Ready);
    QVERIFY(!bridge.saved());
}

void ArtistServiceComposedTests::lateReplyAfterLeaveArtistIsDroppedByGenerationFence() {
    FakeArtistSource source;
    populateStandardSource(source);
    source.setDeferredDelivery(true);

    auto service = createComposedArtistService(source);
    ArtistServiceBridge &bridge = service->bridge();

    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QVERIFY(bridge.requestArtist(ARTIST_A_TEXT));
    QVERIFY(bridge.requestDiscography({}, QStringLiteral("all")));
    QVERIFY(bridge.requestMedia());
    QVERIFY(bridge.requestArtwork());

    QCOMPARE(bridge.artistState(), State::Loading);
    QCOMPARE(bridge.discographyState(), State::Loading);
    QCOMPARE(bridge.mediaState(), State::Loading);
    QCOMPARE(bridge.artworkState(), State::Loading);
    QVERIFY(source.pendingCallbackCount() > 0);

    // Leave artist before any provider callbacks arrive
    bridge.leaveArtist();
    QVERIFY(!bridge.active());
    QCOMPARE(bridge.artistState(), State::Idle);
    QCOMPARE(bridge.discographyState(), State::Idle);
    QCOMPARE(bridge.mediaState(), State::Idle);
    QCOMPARE(bridge.artworkState(), State::Idle);

    // Deliver all queued callbacks late; every lane must remain Idle and empty
    source.deliverAllPending();
    QVERIFY(!bridge.active());
    QCOMPARE(bridge.artistState(), State::Idle);
    QCOMPARE(bridge.discographyState(), State::Idle);
    QCOMPARE(bridge.mediaState(), State::Idle);
    QCOMPARE(bridge.artworkState(), State::Idle);
    QVERIFY(bridge.artist().isEmpty());
    QVERIFY(bridge.discography().isEmpty());
    QVERIFY(bridge.media().isEmpty());
    QVERIFY(bridge.artwork().isEmpty());
}

void ArtistServiceComposedTests::togglingSavedRoundTripsAndSurvivesReload() {
    FakeArtistSource source;
    populateStandardSource(source);
    InMemoryPersonalBytesPort sharedStore;

    {
        auto firstInstance = createComposedArtistService(source, sharedStore);
        ArtistServiceBridge &bridge = firstInstance->bridge();

        QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
        QCOMPARE(bridge.savedState(), State::Ready);
        QVERIFY(!bridge.saved());

        QVERIFY(bridge.toggleSavedArtist());
        QCOMPARE(bridge.savedState(), State::Ready);
        QVERIFY(bridge.saved());
        QVERIFY(sharedStore.hasSnapshot(PROFILE_1));
    }

    // Simulate reload by constructing a brand-new service over the same persistent byte store
    {
        auto reloadedInstance = createComposedArtistService(source, sharedStore);
        ArtistServiceBridge &reloadedBridge = reloadedInstance->bridge();

        QVERIFY(reloadedBridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
        QCOMPARE(reloadedBridge.savedState(), State::Ready);
        QVERIFY(reloadedBridge.saved());

        // Toggle off (unsave) and verify it persists across another reload
        QVERIFY(reloadedBridge.toggleSavedArtist());
        QCOMPARE(reloadedBridge.savedState(), State::Ready);
        QVERIFY(!reloadedBridge.saved());
    }

    {
        auto thirdInstance = createComposedArtistService(source, sharedStore);
        ArtistServiceBridge &thirdBridge = thirdInstance->bridge();

        QVERIFY(thirdBridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
        QCOMPARE(thirdBridge.savedState(), State::Ready);
        QVERIFY(!thirdBridge.saved());
    }
}

void ArtistServiceComposedTests::selectRecordingEmitsPlayRequestAndRefusesMismatchedEdition() {
    FakeArtistSource source;
    populateStandardSource(source);

    auto service = createComposedArtistService(source, /*discographyPageSize=*/4);
    ArtistServiceBridge &bridge = service->bridge();
    RecordingPlaybackPort &playback = service->playbackPort();
    QSignalSpy playSpy(&playback, &RecordingPlaybackPort::playRequested);

    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QVERIFY(bridge.requestArtist(ARTIST_A_TEXT));
    QVERIFY(bridge.requestDiscography({}, QStringLiteral("all")));
    QCOMPARE(bridge.artistState(), State::Ready);
    QCOMPARE(bridge.discographyState(), State::Ready);

    // Valid selection: GROUP_1 -> RELEASE_1 -> RECORDING_1
    QVERIFY(bridge.selectRecording(ARTIST_A_TEXT, GROUP_1_TEXT, RELEASE_1_TEXT, RECORDING_1_TEXT));
    QVERIFY(bridge.playbackError().isEmpty());
    QCOMPARE(playSpy.count(), 1);
    QCOMPARE(playback.playRequests().size(), 1);

    const PlayRequest emitted = playback.playRequests().first();
    QCOMPARE(emitted.profileScopeId(), PROFILE_1);
    QCOMPARE(emitted.artistMbid(), ARTIST_A_TEXT);
    QCOMPARE(emitted.releaseGroupMbid(), GROUP_1_TEXT);
    QCOMPARE(emitted.releaseMbid(), RELEASE_1_TEXT);
    QCOMPARE(emitted.recordingMbid(), RECORDING_1_TEXT);

    // Mismatched edition: RELEASE_2 belongs to GROUP_2, not GROUP_1
    QVERIFY(!bridge.selectRecording(ARTIST_A_TEXT, GROUP_1_TEXT, RELEASE_2_TEXT, RECORDING_1_TEXT));
    QVERIFY(!bridge.playbackError().isEmpty());
    QCOMPARE(playSpy.count(), 1);
    QCOMPARE(playback.playRequests().size(), 1);

    // Mismatched recording/edition link: RELEASE_1_ALT belongs to GROUP_1, but only has RECORDING_2
    QVERIFY(!bridge.selectRecording(
        ARTIST_A_TEXT, GROUP_1_TEXT, RELEASE_1_ALT_TEXT, RECORDING_1_TEXT));
    QVERIFY(!bridge.playbackError().isEmpty());
    QCOMPARE(playSpy.count(), 1);
    QCOMPARE(playback.playRequests().size(), 1);
}

void ArtistServiceComposedTests::artistWithNoArtworkReturnsDeterministicFallbackNotFakeImage() {
    FakeArtistSource source;
    populateStandardSource(source);

    auto service = createComposedArtistService(source);
    ArtistServiceBridge &bridge = service->bridge();

    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_NO_ART_TEXT));
    QVERIFY(bridge.requestArtwork());
    QCOMPARE(bridge.artworkState(), State::Ready);

    const QVariantMap art1 = bridge.artwork();
    QCOMPARE(art1.value(QStringLiteral("artistMbid")).toString(), ARTIST_NO_ART_TEXT);
    QCOMPARE(art1.value(QStringLiteral("mode")).toString(), QStringLiteral("composition"));
    QVERIFY(art1.value(QStringLiteral("slides")).toList().isEmpty());
    QCOMPARE(art1.value(QStringLiteral("slideCount")).toInt(), 0);

    const QVariantMap fallback1 = art1.value(QStringLiteral("fallback")).toMap();
    QCOMPARE(fallback1.value(QStringLiteral("artistMbid")).toString(), ARTIST_NO_ART_TEXT);
    QCOMPARE(fallback1.value(QStringLiteral("name")).toString(),
             QStringLiteral("Easy Wanderlings"));
    QCOMPARE(fallback1.value(QStringLiteral("monogram")).toString(), QStringLiteral("EW"));
    const qulonglong seed1 = fallback1.value(QStringLiteral("layoutSeed")).toULongLong();
    QVERIFY(seed1 != 0ULL);
    QVERIFY(!fallback1.value(QStringLiteral("hasPortrait")).toBool());

    // Re-requesting artwork produces the exact same deterministic fallback seed and monogram
    QVERIFY(bridge.requestArtwork());
    QCOMPARE(bridge.artworkState(), State::Ready);
    const QVariantMap fallback2 = bridge.artwork().value(QStringLiteral("fallback")).toMap();
    QCOMPARE(fallback2.value(QStringLiteral("monogram")).toString(), QStringLiteral("EW"));
    QCOMPARE(fallback2.value(QStringLiteral("layoutSeed")).toULongLong(), seed1);
}

void ArtistServiceComposedTests::catalogueCancellationDoesNotCancelAnotherLane() {
    CancellingSource source; populateStandardSource(source.data);
    source.data.setDeferredDelivery(true);
    auto service = createComposedArtistService(source);
    auto &bridge = service->bridge();
    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QVERIFY(bridge.requestArtist(ARTIST_A_TEXT));
    QVERIFY(bridge.requestDiscography());
    QVERIFY(bridge.requestArtist(ARTIST_A_TEXT)); // cancels only Artist lookup
    source.data.deliverAllPending();
    source.data.deliverAllPending(); // discography's second-stage page
    QCOMPARE(bridge.artistState(), State::Ready);
    QCOMPARE(bridge.discographyState(), State::Ready);
}
void ArtistServiceComposedTests::mediaCancellationUsesTransportIdentityAfterGenerationGap() {
    CancellingSource source; populateStandardSource(source.data);
    source.data.setDeferredDelivery(true);
    auto service = createComposedArtistService(source);
    auto &bridge = service->bridge();
    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QVERIFY(bridge.requestMedia());
    QVERIFY(bridge.requestMedia()); // cancel increments query generation independently
    QCOMPARE(source.activeMedia.size(), 1);
    bridge.leaveArtist();
    QVERIFY2(source.activeMedia.isEmpty(), "leaving must cancel the actual outstanding transport");
}
void ArtistServiceComposedTests::selectionRequiresEditionRecordingMapping() {
    FakeArtistSource source; populateStandardSource(source);
    auto service = createComposedArtistService(source, 4);
    auto &bridge = service->bridge();
    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QVERIFY(bridge.requestArtist(ARTIST_A_TEXT));
    QVERIFY(bridge.requestDiscography());
    // GROUP_3 lists a recording but supplies no authoritative edition tracklist.
    QVERIFY(!bridge.selectRecording(ARTIST_A_TEXT, GROUP_3_TEXT, RELEASE_3_TEXT, RECORDING_1_TEXT));
}
void ArtistServiceComposedTests::selectionRejectsRepeatedRecordingOccurrences() {
    FakeArtistSource source; populateStandardSource(source);
    auto service = createComposedArtistService(source, 4);
    auto &bridge = service->bridge();
    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QVERIFY(bridge.requestArtist(ARTIST_A_TEXT));
    QVERIFY(bridge.requestDiscography());
    const auto rec = mustParse<::vinyl::RecordingMbid>(RECORDING_1_TEXT);
    source.setEditionRecordings(mustParse<::vinyl::ReleaseMbid>(RELEASE_1_TEXT), {rec, rec});
    QVERIFY2(!bridge.selectRecording(ARTIST_A_TEXT, GROUP_1_TEXT, RELEASE_1_TEXT, RECORDING_1_TEXT),
             "repeated recording occurrences require an explicit occurrence selection");
    source.setEditionRecordings(mustParse<::vinyl::ReleaseMbid>(RELEASE_1_TEXT), {rec});
    QVERIFY(bridge.selectRecording(ARTIST_A_TEXT, GROUP_1_TEXT, RELEASE_1_TEXT, RECORDING_1_TEXT));
}
void ArtistServiceComposedTests::offlineSourceAndIntegerLengthProjection() {
    FakeArtistSource source; populateStandardSource(source);
    auto service = createComposedArtistService(source, 4);
    auto &bridge = service->bridge();
    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QVERIFY(bridge.requestDiscography());
    const auto recordings = bridge.discography().first().toMap().value("recordings").toList();
    QVERIFY(!recordings.isEmpty());
    QCOMPARE(recordings.first().toMap().value("lengthMs").toLongLong(), qlonglong(215000));
    auto unknownLength = buildGroupRow(mustParse<::vinyl::ArtistMbid>(ARTIST_A_TEXT), QStringLiteral("Lifafa"),
        mustParse<::vinyl::ReleaseGroupMbid>(GROUP_1_TEXT), mustParse<::vinyl::ReleaseMbid>(RELEASE_1_TEXT),
        QStringLiteral("Album"), 2000, ::vinyl::catalogue::ReleaseGroupPrimaryType::Album,
        "unknown-length-group", mustParse<::vinyl::RecordingMbid>(RECORDING_1_TEXT), QStringLiteral("Song"));
    unknownLength.recordings[0].canonical.lengthMs = 0;
    source.setReleaseGroups(mustParse<::vinyl::ArtistMbid>(ARTIST_A_TEXT), {unknownLength});
    QVERIFY(bridge.requestDiscography());
    const auto unknown = bridge.discography().first().toMap().value("recordings").toList().first().toMap();
    QVERIFY(!unknown.value("lengthMs").isValid());
    source.setOnline(false);
    QVERIFY(bridge.requestMedia());
    QCOMPARE(bridge.mediaState(), State::Unavailable);
}

void ArtistServiceComposedTests::occurrenceProjectionPreservesEditionCoordinates() {
    FakeArtistSource source;
    const auto artist = mustParse<::vinyl::ArtistMbid>(ARTIST_A_TEXT);
    const auto group = mustParse<::vinyl::ReleaseGroupMbid>(GROUP_1_TEXT);
    const auto release = mustParse<::vinyl::ReleaseMbid>(RELEASE_1_TEXT);
    const auto recording = mustParse<::vinyl::RecordingMbid>(RECORDING_1_TEXT);
    auto row = buildGroupRow(artist, QStringLiteral("Lifafa"), group, release, QStringLiteral("Album"),
        2000, ::vinyl::catalogue::ReleaseGroupPrimaryType::Album, "group-feed", recording, QStringLiteral("Song"));
    const auto t1 = mustParse<::vinyl::ReleaseTrackMbid>(QStringLiteral("71111111-1111-4111-8111-111111111111"));
    const auto t2 = mustParse<::vinyl::ReleaseTrackMbid>(QStringLiteral("72222222-2222-4222-8222-222222222222"));
    row.occurrences = {{*::vinyl::ReleaseTrackCoordinate::create(group, release, t1, recording, 1, 2).value, std::nullopt},
                       {*::vinyl::ReleaseTrackCoordinate::create(group, release, t2, recording, 2, 3).value, std::nullopt}};
    source.addArtist({::vinyl::catalogue::Artist{artist, QStringLiteral("Lifafa"), {}, {}, {}}, std::nullopt});
    source.setReleaseGroups(artist, {row});
    auto service = createComposedArtistService(source);
    auto &bridge = service->bridge();
    QVERIFY(bridge.enterArtist(PROFILE_1, ARTIST_A_TEXT));
    QVERIFY(bridge.requestDiscography());
    QCOMPARE(bridge.discographyState(), State::Ready);
    const auto items = bridge.discography().first().toMap().value("occurrences").toList();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).toMap().value("recordingMbid").toString(), RECORDING_1_TEXT);
    QCOMPARE(items.at(1).toMap().value("recordingMbid").toString(), RECORDING_1_TEXT);
    QCOMPARE(items.at(0).toMap().value("releaseTrackMbid").toString(), t1.toString());
    QCOMPARE(items.at(1).toMap().value("releaseTrackMbid").toString(), t2.toString());
    QCOMPARE(items.at(0).toMap().value("mediumPosition").toInt(), 1);
    QCOMPARE(items.at(1).toMap().value("mediumPosition").toInt(), 2);
    QCOMPARE(items.at(1).toMap().value("trackPosition").toInt(), 3);
    ExactSelection selected{{PROFILE_1}, {ARTIST_A_TEXT}, {GROUP_1_TEXT}, {RELEASE_1_TEXT}, {RECORDING_1_TEXT}};
    QVERIFY(!service->playbackPort().selectRecording(selected).accepted);
    row.occurrences.removeLast();
    source.setReleaseGroups(artist, {row});
    QVERIFY(service->playbackPort().selectRecording(selected).accepted);
}

QTEST_GUILESS_MAIN(ArtistServiceComposedTests)
#include "test_artist_service_composed.moc"
