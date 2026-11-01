#include "VinylPlaylist.h"
#include "VinylQueue.h"

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

template <typename T>
T mustPlaylist(vinyl::PlaylistResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::ProfileScopeId profile(const QString &value)
{
    return mustDomain(vinyl::ProfileScopeId::parse(value),
                      "profile fixture should parse");
}

vinyl::PlaylistId playlistId(const QString &value)
{
    return mustPlaylist(vinyl::PlaylistId::parse(value),
                        "playlist ID fixture should parse");
}

vinyl::PlaylistItemId itemId(const QString &value)
{
    return mustPlaylist(vinyl::PlaylistItemId::parse(value),
                        "playlist item ID fixture should parse");
}

vinyl::PlaylistItem item(const QString &id,
                         vinyl::RecordingMbid recording,
                         std::optional<vinyl::ReleaseTrackCoordinate> releaseContext = std::nullopt)
{
    return mustPlaylist(
        vinyl::PlaylistItem::create(
            itemId(id),
            std::move(recording),
            2000,
            std::move(releaseContext)),
        "playlist item fixture should construct");
}

vinyl::Playlist playlist(const QString &profileId,
                         const QString &id,
                         const QString &name,
                         vinyl::PlaylistProvenanceKind provenance,
                         qint64 createdAtMs = 1000)
{
    return mustPlaylist(
        vinyl::Playlist::create(
            profile(profileId),
            playlistId(id),
            name,
            createdAtMs,
            provenance),
        "playlist fixture should construct");
}

void requireOrder(const vinyl::Playlist &playlist,
                  const QStringList &expected)
{
    require(playlist.size() == expected.size(),
            "playlist size should match expected order length");

    for (int i = 0; i < expected.size(); ++i) {
        require(playlist.items().at(i).itemId().value() == expected.at(i),
                "playlist order should match exact PlaylistItemId sequence");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "verification harness must execute against Qt 6.11.1");

    static_assert(!std::is_same<PlaylistItemId, RecordingMbid>::value,
                  "playlist item identity must remain distinct from Recording MBID");
    static_assert(!std::is_convertible<RecordingMbid, PlaylistItemId>::value,
                  "Recording MBID cannot become playlist item identity");
    static_assert(!std::is_same<PlaylistItemId, QueueOccurrenceId>::value,
                  "playlist item identity must remain distinct from queue occurrence identity");
    static_assert(!std::is_convertible<QueueOccurrenceId, PlaylistItemId>::value,
                  "queue occurrence identity cannot become playlist item identity");
    static_assert(!std::is_convertible<ProviderItemRef, RecordingMbid>::value,
                  "provider identity cannot replace canonical Recording identity");

    const RecordingMbid a = mustDomain(
        RecordingMbid::parse(QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "recording a should parse");
    const RecordingMbid b = mustDomain(
        RecordingMbid::parse(QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "recording b should parse");
    const RecordingMbid c = mustDomain(
        RecordingMbid::parse(QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "recording c should parse");

    const auto invalidPlaylist = PlaylistId::parse(QStringLiteral(" bad "));
    require(!invalidPlaylist.ok(), "playlist ID with surrounding whitespace should fail");
    require(invalidPlaylist.error.code == PlaylistErrorCode::InvalidPlaylistId,
            "invalid playlist ID should use typed error");

    const auto invalidItem = PlaylistItemId::parse(QString());
    require(!invalidItem.ok(), "blank playlist item ID should fail");
    require(invalidItem.error.code == PlaylistErrorCode::InvalidPlaylistItemId,
            "invalid playlist item ID should use typed error");

    Playlist duplicates = playlist(
        QStringLiteral("local"),
        QStringLiteral("duplicates"),
        QStringLiteral("Duplicates"),
        PlaylistProvenanceKind::Manual);

    require(duplicates.append(item(QStringLiteral("a-1"), a)).ok,
            "first duplicate recording occurrence should append");
    require(duplicates.append(item(QStringLiteral("a-2"), a)).ok,
            "second duplicate recording occurrence should append");
    require(duplicates.size() == 2,
            "duplicate Recording MBIDs must remain separate playlist items");
    require(duplicates.items().at(0).recordingMbid() == a
                && duplicates.items().at(1).recordingMbid() == a,
            "duplicate playlist items should preserve the same canonical Recording MBID");
    require(duplicates.items().at(0).itemId() != duplicates.items().at(1).itemId(),
            "duplicate recordings must remain separately addressable");

    const PlaylistMutationStatus duplicateItemId =
        duplicates.append(item(QStringLiteral("a-1"), b));
    require(!duplicateItemId.ok,
            "reusing PlaylistItemId should fail even for another recording");
    require(duplicateItemId.error.code == PlaylistErrorCode::DuplicatePlaylistItemId,
            "duplicate PlaylistItemId should use typed error");

    require(duplicates.remove(itemId(QStringLiteral("a-1"))).ok,
            "removing one duplicate should succeed");
    require(duplicates.size() == 1,
            "removing one duplicate should leave the other");
    require(duplicates.items().constFirst().itemId().value()
                == QStringLiteral("a-2"),
            "removing one duplicate must leave the exact other PlaylistItemId");
    require(duplicates.items().constFirst().recordingMbid() == a,
            "remaining duplicate must preserve canonical Recording identity");

    Playlist order = playlist(
        QStringLiteral("local"),
        QStringLiteral("order"),
        QStringLiteral("Order"),
        PlaylistProvenanceKind::Manual);
    require(order.append(item(QStringLiteral("a-first"), a)).ok, "append a-first");
    require(order.append(item(QStringLiteral("b"), b)).ok, "append b");
    require(order.append(item(QStringLiteral("a-second"), a)).ok, "append a-second");
    require(order.append(item(QStringLiteral("c"), c)).ok, "append c");
    requireOrder(order, {QStringLiteral("a-first"),
                         QStringLiteral("b"),
                         QStringLiteral("a-second"),
                         QStringLiteral("c")});

    const PlaylistMutationStatus moved =
        order.move(itemId(QStringLiteral("a-second")), 0);
    require(moved.ok && moved.changed,
            "moving one duplicate should mutate");
    requireOrder(order, {QStringLiteral("a-second"),
                         QStringLiteral("a-first"),
                         QStringLiteral("b"),
                         QStringLiteral("c")});
    require(order.items().at(1).recordingMbid() == a,
            "moving one duplicate must not retarget the other duplicate");

    const PlaylistMutationStatus sameMove =
        order.move(itemId(QStringLiteral("a-first")), 1);
    require(sameMove.ok && !sameMove.changed,
            "moving an item to its current final index should be a successful no-op");

    require(order.insert(2, item(QStringLiteral("b-2"), b)).ok,
            "explicit insertion should succeed");
    requireOrder(order, {QStringLiteral("a-second"),
                         QStringLiteral("a-first"),
                         QStringLiteral("b-2"),
                         QStringLiteral("b"),
                         QStringLiteral("c")});

    const PlaylistMutationStatus badInsert =
        order.insert(99, item(QStringLiteral("outside"), c));
    require(!badInsert.ok && badInsert.error.code == PlaylistErrorCode::IndexOutOfRange,
            "out-of-range insertion should fail with typed error");

    const PlaylistMutationStatus badMove =
        order.move(itemId(QStringLiteral("c")), order.size());
    require(!badMove.ok && badMove.error.code == PlaylistErrorCode::IndexOutOfRange,
            "move destination outside final order should fail with typed error");

    const PlaylistMutationStatus missingRemove =
        order.remove(itemId(QStringLiteral("missing")));
    require(!missingRemove.ok
                && missingRemove.error.code == PlaylistErrorCode::PlaylistItemNotFound,
            "removing an unknown PlaylistItemId should fail with typed error");

    const ReleaseGroupMbid rg = mustDomain(
        ReleaseGroupMbid::parse(QStringLiteral("44444444-4444-4444-8444-444444444444")),
        "release-group fixture should parse");
    const ReleaseMbid release = mustDomain(
        ReleaseMbid::parse(QStringLiteral("55555555-5555-4555-8555-555555555555")),
        "release fixture should parse");
    const ReleaseTrackMbid releaseTrack = mustDomain(
        ReleaseTrackMbid::parse(QStringLiteral("66666666-6666-4666-8666-666666666666")),
        "release-track fixture should parse");

    const ReleaseTrackCoordinate context = mustDomain(
        ReleaseTrackCoordinate::create(
            rg,
            release,
            std::optional<ReleaseTrackMbid>{releaseTrack},
            a,
            1,
            7),
        "release context should construct");

    const PlaylistItem contextual = mustPlaylist(
        PlaylistItem::create(
            itemId(QStringLiteral("context-a")),
            a,
            2000,
            std::optional<ReleaseTrackCoordinate>{context}),
        "matching release context should construct");
    require(contextual.releaseContext().has_value(),
            "optional release context should remain attached");
    require(contextual.releaseContext()->trackPosition() == 7,
            "release context should preserve track position");

    const auto mismatch = PlaylistItem::create(
        itemId(QStringLiteral("context-bad")),
        b,
        2000,
        std::optional<ReleaseTrackCoordinate>{context});
    require(!mismatch.ok(), "mismatched release context should fail closed");
    require(mismatch.error.code == PlaylistErrorCode::RecordingContextMismatch,
            "recording/context mismatch should use typed error");

    Playlist manual = playlist(
        QStringLiteral("profile-a"),
        QStringLiteral("manual"),
        QStringLiteral("Manual"),
        PlaylistProvenanceKind::Manual);
    Playlist generated = playlist(
        QStringLiteral("profile-a"),
        QStringLiteral("generated"),
        QStringLiteral("Generated"),
        PlaylistProvenanceKind::Generated);
    require(manual.append(item(QStringLiteral("manual-a"), a)).ok,
            "manual playlist should accept recording");
    require(generated.append(item(QStringLiteral("generated-a"), a)).ok,
            "generated playlist should accept same recording");
    require(manual.items().constFirst().recordingMbid()
                == generated.items().constFirst().recordingMbid(),
            "playlist provenance must not alter canonical Recording identity");
    require(manual.provenance() == PlaylistProvenanceKind::Manual,
            "manual provenance should remain explicit");
    require(generated.provenance() == PlaylistProvenanceKind::Generated,
            "generated provenance should remain explicit");

    const PlaylistItemId playlistItemId = itemId(QStringLiteral("shared-text-id"));
    require(playlistItemId.value() == QStringLiteral("shared-text-id"),
            "playlist item opaque text should remain preserved while its strong type stays distinct from QueueOccurrenceId");

    require(playlistErrorCodeName(PlaylistErrorCode::DuplicatePlaylistItemId)
                == QStringLiteral("duplicate_playlist_item_id"),
            "playlist errors should expose stable snake_case names");
    require(playlistProvenanceKindName(PlaylistProvenanceKind::Generated)
                == QStringLiteral("generated"),
            "playlist provenance should expose stable names");

    qInfo().noquote()
        << "VINYL_BA5B_PLAYLIST_OK checks=" << g_checks
        << "qt=" << qVersion();
    return 0;
}
