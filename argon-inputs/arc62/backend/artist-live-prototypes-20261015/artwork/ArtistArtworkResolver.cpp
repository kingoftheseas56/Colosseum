#include "ArtistArtworkResolver.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace vinyl::artwork {
namespace {

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

bool isMbid(const std::string& s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!isHex(s[i])) return false;
    }
    return true;
}

bool digest(const std::string& value, const std::string& prefix, std::size_t digits) {
    if (value.size() != prefix.size() + digits ||
        value.compare(0, prefix.size(), prefix) != 0) return false;
    return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(prefix.size()),
                       value.end(), isHex);
}

bool opaqueKey(const std::string& value) {
    if (value.empty() || value.size() > 128 || value.find("..") != std::string::npos)
        return false;
    return std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}

std::string normalizedUrl(const std::string& url) {
    if (url.size() < 12 || url.size() > 2048 || url.compare(0, 8, "https://") != 0)
        return {};
    if (std::any_of(url.begin(), url.end(), [](unsigned char c) {
        return c <= 0x20 || c == 0x7f || c == '\\';
    })) return {};
    auto hostEnd = url.find_first_of("/?#", 8);
    if (hostEnd == std::string::npos) hostEnd = url.size();
    auto host = lower(url.substr(8, hostEnd - 8));
    if (host.size() < 4 || host.size() > 253 || host.find('.') == std::string::npos ||
        host.find('@') != std::string::npos || host.find(':') != std::string::npos ||
        host.front() == '.' || host.back() == '.' ||
        host == "localhost" || host.rfind("127.", 0) == 0 ||
        host.rfind("10.", 0) == 0 || host.rfind("192.168.", 0) == 0)
        return {};
    if (!std::all_of(host.begin(), host.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '-' || c == '.';
    })) return {};
    auto frag = url.find('#', hostEnd);
    return "https://" + host + url.substr(hostEnd, frag == std::string::npos
                                                  ? std::string::npos : frag - hostEnd);
}

bool looksPlaceholder(const PhotoCandidate& photo) {
    if (photo.placeholder) return true;
    const auto url = lower(photo.sourceUrl);
    for (const auto* marker : {"/placeholder", "no-image", "noimage", "missing_image",
                               "default_artist", "default-artist", "/blank."})
        if (url.find(marker) != std::string::npos) return true;
    return false;
}

bool rightsCleared(const RightsInfo& rights, std::int64_t now) {
    return rights.status == RightsStatus::VerifiedPermitted &&
           rights.artistHeroPermitted && !rights.licenseId.empty() &&
           !rights.author.empty() && !rights.attribution.empty() &&
           !normalizedUrl(rights.evidenceUrl).empty() &&
           rights.validUntilEpoch > now;
}

bool photoGeometry(const PhotoCandidate& photo) {
    if (photo.width <= 0 || photo.height <= 0 ||
        photo.width > 16384 || photo.height > 16384) return false;
    const auto ratio = static_cast<double>(photo.width) / photo.height;
    if (photo.kind == PhotoKind::Hero)
        return photo.width >= 1280 && photo.height >= 720 &&
               ratio >= 1.40 && ratio <= 2.50;
    if (photo.kind == PhotoKind::Portrait)
        return photo.width >= 400 && photo.height >= 400 &&
               ratio >= 0.55 && ratio <= 1.80;
    return false;
}

std::string validate(const PhotoCandidate& photo, const Request& req) {
    if (!isMbid(req.artistMbid) || !isMbid(photo.artistMbid) ||
        lower(photo.artistMbid) != lower(req.artistMbid)) return "artist-mbid-mismatch";
    if (photo.provider.empty() || photo.providerAssetId.empty() ||
        !photo.canonicalArtistConfirmed) return "unverified-artist-identity";
    if (!photo.actualPhotograph || !photo.cleanImage || !photo.subjectCropSafe ||
        looksPlaceholder(photo)) return "not-clean-artist-photo";
    if (normalizedUrl(photo.sourceUrl).empty()) return "untrusted-source-url";
    if (!photoGeometry(photo)) return "bad-geometry-or-kind";
    if ((!photo.sha256.empty() && !digest(photo.sha256, "sha256:", 64)) ||
        (!photo.phash64.empty() && !digest(photo.phash64, "phash64:", 16)))
        return "invalid-fingerprint";
    if (!rightsCleared(photo.rights, req.nowEpoch)) return "rights-not-cleared";
    return {};
}

