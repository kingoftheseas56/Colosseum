#include "../native/nuvio/NuvioManifest.h"
#include "../native/nuvio/NuvioRepositoryStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <utility>

namespace {
const QUrl ManifestUrl(QStringLiteral("https://repo.example/manifest.json"));
const QUrl AlphaUrl(QStringLiteral("https://repo.example/plugins/alpha.js"));
const QUrl BetaUrl(QStringLiteral("https://repo.example/beta.js"));

QJsonObject scraper(const char *id, const char *filename, const char *type)
{
    return {
        {QStringLiteral("id"), QString::fromLatin1(id)},
        {QStringLiteral("name"), QString::fromLatin1(id)},
        {QStringLiteral("filename"), QString::fromLatin1(filename)},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("supportedTypes"), QJsonArray{QString::fromLatin1(type)}}
    };
}

QByteArray manifest(const QString &name, const QJsonArray &scrapers)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("scrapers"), scrapers}
    }).toJson();
}

struct FakeFetch {
    struct Request {
        QUrl url;
        qsizetype limit;
        NuvioRepositoryStore::FetchCompletion complete;
    };
    QList<Request> requests;

    NuvioRepositoryStore::Fetch function()
    {
        return [this](const QUrl &url, qsizetype limit, QObject *,
                      NuvioRepositoryStore::FetchCompletion complete) {
            requests.append({url, limit, std::move(complete)});
        };
    }

    void respond(const QUrl &url, const QByteArray &body, const QString &error = {})
    {
        QVERIFY(!requests.isEmpty());
        const Request request = requests.takeFirst();
        QCOMPARE(request.url, url);
        QVERIFY(request.limit > 0);
        request.complete(body, url, error);
    }
};

QString repoId(const NuvioRepositoryStore &store)
{
    return store.repositories().value(0).toMap().value(QStringLiteral("id")).toString();
}

QByteArray snapshot(const NuvioRepositoryStore::Plugin &plugin)
{
    QFile file(plugin.scriptPath);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QString scriptPath(const QTemporaryDir &directory, const QByteArray &contents)
{
    const auto hash = QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
    return directory.filePath(QStringLiteral("scripts/%1.js").arg(QString::fromLatin1(hash)));
}
} // namespace

class NuvioRepositoryStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void previewDetection();
    void installToggleAndRestart();
    void refreshIsTransactional();
    void refreshedDisabledPluginFetchesOnNextEnable();
    void rejectsUnsafeManifestAndUrls();
    void startupPrunesOnlyWithReadableIndex();
};

void NuvioRepositoryStoreTest::previewDetection()
{
    const QByteArray bytes = manifest(QStringLiteral("Community"),
                                      {scraper("alpha", "plugins/alpha.js", "movie")});
    QJsonObject document = QJsonDocument::fromJson(bytes).object();
    QVERIFY(NuvioManifest::isPreviewCandidate(document));
    document.insert(QStringLiteral("resources"), QJsonArray{QStringLiteral("stream")});
    QVERIFY(!NuvioManifest::isPreviewCandidate(document));
    document.remove(QStringLiteral("scrapers"));
    QVERIFY(!NuvioManifest::isPreviewCandidate(document));
}

