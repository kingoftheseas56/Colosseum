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

// ---------------------------------------------------------------------------
// TASK 7 — reveal state machine + rail ticks (pure; proven headless).
// ---------------------------------------------------------------------------

// Chrome idle timeout (ms). Kept in lock-step with Theme.idleMs, and matched to the
// comic reader (MangaReader.qml) so both readers retreat on the same 3s beat. There is
// deliberately NO pointer-move event into this reducer: the chrome wakes ONLY when you
// reach for it (cursor enters the top/bottom edge band → "enterBar"), on the book-open
// orientation beat / an explicit double-click ("wake"/"toggle"), or a panel. Body
// movement, scroll, and keys can NEVER wake it — the whole point of the naked surface.
var REVEAL_IDLE_MS = 3000

// revealReducer(state, event, nowMs) → a NEW reveal state (pure, no side effects).
//   state : { shown:bool, lastActive:ms, pinned:bool, frozen:bool }
//   events:
//     "wake"       book opened / toggle-from-hidden → show + restart the idle beat
//     "enterBar"   cursor entered the top/bottom edge band → show AND freeze (idle
//                  can't hide it while the cursor stays inside the band)
//     "exitBar"    cursor left the band → drop the freeze + restart the idle countdown
//     "tick"       time advanced → hide IFF shown, not pinned, not frozen, and idle
//                  past the timeout
//     "toggle"     double-click → hide if shown (and not pinned), else show
//     "panelOpen"  a panel opened → pin shown (idle can't hide it)
//     "panelClose" panel closed → unpin + restart the idle countdown from nowMs
//   anything else — a stray "move", a key the chrome deliberately does NOT route here,
//   or any unknown event — returns the state UNCHANGED. That is the guarantee: body
//   movement can never wake the chrome.
function revealReducer(state, event, nowMs) {
    var s = state || {}
    var shown = !!s.shown
    var lastActive = Number.isFinite(s.lastActive) ? s.lastActive : 0
    var pinned = !!s.pinned
    var frozen = !!s.frozen
    var now = Number.isFinite(nowMs) ? nowMs : 0

    switch (event) {
    case "wake":
        return { shown: true, lastActive: now, pinned: pinned, frozen: false }
    case "enterBar":
        return { shown: true, lastActive: now, pinned: pinned, frozen: true }
    case "exitBar":
        return { shown: shown, lastActive: now, pinned: pinned, frozen: false }
    case "tick":
        if (shown && !pinned && !frozen && (now - lastActive) > REVEAL_IDLE_MS)
            return { shown: false, lastActive: lastActive, pinned: pinned, frozen: frozen }
        return { shown: shown, lastActive: lastActive, pinned: pinned, frozen: frozen }
    case "toggle":
        if (shown && !pinned)
            return { shown: false, lastActive: now, pinned: pinned, frozen: false }
        return { shown: true, lastActive: now, pinned: pinned, frozen: false }
    case "panelOpen":
        return { shown: true, lastActive: now, pinned: true, frozen: frozen }
    case "panelClose":
        return { shown: shown, lastActive: now, pinned: false, frozen: frozen }
    default:
        return { shown: shown, lastActive: lastActive, pinned: pinned, frozen: frozen }
    }
}

// railTicks(toc, sections) → chapter-mark positions as fractions in (0,1), ascending.
// Prefer explicit per-entry fractions when the toc carries them; otherwise fall back to
// evenly-spaced interior marks (one between each pair of consecutive sections). Pure: no
// engine, no Date, no side effects.
//
// ALL-OR-NOTHING (the fix): the glue (paper_glue.js `ready`) attaches a start `fraction`
// per toc entry best-effort — but SOME entries can fail to resolve (an href the book
// can't map, a book that exposes no section fractions). Taking only the RESOLVED subset
// would draw a sparse/skewed rail (e.g. ticks only for the 3 of 12 chapters that mapped).
// So the real-fraction fast-path fires ONLY when EVERY entry carries a finite fraction in
// (0,1); if even one is missing/out-of-range we fall back to even spacing for the WHOLE
// set. (The glue keeps its own per-entry guard; this is the whole-set gate.)
function railTicks(toc, sections) {
    var out = []
    if (toc && toc.length) {
        var n = toc.length
        var all = []
        var everyHasFraction = true
        for (var i = 0; i < n; i++) {
            var t = toc[i]
            var f = (t && typeof t === "object") ? t.fraction : undefined
            if (Number.isFinite(f) && f > 0 && f < 1) all.push(f)
            else { everyHasFraction = false; break }   // one gap → whole set falls back
        }
        if (everyHasFraction && all.length === n) {
            all.sort(function (a, b) { return a - b })
            return all
        }
        for (var j = 1; j < n; j++) out.push(j / n)   // even interior marks by toc count
        return out
    }
    var s = (Number.isFinite(sections) && sections > 1) ? Math.floor(sections) : 0
    for (var k = 1; k < s; k++) out.push(k / s)
    return out
}

