// MangaChapterGrouping.js — pure Arc 39 chapter grouping policy.
// Generic mode always windows at ten. Exact-volume mode is allowed only when
// the canonical Arc 1 evidence proves the complete volume boundary set.
.pragma library

var WINDOW_SIZE = 10

function numericValue(raw) {
    if (raw === undefined || raw === null || raw === "") return null
    var n = Number(raw)
    return isFinite(n) ? n : null
}

function chapterNumber(chapter) {
    if (!chapter) return null
    var direct = numericValue(chapter.number)
    if (direct !== null) return direct
    var text = String(chapter.label || chapter.name || "")
    var match = /-?\d+(?:\.\d+)?/.exec(text)
    return match ? Number(match[0]) : null
}

function chapterToken(chapter) {
    var n = chapterNumber(chapter)
    if (n !== null) return String(n)
    var label = String((chapter && (chapter.label || chapter.name)) || "").trim()
    return label.length ? label : "?"
}

function stableKey(prefix, chapters) {
    if (!chapters || !chapters.length) return prefix + ":empty"
    return prefix + ":" + String(chapters[0].id) + ":" + String(chapters[chapters.length - 1].id)
}

function windowsFor(chapters, prefix) {
    var out = []
    var rows = chapters || []
    for (var i = 0; i < rows.length; i += WINDOW_SIZE) {
        var slice = rows.slice(i, i + WINDOW_SIZE)
        out.push({
            key: stableKey(prefix + ":window", slice),
            label: "Chapters " + chapterToken(slice[0]) + "–" + chapterToken(slice[slice.length - 1]),
            chapters: slice
        })
    }
    return out
}

function genericGroups(chapters, kind) {
    var out = []
    var rows = chapters || []
    for (var i = 0; i < rows.length; i += WINDOW_SIZE) {
        var slice = rows.slice(i, i + WINDOW_SIZE)
        var key = stableKey(kind || "generic", slice)
        out.push({
            key: key,
            kind: kind || "generic",
            label: "Chapters " + chapterToken(slice[0]) + "–" + chapterToken(slice[slice.length - 1]),
            volumeNumber: "",
            windows: [{ key: key + ":window", label: "", chapters: slice }]
        })
    }
    return out
}

function fallback(chapters, reason) {
    return { mode: "generic", reason: reason, groups: genericGroups(chapters, "generic") }
}

function rangeStatusRejected(status) {
    var s = String(status || "").toLowerCase()
    return s.indexOf("guess") >= 0 || s.indexOf("infer") >= 0
        || s.indexOf("interpolat") >= 0 || s.indexOf("partial") >= 0
        || s.indexOf("conflict") >= 0
}

function checkedRanges(record, canonicalSeriesId) {
    if (!record || !record.volumes || !record.volumes.length)
        return { ok: false, reason: "no-exact-ranges" }
    if (String(record.source || "") !== "arc1")
        return { ok: false, reason: "noncanonical-source" }
    if (String(record.canonicalSeriesId || "") !== String(canonicalSeriesId || ""))
        return { ok: false, reason: "identity-mismatch" }
    if (record.canonicalVolumeCountKnown !== true)
        return { ok: false, reason: "incomplete-volume-count" }
    if (record.matchesCanonicalVolumeCount !== true)
        return { ok: false, reason: "volume-count-mismatch" }
    if (record.looseStatus !== "verified_none" && record.looseStatus !== "verified_cross_checked")
        return { ok: false, reason: "loose-tail-unverified" }

    var ranges = []
    for (var i = 0; i < record.volumes.length; ++i) {
        var row = record.volumes[i] || ({})
        if (String(row.rangeState || "") === "conflict")
            return { ok: false, reason: "range-conflict" }
        if (String(row.rangeState || "") !== "known")
            return { ok: false, reason: "range-incomplete" }
        if (rangeStatusRejected(row.rangeStatus))
            return { ok: false, reason: "range-unverified" }
        var start = numericValue(row.chapterStart)
        var end = numericValue(row.chapterEnd)
        if (start === null || end === null || start > end)
            return { ok: false, reason: "range-malformed" }
        if (ranges.length) {
            var previous = ranges[ranges.length - 1]
            if (start <= previous.end)
                return { ok: false, reason: "range-overlap" }
            if (start - previous.end > 1.000001)
                return { ok: false, reason: "range-gap" }
        }
        ranges.push({
            number: String(row.number || row.volumeNumber || ""),
            start: start,
            end: end
        })
        if (!ranges[ranges.length - 1].number.length)
            return { ok: false, reason: "range-malformed" }
    }
    return { ok: true, ranges: ranges }
}

function looseNumberSet(record) {
    var set = ({})
    var values = record.looseChapterNumbers || []
    for (var i = 0; i < values.length; ++i) {
        var n = numericValue(values[i])
        if (n !== null) set[String(n)] = true
    }
    return set
}

function exactGroups(chapters, record, ranges) {
    var buckets = []
    for (var i = 0; i < ranges.length; ++i) buckets.push([])
    var loose = []
    var looseSet = looseNumberSet(record)
    var maxEnd = ranges[ranges.length - 1].end

    for (var c = 0; c < chapters.length; ++c) {
        var ch = chapters[c]
        var n = chapterNumber(ch)
        if (n === null) return { ok: false, reason: "unnumbered-chapter" }
        var assigned = false
        for (var r = 0; r < ranges.length; ++r) {
            if (n >= ranges[r].start && n <= ranges[r].end) {
                buckets[r].push(ch)
                assigned = true
                break
            }
        }
        if (assigned) continue
        if (n <= maxEnd) return { ok: false, reason: "unmapped-chapter" }
        if (record.looseStatus === "verified_none")
            return { ok: false, reason: "stale-range-evidence" }
        if (!looseSet[String(n)])
            return { ok: false, reason: "loose-tail-mismatch" }
        loose.push(ch)
    }

    var groups = []
    for (var b = 0; b < ranges.length; ++b) {
        if (!buckets[b].length)
            return { ok: false, reason: "range-has-no-source-chapters" }
        groups.push({
            key: stableKey("volume:" + ranges[b].number, buckets[b]),
            kind: "volume",
            label: "Volume " + ranges[b].number,
            volumeNumber: ranges[b].number,
            windows: windowsFor(buckets[b], "volume:" + ranges[b].number)
        })
    }
    if (loose.length) {
        var tail = genericGroups(loose, "loose")
        for (var g = 0; g < tail.length; ++g) groups.push(tail[g])
    }
    return { ok: true, groups: groups }
}

function group(chapters, exactRecord, canonicalSeriesId) {
    var rows = chapters || []
    if (!rows.length) return { mode: "generic", reason: "no-chapters", groups: [] }
    var checked = checkedRanges(exactRecord || ({}), canonicalSeriesId)
    if (!checked.ok) return fallback(rows, checked.reason)
    var exact = exactGroups(rows, exactRecord, checked.ranges)
    if (!exact.ok) return fallback(rows, exact.reason)
    return { mode: "exact-volume", reason: "verified-canonical-ranges", groups: exact.groups }
}
