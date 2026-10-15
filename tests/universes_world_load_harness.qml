// Lazy-Loader gate for UniversesWorld (the fourth world) — boot smokes are blind to
// creation-time errors behind an inactive Loader, so this instantiates the world for
// real. (backdrop stays null under the harness — Glass is null-safe; creation succeeding
// is exactly what READY proves.) Also pins the roster contract: the four seeded
// installed universes + every curated door with a real page (Dragon Ball's story grid
// joined 2026-10-04; Marvel and Weekly Shonen Jump joined 2026-10-05 with the shelves page; Cosmere left the list 2026-10-05 — a Tribute with no standard page lives in the carousel) = 20 banners.
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
        if (roster.length !== 20) {
            console.log("[diag] HARNESS FAIL — expected 20 roster banners, got " + roster.length)
            Qt.exit(2)
        }
        // the list is alphabetical (articles aside), and the carousel holds the art pages
        if (roster[0].name !== "Attack on Titan" || roster[roster.length - 1].name !== "The Witcher") {
            console.log("[diag] HARNESS FAIL — roster must be alphabetical: " + roster[0].name + " … " + roster[roster.length - 1].name)
            Qt.exit(2)
        }
        // the filter CLASSIFIES: a universe sits under what it primarily is (one label, or two
        // for a direct adaptation), never under everything it happens to hold
        const facets = Universes.rosterFacets(roster).map(function(f) { return f.key }).join(",")
        if (facets !== "books,manga,anime,films,series") {
            console.log("[diag] HARNESS FAIL — facets: " + facets)
            Qt.exit(2)
        }
        const manga = Universes.filterRoster(roster, "manga").map(function(e) { return e.name }).join("|")
        if (manga !== "Attack on Titan|Dragon Ball|Naruto|One Piece|Weekly Shonen Jump") {
            console.log("[diag] HARNESS FAIL — manga filter: " + manga)
            Qt.exit(2)
        }
        const expected = {
            books: "Dune|Harry Potter|Lord of the Rings|Percy Jackson|Sherlock Holmes|A Song of Ice and Fire|The Witcher",
            anime: "Attack on Titan|Dragon Ball|Naruto|One Piece|Studio Ghibli",
            films: "Dune|Harry Potter|James Bond|Jurassic Park|Lord of the Rings|Marvel Cinematic Universe|Star Trek|Star Wars|Studio Ghibli",
            series: "Avatar: The Last Airbender|DC Animated Universe|Marvel Cinematic Universe|Percy Jackson|Sherlock Holmes|A Song of Ice and Fire|Star Trek|Star Wars|The Witcher" }
        for (const key in expected) {
            const got = Universes.filterRoster(roster, key).map(function(e) { return e.name }).join("|")
            if (got !== expected[key]) {
                console.log("[diag] HARNESS FAIL — " + key + " filter: " + got)
                Qt.exit(2)
            }
        }
        // every universe is classified, with one or two labels
        for (let p = 0; p < roster.length; ++p) {
            const labels = roster[p].primary
            if (!labels || labels.length < 1 || labels.length > 2) {
                console.log("[diag] HARNESS FAIL — " + roster[p].name + " needs one or two primary labels")
                Qt.exit(2)
            }
        }
        if (Universes.filterRoster(roster, "").length !== 20) {
            console.log("[diag] HARNESS FAIL — the empty filter must show everything")
            Qt.exit(2)
        }
        // every banner opens a standard page and wears its identity mark and its About copy
        for (let i = 0; i < roster.length; ++i) {
            const e = roster[i]
            if (!(e.page === "shelves" || e.page === "saga") || !e.logo || !e.logoShape || !e.blurb) {
                console.log("[diag] HARNESS FAIL — incomplete banner entry: " + e.name + " page=" + e.page + " logo=" + e.logo)
                Qt.exit(2)
            }
        }
        if (roster.some(function(e) { return e.name === "Cosmere" })) {
            console.log("[diag] HARNESS FAIL — Cosmere has no standard page; it is a Tribute only")
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
