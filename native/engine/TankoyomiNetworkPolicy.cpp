#include "TankoyomiNetworkPolicy.h"

namespace TankoyomiNetworkPolicy {

bool metadataHostAllowed(const QString &host, const QStringList &allowedHosts)
{
    const QString normalized = host.trimmed().toLower();
    if (normalized.isEmpty()) return false;
    for (const QString &entry : allowedHosts) {
        const QString allowed = entry.trimmed().toLower();
        if (!allowed.isEmpty()
            && (normalized == allowed || normalized.endsWith(QLatin1Char('.') + allowed)))
            return true;
    }
    return false;
}

bool resolvedAddressAllowed(const QHostAddress &address, const QString &policy)
{
    // Qt isGlobal() includes RFC1918, ULA and deprecated site-local addresses.
    // A public-network capability must explicitly exclude those as well.
    if (policy != QLatin1String("public-https") || address.isNull()
        || !address.scopeId().isEmpty() || !address.isGlobal()
        || address.isLoopback() || address.isLinkLocal() || address.isSiteLocal()
        || address.isUniqueLocalUnicast() || address.isPrivateUse()
        || address.isMulticast() || address.isBroadcast())
        return false;

    bool isV4 = false;
    const quint32 v4 = address.toIPv4Address(&isV4);
    if (isV4) {
        // Normalize IPv4-mapped IPv6 before testing special-purpose ranges.
        const QHostAddress ipv4(v4);
        static const char *const excluded[] = {
            "0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8",
            "169.254.0.0/16", "172.16.0.0/12", "192.0.0.0/24", "192.0.2.0/24",
            "192.88.99.0/24", "192.168.0.0/16", "198.18.0.0/15",
            "198.51.100.0/24", "203.0.113.0/24", "224.0.0.0/4", "240.0.0.0/4"
        };
        for (const char *subnet : excluded) {
            if (ipv4.isInSubnet(QHostAddress::parseSubnet(QString::fromLatin1(subnet))))
                return false;
        }
        return true;
    }
    // Only global-unicast IPv6 is eligible. Translation/tunnel and documentation
    // ranges are not an escape hatch to non-public IPv4 destinations.
    if (!address.isInSubnet(QHostAddress::parseSubnet(QStringLiteral("2000::/3"))))
        return false;
    for (const char *subnet : {"2001::/32", "2001:2::/48", "2001:10::/28",
                              "2001:20::/28", "2001:db8::/32", "2002::/16", "3fff::/20"}) {
        if (address.isInSubnet(QHostAddress::parseSubnet(QString::fromLatin1(subnet))))
            return false;
    }
    return true;
}

bool pageUrlAllowedBeforeDns(const QUrl &url, const QString &policy)
{
    if (policy != QLatin1String("public-https") || !url.isValid()
        || url.scheme() != QLatin1String("https") || url.host().isEmpty()
        || url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@')))
        return false;
    QString host = url.host().toLower();
    if (host.endsWith(QLatin1Char('.'))) host.chop(1);
    if (host == QLatin1String("localhost") || host.endsWith(QLatin1String(".localhost")))
        return false;
    const QHostAddress literal(host);
    return literal.isNull() || resolvedAddressAllowed(literal, policy);
}

bool redirectAllowed(const QUrl &from, const QUrl &target,
                     const QStringList &allowedHosts)
{
    if (target.isEmpty() || !target.isValid()) return false;
    const QUrl resolved = from.resolved(target);
    return pageUrlAllowedBeforeDns(resolved, QStringLiteral("public-https"))
        && metadataHostAllowed(resolved.host(), allowedHosts);
}

} // namespace TankoyomiNetworkPolicy
