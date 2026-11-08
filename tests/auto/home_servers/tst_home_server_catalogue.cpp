#include "home_servers/HomeServerCatalogueStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <QUuid>
#include <utility>

using namespace Colosseum::HomeServers;

namespace {
ServerCatalogue example(const QString &profile, const QString &id)
{
    ServerCatalogue snapshot;
    snapshot.profileId = profile;
    snapshot.connectionId = id;
    snapshot.userId = QStringLiteral("user-123");
    snapshot.serverId = QStringLiteral("server-123");
    snapshot.syncedAtUtcMs = 1800000000000LL;
    snapshot.libraries = {
        {QStringLiteral("movies"), QStringLiteral("Movies"), QStringLiteral("movies")},
        {QStringLiteral("shows"), QStringLiteral("TV"), QStringLiteral("tvshows")}
    };
    ServerItem movie;
    movie.serverId = snapshot.serverId;
    movie.userId = snapshot.userId;
    movie.id = QStringLiteral("film-a");
    movie.name = QStringLiteral("Film");
    movie.type = QStringLiteral("Movie");
    movie.imdbId = QStringLiteral("tt1234567");
    movie.mediaSources = QJsonArray{QJsonObject{
        {QStringLiteral("Id"), QStringLiteral("source-hd")},
        {QStringLiteral("SupportsDirectPlay"), true}
    }};
    movie.userData = QJsonObject{
        {QStringLiteral("PlaybackPositionTicks"), 120000000}
    };
    ServerItem episode;
    episode.serverId = snapshot.serverId;
    episode.userId = snapshot.userId;
    episode.id = QStringLiteral("episode-a");
    episode.type = QStringLiteral("Episode");
    episode.seriesId = QStringLiteral("series-a");
    episode.seasonNumber = 1;
    episode.episodeNumber = 2;
    snapshot.items = {
        {QStringLiteral("movies"), movie},
        {QStringLiteral("shows"), episode}
    };
    return snapshot;
}
}

class tst_home_server_catalogue final : public QObject {
    Q_OBJECT
private slots:
    void roundTripRetainsVersionsAndEpisodeIdentity();
    void replacementOnlyHappensForValidCompleteSnapshot();
    void malformedIdentityNeverBecomesVisible();
    void removalIsScopedToOneUuid();
};

void tst_home_server_catalogue::roundTripRetainsVersionsAndEpisodeIdentity()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    HomeServerCatalogueStore store(QStringLiteral("local"), dir.path());
    QVERIFY(store.available());
    const auto saved = example(QStringLiteral("local"), id);
    QCOMPARE(saved.identifiedCount(), 1);
    QVERIFY(store.replace(saved));
    const auto loaded = store.load(id, saved.userId, saved.serverId);
    QVERIFY(loaded);
    QCOMPARE(loaded->items.size(), 2);
    QCOMPARE(loaded->identifiedCount(), 1);
    QCOMPARE(loaded->syncedAtUtcMs, saved.syncedAtUtcMs);
    QCOMPARE(loaded->items.first().item.mediaSources.size(), 1);
    QCOMPARE(loaded->items.first().item.userData.value(
        QStringLiteral("PlaybackPositionTicks")).toInt(), 120000000);
    QCOMPARE(loaded->items.last().item.seriesId, QStringLiteral("series-a"));
    QCOMPARE(loaded->items.last().item.seasonNumber, 1);
    QCOMPARE(loaded->items.last().item.episodeNumber, 2);
    QVERIFY(!store.load(id, QStringLiteral("another-user"), saved.serverId));
    QVERIFY(!store.load(id, saved.userId, QStringLiteral("another-server")));
    HomeServerCatalogueStore stranger(QStringLiteral("different"), dir.path());
    QVERIFY(!stranger.load(id, saved.userId, saved.serverId));
    HomeServerCatalogueStore invalid(QStringLiteral("../unsafe"), dir.path());
    QVERIFY(!invalid.available());
}

void tst_home_server_catalogue::replacementOnlyHappensForValidCompleteSnapshot()
{
    QTemporaryDir dir;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    HomeServerCatalogueStore store(QStringLiteral("local"), dir.path());
    const auto original = example(QStringLiteral("local"), id);
    QVERIFY(store.replace(original));
    auto invalid = original;
    invalid.items.append(invalid.items.first()); // Duplicate remote item ID.
    QVERIFY(!store.replace(invalid));
    QCOMPARE(store.load(id, original.userId, original.serverId)->items.size(), 2);
    invalid = original;
    invalid.items[0].libraryId = QStringLiteral("unknown");
    QVERIFY(!store.replace(invalid));
    QCOMPARE(store.load(id, original.userId, original.serverId)->items.size(), 2);
    invalid = original;
    invalid.items[0].item.type = QStringLiteral("Music");
    QVERIFY(!store.replace(invalid));
    invalid = original;
    invalid.profileId = QStringLiteral("different");
    QVERIFY(!store.replace(invalid));
    auto next = original;
    next.syncedAtUtcMs += 10000;
    next.items.removeLast(); // Successful complete refresh removes stale episode.
    QVERIFY(store.replace(next));
    const auto loaded = store.load(id, original.userId, original.serverId);
    QVERIFY(loaded);
    QCOMPARE(loaded->items.size(), 1);
    QCOMPARE(loaded->items[0].item.id, QStringLiteral("film-a"));
}

void tst_home_server_catalogue::malformedIdentityNeverBecomesVisible()
{
    QTemporaryDir dir;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    HomeServerCatalogueStore store(QStringLiteral("local"), dir.path());
    const auto saved = example(QStringLiteral("local"), id);
    QVERIFY(store.replace(saved));
    const QString path = dir.path() + QStringLiteral("/home-servers/index-v1/")
                         + id + QStringLiteral(".json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonObject record = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    record.insert(QStringLiteral("profile"), QStringLiteral("other"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
    file.close();
    QVERIFY(!store.load(id, saved.userId, saved.serverId));
    QVERIFY(store.replace(saved));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{corrupt");
    file.close();
    QVERIFY(!store.load(id, saved.userId, saved.serverId));
}

void tst_home_server_catalogue::removalIsScopedToOneUuid()
{
    QTemporaryDir dir;
    const QString id1 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString id2 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    HomeServerCatalogueStore store(QStringLiteral("local"), dir.path());
    const auto first = example(QStringLiteral("local"), id1);
    const auto second = example(QStringLiteral("local"), id2);
    QVERIFY(store.replace(first));
    QVERIFY(store.replace(second));
    QVERIFY(!store.remove(QStringLiteral("../../other")));
    QVERIFY(store.remove(id1));
    QVERIFY(!store.load(id1, first.userId, first.serverId));
    QVERIFY(store.load(id2, second.userId, second.serverId));
    QVERIFY(store.remove(id1));
}
QTEST_GUILESS_MAIN(tst_home_server_catalogue)
#include "tst_home_server_catalogue.moc"
