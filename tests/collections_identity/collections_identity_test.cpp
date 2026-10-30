#include "../../native/engine/ColosseumTitleIdentityRegistry.h"
#include "../../native/NamedCollectionsStore.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDebug>

namespace {
int checks = 0;
void require(bool condition, const char *message) {
    ++checks;
    if (!condition) qFatal("collections_identity_test: %s", message);
}
QVariantMap candidate(const QString &world, const QString &kind, const QString &source,
                     const QString &sourceId, const QString &route, const QString &edition = {}) {
    return {{"world",world},{"kind",kind},{"source",source},{"sourceId",sourceId},
        {"route",route},{"editionId",edition},{"title","A title <&> \"quoted\""},
        {"cover","https://example.invalid/cover.jpg"},{"creator","Creator"},{"year","2026"},{"payload",QVariantMap{}}};
}
QVariantMap admit(const ColosseumTitleIdentityRegistry &registry, const QVariantMap &raw) {
    const auto result = registry.collectionReference(raw);
    if (!result.value("ok").toBool()) qCritical() << result.value("error");
    require(result.value("ok").toBool(), "expected an admitted exact reference");
    const auto reference = result.value("reference").toMap();
    require(reference.size() == 13, "persisted reference schema must have exactly 13 fields");
    require(ColosseumTitleIdentityRegistry::isCanonicalMediaId(reference.value("mediaId").toString()), "canonical media identity missing");
    const auto key = QJsonDocument::fromJson(reference.value("key").toString().toUtf8()).array();
    require(key.size() == 2 && key[0].toString() == reference.value("mediaId").toString()
            && key[1].toString() == reference.value("editionId").toString(), "key must encode canonical identity and edition");
    require(registry.collectionReference(reference).value("ok").toBool(), "normalized reference must reopen unchanged");
    return reference;
}
void reject(const ColosseumTitleIdentityRegistry &registry, const QVariantMap &raw) {
    const auto result = registry.collectionReference(raw);
    require(!result.value("ok").toBool() && !result.value("error").toString().isEmpty(), "invalid reference must fail with a recoverable reason");
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "temporary test directory unavailable");
    QFile fixture(directory.filePath("registry.json"));
    require(fixture.open(QIODevice::WriteOnly), "registry fixture could not be written");
    fixture.write("{\"version\":1,\"titles\":[]}");
    fixture.close();
    ColosseumTitleIdentityRegistry registry(fixture.fileName());
    require(registry.ready(), "empty valid registry must allow deterministic catalogue identities");
    registry.setAliasUnionsPath(directory.filePath("alias-unions.json"));
    require(registry.knownTheatreAnchor("kitsu:13783").isEmpty(), "unknown imported pivots must not become trusted");
    require(!QFile::exists(directory.filePath("alias-unions.json")), "read-only pivot lookup must not create an alias file");
    require(registry.knownTheatreAnchor("tt1234567") == "tt1234567", "direct IMDb identity needs no learned pivot");

    auto movie = candidate("theatre","movie","theatre","tt2543164","theatre-detail");
    auto film = admit(registry,movie);
    auto ordinary = registry.resolve("theatre","movie",{},QVariantList{QVariantMap{{"namespace","theatre-source-id"},{"value","tt2543164"}}});
    require(film.value("mediaId") == ordinary.value("mediaId"), "collection must reuse existing title identity authority");
    auto vault = candidate("vault","film","imdb","imdb:tt2543164","vault-film-detail");
    vault["payload"] = QVariantMap{{"vaultKey","film:arrival"}};
    require(admit(registry,vault).value("key") == film.value("key"), "Theatre and identified Vault film must dedupe canonically");
    movie["title"] = "Renamed metadata";
    require(admit(registry,movie).value("key") == film.value("key"), "display metadata must never change identity");
    movie["sourceId"] = "tt0080684";
    require(admit(registry,movie).value("key") != film.value("key"), "same title text cannot merge different source IDs");
    auto series = candidate("theatre","series","theatre","kitsu:13783","theatre-detail");
    reject(registry,series);
    series["payload"] = QVariantMap{{"canonicalSourceId","tt1234567"}};
    const auto anime = admit(registry,series);
    require(anime.value("sourceId").toString() == "kitsu:13783", "original provider door must remain attached to canonical identity");
    require(anime.value("mediaId") != film.value("mediaId"), "movie and series identities cannot collide");
    const auto pivoted = registry.resolve("theatre","series",{},QVariantList{QVariantMap{{"namespace","theatre-source-id"},{"value","kitsu:13783"}}});
    require(pivoted.value("mediaId") == anime.value("mediaId"), "resolved provider pivot must share the existing registry union");
    require(registry.collectionReference(anime).value("ok").toBool(), "persisted post-pivot reference must remain reopenable");
    QFile unionFile(directory.filePath("alias-unions.json"));
    require(unionFile.open(QIODevice::ReadOnly), "trusted pivot file must exist");
    const QByteArray unionBefore = unionFile.readAll();
    unionFile.close();
    require(registry.knownTheatreAnchor("kitsu:13783") == "tt1234567", "recipient-owned pivot must be exposed exactly");
    require(registry.knownTheatreAnchor("kitsu:13783") != "tt9999999", "sender's different anchor must fail the imported guard");
    require(registry.knownTheatreAnchor("kitsu:9999999").isEmpty(), "sender's unknown pivot must stay unavailable");
    require(unionFile.open(QIODevice::ReadOnly) && unionFile.readAll() == unionBefore, "import eligibility checks must not modify persistent alias unions");
    unionFile.close();
    ColosseumTitleIdentityRegistry reopenedRegistry(fixture.fileName());
    reopenedRegistry.setAliasUnionsPath(directory.filePath("alias-unions.json"));
    require(reopenedRegistry.knownTheatreAnchor("kitsu:13783") == "tt1234567", "trusted pivots survive restart without learning sender data");

