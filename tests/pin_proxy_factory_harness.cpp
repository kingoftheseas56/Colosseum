#include "../native/net/PinProxyFactory.h"
#include "../native/net/Ipv4PinStore.h"
#include <QCoreApplication>
#include <QDir>
#include <QNetworkProxyFactory>
#include <QTemporaryDir>
#include <QUrl>
#include <cstdio>

static int fails = 0;
#define CHECK(c,l) do{ if(!(c)){ ++fails; std::printf("FAIL: %s\n", l);} }while(0)
static QNetworkProxy first(QNetworkProxyFactory& f, const char* url) {
    return f.queryProxy(QNetworkProxyQuery(QUrl(QString::fromUtf8(url)))).first();
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
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
    std::printf(fails ? "FAILS: %d\n" : "pin_proxy_factory_harness: ALL PASS\n", fails);
    return fails;
}
