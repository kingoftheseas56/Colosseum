#include "nuvio/NuvioSourceResolver.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <csignal>
#include <cstdio>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
int fakeWorker(const QStringList &args)
{
    QJsonArray rows;
    if (args[1] == QLatin1String("--lookup")) {
        rows.append(QJsonObject{{"tmdbId", 550}});
    } else {
        QFile snapshot(args[2]);
        if (!snapshot.open(QIODevice::ReadOnly))
            return 2;
        const auto script = snapshot.readAll();
        if (script == "slow")
            QThread::msleep(6000);
        if (script == "crash") {
#ifdef Q_OS_WIN
            TerminateProcess(GetCurrentProcess(), 0xc0000005);
#else
            std::raise(SIGKILL);
#endif
            return 2;
        }
        rows.append(QJsonObject{
            {"url", "https://8.8.8.8/stream"}, {"name", QString::fromLatin1(script)},
            {"headers", QJsonObject{{"Referer", "https://origin.example/"}, {"Host", "not-accepted"},
                                   {"X-Invalid", "bad\r\nvalue"}}},
            {"behaviorHints", QJsonObject{{"proxyHeaders", QJsonObject{
                {"request", QJsonObject{{"User-Agent", "fixture-agent"}}},
                {"response", QJsonObject{{"X-Response", "not-a-request-header"}}}
            }}}}
        });
        rows.append(QJsonObject{{"url", "http://127.0.0.1/stream"}, {"name", "loopback"}});
        rows.append(QJsonObject{{"url", "http://8.8.8.8/stream"}, {"name", "plain HTTP"}});
        rows.append(QJsonObject{{"url", "https://8.8.8.8/stream"}, {"infoHash", "abcdef"}});
    }
    const auto payload = QJsonDocument(QJsonObject{{"status", "OK"}, {"streams", rows}})
        .toJson(QJsonDocument::Compact);
    std::fwrite(payload.constData(), 1, payload.size(), stdout);
    return 0;
}

void fixtureRepo(const QUrl &url, qsizetype, QObject *, NuvioRepositoryStore::FetchCompletion complete)
{
    if (url.fileName() == "manifest.json") {
        complete(R"JSON({"name":"Test repo","scrapers":[
            {"id":"good","name":"Good Plugin","version":"1","filename":"good.js","supportedTypes":["movie"]},
            {"id":"slow","name":"Slow Plugin","version":"1","filename":"slow.js","supportedTypes":["movie"]},
            {"id":"crash","name":"Crash Plugin","version":"1","filename":"crash.js","supportedTypes":["movie"]},
            {"id":"off","name":"Disabled Plugin","version":"1","filename":"off.js","supportedTypes":["movie"]}
        ]})JSON", url, {});
    } else {
        complete(url.fileName().chopped(3).toLatin1(), url, {});
    }
}
}

class NuvioSourceResolverTest final : public QObject
{
    Q_OBJECT

private slots:
    void resultsSurviveCrashTimeoutAndDisabledPlugin();
    void supersededSearchDoesNotDeliverRows();
};

void NuvioSourceResolverTest::resultsSurviveCrashTimeoutAndDisabledPlugin()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    NuvioRepositoryStore store(temporary.path(), fixtureRepo);
    store.addRepository(QStringLiteral("https://repo.example/manifest.json"));
    QTRY_COMPARE(store.repositories().size(), 1);
    NuvioSourceResolver resolver(&store, QCoreApplication::applicationFilePath(), nullptr, 2500);
    QSignalSpy batches(&resolver, &NuvioSourceResolver::streamsReady);
    QSignalSpy finished(&resolver, &NuvioSourceResolver::finished);
    QCOMPARE(resolver.resolve("550", "movie", 1, 1), 0);

    const auto repoId = store.repositories().first().toMap().value("id").toString();
    for (const auto &plugin : {"good", "slow", "crash", "off"}) {
        store.setPluginEnabled(repoId, QString::fromLatin1(plugin), true);
        QTRY_VERIFY(!store.busy());
        QVERIFY(store.error().isEmpty());
    }
    store.setPluginEnabled(repoId, QStringLiteral("off"), false);
    QTRY_COMPARE(store.enabledPlugins("movie").size(), 3);

    const int requestId = resolver.resolve("tmdb:550", "movie", 1, 1);
    QVERIFY(requestId > 0);
    QTRY_COMPARE_WITH_TIMEOUT(batches.size(), 1, 8000);
    QCOMPARE(batches.first().first().toInt(), requestId);
    const auto rows = batches.first().at(1).toList();
    QCOMPARE(rows.size(), 1);
    const auto row = rows.first().toMap();
    QCOMPARE(row.value("pluginName").toString(), QStringLiteral("Good Plugin"));
    QCOMPARE(row.value("pluginId").toString(), QStringLiteral("good"));
    QCOMPARE(row.value("repoId").toString(), repoId);
    QCOMPARE(row.value("headers").toMap(), (QVariantMap{
        {"referer", "https://origin.example/"}, {"user-agent", "fixture-agent"}}));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 8000);
    QCOMPARE(finished.first().first().toInt(), requestId);
    QCOMPARE(batches.size(), 1);
}

void NuvioSourceResolverTest::supersededSearchDoesNotDeliverRows()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    NuvioRepositoryStore store(temporary.path(), fixtureRepo);
    store.addRepository(QStringLiteral("https://repo.example/manifest.json"));
    QTRY_COMPARE(store.repositories().size(), 1);
    const auto repoId = store.repositories().first().toMap().value("id").toString();
    store.setPluginEnabled(repoId, QStringLiteral("good"), true);
    QTRY_COMPARE(store.enabledPlugins("movie").size(), 1);
    NuvioSourceResolver resolver(&store, QCoreApplication::applicationFilePath());
    QSignalSpy batches(&resolver, &NuvioSourceResolver::streamsReady);

    const int cancelled = resolver.resolve("tt0137523", "movie", 1, 1);
    QVERIFY(cancelled > 0);
    resolver.cancel(cancelled);
    const int replacement = resolver.resolve("tt0137523", "movie", 1, 1);
    QVERIFY(replacement > cancelled);
    QTRY_COMPARE(batches.size(), 1);
    QCOMPARE(batches.first().first().toInt(), replacement);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() > 2 && (args[1] == "--plugin" || args[1] == "--lookup"))
        return fakeWorker(args);
    NuvioSourceResolverTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "nuvio_source_resolver_test.moc"
