#pragma once
// DlnaServerCore — the thread-boundary object between DlnaServer (GUI thread) and the pupnp
// worker threads (Arc 71). Everything here is written BEFORE the device is registered (or
// AFTER UpnpFinish), and read freely from SOAP/GENA/vdir callbacks. The only mutable pieces
// are the two atomics below, which live media reads watch so a stopped/restarted server aborts
// them: `serving` (cleared first thing in teardown) and revokeEpoch (bumped on stop/restart).

#include "DlnaSnapshot.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

struct DlnaServerCore {
    using SnapshotProvider = std::function<std::shared_ptr<const DlnaSnapshot>()>;

    SnapshotProvider provider;             // thread-safe snapshot access; never null once set
    std::atomic<uint64_t> revokeEpoch{0};  // bumped on stop/restart; in-flight reads abort
    std::atomic<bool> serving{false};      // vdir callbacks answer only while true
    std::atomic<int> deviceHandle{-1};
    QByteArray udn;                        // "uuid:..." (latin1)
    QString baseUrl;                       // "http://ip:port", set before registration
    QString friendlyName;
    QString modelNumber;
    QByteArray icon48;                     // PNG bytes from :/dlna/icons/
    QByteArray icon120;
    QByteArray cdScpd;                     // ContentDirectory:1 SCPD, served from memory
    QByteArray cmScpd;                     // ConnectionManager:1 SCPD
};
