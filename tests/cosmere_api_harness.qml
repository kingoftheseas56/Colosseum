// Headless behavior gate for the Cognitive Atlas resolver. The pure snapshot seam is the
// same code used after live Apple Books lookups: declared order wins, unresolved titles
// disappear, and rendered slots are full Biblio objects rather than search strings.
import QtQuick
import "../qml/CosmereApi.js" as Cosmere

QtObject {
    Component.onCompleted: {
        try { runChecks(); Qt.exit(0) }
        catch (e) { console.log("HARNESS FAIL: " + e.message); Qt.exit(2) }
    }

    function runChecks() {
        var cfg = {
            name: "Test Cosmere",
            blurb: "Connected worlds.",
            banner: "banner",
            cosmereStarters: [
                { label: "Mistborn", query: "mistborn" },
                { label: "Stormlight", query: "stormlight" },
                { label: "Missing", query: "missing" }
            ],
            cosmereWorlds: [
                { name: "Scadrial", books: [
                    { label: "Mistborn", query: "mistborn" },
                    { label: "Wax and Wayne", query: "alloy" }
                ] },
                { name: "Roshar", books: [
                    { label: "Stormlight", query: "stormlight" }
                ] }
            ]
        }
        var resolved = {
            "stormlight": { id: 30, title: "The Way of Kings", author: "Brandon Sanderson" },
            "mistborn": { id: 10, title: "Mistborn", author: "Brandon Sanderson" },
            "alloy": { id: 20, title: "The Alloy of Law", author: "Brandon Sanderson" }
        }
        var atlas = Cosmere.snapshot(cfg, resolved)
        if (atlas.starters.length !== 2) throw new Error("unresolved starter must stay absent")
        if (atlas.starters[0].book.title !== "Mistborn") throw new Error("starter order drifted")
        if (atlas.starters[1].book.title !== "The Way of Kings") throw new Error("starter slot two wrong")
        if (typeof atlas.starters[0].book !== "object" || !atlas.starters[0].book.id)
            throw new Error("starter must carry a full Biblio object")
        if (atlas.worlds.length !== 2 || atlas.worlds[0].name !== "Scadrial")
            throw new Error("world order drifted")
        if (atlas.worlds[0].books[1].book.title !== "The Alloy of Law")
            throw new Error("book order drifted")
        if (atlas.worlds[1].books[0].book.title !== "The Way of Kings")
            throw new Error("shared resolved book missing")
    }
}

