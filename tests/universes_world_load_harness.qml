// Lazy-Loader gate for UniversesWorld (the fourth world) — boot smokes are blind to
// creation-time errors behind an inactive Loader, so this instantiates the world for
// real. (backdrop stays null under the harness — Glass is null-safe; creation succeeding
// is exactly what READY proves.) Also pins the roster contract: the four seeded
// installed universes + every curated door with a real page = 18 ledger bars.
import QtQuick
import QtQuick.Window
import "../qml/Universes.js" as Universes

Window {
    visible: true
    width: 300; height: 200
    Component.onCompleted: {
        const four = [
            { extensionId: "com.colosseum.universe.onepiece", name: "One Piece",
              banner: "", logo: "" },
            { extensionId: "com.colosseum.universe.cosmere", name: "Cosmere",
              banner: "", logo: "" },
            { extensionId: "com.colosseum.universe.dcau", name: "DC Animated Universe",
              banner: "", logo: "" },
            { extensionId: "com.colosseum.universe.starwars", name: "Star Wars",
              banner: "", logo: "" } ]
        const roster = Universes.worldRoster(four)
        console.log("[diag] worldRoster length = " + roster.length)
        if (roster.length !== 18) {
            console.log("[diag] HARNESS FAIL — expected 18 roster bars, got " + roster.length)
            Qt.exit(2)
        }
        // the list is alphabetical (articles aside), and the carousel holds the art pages
        if (roster[0].name !== "Attack on Titan" || roster[roster.length - 1].name !== "The Witcher") {
            console.log("[diag] HARNESS FAIL — roster must be alphabetical: " + roster[0].name + " … " + roster[roster.length - 1].name)
            Qt.exit(2)
        }
        // the filter offers only what some universe holds, and narrows the list by it
        const facets = Universes.rosterFacets(roster).map(function(f) { return f.key }).join(",")
        if (facets !== "books,manga,anime,films,series,comics") {
            console.log("[diag] HARNESS FAIL — facets: " + facets)
            Qt.exit(2)
        }
        const manga = Universes.filterRoster(roster, "manga").map(function(e) { return e.name }).join("|")
        if (manga !== "Attack on Titan|Naruto|One Piece") {
            console.log("[diag] HARNESS FAIL — manga filter: " + manga)
            Qt.exit(2)
        }
        if (Universes.filterRoster(roster, "").length !== 18) {
            console.log("[diag] HARNESS FAIL — the empty filter must show everything")
            Qt.exit(2)
        }
        if (Universes.tributes(four).length !== 4) {
            console.log("[diag] HARNESS FAIL — the four installed universes are the Tributes")
            Qt.exit(2)
        }
    }
    Loader {
        id: l
        source: "../qml/UniversesWorld.qml"
        onStatusChanged: {
            if (status === Loader.Error) console.log("[diag] LOADER ERROR — see warnings above")
            if (status === Loader.Ready) console.log("[diag] LOADER READY — world created fine")
        }
    }
    Timer { interval: 3000; running: true; onTriggered: Qt.quit() }
}
