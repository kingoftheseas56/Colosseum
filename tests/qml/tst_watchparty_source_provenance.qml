import QtQuick 2.15
import QtTest 1.3
import "../../qml/AddonClient.js" as AddonClient

// Watch Party Slice 2 — source provenance survives the generic extension flattening.
// This deliberately tests AddonClient.js directly. PlayerPage itself is not loaded by
// the generic QuickTest runner because Colosseum.Player is manually registered in main.
TestCase {
    name: "WatchPartySourceProvenance"

    function test_torrent_row_keeps_stable_addon_id() {
        var row = AddonClient.parseStream(
                    {
                        "infoHash": "0123456789abcdef0123456789abcdef01234567",
                        "fileIdx": 3,
                        "title": "Example"
                    },
                    "Example Addon",
                    4,
                    "com.example.streams")

        verify(row !== null)
        compare(row.streamKind, "Torrent")
        compare(row.addonName, "Example Addon")
        compare(row.addonId, "com.example.streams")
        compare(row.infoHash, "0123456789abcdef0123456789abcdef01234567")
        compare(row.fileIdx, 3)
        verify(row.transportUrl === undefined)
    }

    function test_direct_row_keeps_addon_id_but_not_extension_transport_url() {
        var row = AddonClient.parseStream(
                    {
                        "url": "https://media.example/video.mkv",
                        "behaviorHints": {
                            "proxyHeaders": {
                                "Referer": "https://media.example/"
                            }
                        }
                    },
                    "Example HTTP",
                    2,
                    "com.example.http")

        verify(row !== null)
        compare(row.streamKind, "Direct")
        compare(row.addonId, "com.example.http")
        compare(row.url, "https://media.example/video.mkv")
        compare(row.headers.Referer, "https://media.example/")
        verify(row.transportUrl === undefined)
        verify(row.providerId === undefined)
    }
}
