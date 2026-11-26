.pragma library
// Book metadata and chapter timestamps for the listening page.

function text(v) { return v === undefined || v === null ? "" : String(v) }

function list(v) {
    if (!v) return []
    var out = [], src = (typeof v === "string") ? [v] : v
    for (var i = 0; i < src.length; i++) {
        var s = text(src[i]).trim()
        if (s.length) out.push(s)
    }
    return out
}

function formatTime(seconds) {
    var s = Math.floor(Number(seconds))
    if (!isFinite(s) || s < 0) s = 0
    var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60
    var pad = function (n) { return (n < 10 ? "0" : "") + n }
    return h > 0 ? h + ":" + pad(m) + ":" + pad(r) : m + ":" + pad(r)
}

// A local file path becomes a file URL; anything already a URL is kept; empty stays empty.
function coverUrl(cover) {
    var c = text(cover)
    if (!c.length || /^(file:|qrc:|https?:|data:)/.test(c)) return c
    if (/^[A-Za-z]:[\\/]/.test(c)) return "file:///" + c.replace(/\\/g, "/")
    return ""
}

// The edition a Vinyl page shows: the book itself. Artists are the authors; narrators ride
// along only when given.
function edition(book) {
    var b = book || ({})
    var authors = list(b.authors)
    return {
        id: text(b.id), title: text(b.title), name: text(b.title),
        artist: authors.join(" \u00b7 "), artists: authors,
        narrators: list(b.narrators), cover: text(b.cover),
        origin: "local"
    }
}
