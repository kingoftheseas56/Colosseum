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

// Substring blocklist for virtual/tunnel adapter display names (case-insensitive). These are
// the spec's skip list; everything virtual a home machine grows (Hyper-V, WSL, VPN tools)
// names itself loudly, and a DLNA server must never bind any of them.
const char* const kVirtualNameFragments[] = {
    "vethernet", "hyper-v", "wsl", "virtualbox", "vmware", "tap", "tun",
    "tailscale", "zerotier", "wireguard", "npcap", "loopback", "vpn", "meta",
};

bool isVirtualName(const QString& name)
{
    const QString n = name.toLower();
    for (const char* frag : kVirtualNameFragments)
        if (n.contains(QLatin1String(frag)))
            return true;
    return false;
}

bool isPrivateV4(quint32 addrHostOrder)
{
    return (addrHostOrder >> 24) == 10
        || (addrHostOrder >> 20) == ((172u << 4) | 1)
        || (addrHostOrder >> 16) == ((192u << 8) | 168);
}

QString ansiFriendlyName(PIP_ADAPTER_ADDRESSES adapter)
{
    // pupnp's UpnpGetIfInfo matches IfName against the FriendlyName converted with wcstombs
    // (ANSI code page). Produce exactly that string so the two sides always agree.
    char buf[256] = {0};
    WideCharToMultiByte(CP_ACP, 0, adapter->FriendlyName, -1, buf, sizeof(buf), nullptr, nullptr);
    return QString::fromLocal8Bit(buf);
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
        for (auto ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
            const sockaddr* sa = ua->Address.lpSockaddr;
            if (!sa || sa->sa_family != AF_INET)
                continue;
            char ip[64] = {0};
            getnameinfo(sa, ua->Address.iSockaddrLength, ip, sizeof(ip), nullptr, 0,
                        NI_NUMERICHOST);
            DlnaNetAdapter adapter;
            adapter.ifName = ansiFriendlyName(a);
            adapter.ip = QString::fromLatin1(ip);
            adapter.label = QStringLiteral("%1 · %2").arg(QString::fromWCharArray(a->FriendlyName),
                                                          adapter.ip);
            out.append(adapter);
            break; // first IPv4 address per adapter is the one pupnp binds too
        }
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
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL
            || a->IfType == IF_TYPE_PPP)
            continue;
        const QString friendly = QString::fromWCharArray(a->FriendlyName);
        const QString description = a->Description ? QString::fromWCharArray(a->Description)
                                                   : QString();
        if (isVirtualName(friendly) || isVirtualName(description))
            continue;
        const char* ip = nullptr;
        bool privateV4 = false;
        for (auto ua = a->FirstUnicastAddress; ua && !privateV4; ua = ua->Next) {
            const sockaddr* sa = ua->Address.lpSockaddr;
            if (!sa || sa->sa_family != AF_INET)
                continue;
            const quint32 h = ntohl(reinterpret_cast<const sockaddr_in*>(sa)->sin_addr.S_un.S_addr);
            if (!isPrivateV4(h))
                continue; // link-local and public addresses are not "home network" material
            char ipbuf[64] = {0};
            getnameinfo(sa, ua->Address.iSockaddrLength, ipbuf, sizeof(ipbuf), nullptr, 0,
                        NI_NUMERICHOST);
            ip = ipbuf;
            privateV4 = true;
        }
        if (!privateV4)
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
        c.adapter.ifName = ansiFriendlyName(a);
        c.adapter.ip = QString::fromLatin1(ip);
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
            const quint32 h = e.ip().toIPv4Address();
            const bool priv = (h >> 24) == 10 || (h >> 20) == ((172u << 4) | 1)
                              || (h >> 16) == ((192u << 8) | 168);
            if (!priv)
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

std::optional<DlnaNetAdapter> DlnaNetwork::loopbackAdapter()
{
    const QList<DlnaNetAdapter> all = enumerateAdapters();
    for (const DlnaNetAdapter& a : all)
        if (a.ip == QLatin1String("127.0.0.1"))
            return a;
    return std::nullopt;
}
