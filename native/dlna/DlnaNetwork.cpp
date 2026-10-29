#include "DlnaNetwork.h"

#include <QNetworkInterface>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <winioctl.h>
#else
#include <QHostAddress>
#endif

#include <algorithm>

namespace {

#ifdef Q_OS_WIN

// Substring blocklist for virtual/tunnel adapter names and descriptions (case-insensitive).
// These are the spec's skip list; everything virtual a home machine grows (Hyper-V, WSL, VPN
// tools) names itself loudly, and a DLNA server must never bind any of them. Many VPN
// drivers register as plain Ethernet (IfType 6), so the name list is the second fence behind
// the IfType allow-list.
const char* const kVirtualNameFragments[] = {
    "vethernet", "hyper-v", "wsl", "virtualbox", "vmware", "tap", "tun",
    "tailscale", "zerotier", "wireguard", "npcap", "loopback", "vpn", "meta",
    "virtual", "miniport", "anyconnect", "pangp", "globalprotect", "fortinet",
    "hamachi", "wintun", "nordlynx", "openvpn",
};

bool isVirtualName(const QString& name)
{
    const QString n = name.toLower();
    for (const char* frag : kVirtualNameFragments)
        if (n.contains(QLatin1String(frag)))
            return true;
    return false;
}

// IPv4 unicast address pupnp will bind for this adapter: its UpnpGetIfInfo walks every unicast
// address and keeps the LAST IPv4 one, so the picker must predict that one, not the first.
// Empty when the adapter has no IPv4 address.
QString lastIpv4(PIP_ADAPTER_ADDRESSES adapter)
{
    QString ip;
    for (auto ua = adapter->FirstUnicastAddress; ua; ua = ua->Next) {
        const sockaddr* sa = ua->Address.lpSockaddr;
        if (!sa || sa->sa_family != AF_INET)
            continue;
        char buf[64] = {0};
        if (getnameinfo(sa, ua->Address.iSockaddrLength, buf, sizeof(buf), nullptr, 0,
                        NI_NUMERICHOST) == 0)
            ip = QString::fromLatin1(buf);
    }
    return ip;
}

// pupnp (built with UPNP_ADAPTER_UUID_NAME) selects the interface by AdapterName - the
// adapter GUID, pure ASCII - so Unicode friendly names ("Wi-Fi 2", "Ethernet für Heim") can
// never miss the match through a lossy ANSI conversion.
QString pupnpIfName(PIP_ADAPTER_ADDRESSES adapter)
{
    return QString::fromLatin1(adapter->AdapterName);
}

QList<DlnaNetAdapter> enumerateWin()
{
    QList<DlnaNetAdapter> out;
    ULONG needed = 16 * 1024;
    QByteArray buf;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int i = 0; i < 4 && rc == ERROR_BUFFER_OVERFLOW; i++) {
        buf.resize(int(needed));
        rc = GetAdaptersAddresses(AF_INET,
                                  GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                                      | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS,
                                  nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()),
                                  &needed);
    }
    if (rc != ERROR_SUCCESS)
        return out;
    for (auto a = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp)
            continue;
        const QString ip = lastIpv4(a);
        if (ip.isEmpty())
            continue;
        DlnaNetAdapter adapter;
        adapter.ifName = pupnpIfName(a);
        adapter.ip = ip; // the address pupnp binds (the adapter's last IPv4)
        adapter.label = QStringLiteral("%1 · %2").arg(QString::fromWCharArray(a->FriendlyName),
                                                      adapter.ip);
        out.append(adapter);
    }
    return out;
}