void NuvioRepositoryStoreTest::installToggleAndRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeFetch fetch;
    QString id;
    QByteArray hash;
    {
        NuvioRepositoryStore store(directory.path(), fetch.function());
        QSignalSpy changed(&store, &NuvioRepositoryStore::changed);
        store.addRepository(ManifestUrl.toString());
        QVERIFY(store.busy());
        store.addRepository(ManifestUrl.toString());
        QCOMPARE(fetch.requests.size(), 1);
        fetch.respond(ManifestUrl, manifest(QStringLiteral("Community"),
                                           {scraper("alpha", "plugins/alpha.js", "movie")}));
        QCOMPARE(changed.size(), 1);
        QVERIFY(!store.busy());
        QVERIFY(fetch.requests.isEmpty());
        QVERIFY(store.enabledPlugins(QStringLiteral("movie")).isEmpty());
        id = repoId(store);
        QVERIFY(!id.isEmpty());

        store.setPluginEnabled(id, QStringLiteral("alpha"), true);
        QVERIFY(store.enabledPlugins(QStringLiteral("movie")).isEmpty());
        fetch.respond(AlphaUrl, "exports.getStreams = () => [];");
        QCOMPARE(changed.size(), 2);
        const auto movie = store.enabledPlugins(QStringLiteral("movie"));
        QCOMPARE(movie.size(), 1);
        QCOMPARE(snapshot(movie.first()), QByteArray("exports.getStreams = () => [];"));
        hash = movie.first().sha256;
        QCOMPARE(hash.size(), 64);
        QVERIFY(store.enabledPlugins(QStringLiteral("tv")).isEmpty());
    }
    NuvioRepositoryStore restarted(directory.path(), fetch.function());
    QCOMPARE(repoId(restarted), id);
    QCOMPARE(restarted.enabledPlugins(QStringLiteral("movie")).size(), 1);
    restarted.setPluginEnabled(id, QStringLiteral("alpha"), false);
    QVERIFY(restarted.enabledPlugins(QStringLiteral("movie")).isEmpty());
    const QString originalPath = scriptPath(directory, "exports.getStreams = () => [];");
    QVERIFY(QFile::exists(originalPath));
    restarted.setPluginEnabled(id, QStringLiteral("alpha"), true);
    QVERIFY(fetch.requests.isEmpty());
    QCOMPARE(restarted.enabledPlugins(QStringLiteral("movie")).first().sha256, hash);
    restarted.removeRepository(id);
    QVERIFY(restarted.enabledPlugins(QStringLiteral("movie")).isEmpty());
    QVERIFY(restarted.repositories().isEmpty());
    QVERIFY(!QFile::exists(originalPath));
    NuvioRepositoryStore removed(directory.path(), fetch.function());
    QVERIFY(removed.repositories().isEmpty());
}

void NuvioRepositoryStoreTest::refreshIsTransactional()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeFetch fetch;
    NuvioRepositoryStore store(directory.path(), fetch.function());
    const auto entries = QJsonArray{scraper("alpha", "plugins/alpha.js", "movie"),
                                    scraper("beta", "beta.js", "tv")};
    store.addRepository(ManifestUrl.toString());
    fetch.respond(ManifestUrl, manifest(QStringLiteral("Original"), entries));
    const QString id = repoId(store);
    store.setPluginEnabled(id, QStringLiteral("alpha"), true);
    fetch.respond(AlphaUrl, "original alpha");
    store.setPluginEnabled(id, QStringLiteral("beta"), true);
    fetch.respond(BetaUrl, "original beta");

    QSignalSpy changed(&store, &NuvioRepositoryStore::changed);
    auto refreshed = entries;
    refreshed.append(scraper("new", "new.js", "movie"));
    store.refreshRepository(id);
    fetch.respond(ManifestUrl, manifest(QStringLiteral("Refreshed"), refreshed));
    fetch.respond(AlphaUrl, "new alpha");
    const QString stagedPath = scriptPath(directory, "new alpha");
    QVERIFY(QFile::exists(stagedPath));
    fetch.respond(BetaUrl, {}, QStringLiteral("network failure"));
    QVERIFY(!QFile::exists(stagedPath));
    QVERIFY(!store.error().isEmpty());
    QCOMPARE(changed.size(), 0);
    QCOMPARE(snapshot(store.enabledPlugins(QStringLiteral("movie")).first()), QByteArray("original alpha"));
    NuvioRepositoryStore restarted(directory.path(), fetch.function());
    QCOMPARE(restarted.repositories().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Original"));

    store.refreshRepository(id);
    fetch.respond(ManifestUrl, manifest(QStringLiteral("Refreshed"), refreshed));
    fetch.respond(AlphaUrl, "new alpha");
    fetch.respond(BetaUrl, "new beta");
    QVERIFY(fetch.requests.isEmpty());
    QCOMPARE(changed.size(), 1);
    QVERIFY(store.error().isEmpty());
    QCOMPARE(store.enabledPlugins(QStringLiteral("movie")).size(), 1);
    QCOMPARE(snapshot(store.enabledPlugins(QStringLiteral("movie")).first()), QByteArray("new alpha"));
    QCOMPARE(snapshot(store.enabledPlugins(QStringLiteral("tv")).first()), QByteArray("new beta"));
    QVERIFY(!QFile::exists(scriptPath(directory, "original alpha")));
    QVERIFY(!QFile::exists(scriptPath(directory, "original beta")));
}

