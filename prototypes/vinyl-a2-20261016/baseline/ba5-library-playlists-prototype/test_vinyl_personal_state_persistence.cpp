#include "VinylPersonalStatePersistence.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

template <typename T>
T mustPersist(vinyl::PersonalStatePersistenceResult<T> result,
              const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    const ProfileScopeId profile = mustDomain(
        ProfileScopeId::parse(QStringLiteral("profile-a")), "profile parse");

    const RecordingMbid recording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "recording parse");
    const ArtistMbid artist = mustDomain(
        ArtistMbid::parse(
            QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "artist parse");
    const PersonalDisplaySnapshot recordingDisplay{
        QStringLiteral("Fixture Song"),
        QStringLiteral("Fixture Artist")};
    const PersonalDisplaySnapshot playlistDisplay{
        QStringLiteral("Playlist Fixture Song"),
        QStringLiteral("Playlist Fixture Artist")};

    VinylLibrary library(profile);
    require(library.save(mustLibrary(
        LibraryEntry::create(
            CanonicalEntityRef::from(recording), 1000, recordingDisplay),
        "recording library entry")).ok,
        "recording library save");
    require(library.save(mustLibrary(
        LibraryEntry::create(CanonicalEntityRef::from(artist), 1500),
        "artist library entry")).ok,
        "artist library save");

    Playlist playlist = mustPlaylist(
        Playlist::create(
            profile,
            mustPlaylist(PlaylistId::parse(QStringLiteral("favorites")), "playlist id"),
            QStringLiteral("Favorites"),
            1000,
            PlaylistProvenanceKind::Manual),
        "playlist create");
    require(playlist.append(mustPlaylist(
        PlaylistItem::create(
            mustPlaylist(PlaylistItemId::parse(QStringLiteral("item-1")), "item id"),
            recording,
            2000,
            std::nullopt,
            playlistDisplay),
        "playlist item")).ok,
        "playlist append");

    Playlist alpha = mustPlaylist(
        Playlist::create(
            profile,
            mustPlaylist(PlaylistId::parse(QStringLiteral("alpha")), "alpha id"),
            QStringLiteral("Alpha"),
            900,
            PlaylistProvenanceKind::Manual),
        "alpha playlist create");

    QVector<Playlist> playlists{playlist, alpha};
    const QByteArray first = mustPersist(
        VinylPersonalStatePersistence::capture(profile, library, playlists),
        "capture");

    const QByteArray second = mustPersist(
        VinylPersonalStatePersistence::capture(profile, library, playlists),
        "second capture");
    require(first == second,
            "same BA5 state must serialize to identical canonical bytes");

    const QJsonObject emittedRoot = QJsonDocument::fromJson(first).object();
    require(emittedRoot.value(QStringLiteral("version")).toInt()
                == VinylPersonalStatePersistence::SchemaVersion,
            "writer must emit the current BA5 schema version");

    VinylLibrary reorderedLibrary(profile);
    require(reorderedLibrary.save(mustLibrary(
        LibraryEntry::create(CanonicalEntityRef::from(artist), 1500),
        "reordered artist")).ok,
        "reordered artist save");
    require(reorderedLibrary.save(mustLibrary(
        LibraryEntry::create(
            CanonicalEntityRef::from(recording), 1000, recordingDisplay),
        "reordered recording")).ok,
        "reordered recording save");
    QVector<Playlist> reorderedPlaylists{alpha, playlist};
    const QByteArray reorderedBytes = mustPersist(
        VinylPersonalStatePersistence::capture(
            profile, reorderedLibrary, reorderedPlaylists),
        "reordered BA5 capture");
    require(first == reorderedBytes,
            "canonical BA5 serialization must ignore non-semantic insertion order");

    const RestoredVinylPersonalState restored = mustPersist(
        VinylPersonalStatePersistence::restore(profile, first),
        "round-trip restore");
    require(restored.library.size() == 2,
            "library memberships must round-trip");
    require(restored.playlists.size() == 2,
            "playlists must round-trip");
    const Playlist &restoredFavorites = restored.playlists.at(1);
    require(restoredFavorites.name() == QStringLiteral("Favorites"),
            "playlist name must round-trip");
    require(restoredFavorites.createdAtMs() == 1000,
            "playlist creation time must round-trip");
    require(restoredFavorites.items().constFirst().addedAtMs() == 2000,
            "playlist item added-at must round-trip");
    const LibraryEntry *restoredRecording =
        restored.library.find(CanonicalEntityRef::from(recording));
    require(restoredRecording
                && restoredRecording->displaySnapshot().has_value()
                && *restoredRecording->displaySnapshot() == recordingDisplay,
            "Library display snapshot must round-trip");
    require(restoredFavorites.items().constFirst().displaySnapshot().has_value()
                && *restoredFavorites.items().constFirst().displaySnapshot()
                    == playlistDisplay,
            "Playlist display snapshot must round-trip");

    const auto malformed = VinylPersonalStatePersistence::restore(
        profile, QByteArrayLiteral("{not-json"));
    require(!malformed.ok()
                && malformed.error.code
                    == PersonalStatePersistenceErrorCode::MalformedSnapshot,
            "malformed JSON must return a typed corruption error");

    auto previousSchemaRoot = QJsonDocument::fromJson(first).object();
    QJsonArray previousLibrary;
    for (const QJsonValue &value :
         previousSchemaRoot.value(QStringLiteral("library")).toArray()) {
        QJsonObject entry = value.toObject();
        entry.remove(QStringLiteral("displaySnapshot"));
        previousLibrary.append(entry);
    }
    previousSchemaRoot.insert(QStringLiteral("library"), previousLibrary);
    QJsonArray previousPlaylists;
    for (const QJsonValue &value :
         previousSchemaRoot.value(QStringLiteral("playlists")).toArray()) {
        QJsonObject playlistObject = value.toObject();
        QJsonArray previousItems;
        for (const QJsonValue &itemValue :
             playlistObject.value(QStringLiteral("items")).toArray()) {
            QJsonObject item = itemValue.toObject();
            item.remove(QStringLiteral("displaySnapshot"));
            previousItems.append(item);
        }
        playlistObject.insert(QStringLiteral("items"), previousItems);
        previousPlaylists.append(playlistObject);
    }
    previousSchemaRoot.insert(QStringLiteral("playlists"), previousPlaylists);

    QJsonObject v2Root = previousSchemaRoot;
    v2Root.insert(QStringLiteral("version"), 2);
    const QByteArray v2Fixture =
        QJsonDocument(v2Root).toJson(QJsonDocument::Compact);
    const RestoredVinylPersonalState restoredV2 = mustPersist(
        VinylPersonalStatePersistence::restore(profile, v2Fixture),
        "previous-version v2 fixture should restore");
    require(!restoredV2.library.find(CanonicalEntityRef::from(recording))
                 ->displaySnapshot().has_value(),
            "v2 restore must represent absent display fallback as unknown");

    QJsonObject v1Root = previousSchemaRoot;
    v1Root.insert(QStringLiteral("version"), 1);
    const QByteArray v1Fixture =
        QJsonDocument(v1Root).toJson(QJsonDocument::Compact);
    const RestoredVinylPersonalState migrated = mustPersist(
        VinylPersonalStatePersistence::restore(profile, v1Fixture),
        "supported v1 fixture should migrate in memory");
    const QByteArray migratedBytes = mustPersist(
        VinylPersonalStatePersistence::capture(
            profile, migrated.library, migrated.playlists),
        "migrated BA5 state should reserialize");
    require(QJsonDocument::fromJson(migratedBytes)
                .object().value(QStringLiteral("version")).toInt()
                == VinylPersonalStatePersistence::SchemaVersion,
            "legacy reader must upgrade to current schema on next write");

    const auto truncated = VinylPersonalStatePersistence::restore(
        profile, first.left(first.size() - 1));
    require(!truncated.ok()
                && truncated.error.code
                    == PersonalStatePersistenceErrorCode::MalformedSnapshot,
            "truncated BA5 file must return typed malformed_snapshot");

    QJsonObject corruptDisplayRoot = QJsonDocument::fromJson(first).object();
    QJsonArray corruptLibrary =
        corruptDisplayRoot.value(QStringLiteral("library")).toArray();
    bool corruptedDisplay = false;
    for (qsizetype i = 0; i < corruptLibrary.size(); ++i) {
        QJsonObject entry = corruptLibrary.at(i).toObject();
        if (!entry.value(QStringLiteral("displaySnapshot")).isObject())
            continue;
        QJsonObject display =
            entry.value(QStringLiteral("displaySnapshot")).toObject();
        display.insert(QStringLiteral("title"), QJsonArray{});
        entry.insert(QStringLiteral("displaySnapshot"), display);
        corruptLibrary.replace(i, entry);
        corruptedDisplay = true;
        break;
    }
    require(corruptedDisplay, "display corruption fixture should be prepared");
    corruptDisplayRoot.insert(QStringLiteral("library"), corruptLibrary);
    const auto corruptDisplay = VinylPersonalStatePersistence::restore(
        profile,
        QJsonDocument(corruptDisplayRoot).toJson(QJsonDocument::Compact));
    require(!corruptDisplay.ok()
                && corruptDisplay.error.code
                    == PersonalStatePersistenceErrorCode::MalformedSnapshot,
            "typed display-snapshot corruption must fail closed");

    QJsonObject unknownRoot = QJsonDocument::fromJson(first).object();
    unknownRoot.insert(QStringLiteral("futureField"), 1);
    const auto unknown = VinylPersonalStatePersistence::restore(
        profile, QJsonDocument(unknownRoot).toJson(QJsonDocument::Compact));
    require(!unknown.ok()
                && unknown.error.code
                    == PersonalStatePersistenceErrorCode::MalformedSnapshot,
            "unknown fields must fail closed within a declared schema version");

    QJsonObject futureRoot = QJsonDocument::fromJson(first).object();
    futureRoot.insert(
        QStringLiteral("version"),
        VinylPersonalStatePersistence::SchemaVersion + 1);
    const auto future = VinylPersonalStatePersistence::restore(
        profile, QJsonDocument(futureRoot).toJson(QJsonDocument::Compact));
    require(!future.ok()
                && future.error.code
                    == PersonalStatePersistenceErrorCode::UnsupportedVersion,
            "future schema versions must fail closed");

    qInfo().noquote()
        << "VINYL_BA5_PERSONAL_STATE_PERSISTENCE_OK checks=" << g_checks
        << "qt=" << qVersion();
    return 0;
}
