// Genre shelves for comics (2026-07-15): ComicsDb summarizes the catalog's
// `genres` field into mosaic shelves (the JS face of the ComicsCatalog
// curatedGenreShelves seam). The old Top-Comics catalog page that filtered by
// one shelf was torn down 2026-07-18; this probe keeps the shelf-summary check.
import QtQuick
import "../qml/ComicsDb.js" as ComicsDb

Item {
    Component.onCompleted: {
        try {
            var ok = ComicsDb.setData({ series: [
                { rank: 1, title: "Batman", locg_id: "gcd-1", publisher: "DC", cover: "c1",
                  genres: ["Superhero"], editions: [] },
                { rank: 2, title: "Saga", locg_id: "gcd-2", publisher: "Image", cover: "c2",
                  genres: ["Science Fiction", "Fantasy"], editions: [] },
                { rank: 3, title: "The Walking Dead", locg_id: "gcd-3", publisher: "Image", cover: "c3",
                  genres: ["Horror"], editions: [] },
                { rank: 4, title: "Fables", locg_id: "versedb-4", publisher: "DC", cover: "c4",
                  genres: ["Fantasy"], editions: [] },
                { rank: 5, title: "Unshelved", locg_id: "gcd-5", publisher: "", cover: "",
                  genres: [], editions: [] }
            ] })
            if (!ok) throw new Error("ingest failed")

            // ranked rows must carry genres through to the UI layer
            var rows = ComicsDb.rankedSeries()
            if (!rows[0].genres || rows[0].genres[0] !== "Superhero")
                throw new Error("rankedSeries dropped genres")

            // shelves: name+count+covers, biggest first, unshelved series excluded
            var shelves = ComicsDb.genreShelves()
            if (shelves.length !== 4) throw new Error("expected 4 shelves, got " + shelves.length)
            if (shelves[0].name !== "Fantasy" || shelves[0].count !== 2)
                throw new Error("Fantasy(2) should lead, got " + shelves[0].name + "(" + shelves[0].count + ")")
            if (shelves[0].covers.indexOf("c2") < 0 || shelves[0].covers.indexOf("c4") < 0)
                throw new Error("shelf covers missing")

            console.log("COMIC_GENRE_SHELVES_OK")
            Qt.exit(0)
        } catch (e) { console.error("COMIC_GENRE_SHELVES_FAIL", e); Qt.exit(1) }
    }
}
