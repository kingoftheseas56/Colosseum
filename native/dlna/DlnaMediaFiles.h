#pragma once
// DlnaMediaFiles — pupnp's virtual directories for the DLNA server (Arc 71). One static
// callback set (the 22.x API: per-callback setters, global, one cookie) routes five prefixes:
//   /m/<itemId>.<ext>   media bytes (Range/HEAD handled by pupnp; we tell the truth about
//                       length, mtime and content type, and stamp the DLNA headers)
//   /art/<itemId>       an item's local cover image
//   /sub/<itemId>.srt   a same-basename subtitle discovered beside a video
//   /icons/colosseum-48.png / colosseum-120.png   device icons from the qrc
//   /scpd/cd.xml /scpd/cm.xml                     service descriptions from memory
// Every lookup runs against the CURRENT snapshot; Open re-verifies size+mtime against the
// snapshot item; Read aborts the moment the item leaves the snapshot or the server's revoke
// epoch moves (removed item / sharing stopped) — the connection then ends early.
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
    // Remove the virtual dirs (before UpnpFinish). The six global callbacks stay set; they
    // are inert once no vdir matches.
    static void uninstall();
};
