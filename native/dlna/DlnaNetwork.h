#pragma once
// DlnaNetwork — picks the ONE adapter the DLNA server binds (Arc 71). The rules come from the
// build spec: the active private IPv4 adapter carrying the default route, Ethernet or Wi-Fi
// only (IfType 6 / 71), skipping loopback, VPN/tunnel and virtual adapters (Hyper-V vEthernet,
// WSL, VirtualBox, VMware, TAP/TUN, Tailscale, ZeroTier, WireGuard, Npcap, AnyConnect,
// GlobalProtect, ...). `ifName` is exactly the string pupnp's UpnpInit2 matches on Windows
// (the adapter GUID, because pupnp is built with UPNP_ADAPTER_UUID_NAME); `ip` is the address
// pupnp will bind (the adapter's last IPv4). Tests may instead force the loopback
// pseudo-interface. QtCore-only surface so both the app and the tests use one truth.

#include <QList>
#include <QString>

#include <optional>

struct DlnaNetAdapter {
    QString ifName;  // pupnp UpnpInit2 argument (Windows: the adapter GUID)
    QString ip;      // the IPv4 address pupnp will bind
    QString label;   // UI text, e.g. "Wi-Fi · 192.168.1.20" (friendly name, may be Unicode)
};

class DlnaNetwork
{
public:
    // Every adapter that is Up with an IPv4 address, loopback included. Diagnostic + test use.
    static QList<DlnaNetAdapter> enumerateAdapters();

    // The one home adapter per the network rules; nullopt when there is none (the server must
    // then refuse to start with "No home network found").
    static std::optional<DlnaNetAdapter> pickHomeAdapter();

    // RFC1918 (10/8, 172.16/12, 192.168/16) dotted-quad check.
    static bool isPrivateIpv4(const QString& dottedQuad);

    // The loopback pseudo-interface (127.0.0.1) — the test lane; never the production pick.
    static std::optional<DlnaNetAdapter> loopbackAdapter();
};
