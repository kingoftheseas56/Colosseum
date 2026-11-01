#pragma once
// DlnaMediaFiles — pupnp's virtual directories for the DLNA server (Arc 71). One static
// callback set (the 22.x API: per-callback setters, global, one cookie) routes five prefixes:
//   /m/<itemId>.<ext>   media bytes (Range/HEAD handled by pupnp; we tell the truth about
//                       length, mtime and content type, and stamp the DLNA headers)
//   /art/<itemId>       an item's local cover image
//   /sub/<itemId>.srt   a same-basename subtitle discovered beside a video
//   /icons/colosseum-48.png / colosseum-120.png   device icons from the qrc
//   /scpd/cd.xml /scpd/cm.xml                     service descriptions from memory
// Every lookup runs against the CURRENT snapshot. GetInfo checks size+mtime against the
// snapshot item BEFORE any header is sent (a changed file is a 404, never a truncated 200),
// advertises the snapshot's length, and passes an identity token to Open through the request
// cookie; Open refuses if the file no longer matches what the headers promised. Read aborts the
// moment the item leaves the snapshot, `serving` drops or the revoke epoch moves (removed item /
// sharing stopped) — the connection then ends early.
//
// Callbacks run on pupnp worker threads: no Qt objects, no GUI-thread hops. The only state
// touched is DlnaServerCore (see DlnaServerCore.h) and the per-file handle.

#include "DlnaServerCore.h"

class DlnaMediaFiles
{
public:
    // Set the six callbacks and add the five virtual dirs (cookie = core). Call after
    // UpnpInit2 — web_server_init resets the callbacks, so ordering matters (spike-proven).
    static void install(DlnaServerCore* core);
    // There is deliberately no uninstall: UpnpFinish removes the virtual dirs itself after its
    // thread pools stop, whereas UpnpRemoveVirtualDir would edit the list under pupnp's
    // unlocked walk in the webserver threads. DlnaServerCore::serving gates the callbacks first.

    // Number of vdir handles currently open (a stalled client holds one until its worker is
    // freed). Diagnostic + tests.
    static int activeHandles();
};
