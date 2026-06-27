// MangaVolumes.js — volume-structure adapter + chapter grouping. The native MangaFire client
// (MangaFireCatalogClient) hands us CLEAN, contiguous volumes directly — number, real per-volume
// cover, and the chapter range MangaFire reports — so the old MangaDex aggregate reconstruction is
// gone. fromMangaFire() just normalizes the engine's list into numeric ranges; group() (ported from
// Tankoban Electron's MangaSeries.jsx volumeGroups) still buckets the flat WeebCentral chapter list
// into those volumes for the selector.
.pragma library

function chapterNum(raw) {
    var m = /-?\d+(?:\.\d+)?/.exec(String(raw || ''))
    return m ? Number(m[0]) : null
}

// engine volumes: [{ number, cover, chapterStart, chapterEnd }] (ascending) →
//   [{ number, cover, startNum, endNum, chapterStart, chapterEnd }] with numeric ranges for group().
// MangaFire already gives clean contiguous volumes, so there is nothing to reconstruct — we only
// parse the leading chapter number out of each range string and sort by it.
function fromMangaFire(volumes) {
    if (!volumes || !volumes.length) return []
    // 1. parse {number, cover, start, rawEnd}; start = the volume's first chapter (MangaFire chapterStart)
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

    // 2. Repair starts to be STRICTLY INCREASING by volume number. MangaFire tags stray "special"
    //    chapters (a prologue ch 0, a .5 omake) to odd volumes, corrupting that volume's first/last —
    //    e.g. Vinland's Vol 14 returns ch 0..209 though it's really ~94..100. Volume starts are
    //    otherwise clean and evenly spaced, so: drop any start that isn't greater than all earlier
    //    ones, then interpolate the gaps from clean neighbours. Rebuilds sane, non-overlapping bounds.
    var n = raw.length
    if (n >= 2 && raw[0].start !== null && raw[1].start !== null && raw[0].start > raw[1].start)
        raw[0].start = null                                    // corrupt FIRST (e.g. a Vol 0 of specials)
    var runMax = -Infinity
    for (var a = 0; a < n; a++) {
        if (raw[a].start === null || raw[a].start <= runMax) raw[a].start = null
        else runMax = raw[a].start
    }
    if (raw[0].start === null) raw[0].start = 0
    for (var b = 1; b < n; b++) {
        if (raw[b].start !== null) continue
        var j = b + 1
        while (j < n && raw[j].start === null) j++
        var prev = raw[b - 1].start
        if (j < n) {
            var step = (raw[j].start - prev) / (j - (b - 1))
            for (var k = b; k < j; k++) raw[k].start = prev + step * (k - (b - 1))
        } else {
            for (var k2 = b; k2 < n; k2++) raw[k2].start = prev + (k2 - (b - 1))
        }
    }

    // 3. contiguous ranges: each volume owns [start, nextStart); the last owns the tail.
    var out = []
    for (var c = 0; c < n; c++) {
        var s = raw[c].start
        var hasNext = (c + 1 < n)
        var ds = Math.max(0, Math.round(s))
        var e, de
        if (hasNext) {
            e = raw[c + 1].start - 0.001
            de = Math.max(ds, Math.round(raw[c + 1].start - 1))
        } else {
            e = Infinity                                       // last volume catches every trailing chapter
            de = (raw[c].rawEnd !== null && raw[c].rawEnd > s) ? Math.round(raw[c].rawEnd) : ds
        }
        out.push({ number: raw[c].number, cover: raw[c].cover,
                   startNum: s, endNum: e, chapterStart: String(ds), chapterEnd: String(de) })
    }
    return out
}

// Group the flat (ascending) chapter list into the reconstructed volumes.
// chapters: [{ number, name, ... }]  volumes: build() output.
//   → { options: [{ key, label }], byKey: { <volNumber>: [chapters], X: [loose tail] } }
function group(chapters, volumes) {
    var empty = { options: [], byKey: {} }
    if (!chapters || !chapters.length || !volumes || !volumes.length) return empty

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
