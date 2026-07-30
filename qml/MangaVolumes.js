// MangaVolumes.js — volume adapter + chapter grouping for the tankoban surface.
// The native ComickCatalogClient hands us COMPLETE, gate-qualified volume ranges
// (our volume DB first, live Comick scrape on a miss) — or nothing at all. There is
// deliberately NO interpolation and NO anchor-repair here any more: a series either
// has a real, complete volume->chapter mapping or it shows the flat WeebCentral
// chapter list. Estimated boundaries are rejected doctrine (2026-07-29).
// group() (ported from Tankoban Electron's MangaSeries.jsx volumeGroups) buckets the
// flat WeebCentral chapter list into the ranged volumes; chapters past the last range
// land in the 'X' ("Latest chapters") bucket.
.pragma library

// KNOWN LIMIT (accepted, QML side only): this parses a label to a JS Number, so the
// ORDINAL sub-chapters "315.10" and "315.1" collapse to the same value — the exact
// defect that WAS fixed on the C++/Python side. It only decides which bucket a side
// chapter falls into for display; it never produces a published boundary, and the
// volume ranges themselves arrive from the engine as already-verified strings. Do not
// assume this matches the engine's precision.
function chapterNum(raw) {
    var m = /-?\d+(?:\.\d+)?/.exec(String(raw || ''))
    return m ? Number(m[0]) : null
}

// engine volumes: [{ number, cover, chapterStart, chapterEnd }] (ascending, ranges
// complete — the gate guarantees it) →
//   [{ number, cover, startNum, endNum, chapterStart, chapterEnd }] for group().
function fromEngine(volumes) {
    if (!volumes || !volumes.length) return []
    var out = []
    for (var i = 0; i < volumes.length; i++) {
        var v = volumes[i]
        var number = Number(v.number)
        var s = chapterNum(v.chapterStart)
        var e = chapterNum(v.chapterEnd)
        if (isNaN(number) || s === null || e === null) continue   // malformed: drop, never guess
        out.push({ number: number, cover: v.cover || "",
                   startNum: s, endNum: Math.max(s, e),
                   chapterStart: String(v.chapterStart), chapterEnd: String(v.chapterEnd) })
    }
    out.sort(function (a, b) { return a.number - b.number })
    // Contiguous handoff: each volume owns up to the next volume's start, so an
    // untagged sub-chapter between two reported ends (a 27.5 omake) lands in the
    // right book rather than in 'X'. Only ever EXTENDS a volume's end to just before
    // the next volume's real start — it never moves a start or invents a boundary.
    for (var j = 0; j + 1 < out.length; j++)
        if (out[j + 1].startNum > out[j].endNum)
            out[j].endNum = out[j + 1].startNum - 0.001
    return out
}

// Group the flat (ascending) chapter list into the reconstructed volumes.
// chapters: [{ number, name, ... }]  volumes: fromEngine() output — rangeless entries
// (startNum === null) are shelf-only and take no part in grouping.
//   → { options: [{ key, label }], byKey: { <volNumber>: [chapters], X: [loose tail] } }
function group(chapters, allVolumes) {
    var empty = { options: [], byKey: {} }
    if (!chapters || !chapters.length || !allVolumes || !allVolumes.length) return empty
    var volumes = allVolumes.filter(function (v) { return v.startNum !== null })
    if (!volumes.length) return empty

    var maxEnd = volumes[0].endNum
    for (var e = 0; e < volumes.length; e++) maxEnd = Math.max(maxEnd, volumes[e].endNum)

    var byKey = { X: [] }
    for (var vi = 0; vi < volumes.length; vi++) byKey[volumes[vi].number] = []

    function assign(ch) {
        var n = (ch.number !== undefined && ch.number !== null && ch.number !== 0)
                ? Number(ch.number) : chapterNum(ch.name)
        if (n === null || isNaN(n)) return 'X'
        for (var a = 0; a < volumes.length; a++)
            if (n >= volumes[a].startNum && n <= volumes[a].endNum) return volumes[a].number
        for (var f = 0; f < volumes.length; f++)
            if (Math.floor(n) >= Math.floor(volumes[f].startNum) && Math.floor(n) <= Math.floor(volumes[f].endNum))
                return volumes[f].number
        if (n > maxEnd) return 'X'
        var best = null
        for (var bb = 0; bb < volumes.length; bb++)
            if (volumes[bb].startNum <= n && (!best || volumes[bb].startNum > best.startNum)) best = volumes[bb]
        return best ? best.number : 'X'
    }

    for (var c = 0; c < chapters.length; c++) byKey[assign(chapters[c])].push(chapters[c])

    var options = []
    for (var o = 0; o < volumes.length; o++) {
        var list = byKey[volumes[o].number]
        if (!list || list.length === 0) continue
        options.push({ key: String(volumes[o].number), label: "Volume " + volumes[o].number })
    }
    if (byKey.X.length > 0) options.push({ key: 'X', label: "Latest chapters (" + byKey.X.length + ")" })
    return { options: options, byKey: byKey }
}
