// Headless behavioral harness for the pure ContinueSeeAll.js chip logic (see-all page).
// Grep contracts only prove strings exist; this proves sorts/filters WORK. Driven by
// qml.exe; verdict rides the exit code (console won't flush; a throw in onCompleted
// would HANG qml.exe) — so everything wraps in try/catch → Qt.exit.
import QtQuick
import "../qml/ContinueSeeAll.js" as SeeAll

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
        // fixture: mixed kinds, out-of-order timestamps, one watched, one title-less,
        // one timestamp-less (missing-field entries must not throw)
        var items = [
            { id: "a", kind: "video", title: "Zoro Falls",   updatedAt: 500, watched: false },
            { id: "b", kind: "manga", title: "berserk",      updatedAt: 900 },
            { id: "c", kind: "video", title: "Alpha House",  updatedAt: 700, watched: true },
            { id: "d", kind: "comic", caption: "Moon Knight", updatedAt: 800 },
            { id: "e", kind: "book",  title: "Dune" }
        ]

        function ids(a) { return a.map(function(e) { return e.id }).join(",") }

        // --- recent (default): updatedAt desc, missing updatedAt sinks last ---
        if (ids(SeeAll.apply(items, "recent", "")) !== "b,d,c,a,e")
            throw new Error("recent order wrong: " + ids(SeeAll.apply(items, "recent", "")))

        // --- az / za: display label (title falling back to caption), case-insensitive ---
        if (ids(SeeAll.apply(items, "az", "")) !== "c,b,e,d,a")
            throw new Error("az order wrong: " + ids(SeeAll.apply(items, "az", "")))
        if (ids(SeeAll.apply(items, "za", "")) !== "a,d,e,b,c")
            throw new Error("za order wrong: " + ids(SeeAll.apply(items, "za", "")))

        // --- watched / unwatched: filters, recency order kept ---
        if (ids(SeeAll.apply(items, "watched", "")) !== "c")
            throw new Error("watched filter wrong: " + ids(SeeAll.apply(items, "watched", "")))
        if (ids(SeeAll.apply(items, "unwatched", "")) !== "b,d,a,e")
            throw new Error("unwatched filter wrong: " + ids(SeeAll.apply(items, "unwatched", "")))

        // --- medium filter (home chips) composes with sort ---
        if (ids(SeeAll.apply(items, "recent", "video")) !== "c,a")
            throw new Error("video medium filter wrong")
        if (ids(SeeAll.apply(items, "az", "video")) !== "c,a")
            throw new Error("video+az compose wrong")
        if (ids(SeeAll.apply(items, "recent", "book")) !== "e")
            throw new Error("book medium filter wrong")

        // --- tankoban merge: manga+comic concat comes back interleaved by recency ---
        var merged = SeeAll.apply(
            [{ id: "m1", kind: "manga", title: "M1", updatedAt: 100 }].concat(
            [{ id: "c1", kind: "comic", title: "C1", updatedAt: 200 }]), "recent", "")
        if (ids(merged) !== "c1,m1")
            throw new Error("tankoban merge order wrong: " + ids(merged))

        // --- empties: never throw, always return an array ---
        if (SeeAll.apply([], "recent", "").length !== 0) throw new Error("empty input must stay empty")
        if (SeeAll.apply(null, "az", "").length !== 0) throw new Error("null input must yield []")
        if (SeeAll.apply(items, "watched", "book").length !== 0) throw new Error("watched+book should be empty")

        // --- input must NOT be mutated (page re-applies chips on the same raw list) ---
        SeeAll.apply(items, "az", "")
        if (items[0].id !== "a") throw new Error("apply() mutated its input")
    }
}