    auto manga = candidate("tankoban","manga","mal","mal:13","manga-detail");
    const auto mono = admit(registry,manga);
    manga["sourceId"] = "mal:13:color"; manga["editionId"] = "color";
    require(admit(registry,manga).value("key") != mono.value("key"), "colored manga edition must stay distinct");
    manga["editionId"] = ""; reject(registry,manga);
    manga["sourceId"] = "One Piece"; reject(registry,manga);
    admit(registry,candidate("tankoban","manga","weebcentral","01J123456789ABCDEFGHIJKLMNOP","manga-weebcentral-detail"));

    auto book = candidate("biblio","book","apple-books","123456789","book-detail","ebook");
    book["payload"] = QVariantMap{{"book",QVariantMap{{"id","123456789"},{"title","A Book"},
        {"pairKey","a book|an author"},{"localPath","C:/must-not-persist.epub"},{"isAudiobook",false}}}};
    const auto ebook = admit(registry,book);
    const auto cleanBook = ebook.value("payload").toMap().value("book").toMap();
    require(!cleanBook.contains("pairKey") && !cleanBook.contains("localPath"), "name-pairing and delivery paths do not become collection identity");
    book["editionId"] = "audiobook";
    auto audioPayload = book.value("payload").toMap();
    auto audioBook = audioPayload.value("book").toMap(); audioBook["isAudiobook"] = true;
    audioPayload["book"] = audioBook; book["payload"] = audioPayload;
    require(admit(registry,book).value("key") != ebook.value("key"), "book editions retain their ebook/audiobook source distinction");
    book["sourceId"] = "title|author"; reject(registry,book);

    admit(registry,candidate("tankoban","comic","locg","locg:12345","comic-locg-detail"));
    admit(registry,candidate("tankoban","comic","gcd","gcd:119237","comic-gcd-detail"));
    auto tag = candidate("tankoban","comic","getcomics","gc:batman","comic-tag-detail");
    tag["payload"] = QVariantMap{{"tag","batman"},{"tagId","55"}};
    admit(registry,tag);
    tag["payload"] = QVariantMap{{"tag","superman"}}; reject(registry,tag);
    auto posts = candidate("tankoban","comic","getcomics","gc:posts:101","comic-posts-detail");
    posts["payload"] = QVariantMap{{"posts",QVariantList{"101","202"}}};
    admit(registry,posts);
    posts["payload"] = QVariantMap{{"posts",QVariantList{"202","101"}}}; reject(registry,posts);
    admit(registry,candidate("tankoban","comic","comic-packs","pack:exact-source-9","comic-pack-detail"));
    require(!registry.resolve("tankoban","comic",{},{}).value("available").toBool(), "collections cannot widen ratings admission");

