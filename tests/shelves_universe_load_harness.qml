// Lazy-Loader gate for ShelvesUniversePage — boot smokes are blind to creation-time errors
// behind an inactive Loader, so this instantiates the page for real. (backdrop stays null
// under the harness — Glass is null-safe; creation succeeding is what READY proves.) It also
// pins SagaApi.loadShelves against the pinned canon for all twelve universes on this page:
// which shelves, how many works, how the runs divide them, and that every work is resolved.
import QtQuick
import QtQuick.Window
import "../qml/SagaApi.js" as Saga

Window {
    visible: true
    width: 300; height: 200
    // "shelf: n (run sizes)" per shelf — the anime four are DERIVED from manga/saga/filmEras,
    // the other eight are the curated `shelves` generated into Universes.js (Arc 58-E)
    readonly property var expected: ({
        "Dragon Ball": "Manga:8(2+6) | Anime:7(7) | Films:25(4+17+4)",
        "One Piece": "Manga:8(1+7) | Anime:3(3) | Films:17(12+5)",
        "Naruto": "Manga:4(3+1) | Anime:3(3) | Films:11(3+7+1)",
        "Attack on Titan": "Manga:3(1+2) | Anime:1(1) | Films:7(5+2)",
        "James Bond": "Films:25(6+1+7+2+4+5) | Books:14(14)",
        "Marvel Cinematic Universe": "Films:40(6+6+11+7+6+4) | Series:20(9+6+4+1) | Specials:3(3)",
        "Star Trek": "Series:13(6+7) | Films:14(6+4+3+1)",
        "Star Wars": "Films:11(3+3+3+2) | Series:17(7+10)",
        "DC Animated Universe": "Series:8(2+4+2) | Films:4(4)",
        "Avatar: The Last Airbender": "Series:3(2+1) | Films:2(2) | Books:5(5)",
        "Studio Ghibli": "Films:23(5+7+5+5+1)",
        "Weekly Shonen Jump": "In the magazine now:20(20) | The classics:20(20)"
    })
    function fail(msg) { console.log("[diag] HARNESS FAIL — " + msg); Qt.exit(2) }
    Component.onCompleted: {
        let pushes = 0
        for (const name in expected) {
            Saga.loadShelves(name, function(u) {
                pushes++
                const shape = u.shelves.map(function(s) {
                    const sizes = s.runs.map(function(r) { return r.items.length }).filter(function(n) { return n > 0 })
                    return s.title + ":" + sizes.reduce(function(a, b) { return a + b }, 0) + "(" + sizes.join("+") + ")"
                }).join(" | ")
                if (shape !== expected[name]) fail(name + " shelves: " + shape)
                u.shelves.forEach(function(s) { s.runs.forEach(function(r) { r.items.forEach(function(w) {
                    if (w.resolved !== true || !w.title || !w.key) fail(name + " has an unresolved work: " + w.title)
                    if ((s.kind === "movie" || s.kind === "series") && (!/^tt\d+$/.test(w.id) || w.type !== s.kind || !w.cover))
                        fail(name + " screen work without a Cinemeta identity: " + w.title)
                    if (s.kind === "book" && !(w.apple > 0 && w.id === w.apple)) fail(name + " book without an Apple Books id: " + w.title)
                    if (s.form === "table" && !(w.mal > 0 && w.al > 0 && w.from > 0)) fail(name + " table row without its pins: " + w.title)
                }) }) })
            })
        }
        if (pushes !== 12) fail("loadShelves must push exactly once per universe, got " + pushes)
        Saga.loadShelves("One Piece", function(u) {
            const anime = u.shelves[1].runs[0].items
            if (!(anime[1].live === true && anime[2].upcoming === true)) fail("One Piece live-action / upcoming marks lost")
            if (u.category !== "anime") fail("a derived anime universe must read ANIME")
        })
        Saga.loadShelves("Weekly Shonen Jump", function(u) {
            if (!(u.category === "magazine" && u.shelves[0].form === "table" && u.shelves[0].tab === "Now"
                  && u.shelves[0].runs[0].items[0].title === "One Piece" && u.shelves[0].runs[0].items[0].mal === 13))
                fail("Weekly Shonen Jump table contract")
        })
        console.log("[diag] loadShelves contract holds for 12 universes")
    }
    Loader {
        id: l
        source: "../qml/ShelvesUniversePage.qml"
        onStatusChanged: {
            if (status === Loader.Error) { console.log("[diag] LOADER ERROR — see warnings above"); Qt.exit(2) }
            if (status === Loader.Ready) console.log("[diag] LOADER READY — page created fine")
        }
    }
    Timer { interval: 3000; running: true; onTriggered: Qt.quit() }
}
