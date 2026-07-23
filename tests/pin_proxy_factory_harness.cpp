// pin_proxy_factory_harness.cpp — proves the concierge is scoped: metahub → the
// loopback HttpProxy; everything else → NoProxy; disabled → NoProxy even for metahub.
#include "../native/net/PinProxyFactory.h"
#include <QUrl>
#include <cstdio>

static int fails = 0;
#define CHECK(c,l) do{ if(!(c)){ ++fails; std::printf("FAIL: %s\n", l);} }while(0)

static QNetworkProxy first(QNetworkProxyFactory& f, const char* url) {
    return f.queryProxy(QNetworkProxyQuery(QUrl(QString::fromUtf8(url)))).first();
}

int main() {
    const QSet<QString> hosts = { QStringLiteral("live.metahub.space"),
                                  QStringLiteral("images.metahub.space") };

    PinProxyFactory on(hosts, 54321, /*enabled=*/true);
    const QNetworkProxy m = first(on, "https://images.metahub.space/poster/small/tt1/img");
    CHECK(m.type() == QNetworkProxy::HttpProxy, "metahub → HttpProxy");
    CHECK(m.hostName() == QStringLiteral("127.0.0.1") && m.port() == 54321, "metahub → 127.0.0.1:54321");

    const QNetworkProxy other = first(on, "https://v3-cinemeta.strem.io/meta/movie/tt1.json");
    CHECK(other.type() == QNetworkProxy::NoProxy, "non-metahub → NoProxy");

    PinProxyFactory off(hosts, 0, /*enabled=*/false);
    CHECK(first(off, "https://images.metahub.space/x").type() == QNetworkProxy::NoProxy,
          "disabled → NoProxy even for metahub");

    std::printf(fails ? "FAILS: %d\n" : "pin_proxy_factory_harness: ALL PASS\n", fails);
    return fails;
}
