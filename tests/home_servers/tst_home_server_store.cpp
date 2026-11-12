#include "homeserver/HomeServerStore.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
class FakeSecrets : public AccountSecretBackend {
public:
  bool online = true, failWrite = false, failRead = false, failRemove = false;
  QHash<QString, QByteArray> blobs;
  bool isAvailable() const override { return online; }
  bool write(const QString &k, const QByteArray &v) override {
    if (failWrite)
      return false;
    blobs[k] = v;
    return true;
  }
  std::optional<QByteArray> read(const QString &k) const override {
    if (failRead || !blobs.contains(k))
      return {};
    return blobs[k];
  }
  bool remove(const QString &k) override {
    if (failRemove)
      return false;
    blobs.remove(k);
    return true;
  }
  QStringList targets(const QString &p) const override {
    QStringList out;
    for (auto i = blobs.cbegin(); i != blobs.cend(); ++i)
      if (i.key().startsWith(p))
        out << i.key();
    return out;
  }
};
class StoreTest : public QObject {
  Q_OBJECT
  static QJsonObject metadata() {
    return {{"provider", "jellyfin"},
            {"name", "Home"},
            {"origin", "https://example.test/proxy/"},
            {"userId", "user"},
            {"password", "secret-password"},
            {"token", "secret-token"}};
  }
private slots:
  void secureRoundTrip() {
    QTemporaryDir dir;
    auto secrets = std::make_shared<FakeSecrets>();
    HomeServerStore s(secrets);
    QVERIFY(!s.available());
    QVERIFY(s.activate("a", dir.path()));
    const auto id = s.add(metadata(), "native-token");
    QVERIFY(!id.isEmpty());
    QCOMPARE(s.token(id), "native-token");
    QFile disk(dir.filePath("home-servers/connections-v1.json"));
    QVERIFY(disk.open(QIODevice::ReadOnly));
    auto data = disk.readAll();
    disk.close();
    QVERIFY(!data.contains("secret-password"));
    QVERIFY(!data.contains("secret-token"));
    QVERIFY(!data.contains("native-token"));
    QJsonArray rows{QJsonObject{
        {"id", "movie"},
        {"title", "Title"},
        {"token", "injected"},
        {"posterPath", "https://evil.test/?token=secret"},
        {"identity", QJsonObject{{"imdbId", "tt123"},
                                 {"tmdbId", 123},
                                 {"tvdbId", 456},
                                 {"token", "injected"}}},
        {"progress", QJsonObject{{"positionMs", 1200}, {"played", false}}}}};
    QVERIFY(s.replaceItems(id, rows));
    auto item = s.items(id).at(0).toObject();
    QCOMPARE(item.value("identity").toObject().value("imdbId").toString(),
             "tt123");
    QCOMPARE(item.value("identity").toObject().value("tmdbId").toInt(), 123);
    QCOMPARE(item.value("identity").toObject().value("tvdbId").toInt(), 456);
    QCOMPARE(item.value("progress").toObject().value("positionMs").toInt(),
             1200);
    QVERIFY(!QJsonDocument(s.items(id)).toJson().contains("injected"));
    QVERIFY(!item.contains("posterPath"));
    HomeServerStore reload(secrets);
    QVERIFY(reload.activate("a", dir.path()));
    QCOMPARE(reload.items(id), s.items(id));
    QVERIFY(reload.setEnabled(id, false));
    QCOMPARE(reload.token(id), "native-token");
    QVERIFY(reload.remove(id));
    QVERIFY(secrets->blobs.isEmpty());
    QVERIFY(!QFile::exists(dir.filePath("home-servers/index-" + id + ".json")));
  }
  void failClosed() {
    QTemporaryDir dir;
    auto secrets = std::make_shared<FakeSecrets>();
    HomeServerStore s(secrets);
    secrets->online = false;
    QVERIFY(!s.activate("a", dir.path()));
    QVERIFY(!QFile::exists(dir.filePath("home-servers")));
    secrets->online = true;
    QVERIFY(s.activate("a", dir.path()));
    secrets->failWrite = true;
    QVERIFY(s.add(metadata(), "token").isEmpty());
    QVERIFY(s.connections().isEmpty());
    secrets->failWrite = false;
    secrets->failRead = true;
    QVERIFY(s.add(metadata(), "token").isEmpty());
    QVERIFY(s.connections().isEmpty());
    QVERIFY(secrets->blobs.isEmpty());
    secrets->failRead = false;
    auto id = s.add(metadata(), "token");
    QVERIFY(!id.isEmpty());
    QVERIFY(!s.setEnabled("../outside", true));
    QVERIFY(!s.remove("../outside"));
    QVERIFY(!s.replaceItems("../outside", {}));
    QVERIFY(s.items("../outside").isEmpty());
    HomeServerStore other(secrets);
    QVERIFY(!other.activate("b", dir.path()));
    QVERIFY(other.token(id).isEmpty());
    QVERIFY(other.connections().isEmpty());
  }
  void atomicFailures() {
    QTemporaryDir dir;
    auto secrets = std::make_shared<FakeSecrets>();
    HomeServerStore s(secrets);
    QVERIFY(s.activate("a", dir.path()));
    auto id = s.add(metadata(), "token");
    QVERIFY(!id.isEmpty());
    auto path = dir.filePath("home-servers/connections-v1.json");
    QVERIFY(QFile::rename(path, path + ".saved"));
    QVERIFY(QDir().mkdir(path));
    QVERIFY(!s.setEnabled(id, false));
    QVERIFY(s.connection(id).value("enabled").toBool());
    QVERIFY(!s.remove(id));
    QCOMPARE(s.token(id), "token");
    auto count = secrets->blobs.size();
    QVERIFY(s.add(metadata(), "new-token").isEmpty());
    QCOMPARE(secrets->blobs.size(), count);
    auto ip = dir.filePath("home-servers/index-" + id + ".json");
    QVERIFY(s.replaceItems(id, QJsonArray{QJsonObject{{"id", "one"}}}));
    QVERIFY(QFile::rename(ip, ip + ".saved"));
    QVERIFY(QDir().mkdir(ip));
    QVERIFY(!s.replaceItems(id, QJsonArray{QJsonObject{{"id", "two"}}}));
    QFile saved(ip + ".saved");
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QVERIFY(saved.readAll().contains("one"));
  }
  void profileIsolationAndOwnRemoval() {
    QTemporaryDir first, second;
    auto secrets = std::make_shared<FakeSecrets>();
    HomeServerStore a(secrets), b(secrets);
    QVERIFY(a.activate("same", first.path()));
    auto id = a.add(metadata(), "first-token");
    auto other = a.add(metadata(), "other-token");
    QVERIFY(!id.isEmpty());
    QVERIFY(!other.isEmpty());
    QVERIFY(a.replaceItems(id, QJsonArray{QJsonObject{{"id", "one"}}}));
    QVERIFY(a.replaceItems(other, QJsonArray{QJsonObject{{"id", "two"}}}));
    QDir().mkpath(second.filePath("home-servers"));
    QVERIFY(QFile::copy(first.filePath("home-servers/connections-v1.json"),
                        second.filePath("home-servers/connections-v1.json")));
    QVERIFY(b.activate("same", second.path()));
    QVERIFY(b.token(id).isEmpty());
    QCOMPARE(a.token(id), "first-token");
    QVERIFY(a.remove(id));
    QCOMPARE(a.token(other), "other-token");
    QCOMPARE(a.items(other).size(), 1);
    QCOMPARE(secrets->blobs.size(), 1);
    a.deactivate();
    QVERIFY(!a.available());
    QVERIFY(a.connections().isEmpty());
    QVERIFY(a.token(other).isEmpty());
  }
  void indexRejectsInvalidReplacement() {
    QTemporaryDir dir;
    auto secrets = std::make_shared<FakeSecrets>();
    HomeServerStore s(secrets);
    QVERIFY(s.activate("a", dir.path()));
    auto id = s.add(metadata(), "token");
    QVERIFY(!id.isEmpty());
    auto original = QJsonArray{QJsonObject{
        {"id", "one"},
        {"versions",
         QJsonArray{QJsonObject{
             {"id", "v1"},
             {"subtitles",
              QJsonArray{QJsonObject{{"id", "s1"},
                                     {"url", "https://evil.test/?token=hidden"},
                                     {"token", "hidden"}}}}}}}}};
    QVERIFY(s.replaceItems(id, original));
    auto saved = s.items(id);
    QVERIFY(!QJsonDocument(saved).toJson().contains("hidden"));
    QVERIFY(!s.replaceItems(id, QJsonArray{QJsonObject{{"id", "duplicate"}},
                                           QJsonObject{{"id", "duplicate"}}}));
    QCOMPARE(s.items(id), saved);
  }
  void rejectsInvalidMetadata() {
    QTemporaryDir dir;
    auto secrets = std::make_shared<FakeSecrets>();
    HomeServerStore s(secrets);
    QVERIFY(s.activate("a", dir.path()));
    for (const auto &url :
         {"file:///tmp/private", "https://user:pass@example.test",
          "https://example.test/?token=secret",
          "https://example.test/#fragment"}) {
      auto row = metadata();
      row.insert("origin", url);
      QVERIFY(s.add(row, "native-token").isEmpty());
    }
    auto row = metadata();
    row.insert("provider", "unknown");
    QVERIFY(s.add(row, "native-token").isEmpty());
    row = metadata();
    row.insert("userId", "");
    QVERIFY(s.add(row, "native-token").isEmpty());
    QVERIFY(s.connections().isEmpty());
    QVERIFY(secrets->blobs.isEmpty());
    row = metadata();
    row.insert("id", "../chosen-id");
    auto id = s.add(row, "native-token");
    QVERIFY(!id.isEmpty());
    QVERIFY(id != "../chosen-id");
    QCOMPARE(s.connection(id).value("origin").toString(),
             "https://example.test/proxy/");
  }
  void corruptMetadata() {
    QTemporaryDir dir;
    QDir().mkpath(dir.filePath("home-servers"));
    QFile f(dir.filePath("home-servers/connections-v1.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{broken");
    f.close();
    HomeServerStore s(std::make_shared<FakeSecrets>());
    QVERIFY(!s.activate("a", dir.path()));
    QVERIFY(!s.available());
    QVERIFY(s.connections().isEmpty());
  }
};
QTEST_GUILESS_MAIN(StoreTest)
#include "tst_home_server_store.moc"