std::uint64_t phashValue(const std::string& value) {
    std::uint64_t result = 0;
    for (char c : value.substr(8)) {
        result = (result << 4U) |
            static_cast<std::uint64_t>(c <= '9' ? c - '0' :
               (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10));
    }
    return result;
}

unsigned hamming(std::uint64_t a, std::uint64_t b) {
    auto bits = a ^ b;
    unsigned count = 0;
    while (bits) { bits &= (bits - 1); ++count; }
    return count;
}

std::int64_t visualScore(const PhotoCandidate& p) {
    const auto area = static_cast<std::int64_t>(p.width) * p.height;
    const auto cropError = std::llabs(static_cast<std::int64_t>(p.width) * 9 -
                                     static_cast<std::int64_t>(p.height) * 16);
    return std::min(area, std::int64_t{3840} * 2160) / 100 -
           (cropError * 500 / p.height);
}

bool better(const HeroFrame& a, const HeroFrame& b) {
    const auto as = visualScore(a.photo), bs = visualScore(b.photo);
    if (as != bs) return as > bs;
    if (a.fromVerifiedCache != b.fromVerifiedCache) return a.fromVerifiedCache;
    return std::tie(a.photo.provider, a.photo.providerAssetId, a.photo.sourceUrl,
                    a.photo.sha256, a.photo.phash64, a.displayRef) <
           std::tie(b.photo.provider, b.photo.providerAssetId, b.photo.sourceUrl,
                    b.photo.sha256, b.photo.phash64, b.displayRef);
}

bool duplicates(const HeroFrame& x, const HeroFrame& y) {
    if ((!x.displayRef.empty() && x.displayRef == y.displayRef) ||
        normalizedUrl(x.photo.sourceUrl) == normalizedUrl(y.photo.sourceUrl))
        return true;
    if (!x.photo.sha256.empty() && !y.photo.sha256.empty() &&
        lower(x.photo.sha256) == lower(y.photo.sha256)) return true;
    if (!x.photo.phash64.empty() && !y.photo.phash64.empty())
        return hamming(phashValue(x.photo.phash64), phashValue(y.photo.phash64)) <= 3;
    // Different URLs or byte hashes do not prove two images are different photos.
    return !(x.photo.humanReviewedAsDistinctInArtistSet &&
             y.photo.humanReviewedAsDistinctInArtistSet);
}

std::string monogram(const std::string& name, const std::string& mbid) {
    std::string initials;
    bool wordStart = true;
    for (unsigned char c : name) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9');
        if (letter && wordStart && initials.size() < 2) {
            initials += static_cast<char>(std::toupper(c));
        }
        if (letter) wordStart = false;
        else if (c == ' ' || c == '-' || c == '_') wordStart = true;
    }
    if (initials.empty()) return lower(mbid).substr(0, 2);
    return initials;
}

std::uint64_t layoutSeed(const std::string& mbid) {
    std::uint64_t state = 14695981039346656037ULL;
    for (unsigned char c : lower(mbid)) {
        state ^= c;
        state *= 1099511628211ULL;
    }
    return state;
}

void updateMode(Decision& decision) {
    switch (decision.slides.size()) {
    case 0: decision.mode = DisplayMode::ArtistComposition; break;
    case 1: decision.mode = DisplayMode::StaticPhoto; break;
    case 2: decision.mode = DisplayMode::Crossfade; break;
    default: decision.mode = DisplayMode::Rotating; break;
    }
    decision.releaseCleared =
        std::all_of(decision.slides.begin(), decision.slides.end(),
                    [](const HeroFrame& frame) { return frame.releaseCleared; });
}

} // namespace

bool ArtistArtworkResolver::isCanonicalArtistMbid(const std::string& mbid) {
    return isMbid(mbid);
}

