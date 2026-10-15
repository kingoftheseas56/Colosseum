// TheatreFacts.factRows — pure-logic contract. AF2 discipline: a row with a blank
// value is OMITTED (hide-when-blank), never rendered empty.
const fs = require("fs"), path = require("path")
const src = fs.readFileSync(path.join(__dirname, "..", "qml", "TheatreFacts.js"), "utf8")
    .replace(/^\.pragma library\s*/m, "").replace(/^\.import .*$/gm, "")
const lib = {}
new Function("exports", src + "\nexports.factRows = factRows;")(lib)

function assert(c, m) { if (!c) { console.error("FAIL: " + m); process.exit(1) } }

const meta = {
    director: ["Tetsuro Araki"], writer: ["Hajime Isayama", "Yasuko Kobayashi"],
    country: "Japan", releaseInfo: "2013-", status: ""
}
const rows = lib.factRows(meta, { studio: "WIT Studio", source: "Manga" })
const keys = rows.map(r => r.k)
assert(keys.indexOf("Director") >= 0, "director row present")
assert(keys.indexOf("Studio") >= 0, "anime extras merge in (studio)")
assert(keys.indexOf("Source") >= 0, "anime extras merge in (source)")
assert(keys.indexOf("Network") < 0, "blank network row OMITTED")
assert(keys.indexOf("Status") < 0, "blank status row OMITTED")
assert(rows.filter(r => r.k === "Writers")[0].v === "Hajime Isayama, Yasuko Kobayashi", "arrays join with commas")
assert(rows.filter(r => r.k === "Aired")[0].v === "2013-", "releaseInfo becomes Aired")
assert(lib.factRows(null).length === 0, "null meta yields no rows")
assert(lib.factRows({}, null).length === 0, "empty meta yields no rows")
const many = lib.factRows({ director: ["A", "B", "C", "D", "E"] })
assert(many[0].v === "A, B, C", "name lists cap at 3")
console.log("TheatreFacts contract passed.")
