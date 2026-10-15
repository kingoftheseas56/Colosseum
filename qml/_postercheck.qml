// THROWAWAY verification harness — Continue poster pipes.
//   1. ContinueCovers ladder: AniList (403-disabled) must fall through to Kitsu and return art.
//   2. ComicsApi.posterFor verified matching: Avatar gets an Avatar cover, a nonsense
//      title gets "" (never a fabricated first-hit poster — the Immortal Hulk bug).
//   native/build-msvc/colosseum.exe qml/_postercheck.qml
import QtQuick
import QtQuick.Window
import "ContinueCovers.js" as Covers
import "ComicsApi.js" as Api

Window {
    id: win
    width: 640; height: 160; visible: true; color: "#05060a"; title: "postercheck"

    property int pending: 3
    property int failures: 0

    function check(name, ok, detail) {
        console.log((ok ? "[postercheck] PASS " : "[postercheck] FAIL ") + name + " — " + detail)
        if (!ok) failures++
        if (--pending === 0)
            console.log(failures === 0 ? "[postercheck] ALL GREEN" : "[postercheck] " + failures + " FAILURES")
    }

    Component.onCompleted: {
        Covers.fetch("One Piece", function(u) {
            win.check("manga fallback cover", u.length > 0, "One Piece → " + (u || "(none)"))
        })
        Api.posterFor("Avatar - The Last Airbender comic", function(u) {
            win.check("verified western poster", u.length > 0, "Avatar → " + (u || "(none)"))
        })
        Api.posterFor("Zxqvw Blorptastic Saga comic", function(u) {
            win.check("no fabricated poster", u.length === 0, "nonsense → " + (u || "(honest empty)"))
        })
    }

    Timer { interval: 40000; running: true
        onTriggered: { console.log("[postercheck] TIMEOUT with", win.pending, "pending"); win.pending = 0 } }
}