Decision ArtistArtworkResolver::resolve(const Request& request) const {
    Decision decision;
    decision.fallback.artistMbid = request.artistMbid;
    decision.fallback.name = request.artistName.empty()
        ? "Artist " + request.artistMbid.substr(0, 8) : request.artistName;
    decision.fallback.monogram = monogram(decision.fallback.name, request.artistMbid);
    decision.fallback.layoutSeed = layoutSeed(request.artistMbid);

    if (!isMbid(request.artistMbid)) {
        decision.rejectionReasons.push_back("invalid-request-artist-mbid");
        return decision;
    }

    std::vector<HeroFrame> heroes;
    std::vector<HeroFrame> portraits;
    const auto consider = [&](HeroFrame frame) {
        const auto reason = validate(frame.photo, request);
        if (!reason.empty()) {
            decision.rejectionReasons.push_back(
                frame.photo.provider + "/" + frame.photo.providerAssetId + ": " + reason);
            return;
        }
        if (frame.photo.kind == PhotoKind::Portrait)
            portraits.push_back(std::move(frame));
        else heroes.push_back(std::move(frame));
    };

    // Disk/cache validation is injected by the caller. Never trust "available"
    // from serialized metadata: decoder resets verifiedBytesPresent to false.
    for (const auto& saved : request.cached) {
        if (!saved.verifiedBytesPresent || !opaqueKey(saved.localAssetKey) ||
            !digest(saved.bytesSha256, "sha256:", 64) ||
            (!saved.candidate.sha256.empty() &&
             lower(saved.candidate.sha256) != lower(saved.bytesSha256)) ||
            saved.expiresAtEpoch <= request.nowEpoch ||
            !saved.candidate.rights.localCachePermitted) {
            decision.rejectionReasons.push_back(
                saved.candidate.providerAssetId + ": cache-integrity-or-expiry");
            continue;
        }
        HeroFrame frame;
        frame.photo = saved.candidate;
        if (frame.photo.sha256.empty()) frame.photo.sha256 = saved.bytesSha256;
        frame.displayRef = "cache:" + saved.localAssetKey;
        frame.fromVerifiedCache = true;
        frame.releaseCleared = true;
        consider(std::move(frame));
    }

    if (request.providerAvailable) {
        for (const auto& photo : request.fetched) {
            HeroFrame frame;
            frame.photo = photo;
            frame.displayRef = normalizedUrl(photo.sourceUrl);
            frame.releaseCleared = true;
            consider(std::move(frame));
        }
    }

    std::sort(portraits.begin(), portraits.end(), better);
    if (!portraits.empty()) decision.fallback.eligiblePortrait = portraits.front();

    // Optional private override is a separate lane. Never make it release
    // eligible, upload it, or infer commercial redistribution rights.
    if (request.userOverride) {
        const auto& over = *request.userOverride;
        const double ratio = over.height > 0 ? static_cast<double>(over.width) / over.height : 0;
        if (request.scope == Scope::LocalPreview &&
            isMbid(over.artistMbid) && lower(over.artistMbid) == lower(request.artistMbid) &&
            opaqueKey(over.localAssetKey) && over.verifiedBytesPresent &&
            over.artistSpecificConfirmed && over.width >= 1280 && over.height >= 720 &&
            ratio >= 1.40 && ratio <= 2.50) {
            HeroFrame frame;
            frame.photo.artistMbid = request.artistMbid;
            frame.photo.kind = PhotoKind::Hero;
            frame.photo.provider = "LocalUserOverride";
            frame.photo.providerAssetId = over.localAssetKey;
            frame.photo.width = over.width;
            frame.photo.height = over.height;
            frame.displayRef = "cache:" + over.localAssetKey;
            frame.fromVerifiedCache = true;
            frame.localOnly = true;
            frame.releaseCleared = false;
            decision.slides.push_back(std::move(frame));
            updateMode(decision);
            return decision;
        }
        decision.rejectionReasons.push_back("override-not-eligible-in-this-scope");
    }

    std::sort(heroes.begin(), heroes.end(), better);
    std::vector<HeroFrame> unique;
    const auto cap = std::min<std::size_t>(request.maxSlides, 12);
    for (const auto& frame : heroes) {
        if (std::any_of(unique.begin(), unique.end(),
                        [&](const HeroFrame& existing) { return duplicates(existing, frame); })) {
            decision.rejectionReasons.push_back(
                frame.photo.providerAssetId + ": duplicate-photo");
            continue;
        }
        if (unique.size() == cap) break;
        unique.push_back(frame);
        // Cache only when independent permission to retain media was verified.
        if (!frame.fromVerifiedCache && frame.photo.rights.localCachePermitted)
            decision.cacheSuggestions.push_back(frame.photo);
    }
    decision.slides = std::move(unique);
    updateMode(decision);
    return decision;
}

