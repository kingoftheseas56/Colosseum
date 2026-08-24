#include "../native/net/Ipv4PinStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

static int fails = 0;
#define CHECK(c,l) do { if (!(c)) { ++fails; std::printf("FAIL: %s\n", l); } } while (0)

static void writeCache(const QString& path, const QString& host, const QString& ipv4)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    const QJsonObject root{{QStringLiteral("schema"), 1},
                           {QStringLiteral("pins"), QJsonObject{{host, ipv4}}}};
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    CHECK(temp.isValid(), "temporary directory exists");

    const QString cachePath = QDir(temp.path()).filePath(QStringLiteral("pins.json"));
    writeCache(cachePath, QStringLiteral("cached.example"), QStringLiteral("10.0.0.4"));

    int lookups = 0;
    Ipv4PinStore store(cachePath,
        [&lookups](const QString& host, Ipv4PinStore::LookupDone done) {
            ++lookups;
            QTimer::singleShot(0, [host, done = std::move(done)]() mutable {
                if (host == QStringLiteral("fresh.example"))
                    done(QStringLiteral("10.0.0.8"));
                else
                    done(QString());
            });
        });

    CHECK(store.pinForHost(QStringLiteral("cached.example")) == QStringLiteral("10.0.0.4"),
          "last-known-good pin loads synchronously from local cache");
    CHECK(lookups == 0, "constructing store performs no DNS lookup");

    bool finished = false;
    store.setRefreshFinishedCallback([&] {
        finished = true;
        app.quit();
    });
    store.refresh({QStringLiteral("fresh.example"), QStringLiteral("cached.example")});
    CHECK(!finished, "refresh returns before asynchronous lookup completion");
    CHECK(lookups == 2, "refresh starts one lookup per unique requested host");

    QTimer::singleShot(2000, &app, [&] {
        if (!finished) {
            ++fails;
            std::printf("FAIL: asynchronous refresh timed out\n");
            app.quit();
        }
    });
    app.exec();

    CHECK(finished, "refresh completion signal emitted");
    CHECK(store.pinForHost(QStringLiteral("fresh.example")) == QStringLiteral("10.0.0.8"),
          "successful asynchronous lookup becomes live in same session");
    CHECK(store.pinForHost(QStringLiteral("cached.example")) == QStringLiteral("10.0.0.4"),
          "failed refresh retains cached last-known-good pin");
    CHECK(lookups == 5, "failed host receives exactly four asynchronous attempts");

    Ipv4PinStore reloaded(cachePath);
    CHECK(reloaded.pinForHost(QStringLiteral("fresh.example")) == QStringLiteral("10.0.0.8"),
          "successful refresh persists atomically for next launch");
    CHECK(reloaded.pinForHost(QStringLiteral("cached.example")) == QStringLiteral("10.0.0.4"),
          "retained last-known-good pin remains persisted");
    int dedupeLookups = 0;
    Ipv4PinStore dedupeStore(QDir(temp.path()).filePath(QStringLiteral("dedupe.json")),
        [&dedupeLookups](const QString&, Ipv4PinStore::LookupDone done) {
            ++dedupeLookups;
            QTimer::singleShot(20, [done = std::move(done)]() mutable {
                done(QStringLiteral("127.0.0.1"));
            });
        });
    dedupeStore.refresh({QStringLiteral("same.example"), QStringLiteral("same.example")});
    dedupeStore.refresh({QStringLiteral("same.example")});
    CHECK(dedupeLookups == 1, "in-flight host lookup is deduplicated");

    std::printf(fails ? "FAILS: %d\n" : "ipv4_pin_store_harness: ALL PASS\n", fails);
    return fails;
}
