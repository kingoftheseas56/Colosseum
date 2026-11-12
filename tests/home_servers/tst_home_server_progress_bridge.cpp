#include "HomeServerProgressOwner.h"
#include "ProgressStore.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ProgressBridgeTest : public QObject {
  Q_OBJECT
  static QVariantMap video(QString id, qint64 at = 12345,
                           double progress = 0.25) {
    return {
        {"kind", "video"},
        {"id", id},
        {"title", id},
        {"updatedAt", at},
        {"progress", progress},
        {"resume", QVariantMap{{"positionMs", 2500}, {"durationMs", 10000}}}};
  }
private slots:
  void exactAdapterPreservesTimestampsAndEpisodes() {
    QTemporaryDir dir;
    ProgressStore store(dir.filePath("progress.ini"));
    auto owner = homeServerProgressBridge(&store);
    const QString episode = "tmdb:tv:101:1:2";
    auto entry = video(episode);
    entry.insert("watched", true);
    QVERIFY(owner->apply(entry));
    QCOMPARE(owner->get(episode).value("updatedAt").toLongLong(),
             qint64(12345));
    QVERIFY(owner->get(episode).value("watched").toBool());
    QCOMPARE(owner->watch("tmdb:tv:101").mark, 0);
    QCOMPARE(owner->entries().size(), 1);
    QCOMPARE(owner->entries().first().toMap().value("id").toString(), episode);
    const QString sibling = "tmdb:tv:101:1:3";
    QVERIFY(owner->apply(video(sibling)));
    auto unknown = video("tmdb:movie:202", 0);
    QVERIFY(owner->apply(unknown));
    QCOMPARE(owner->get("tmdb:movie:202").value("updatedAt").toLongLong(),
             qint64(0));
    QVERIFY(owner->remove(episode));
    QVERIFY(owner->get(episode).isEmpty());
    QVERIFY(!owner->get(sibling).isEmpty());
    QVERIFY(!owner->get("tmdb:movie:202").isEmpty());
    store.flush();
    ProgressStore restored(dir.filePath("progress.ini"));
    auto reopened = homeServerProgressBridge(&restored);
    QCOMPARE(reopened->get("tmdb:movie:202").value("updatedAt").toLongLong(),
             qint64(0));
  }
  void watchAdapterPreservesActionAndProvenance() {
    QTemporaryDir dir;
    ProgressStore store(dir.filePath("progress.ini"));
    auto owner = homeServerProgressBridge(&store);
    QVERIFY(owner->applyWatched("tmdb:movie:101", true, 12345));
    auto watched = owner->watch("tmdb:movie:101");
    QCOMPARE(watched.mark, 1);
    QCOMPARE(watched.updatedAt, qint64(12345));
    QVERIFY(!watched.manual);
    QVERIFY(owner->applyWatched("tmdb:movie:102", false, 0));
    watched = owner->watch("tmdb:movie:102");
    QCOMPARE(watched.mark, -1);
    QCOMPARE(watched.updatedAt, qint64(0));
    QVERIFY(!watched.manual);
    store.setWatchedMark("tmdb:movie:103", true);
    watched = owner->watch("tmdb:movie:103");
    QCOMPARE(watched.mark, 1);
    QVERIFY(watched.updatedAt > 0);
    QVERIFY(watched.manual);
    QCOMPARE(owner->watch("tmdb:movie:104").mark, 0);
  }
  void destroyedOwnerFailsClosed() {
    QTemporaryDir dir;
    auto *store = new ProgressStore(dir.filePath("progress.ini"));
    auto owner = homeServerProgressBridge(store);
    QVERIFY(owner->apply(video("tt101")));
    delete store;
    QVERIFY(owner->entries().isEmpty());
    QVERIFY(owner->get("tt101").isEmpty());
    QVERIFY(!owner->apply(video("tt102")));
    QVERIFY(!owner->remove("tt101"));
    QVERIFY(!owner->applyWatched("tt101", true, 12345));
    auto watch = owner->watch("tt101");
    QCOMPARE(watch.mark, 0);
    QCOMPARE(watch.updatedAt, qint64(0));
    QVERIFY(!watch.manual);
    auto absent = homeServerProgressBridge(nullptr);
    QVERIFY(!absent->apply(video("tt101")));
  }
  void hiddenServerRowsRetainCanonicalProgress() {
    QTemporaryDir dir;
    ProgressStore store(dir.filePath("progress.ini"));
    auto owner = homeServerProgressBridge(&store);
    const QString id = "tmdb:movie:701";
    auto entry = video(id, 45678);
    entry.insert("_homeServerHidden", true);
    QVERIFY(owner->apply(entry));
    QCOMPARE(owner->get(id).value("updatedAt").toLongLong(), qint64(45678));
    QCOMPARE(owner->get(id).value("resume").toMap(),
             entry.value("resume").toMap());
    QCOMPARE(owner->entries().size(), 1);
    QVERIFY(store.recent("video").isEmpty());
    QVERIFY(store.recent().isEmpty());
    entry.insert("_homeServerHidden", false);
    QVERIFY(owner->apply(entry));
    QCOMPARE(store.recent("video").size(), 1);
    QCOMPARE(owner->get(id).value("updatedAt").toLongLong(), qint64(45678));
    entry.insert("_homeServerHidden", QStringLiteral("true"));
    QVERIFY(owner->apply(entry));
    QCOMPARE(store.recent("video").size(),
             1); // Only a boolean true hides a row.
  }
  void distinctSeriesGroupingAndMarks() {
    QTemporaryDir dir;
    ProgressStore store(dir.filePath("progress.ini"));
    auto owner = homeServerProgressBridge(&store);
    for (const auto &id :
         {"tmdb:tv:101:1:1", "tmdb:tv:101:1:2", "tmdb:tv:102:1:1",
          "tvdb:tv:201:1:1", "tvdb:tv:202:1:1", "tmdb:movie:301",
          "tmdb:movie:302"})
      QVERIFY(owner->apply(video(id)));
    QCOMPARE(store.recent("video").size(), 6);
    QCOMPARE(owner->entries().size(), 7);
    QVERIFY(owner->applyWatched("tmdb:tv:101", true, 100));
    QCOMPARE(owner->watch("tmdb:tv:101:1:2").mark, 1);
    QCOMPARE(owner->watch("tmdb:tv:102").mark, 0);
    QVERIFY(owner->applyWatched("tvdb:tv:201", true, 200));
    QCOMPARE(owner->watch("tvdb:tv:202").mark, 0);
    store.forget("video", "tmdb:tv:101:1:2");
    QVERIFY(owner->get("tmdb:tv:101:1:1").isEmpty());
    QVERIFY(owner->get("tmdb:tv:101:1:2").isEmpty());
    QVERIFY(!owner->get("tmdb:tv:102:1:1").isEmpty());
    QVERIFY(!owner->get("tvdb:tv:201:1:1").isEmpty());
  }
  void movieCompletionAndLegacyIdentityBehavior() {
    QTemporaryDir dir;
    ProgressStore store(dir.filePath("progress.ini"));
    QSignalSpy completion(&store, &ProgressStore::completionCrossed);
    for (const auto &id : {"tmdb:movie:301", "tt301", "vault:hash301"}) {
      store.record(video(id, 100, 0.25));
      QVERIFY(!store.get("video", id).isEmpty());
      store.record(video(id, 200, 0.95));
      QVERIFY2(store.get("video", id).isEmpty(), id);
    }
    QCOMPARE(completion.size(), 3);
    for (const auto &row : completion)
      QCOMPARE(row.first().toString(), QString("movie"));
    for (const auto &id :
         {"tmdb:tv:101:1:1", "tvdb:tv:201:1:1", "tt101:1:1", "kitsu:101:1:1"}) {
      store.record(video(id, 100, 0.25));
      store.record(video(id, 200, 0.95));
      QVERIFY(store.get("video", id).value("watched").toBool());
    }
    QCOMPARE(completion.size(), 7);
    for (int i = 3; i < completion.size(); ++i)
      QCOMPARE(completion.at(i).first().toString(), QString("episode"));
    store.record(video("kitsu:501:1:1"));
    store.record(video("kitsu:501:1:2"));
    store.record(video("kitsu:502:1:1"));
    store.forget("video", "kitsu:501:1:2");
    QVERIFY(store.get("video", "kitsu:501:1:1").isEmpty());
    QVERIFY(!store.get("video", "kitsu:502:1:1").isEmpty());
  }
};
QTEST_GUILESS_MAIN(ProgressBridgeTest)
#include "tst_home_server_progress_bridge.moc"
