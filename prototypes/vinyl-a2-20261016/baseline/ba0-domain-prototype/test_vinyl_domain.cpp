#include "VinylDomainTypes.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <type_traits>

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

template <typename T>
T mustParse(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    static_assert(!std::is_same<ArtistMbid, RecordingMbid>::value,
                  "artist and recording ids collapsed");
    static_assert(!std::is_same<ReleaseGroupMbid, ReleaseMbid>::value,
                  "release group and release ids collapsed");
    static_assert(!std::is_same<ReleaseTrackMbid, RecordingMbid>::value,
                  "release track and recording ids collapsed");
    static_assert(!std::is_same<ProfileScopeId, PlayInstanceId>::value,
                  "profile and play-instance identity collapsed");

    const auto profile = mustParse(
        ProfileScopeId::parse(QStringLiteral("profile-A")),
        "valid host profile scope should parse");
    require(profile.value() == QStringLiteral("profile-A"),
            "profile scope should preserve host-owned spelling");
    const auto invalidProfile = ProfileScopeId::parse(QStringLiteral(" profile-A "));
    require(!invalidProfile.ok()
                && invalidProfile.error.code == DomainErrorCode::InvalidProfileScopeId,
            "profile scope should fail typed on unsafe opaque token");

    const auto playInstance = mustParse(
        PlayInstanceId::parse(QStringLiteral("play-20261006-0001")),
        "valid durable play instance should parse");
    require(playInstance.value() == QStringLiteral("play-20261006-0001"),
            "play instance should preserve host-owned spelling");
    const auto invalidPlayInstance = PlayInstanceId::parse(QString());
    require(!invalidPlayInstance.ok()
                && invalidPlayInstance.error.code == DomainErrorCode::InvalidPlayInstanceId,
            "play instance should fail typed when missing");

    const auto artist = mustParse(
        ArtistMbid::parse(QStringLiteral("{AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA}")),
        "valid artist MBID should parse");
    require(artist.toString() == QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
            "MBID spelling should canonicalize to lowercase without braces");

    const auto missing = RecordingMbid::parse(QString());
    require(!missing.ok(), "missing MBID should fail");
    require(missing.error.code == DomainErrorCode::MissingCanonicalId,
            "missing MBID should return MissingCanonicalId");

    const auto malformed = RecordingMbid::parse(QStringLiteral("not-a-mbid"));
    require(!malformed.ok(), "malformed MBID should fail");
    require(malformed.error.code == DomainErrorCode::InvalidMbid,
            "malformed MBID should return InvalidMbid");

    const auto nil = RecordingMbid::parse(
        QStringLiteral("00000000-0000-0000-0000-000000000000"));
    require(!nil.ok(), "nil UUID should not be accepted as canonical identity");

    const auto recording = mustParse(
        RecordingMbid::parse(QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "recording MBID should parse");
    const auto recordingRef = CanonicalEntityRef::from(recording);
    require(recordingRef.kind() == CanonicalEntityKind::Recording,
            "generic canonical ref should preserve recording kind");
    require(recordingRef.mbidString() == recording.toString(),
            "generic canonical ref should preserve MBID");

    const auto sameRawArtist = mustParse(
        ArtistMbid::parse(recording.toString()),
        "same UUID text may exist under a different typed entity boundary");
    const auto artistRef = CanonicalEntityRef::from(sameRawArtist);
    require(artistRef != recordingRef,
            "same UUID under different entity kinds must remain distinct");

    const auto releaseGroup = mustParse(
        ReleaseGroupMbid::parse(QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "release-group MBID should parse");
    const auto release = mustParse(
        ReleaseMbid::parse(QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "release MBID should parse");
    const auto releaseTrack = mustParse(
        ReleaseTrackMbid::parse(QStringLiteral("44444444-4444-4444-8444-444444444444")),
        "release-track MBID should parse");

    const auto coordinate = ReleaseTrackCoordinate::create(
        releaseGroup,
        release,
        std::optional<ReleaseTrackMbid>{releaseTrack},
        recording,
        2,
        7);
    require(coordinate.ok(), "valid release-track coordinate should be accepted");
    require(coordinate.value->mediumPosition() == 2,
            "medium position should be preserved");
    require(coordinate.value->trackPosition() == 7,
            "track position should be preserved");
    require(coordinate.value->recordingMbid() == recording,
            "coordinate should preserve canonical recording identity");
    require(coordinate.value->releaseTrackMbid().has_value(),
            "coordinate should preserve release-track identity when available");
    require(*coordinate.value->releaseTrackMbid() == releaseTrack,
            "coordinate should keep release-track identity separate from Recording");

    const auto coordinateWithoutReleaseTrack = ReleaseTrackCoordinate::create(
        releaseGroup, release, std::nullopt, recording, 1, 1);
    require(coordinateWithoutReleaseTrack.ok(),
            "release-track MBID may be absent when upstream context lacks it");
    require(!coordinateWithoutReleaseTrack.value->releaseTrackMbid().has_value(),
            "absent release-track identity must remain explicitly absent");

    const auto badMedium = ReleaseTrackCoordinate::create(
        releaseGroup,
        release,
        std::optional<ReleaseTrackMbid>{releaseTrack},
        recording,
        0,
        7);
    require(!badMedium.ok(), "zero medium position should fail");
    require(badMedium.error.code == DomainErrorCode::InvalidTrackCoordinate,
            "bad medium should use typed coordinate error");
    require(badMedium.error.field == QStringLiteral("mediumPosition"),
            "bad medium should identify the failing field");

    const auto badTrack = ReleaseTrackCoordinate::create(
        releaseGroup,
        release,
        std::optional<ReleaseTrackMbid>{releaseTrack},
        recording,
        1,
        0);
    require(!badTrack.ok(), "zero track position should fail");
    require(badTrack.error.field == QStringLiteral("trackPosition"),
            "bad track should identify the failing field");

    const auto provider = mustParse(
        ProviderId::parse(QStringLiteral("YouTube-Music")),
        "safe provider ID should parse");
    require(provider.value() == QStringLiteral("youtube-music"),
            "provider ID should normalize to lowercase");

    const auto unsafeProvider = ProviderId::parse(QStringLiteral("youtube music"));
    require(!unsafeProvider.ok(), "provider ID with spaces should fail");
    require(unsafeProvider.error.code == DomainErrorCode::InvalidProviderId,
            "unsafe provider ID should use typed provider error");

    const auto providerItem = mustParse(
        ProviderItemId::parse(QStringLiteral("AbC_123-xYz")),
        "opaque provider item should parse");
    require(providerItem.value() == QStringLiteral("AbC_123-xYz"),
            "provider item ID must preserve provider-owned case and punctuation");

    const auto blankProviderItem = ProviderItemId::parse(QStringLiteral(" AbC_123-xYz"));
    require(!blankProviderItem.ok(),
            "provider item with surrounding whitespace should fail");

    const ProviderItemRef providerItemRef(provider, providerItem);
    const ProviderMapping mapping(recordingRef, providerItemRef);
    require(mapping.canonical() == recordingRef,
            "provider mapping should preserve canonical Vinyl identity");
    require(mapping.providerItem().providerId().value() == QStringLiteral("youtube-music"),
            "provider mapping should preserve provider identity");
    require(mapping.providerItem().itemId().value() == QStringLiteral("AbC_123-xYz"),
            "provider mapping should preserve opaque provider item identity");
    require(mapping.canonical().mbidString() != mapping.providerItem().itemId().value(),
            "provider item ID must not replace canonical MBID");

    require(domainErrorCodeName(DomainErrorCode::InvalidMbid)
                == QStringLiteral("invalid_mbid"),
            "domain errors should expose stable snake_case names");
    require(canonicalEntityKindName(CanonicalEntityKind::ReleaseGroup)
                == QStringLiteral("release_group"),
            "entity kinds should expose stable snake_case names");

    qInfo().noquote() << "VINYL_BA0_DOMAIN_OK checks=" << g_checks;
    return 0;
}
