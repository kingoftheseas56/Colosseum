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
    // the other eight are the curated `shelves` generated into Universes.js (Arc 58-E);
    // One Piece carries its own explicit 56-work shelves since the 2026-10-05 catalogue correction
    readonly property var expected: ({
        "Dragon Ball": "Manga:8(2+6) | Anime:7(7) | Films:25(4+17+4)",
        "One Piece": "Manga:15(2+13) | Series:5(4+1) | Films:15(15) | Specials:17(17) | Novels:4(4)",
        "Naruto": "Manga:4(3+1) | Anime:3(3) | Films:11(3+7+1) | Novels:11(11)",
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
            const series = u.shelves[1].runs
            if (!(series[1].items[0].live === true)) fail("One Piece live-action mark lost")
            if (!(series[0].items[3].upcoming === true)) fail("One Piece upcoming mark lost")
            if (u.category !== "onepiece") fail("explicit shelves keep the universe's own category")
            const manga = u.shelves[0].runs[0].items
            if (!(manga[0].title === "One Piece" && manga[0].mal === 13)) fail("the first manga is One Piece with mal 13")
            const color = manga[1]
            if (!(color && color.provider === "tankoban" && color.malId === "13"
                  && color.seriesId === "mal:13:color"))
                fail("the colour edition must carry its Tankoban profile")
            if (color && color.mal) fail("the colour edition must not borrow the base series' mal")
            // no work appears twice across the shelves, and every work is resolved
            const seen = {}
            u.shelves.forEach(function(s) { s.runs.forEach(function(r) { r.items.forEach(function(w) {
                const k = w.medium + ":" + String(w.id || w.apple || w.al || w.title)
                if (seen[k]) fail("One Piece work appears twice: " + w.title + " (" + seen[k] + " and " + s.title + ")")
                seen[k] = s.title
                if (w.resolved !== true) fail("One Piece unresolved work: " + w.title)
                if (s.kind === "manga" && w.provider === undefined && !(w.mal > 0 && w.al > 0 && w.cover))
                    fail("an ordinary manga lost its pins or cover: " + w.title)
            }) }) })
            // the three Episode Of specials live under Specials only — never also under Films
            const filmIds = u.shelves[2].runs[0].items.map(function(w) { return w.id })
            const specialIds = u.shelves[3].runs[0].items.map(function(w) { return w.id })
            ;["tt3354344", "tt3354352", "tt5098548"].forEach(function(tt) {
                if (filmIds.indexOf(tt) >= 0) fail("Episode Of special " + tt + " must not sit under Films")
                if (specialIds.indexOf(tt) < 0) fail("Episode Of special " + tt + " missing from Specials")
            })
        })
        Saga.loadShelves("Dragon Ball", function(u) {
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
