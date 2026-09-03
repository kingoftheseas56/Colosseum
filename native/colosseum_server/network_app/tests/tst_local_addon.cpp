#include "../NetworkAppServices.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace colosseum::server::app;

class FakeFileDiscovery final : public LocalFileDiscovery {
public:
    QStringList paths;
    QStringList discover() override { return paths; }
};

class FakeLocalIndexer final : public LocalFileIndexer {
public:
    LocalAddonEntry entry;
    std::optional<LocalAddonEntry> indexFile(const QString &path) override
    {
        LocalAddonEntry copy = entry;
        copy.primaryKey = path;
        return copy;
    }
};

static LocalAddonEntry fixtureEntry()
{
    LocalAddonEntry entry;
    entry.itemId = "local:tt1234567";
    entry.name = "Fixture Movie";
    LocalAddonFile file;
    file.path = "C:/Media/Fixture.Movie.2026.mkv";
    file.name = "Fixture.Movie.2026.mkv";
    file.length = 1000;
    file.parsedName = "Fixture Movie";
    file.type = "movie";
    file.imdbId = "tt1234567";
    entry.files = {file};
    return entry;
}

static void testManifestAndIndex(TestState &t)
{
    FakeFileDiscovery discovery;
    FakeLocalIndexer indexer;
    indexer.entry = fixtureEntry();
    QTemporaryDir temp;
    const QString media = temp.filePath("Fixture.Movie.2026.mkv");
    QFile file(media);
    t.require(file.open(QIODevice::WriteOnly), "fixture media opens");
    file.write("fixture");
    file.close();
    discovery.paths = {media, media};

    LocalAddonService service(false, discovery, indexer);
    const QByteArray manifest = service.manifestBytes();
    t.equal(manifest.size(), qsizetype(367), "disabled manifest matches oracle byte length");
    t.equal(QCryptographicHash::hash(manifest, QCryptographicHash::Sha256).toHex(),
            QByteArray("4b7e0cc4352e7f861c4a603de1b800b33ff9fc6041a7281b58f82e2b3733fa9f"),
            "disabled manifest matches pinned oracle hash");

    const QString db = temp.filePath("localFiles");
    t.require(service.startIndexing(db), "local addon indexing starts");
    t.equal(service.entryCount(), qsizetype(1), "duplicate discovery path is indexed once");

    FakeFileDiscovery emptyDiscovery;
    LocalAddonService reloaded(false, emptyDiscovery, indexer);
    t.require(reloaded.startIndexing(db), "persisted local addon storage reloads");
    t.equal(reloaded.entryCount(), qsizetype(1), "persisted local addon entry survives reload");
}

static void testRoutes(TestState &t)
{
    FakeFileDiscovery discovery;
    FakeLocalIndexer indexer;
    indexer.entry = fixtureEntry();
    QTemporaryDir temp;
    const QString media = temp.filePath("Fixture.Movie.2026.mkv");
    QFile mediaFile(media);
    t.require(mediaFile.open(QIODevice::WriteOnly), "route fixture media opens");
    mediaFile.close();
    discovery.paths = {media};
    LocalAddonService service(false, discovery, indexer);
    t.require(service.startIndexing(temp.filePath("localFiles")), "route indexing starts");

    AppRequest manifestReq;
    manifestReq.path = "/local-addon/manifest.json";
    const AppResponse manifest = service.handle(manifestReq);
    t.equal(manifest.status, 200, "local addon manifest route succeeds");
    t.equal(headerValue(manifest.headers, "content-type"),
            QByteArray("application/json; charset=utf-8"),
            "manifest content type matches addon SDK");

    AppRequest catalogReq;
    catalogReq.path = "/local-addon/catalog/other/local.json";
    const QJsonObject catalog = QJsonDocument::fromJson(service.handle(catalogReq).body).object();
    t.equal(catalog.value("metas").toArray().size(), qsizetype(1),
            "catalog handler remains callable even when hidden from manifest");

    AppRequest metaReq;
    metaReq.path = "/local-addon/meta/other/local:tt1234567.json";
    const QJsonObject meta = QJsonDocument::fromJson(service.handle(metaReq).body).object();
    t.equal(meta.value("meta").toObject().value("videos").toArray().size(), qsizetype(1),
            "meta exposes indexed video");

    AppRequest streamReq;
    streamReq.path = "/local-addon/stream/movie/tt1234567.json";
    const QJsonArray streams = QJsonDocument::fromJson(service.handle(streamReq).body)
                                   .object().value("streams").toArray();
    t.equal(streams.size(), qsizetype(1), "stream route exposes matching local movie");
    t.require(streams.first().toObject().value("url").toString().startsWith("file://"),
              "local stream uses file URL");
}