Decision ArtistArtworkResolver::afterLoadFailure(
    Decision decision, const std::string& failingDisplayRef) const {
    if (failingDisplayRef.empty()) return decision;
    decision.slides.erase(
        std::remove_if(decision.slides.begin(), decision.slides.end(),
                       [&](const HeroFrame& frame) {
                           return frame.displayRef == failingDisplayRef;
                       }),
        decision.slides.end());
    if (decision.fallback.eligiblePortrait &&
        decision.fallback.eligiblePortrait->displayRef == failingDisplayRef)
        decision.fallback.eligiblePortrait.reset();
    decision.cacheSuggestions.erase(
        std::remove_if(decision.cacheSuggestions.begin(), decision.cacheSuggestions.end(),
                       [&](const PhotoCandidate& photo) {
                           return normalizedUrl(photo.sourceUrl) == failingDisplayRef;
                       }),
        decision.cacheSuggestions.end());
    decision.rejectionReasons.push_back("image-load-failed:" + failingDisplayRef);
    updateMode(decision);
    return decision;
}

namespace {
constexpr const char* cacheHeader = "ARTIST_ARTWORK_CACHE_V2";
constexpr char hexDigits[] = "0123456789abcdef";

std::string hexEncode(const std::string& s) {
    std::string result;
    result.reserve(s.size() * 2);
    for (unsigned char c : s) {
        result.push_back(hexDigits[c >> 4]);
        result.push_back(hexDigits[c & 15]);
    }
    return result;
}

std::optional<std::string> hexDecode(const std::string& input) {
    if (input.size() % 2 || input.size() > 8192) return std::nullopt;
    std::string result;
    result.reserve(input.size() / 2);
    auto toNibble = [](char c) -> unsigned {
        return c <= '9' ? static_cast<unsigned>(c - '0') :
               c <= 'F' ? static_cast<unsigned>(c - 'A' + 10) :
                          static_cast<unsigned>(c - 'a' + 10);
    };
    for (std::size_t i = 0; i < input.size(); i += 2) {
        if (!isHex(input[i]) || !isHex(input[i+1])) return std::nullopt;
        result += static_cast<char>((toNibble(input[i]) << 4) | toNibble(input[i+1]));
    }
    return result;
}

std::int64_t number(const std::string& s) {
    std::int64_t result = 0;
    const auto read = std::from_chars(s.data(), s.data() + s.size(), result);
    if (s.empty() || read.ec != std::errc{} || read.ptr != s.data() + s.size())
        throw std::invalid_argument("invalid manifest number");
    return result;
}

bool flag(const std::string& s) {
    const auto n = number(s);
    if (n != 0 && n != 1) throw std::invalid_argument("invalid manifest boolean");
    return n == 1;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (begin <= s.size()) {
        auto end = s.find('|', begin);
        if (end == std::string::npos) end = s.size();
        fields.push_back(s.substr(begin, end - begin));
        if (end == s.size()) break;
        begin = end + 1;
    }
    return fields;
}
} // namespace