// selectionMenuPos(sel, frameW, frameH, cardW, cardH, gap, margin) → { x, y } for the
// selection menu CARD: horizontally centered on the selection and clamped inside the
// frame; placed ABOVE the selection when there's room, else BELOW (also clamped). `sel`
// is the paper's selection rect { x, y, w, h } in frame coordinates. Pure — no engine,
// no Date; the SelectionMenu binds card.x/card.y to this so the popover never spills off
// screen. (The actual on-screen feel is Hemanth's eyes-on; this just keeps it in-bounds.)
function selectionMenuPos(sel, frameW, frameH, cardW, cardH, gap, margin) {
    var s = sel || {}
    var sx = Number.isFinite(s.x) ? s.x : 0
    var sy = Number.isFinite(s.y) ? s.y : 0
    var sw = Number.isFinite(s.w) ? s.w : 0
    var sh = Number.isFinite(s.h) ? s.h : 0
    var fw = Number.isFinite(frameW) ? frameW : 0
    var fh = Number.isFinite(frameH) ? frameH : 0
    var cw = Number.isFinite(cardW) ? cardW : 0
    var ch = Number.isFinite(cardH) ? cardH : 0
    var g = Number.isFinite(gap) ? gap : 10
    var m = Number.isFinite(margin) ? margin : 8

    // horizontal: center on the selection, clamp within [m, fw - cw - m].
    var x = sx + sw / 2 - cw / 2
    var maxX = fw - cw - m
    if (maxX < m) maxX = m
    if (x < m) x = m
    else if (x > maxX) x = maxX

    // vertical: prefer above the selection; if it won't fit, drop below. Clamp either way.
    var above = sy - ch - g
    var below = sy + sh + g
    var y = (above >= m) ? above : below
    var maxY = fh - ch - m
    if (maxY < m) maxY = m
    if (y < m) y = m
    else if (y > maxY) y = maxY

    return { x: x, y: y }
}

// ---------------------------------------------------------------------------
// TASK 8 — left panel row shaping (pure; proven headless).
// ---------------------------------------------------------------------------

// tocRowState(index, currentIndex) → "read" | "current" | "unread" for a Contents
// row. Index-based (relocated carries a flat tocIndex): rows BEFORE the current one
// are dimmed as read, the current row gets the gold wash, the rest are normal. When
// the current index is unknown (< 0, e.g. before the first relocate or an href the
// engine couldn't map), every row is "unread" — no false "read" dimming.
function tocRowState(index, currentIndex) {
    var i = Number(index)
    var c = Number(currentIndex)
    if (!Number.isFinite(c) || c < 0) return "unread"
    if (i === c) return "current"
    if (i < c) return "read"
    return "unread"
}

// bookmarkRow(bm) → { id, cfi, where, snippet } display row, tolerant of BOTH the
// reader2 write shape AND the old reader's records (zero migration). The old reader
// stores { locator:{cfi,...}, label, snippet } and stamps id; ours adds a `page`.
//   cfi     : the jump target (locator.cfi, or a bare cfi field).
//   where   : the uppercase "where" line — the chapter/label.
//   snippet : the serif detail line — a real text quote if we ever capture one, else
//             the stored snippet (only when it differs from `where`, so old records
//             whose label==snippet don't render the same string twice), else a
//             "Page N" hint. May be "" → the row shows the where line alone.
function bookmarkRow(bm) {
    var b = bm || {}
    var loc = (b.locator && typeof b.locator === "object") ? b.locator : {}
    var cfi = (loc.cfi !== undefined && loc.cfi !== null) ? String(loc.cfi)
            : (b.cfi !== undefined && b.cfi !== null) ? String(b.cfi) : ""
    var where = b.label ? String(b.label)
              : b.chapterLabel ? String(b.chapterLabel)
              : b.snippet ? String(b.snippet) : ""
    var snippet = ""
    if (b.text) snippet = String(b.text)
    else if (b.snippet && String(b.snippet) !== where) snippet = String(b.snippet)
    else if (b.page) snippet = "Page " + b.page
    return { id: b.id !== undefined && b.id !== null ? String(b.id) : "",
             cfi: cfi, where: where, snippet: snippet }
}

// highlightRow(h) → { id, cfi, where, text, note, color } display row. Tolerant of
// the old annotations.json shape { id, cfi, text, color, note, chapterLabel } AND a
// `value:cfi`/`locator.cfi` variant. Highlights are CREATED in Task 9; this only
// renders + jumps to existing ones.
//   cfi   : jump target (value, else cfi, else locator.cfi).
//   where : uppercase "where" line — chapterLabel/label if present.
//   text  : the serif quote (the highlighted text).
//   note  : optional indented note.
//   color : the left edge-rule color (the highlight's own color).
function highlightRow(h) {
    var a = h || {}
    var loc = (a.locator && typeof a.locator === "object") ? a.locator : {}
    var cfi = (a.value !== undefined && a.value !== null) ? String(a.value)
            : (a.cfi !== undefined && a.cfi !== null) ? String(a.cfi)
            : (loc.cfi !== undefined && loc.cfi !== null) ? String(loc.cfi) : ""
    var where = a.chapterLabel ? String(a.chapterLabel)
              : a.label ? String(a.label) : ""
    return { id: a.id !== undefined && a.id !== null ? String(a.id) : "",
             cfi: cfi, where: where,
             text: a.text ? String(a.text) : "",
             note: a.note ? String(a.note) : "",
             color: a.color ? String(a.color) : "" }
}

// authorText(metadata) → a display author string from foliate book metadata, whose
// `author` field may be a plain string, an array of strings, or an array/one of
// { name } objects. Pure normalizer so the chrome shows one clean line.
function authorText(metadata) {
    var a = metadata && metadata.author
    if (!a) return ""
    function nameOf(x) {
        if (!x) return ""
        if (typeof x === "string") return x
        if (typeof x === "object" && x.name) return String(x.name)
        return String(x)
    }
    if (Array.isArray(a)) {
        var parts = []
        for (var i = 0; i < a.length; i++) { var nm = nameOf(a[i]); if (nm) parts.push(nm) }
        return parts.join(", ")
    }
    return nameOf(a)
}