    auto album = candidate("vinyl","album","vinyl-local","vinyl:local-album:abc","vinyl-album-detail","vinyl:local-album:abc:local");
    album["payload"] = QVariantMap{{"albumId","vinyl:local-album:abc"},{"browseId","vinyl:local-album:abc"}};
    const auto firstEdition = admit(registry,album);
    album["editionId"] = "vinyl:local-album:abc:deluxe";
    require(admit(registry,album).value("key") != firstEdition.value("key"), "album editions cannot merge");
    album["source"] = "youtube-music";
    reject(registry,album);
    auto remoteAlbum = candidate("vinyl","album","youtube-music","ytm:album:MPREb_exact","vinyl-album-detail","MPREb_exact");
    remoteAlbum["payload"] = QVariantMap{{"albumId","ytm:album:MPREb_exact"},{"browseId","MPREb_exact"}};
    require(admit(registry,remoteAlbum).value("mediaId") != firstEdition.value("mediaId"), "local and provider music identities remain separate");
    auto remoteSong = candidate("vinyl","song","youtube-music","music:recording:exact-track","vinyl-song-detail","music:release:exact-album");
    remoteSong["payload"] = QVariantMap{{"albumId","music:release:exact-album"},{"browseId","MPREb_exact"}};
    admit(registry,remoteSong);
    for (const QString &unsupported : {QStringLiteral("ytm:track:abcdefghijk"),
            QStringLiteral("music:recording:exact-track"),QStringLiteral("ytm:playlist:PLexact")}) {
        remoteSong["payload"] = QVariantMap{{"albumId","music:release:exact-album"},{"browseId",unsupported}};
        reject(registry,remoteSong);
    }
    auto song = candidate("vinyl","song","vinyl-local","vault:track-a","vinyl-song-detail","vinyl:local-album:abc:local");
    song["payload"] = QVariantMap{{"albumId","vinyl:local-album:abc"},{"browseId","vinyl:local-album:abc"}};
    const auto songRef = admit(registry,song);
    require(songRef.value("payload").toMap().value("trackId").toString() == "vault:track-a", "song must reopen by exact track ID");
    song["sourceId"] = ""; reject(registry,song);
    admit(registry,candidate("vinyl","artist","youtube-music","ytm:artist:UCabcdefghijklmnopqrstuv","vinyl-artist-detail"));
    reject(registry,candidate("vinyl","artist","youtube-music","Artist Name","vinyl-artist-detail"));
    admit(registry,candidate("vinyl","artist","vinyl-local","vinyl:local-artist:abc","vinyl-artist-detail"));
    const QString unicodeAlbumId = QStringLiteral("vinyl:local-release:vault:fixture:音楽");
    auto unicodeAlbum = candidate("vinyl","album","vinyl-local",unicodeAlbumId,"vinyl-album-detail",unicodeAlbumId + QStringLiteral(":初版"));
    unicodeAlbum["payload"] = QVariantMap{{"albumId",unicodeAlbumId},{"browseId",unicodeAlbumId}};
    admit(registry,unicodeAlbum);
    admit(registry,candidate("vinyl","artist","vinyl-local",QStringLiteral("vinyl:local-artist:vault:fixture:作曲家"),"vinyl-artist-detail"));
    auto unicodeSong = candidate("vinyl","song","vinyl-local","vault:track-a","vinyl-song-detail",unicodeAlbumId + QStringLiteral(":初版"));
    unicodeSong["payload"] = unicodeAlbum.value("payload");
    admit(registry,unicodeSong);
    unicodeAlbum["editionId"] = unicodeAlbumId + QStringLiteral(":invalid key"); reject(registry,unicodeAlbum);
    reject(registry,candidate("tankoban","comic","locg",QStringLiteral("locg:作品"),"comic-locg-detail"));
    require(!registry.resolve("vinyl","album",{},{}).value("available").toBool(), "music collection IDs must not change ratings behavior");

    auto corrupt = film; corrupt["key"] = "different"; reject(registry,corrupt);
    corrupt = film; corrupt["mediaId"] = mono.value("mediaId"); reject(registry,corrupt);
    corrupt = film; corrupt["route"] = "fictional-movie-route"; reject(registry,corrupt);
    corrupt = film; corrupt["source"] = "unrecognized-provider"; reject(registry,corrupt);
    corrupt = film; corrupt["sourceId"] = QString(513, 'a'); reject(registry,corrupt);
    corrupt = film; corrupt["title"] = "Title\nwith a line break"; reject(registry,corrupt);
    corrupt = film; corrupt["creator"] = QString(QChar(0xd800)); reject(registry,corrupt);
    NamedCollectionsStore store(directory.filePath("named-collections-v2.json"));
    require(store.healthy(), "fresh integration store must be healthy");
    const QString collectionId = store.createCollection("Identity round trip", film);
    require(!collectionId.isEmpty(), "admitted native reference must be accepted by actual store");
    require(!store.saveMemberships(corrupt, QStringList{collectionId}).value("ok").toBool(), "store must also reject malformed Unicode");
    NamedCollectionsStore reopened(directory.filePath("named-collections-v2.json"));
    require(reopened.healthy() && reopened.contains(collectionId, film.value("key").toString()), "admitted reference survives real store reload");
    reject(registry,candidate("music","audiobook","unknown","123","fake-route"));
    reject(registry,candidate("vault","film","imdb","Filename without identity","vault-film-detail"));
    ColosseumTitleIdentityRegistry unavailable(directory.filePath("missing.json"));
    reject(unavailable,film);
    qInfo() << "collections_identity_test:" << checks << "checks passed";
    return 0;
}
