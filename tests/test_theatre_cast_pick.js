// Cast-source discriminator: anime (mal:/anilist: ORIGINAL requested id) → AniList
// faces; kitsu:/anidb: have no AniList key → fall to Cinemeta names; tt… → names.
// Keyed off the door we ENTERED by, never resolvedId (anime pivots to tt… post-load).
const fs = require("fs"), path = require("path")
const src = fs.readFileSync(path.join(__dirname, "..", "qml", "TheatreApi.js"), "utf8")
    .replace(/^\.pragma library\s*/m, "").replace(/^\.import .*$/gm, "")
const lib = {}
new Function("exports", src + "\nexports.animeIdFor = animeIdFor;")(lib)
function assert(c, m) { if (!c) { console.error("FAIL: " + m); process.exit(1) } }
assert(JSON.stringify(lib.animeIdFor("mal:16498")) === JSON.stringify({ site: "mal", id: 16498 }), "mal id parses")
assert(JSON.stringify(lib.animeIdFor("anilist:16498")) === JSON.stringify({ site: "anilist", id: 16498 }), "anilist id parses")
assert(lib.animeIdFor("kitsu:7442") === null, "kitsu has no AniList key -> names fallback")
assert(lib.animeIdFor("anidb:9541") === null, "anidb -> names fallback")
assert(lib.animeIdFor("tt0388629") === null, "live-action -> names")
assert(lib.animeIdFor("") === null && lib.animeIdFor(null) === null, "blank-safe")
console.log("Cast-pick contract passed.")
