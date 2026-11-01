#include "VinylPersonalStatePersistence.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <limits>

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


    const QString repairCase = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (repairCase.isEmpty() || repairCase == "duplicate-library") {
        auto root = QJsonDocument::fromJson(first).object(); auto rows = root.value("library").toArray();
        rows.append(rows.at(0)); root.insert("library", rows);
        require(!VinylPersonalStatePersistence::restore(profile, QJsonDocument(root).toJson()).ok(),
            "M0R duplicate persisted membership must fail instead of discard");
    }
    if (repairCase.isEmpty() || repairCase == "coordinate-range") {
        auto root = QJsonDocument::fromJson(first).object(); auto lists = root.value("playlists").toArray();
        auto list = lists.at(1).toObject(); auto items = list.value("items").toArray(); auto item = items.at(0).toObject();
        QJsonObject context{{"releaseGroupMbid", "33333333-3333-4333-8333-333333333333"},
            {"releaseMbid", "44444444-4444-4444-8444-444444444444"}, {"releaseTrackMbid", QJsonValue::Null},
            {"recordingMbid", recording.toString()}, {"mediumPosition", 4294967297.0}, {"trackPosition", 1}};
        item.insert("releaseContext", context); items[0] = item; list.insert("items", items); lists[1] = list; root.insert("playlists", lists);
        require(!VinylPersonalStatePersistence::restore(profile, QJsonDocument(root).toJson()).ok(),
            "M0R release position must not narrow to positive int");
    }
    if (repairCase.isEmpty() || repairCase == "display-budget") {
        VinylLibrary large(profile); auto row = LibraryEntry::create(CanonicalEntityRef::from(recording), 1000,
            PersonalDisplaySnapshot{QString(4097, QLatin1Char('x')), "artist"});
        if (row.ok()) large.save(*row.value);
        require(!row.ok() || !VinylPersonalStatePersistence::capture(profile, large, {}).ok(),
            "M0R oversized library display must fail");
        auto root = QJsonDocument::fromJson(first).object(); auto rows = root.value("library").toArray();
        auto entry = rows.at(0).toObject(); entry.insert("displaySnapshot", QJsonObject{{"title", QString(4097, QLatin1Char('x'))},{"artistCredit", "artist"}});
        rows[0] = entry; root.insert("library", rows);
        require(!VinylPersonalStatePersistence::restore(profile, QJsonDocument(root).toJson()).ok(), "M0R oversized persisted display must fail");
    }
    if (repairCase.isEmpty() || repairCase == "container-budget") {
        require(!VinylPersonalStatePersistence::restore(profile, first + QByteArray(8*1024*1024, ' ')).ok(),
            "M0R oversized personal container must fail");
    }
    if (repairCase.isEmpty() || repairCase == "capture-item-budget") {
        auto oversized = alpha;
        for (int i = 0; i <= 10000; ++i) {
            auto id = PlaylistItemId::parse(QStringLiteral("budget-") + QString::number(i));
            auto item = PlaylistItem::create(*id.value, recording, 2000);
            require(id.ok() && item.ok() && oversized.append(*item.value).ok,
                    "large playlist fixture should construct");
        }
        require(!VinylPersonalStatePersistence::capture(profile, library, {oversized}).ok(),
                "M0R oversized nested playlist capture must reject before serialization");
    }
    if (repairCase.isEmpty() || repairCase == "integer-boundary") {
        auto root = QJsonDocument::fromJson(first).object(); auto rows = root.value("library").toArray();
        auto row = rows.at(0).toObject(); row.insert("savedAtMs", QJsonValue(std::numeric_limits<qint64>::max())); rows[0] = row; root.insert("library", rows);
        auto maxValue = VinylPersonalStatePersistence::restore(profile, QJsonDocument(root).toJson());
        require(maxValue.ok() && maxValue.value->library.items().at(0).savedAtMs() == std::numeric_limits<qint64>::max(),
            "M0R exact personal timestamp must survive qint64 JSON boundary");
        root.insert("version", 4294967299.0);
        require(!VinylPersonalStatePersistence::restore(profile, QJsonDocument(root).toJson()).ok(), "M0R personal version must not wrap");
    }

    qInfo().noquote()
        << "VINYL_BA5_PERSONAL_STATE_PERSISTENCE_OK checks=" << g_checks
        << "qt=" << qVersion();
    return 0;
}
