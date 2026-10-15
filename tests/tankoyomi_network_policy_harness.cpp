#include "engine/TankoyomiNetworkPolicy.h"
#include <QCoreApplication>
#include <QDebug>
#include <QHostAddress>
#include <QUrl>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&failures](bool ok, const QString &label) {
        qInfo().noquote() << (ok ? "ok" : "FAIL") << label;
        if (!ok) ++failures;
    };
    using namespace TankoyomiNetworkPolicy;
    const QString policy = QStringLiteral("public-https");
    const QStringList hosts{QStringLiteral("source.example")};
    check(metadataHostAllowed(QStringLiteral("SOURCE.example"), hosts), "exact metadata host");
    check(metadataHostAllowed(QStringLiteral("api.source.example"), hosts), "allowed subdomain");
    check(!metadataHostAllowed(QStringLiteral("badsource.example"), hosts), "suffix boundary");
    check(!metadataHostAllowed(QStringLiteral("source.example.evil.test"), hosts), "suffix escape");
    check(!metadataHostAllowed(QString(), hosts), "empty host");
    check(pageUrlAllowedBeforeDns(QUrl("https://cdn.example/page.jpg"), policy), "public HTTPS shape");
    for (const char *url : {"http://cdn.example/a", "file:///etc/passwd", "https:///a",
                           "https://user:pass@cdn.example/a", "https://@cdn.example/a",
                           "https://localhost/a", "https://localhost./a", "https://x.localhost/a"})
        check(!pageUrlAllowedBeforeDns(QUrl(QString::fromLatin1(url)), policy), QString::fromLatin1(url));
    for (const char *ip : {"0.0.0.0", "0.1.2.3", "127.0.0.1", "10.0.0.1", "172.16.0.1",
                          "192.168.1.2", "169.254.169.254", "100.64.0.1", "192.0.0.1",
                          "192.0.2.1", "198.18.0.1", "198.51.100.1", "203.0.113.1",
                          "224.0.0.1", "240.0.0.1", "255.255.255.255", "::", "::1",
                          "fe80::1", "fec0::1", "fc00::1", "ff02::1", "2001:db8::1",
                          "::ffff:127.0.0.1", "::ffff:192.168.1.1"}) {
        QHostAddress address(QString::fromLatin1(ip));
        check(!resolvedAddressAllowed(address, policy), QStringLiteral("reject address ") + QString::fromLatin1(ip));
        QUrl url; url.setScheme("https"); url.setHost(QString::fromLatin1(ip)); url.setPath("/page.jpg");
        check(!pageUrlAllowedBeforeDns(url, policy), QStringLiteral("reject literal ") + QString::fromLatin1(ip));
    }
    check(!resolvedAddressAllowed(QHostAddress(), policy), "empty DNS fails closed");
    check(resolvedAddressAllowed(QHostAddress("93.184.216.34"), policy), "public IPv4");
    check(resolvedAddressAllowed(QHostAddress("2606:4700:4700::1111"), policy), "public IPv6");
    check(!resolvedAddressAllowed(QHostAddress("93.184.216.34"), "unknown"), "unknown address policy");
    check(!pageUrlAllowedBeforeDns(QUrl("https://cdn.example/a"), ""), "missing URL policy");
    const QUrl from("https://source.example/series/a");
    check(redirectAllowed(from, QUrl("../chapter"), hosts), "relative metadata redirect");
    check(redirectAllowed(from, QUrl("https://api.source.example/a"), hosts), "allowed metadata redirect");
    check(!redirectAllowed(from, QUrl("https://evil.test/a"), hosts), "foreign metadata redirect");
    check(!redirectAllowed(from, QUrl("http://source.example/a"), hosts), "HTTPS downgrade");
    check(!redirectAllowed(from, QUrl("https://x@source.example/a"), hosts), "redirect credentials");
    qInfo() << (failures ? "TANKOYOMI_NETWORK_POLICY_FAIL" : "TANKOYOMI_NETWORK_POLICY_OK");
    return failures ? 1 : 0;
}
