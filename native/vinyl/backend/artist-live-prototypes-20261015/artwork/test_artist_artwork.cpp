#include "ArtistArtworkResolver.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace vinyl::artwork;

namespace {
constexpr const char* mf = "188711ed-c99b-439c-844a-ca831f63a727";
constexpr const char* doc = "535f74d5-8328-40ff-9cea-568ef916b3c8";
int passed = 0;
int failed = 0;

void check(bool outcome, const char* name) {
    if (outcome) { ++passed; std::cout << "PASS " << name << '\n'; }
    else { ++failed; std::cerr << "FAIL " << name << '\n'; }
}

std::string sha(char digit) {
    return "sha256:" + std::string(64, digit);
}

PhotoCandidate makePhoto(std::string id, std::string artist = mf) {
    PhotoCandidate c;
    c.artistMbid = std::move(artist);
    c.provider = "SyntheticRightsFixture";
    c.providerAssetId = std::move(id);
    c.sourceUrl = "https://photos.example.test/" + c.providerAssetId + ".jpg";
    c.kind = PhotoKind::Hero;
    c.width = 1920;
    c.height = 1080;
    c.canonicalArtistConfirmed = true;
    c.actualPhotograph = true;
    c.cleanImage = true;
    c.subjectCropSafe = true;
    c.humanReviewedAsDistinctInArtistSet = true; // Synthetic proof only.
    c.rights.status = RightsStatus::VerifiedPermitted;
    c.rights.licenseId = "SYNTHETIC-TEST-ONLY";
    c.rights.evidenceUrl = "https://rights.example.test/licence";
    c.rights.author = "Fixture";
    c.rights.attribution = "Synthetic fixture, not a real license";
    c.rights.artistHeroPermitted = true;
    c.rights.localCachePermitted = true;
    c.rights.validUntilEpoch = 200000;
    c.observedAtEpoch = 1000;
    return c;
}

Request makeRequest(std::vector<PhotoCandidate> images = {}) {
    Request request;
    request.artistMbid = mf;
    request.artistName = "MF DOOM";
    request.fetched = std::move(images);
    request.nowEpoch = 1000;
    return request;
}

CachedPhoto cacheFor(const PhotoCandidate& candidate) {
    CachedPhoto cached;
    cached.candidate = candidate;
    cached.localAssetKey = "artist-image-001";
    cached.bytesSha256 = sha('a');
    cached.expiresAtEpoch = 200000;
    cached.verifiedBytesPresent = true; // Synthetic adapter proof.
    return cached;
}

bool modeAndCount(const Decision& decision, DisplayMode mode, std::size_t count) {
    return decision.mode == mode && decision.slides.size() == count;
}

} // namespace

