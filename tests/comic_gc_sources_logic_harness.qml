// Headless logic harness for ComicGcSources.groupSources / sizeText.
// Verdict = exit code (offscreen qml.exe); throws are caught -> Qt.exit(1).
import QtQuick
import "../qml/ComicGcSources.js" as Gc

QtObject {
    Component.onCompleted: {
        try {
            var sources = [
                { id: 1, title: "Saga Vol. 12 (TPB)", link: "https://g/1/", date: "2025-05-01", kind: "collection" },
                { id: 2, title: "Saga Vol. 11 (TPB)", link: "https://g/2/", date: "2023-12-21", kind: "collection" },
                { id: 3, title: "Saga Compendium One", link: "https://g/3/", date: "2019-08-01", kind: "bundle" },
                { id: 4, title: "Saga #72",           link: "https://g/4/", date: "2025-01-01", kind: "single", fan_made: true },
                { id: 5, title: "Saga Vol. 1 - 10 Pack", link: "https://g/5/", date: "2022-03-01", kind: "bundle" }
            ]
            // -- no enrichment yet: date DESC within groups, fixed group order --
            var g = Gc.groupSources(sources, null)
            if (g.length !== 3) throw new Error("expected 3 groups, got " + g.length)
            if (g[0].label !== "Multi-volume packs" || g[1].label !== "Collected editions"
                    || g[2].label !== "Single issues")
                throw new Error("group order wrong: " + g.map(function(x){return x.label}).join("|"))
            if (g[0].rows[0].id !== 5 || g[0].rows[1].id !== 3)
                throw new Error("date-desc fallback wrong in packs: " + JSON.stringify(g[0].rows))
            if (g[1].rows[0].id !== 1 || g[1].rows[1].id !== 2)
                throw new Error("date-desc fallback wrong in collections")
            if (g[2].rows[0].fan_made !== true)
                throw new Error("fan_made must pass through")
            if (g[2].rows[0].year !== 2025)
                throw new Error("year must fall back to the baked date's year, got " + g[2].rows[0].year)
            // -- enrichment lands: size DESC wins within each group --
            var enrich = { "3": { cover: "https://c/3.jpg", sizeMB: 4300, year: 2019 },
                           "5": { cover: "https://c/5.jpg", sizeMB: 2100, year: 2022 },
                           "1": { cover: "https://c/1.jpg", sizeMB: 350,  year: 2025 },
                           "2": { cover: "",               sizeMB: 620,  year: 2023 } }
            g = Gc.groupSources(sources, enrich)
            if (g[0].rows[0].id !== 3 || g[0].rows[1].id !== 5)
                throw new Error("size-desc wrong in packs: compendium (4.3GB) must lead")
            if (g[1].rows[0].id !== 2 || g[1].rows[1].id !== 1)
                throw new Error("size-desc wrong in collections: 620MB before 350MB")
            if (g[0].rows[0].cover !== "https://c/3.jpg")
                throw new Error("cover must merge from enrichment")
            // -- unknown sizes sort AFTER known ones (size 0 = date fallback tail) --
            var mixed = Gc.groupSources(sources, { "1": { cover: "", sizeMB: 350, year: 2025 } })
            if (mixed[1].rows[0].id !== 1)
                throw new Error("known size must outrank unknown within the group")
            // -- empty group omitted --
            var only = Gc.groupSources([sources[3]], null)
            if (only.length !== 1 || only[0].key !== "single")
                throw new Error("empty groups must be omitted")
            // -- unknown kind buckets as single (fail-safe, never dropped) --
            var odd = Gc.groupSources([{ id: 9, title: "X", link: "https://g/9/", date: "2020-01-01", kind: "mystery" }], null)
            if (odd.length !== 1 || odd[0].key !== "single" || odd[0].rows[0].id !== 9)
                throw new Error("unknown kind must bucket into singles, never be dropped")
            // -- sizeText formatting --
            if (Gc.sizeText(0) !== "") throw new Error("sizeText(0) must be empty")
            if (Gc.sizeText(350) !== "350 MB") throw new Error("sizeText(350): " + Gc.sizeText(350))
            if (Gc.sizeText(4300) !== "4.2 GB") throw new Error("sizeText(4300): " + Gc.sizeText(4300))
            console.log("GC-SOURCES-LOGIC OK")
            Qt.exit(0)
        } catch (e) {
            console.error("GC-SOURCES-LOGIC FAIL: " + e.message)
            Qt.exit(1)
        }
    }
}