std::string encodeCacheManifest(const std::vector<CachedPhoto>& records) {
    std::ostringstream out;
    out << cacheHeader << '\n';
    std::size_t count = 0;
    for (const auto& entry : records) {
        if (++count > 128) break;
        const auto& c = entry.candidate;
        if (!isMbid(c.artistMbid) || !opaqueKey(entry.localAssetKey) ||
            !digest(entry.bytesSha256, "sha256:", 64) ||
            !c.rights.localCachePermitted ||
            c.rights.status != RightsStatus::VerifiedPermitted) continue;
        const std::vector<std::string> values = {
            hexEncode(c.artistMbid), hexEncode(c.provider),
            hexEncode(c.providerAssetId), hexEncode(c.sourceUrl),
            std::to_string(static_cast<int>(c.kind)),
            std::to_string(c.width), std::to_string(c.height),
            std::to_string(c.canonicalArtistConfirmed ? 1 : 0),
            std::to_string(c.actualPhotograph ? 1 : 0),
            std::to_string(c.cleanImage ? 1 : 0),
            std::to_string(c.subjectCropSafe ? 1 : 0),
            std::to_string(c.placeholder ? 1 : 0),
            std::to_string(c.humanReviewedAsDistinctInArtistSet ? 1 : 0),
            std::to_string(static_cast<int>(c.rights.status)),
            hexEncode(c.rights.licenseId), hexEncode(c.rights.evidenceUrl),
            hexEncode(c.rights.author), hexEncode(c.rights.attribution),
            std::to_string(c.rights.artistHeroPermitted ? 1 : 0),
            std::to_string(c.rights.localCachePermitted ? 1 : 0),
            std::to_string(c.rights.validUntilEpoch),
            hexEncode(c.sha256), hexEncode(c.phash64),
            std::to_string(c.observedAtEpoch),
            hexEncode(entry.localAssetKey), hexEncode(entry.bytesSha256),
            std::to_string(entry.expiresAtEpoch)
        };
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i) out << '|';
            out << values[i];
        }
        out << '\n';
    }
    return out.str();
}

std::vector<CachedPhoto> decodeCacheManifest(const std::string& wire) {
    std::vector<CachedPhoto> result;
    if (wire.size() > 1024 * 1024) return result;
    std::istringstream input(wire);
    std::string line;
    if (!std::getline(input, line) || line != cacheHeader) return result;
    while (std::getline(input, line) && result.size() < 128) {
        if (line.empty() || line.size() > 24000) continue;
        try {
            const auto parts = split(line);
            if (parts.size() != 27) continue;
            const auto stringField = [&](std::size_t i) {
                auto value = hexDecode(parts.at(i));
                if (!value) throw std::invalid_argument("invalid hex field");
                return *value;
            };
            const auto intField = [&](std::size_t i) { return number(parts.at(i)); };
            const auto boolField = [&](std::size_t i) { return flag(parts.at(i)); };
            const auto kind = intField(4), status = intField(13);
            if (kind < 0 || kind > 2 || status != 1 ||
                intField(5) < 0 || intField(5) > 16384 ||
                intField(6) < 0 || intField(6) > 16384) continue;
            CachedPhoto entry;
            auto& c = entry.candidate;
            c.artistMbid = stringField(0);
            c.provider = stringField(1);
            c.providerAssetId = stringField(2);
            c.sourceUrl = stringField(3);
            c.kind = static_cast<PhotoKind>(kind);
            c.width = static_cast<int>(intField(5));
            c.height = static_cast<int>(intField(6));
            c.canonicalArtistConfirmed = boolField(7);
            c.actualPhotograph = boolField(8);
            c.cleanImage = boolField(9);
            c.subjectCropSafe = boolField(10);
            c.placeholder = boolField(11);
            c.humanReviewedAsDistinctInArtistSet = boolField(12);
            c.rights.status = static_cast<RightsStatus>(status);
            c.rights.licenseId = stringField(14);
            c.rights.evidenceUrl = stringField(15);
            c.rights.author = stringField(16);
            c.rights.attribution = stringField(17);
            c.rights.artistHeroPermitted = boolField(18);
            c.rights.localCachePermitted = boolField(19);
            c.rights.validUntilEpoch = intField(20);
            c.sha256 = stringField(21);
            c.phash64 = stringField(22);
            c.observedAtEpoch = intField(23);
            entry.localAssetKey = stringField(24);
            entry.bytesSha256 = stringField(25);
            entry.expiresAtEpoch = intField(26);
            entry.verifiedBytesPresent = false;
            Request validation;
            validation.artistMbid = c.artistMbid;
            validation.nowEpoch = 0;
            if (!opaqueKey(entry.localAssetKey) ||
                !digest(entry.bytesSha256, "sha256:", 64) ||
                !c.rights.localCachePermitted ||
                entry.expiresAtEpoch <= 0 || !validate(c, validation).empty())
                continue;
            result.push_back(std::move(entry));
        } catch (const std::exception&) {
            // Corrupted/truncated/out-of-schema rows cannot become eligible art.
        }
    }
    return result;
}

} // namespace vinyl::artwork