int main() {
    ArtistArtworkResolver resolver;
    auto empty = resolver.resolve(makeRequest());
    check(modeAndCount(empty, DisplayMode::ArtistComposition, 0) &&
          empty.fallback.name == "MF DOOM" && !empty.fallback.monogram.empty() &&
          empty.fallback.artistMbid == mf && empty.fallback.layoutSeed != 0,
          "zero-heros-artist-specific-composition");

    auto a = makePhoto("real-photo-a");
    auto b = makePhoto("real-photo-b");
    auto c = makePhoto("real-photo-c");
    auto d = makePhoto("real-photo-d");

    auto one = resolver.resolve(makeRequest({a}));
    check(modeAndCount(one, DisplayMode::StaticPhoto, 1) &&
          one.releaseCleared && one.cacheSuggestions.size() == 1,
          "one-eligible-photo-is-static");
    auto two = resolver.resolve(makeRequest({a, b}));
    check(modeAndCount(two, DisplayMode::Crossfade, 2),
          "two-eligible-photos-crossfade");
    auto three = resolver.resolve(makeRequest({a, b, c}));
    check(modeAndCount(three, DisplayMode::Rotating, 3),
          "three-eligible-photos-rotate");
    auto four = resolver.resolve(makeRequest({a, b, c, d}));
    check(modeAndCount(four, DisplayMode::Rotating, 4),
          "four-real-photos-are-not-mandatory");

    auto shuffled = resolver.resolve(makeRequest({d, c, b, a}));
    bool sameOrder = shuffled.slides.size() == four.slides.size();
    for (std::size_t i = 0; sameOrder && i < four.slides.size(); ++i)
        sameOrder = shuffled.slides[i].photo.providerAssetId ==
                    four.slides[i].photo.providerAssetId;
    check(sameOrder, "deterministic-selection-independent-of-provider-order");

    auto limited = makeRequest({a, b, c, d});
    limited.maxSlides = 2;
    check(modeAndCount(resolver.resolve(limited), DisplayMode::Crossfade, 2),
          "bounded-slide-list-no-clones-to-fill-slots");

    auto wrong = makePhoto("other-artist", doc);
    check(modeAndCount(resolver.resolve(makeRequest({wrong})),
                       DisplayMode::ArtistComposition, 0),
          "reject-photo-for-other-valid-artist-mbid");
    auto invalid = makePhoto("no-mbid", "MF DOOM");
    check(modeAndCount(resolver.resolve(makeRequest({invalid})),
                       DisplayMode::ArtistComposition, 0),
          "reject-name-only-identity");

    auto repeatUrl = makePhoto("different-source-id");
    repeatUrl.sourceUrl = "https://PHOTOS.EXAMPLE.TEST/real-photo-a.jpg#different";
    check(modeAndCount(resolver.resolve(makeRequest({a, repeatUrl})),
                       DisplayMode::StaticPhoto, 1),
          "dedup-normalized-source-url");

    auto hashA = makePhoto("other-url-one");
    hashA.sha256 = sha('b');
    auto hashB = makePhoto("other-url-two");
    hashB.sha256 = sha('b');
    check(modeAndCount(resolver.resolve(makeRequest({hashA, hashB})),
                       DisplayMode::StaticPhoto, 1),
          "dedup-sha256-across-different-urls");

    auto perceptualA = makePhoto("alt-toning-one");
    perceptualA.phash64 = "phash64:0123456789abcdef";
    auto perceptualB = makePhoto("alt-toning-two");
    perceptualB.phash64 = "phash64:0123456789abcdee";
    check(modeAndCount(resolver.resolve(makeRequest({perceptualA, perceptualB})),
                       DisplayMode::StaticPhoto, 1),
          "dedup-near-perceptual-hash-tinted-variant");

    auto uncheckedA = makePhoto("unchecked-a");
    auto uncheckedB = makePhoto("unchecked-b");
    uncheckedA.humanReviewedAsDistinctInArtistSet = false;
    uncheckedB.humanReviewedAsDistinctInArtistSet = false;
    check(modeAndCount(resolver.resolve(makeRequest({uncheckedA, uncheckedB})),
                       DisplayMode::StaticPhoto, 1),
          "unreviewed-different-urls-cannot-fabricate-distinct-slides");
    check(modeAndCount(resolver.resolve(makeRequest({uncheckedA})),
                       DisplayMode::StaticPhoto, 1),
          "single-unreviewed-photo-still-allowed");
    uncheckedA.phash64 = "phash64:0123456789abcdef";
    uncheckedB.phash64 = "phash64:fedcba9876543210";
    check(modeAndCount(resolver.resolve(makeRequest({uncheckedA, uncheckedB})),
                       DisplayMode::Crossfade, 2),
          "independent-perceptual-signatures-prove-distinct-slides");

    auto sizeReject = makePhoto("small");
    sizeReject.width = 640; sizeReject.height = 360;
    auto aspectReject = makePhoto("extreme");
    aspectReject.width = 4000; aspectReject.height = 720;
    check(modeAndCount(resolver.resolve(makeRequest({sizeReject, aspectReject})),
                       DisplayMode::ArtistComposition, 0),
          "reject-too-small-or-extreme-aspect");

    auto placebo = makePhoto("placeholder");
    placebo.sourceUrl = "https://photos.example.test/placeholder/photo.jpg";
    auto branded = makePhoto("brand");
    branded.cleanImage = false;
    auto album = makePhoto("album-cover");
    album.kind = PhotoKind::Other;
    check(modeAndCount(resolver.resolve(makeRequest({placebo, branded, album})),
                       DisplayMode::ArtistComposition, 0),
          "reject-placeholders-baked-branding-and-album-covers");

    auto unknown = makePhoto("rights-unknown");
    unknown.rights.status = RightsStatus::Unknown;
    auto restricted = makePhoto("rights-restricted");
    restricted.rights.status = RightsStatus::VerifiedRestricted;
    auto undocumented = makePhoto("no-evidence");
    undocumented.rights.evidenceUrl.clear();
    check(modeAndCount(resolver.resolve(makeRequest({unknown, restricted, undocumented})),
                       DisplayMode::ArtistComposition, 0),
          "unknown-restricted-or-unsubstantiated-rights-never-release-cleared");

    auto heroOnly = makePhoto("no-cache-permission");
    heroOnly.rights.localCachePermitted = false;
    auto heroOnlyResult = resolver.resolve(makeRequest({heroOnly}));
    check(modeAndCount(heroOnlyResult, DisplayMode::StaticPhoto, 1) &&
          heroOnlyResult.cacheSuggestions.empty(),
          "render-permission-does-not-automatically-authorize-caching");

    auto staleRights = makePhoto("expired-rights");
    staleRights.rights.validUntilEpoch = 999;
    check(modeAndCount(resolver.resolve(makeRequest({staleRights})),
                       DisplayMode::ArtistComposition, 0),
          "expired-rights-rejected");

    auto portrait = makePhoto("portrait");
    portrait.kind = PhotoKind::Portrait;
    portrait.width = 500; portrait.height = 500;
    auto composed = resolver.resolve(makeRequest({portrait}));
    check(modeAndCount(composed, DisplayMode::ArtistComposition, 0) &&
          composed.fallback.eligiblePortrait.has_value() &&
          composed.fallback.eligiblePortrait->photo.kind == PhotoKind::Portrait,
          "eligible-portrait-in-composition-not-stretched-hero");

    auto offline = makeRequest();
    offline.providerAvailable = false;
    offline.cached = {cacheFor(a), cacheFor(b)};
    offline.cached[1].localAssetKey = "artist-image-002";
    offline.cached[1].bytesSha256 = sha('b');
    auto cachedDecision = resolver.resolve(offline);
    check(modeAndCount(cachedDecision, DisplayMode::Crossfade, 2) &&
          cachedDecision.slides[0].fromVerifiedCache &&
          cachedDecision.slides[1].fromVerifiedCache,
          "offline-reuses-verified-metadata-and-bytes");

    auto keyCollision = offline;
    keyCollision.cached[1].localAssetKey = keyCollision.cached[0].localAssetKey;
    check(modeAndCount(resolver.resolve(keyCollision), DisplayMode::StaticPhoto, 1),
          "same-local-cache-key-cannot-count-as-two-slides");
    auto bytesCollision = offline;
    bytesCollision.cached[1].bytesSha256 = bytesCollision.cached[0].bytesSha256;
    check(modeAndCount(resolver.resolve(bytesCollision), DisplayMode::StaticPhoto, 1),
          "same-cached-bytes-across-origins-are-deduped");
    auto digestMismatch = offline;
    digestMismatch.cached[0].candidate.sha256 = sha('c');
    check(modeAndCount(resolver.resolve(digestMismatch), DisplayMode::StaticPhoto, 1),
          "cache-rejects-conflicting-origin-and-local-byte-digests");

    auto onlineAndCache = makeRequest({a});
    onlineAndCache.cached = {cacheFor(a)};
    const auto reused = resolver.resolve(onlineAndCache);
    check(modeAndCount(reused, DisplayMode::StaticPhoto, 1) &&
          reused.slides[0].fromVerifiedCache,
          "cached-asset-preferred-over-same-origin-candidate");

    auto corrupted = offline;
    corrupted.cached[0].bytesSha256 = "not-a-digest";
    corrupted.cached[1].expiresAtEpoch = 900;
    check(modeAndCount(resolver.resolve(corrupted),
                       DisplayMode::ArtistComposition, 0),
          "stale-or-unverified-cache-fails-closed");

    auto serialized = encodeCacheManifest(offline.cached);
    auto reloaded = decodeCacheManifest(serialized);
    check(reloaded.size() == 2 && !reloaded[0].verifiedBytesPresent &&
          !reloaded[1].verifiedBytesPresent,
          "metadata-roundtrip-never-persists-trusted-byte-presence");
    auto restarted = makeRequest();
    restarted.providerAvailable = false;
    restarted.cached = reloaded;
    check(modeAndCount(resolver.resolve(restarted),
                       DisplayMode::ArtistComposition, 0),
          "offline-reload-does-not-automatically-trust-old-cache");
    for (auto& cache : restarted.cached) cache.verifiedBytesPresent = true;
    check(modeAndCount(resolver.resolve(restarted), DisplayMode::Crossfade, 2),
          "cache-revalidated-after-restart-enables-offline-playback");
    check(decodeCacheManifest("wrong-version\n" + serialized).empty() &&
          decodeCacheManifest("ARTIST_ARTWORK_CACHE_V2\nx|y\n").empty(),
          "malformed-cache-rows-and-versions-ignored");

    auto privateRequest = makeRequest({a, b});
    privateRequest.scope = Scope::LocalPreview;
    privateRequest.userOverride = LocalOverride{mf, "user-artist-photo", 1920, 1080,
                                                true, true};
    auto privateDecision = resolver.resolve(privateRequest);
    check(modeAndCount(privateDecision, DisplayMode::StaticPhoto, 1) &&
          !privateDecision.releaseCleared && privateDecision.slides[0].localOnly &&
          privateDecision.cacheSuggestions.empty(),
          "private-user-override-is-only-local-never-release-cleared");

    privateRequest.scope = Scope::Release;
    auto releaseWithOverride = resolver.resolve(privateRequest);
    check(modeAndCount(releaseWithOverride, DisplayMode::Crossfade, 2) &&
          releaseWithOverride.releaseCleared,
          "release-ignores-local-only-override");

    privateRequest.scope = Scope::LocalPreview;
    privateRequest.userOverride->artistMbid = doc;
    check(modeAndCount(resolver.resolve(privateRequest), DisplayMode::Crossfade, 2),
          "different-artist-manual-override-rejected");
    privateRequest.userOverride->artistMbid = mf;
    privateRequest.userOverride->localAssetKey = "../secret";
    check(modeAndCount(resolver.resolve(privateRequest), DisplayMode::Crossfade, 2),
          "override-key-traversal-rejected");

    auto fail3 = resolver.afterLoadFailure(three, three.slides[0].displayRef);
    check(modeAndCount(fail3, DisplayMode::Crossfade, 2),
          "three-to-two-after-image-load-failure");
    auto fail2 = resolver.afterLoadFailure(fail3, fail3.slides[0].displayRef);
    check(modeAndCount(fail2, DisplayMode::StaticPhoto, 1),
          "two-to-one-after-image-load-failure");
    auto fail1 = resolver.afterLoadFailure(fail2, fail2.slides[0].displayRef);
    check(modeAndCount(fail1, DisplayMode::ArtistComposition, 0) &&
          fail1.fallback.name == "MF DOOM",
          "last-image-failure-uses-artist-composition");

    std::cout << "RESULT " << passed << " PASS / " << failed << " FAIL\n";
    return failed ? 1 : 0;
}
