#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QJSEngine>
#include "tankoyomi_fake_network.h"

#include "engine/MangaImageHostResolver.h"
#include "engine/TankoyomiScriptProvider.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    QFile script(directory.filePath(QStringLiteral("provider.js")));
    if (!script.open(QIODevice::WriteOnly)) return 2;
    script.write(R"JS(var TankoyomiProvider = {
      searchSeries: function(ctx, url) {
        return ctx.fetchText(url, {timeoutMs: 1000}).then(function(body) {
          return [{id: 'result', title: body}];
        });
      },
      getChapters: function(ctx, request) {
        return ctx.fetchText(request.url, {method: request.method, body: 'payload', timeoutMs: 1000})
          .then(function(body) { return [{id: 'result', title: body}]; });
      }
    };)JS");
    script.close();
    int failures = 0;
    const auto check = [&failures](bool ok, const char *label) {
        qInfo() << (ok ? "ok" : "FAIL") << label;
        if (!ok) ++failures;
    };
    struct Result { bool resolved = false; bool failed = false; QVariant value; QString error; qint64 elapsed = 0; };
    const auto pinningLookup = [](const QString &, MangaImageHostResolver::LookupDone done) {
        done(QStringLiteral("93.184.216.34"));
    };
    const auto perform = [&](TankoyomiTest::Nam &nam, const QString &method = QStringLiteral("GET"),
                             MangaImageHostResolver::Lookup lookup = {}) {
        QObject owner;
        auto *provider = new TankoyomiScriptProvider("fixture", "en", script.fileName(),
                                                     {QStringLiteral("source.example")}, &nam,
                                                     lookup ? lookup : MangaImageHostResolver::Lookup(pinningLookup),
                                                     &owner);
        Result result;
        QEventLoop loop;
        QElapsedTimer elapsed;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(provider, &TankoyomiScriptProvider::resolved, &loop,
                         [&](const QString &, const QVariant &value) {
            result.resolved = true; result.value = value; loop.quit();
        });
        QObject::connect(provider, &TankoyomiScriptProvider::failed, &loop,
                         [&](const QString &, const QString &message) {
            result.failed = true; result.error = message; loop.quit();
        });
        elapsed.start(); watchdog.start(1800);
        if (method == QLatin1String("GET"))
            provider->searchSeries("request", "https://source.example/start");
        else
            provider->getChapters("request", {{"url", "https://source.example/start"}, {"method", method}});
        if (!result.resolved && !result.failed) loop.exec();
        result.elapsed = elapsed.elapsed();
        return result;
    };
    {
        TankoyomiTest::Nam nam;
        nam.respond = [](const QNetworkRequest &, QNetworkAccessManager::Operation, int count) {
            TankoyomiTest::ReplySpec spec;
            if (count == 1) { spec.status = 302; spec.redirect = QUrl("/final"); spec.body = "rejected-body"; }
            return spec;
        };
        const auto result = perform(nam);
        check(result.resolved && !result.failed && nam.requests.size() == 2,
              "injected manager follows an allowed relative redirect");
        check(result.value.toList().value(0).toMap().value("title") == QStringLiteral("accepted"),
              "only the final response body reaches JavaScript");
        check(!nam.requests.isEmpty() && nam.requests.first().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()
                  == QNetworkRequest::ManualRedirectPolicy, "metadata requests never auto-follow redirects");
    }
    {
        TankoyomiTest::Nam nam;
        nam.respond = [](const QNetworkRequest &, QNetworkAccessManager::Operation, int) {
            TankoyomiTest::ReplySpec spec; spec.status = 302;
            spec.redirect = QUrl("https://evil.example/escape"); spec.body = "must-not-leak"; return spec;
        };
        const auto result = perform(nam);
        check(result.failed && !result.resolved && nam.requests.size() == 1,
              "foreign redirect fails before a second request or body delivery");
    }
    {
        TankoyomiTest::Nam nam;
        nam.respond = [](const QNetworkRequest &, QNetworkAccessManager::Operation, int) {
            TankoyomiTest::ReplySpec spec; spec.status = 302; spec.redirect = QUrl("/again"); return spec;
        };
        const auto result = perform(nam);
        check(result.failed && !result.resolved && nam.requests.size() == 6,
              "five redirects are allowed and the sixth is rejected");
    }
    {
        TankoyomiTest::Nam nam;
        const auto probe = std::make_shared<TankoyomiTest::ReplyProbe>();
        nam.respond = [probe](const QNetworkRequest &, QNetworkAccessManager::Operation, int) {
            TankoyomiTest::ReplySpec spec; spec.neverFinish = true; spec.trickle = true; spec.probe = probe; return spec;
        };
        const auto result = perform(nam);
        qInfo() << "deadline evidence" << result.failed << result.resolved << result.elapsed << result.error;
        check(result.failed && !result.resolved && result.elapsed < 1500
                  && result.error.contains("timeout", Qt::CaseInsensitive), "absolute deadline defeats trickling data");
        check(probe->ticks > 0 && probe->aborts == 1, "expired reply is aborted exactly once");
    }
    {
        TankoyomiTest::Nam nam;
        const auto result = perform(nam, "DELETE");
        check(result.failed && !result.resolved && nam.requests.isEmpty(), "unsupported method fails before network access");
    }
    {
        TankoyomiTest::Nam nam;
        nam.respond = [](const QNetworkRequest &, QNetworkAccessManager::Operation, int count) {
            TankoyomiTest::ReplySpec spec;
            if (count == 1) { spec.status = 307; spec.redirect = QUrl("https://api.source.example/final"); }
            return spec;
        };
        const auto result = perform(nam, "POST");
        check(result.resolved && nam.operations == QList<QNetworkAccessManager::Operation>{
                  QNetworkAccessManager::PostOperation, QNetworkAccessManager::PostOperation},
              "307 preserves POST inside the declared host capability");
    }
    {
        TankoyomiTest::Nam nam;
        QStringList resolvedHosts;
        const auto trackingLookup = [&resolvedHosts](const QString &host, MangaImageHostResolver::LookupDone done) {
            resolvedHosts.append(host);
            done(QStringLiteral("93.184.216.34"));
        };
        nam.respond = [&resolvedHosts](const QNetworkRequest &, QNetworkAccessManager::Operation, int count) {
            TankoyomiTest::ReplySpec spec;
            if (count == 1) { spec.status = 302; spec.redirect = QUrl("https://api.source.example/final"); }
            return spec;
        };
        const auto result = perform(nam, QStringLiteral("GET"), trackingLookup);
        check(result.resolved && !result.failed && nam.requests.size() == 2,
              "pinned metadata requests follow allowed redirects");
        check(!nam.requests.isEmpty() && nam.requests.first().url().host() == QStringLiteral("93.184.216.34"),
              "metadata requests travel to the resolved public IPv4 wire address");
        check(!nam.requests.isEmpty() && nam.requests.first().rawHeader("Host") == QByteArrayLiteral("source.example")
                  && nam.requests.first().peerVerifyName() == QStringLiteral("source.example"),
              "pinned metadata keeps the logical hostname for Host and TLS identity");
        check(!nam.requests.isEmpty()
                  && !nam.requests.first().attribute(QNetworkRequest::Http2AllowedAttribute).toBool(),
              "pinned metadata disables HTTP/2");
        check(resolvedHosts == QStringList{QStringLiteral("source.example"), QStringLiteral("api.source.example")},
              "every redirected host re-enters resolution before its request");
        check(nam.requests.size() > 1 && nam.requests.at(1).url().host() == QStringLiteral("93.184.216.34")
                  && nam.requests.at(1).rawHeader("Host") == QByteArrayLiteral("api.source.example"),
              "redirected metadata hops pin again with their own logical Host");
    }
    {
        for (const QString &resolution : {QString(), QStringLiteral("192.168.1.5"), QStringLiteral("::1")}) {
            TankoyomiTest::Nam nam;
            const auto deadLookup = [resolution](const QString &, MangaImageHostResolver::LookupDone done) {
                done(resolution);
            };
            const auto result = perform(nam, QStringLiteral("GET"), deadLookup);
            check(result.failed && !result.resolved && nam.requests.isEmpty(),
                  "empty or non-public resolution fails closed before any network request");
        }
    }
    {
        TankoyomiTest::Nam nam;
        MangaImageHostResolver::LookupDone lateDone;
        const auto holdingLookup = [&lateDone](const QString &, MangaImageHostResolver::LookupDone done) {
            lateDone = done;
        };
        QObject owner;
        auto *provider = new TankoyomiScriptProvider("fixture", "en", script.fileName(),
                                                     {QStringLiteral("source.example")}, &nam,
                                                     holdingLookup, &owner);
        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(provider, &TankoyomiScriptProvider::failed, &loop, &QEventLoop::quit);
        QObject::connect(provider, &TankoyomiScriptProvider::resolved, &loop, &QEventLoop::quit);
        watchdog.start(200);
        provider->searchSeries("request", "https://source.example/start");
        loop.exec();
        delete provider;
        if (lateDone) lateDone(QStringLiteral("93.184.216.34"));
        QCoreApplication::processEvents();
        check(nam.requests.isEmpty(),
              "a late resolver callback cannot issue a request after provider destruction");
    }
    qInfo() << (failures ? "TANKOYOMI_TRANSPORT_FAIL" : "TANKOYOMI_TRANSPORT_OK");
    return failures ? 1 : 0;
}
