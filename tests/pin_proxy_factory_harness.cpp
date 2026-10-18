#include "../native/net/PinProxyFactory.h"
#include "../native/net/Ipv4PinStore.h"
#include <QCoreApplication>
#include <QDir>
#include <QNetworkProxyFactory>
#include <QTemporaryDir>
#include <QUrl>
#include <cstdio>

static int fails = 0;
static QString capturedMessages;
static void captureMessage(QtMsgType, const QMessageLogContext&, const QString& message) {
    capturedMessages += message;
}
#define CHECK(c,l) do{ if(!(c)){ ++fails; std::printf("FAIL: %s\n", l);} }while(0)
static QNetworkProxy first(QNetworkProxyFactory& f, const char* url) {
    return f.queryProxy(QNetworkProxyQuery(QUrl(QString::fromUtf8(url)))).first();
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // Process-local Qt configuration only; this harness never opens a connection.
    qputenv("http_proxy", "http://127.0.0.1:32123");
    qputenv("https_proxy", "http://127.0.0.1:32123");
    qputenv("no_proxy", "bypass.example");
    qunsetenv("COLOSSEUM_USE_SYSTEM_PROXY");
    QTemporaryDir temp;
    Ipv4PinStore pins(QDir(temp.path()).filePath(QStringLiteral("pins.json")),
        [](const QString&, Ipv4PinStore::LookupDone done) { done(QStringLiteral("127.0.0.1")); });
    const QSet<QString> hosts = {QStringLiteral("live.metahub.space"),
                                 QStringLiteral("images.metahub.space")};
    PinProxyFactory on(hosts, &pins, 54321, true);
    CHECK(first(on, "https://images.metahub.space/x").type() == QNetworkProxy::NoProxy,
          "unpinned cold host uses NoProxy");
    pins.refresh({QStringLiteral("images.metahub.space")});
    const QNetworkProxy m = first(on, "https://images.metahub.space/x");
    CHECK(m.type() == QNetworkProxy::HttpProxy, "live pin enables concierge");
    CHECK(m.hostName() == QStringLiteral("127.0.0.1") && m.port() == 54321,
          "concierge endpoint exact");
    CHECK(first(on, "https://v3-cinemeta.strem.io/x").type() == QNetworkProxy::NoProxy,
          "unscoped host uses NoProxy");
    PinProxyFactory off(hosts, &pins, 0, false);
    CHECK(first(off, "https://images.metahub.space/x").type() == QNetworkProxy::NoProxy,
          "disabled concierge uses NoProxy");
    for (const QByteArray flag : {QByteArray("0"), QByteArray("true"), QByteArray("yes"),
                                  QByteArray("01"), QByteArray(" 1"), QByteArray("garbage")}) {
        qputenv("COLOSSEUM_USE_SYSTEM_PROXY", flag);
        PinProxyFactory disabled(hosts, &pins, 54321, true);
        CHECK(first(disabled, "https://remote.example/x").type() == QNetworkProxy::NoProxy,
              "disabled/malformed flag retains direct default");
        CHECK(first(disabled, "https://images.metahub.space/x").port() == 54321,
              "disabled/malformed flag retains concierge");
    }
    qputenv("COLOSSEUM_USE_SYSTEM_PROXY", "1");
    PinProxyFactory system(hosts, &pins, 54321, true);
#ifdef Q_OS_LINUX
    // Actual Qt system resolver reads the controlled process configuration.
    for (const char *url : {"http://remote.example/x", "https://remote.example/x",
                            "https://images.metahub.space/x"}) {
        CHECK(first(system, url).type() == QNetworkProxy::HttpProxy
              && first(system, url).port() == 32123,
              "opt-in Qt system proxy supersedes direct and concierge for HTTP/HTTPS");
    }
    CHECK(first(system, "https://bypass.example/x").type() == QNetworkProxy::NoProxy,
          "Qt configured no_proxy bypass respected");
#endif
    for (const char *url : {"http://localhost/x", "http://LOCALHOST./x",
                            "http://service.localhost/x", "http://127.0.0.1/x",
                            "http://127.42.0.1/x", "http://[::1]/x"}) {
        CHECK(first(system, url).type() == QNetworkProxy::NoProxy,
              "loopback service stays direct");
    }
    const QNetworkProxy http(QNetworkProxy::HttpProxy, QStringLiteral("proxy.example"), 8080,
                             QStringLiteral("fixture-user"), QStringLiteral("fixture-secret"));
    const QNetworkProxy socks(QNetworkProxy::Socks5Proxy, QStringLiteral("proxy2.example"), 1080);
    int queries = 0;
    PinProxyFactory mixed(hosts, &pins, 54321, true,
        [&](const QNetworkProxyQuery& query) {
            ++queries;
            CHECK(query.url() == QUrl("https://images.metahub.space/x"),
                  "system resolver receives canonical HTTPS query");
            return QList<QNetworkProxy>{http, QNetworkProxy(QNetworkProxy::NoProxy),
                QNetworkProxy(QNetworkProxy::DefaultProxy), socks};
        });
    const auto oldHandler = qInstallMessageHandler(captureMessage);
    const auto selected = mixed.queryProxy(QNetworkProxyQuery(QUrl("https://images.metahub.space/x")));
    qInstallMessageHandler(oldHandler);
    CHECK(!capturedMessages.contains(QStringLiteral("fixture-user"))
          && !capturedMessages.contains(QStringLiteral("fixture-secret"))
          && !capturedMessages.contains(QStringLiteral("proxy.example")),
          "system configuration and credentials are not logged");
    CHECK(selected == QList<QNetworkProxy>({http, socks}),
          "explicit proxy order and credentials preserved without direct fallback");
    CHECK(first(mixed, "http://127.0.0.1/x").type() == QNetworkProxy::NoProxy && queries == 1,
          "loopback bypass does not call platform resolver");
    PinProxyFactory direct(hosts, &pins, 54321, true,
        [](const QNetworkProxyQuery&) { return QList<QNetworkProxy>{QNetworkProxy(QNetworkProxy::NoProxy)}; });
    CHECK(first(direct, "https://images.metahub.space/x").type() == QNetworkProxy::NoProxy,
          "platform direct bypass wins over concierge even with a warm pin");
    PinProxyFactory empty(hosts, &pins, 54321, true,
        [](const QNetworkProxyQuery&) { return QList<QNetworkProxy>{}; });
    CHECK(first(empty, "https://remote.example/x").type() == QNetworkProxy::NoProxy,
          "empty platform configuration uses direct");
    int nonLoopbackQueries = 0;
    PinProxyFactory managed({}, nullptr, 0, false,
        [&](const QNetworkProxyQuery&) { ++nonLoopbackQueries; return QList<QNetworkProxy>{http}; });
    for (const char *url : {"https://10.0.0.1/x", "https://192.168.1.1/x",
                           "https://169.254.1.1/x", "https://service.local/x", "https://intranet/x"}) {
        CHECK(first(managed, url) == http, "non-loopback names and addresses retain platform policy");
    }
    CHECK(nonLoopbackQueries == 5, "no blanket private-network bypass");
#ifdef Q_OS_LINUX
    qunsetenv("no_proxy");
    qputenv("NO_PROXY", "uppercase-bypass.example");
    const QNetworkProxyQuery uppercaseQuery(QUrl("https://uppercase-bypass.example/x"));
    const auto qtUppercase = QNetworkProxyFactory::systemProxyForQuery(uppercaseQuery);
    CHECK(system.queryProxy(uppercaseQuery).first() == qtUppercase.first(),
          "uppercase NO_PROXY behavior follows actual Qt platform resolver");
    std::printf("Qt Linux uppercase-only NO_PROXY honored: %s\n",
                qtUppercase.first().type() == QNetworkProxy::NoProxy ? "yes" : "no");
#endif
    qunsetenv("COLOSSEUM_USE_SYSTEM_PROXY");
    PinProxyFactory untouched(hosts, &pins, 54321, true,
        [&](const QNetworkProxyQuery&) { ++queries; return QList<QNetworkProxy>{http}; });
    CHECK(first(untouched, "https://remote.example/x").type() == QNetworkProxy::NoProxy && queries == 1,
          "default never asks system configuration");
    std::printf(fails ? "FAILS: %d\n" : "pin_proxy_factory_harness: ALL PASS\n", fails);
    return fails;
}
