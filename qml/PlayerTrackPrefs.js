.pragma library

var MAX_PREFS = 200

// The only fields a per-show record persists. An allowlist keeps the store bounded and
// self-documenting: a stray patch key from a future caller can never bloat a record.
//   audioLang / subtitleLang   normalized language codes of the manual pick
//   audioTrackTitle / subtitleTrackTitle   human label, for diagnostics only
//   subtitlesOff               user turned subtitles off for this show
//   audioDelay / subDelay      per-show A/V sync offsets, in seconds
var KNOWN_FIELDS = [
    "audioLang", "audioTrackTitle",
    "subtitleLang", "subtitleTrackTitle",
    "subtitlesOff", "audioDelay", "subDelay"
]

function readStore(jsonText) {
    try {
        var parsed = JSON.parse(String(jsonText || "{}"))
        if (!parsed || typeof parsed !== "object") return ({})
        return parsed
    } catch (e) {
        return ({})
    }
}

function getPref(jsonText, showKey) {
    var store = readStore(jsonText)
    return store[String(showKey || "")] || ({})
}

function compact(store) {
    var rows = []
    for (var k in store) {
        if (!store.hasOwnProperty(k)) continue
        var rec = store[k] || ({})
        rec.showKey = k
        rows.push(rec)
    }
    rows.sort(function(a, b) { return Number(b.updatedAt || 0) - Number(a.updatedAt || 0) })
    rows = rows.slice(0, MAX_PREFS)
    var out = {}
    for (var i = 0; i < rows.length; i++) {
        var key = rows[i].showKey
        delete rows[i].showKey
        out[key] = rows[i]
    }
    return out
}

function upsertPref(jsonText, showKey, patch, nowMs) {
    var key = String(showKey || "")
    if (!key.length) return String(jsonText || "{}")
    var store = readStore(jsonText)
    var prev = store[key] || ({})
    var next = {}
    for (var a in prev) if (prev.hasOwnProperty(a)) next[a] = prev[a]
    patch = patch || ({})
    for (var f = 0; f < KNOWN_FIELDS.length; f++) {
        var field = KNOWN_FIELDS[f]
        if (patch.hasOwnProperty(field)) next[field] = patch[field]
    }
    next.updatedAt = Number(nowMs || Date.now())
    store[key] = next
    return JSON.stringify(compact(store))
}
