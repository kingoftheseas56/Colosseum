.pragma library

// VaultApi — the pure derivation the Vault surfaces share (Slice 14): join the rebuildable index
// rows against the live Progress store, and (VaultApi.continueRail, added with the rail) derive the
// Vault Continue list. It owns no backend; Progress is passed in as an argument so this stays a
// .pragma library with no context capture.
//
// THERE IS NO KIND TRANSLATION HERE, DELIBERATELY. The Vault files each item under its canonical
// kind (comic | book | video), and — after the Slice 14 source-label fix in VaultComicReader — every
// reader/player persists Progress under that SAME kind (comics "comic" not "manga"; books "book";
// video "video"). So the join is a straight Progress.get(row.kind, row.id). If a future reader
// regresses to a non-canonical kind, fix the WRITER, not this file: a translation map here would
// silently leak local items into a catalogue namespace (the hazard Preflight flagged for the
// translate-at-the-boundary approach this fix supersedes).

function _frac(v) { var n = Number(v); return (n > 0) ? (n < 1 ? n : 1) : 0 }

// Decorate ONE index row with live Progress facts. Returns a NEW object (never mutates the index
// snapshot): progressFraction 0..1, lastReadMs (Progress.updatedAt, else 0), hasProgress, and
// `progressed` overridden to the live truth so the row's read tick reflects real reads, not the
// coarse static index column.
function joinRow(progress, row) {
    if (!row) return row
    var out = {}
    for (var k in row) out[k] = row[k]
    var pg = (progress && row.id) ? progress.get(row.kind, row.id) : null
    var has = !!(pg && pg.id !== undefined)
    out.progressFraction = has ? _frac(pg.progress) : 0
    out.lastReadMs = has ? (Number(pg.updatedAt) || 0) : 0
    out.hasProgress = has
    out.progressFinished = has && !!(pg.finished || pg.watched
                                      || (pg.resume && pg.resume.finished))
    out.progressed = has
    return out
}

function joinRows(progress, rows) {
    var src = rows || [], out = []
    for (var i = 0; i < src.length; i++) out.push(joinRow(progress, src[i]))
    return out
}

// The most-recently-read row that still carries progress — the preview "Continue" target. Ordered
// by lastReadMs (not array position), so Continue resumes the file you were actually mid-read on,
// not the top of the folder. Returns the row, or null when nothing carries progress.
function resumeTarget(rows) {
    var src = rows || [], best = null
    for (var i = 0; i < src.length; i++) {
        var r = src[i]
        if (r && r.hasProgress && (!best || r.lastReadMs > best.lastReadMs)) best = r
    }
    return best
}

// --- Vault Continue rail (Slice 14) -----------------------------------------------------------
function isVault(id) { return String(id || "").indexOf("vault:") === 0 }

// Coerce a caller limit to a positive whole number; 0 means "no cap". Guards NaN/negative/Infinity.
function _positiveLimit(limit) {
    var n = Number(limit || 0)
    if (!isFinite(n) || n <= 0) return 0
    return Math.floor(n)
}

// A Vault video is admissible to Continue ONLY when its durable verdict is EXACTLY "Admitted".
// Unprobed (absent from the map), rejected, or non-vault ids never qualify.
function isAdmittedVault(admissionById, id) {
    if (!isVault(id) || !admissionById) return false
    var verdict = admissionById[id]
    return verdict !== undefined && verdict !== null && String(verdict) === "Admitted"
}

// The file path to reopen, pulled from the resume payload each reader/player already writes: books
// carry resume.path, video resume.localPath, and a Vault comic resume.chapterId (== the archive
// path, because VaultComicReader feeds the shell chapterId = archivePath). LocalLaunch re-derives
// family/id/title from the path, so the rail reopens exactly like the picker / Open Recent.
function vaultPathOf(rec) {
    var r = (rec && rec.resume) ? rec.resume : {}
    return r.path || r.localPath || r.chapterId || ""
}

// The Vault Continue list: the live Progress recents that belong to the Vault (vault: ids), mapped
// to the tiles the rail renders and routes. Catalogue recents are excluded — they own their own
// rails, and §9 keeps the two from double-showing. A row with no resolvable path is dropped
// (nothing to reopen). Title/cover/fraction come straight off the Progress entry (no index lookup).
function continueRail(progress, limit, admissionById) {
    if (!progress) return []

    // Unbounded read FIRST, then cap after filtering. Rejected, unprobed, catalogue, or pathless
    // rows must not consume the requested output cap (the old filter-after-limit trap).
    var all = progress.recent("", 0)
    var cap = _positiveLimit(limit)
    var out = []

    for (var i = 0; i < all.length; i++) {
        var e = all[i]
        if (!isAdmittedVault(admissionById, e.id)) continue

        var path = vaultPathOf(e)
        if (!path) continue

        out.push({ id: e.id, kind: e.kind, path: path,
                   title: e.title || e.caption || "",
                   cover: e.cover || "",
                   progressFraction: _frac(e.progress) })

        if (cap > 0 && out.length >= cap) break
    }
    return out
}

// §9 catalogue isolation: strip vault: rows from a catalogue recents read BEFORE the hard cap, so a
// run of local items can never shrink a catalogue rail below its intended length.
function recentWithoutVault(progress, kind, limit) {
    if (!progress) return []

    var all = progress.recent(kind || "", 0)
    var cap = _positiveLimit(limit)
    var out = []

    for (var i = 0; i < all.length; i++) {
        var e = all[i]
        if (isVault(e && e.id)) continue
        out.push(e)
        if (cap > 0 && out.length >= cap) break
    }
    return out
}
