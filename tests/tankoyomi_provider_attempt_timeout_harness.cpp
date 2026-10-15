#include "engine/TankoyomiChapterService.h"
#include "engine/TankoyomiConfigurationStore.h"
#include "engine/TankoyomiIdentity.h"
#include "tankoyomi_fake_network.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    int failures = 0;
    const auto check = [&failures](bool ok, const QString &label) {
        qInfo().noquote() << (ok ? "ok" : "FAIL") << label;
        if (!ok) ++failures;
    };
    for (const QString mode : {"stall-fallback", "late-first", "last-timeout", "all-disabled", "unsupported",
                               "unrelated-identity", "ambiguous-identity", "spin-off-identity"}) {
        TankoyomiTest::Nam nam;
        nam.respond = [mode](const QNetworkRequest &request, QNetworkAccessManager::Operation, int) {
            TankoyomiTest::ReplySpec reply;
            reply.contentType = "application/json";
            const QString path = request.url().path();
            reply.body = path.endsWith(QLatin1String("/search"))
                ? R"JSON([{"id":"series","title":"Fixture","url":"https://93.184.216.34/series"}])JSON"
                : R"JSON([{"id":"chapter","title":"Chapter 1","number":1,"url":"https://93.184.216.34/chapter"}])JSON";
            if (path == QLatin1String("/first/search")) {
                reply.neverFinish = mode == QLatin1String("stall-fallback") || mode == QLatin1String("last-timeout");
                reply.delayMs = mode == QLatin1String("late-first") ? 160 : 0;
                if (mode == QLatin1String("unrelated-identity"))
                    reply.body = R"JSON([{"id":"other","title":"Unrelated Series"}])JSON";
                if (mode == QLatin1String("spin-off-identity"))
                    reply.body = R"JSON([{"id":"spinoff","title":"Fixture Party"}])JSON";
                if (mode == QLatin1String("ambiguous-identity"))
                    reply.body = R"JSON([{"id":"one","title":"Fixture"},{"id":"two","title":"Fixture"}])JSON";
            }
            return reply;
        };
        const auto registry = TankoyomiProviderRegistry::fromResource();
        check(registry.isValid(), mode + " fixture registry");
        TankoyomiConfigurationStore configuration(registry, directory.filePath(mode + ".ini"));
        if (mode == QLatin1String("last-timeout") || mode == QLatin1String("all-disabled"))
            configuration.setProviderEnabled("es", "second", false);
        if (mode == QLatin1String("all-disabled"))
            configuration.setProviderEnabled("es", "first", false);
        TankoyomiChapterService service(&nam, &configuration, 80,
                                         MangaImageHostResolver::Lookup(
                                             [](const QString &, MangaImageHostResolver::LookupDone done) {
                                                 done(QStringLiteral("93.184.216.34"));
                                             }));
        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
        int ready = 0;
        int failed = 0;
        QString selected;
        QString message;
        qint64 settledAt = -1;
        QElapsedTimer elapsed;
        QObject::connect(&service, &TankoyomiChapterService::catalogueReady, &loop,
                         [&](const QString &, const QString &, const QVariantList &chapters) {
            ++ready;
            selected = chapters.value(0).toMap().value("source").toString();
            settledAt = elapsed.elapsed(); loop.quit();
        });
        QObject::connect(&service, &TankoyomiChapterService::catalogueFailed, &loop,
                         [&](const QString &, const QString &error) {
            ++failed; message = error; settledAt = elapsed.elapsed(); loop.quit();
        });
        elapsed.start(); watchdog.start(500);
        service.fetchCatalogue("request", "Fixture", mode == QLatin1String("unsupported") ? "zz" : "es");
        if (!ready && !failed) loop.exec();
        watchdog.stop();
        if (mode == QLatin1String("late-first")) {
            QEventLoop late;
            QTimer::singleShot(220, &late, &QEventLoop::quit);
            late.exec();
        }
        if (mode == QLatin1String("stall-fallback") || mode == QLatin1String("late-first")) {
            check(ready == 1 && failed == 0 && selected == QLatin1String("second")
                      && settledAt >= 40 && settledAt < 400, mode + " advances once to the same-language provider");
            check(nam.requests.size() == 3, mode + " late first-provider search never starts chapters");
        } else if (mode.endsWith(QLatin1String("-identity"))) {
            check(ready == 1 && failed == 0 && selected == QLatin1String("second")
                      && settledAt < 400 && nam.requests.size() == 3,
                  mode + " rejects guessed identity before fetching first-provider chapters");
        } else if (mode == QLatin1String("last-timeout")) {
            check(ready == 0 && failed == 1 && settledAt >= 40 && settledAt < 400
                      && message.contains("first") && message.contains("es")
                      && message.contains("timeout", Qt::CaseInsensitive),
                  mode + " final timeout names provider and language");
        } else {
            check(ready == 0 && failed == 1 && nam.requests.isEmpty() && settledAt < 100,
                  mode + " fails immediately without provider requests");
        }
        bool crossedLanguage = false;
        for (const auto &request : nam.requests)
            crossedLanguage = crossedLanguage || request.url().path().contains("english");
        check(!crossedLanguage, mode + " never crosses into English");
    }
    qInfo() << (failures ? "TANKOYOMI_ATTEMPT_TIMEOUT_FAIL" : "TANKOYOMI_ATTEMPT_TIMEOUT_OK");
    return failures ? 1 : 0;
}