static void testTorrentFileIndexQuirk(TestState &t)
{
    FakeFileDiscovery discovery;
    FakeLocalIndexer indexer;
    QTemporaryDir temp;
    const QString probe = temp.filePath("fixture.torrent");
    QFile file(probe);
    t.require(file.open(QIODevice::WriteOnly), "torrent fixture opens");
    file.close();
    discovery.paths = {probe};

    LocalAddonEntry entry;
    entry.itemId = "bt:deadbeef";
    entry.infoHash = "deadbeef";
    entry.name = "Torrent Fixture";
    LocalAddonFile first;
    first.path = "Movie.A.mkv";
    first.name = "Movie.A.mkv";
    first.type = "movie";
    first.imdbId = "tt7654321";
    first.index = 4;
    LocalAddonFile second = first;
    second.path = "Movie.B.mkv";
    second.name = "Movie.B.mkv";
    second.index = 9;
    entry.files = {first, second};
    indexer.entry = entry;

    LocalAddonService service(false, discovery, indexer);
    t.require(service.startIndexing(temp.filePath("localFiles")), "torrent entry indexes");
    AppRequest request;
    request.path = "/local-addon/stream/movie/tt7654321.json";
    const QJsonArray streams = QJsonDocument::fromJson(service.handle(request).body)
                                   .object().value("streams").toArray();
    t.equal(streams.size(), qsizetype(2), "torrent stream exposes both filtered files");
    t.equal(streams[0].toObject().value("fileIdx").toInt(), 0,
            "module 1078 uses filtered-array index for first torrent stream");
    t.equal(streams[1].toObject().value("fileIdx").toInt(), 1,
            "module 1078 uses filtered-array index for second torrent stream");
}

static void testMetaProjectionQuirks(TestState &t)
{
    FakeFileDiscovery discovery;
    FakeLocalIndexer indexer;
    QTemporaryDir temp;
    const QString probe = temp.filePath("fixture.mkv");
    QFile file(probe);
    t.require(file.open(QIODevice::WriteOnly), "meta fixture opens");
    file.close();
    discovery.paths = {probe};

    LocalAddonEntry entry;
    entry.itemId = "local:tt2222222";
    entry.name = "Fallback Name";
    LocalAddonFile small;
    small.path = "Small.mkv";
    small.name = "Small.mkv";
    small.length = 100;
    small.parsedName = "Small Name";
    small.type = "series";
    small.imdbId = "tt2222222";
    small.season = 1;
    small.episode = 1;
    LocalAddonFile biggest = small;
    biggest.path = "Biggest.mkv";
    biggest.name = "Biggest.mkv";
    biggest.length = 1000;
    biggest.parsedName = "Biggest Name";
    biggest.season = 2;
    entry.files = {small, biggest};
    indexer.entry = entry;

    LocalAddonService service(false, discovery, indexer);
    t.require(service.startIndexing(temp.filePath("localFiles")), "meta entry indexes");
    AppRequest request;
    request.path = "/local-addon/meta/other/local:tt2222222.json";
    const QJsonObject meta = QJsonDocument::fromJson(service.handle(request).body)
                                 .object().value("meta").toObject();
    t.equal(meta.value("name").toString(), QString("Biggest Name"),
            "module 502 generic meta uses largest parsed file name");
    const QJsonArray videos = meta.value("videos").toArray();
    t.equal(videos.size(), qsizetype(2), "meta exposes both videos");
    t.require(!videos[0].toObject().value("publishedAt").toString().isEmpty(),
              "module 1056 defaults publishedAt when dateModified is absent");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TestState t;
    testManifestAndIndex(t);
    testRoutes(t);
    testTorrentFileIndexQuirk(t);
    testMetaProjectionQuirks(t);
    return finishTests(t, "local-addon");
}
