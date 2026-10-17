.pragma library

// Catalogue rails exclude local-file identities before applying their row cap.
// This presentation filter depends only on Progress, never on a local-media UI.
function recent(progress, kind, limit) {
    if (!progress) return []
    var all = progress.recent(kind || "", 0)
    var n = Number(limit || 0)
    var cap = isFinite(n) && n > 0 ? Math.floor(n) : 0
    var out = []
    for (var i = 0; i < all.length; i++) {
        var e = all[i]
        if (String((e && e.id) || "").indexOf("vault:") === 0) continue
        out.push(e)
        if (cap > 0 && out.length >= cap) break
    }
    return out
}
