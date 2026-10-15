.pragma library

var LANG_ALIASES = {
    eng: "eng", en: "eng", english: "eng",
    jpn: "jpn", ja: "jpn", jp: "jpn", japanese: "jpn",
    spa: "spa", es: "spa", spanish: "spa",
    fre: "fre", fra: "fre", fr: "fre", french: "fre",
    ger: "ger", deu: "ger", de: "ger", german: "ger",
    kor: "kor", ko: "kor", korean: "kor",
    chi: "chi", zho: "chi", zh: "chi", chinese: "chi"
}

function normalizeLang(value) {
    var raw = String(value || "").trim().toLowerCase()
    if (!raw.length) return ""
    raw = raw.replace(/[^a-z]/g, "")
    return LANG_ALIASES[raw] || raw
}

function parseLanguageList(value, fallback) {
    var raw = String(value || "").split(",")
    var out = []
    for (var i = 0; i < raw.length; i++) {
        var n = normalizeLang(raw[i])
        if (n.length && out.indexOf(n) < 0)
            out.push(n)
    }
    return out.length ? out : (fallback || []).slice()
}

function showKey(kind, id) {
    var k = String(kind || "video")
    var s = String(id || "")
    var parts = s.split(":")
    if (parts.length >= 3 && parts[0].indexOf("tt") === 0)
        return kind + ":" + parts[0]
    if (parts.length >= 3)
        return kind + ":" + parts[0] + ":" + parts[1]
    return k + ":" + s
}

function trackLang(track) {
    var lang = normalizeLang(track && track.lang)
    if (lang.length) return lang
    var text = String((track && (track.title || track.label)) || "").toLowerCase()
    if (/\b(japanese|jpn|ja)\b/.test(text)) return "jpn"
    if (/\b(english|eng|en)\b/.test(text)) return "eng"
    return ""
}

// A matched language always scores strictly positive (min 1), so callers can treat a
// zero score as "not in the preferred list" even for long preference lists.
function languageScore(track, preferredLangs) {
    var lang = trackLang(track)
    if (!lang.length) return 0
    var idx = (preferredLangs || []).indexOf(lang)
    return idx < 0 ? 0 : Math.max(1, 100 - idx * 10)
}

function blockedWords(value) {
    var raw = String(value || "").split(",")
    var out = []
    for (var i = 0; i < raw.length; i++) {
        var w = String(raw[i] || "").trim().toLowerCase()
        if (w.length) out.push(w)
    }
    return out
}

function filterBlockedTracks(tracks, blocked) {
    var words = blockedWords(blocked)
    if (!words.length) return (tracks || []).slice()
    var out = []
    for (var i = 0; i < (tracks || []).length; i++) {
        var t = tracks[i] || ({})
        var hay = String((t.title || "") + " " + (t.label || "")).toLowerCase()
        var hit = false
        for (var w = 0; w < words.length; w++) {
            if (hay.indexOf(words[w]) >= 0) { hit = true; break }
        }
        if (!hit) out.push(t)
    }
    if (out.length === 0) return tracks
    return out
}

function sourceScore(track, preferEmbeddedSubtitles) {
    var external = !!(track && track.external)
    if (preferEmbeddedSubtitles)
        return external ? 1 : 3
    return external ? 3 : 1
}

function stableSortCopy(tracks, scoreFn) {
    var rows = []
    for (var i = 0; i < (tracks || []).length; i++)
        rows.push({ row: tracks[i], index: i, score: scoreFn(tracks[i], i) })
    rows.sort(function(a, b) {
        if (a.score !== b.score) return b.score - a.score
        return a.index - b.index
    })
    return rows.length && rows[0].score > 0 ? rows[0].row : null
}

// Language match GATES selection: a track with no preferred-language match scores 0 and is
// never auto-picked (source/default are only tie-breakers among language-matched tracks).
// Without this gate, sourceScore (always >= 1) would make any subtitle pickable regardless of
// language, forcing a wrong-language track and pre-empting the pickDefault fallback.
function pickBestAudioTrack(tracks, preferredLangs, blocked) {
    var candidates = filterBlockedTracks(tracks || [], blocked)
    return stableSortCopy(candidates, function(t) {
        var ls = languageScore(t, preferredLangs)
        if (ls <= 0) return 0
        return ls + (t.default ? 3 : 0)
    })
}

function pickBestSubtitleTrack(tracks, preferredLangs, options) {
    options = options || ({})
    var candidates = filterBlockedTracks(tracks || [], options.blockedTrackWords || "")
    return stableSortCopy(candidates, function(t) {
        if (options.forcedOnly && !t.forced) return 0
        if (!options.forcedOnly && t.forced) return 0
        var ls = languageScore(t, preferredLangs)
        if (ls <= 0) return 0
        return ls
             + sourceScore(t, !!options.preferEmbeddedSubtitles)
             + (t.default ? 2 : 0)
    })
}

function ids(list) {
    var out = []
    for (var i = 0; i < (list || []).length; i++) {
        var t = list[i] || ({})
        out.push(String(t.id || t.url || "") + ":" + String(t.lang || "") + ":" + String(t.title || t.label || ""))
    }
    return out.sort().join("|")
}

function trackSignature(audioTracks, subtitleTracks, onlineSubs, policyKey) {
    return [
        ids(audioTracks),
        ids(subtitleTracks),
        ids(onlineSubs),
        String(policyKey || ""),
        "subtitleAutoUpgrade"
    ].join("||")
}
