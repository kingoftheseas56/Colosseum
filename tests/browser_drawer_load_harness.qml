import QtQuick
import QtQuick.Window

// Headless load gate for the Feature 8 drawer. BrowserDrawer sits behind PlayerPage's
// lazy creation path, so a creation-time QML error (a fractional literal on an int, a
// bad property ref) would ship invisibly — qmllint and the boot smoke both miss it
// (the DownloadsPage 12.5px lesson). Unlike PlayerPage, BrowserDrawer imports no native
// module, so it CAN be instantiated standalone. We hand it a realistic queue + candidates
// so the delegates actually build. Verdict rides the LOADER READY line.
Window {
    visible: true
    width: 400; height: 300

    Loader {
        id: ld
        source: "../qml/BrowserDrawer.qml"
        onStatusChanged: {
            if (status === Loader.Error)
                console.log("[diag] LOADER ERROR — BrowserDrawer")
            if (status === Loader.Ready) {
                // force the delegates to build by opening on real data
                ld.item.queue = [
                    { id: "tt1:1:1", title: "Show - S1E1", season: 1, episode: 1 },
                    { id: "tt1:1:2", title: "Show - S1E2", season: 1, episode: 2 }
                ]
                ld.item.candidates = [
                    { infoHash: "abc", title: "Rip 1080p", sourceName: "Torrentio", quality: "1080p", seeders: 42 },
                    { infoHash: "def", title: "Rip 720p", sourceName: "Torrentio", quality: "720p", seeders: 8 }
                ]
                ld.item.nowId = "tt1:1:1"
                ld.item.mediaTitle = "Show - S1E1"
                ld.item.currentStreamIndex = 0
                ld.item.open = true
                console.log("[diag] LOADER READY — BrowserDrawer")
            }
        }
    }

    Timer { interval: 2500; running: true; onTriggered: Qt.quit() }
}