void NuvioRepositoryStoreTest::refreshedDisabledPluginFetchesOnNextEnable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeFetch fetch;
    NuvioRepositoryStore store(directory.path(), fetch.function());
    const QString name = QStringLiteral("Community");
    auto alpha = scraper("alpha", "plugins/alpha.js", "movie");
    store.addRepository(ManifestUrl.toString());
    fetch.respond(ManifestUrl, manifest(name, {alpha}));
    const QString id = repoId(store);
    store.setPluginEnabled(id, QStringLiteral("alpha"), true);
    fetch.respond(AlphaUrl, "original alpha");
    store.setPluginEnabled(id, QStringLiteral("alpha"), false);

    alpha.insert(QStringLiteral("version"), QStringLiteral("2.0.0"));
    store.refreshRepository(id);
    fetch.respond(ManifestUrl, manifest(name, {alpha}));
    QVERIFY(fetch.requests.isEmpty());
    QVERIFY(store.enabledPlugins(QStringLiteral("movie")).isEmpty());
    store.setPluginEnabled(id, QStringLiteral("alpha"), true);
    fetch.respond(AlphaUrl, "new alpha");
    QCOMPARE(store.enabledPlugins(QStringLiteral("movie")).size(), 1);
    QCOMPARE(snapshot(store.enabledPlugins(QStringLiteral("movie")).first()), QByteArray("new alpha"));
}

void NuvioRepositoryStoreTest::rejectsUnsafeManifestAndUrls()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeFetch fetch;
    NuvioRepositoryStore store(directory.path(), fetch.function());
    for (const QString &address : {QStringLiteral("http://repo.example/manifest.json"),
                                   QStringLiteral("https://user:pass@repo.example/manifest.json"),
                                   QStringLiteral("https://repo.example/manifest.json?token=test")}) {
        store.addRepository(address);
        QVERIFY(!store.error().isEmpty());
        QVERIFY(fetch.requests.isEmpty());
    }

    const auto valid = scraper("alpha", "plugins/alpha.js", "movie");
    for (const QJsonArray &entries : {QJsonArray{valid, valid},
                                      QJsonArray{scraper("alpha", "../alpha.js", "movie")},
                                      QJsonArray{scraper("alpha", "%2e%2e/alpha.js", "movie")},
                                      QJsonArray{scraper("alpha", "https://other.example/alpha.js", "movie")},
                                      QJsonArray{scraper("alpha", "/alpha.js", "movie")},
                                      QJsonArray{scraper("../alpha", "alpha.js", "movie")}}) {
        store.addRepository(ManifestUrl.toString());
        fetch.respond(ManifestUrl, manifest(QStringLiteral("Unsafe"), entries));
        QVERIFY(!store.error().isEmpty());
        QVERIFY(store.repositories().isEmpty());
    }

    store.addRepository(ManifestUrl.toString());
    fetch.respond(ManifestUrl, manifest(QStringLiteral("Community"), {valid}));
    store.setPluginEnabled(repoId(store), QStringLiteral("alpha"), true);
    QCOMPARE(fetch.requests.size(), 1);
    const auto request = fetch.requests.takeFirst();
    request.complete("remote", QUrl(QStringLiteral("https://other.example/alpha.js")), {});
    QVERIFY(!store.error().isEmpty());
    QVERIFY(store.enabledPlugins(QStringLiteral("movie")).isEmpty());
    request.complete("late", AlphaUrl, {});
    QVERIFY(store.enabledPlugins(QStringLiteral("movie")).isEmpty());
}

void NuvioRepositoryStoreTest::startupPrunesOnlyWithReadableIndex()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString scripts = directory.filePath(QStringLiteral("scripts"));
    QVERIFY(QDir().mkpath(scripts));
    const QString orphan = scriptPath(directory, "orphan");
    const QString unrelated = QDir(scripts).filePath(QStringLiteral("notes.js"));
    for (const QString &path : {orphan, unrelated}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("orphan"), 6);
    }
    QFile index(directory.filePath(QStringLiteral("repositories.json")));
    QVERIFY(index.open(QIODevice::WriteOnly));
    QCOMPARE(index.write("corrupt"), 7);
    index.close();

    FakeFetch fetch;
    {
        NuvioRepositoryStore unreadable(directory.path(), fetch.function());
        QVERIFY(!unreadable.error().isEmpty());
        QVERIFY(QFile::exists(orphan));
    }

    QVERIFY(index.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(index.write(R"({"schema":1,"repositories":[]})") > 0);
    index.close();
    NuvioRepositoryStore readable(directory.path(), fetch.function());
    QVERIFY(readable.error().isEmpty());
    QVERIFY(!QFile::exists(orphan));
    QVERIFY(QFile::exists(unrelated));
}

QTEST_GUILESS_MAIN(NuvioRepositoryStoreTest)
#include "nuvio_repository_store_test.moc"
