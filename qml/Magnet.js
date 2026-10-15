.pragma library
// Magnet.js — build the copyable link for one sources-sheet row (spec 2026-07-08).
// Torrent rows become a standard magnet URI (lowercase btih + display name + the open
// tracker set the Stremio ecosystem ships). Direct-HTTP rows (debrid / hosts) ride the
// "url:" infoHash routing prefix — for those the honest link IS the url, so we return it
// as-is: the copy button never fabricates a magnet for something that isn't a torrent.

var TRACKERS = [
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.demonii.com:1337/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://exodus.desync.com:6969/announce",
    "udp://tracker.openbittorrent.com:6969/announce"
];

function linkFor(row) {
    row = row || ({});
    var hash = String(row.infoHash || "");
    if (hash.indexOf("url:") === 0)
        return hash.substring(4);
    if (!hash.length)
        return "";
    var link = "magnet:?xt=urn:btih:" + hash.toLowerCase();
    var name = String(row.filename || row.release || "");
    if (name.length)
        link += "&dn=" + encodeURIComponent(name);
    for (var i = 0; i < TRACKERS.length; i++)
        link += "&tr=" + encodeURIComponent(TRACKERS[i]);
    return link;
}
