#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace vinyl::artwork {

enum class PhotoKind { Hero, Portrait, Other };
enum class RightsStatus { Unknown, VerifiedPermitted, VerifiedRestricted };
enum class DisplayMode { ArtistComposition, StaticPhoto, Crossfade, Rotating };
enum class Scope { Release, LocalPreview };

struct RightsInfo {
    RightsStatus status = RightsStatus::Unknown;
    std::string licenseId;
    std::string evidenceUrl;
    std::string author;
    std::string attribution;
    bool artistHeroPermitted = false;
    bool localCachePermitted = false;
    std::int64_t validUntilEpoch = 0; // 0 is unverified, not unlimited.
};

struct PhotoCandidate {
    std::string artistMbid;
    std::string provider;
    std::string providerAssetId;
    std::string sourceUrl;
    PhotoKind kind = PhotoKind::Other;
    int width = 0;
    int height = 0;
    bool canonicalArtistConfirmed = false;
    bool actualPhotograph = false;
    bool cleanImage = false; // Reject baked-in logo/banner, artificial copies.
    bool subjectCropSafe = false;
    bool humanReviewedAsDistinctInArtistSet = false;
    bool placeholder = false;
    RightsInfo rights;
    std::string sha256; // "sha256:" + 64 hexadecimal digits, if supplied.
    std::string phash64; // "phash64:" + 16 hexadecimal digits, if supplied.
    std::int64_t observedAtEpoch = 0;
};

struct CachedPhoto {
    PhotoCandidate candidate;
    std::string localAssetKey; // Opaque device-cache key, never a filesystem path.
    std::string bytesSha256;
    std::int64_t expiresAtEpoch = 0;
    bool verifiedBytesPresent = false; // Rechecked by local cache layer, NEVER serialized.
};

struct LocalOverride {
    std::string artistMbid;
    std::string localAssetKey; // Opaque key; no paths, URLs, or traversal.
    int width = 0;
    int height = 0;
    bool artistSpecificConfirmed = false;
    bool verifiedBytesPresent = false;
};

struct Request {
    std::string artistMbid;
    std::string artistName;
    std::vector<PhotoCandidate> fetched;
    std::vector<CachedPhoto> cached;
    std::optional<LocalOverride> userOverride;
    Scope scope = Scope::Release;
    bool providerAvailable = true;
    std::int64_t nowEpoch = 0;
    std::size_t maxSlides = 8;
};

struct HeroFrame {
    PhotoCandidate photo;
    std::string displayRef; // Qualified HTTPS origin, or "cache:" + opaque key.
    bool fromVerifiedCache = false;
    bool localOnly = false;
    bool releaseCleared = false;
};

struct ArtistComposition {
    std::string artistMbid;
    std::string name;
    std::string monogram;
    std::uint64_t layoutSeed = 0;
    std::optional<HeroFrame> eligiblePortrait;
};

struct Decision {
    DisplayMode mode = DisplayMode::ArtistComposition;
    std::vector<HeroFrame> slides;
    ArtistComposition fallback; // Always available, including after image load failure.
    std::vector<PhotoCandidate> cacheSuggestions; // Metadata only; never fetches media.
    std::vector<std::string> rejectionReasons;
    bool releaseCleared = true; // false when the private-only local override is active.
};

class ArtistArtworkResolver {
public:
    // Pure, synchronous, provider/transport-independent. No network or disk access.
    Decision resolve(const Request& request) const;
    Decision afterLoadFailure(Decision decision, const std::string& failingDisplayRef) const;

    static bool isCanonicalArtistMbid(const std::string& mbid);
};

// Metadata-only schema. Unknown fields/invalid versions fail closed.
// On read, verifiedBytesPresent ALWAYS resets to false until cache revalidation.
std::string encodeCacheManifest(const std::vector<CachedPhoto>& records);
std::vector<CachedPhoto> decodeCacheManifest(const std::string& wire);

} // namespace vinyl::artwork
