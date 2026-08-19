.pragma library

// Shared pure decision logic behind the "Your Colosseum" reading activity hooks
// (CPP-PORT-CONTRACT.md §7 identity rules, §9 Lane C, §10 fixed-page dedupe). Used by
// qml/comicreader/ComicReaderShell.qml (session/identity/completion) — its QuickTest coverage
// in tests/qml/tst_comic_activity.qml exercises the SAME code the shell actually runs, not a
// parallel reimplementation that could drift.
//
// This file owns ONLY pure functions (no QObject, no core/backend/context-property access) so
// it is trivially unit-testable and safe to `.import` from a QuickTest with no component tree
// — mirrors qml/ActivityLaneHelpers.js's own rule, which ComicReaderShell.qml also imports
// directly for the GENERIC begin/no-op/end session-transition rule (keyFor / decideTransition,
// already shared by Player 1 and the audiobook lane) rather than re-deriving a third copy here.

// §7 Lane C kind derivation. `progressKind` is ComicReaderState.progressKind(entryKind,
// western)'s own three-way answer ("manga" | "comic" | "tankoban") — reused rather than
// re-deriving the manga/western crossing rule a second time (that rule lives in exactly one
// place, ComicReaderState.js). An unrecognised value fails closed to null (§25): no stable
// activity kind, no invented fact.
function activityKindFor(progressKind) {
    if (progressKind === "manga") return "manga_chapter"
    if (progressKind === "comic") return "comic_issue"
    if (progressKind === "tankoban") return "tankoban_volume"
    return null
}

// §7 "Manga/comic/Tankoban" identity block: titleKey is ALWAYS the tankoban: namespace,
// whatever the lane — a western comic and a scanlated manga chapter both group by the same
// series identity, only `kind` tells them apart. Fails closed to null when either half of the
// identity (series/entry) or the derived kind is not resolvable (§25 fail-closed).
function identityFor(seriesId, entryId, progressKind) {
    var sid = String(seriesId || "")
    var eid = String(entryId || "")
    if (!sid.length || !eid.length)
        return null
    var kind = activityKindFor(progressKind)
    if (!kind)
        return null
    return { kind: kind, titleKey: "tankoban:" + sid, itemKey: eid }
}

// §15 "Fields that must never enter ordinary sync" / "A cover field is either empty or a
// portable safe locator": comic/manga covers are provider URLs in the common case, but a
// Tankoban volume with no online source can hand back a local extracted-cover path, and a
// local file:/qrc:/drive-letter/UNC/relative path is not portable activity metadata.
// ALLOWLIST (http/https/data), not a blocklist — fails closed to "" for anything unrecognised,
// the safe direction per §25 ("unsafe/path identity -> local-only event").
function portableCover(url) {
    var u = String(url || "")
    if (!u.length)
        return ""
    return /^(https?|data):/i.test(u) ? u : ""
}

// The stable physical page-key format every surface + the shell's coverage check share.
// 0-based, namespaced by the caller's kind+itemKey scope — §10's dedupe key is exactly
// sessionId+kind+itemKey+pageKey, so the key string itself only has to be unique WITHIN one
// entry, never globally.
function pageKeyFor(index0) {
    return "p" + index0
}

// Map+dedupe+filter a raw 0-based index list (a surface's activityPagesPresented payload) into
// stable page keys, preserving the surface's own order. Negative/non-numeric indices (a
// surface reporting "nothing rendered") are dropped rather than turned into a bogus "p-1" key.
function pageKeysFor(indices0) {
    var out = []
    var seen = ({})
    var list = indices0 || []
    for (var i = 0; i < list.length; i++) {
        var idx = Number(list[i])
        if (!isFinite(idx) || idx < 0)
            continue
        var key = pageKeyFor(idx)
        if (seen[key])
            continue
        seen[key] = true
        out.push(key)
    }
    return out
}

// §9 Lane C "Fixed-page completion": requiredPageKeys is the entry's exact required
// NON-TERMINAL-BROKEN physical page set — every page 0..pageCount-1 except one the shell's own
// core.pageInfo() currently reports as terminally damaged. Pure once the shell has already
// resolved which indices are broken (an impure, core-reading question the shell itself
// answers via _pageBroken — this function only shapes the result).
function requiredPageKeys(pageCount, brokenIndices0) {
    var broken = ({})
    var list = brokenIndices0 || []
    for (var i = 0; i < list.length; i++)
        broken[Number(list[i])] = true
    var out = []
    var count = Number(pageCount) || 0
    for (var p = 0; p < count; p++) {
        if (broken[p])
            continue
        out.push(pageKeyFor(p))
    }
    return out
}