std::optional<DlnaNetAdapter> pickHomeWin()
{
    // Default route: has an IPv4 gateway; among candidates the lowest interface metric wins
    // (Windows' own route-preference order).
    struct Cand { DlnaNetAdapter adapter; ULONG metric; bool hasGateway; };
    QList<Cand> cands;
    ULONG needed = 16 * 1024;
    QByteArray buf;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int i = 0; i < 4 && rc == ERROR_BUFFER_OVERFLOW; i++) {
        buf.resize(int(needed));
        rc = GetAdaptersAddresses(AF_INET,
                                  GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                                      | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS,
                                  nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()),
                                  &needed);
    }
    if (rc != ERROR_SUCCESS)
        return std::nullopt;
    for (auto a = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp)
            continue;
        // pupnp's own adapter walk skips adapters without multicast; one it would not match
        // must not be picked.
        if (a->Flags & IP_ADAPTER_NO_MULTICAST)
            continue;
        // Allow-list, not block-list: only real wired (Ethernet, 6) and Wi-Fi (IEEE 802.11, 71)
        // links are home-network material. VPN drivers that dress up as Ethernet are caught by
        // the name fence below.
        if (a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211)
            continue;
        const QString friendly = QString::fromWCharArray(a->FriendlyName);
        const QString description = a->Description ? QString::fromWCharArray(a->Description)
                                                   : QString();
        if (isVirtualName(friendly) || isVirtualName(description))
            continue;
        // pupnp binds the adapter's LAST IPv4 address, so that is the one that must be a
        // private (home) address; link-local and public addresses are not "home network".
        const QString ip = lastIpv4(a);
        if (ip.isEmpty() || !DlnaNetwork::isPrivateIpv4(ip))
            continue;
        bool hasGateway = false;
        for (auto g = a->FirstGatewayAddress; g; g = g->Next) {
            if (g->Address.lpSockaddr && g->Address.lpSockaddr->sa_family == AF_INET) {
                hasGateway = true;
                break;
            }
        }
        MIB_IPINTERFACE_ROW row{};
        row.Family = AF_INET;
        row.InterfaceIndex = a->IfIndex;
        ULONG metric = 100000; // unknown metric sorts last
        if (GetIpInterfaceEntry(&row) == NO_ERROR)
            metric = row.Metric;
        Cand c;
        c.adapter.ifName = pupnpIfName(a);
        c.adapter.ip = ip;
        c.adapter.label = QStringLiteral("%1 · %2").arg(friendly, c.adapter.ip);
        c.metric = metric;
        c.hasGateway = hasGateway;
        cands.append(c);
    }
    QList<const Cand*> routed;
    for (const Cand& c : std::as_const(cands))
        if (c.hasGateway)
            routed.append(&c);
    if (routed.isEmpty())
        return std::nullopt;
    std::sort(routed.begin(), routed.end(), [](const Cand* a, const Cand* b) {
        return a->metric < b->metric;
    });
    return routed.first()->adapter;
}

#else // !Q_OS_WIN — QNetworkInterface fallback (same rules, best effort)

std::optional<DlnaNetAdapter> adapterFrom(const QNetworkInterface& ni, const QNetworkAddressEntry& e)
{
    DlnaNetAdapter a;
    a.ifName = ni.name();
    a.ip = e.ip().toString();
    a.label = QStringLiteral("%1 · %2").arg(ni.humanReadableName(), a.ip);
    return a;
}

QList<DlnaNetAdapter> enumerateGeneric()
{
    QList<DlnaNetAdapter> out;
    for (const QNetworkInterface& ni : QNetworkInterface::allInterfaces()) {
        if (!(ni.flags() & QNetworkInterface::IsUp))
            continue;
        for (const QNetworkAddressEntry& e : ni.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                out.append(*adapterFrom(ni, e));
                break;
            }
    }
    return out;
}

std::optional<DlnaNetAdapter> pickHomeGeneric()
{
    for (const QNetworkInterface& ni : QNetworkInterface::allInterfaces()) {
        if (!(ni.flags() & QNetworkInterface::IsUp) || ni.flags() & QNetworkInterface::IsLoopBack)
            continue;
        if (ni.type() == QNetworkInterface::Virtual || ni.type() == QNetworkInterface::Unknown)
            continue;
        for (const QNetworkAddressEntry& e : ni.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            if (!DlnaNetwork::isPrivateIpv4(e.ip().toString()))
                continue;
            return adapterFrom(ni, e);
        }
    }
    return std::nullopt;
}

#endif

} // namespace

QList<DlnaNetAdapter> DlnaNetwork::enumerateAdapters()
{
#ifdef Q_OS_WIN
    return enumerateWin();
#else
    return enumerateGeneric();
#endif
}

std::optional<DlnaNetAdapter> DlnaNetwork::pickHomeAdapter()
{
#ifdef Q_OS_WIN
    return pickHomeWin();
#else
    return pickHomeGeneric();
#endif
}

bool DlnaNetwork::isPrivateIpv4(const QString& dottedQuad)
{
    const QStringList parts = dottedQuad.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return false;
    quint32 h = 0;
    for (const QString& part : parts) {
        bool ok = false;
        const uint octet = part.toUInt(&ok, 10);
        if (!ok || octet > 255 || part.size() > 3)
            return false;
        h = (h << 8) | octet;
    }
    return (h >> 24) == 10 || (h >> 20) == ((172u << 4) | 1) || (h >> 16) == ((192u << 8) | 168);
}

std::optional<DlnaNetAdapter> DlnaNetwork::loopbackAdapter()
{
    const QList<DlnaNetAdapter> all = enumerateAdapters();
    for (const DlnaNetAdapter& a : all)
        if (a.ip == QLatin1String("127.0.0.1"))
            return a;
    return std::nullopt;
}
