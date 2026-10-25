#include "ArtistMediaQuery.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vinyl;
using namespace vinyl::artist_media;
using Kind = RelatedVideoRelationshipKind;
using EK = RelationshipEvidenceKind;
using ES = RelationshipEvidenceStrength;

namespace {
ArtistMbid artistA() {
    return *ArtistMbid::parse("11111111-1111-4111-8111-111111111111").value;
}
ArtistMbid artistB() {
    return *ArtistMbid::parse("22222222-2222-4222-8222-222222222222").value;
}
RecordingMbid recA() {
    return *RecordingMbid::parse("33333333-3333-4333-8333-333333333333").value;
}
RecordingMbid recB() {
    return *RecordingMbid::parse("44444444-4444-4444-8444-444444444444").value;
}
ProviderId provider(const char *id = "youtube-music") {
    return *ProviderId::parse(QString::fromLatin1(id)).value;
}
ProviderItemId item(const char *id) {
    return *ProviderItemId::parse(QString::fromLatin1(id)).value;
}
QString artistText() { return artistA().toString(); }
QString recText() { return recA().toString(); }

void expect(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
QVector<RelationshipEvidence> officialEvidence() {
    return {{EK::CanonicalMapping, ES::Strong, QStringLiteral("recording link")},
            {EK::ProviderOfficialDesignation, ES::Strong, QStringLiteral("verified provider official flag")}};
}
QVector<RelationshipEvidence> curatedEvidence() {
    return {{EK::CuratedMapping, ES::Strong, QStringLiteral("exact curated item relation")}};
}
QVector<RelationshipEvidence> weakEvidence() {
    return {{EK::TextSearchSimilarity, ES::Weak, QStringLiteral("similar-looking title")}};
}
ProviderVideoRow row(const char *id, QString title = QStringLiteral("Video")) {
    return {provider(), item(id), std::move(title), QStringLiteral("source channel"),
            std::nullopt, std::nullopt, std::nullopt,
            RelatedVideoDiscoveryMethod::ProviderSearch, std::nullopt};
}
VerifiedMediaMapping mapping(const char *id, Kind kind,
                             std::optional<RecordingMbid> rec,
                             QVector<RelationshipEvidence> ev = curatedEvidence()) {
    return {artistA(), std::move(rec), provider(), item(id), kind,
            std::move(ev), QStringLiteral("trusted-exact-map:v1")};
}
SourceBatch batch(const ArtistMbid &a, QVector<ProviderVideoRow> rows) {
    return {a, QueryStatus::Ready, QStringLiteral("YouTube Music local fixture"),
            QStringLiteral("2026-10-14/source-snapshot"), std::move(rows)};
}
struct Source final : ArtistMediaSource {
    QVector<ArtistMbid> requests;
    std::vector<Reply> replies;
    bool failFetch = false;
    void fetch(const ArtistMbid &artist, Reply reply) override {
        if (failFetch) throw std::runtime_error("synthetic provider failure");
        requests.append(artist);
        replies.push_back(std::move(reply));
    }
    void deliver(size_t slot, SourceBatch b) { replies.at(slot)(std::move(b)); }
};
struct Maps final : ArtistMediaMappingPort {
    std::vector<VerifiedMediaMapping> entries;
    bool throwLookup = false;
    std::optional<VerifiedMediaMapping> lookup(
        const ArtistMbid &, const ProviderItemRef &p) const override {
        if (throwLookup) throw std::runtime_error("synthetic mapping-store failure");
        for (const auto &m : entries) {
            if (m.provider == p.providerId() && m.providerItem == p.itemId())
                return m; // Service still enforces artist scope itself.
        }
        return std::nullopt;
    }
};
ArtistMediaResult runQuery(Source &src, Maps &maps, SourceBatch b) {
    ArtistMediaQuery service(src, maps);
    std::optional<ArtistMediaResult> answer;
    service.start(artistText(), [&](const ArtistMediaResult &r) { answer = r; });
    src.deliver(0, std::move(b));
    expect(answer.has_value(), "delivery callback missing");
    return *answer;
}

void unknownEmpty() {
    Source src; Maps maps;
    auto r = runQuery(src, maps, batch(artistA(), {}));
    expect(r.status == QueryStatus::Empty, "unknown artist must be Empty");
    expect(r.videos.isEmpty() && r.live.isEmpty(), "invented rows");
}
void unknownRowsUnqualified() {
    Source src; Maps maps;
    auto x = row("S9bCLPwzSC0", QStringLiteral("Mockingbird (Official Video)"));
    x.sourceClaim = Kind::OfficialMusicVideo;
    auto r = runQuery(src, maps, batch(artistA(), {x}));
    expect(r.status == QueryStatus::Empty, "search officiality must not qualify");
    expect(r.videos.isEmpty() && r.live.isEmpty(), "unmapped fixture entered rail");
}
void officialAndLiveSeparate() {
    Source src; Maps maps;
    auto official = row("S9bCLPwzSC0", QStringLiteral("Source-backed Video"));
    official.thumbnail = VideoThumbnailProvenance{
        provider("img-fixture"), item("frame-thumbnail-1"),
        QStringLiteral("https://images.example.invalid/video.jpg")};
    official.durationMs = 210000;
    maps.entries.push_back(mapping("S9bCLPwzSC0", Kind::OfficialMusicVideo, recA(), officialEvidence()));
    maps.entries.push_back(mapping("live-slot", Kind::LivePerformance, std::nullopt));
    maps.entries.push_back(mapping("alt-slot", Kind::AlternatePerformance, recB()));
    auto r = runQuery(src, maps, batch(artistA(),
                            {row("live-slot"), row("alt-slot"), official}));
    expect(r.status == QueryStatus::Ready, "ready missing");
    expect(r.videos.size() == 1 && r.live.size() == 2, "live and video not separated");
    const auto &v = r.videos.first();
    expect(v.kind == Kind::OfficialMusicVideo && v.recording == recA(), "official mapping");
    expect(v.bf2Candidate && v.directSwitchEligible, "BF2 switch qualification lost");
    expect(v.providerItem.value() == "S9bCLPwzSC0", "provider video ID changed");
    expect(v.videoThumbnail && v.videoThumbnail->imageProvider == provider("img-fixture"),
           "thumbnail provenance merged with provider video ID");
    expect(v.videoThumbnail->imageAsset.value() == "frame-thumbnail-1", "image key lost");
    expect(v.sourceLabel == "YouTube Music local fixture" &&
           v.mappingProvenance == "trusted-exact-map:v1", "source/map provenance lost");
    expect(r.live.first().kind == Kind::AlternatePerformance &&
           r.live.last().kind == Kind::LivePerformance, "stable group ordering");
    expect(!r.live.first().directSwitchEligible && !r.live.last().directSwitchEligible,
           "live/alternate became direct switches");
}
void unrelatedMappingRejected() {
    Source src; Maps maps;
    auto wrongArtist = mapping("video", Kind::OfficialMusicVideo, recA(), officialEvidence());
    wrongArtist.artist = artistB();
    maps.entries.push_back(wrongArtist);
    auto r = runQuery(src, maps, batch(artistA(), {row("video")}));
    expect(r.status == QueryStatus::Empty, "cross-artist mapping admitted");
    Source src2; Maps maps2;
    auto wrongItem = mapping("other", Kind::OfficialMusicVideo, recA(), officialEvidence());
    maps2.entries.push_back(wrongItem);
    r = runQuery(src2, maps2, batch(artistA(), {row("video")}));
    expect(r.status == QueryStatus::Empty, "cross-provider-item mapping admitted");
}
void officialSpoofAndFuzzyRejected() {
    Source src; Maps maps;
    maps.entries.push_back(mapping("fuzzy", Kind::OfficialMusicVideo, recA(), weakEvidence()));
    auto spoof = row("fuzzy", QStringLiteral("Hit title [official]"));
    spoof.sourceClaim = Kind::OfficialMusicVideo;
    auto r = runQuery(src, maps, batch(artistA(), {spoof}));
    expect(r.status == QueryStatus::Empty, "weak search relation became official");
    Source src2; Maps maps2;
    maps2.entries.push_back(mapping("canonical-only", Kind::OfficialMusicVideo, recA(),
        {{EK::CanonicalMapping, ES::Strong, std::nullopt}}));
    r = runQuery(src2, maps2, batch(artistA(), {row("canonical-only")}));
    expect(r.status == QueryStatus::Empty, "canonical link alone forged officiality");
}
void strongFuzzyEvidenceInvalid() {
    Source src; Maps maps;
    maps.entries.push_back(mapping("fuzzy", Kind::LyricVideo, recA(),
        {{EK::TextSearchSimilarity, ES::Strong, std::nullopt}}));
    auto r = runQuery(src, maps, batch(artistA(), {row("fuzzy")}));
    expect(r.status == QueryStatus::Invalid, "fabricated strong fuzzy evidence survived");
    expect(r.videos.isEmpty(), "invalid evidence emitted media");
}
void dedupDeterminism() {
    Source s1, s2; Maps maps;
    maps.entries.push_back(mapping("zzzz", Kind::LivePerformance, std::nullopt));
    maps.entries.push_back(mapping("aaaa", Kind::LivePerformance, std::nullopt));
    const auto a = row("aaaa");
    const auto z = row("zzzz");
    auto r1 = runQuery(s1, maps, batch(artistA(), {z, a, z}));
    auto r2 = runQuery(s2, maps, batch(artistA(), {a, z, a}));
    expect(r1.status == QueryStatus::Ready && r1.live.size() == 2,
           "identical provider dup not deduped");
    expect(r1.live.first().providerItem.value() == "aaaa", "provider order leaked");
    expect(r1.live.first().stableKey == r2.live.first().stableKey &&
           r1.live.last().stableKey == r2.live.last().stableKey, "not deterministic");
}
void duplicateConflictInvalid() {
    Source src; Maps maps;
    auto a = row("dup", QStringLiteral("One"));
    auto b = row("dup", QStringLiteral("Conflicting label"));
    auto r = runQuery(src, maps, batch(artistA(), {a, b}));
    expect(r.status == QueryStatus::Invalid && r.videos.isEmpty(), "conflicting duplicate");
}
void invalidEnvelopeAndThumb() {
    Source src; Maps maps;
    auto b = batch(artistB(), {row("video")});
    auto r = runQuery(src, maps, b);
    expect(r.status == QueryStatus::Invalid && r.errorCode == "artist_scope_mismatch",
           "wrong source artist");
    Source src2; Maps maps2;
    auto v = row("video");
    v.thumbnail = VideoThumbnailProvenance{
        provider(), item("thumbnail"), QStringLiteral("http://evil.invalid/x")};
    r = runQuery(src2, maps2, batch(artistA(), {v}));
    expect(r.status == QueryStatus::Invalid, "unsafe image URL accepted");
    Source src3; Maps maps3;
    v = row("video");
    v.durationMs = 0;
    r = runQuery(src3, maps3, batch(artistA(), {v}));
    expect(r.status == QueryStatus::Invalid, "invalid duration accepted");
}
void unavailableFailures() {
    Source src; Maps maps;
    auto b = batch(artistA(), {});
    b.status = QueryStatus::Unavailable;
    auto r = runQuery(src, maps, b);
    expect(r.status == QueryStatus::Unavailable, "unavailable lost");
    Source src2; Maps maps2; maps2.throwLookup = true;
    r = runQuery(src2, maps2, batch(artistA(), {row("video")}));
    expect(r.status == QueryStatus::Unavailable, "mapping port failure lost");
    Source src3; Maps maps3;
    src3.failFetch = true;
    ArtistMediaQuery service(src3, maps3);
    std::optional<ArtistMediaResult> received;
    service.start(artistText(), [&](const ArtistMediaResult &x) { received = x; });
    expect(received && received->status == QueryStatus::Unavailable,
           "provider fetch exception callback");
}
void staleAndCancellation() {
    Source src; Maps maps;
    maps.entries.push_back(mapping("live", Kind::LivePerformance, std::nullopt));
    ArtistMediaQuery query(src, maps);
    int first = 0, second = 0;
    auto t1 = query.start(artistText(), [&](const ArtistMediaResult &) { ++first; });
    auto t2 = query.start(artistText(), [&](const ArtistMediaResult &) { ++second; });
    src.deliver(0, batch(artistA(), {row("live")}));
    expect(first == 0, "stale earlier callback escaped");
    src.deliver(1, batch(artistA(), {row("live")}));
    expect(second == 1, "current result not delivered");
    src.deliver(1, batch(artistA(), {row("live")}));
    expect(second == 1, "duplicate callback escaped");
    auto old = query.select(t1, QStringLiteral("not-a-key"), artistText(),
                    ActionChoice::OpenMedia, std::nullopt, 10);
    expect(old.failure == ActionFailure::Stale, "old ticket selected");
    query.cancel(t1);
    expect(query.select(t2, QStringLiteral("not-a-key"), artistText(),
                    ActionChoice::OpenMedia, std::nullopt, 10).failure ==
                    ActionFailure::NotFound, "stale cancel invalidated new query");
    query.cancel(t2);
    expect(query.select(t2, QStringLiteral("not-a-key"), artistText(),
                    ActionChoice::OpenMedia, std::nullopt, 10).failure ==
                    ActionFailure::Stale, "cancelled ticket selectable");
    src.deliver(1, batch(artistA(), {row("live")}));
    expect(second == 1, "cancelled callback escaped");
    Source sr; Maps mp;
    {
        ArtistMediaQuery shortLived(sr, mp);
        shortLived.start(artistText(), [&](const ArtistMediaResult &) { ++first; });
    }
    sr.deliver(0, batch(artistA(), {}));
    expect(first == 0, "callback ran after service destruction");
}
void exactActionAndExpiry() {
    Source src; Maps maps;
    maps.entries.push_back(mapping("official", Kind::OfficialMusicVideo, recA(), officialEvidence()));
    maps.entries.push_back(mapping("live", Kind::LivePerformance, std::nullopt));
    ArtistMediaQuery query(src, maps);
    std::optional<ArtistMediaResult> result;
    const auto ticket = query.start(artistText(), [&](const ArtistMediaResult &r) { result = r; });
    auto pre = query.select(ticket, QStringLiteral("missing"), artistText(),
                    ActionChoice::OpenMedia, std::nullopt, 50);
    expect(pre.failure == ActionFailure::NotReady, "pending result selectable");
    auto off = row("official"); off.validUntilEpochMs = 1500;
    src.deliver(0, batch(artistA(), {row("live"), off}));
    expect(result && result->status == QueryStatus::Ready, "action setup");
    const QString officialKey = result->videos.first().stableKey;
    const QString liveKey = result->live.first().stableKey;
    auto ok = query.select(ticket, officialKey, artistText(),
                    ActionChoice::RequestAudioVideoSwitch, recText(), 1499);
    expect(ok.ok() && ok.intent->bf2Candidate &&
           ok.intent->bf2Candidate->recordingMbid() == recA() &&
           ok.intent->providerItem == item("official") &&
           ok.intent->artist == artistA(), "switch lost exact canonical/provider identity");
    expect(query.select(ticket, officialKey, artistText(),
                    ActionChoice::RequestAudioVideoSwitch, recB().toString(), 100).failure ==
                    ActionFailure::RecordingMismatch, "cross-recording switch");
    expect(query.select(ticket, officialKey, artistB().toString(),
                    ActionChoice::OpenMedia, std::nullopt, 100).failure ==
                    ActionFailure::ArtistMismatch, "cross-artist action");
    expect(query.select(ticket, liveKey, artistText(),
                    ActionChoice::RequestAudioVideoSwitch, recText(), 100).failure ==
                    ActionFailure::RecordingMismatch, "live recording scope");
    expect(query.select(ticket, liveKey, artistText(),
                    ActionChoice::RequestAudioVideoSwitch, std::nullopt, 100).failure ==
                    ActionFailure::Invalid, "direct switch without recording");
    auto liveOpen = query.select(ticket, liveKey, artistText(),
                    ActionChoice::OpenMedia, std::nullopt, 100);
    expect(liveOpen.ok() && !liveOpen.intent->bf2Candidate &&
           liveOpen.intent->providerItem == item("live") &&
           liveOpen.intent->choice == ActionChoice::OpenMedia,
           "live must remain a distinct open intent");
    expect(query.select(ticket, officialKey, artistText(),
                    ActionChoice::OpenMedia, std::nullopt, 1500).failure ==
                    ActionFailure::Expired, "metadata expiry inclusive boundary");
    expect(query.select(ticket, officialKey, artistText(),
                    ActionChoice::RequestAudioVideoSwitch, recText(), 1501).failure ==
                    ActionFailure::Expired, "expired switch intent issued");
    expect(query.select(ticket, QStringLiteral("forged"), artistText(),
                    ActionChoice::OpenMedia, std::nullopt, 100).failure ==
                    ActionFailure::NotFound, "nonexistent media key selected");
}
void alternateAndArtistOnlyOfficialGuard() {
    Source src; Maps maps;
    maps.entries.push_back(mapping("alternate", Kind::AlternatePerformance, recA()));
    maps.entries.push_back(mapping("artist-only-official", Kind::OfficialMusicVideo,
                                   std::nullopt, officialEvidence()));
    maps.entries.push_back(mapping("visualizer", Kind::OfficialAudioVisualizer,
                                   recB(), officialEvidence()));
    ArtistMediaQuery query(src, maps);
    std::optional<ArtistMediaResult> answer;
    const auto ticket = query.start(artistText(),
        [&](const ArtistMediaResult &r) { answer = r; });
    src.deliver(0, batch(artistA(), {row("alternate"), row("artist-only-official"),
                                   row("visualizer")}));
    expect(answer && answer->status == QueryStatus::Ready, "qualified alternate query");
    expect(answer->live.size() == 1 && answer->videos.size() == 1,
           "artist-only official mapping invented a video");
    const auto &alt = answer->live.first();
    const auto &viz = answer->videos.first();
    expect(alt.kind == Kind::AlternatePerformance && alt.recording == recA(),
           "exact alternate recording not retained");
    expect(!alt.directSwitchEligible && viz.directSwitchEligible,
           "BF2 video vs alternate eligibility violated");
    expect(query.select(ticket, alt.stableKey, artistText(),
           ActionChoice::RequestAudioVideoSwitch, recText(), 10).failure ==
           ActionFailure::NotEligible, "qualified alternate illegally switched");
    expect(query.select(ticket, alt.stableKey, artistText(),
           ActionChoice::OpenMedia, recText(), 10).ok(),
           "explicit alternate open refused");
    expect(query.select(ticket, viz.stableKey, artistText(),
           ActionChoice::RequestAudioVideoSwitch, recB().toString(), 10).ok(),
           "qualified visualizer denied despite BF2 eligibility");
}

void opaqueProviderIdNotMbid() {
    Source src; Maps maps;
    maps.entries.push_back(mapping("33333333-3333-4333-8333-333333333333",
         Kind::LivePerformance, std::nullopt));
    auto r = runQuery(src, maps, batch(artistA(),
              {row("33333333-3333-4333-8333-333333333333")}));
    expect(r.status == QueryStatus::Ready && r.live.size() == 1, "opaque-looking item");
    expect(!r.live.first().recording &&
           r.live.first().providerItem.value() == recText(),
           "provider video UUID was promoted to canonical RecordingMbid");
}
void unavailableSnapshotRetainsUnavailableWithRows() {
    Source src; Maps maps;
    auto snapshot = batch(artistA(), {row("stale-offline-row")});
    snapshot.status = QueryStatus::Unavailable;
    const auto result = runQuery(src, maps, snapshot);
    expect(result.status == QueryStatus::Unavailable, "outage with retained rows must stay Unavailable");
    expect(result.videos.isEmpty() && result.live.isEmpty(), "outage cannot expose stale rows as ready");
}
void invalidArtistNeverFetches() {
    Source src; Maps maps; ArtistMediaQuery q(src, maps);
    std::optional<ArtistMediaResult> result;
    q.start(QStringLiteral("music.youtube.com/channel/artist"), [&](const ArtistMediaResult &r) {
        result = r;
    });
    expect(result && result->status == QueryStatus::Invalid, "invalid artist admitted");
    expect(src.requests.isEmpty(), "invalid MBID reached provider source");
}
} // namespace

int main() {
    struct Case { const char *name; std::function<void()> fn; };
    const std::vector<Case> cases = {
        {"unknown_artist_empty_no_fabrication", unknownEmpty},
        {"unmapped_official_claim_rejected", unknownRowsUnqualified},
        {"qualified_specific_videos_live_separate", officialAndLiveSeparate},
        {"unrelated_artist_and_item_rejected", unrelatedMappingRejected},
        {"spoofed_officiality_fuzzy_rejected", officialSpoofAndFuzzyRejected},
        {"fuzzy_strong_evidence_invalid", strongFuzzyEvidenceInvalid},
        {"provider_duplicates_and_stable_order", dedupDeterminism},
        {"conflicting_provider_ids_invalid", duplicateConflictInvalid},
        {"wrong_artist_thumbnail_duration_invalid", invalidEnvelopeAndThumb},
        {"unavailable_and_port_failures", unavailableFailures},
        {"cancellation_stale_duplicate_destroyed", staleAndCancellation},
        {"exact_open_switch_expiry_errors", exactActionAndExpiry},
        {"alternate_and_artist_only_official_guard", alternateAndArtistOnlyOfficialGuard},
        {"opaque_provider_id_not_canonical_mbid", opaqueProviderIdNotMbid},
        {"invalid_artist_prevents_source_fetch", invalidArtistNeverFetches},
        {"unavailable_snapshot_with_rows", unavailableSnapshotRetainsUnavailableWithRows}
    };
    int failures = 0;
    for (const auto &t : cases) {
        try {
            t.fn();
            std::cout << "PASS " << t.name << "\n";
        } catch (const std::exception &e) {
            ++failures;
            std::cout << "FAIL " << t.name << ": " << e.what() << "\n";
        }
    }
    std::cout << (failures ? "FAIL" : "PASS") << " total="
              << cases.size() << " failed=" << failures << "\n";
    return failures == 0 ? 0 : 1;
}
