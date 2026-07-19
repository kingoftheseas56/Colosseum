const fs = require("fs"), path = require("path")
const src = fs.readFileSync(path.join(__dirname, "..", "qml", "CollectionBackfill.js"), "utf8")
    .replace(/^\.pragma library\s*/m, "").replace(/^\.import .*$/gm, "")
const lib = {}
new Function("exports", src + "\nexports.seriesBaseId=seriesBaseId;exports.entryForTheatreSeries=entryForTheatreSeries;exports.entryForTankobanSeries=entryForTankobanSeries;exports.entryForBook=entryForBook;exports.titleKey=titleKey;")(lib)
function assert(c, m) { if (!c) { console.error("FAIL: " + m); process.exit(1) } }

assert(lib.seriesBaseId("tt123:1:5") === "tt123", "strip tt episode")
assert(lib.seriesBaseId("mal:9:3:4") === "mal:9", "strip anime episode")
assert(lib.seriesBaseId("tt777") === "tt777", "movie id unchanged")

var ep = lib.entryForTheatreSeries({ kind: "episode", key: "show:one piece", title: "One Piece", art: "http://p.jpg" },
                                   [{ id: "tt0388629:13:585" }])
assert(ep.id === "tt0388629" && ep.type === "series" && ep.cover === "http://p.jpg", "theatre episode -> series base id + poster")
var mv = lib.entryForTheatreSeries({ kind: "movie", key: "movie:tt777", title: "A Movie", art: "http://m.jpg" }, [{ id: "tt777" }])
assert(mv.id === "tt777" && mv.type === "movie", "theatre movie -> id")

var mg = lib.entryForTankobanSeries({ kind: "manga", key: "manga:WC123", title: "Berserk", art: "file:///p.png" })
assert(mg.id === "Berserk" && mg.type === "manga", "manga -> title id")
var cm = lib.entryForTankobanSeries({ kind: "comic", key: "comic:gc:batman-slug", title: "Batman", art: "" })
assert(cm.id === "gc:batman-slug" && cm.type === "comic", "comic -> prefixed seriesId")

var bk = lib.entryForBook({ title: "Joe Country", author: "Mick Herron" }, "joe country|mick herron")
assert(bk.id === "joe country|mick herron" && bk.type === "book" && bk.payload.book.author === "Mick Herron", "book -> pairKey id + payload")
assert(lib.entryForTheatreSeries(null) === null && lib.entryForTankobanSeries({kind:"manga",title:""}) === null && lib.entryForBook(null, "x") === null, "null/empty safe")
assert(lib.titleKey("  The   Hobbit ") === "the hobbit", "titleKey normalizes")
assert(lib.titleKey("Joe Country") === lib.titleKey("joe country"), "titleKey case-insensitive")
console.log("CollectionBackfill contract passed.")
