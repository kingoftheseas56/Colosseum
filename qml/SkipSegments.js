.pragma library

var MIN_SEGMENT_SEC = 2
var MAX_SEGMENT_SEC = 360
var MIN_OUTRO_START_FRACTION = 0.5

var INTRO_RE = /\b(opening|op|intro|opening credits|theme song)\b/i
var OUTRO_RE = /\b(ending|ed|outro|credits|closing credits)\b/i
var RECAP_RE = /\b(recap|previously)\b/i

function classifyChapter(title) {
    var t = String(title || "")
    if (RECAP_RE.test(t)) return "recap"
    if (INTRO_RE.test(t)) return "intro"
    if (OUTRO_RE.test(t)) return "outro"
    return ""
}

function keyFor(s) {
    return [s.kind, Math.round(s.startSec * 10), Math.round(s.endSec * 10)].join(":")
}

function normalize(kind, startSec, endSec, source) {
    return {
        "kind": kind,
        "startSec": Number(startSec),
        "endSec": Number(endSec),
        "source": source || "unknown",
        "key": ""
    }
}

function chaptersToSegments(chapters, durationSec) {
    var sorted = (chapters || []).slice().sort(function(a, b) {
        return Number(a.startSec || 0) - Number(b.startSec || 0)
    })
    var out = []
    for (var i = 0; i < sorted.length; i++) {
        var c = sorted[i] || ({})
        var kind = classifyChapter(c.title)
        if (!kind.length) continue
        var start = Number(c.startSec || 0)
        var next = sorted[i + 1] || null
        var end = next ? Number(next.startSec || 0) : (durationSec > 0 ? Number(durationSec) : start + 90)
        if (end > start)
            out.push(normalize(kind, start, end, "chapters"))
    }
    return out
}

function parseAniSkipResults(json) {
    var results = (json && json.results) ? json.results : []
    var out = []
    for (var i = 0; i < results.length; i++) {
        var r = results[i] || ({})
        var interval = r.interval || ({})
        var start = Number(interval.startTime)
        var end = Number(interval.endTime)
        if (!isFinite(start) || !isFinite(end) || end <= start) continue
        var t = String(r.skipType || "")
        var kind = (t === "ed" || t === "mixed-ed") ? "outro" : (t === "recap" ? "recap" : "intro")
        out.push(normalize(kind, start, end, "aniskip"))
    }
    return out
}

function sanitizeSegments(segments, durationSec) {
    var out = []
    var duration = Number(durationSec || 0)
    for (var i = 0; i < (segments || []).length; i++) {
        var s = segments[i] || ({})
        var start = Math.max(0, Number(s.startSec))
        var end = Number(s.endSec)
        if (!isFinite(start) || !isFinite(end)) continue
        if (duration > 0) end = Math.min(end, duration)
        if (end <= start) continue
        var len = end - start
        if (len < MIN_SEGMENT_SEC || len > MAX_SEGMENT_SEC) continue
        if (duration > 0 && s.kind === "outro" && start < duration * MIN_OUTRO_START_FRACTION) continue
        var n = normalize(s.kind, start, end, s.source)
        n.key = keyFor(n)
        out.push(n)
    }
    out.sort(function(a, b) { return a.startSec - b.startSec })
    return out
}

function sourceRank(source) {
    if (source === "aniskip") return 3
    if (source === "chapters") return 2
    return 1
}

function overlaps(a, b) {
    return a.kind === b.kind && a.startSec < b.endSec && b.startSec < a.endSec
}

function mergeSegments(providerLists, durationSec) {
    var flat = []
    for (var i = 0; i < (providerLists || []).length; i++)
        flat = flat.concat(providerLists[i] || [])
    var clean = sanitizeSegments(flat, durationSec)
    var out = []
    for (var c = 0; c < clean.length; c++) {
        var seg = clean[c]
        var replaced = false
        for (var j = 0; j < out.length; j++) {
            if (!overlaps(seg, out[j])) continue
            if (sourceRank(seg.source) > sourceRank(out[j].source))
                out[j] = seg
            replaced = true
            break
        }
        if (!replaced)
            out.push(seg)
    }
    out.sort(function(a, b) { return a.startSec - b.startSec })
    return out
}

function activeSegment(segments, positionSec) {
    var pos = Number(positionSec || 0)
    for (var i = 0; i < (segments || []).length; i++) {
        var s = segments[i]
        if (pos >= s.startSec && pos < s.endSec - 0.75)
            return s
    }
    return null
}
