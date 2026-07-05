// MangaVolumes.js — volume-structure adapter + chapter grouping. The native MangaDex client
// (MangaDexCatalogClient) hands us per-volume tankōbon covers for every volume, plus chapter
// ranges only where MangaDex's chapter DB knows them (partial for big licensed titles — MangaFire,
// the old always-complete source, killed volumes in its 2026-07 relaunch). fromEngine() is
// COVERS-FIRST HONEST: it interpolates range gaps BETWEEN known anchors (the old repair logic) but
// never fabricates ranges with fewer than two anchors, and never past the last anchor — trailing
// chapters land in group()'s existing 'X' ("Latest chapters") bucket instead. group() (ported from
// Tankoban Electron's MangaSeries.jsx volumeGroups) still buckets the flat WeebCentral chapter list
// into the RANGED volumes for the selector; rangeless volumes are shelf-only.
.pragma library

function chapterNum(raw) {
    var m = /-?\d+(?:\.\d+)?/.exec(String(raw || ''))
    return m ? Number(m[0]) : null
}

// engine volumes: [{ number, cover, chapterStart, chapterEnd }] (ascending; empty range strings
// mean "range unknown") →
//   [{ number, cover, startNum, endNum, chapterStart, chapterEnd }] — numeric ranges for group()
//   on volumes whose range is known or safely interpolated; startNum/endNum stay null (and the
//   range strings "") on volumes we genuinely don't know, so nothing downstream fabricates.
function fromEngine(volumes) {
    if (!volumes || !volumes.length) return []
    // 1. parse {number, cover, start, rawEnd}; start = the volume's first known chapter
    var raw = []
    for (var i = 0; i < volumes.length; i++) {
        var v = volumes[i]
        var number = chapterNum(v.number)
        if (number === null) number = Number(v.number)
        if (number === null || isNaN(number)) continue
        raw.push({ number: number, cover: v.cover || "",
                   start: chapterNum(v.chapterStart), rawEnd: chapterNum(v.chapterEnd) })
    }
    if (!raw.length) return []
    raw.sort(function (a, b) { return a.number - b.number })   // trust the volume ORDER

    // 2. Repair starts to be STRICTLY INCREASING by volume number (sources tag stray "special"
    //    chapters — a prologue ch 0, a .5 omake — to odd volumes, corrupting that volume's
    //    first/last: e.g. Vinland's Vol 14 claiming ch 0..209 though it's really ~94..100).
    //    Then interpolate gaps BETWEEN surviving anchors only. With fewer than two anchors, or
    //    past the LAST anchor, starts stay null — covers-first honesty: an unknown range is
    //    shipped as unknown, never invented. (Trailing chapters become group()'s 'X' bucket.)
    var n = raw.length
    if (n >= 2 && raw[0].start !== null && raw[1].start !== null && raw[0].start > raw[1].start)
        raw[0].start = null                                    // corrupt FIRST (e.g. a Vol 0 of specials)
    var runMax = -Infinity, anchors = 0
    for (var a = 0; a < n; a++) {
        if (raw[a].start === null || raw[a].start <= runMax) raw[a].start = null
        else { runMax = raw[a].start; anchors++ }
    }
    if (anchors < 2) {
        for (var z = 0; z < n; z++) raw[z].start = null        // shelf-only: no ranges at all
    } else {
        if (raw[0].start === null) raw[0].start = 0            // leading gap closes against anchor 1
        for (var b = 1; b < n; b++) {
            if (raw[b].start !== null) continue
            var j = b + 1
            while (j < n && raw[j].start === null) j++
            if (j >= n) break                                  // tail past the last anchor: stays null
            var prev = raw[b - 1].start
            var step = (raw[j].start - prev) / (j - (b - 1))
            for (var k = b; k < j; k++) raw[k].start = prev + step * (k - (b - 1))
        }
    }

    // 3. contiguous ranges among the RANGED volumes: each owns [start, nextRangedStart); the last
    //    ranged volume ends at its own reported last chapter — never Infinity, so chapters beyond
    //    it fall to 'X' instead of being mis-filed under a volume that doesn't hold them.
    var out = []
    for (var c = 0; c < n; c++) {
        var s = raw[c].start
        if (s === null) {
            out.push({ number: raw[c].number, cover: raw[c].cover,
                       startNum: null, endNum: null, chapterStart: "", chapterEnd: "" })
            continue
        }
        var ds = Math.max(0, Math.round(s))
        var e, de
        var nextS = (c + 1 < n) ? raw[c + 1].start : null      // nulls only trail, so c+1 covers it
        if (nextS !== null) {
            e = nextS - 0.001
            de = Math.max(ds, Math.round(nextS - 1))
        } else {
            de = (raw[c].rawEnd !== null && raw[c].rawEnd > s) ? Math.round(raw[c].rawEnd) : ds
            e = Math.max(s, de)
        }
        out.push({ number: raw[c].number, cover: raw[c].cover,
                   startNum: s, endNum: e, chapterStart: String(ds), chapterEnd: String(de) })
    }
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
