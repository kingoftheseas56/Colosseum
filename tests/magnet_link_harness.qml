// Headless behavioral harness for the pure Magnet.js link builder (grep contracts only
// prove strings exist). Driven by qml.exe; verdict rides the EXIT CODE — Qt.exit(0) pass,
// non-zero fail — because console output is not guaranteed to flush before exit and an
// uncaught onCompleted throw would HANG qml.exe rather than exit non-zero.
import QtQuick
import "../qml/Magnet.js" as Magnet

QtObject {
    Component.onCompleted: {
        try {
            runChecks()
            Qt.exit(0)
        } catch (e) {
            console.log("HARNESS FAIL: " + e.message)
            Qt.exit(2)
        }
    }

    function runChecks() {
        // --- torrent row: magnet with lowercase hash, encoded name, trackers ---
        var link = Magnet.linkFor({ infoHash: "ABCDEF0123456789ABCDEF0123456789ABCDEF01",
                                    filename: "Show S01E02 1080p.mkv", release: "ignored" })
        if (link.indexOf("magnet:?xt=urn:btih:abcdef0123456789abcdef0123456789abcdef01") !== 0)
            throw new Error("magnet must start with lowercase btih, got " + link)
        if (link.indexOf("&dn=Show%20S01E02%201080p.mkv") < 0)
            throw new Error("dn must be the encoded filename, got " + link)
        if ((link.match(/&tr=/g) || []).length < 3)
            throw new Error("magnet must carry the standard tracker set, got " + link)

        // --- filename missing: dn falls back to the release line ---
        var rel = Magnet.linkFor({ infoHash: "abc123", release: "Some Release" })
        if (rel.indexOf("&dn=Some%20Release") < 0)
            throw new Error("dn must fall back to release, got " + rel)

        // --- nameless torrent: still a valid magnet, just no dn ---
        var bare = Magnet.linkFor({ infoHash: "abc123" })
        if (bare.indexOf("magnet:?xt=urn:btih:abc123") !== 0 || bare.indexOf("&dn=") >= 0)
            throw new Error("nameless torrent must omit dn, got " + bare)

        // --- direct-HTTP row (url: routing prefix): copy the URL itself, never a fake magnet ---
        var direct = Magnet.linkFor({ infoHash: "url:https://host/stream.mp4" })
        if (direct !== "https://host/stream.mp4")
            throw new Error("url-prefixed row must return the direct url, got " + direct)

        // --- empty / junk rows: empty string (button caller no-ops) ---
        if (Magnet.linkFor({}) !== "" || Magnet.linkFor(null) !== "")
            throw new Error("row without a hash must return empty string")
    }
}
