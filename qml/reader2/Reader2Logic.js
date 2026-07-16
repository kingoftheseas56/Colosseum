// Reader2Logic.js — pure logic for the resume seam (TASK 6). No QML types, no
// network, no Date: just data-in / data-out, so a headless harness can prove it
// (tests/reader2_logic_harness.qml). ReaderShell.qml imports this as `L` and does
// the store I/O + timestamp stamping; this file only shapes the record.
//
// `.pragma library` = one shared, stateless singleton across every importer (no
// per-instance copy). A library JS cannot touch the QML engine's context or `Date`,
// so `updatedAt` is stamped by the CALLER and passed in via `relocated.updatedAt`.
//
// [Agent 2 (Claude), biblio]
.pragma library

// Derive a book format ("epub"/"mobi"/"pdf"/...) from a path or file:// URL.
function formatFromPath(p) {
    var s = String(p || "")
    var q = s.indexOf("?")          // tolerate a query on file:// URLs
    if (q >= 0) s = s.slice(0, q)
    var dot = s.lastIndexOf(".")
    var slash = Math.max(s.lastIndexOf("/"), s.lastIndexOf("\\"))
    if (dot < 0 || dot < slash) return ""
    return s.slice(dot + 1).toLowerCase()
}

// progressRecord(prev, relocated, bookPath) → the store value to SAVE.
//
// READ-MODIFY-WRITE: start from `prev` (the existing progress.json entry, or {}),
// overlay the new position from a paper `relocated` event, and PRESERVE every field
// we don't understand (bookMeta, chapter, chapterCount, bookmarks, pageHint,
// scrollFraction, ...). That keeps the entry fully readable by BOTH readers and
// loses no data — the zero-migration promise. Only locator/percent/finished (and the
// identity fields path/format/mediaType when absent) are touched.
function progressRecord(prev, relocated, bookPath) {
    var out = {}
    if (prev && typeof prev === "object")
        for (var k in prev) out[k] = prev[k]

    var r = relocated || {}

    // locator: merge the existing locator with the new position. cfi is the resume
    // anchor; href/fraction/updatedAt update only when the event carries them.
    var loc = {}
    if (out.locator && typeof out.locator === "object")
        for (var lk in out.locator) loc[lk] = out.locator[lk]
    if (r.cfi !== undefined && r.cfi !== null) loc.cfi = r.cfi
    else if (loc.cfi === undefined || loc.cfi === null) loc.cfi = ""
    if (r.href !== undefined && r.href !== null) loc.href = r.href
    if (r.fraction !== undefined && r.fraction !== null) loc.fraction = r.fraction
    if (r.updatedAt !== undefined && r.updatedAt !== null) loc.updatedAt = r.updatedAt
    out.locator = loc

    // percent (0..100 scale, same as the old reader's store) + finished flag.
    if (r.percent !== undefined && r.percent !== null) {
        out.percent = r.percent
        out.finished = r.percent >= 100
    }
    // else: keep prev.percent / prev.finished (already copied above).

    // identity fields — present so the entry is self-describing if prev lacked them.
    if (out.path === undefined || out.path === null) out.path = bookPath
    if (out.mediaType === undefined || out.mediaType === null) out.mediaType = "book"
    if (out.format === undefined || out.format === null) out.format = formatFromPath(out.path)

    return out
}

// resumeCfiOf(entry) → the CFI to open the book at ("" = open at the start).
function resumeCfiOf(entry) {
    return (entry && entry.locator && entry.locator.cfi) ? entry.locator.cfi : ""
}

// railState(relocated, tocLength) → the progress-rail view model for Task 7's chrome.
// Pure derivation from a relocated event; kept simple and side-effect-free.
function railState(relocated, tocLength) {
    var r = relocated || {}
    var pct = Number.isFinite(r.percent) ? r.percent : 0
    var label = ""
    var cur = r.pageInChapter, tot = r.pagesInChapter
    if (Number.isFinite(cur) && Number.isFinite(tot) && tot > 0)
        label = "Page " + cur + " of " + tot + " in chapter"
    return { fillPct: pct, label: label }
}
