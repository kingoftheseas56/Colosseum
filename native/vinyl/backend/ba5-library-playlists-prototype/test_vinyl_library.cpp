#include "VinylLibrary.h"

#include <QCoreApplication>
#include <QDebug>
#include <QtGlobal>

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
T mustDomain(vinyl::DomainResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustLibrary(vinyl::LibraryResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::ProfileScopeId profile(const QString &value)
{
    return mustDomain(vinyl::ProfileScopeId::parse(value),
                      "profile scope fixture should parse");
}

vinyl::LibraryEntry entry(vinyl::CanonicalEntityRef canonical, qint64 savedAtMs)
{
    return mustLibrary(vinyl::LibraryEntry::create(std::move(canonical), savedAtMs),
                       "library entry fixture should construct");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "verification harness must execute against Qt 6.11.1");

    const ArtistMbid artist = mustDomain(
        ArtistMbid::parse(QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "artist fixture should parse");
    const ReleaseGroupMbid album = mustDomain(
        ReleaseGroupMbid::parse(QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "release-group fixture should parse");
    const RecordingMbid recording = mustDomain(
        RecordingMbid::parse(QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "recording fixture should parse");
    const ReleaseMbid release = mustDomain(
        ReleaseMbid::parse(QStringLiteral("44444444-4444-4444-8444-444444444444")),
        "release fixture should parse");

    const auto badBlank = ProfileScopeId::parse(QString());
    require(!badBlank.ok(), "blank profile scope should fail");
    require(badBlank.error.code == DomainErrorCode::InvalidProfileScopeId,
            "blank profile scope should use shared BA0 typed error");

    const auto badWhitespace = ProfileScopeId::parse(QStringLiteral(" local "));
    require(!badWhitespace.ok(), "profile scope with surrounding whitespace should fail");
    require(badWhitespace.error.code == DomainErrorCode::InvalidProfileScopeId,
            "invalid profile scope should use shared BA0 typed error");

    VinylLibrary library(profile(QStringLiteral("local")));
    const LibraryMutationStatus saveArtist =
        library.save(entry(CanonicalEntityRef::from(artist), 1000));
    const LibraryMutationStatus saveAlbum =
        library.save(entry(CanonicalEntityRef::from(album), 2000));
    const LibraryMutationStatus saveRecording =
        library.save(entry(CanonicalEntityRef::from(recording), 3000));

    require(saveArtist.ok && saveArtist.changed, "artist save should mutate");
    require(saveAlbum.ok && saveAlbum.changed, "release-group save should mutate");
    require(saveRecording.ok && saveRecording.changed, "recording save should mutate");
    require(library.size() == 3, "three canonical memberships should be stored");
    require(library.contains(CanonicalEntityRef::from(artist)),
            "artist membership should be queryable");
    require(library.contains(CanonicalEntityRef::from(album)),
            "release-group membership should be queryable");
    require(library.contains(CanonicalEntityRef::from(recording)),
            "recording membership should be queryable");

    VinylLibrary albumOnly(profile(QStringLiteral("album-only")));
    require(albumOnly.save(entry(CanonicalEntityRef::from(album), 4000)).ok,
            "release-group save should succeed");
    require(albumOnly.size() == 1,
            "saving an album family should create exactly one membership");
    require(!albumOnly.contains(CanonicalEntityRef::from(recording)),
            "saving a release group must not implicitly save its recordings");

    const LibraryEntry *beforeDuplicate =
        library.find(CanonicalEntityRef::from(recording));
    require(beforeDuplicate != nullptr, "saved recording should be findable");
    require(beforeDuplicate->savedAtMs() == 3000,
            "original recording save timestamp should be preserved");

    const LibraryMutationStatus duplicate =
        library.save(entry(CanonicalEntityRef::from(recording), 9999));
    require(duplicate.ok && !duplicate.changed,
            "duplicate canonical save should be a successful no-op");
    require(library.size() == 3,
            "duplicate canonical save must not create another membership");

    const LibraryEntry *afterDuplicate =
        library.find(CanonicalEntityRef::from(recording));
    require(afterDuplicate != nullptr && afterDuplicate->savedAtMs() == 3000,
            "duplicate save must preserve the original saved timestamp");

    const auto unsupported =
        LibraryEntry::create(CanonicalEntityRef::from(release), 5000);
    require(!unsupported.ok(),
            "concrete Release identity should not become BA5-A Library membership");
    require(unsupported.error.code == LibraryErrorCode::UnsupportedCanonicalEntityKind,
            "unsupported canonical kind should use typed error");

    const auto invalidTimestamp =
        LibraryEntry::create(CanonicalEntityRef::from(recording), 0);
    require(!invalidTimestamp.ok(), "non-positive saved timestamp should fail");
    require(invalidTimestamp.error.code == LibraryErrorCode::InvalidSavedAt,
            "invalid saved timestamp should use typed error");

    const RecordingMbid missingRecording = mustDomain(
        RecordingMbid::parse(QStringLiteral("55555555-5555-4555-8555-555555555555")),
        "missing-recording fixture should parse");
    const LibraryMutationStatus missingRemove =
        library.remove(CanonicalEntityRef::from(missingRecording));
    require(!missingRemove.ok, "removing unknown membership should fail");
    require(missingRemove.error.code == LibraryErrorCode::EntryNotFound,
            "unknown membership removal should use typed error");

    VinylLibrary firstProfile(profile(QStringLiteral("profile-a")));
    VinylLibrary secondProfile(profile(QStringLiteral("profile-b")));
    require(firstProfile.save(entry(CanonicalEntityRef::from(recording), 6000)).ok,
            "first profile should save recording");
    require(secondProfile.save(entry(CanonicalEntityRef::from(recording), 7000)).ok,
            "second profile should save the same recording independently");
    require(firstProfile.remove(CanonicalEntityRef::from(recording)).ok,
            "first profile should remove its own recording");
    require(!firstProfile.contains(CanonicalEntityRef::from(recording)),
            "first profile membership should be removed");
    require(secondProfile.contains(CanonicalEntityRef::from(recording)),
            "second profile membership must remain untouched");

    const auto providerId = mustDomain(
        ProviderId::parse(QStringLiteral("example.provider")),
        "provider ID fixture should parse");
    const auto providerItemId = mustDomain(
        ProviderItemId::parse(QStringLiteral("provider-track-123")),
        "provider item fixture should parse");
    using ProviderRef = ProviderItemRef;
    static_assert(!std::is_convertible<ProviderRef, CanonicalEntityRef>::value,
                  "provider references must not convert to canonical Library identity");
    const ProviderRef providerRef(providerId, providerItemId);
    require(providerRef.itemId().value() == QStringLiteral("provider-track-123"),
            "provider reference remains separate provenance data");
    require(libraryErrorCodeName(LibraryErrorCode::EntryNotFound)
                == QStringLiteral("entry_not_found"),
            "Library errors should expose stable snake_case names");

    qInfo().noquote()
        << "VINYL_BA5A_LIBRARY_OK checks=" << g_checks
        << "qt=" << qVersion();
    return 0;
}
