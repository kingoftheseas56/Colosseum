// THROWAWAY verification harness — volume-shelf data feed. Proves Manga.volumes(title)
// delivers a non-empty, cover-bearing volume list for big licensed titles, and that the
// MangaVolumes.js normalizer stays HONEST when chapter ranges are unknown (no fabricated
// 1-chapter volumes). Run via the native launcher:
//   native/build-msvc/colosseum.exe qml/_volcheck.qml
import QtQuick
import QtQuick.Window
import "MangaVolumes.js" as Vol

Window {
    id: win
    width: 640; height: 200
    visible: true
    color: "#05060a"
    title: "volcheck"

    property var titles: ["One Piece", "Bleach"]
    property int at: 0
    property int failures: 0

    function fire() {
        console.log("[volcheck] fetching volumes for:", titles[at])
        Manga.volumes(titles[at])
    }

    function check(name, ok, detail) {
        console.log((ok ? "[volcheck] PASS " : "[volcheck] FAIL ") + name + " — " + detail)
        if (!ok) failures++
    }

    Connections {
        target: Manga
        function onVolumesResult(d) {
            var raw = d.volumes || []
            var t = win.titles[win.at]
            win.check(t + " raw volumes", raw.length > 0, raw.length + " volumes from engine")
            if (raw.length) {
                win.check(t + " covers", String(raw[0].cover).indexOf("uploads.mangadex.org") !== -1,
                          "first cover = " + raw[0].cover)
                var ranged = raw.filter(function(v) { return String(v.chapterStart).length > 0 })
                console.log("[volcheck] info " + t + ": " + ranged.length + "/" + raw.length
                            + " volumes carry chapter ranges")
                var norm = Vol.fromEngine(raw)
                win.check(t + " normalized", norm.length > 0, norm.length + " after fromEngine()")
                // honesty: with <2 range anchors the normalizer must NOT invent ranges
                if (ranged.length < 2)
                    win.check(t + " honest fallback", norm.every(function(v) { return v.startNum === null }),
                              "rangeless input stayed rangeless")
                // grouping with a fake flat chapter list must not throw and must respect honesty
                var chapters = []
                for (var i = 1; i <= 40; i++) chapters.push({ number: i, name: "Chapter " + i })
                var g = Vol.group(chapters, norm)
                console.log("[volcheck] info " + t + ": group() options = " + g.options.length)
            }
            win.at++
            if (win.at < win.titles.length) win.fire()
            else {
                console.log(win.failures === 0 ? "[volcheck] ALL GREEN" : "[volcheck] " + win.failures + " FAILURES")
                Qt.exit(win.failures === 0 ? 0 : 1)
            }
        }
    }

    Timer {
        interval: 60000; running: true
        onTriggered: { console.log("[volcheck] TIMEOUT — no volumesResult within 60s"); Qt.exit(2) }
    }

    Component.onCompleted: fire()
}
