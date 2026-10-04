// Headless harness for the saga GRID (Arc 58): SagaApi.sagaGrid turns the curated pairing
// into rows, and gridMove walks it. Verdict rides the exit code.
import QtQuick
import "../qml/SagaApi.js" as Saga
import "../qml/Universes.js" as UDB

QtObject {
    Component.onCompleted: {
        try { runChecks(); Qt.exit(0) }
        catch (e) { console.log("HARNESS FAIL: " + e.message); Qt.exit(2) }
    }
    function need(ok, message) { if (!ok) throw new Error(message) }
    // every canon slot resolved: the grid's shape depends on curation alone
    function resolved(name) {
        var c = UDB.configFor(name)
        function slots(list, medium) {
            return (list || []).map(function(e, i) {
                return { title: e && e.t !== undefined ? e.t : e, canonIndex: i, medium: medium,
                         type: medium === "series" ? "series" : "movie", id: medium + i, resolved: true }
            })
        }
        return { cfg: c, uni: { books: slots(c.novels, "book"), films: slots(c.films, "film"), shows: slots(c.shows, "series") } }
    }
    function grid(name) { var r = resolved(name); return Saga.sagaGrid(r.uni, r.cfg) }
    function labels(g) { return g.rows.map(function(r) { return r.label }).join(",") }
    function runChecks() {
        // Harry Potter: seven stories, eight slots — the last book sits over its two films
        var hp = grid("Harry Potter")
        need(hp.stories.length === 7 && hp.cols === 8, "HP must be 7 stories over 8 columns, got " + hp.stories.length + "/" + hp.cols)
        need(labels(hp) === "BOOKS,FILMS", "HP rows: " + labels(hp))
        need(hp.rows[0].cells[6].form === "center" && hp.rows[0].cells[6].span === 2, "Deathly Hallows book must centre over two films")
        need(hp.rows[1].cells.length === 8, "HP must place eight films")
        need(hp.blocks.map(function(b) { return b.title }).join("|") === "Hogwarts Library|Fantastic Beasts|Harry Potter on television",
             "HP blocks: " + hp.blocks.map(function(b) { return b.title }).join("|"))

        // A Song of Ice and Fire: one show stretched across all five books
        var asoiaf = grid("A Song of Ice and Fire")
        need(labels(asoiaf) === "BOOKS,SERIES", "ASOIAF rows: " + labels(asoiaf))
        var got = asoiaf.rows[1].cells[0]
        need(asoiaf.rows[1].cells.length === 1 && got.form === "banner" && got.col === 0 && got.span === 5, "Game of Thrones must span five stories")

        // Percy Jackson: a row per form — films AND a series over the same books
        var pj = grid("Percy Jackson")
        need(labels(pj) === "BOOKS,FILMS,SERIES", "Percy Jackson rows: " + labels(pj))
        need(pj.rows[2].cells[0].span === 2, "the series covers two books so far")

        // Jurassic Park: a story with no book keeps an empty book slot
        var jp = grid("Jurassic Park")
        need(jp.stories.length === 3 && jp.rows[0].cells.length === 2 && jp.rows[1].cells.length === 3, "JP III must have a film and no book")

        // Sherlock Holmes: no pairing — nine book stories, adaptations go their own way
        var sh = grid("Sherlock Holmes")
        need(sh.cols === 9 && labels(sh) === "BOOKS", "Sherlock must be nine book columns and no screen row: " + sh.cols + " " + labels(sh))
        need(sh.blocks[0].title === "On screen" && sh.blocks[0].shelves.length === 2, "Sherlock's adaptations must fall into On screen")

        // Dune: unpaired Lynch film is On screen; four books have nothing beneath them
        var dune = grid("Dune")
        need(dune.rows[1].cells.length === 3 && dune.blocks[0].title === "On screen", "Dune pairing wrong")

        // every saga's pairing points at real canon entries
        ;["Harry Potter", "Lord of the Rings", "A Song of Ice and Fire", "Dune", "The Witcher",
          "Sherlock Holmes", "Jurassic Park", "Percy Jackson"].forEach(function(name) {
            var g = grid(name)
            need(g.cols > 0 && g.rows.length > 0, name + " grid is empty")
            g.rows.forEach(function(r) { r.cells.forEach(function(c) {
                need(c.item && c.col >= 0 && c.col + c.span <= g.cols, name + " has a cell outside the grid")
            }) })
        })

        // keyboard: left/right walk the story, up/down swap forms, empty forms are skipped
        var m = Saga.gridMove(hp, 0, 6, 0, 1)
        need(m && m.row === 1 && m.cell === 6, "Down from the last HP book must land on Part 1")
        m = Saga.gridMove(hp, 1, 7, 0, -1)
        need(m && m.row === 0 && m.cell === 6, "Up from Part 2 must return to its book")
        need(Saga.gridMove(hp, 0, 6, 1, 0) === null, "Right at the end of a row must leave the grid")
        m = Saga.gridMove(pj, 0, 1, 0, 1)
        need(m && m.row === 1 && m.cell === 1, "Percy: Down from book two is film two")
        m = Saga.gridMove(pj, 1, 1, 0, 1)
        need(m && m.row === 2 && m.cell === 0, "Percy: Down from film two is the series")
        need(Saga.gridMove(pj, 0, 3, 0, 1) === null, "Percy: book four has nothing beneath it")
        m = Saga.gridMove(asoiaf, 1, 0, 0, -1)
        need(m && m.row === 0 && m.cell === 2, "Up from a spanning show lands on its middle book")
        need(Saga.gridMove(jp, 1, 2, 0, -1) === null, "JP III has no book above it")
    }
}
