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

// staleRelocate(eventGen, currentGen) → true when a book-scoped event belongs to a SUPERSEDED
// book open and must be ignored — the cross-book race guard. The glue stamps every book-scoped
// emit with a per-open `gen`: 'relocated', and (as of the hardening pass) 'ready', 'error',
// 'searchResults', 'footnote'. ReaderShell ADOPTS currentGen from a fresh 'ready' and drops any
// event whose gen is OLDER than the current open, so an event from book A still in flight over
// QWebChannel can't land after we've switched to book B (mis-save into B, show A's error /
// footnote / results over B).
//
// STRICTLY-OLDER (`<`), not merely different (`!==`): a 'ready' for a NEWER open (higher gen)
// must be ADOPTED, not dropped — that IS the book switch. A 'relocated' never carries a higher
// gen than currentGen (its own 'ready' set currentGen first, before init's first relocate), so
// `<` and `!==` agree for it; using `<` is what lets the SAME gate also accept a newer 'ready'.
// An event carrying no gen (undefined / non-finite — e.g. a pre-gen glue) is NEVER stale
// (defensive: never suppress a real event just because the stamp is missing).
function staleRelocate(eventGen, currentGen) {
    if (!Number.isFinite(eventGen)) return false
    return eventGen < currentGen
}

// GENERATION OWNERSHIP (re-review #2 rework): QML ISSUES the per-open gen — openAtResume bumps
// currentGen and passes it into paper.open; the glue echoes it on every book-scoped emit. QML
// therefore always knows which gen is "the open I asked for", and the old adopt-a-newer-'ready'
// rule is DEAD (a queued intermediate 'ready' from a superseded slow open could be adopted and
// re-arm bookReady mid-switch — the exact hole Codex found). The gates below all reduce to
// "does this event carry the gen I issued":

// acceptBookEvent(eventGen, currentGen, bookReady) → may a book-scoped DISPLAY/SAVE event
// ('relocated'/'footnote'/'searchResults'/'selection'/'highlightTapped') touch state?
// Accept only when the current open has reached 'ready' AND the event's gen is not from a
// superseded open. Unstamped events (no finite gen) reduce to the bookReady check alone —
// defensive (never gen-dropped), but nothing may paint before the book on screen is the one
// it's for.
function acceptBookEvent(eventGen, currentGen, bookReady) {
    return !!bookReady && !staleRelocate(eventGen, currentGen)
}

// acceptReady(eventGen, currentGen) → is this 'ready' the one the CURRENT open is waiting for?
// EXACT-MATCH, not newest-wins: currentGen is the gen QML issued for the open it asked for, so
// the only acceptable stamped 'ready' is that exact gen — an older one is a superseded open's
// queued 'ready' (drop; adopting it would re-arm bookReady mid-switch), and a newer one was
// never issued (defect — never adopt). An unstamped 'ready' (pre-gen glue / bench) is accepted
// defensively: never suppress the only signal that a book opened.
function acceptReady(eventGen, currentGen) {
    if (!Number.isFinite(eventGen)) return true
    return eventGen === currentGen
}

// errorDisposition(eventGen, currentGen, bookReady) → 'open-fail' | 'operational' | 'drop'.
//
// 'error' cannot use acceptBookEvent: a failed OPEN never reaches 'ready', so its error must
// surface precisely while bookReady is FALSE — but only the error of the open we ISSUED.
// currentGen is set at issue time (openAtResume), so:
//   • gen !== currentGen → 'drop': a superseded open's error (or a gen QML never issued) —
//     never show book A's failure over book B, in any window.
//   • gen === currentGen → this open: 'open-fail' before its 'ready' (show the failed-open
//     surface), 'operational' after (failed search/highlight — trace, don't surface).
//   • no finite gen → pre-gen/boot failure ('boot failed' carries no gen): surface pre-ready,
//     trace when a book is up.
function errorDisposition(eventGen, currentGen, bookReady) {
    if (!Number.isFinite(eventGen)) return bookReady ? "operational" : "open-fail"
    if (eventGen !== currentGen) return "drop"
    return bookReady ? "operational" : "open-fail"
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

// Chrome idle timeout (ms). MANUALLY kept in lock-step with Theme.idleMs (they are two
// separate literals on purpose — a `.pragma library` can't import a QML singleton, so this
// file can't read Theme; change one, change the other). Matched to the comic reader
// (MangaReader.qml) so both readers retreat on the same 3s beat. There is
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

// ---------------------------------------------------------------------------
// TASK 9 (the pen — Round 2) — dictionary + footnote text shaping (pure; headless).
// ---------------------------------------------------------------------------

// stripTags(html) → plain text: drop tags, decode the handful of entities that show up
// in Wiktionary definitions / footnote fragments, collapse whitespace. Pure — no DOM
// (this runs in a .pragma library, where there is no `document`), so the DictCard and
// FootnoteCard get clean strings without parsing HTML in QML.
function stripTags(html) {
    var s = String(html === undefined || html === null ? "" : html)
    // Drop <style>/<script> blocks ENTIRELY (tag AND content) first — Wiktionary
    // definitions embed a <style>.mw-parser-output .defdate{font-size:smaller}</style>,
    // and a bare tag-strip removes the tags but leaves the CSS rules as visible text.
    s = s.replace(/<style\b[^>]*>[\s\S]*?<\/style>/gi, " ")
    s = s.replace(/<script\b[^>]*>[\s\S]*?<\/script>/gi, " ")
    s = s.replace(/<[^>]*>/g, " ")           // drop every remaining tag
    s = s.replace(/&nbsp;/g, " ")
         .replace(/&lt;/g, "<")
         .replace(/&gt;/g, ">")
         .replace(/&quot;/g, '"')
         .replace(/&#39;/g, "'")
         .replace(/&apos;/g, "'")
    s = s.replace(/&#(\d+);/g, function (_, n) { return String.fromCharCode(parseInt(n, 10)) })
    s = s.replace(/&amp;/g, "&")              // last: so "&amp;lt;" → "&lt;" not "<"
    s = s.replace(/\s+/g, " ").trim()
    // A tag became a space, so text like "word</a>." collapses to "word ." — drop the space
    // that now sits before closing punctuation so definitions/footnotes read clean.
    s = s.replace(/\s+([.,;:!?)\]}])/g, "$1")
    return s
}

// firstWord(text) → the word to define. Wiktionary's REST definition endpoint is
// single-word, so a multi-word selection defines its FIRST token. Trims, takes the first
// whitespace-delimited token, and strips surrounding punctuation/quotes while keeping
// internal apostrophes/hyphens (don't, well-being). Pure; ASCII-punctuation only (V4's
// regex has no reliable \p{L} unicode classes).
function firstWord(text) {
    var s = String(text === undefined || text === null ? "" : text).trim()
    if (!s) return ""
    var tok = s.split(/\s+/)[0]
    tok = tok.replace(/^[\"'“”‘’(\[{.,;:!?¿¡…—–\-]+/, "")
             .replace(/[\"'“”‘’)\]}.,;:!?…—–\-]+$/, "")
    return tok
}

// dictParse(json) → normalized definition entries from Wiktionary REST JSON. The endpoint
// returns { "en": [ { partOfSpeech, definitions:[{definition, ...}, ...] }, ... ], ... };
// we read the English ("en") entries only, strip HTML from each definition string, and
// drop empties. Tolerant of a JSON STRING (dictResult hands the raw body across the seam)
// or an already-parsed object. Returns [] on bad/empty input → the DictCard shows its
// "no definition" state. Pure.
function dictParse(json) {
    var data = json
    if (typeof json === "string") {
        try { data = JSON.parse(json) } catch (e) { return [] }
    }
    if (!data || typeof data !== "object") return []
    var en = data.en
    if (!Array.isArray(en)) return []
    var out = []
    for (var i = 0; i < en.length; i++) {
        var entry = en[i] || {}
        var defs = Array.isArray(entry.definitions) ? entry.definitions : []
        var cleaned = []
        for (var j = 0; j < defs.length; j++) {
            var d = defs[j] || {}
            var txt = stripTags(d.definition)
            if (txt) cleaned.push(txt)
        }
        if (cleaned.length)
            out.push({ partOfSpeech: entry.partOfSpeech ? String(entry.partOfSpeech) : "",
                       definitions: cleaned })
    }
    return out
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

// ---------------------------------------------------------------------------
// TASK 10 — appearance model (pure; proven headless).
//
// The APPEARANCE panel (right glass column) is a set of pure knobs over the paper:
// theme swatch / typeface / size / line-spacing / margins / justify, plus the reading
// ruler's CONTROLS (the overlay itself is Task 11). This block holds ALL of it as
// data-in/data-out so ReaderShell can persist it (settings.json → `reader2` sub-object)
// and hand the glue a payload without any QML/Date in the way.
// ---------------------------------------------------------------------------

function clamp_(v, lo, hi) {
    var n = Number(v)
    if (!Number.isFinite(n)) return lo
    return n < lo ? lo : (n > hi ? hi : n)
}

// appearanceDefaults() → the ratified default reader2 appearance (PARITY 2026-07-24:
// full Reader-1 control set; sizePct replaces sizePx — 100% == the old 18px look).
function appearanceDefaults() {
    return {
        theme: "night", font: "literata", sizePct: 100, fontWeight: 400,
        lineHeight: 1.6, marginPx: 72, justify: true, flow: "paginated",
        wordSpacing: 0, letterSpacing: 0, paraSpacing: 0, paraIndent: "book",
        maxLineWidthPx: 960, hyphens: false, columns: "single",
        customPage: "#111214", customInk: "#c9c5bc", customCss: "",
        // GLOBAL keys (reading habits, never per-book): images-in-dark + ruler + read-along.
        invertImages: true,
        rulerOn: false, rulerHeightPx: 92, rulerDimPct: 42, rulerYPct: 40,
        readAlong: { mode: "sentenceWord", wordScale: 1.0 }
    }
}

// themeColors(name) → { bg, fg } for a theme swatch (the four from the mock). Unknown
// names fall back to Night so the paper never renders on a broken/absent theme.
function themeColors(name) {
    switch (String(name || "").toLowerCase()) {
    case "paper": return { bg: "#e9e4d8", fg: "#3a362c" }
    case "sepia": return { bg: "#e5d5b8", fg: "#4a3f2c" }
    case "slate": return { bg: "#232830", fg: "#c6cdd8" }
    case "night": return { bg: "#111013", fg: "#eee9de" }
    default: return { bg: "#111013", fg: "#eee9de" }
    }
}

// fontFamilyFor(name) → the CSS family the glue applies. 'book' = publisher default,
// 'system' = system-ui; a real family name (Literata/Fraunces/Inter) is applied by the
// glue as `* { font-family: NAME !important }` (the paper resolves it via an @font-face).
function fontFamilyFor(name) {
    switch (String(name || "").toLowerCase()) {
    case "literata": return "Literata"
    case "fraunces": return "Fraunces"
    case "inter": return "Inter"
    case "system": return "system"
    case "book": return "book"
    default: return "book"
    }
}

// appearanceToPaper(settings) → the glue payload the paper's setAppearance() takes:
// { theme:{bg,fg}, font, sizePx, lineHeight, marginPx, justify }. Numeric fields are
// CLAMPED to sane ranges here (sizePx 12..26, lineHeight 1.2..2.2, marginPx 24..160) so a
// bad stored value can never break the paper. The ruler fields are intentionally NOT part
// of this payload — they drive the Task 11 overlay, not the paper's text layout.
function appearanceToPaper(settings) {
    var s = settings || {}
    return {
        theme: themeColors(s.theme),
        font: fontFamilyFor(s.font),
        sizePx: clamp_(s.sizePx, 12, 26),
        lineHeight: clamp_(s.lineHeight, 1.2, 2.2),
        marginPx: clamp_(s.marginPx, 24, 160),
        justify: !!s.justify,
        // flow: 'scrolled' is the ONLY non-default; junk or a legacy stored appearance
        // (no flow key) normalizes to 'paginated' so the paper never sees a bad value.
        flow: s.flow === "scrolled" ? "scrolled" : "paginated"
    }
}

// mergeAppearance(prev, patch) → a NEW settings object (prev overlaid by patch), so a
// single control change is a pure update that keeps every other field intact.
function mergeAppearance(prev, patch) {
    var out = {}
    if (prev && typeof prev === "object") for (var k in prev) out[k] = prev[k]
    if (patch && typeof patch === "object") for (var p in patch) out[p] = patch[p]
    return out
}

// migrateAppearance(a) → a full appearance from a possibly-legacy object: new keys are
// filled from defaults, a legacy sizePx (no sizePct) converts losslessly (18px == 100%,
// quantized to the 5% step), and sizePx is dropped from the result.
function migrateAppearance(a) {
    var out = mergeAppearance(appearanceDefaults(), a || {})
    if (a && Number.isFinite(a.sizePx) && !(a && Number.isFinite(a.sizePct)))
        out.sizePct = clamp_(Math.round((a.sizePx / 18) * 100 / 5) * 5, 50, 300)
    delete out.sizePx
    return out
}

// appearanceStore(settingsAll) → the normalized { defaults, books } store from the WHOLE
// settings.json object. Three births: fresh (no reader2), legacy flat (reader2 IS an
// appearance — migrate it into defaults), and the new shape (normalize defaults, keep books).
// The legacy old-old-reader flat `theme` courtesy-seed is preserved from initialAppearance.
function appearanceStore(settingsAll) {
    var s = settingsAll || {}
    var r2 = s.reader2
    if (r2 && typeof r2 === "object" && r2.defaults && typeof r2.defaults === "object") {
        var books = {}
        if (r2.books && typeof r2.books === "object")
            for (var b in r2.books) books[b] = r2.books[b]
        return { defaults: migrateAppearance(r2.defaults), books: books }
    }
    if (r2 && typeof r2 === "object")
        return { defaults: migrateAppearance(r2), books: {} }
    var known = { paper: 1, sepia: 1, slate: 1, night: 1 }
    if (s.theme && known[String(s.theme).toLowerCase()])
        return { defaults: migrateAppearance({ theme: String(s.theme).toLowerCase() }), books: {} }
    return { defaults: appearanceDefaults(), books: {} }
}

// effectiveAppearance(store, bookId) → what this book actually renders with: the defaults
// overlaid by the book's sparse patch (absent/empty patch == the defaults).
function effectiveAppearance(store, bookId) {
    var st = store || { defaults: appearanceDefaults(), books: {} }
    var patch = (bookId && st.books && st.books[bookId]) ? st.books[bookId] : {}
    return mergeAppearance(st.defaults, patch)
}

// GLOBAL appearance keys — reading habits, not book traits: edits write to defaults, never
// into a per-book patch.
var GLOBAL_APPEARANCE_KEYS_ = {
    invertImages: 1, rulerOn: 1, rulerHeightPx: 1, rulerDimPct: 1, rulerYPct: 1, readAlong: 1
}
function isGlobalAppearanceKey(key) { return !!GLOBAL_APPEARANCE_KEYS_[String(key)] }

// applyStorePatch(store, bookId, key, value) → a NEW store with one edit applied to the
// right tier (pure — no mutation of the input).
function applyStorePatch(store, bookId, key, value) {
    var st = store || { defaults: appearanceDefaults(), books: {} }
    var out = { defaults: mergeAppearance(st.defaults, {}), books: {} }
    for (var b in (st.books || {})) out.books[b] = mergeAppearance(st.books[b], {})
    if (isGlobalAppearanceKey(key) || !bookId) {
        out.defaults[key] = value
    } else {
        if (!out.books[bookId]) out.books[bookId] = {}
        out.books[bookId][key] = value
    }
    return out
}

// useAsDefaultStore(store, bookId) → this book's effective appearance becomes the global
// default; its own patch clears (it now IS the default). Other books keep their tuning.
function useAsDefaultStore(store, bookId) {
    var st = store || { defaults: appearanceDefaults(), books: {} }
    var out = { defaults: effectiveAppearance(st, bookId), books: {} }
    for (var b in (st.books || {})) if (b !== bookId) out.books[b] = mergeAppearance(st.books[b], {})
    return out
}

// resetBookStore(store, bookId) → drop this book's patch; it falls back to the defaults.
function resetBookStore(store, bookId) {
    var st = store || { defaults: appearanceDefaults(), books: {} }
    var out = { defaults: mergeAppearance(st.defaults, {}), books: {} }
    for (var b in (st.books || {})) if (b !== bookId) out.books[b] = mergeAppearance(st.books[b], {})
    return out
}

// initialAppearance(settings) → KEPT for existing callers/tests: the defaults-tier
// appearance the store yields for this settings object (legacy flat reader2 included).
function initialAppearance(settings) {
    return effectiveAppearance(appearanceStore(settings), "")
}

// ---------------------------------------------------------------------------
// TASK 11 — search-row shaping + reading-ruler geometry (pure; proven headless).
// ---------------------------------------------------------------------------

// escapeHtml_(s) → HTML-safe text so an excerpt piece can be dropped into a StyledText
// string without a stray '<' or '&' breaking the markup. Order matters: '&' first.
function escapeHtml_(s) {
    return String(s === undefined || s === null ? "" : s)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
}

// searchExcerpt(excerpt) → normalized { pre, match, post } strings. The glue passes the
// foliate excerpt shape { pre, match, post }; tolerate a missing/partial object or a bare
// string (older shape) by putting the whole string in `match` so it still renders.
function searchExcerpt(excerpt) {
    var e = excerpt
    if (e && typeof e === "object")
        return { pre: String(e.pre || ""), match: String(e.match || ""), post: String(e.post || "") }
    return { pre: "", match: String(e === undefined || e === null ? "" : e), post: "" }
}

// searchRowStyled(excerpt, goldHex) → a StyledText string: pre + the matched substring in
// GOLD + post, every piece HTML-escaped. The accent color is PASSED IN from QML (Theme.gold)
// so the gold token stays in QML, not hard-coded here. Pure — no engine, no DOM.
function searchRowStyled(excerpt, goldHex) {
    var e = searchExcerpt(excerpt)
    var g = String(goldHex === undefined || goldHex === null ? "#F0C24A" : goldHex)
    return escapeHtml_(e.pre)
         + '<font color="' + g + '">' + escapeHtml_(e.match) + '</font>'
         + escapeHtml_(e.post)
}

// searchCountText(count, capped) → the sheet's result-count label. Capped searches show
// "300+ results" (we stopped collecting at the cap), else "N result(s)". Pure.
function searchCountText(count, capped) {
    var n = Number.isFinite(count) ? count : 0
    if (capped) return n + "+ results"
    return n + (n === 1 ? " result" : " results")
}

// rulerGeometry(yPct, heightPx, overlayH) → the reading-ruler band + scrim layout, in px.
// The band is a horizontal focus stripe of height `heightPx` whose TOP sits at yPct% down
// the overlay (matching the mock's `.ruler{top:41%}`); the dimmed scrims fill everything
// above and below it. The band is CLAMPED to stay fully on-screen (so a yPct near 100 or a
// tall band never runs off the bottom). Pure — the RulerOverlay binds Rectangles to this.
//   returns { bandTop, bandHeight, topScrimH, botScrimH }
function rulerGeometry(yPct, heightPx, overlayH) {
    var H = Number.isFinite(overlayH) && overlayH > 0 ? overlayH : 0
    var bh = clamp_(heightPx, 0, H)
    var y = clamp_(yPct, 0, 100)
    var top = (y / 100) * H
    if (top + bh > H) top = H - bh    // keep the band fully on-screen
    if (top < 0) top = 0
    var botTop = top + bh
    return { bandTop: top, bandHeight: bh, topScrimH: top, botScrimH: H - botTop }
}

// ---------------------------------------------------------------------------
// TASK 13 — read-along chapter matching (pure; proven headless).
//
// The reader's Audio tab keeps a paired audiobook in step with the page. When
// "Follow my reading" is on, every page turn asks: which AUDIOBOOK chapter matches
// the BOOK chapter I'm now in? chapterFor answers it with ORIGINAL 3-tier matching
// (title / roman / proportional) written for reader2 — inspired by the pairing concept,
// NOT ported: TB-Max had only a manual chapter dropdown + a naive ordinal, no title or
// roman-numeral matching and no proportional fallback. The three tiers:
//   (1) title match  — normalize both titles and pair by text, else by a shared
//        leading chapter NUMBER (so book "Chapter 3" ↔ audio "3. The Spouter-Inn");
//   (2) ordinal      — book chapter N → audio chapter N, when that index exists;
//   (3) proportional — bookTocIndex/bookToc.length scaled onto audioChapters.length,
//        the safety net when the counts differ (a book with more chapters than the
//        audiobook has files/chapters).
// Returns -1 when there is nothing to sync to (no audio chapters) or the book
// chapter is unknown (index < 0). PURE — no Date, no QML, no DOM.
// ---------------------------------------------------------------------------

var ROMAN_ = { i: 1, v: 5, x: 10, l: 50, c: 100, d: 500, m: 1000 }

// romanToArabic_("iv") → 4, or NaN for a non-roman token. Lowercase, subtractive.
function romanToArabic_(s) {
    var t = String(s || "").toLowerCase()
    if (!t || !/^[ivxlcdm]+$/.test(t)) return NaN
    var total = 0, prev = 0
    for (var k = t.length - 1; k >= 0; k--) {
        var v = ROMAN_[t[k]]
        if (!v) return NaN
        if (v < prev) total -= v
        else { total += v; prev = v }
    }
    return total
}

// chapterLabelOf_(entry) → the display title of a toc/chapter entry, tolerating a
// bare string OR an object with .label / .title (book toc uses label; the audiobook
// session's chapterModel uses label too).
function chapterLabelOf_(x) {
    if (x === undefined || x === null) return ""
    if (typeof x === "string") return x
    if (typeof x === "object") return String(x.label || x.title || "")
    return String(x)
}

// chapterKey_(title) → { text, num }. text = the fully normalized comparable string
// (lowercase, a leading section word dropped, punctuation → spaces, whitespace
// collapsed); num = the chapter's leading number as an integer (arabic digits, or a
// leading roman numeral), or NaN when the title carries no leading number.
function chapterKey_(title) {
    var s = String(title === undefined || title === null ? "" : title).toLowerCase().trim()
    // drop a single leading section word ("chapter"/"ch."/"part"/"book"/"section").
    s = s.replace(/^(chapters?|chapitre|ch\.?|parts?|books?|sections?)\s+/, "")
    var text = s.replace(/[^a-z0-9]+/g, " ").replace(/\s+/g, " ").trim()
    var num = NaN
    var mNum = text.match(/^(\d+)\b/)
    if (mNum) num = parseInt(mNum[1], 10)
    else {
        var mRom = text.match(/^([ivxlcdm]+)\b/)
        if (mRom) num = romanToArabic_(mRom[1])
    }
    return { text: text, num: num }
}

// chapterFor(bookTocIndex, bookToc, audioChapters) → the best-matching audiobook
// chapter index (see the block header for the three tiers). Pure.
function chapterFor(bookTocIndex, bookToc, audioChapters) {
    var audio = (audioChapters && audioChapters.length) ? audioChapters : []
    var toc = (bookToc && bookToc.length) ? bookToc : []
    var n = audio.length
    if (n === 0) return -1                                  // nothing to sync to
    var i = Number(bookTocIndex)
    if (!Number.isFinite(i) || i < 0) return -1            // unknown book chapter

    // precompute the audio chapter keys once.
    var akeys = []
    for (var a = 0; a < n; a++) akeys.push(chapterKey_(chapterLabelOf_(audio[a])))

    // (1) title match — by normalized text first, then by a shared leading number.
    if (i < toc.length) {
        var bk = chapterKey_(chapterLabelOf_(toc[i]))
        if (bk.text) {
            for (var t1 = 0; t1 < n; t1++)
                if (akeys[t1].text && akeys[t1].text === bk.text) return t1
        }
        if (Number.isFinite(bk.num)) {
            for (var t2 = 0; t2 < n; t2++)
                if (akeys[t2].num === bk.num) return t2
        }
    }

    // (2) ordinal — same position, when the audiobook has a chapter there.
    if (i < n) return i

    // (3) proportional — the book ran past the audiobook's chapter count.
    var len = toc.length > 0 ? toc.length : (i + 1)
    var idx = Math.floor(i / len * n)
    if (idx < 0) idx = 0
    if (idx >= n) idx = n - 1
    return idx
}

// audiobookMetaLine(chapterCount, totalSeconds) → the card's subtitle. Always names
// the chapter count; prepends an "N h MM m" (or "MM min") duration ONLY when a real
// total is known (a single-file m4b exposes it; a multi-file set does not until every
// file is probed, so we honestly omit it). Pure — the seconds come from the session.
function audiobookMetaLine(chapterCount, totalSeconds) {
    var c = Number(chapterCount)
    if (!Number.isFinite(c) || c < 0) c = 0
    var chapters = c + (c === 1 ? " chapter" : " chapters")
    var t = Number(totalSeconds)
    if (!Number.isFinite(t) || t <= 0) return chapters
    var mins = Math.floor(t / 60)
    var h = Math.floor(mins / 60)
    var m = mins % 60
    var dur = h > 0 ? (h + " h " + (m < 10 ? "0" : "") + m + " m") : (m + " min")
    return dur + " · " + chapters
}

// audiobookTimeLine(chapterLabel, positionSec, durationSec) → the transport's info
// line: "<chapter> · MM:SS / MM:SS". Omits the times until a duration is known, and
// the chapter label when empty. Pure (its own hh:mm:ss formatter; no Date).
function fmtClock_(sec) {
    var t = Math.max(0, Math.floor(Number(sec) || 0))
    var h = Math.floor(t / 3600), m = Math.floor((t % 3600) / 60), s = t % 60
    function pad(x) { return (x < 10 ? "0" : "") + x }
    return (h > 0 ? (h + ":" + pad(m)) : m) + ":" + pad(s)
}
function audiobookTimeLine(chapterLabel, positionSec, durationSec) {
    var label = String(chapterLabel === undefined || chapterLabel === null ? "" : chapterLabel).trim()
    var d = Number(durationSec)
    var times = (Number.isFinite(d) && d > 0)
        ? (fmtClock_(positionSec) + " / " + fmtClock_(durationSec)) : ""
    if (label && times) return label + " · " + times
    return label || times
}

// speedLabel(rate) → the speed pill text, e.g. 1 → "1.0×", 1.25 → "1.25×". Trims a
// trailing zero on the hundredths so 1.50 reads "1.5×" but 1.0 keeps one decimal. Pure.
function speedLabel(rate) {
    var r = Number(rate)
    if (!Number.isFinite(r) || r <= 0) r = 1
    var s = r.toFixed(2)
    s = s.replace(/(\.\d)0$/, "$1")   // 1.50 -> 1.5, but 1.00 -> 1.0
    return s + "×"
}

// ---------------------------------------------------------------------------
// TASK 6 — audiobook↔EPUB read-along WIRING decisions (pure; proven headless).
//
// ReaderShell can't be instantiated offscreen (it needs the WebEngine paper + a dozen
// context singletons), so EVERY read-along decision lives here as data-in / data-out and
// ReaderShell's handlers are thin callers. That keeps the wiring honest AND testable:
// tests/reader2_readalong_harness.qml proves these functions and wires them to fake
// ReadAlong/paper/audioSession objects exactly as ReaderShell does. When the native
// `ReadAlong`/`AudioTextAlignment` context props are ABSENT the reader is DORMANT — every
// function here has a dormant answer (a plain audio seek), so nothing read-along fires.
// ---------------------------------------------------------------------------

// The three read-along modes (Tankoban-Max parity). Sentence + Word is the ratified default.
function readAlongModeValid_(mode) {
    return mode === "sentence" || mode === "word" || mode === "sentenceWord"
}
function readAlongDefaults() { return { mode: "sentenceWord", wordScale: 1.0 } }

// readAlongFrom(appearance) → the persisted { mode, wordScale }, read back from the SAME
// `settings.reader2` object appearance is stored in (its `.readAlong` sub-key), merged over
// the defaults + validated. A missing/junk value falls back so the paper never sees garbage.
// (Persisting read-along under `appearance.readAlong` reuses the appearance store round-trip:
// applyAppearancePatch writes `all.reader2 = appearance` wholesale, so nesting here means the
// two never clobber each other — exactly as the ruler controls ride along in `appearance`.)
function readAlongFrom(appearance) {
    var a = appearance || {}
    var ra = (a.readAlong && typeof a.readAlong === "object") ? a.readAlong : {}
    var mode = readAlongModeValid_(ra.mode) ? ra.mode : "sentenceWord"
    var scale = Number.isFinite(ra.wordScale) ? clamp_(ra.wordScale, 1.0, 2.0) : 1.0
    return { mode: mode, wordScale: scale }
}

// mergeReadAlong(appearance, patch) → a NEW appearance object with `.readAlong` updated by
// `patch` (mode and/or wordScale), validated + clamped. Pure — every other appearance field
// is copied through untouched so a read-along edit never disturbs theme/font/ruler/etc.
function mergeReadAlong(appearance, patch) {
    var out = mergeAppearance({}, appearance)     // shallow copy of the whole appearance
    var cur = readAlongFrom(appearance)
    var next = { mode: cur.mode, wordScale: cur.wordScale }
    var p = patch || {}
    if (p.mode !== undefined && p.mode !== null) next.mode = readAlongModeValid_(p.mode) ? p.mode : next.mode
    if (p.wordScale !== undefined && p.wordScale !== null && Number.isFinite(Number(p.wordScale)))
        next.wordScale = clamp_(p.wordScale, 1.0, 2.0)
    out.readAlong = next
    return out
}

// readAlongStyleFromMode(mode, wordScale) → the paper.setReadAlongStyle() payload:
// { mode, sentence, word, wordScale }. Sentence lights the sentence wash, Word lights the
// word emphasis, Sentence + Word lights both. wordScale is the enlargement (1.0..2.0). Pure.
function readAlongStyleFromMode(mode, wordScale) {
    var m = readAlongModeValid_(mode) ? mode : "sentenceWord"
    var scale = Number.isFinite(Number(wordScale)) ? clamp_(wordScale, 1.0, 2.0) : 1.0
    return { mode: m, sentence: m !== "word", word: m !== "sentence", wordScale: scale }
}

// scrubFractionToTimeMs(fraction, durationSec) → an absolute time IN THE CURRENT STREAM, in
// ms, for previewTime/commitTime. The gold scrub rail's fraction is over the session's
// current-stream duration (position/duration), so this stays in the same "per-current-stream
// ms" convention as sessionToAbsMs below. Clamped; a zero/absent duration yields 0. Pure.
function scrubFractionToTimeMs(fraction, durationSec) {
    var f = clamp_(fraction, 0, 1)
    var d = (Number.isFinite(durationSec) && durationSec > 0) ? durationSec : 0
    return Math.round(f * d * 1000)
}

// sessionToAbsMs(index, positionSec, chapterBoundsMs) → the audiobook time to feed the
// controller. When REAL chapter start offsets are known (Task 12 supplies chapterBoundsMs),
// absolute = boundsMs[index] + position*1000. Absent them we pass position*1000 — exactly
// correct for a single-file m4b (the primary read-along case, where the whole book is one
// stream and `index` is an mpv-chapter marker, not a stream offset), and a consistent
// per-file value for a multi-file set (the controller is fed the SAME convention on both the
// feed and the commit, so the round-trip is coherent until Task 12 lands real durations). Pure.
function sessionToAbsMs(index, positionSec, chapterBoundsMs) {
    var i = Number(index)
    var base = (chapterBoundsMs && Number.isFinite(chapterBoundsMs[i])) ? chapterBoundsMs[i] : 0
    var pos = Number.isFinite(positionSec) ? positionSec : 0
    return Math.round(base + pos * 1000)
}

// audioSeekTargetSec(chapter, timeMs, chapterBoundsMs) → the inverse of sessionToAbsMs: the
// SECONDS to seek within the target chapter's stream when the controller's audioSeekRequested
// asks for absolute `timeMs`. Never negative. Pure.
function audioSeekTargetSec(chapter, timeMs, chapterBoundsMs) {
    var c = Number(chapter)
    var base = (chapterBoundsMs && Number.isFinite(chapterBoundsMs[c])) ? chapterBoundsMs[c] : 0
    var t = Number.isFinite(timeMs) ? timeMs : 0
    return Math.max(0, (t - base) / 1000)
}

// shouldEmitSetPlayhead(prev, next) → did the playhead-relevant identity change? prev/next
// are { chapter, absMs }. Emit on the first playhead (prev null) or whenever the chapter or
// the time moved — this suppresses ONLY true no-ops (a re-notify with an identical position,
// e.g. a paused stream re-emitting). The controller itself further no-ops when the resolved
// sentence/word is unchanged, so feeding every real tick is cheap and correct. Pure.
function shouldEmitSetPlayhead(prev, next) {
    if (!prev || typeof prev !== "object") return true
    var n = next || {}
    return prev.chapter !== n.chapter || prev.absMs !== n.absMs
}

// previewLabelFrom(previewMap) → the scrub-preview view model (timestamp + chapter + the
// synced/located flags), shown while dragging the rail WITHOUT seeking. previewMap is the
// controller's `preview`: { timeMs, chapter, synced, spineHref?, canonicalStart?, ... }.
// Returns { line, time, chapter, synced, located }; `line` is the ready-to-render string.
// Pure — no engine, its own clock formatter (fmtClock_).
function previewLabelFrom(previewMap) {
    var p = previewMap || {}
    var time = Number.isFinite(p.timeMs) ? fmtClock_(p.timeMs / 1000) : ""
    var chapter = Number.isFinite(p.chapter) ? ("Ch " + (Number(p.chapter) + 1)) : ""
    var synced = !!p.synced
    var located = (p.spineHref !== undefined && p.spineHref !== null && String(p.spineHref) !== "")
    var parts = []
    if (time) parts.push(time)
    if (chapter) parts.push(chapter)
    return { line: parts.join(" · "), time: time, chapter: chapter, synced: synced, located: located }
}

// readAlongScrubAction(phase, fraction, durationSec, available) → what the gold scrub rail
// should DO for a hover/drag ("preview") or a release ("commit"). When read-along is
// AVAILABLE: preview → { kind:"preview", timeMs } (NO seek), commit → { kind:"commit",
// timeMs } (exactly one controller commit). When DORMANT: always { kind:"seek", fraction }
// — the reader's existing direct-seek behavior, byte-for-byte. Pure — the ONE branch that
// makes the rail an aligned timeline when available and an ordinary scrub when not.
function readAlongScrubAction(phase, fraction, durationSec, available) {
    if (!available) return { kind: "seek", fraction: clamp_(fraction, 0, 1) }
    var timeMs = scrubFractionToTimeMs(fraction, durationSec)
    if (phase === "commit") return { kind: "commit", timeMs: timeMs }
    return { kind: "preview", timeMs: timeMs }
}

// navModeFor(pendingCommittedJump) → how to honor the controller's navigationRequested: a
// jump we just committed (double-click / scrub release) is a hard "navigate"; a passive
// follow move is a gentle "ensureVisible" (comfort zone). ReaderShell sets the pending flag
// on a commit and clears it when the next navigationRequested consumes it. Pure.
function navModeFor(pendingCommittedJump) { return pendingCommittedJump ? "navigate" : "ensureVisible" }

// ---------------------------------------------------------------------------
// TASK 7 — Text Sync status COPY (pure; proven headless).
//
// Honest presentation of the native AudioTextAlignmentService's OWN status. The LeftPanel
// Text Sync block is a THIN renderer of these: it never re-derives stage or progress, it
// only formats the numbers/codes the service already decided (statusFor -> {stage, ready,
// total, paused}; chaptersFor -> [{index, stage, failureCode, ...}]). The stage and failure
// WIRE CODES are the service's stable contract (design Task 3); QML maps them here to the
// approved plain-language copy and never invents alternate meanings. Everything is data-in /
// data-out so tests/alignment_activity_harness.qml proves the copy directly.
// ---------------------------------------------------------------------------

// stageLabel(stage) → the plain display label for a stage wire code. An unknown/absent code
// falls back to a safe generic so a future or garbage stage never renders blank.
function stageLabel(stage) {
    switch (String(stage === undefined || stage === null ? "" : stage)) {
    case "waiting":      return "Waiting"
    case "preparing":    return "Preparing"
    case "transcribing": return "Transcribing"
    case "matching":     return "Matching"
    case "aligning":     return "Aligning words"
    case "ready":        return "Ready"
    case "couldnt_sync": return "Couldn't sync"
    default:             return "Syncing"
    }
}

// chapterFailureCopy(code) → the approved plain-language line for a terminal failure wire
// code (design Task 3's failure map). QML renders this verbatim on a failed chapter; an
// empty/unknown code is the generic "Couldn't sync". Pure.
function chapterFailureCopy(code) {
    switch (String(code === undefined || code === null ? "" : code)) {
    case "edition_mismatch":       return "Couldn't sync — edition may differ"
    case "chapter_match_missing":  return "Couldn't sync — no matching passage found"
    case "audio_decode_failed":    return "Couldn't sync — audio couldn't be read"
    case "model_missing":          return "Couldn't sync — speech model missing"
    case "model_checksum_failed":  return "Couldn't sync — speech model is damaged"
    case "epub_index_failed":      return "Couldn't sync — book text couldn't be read"
    case "alignment_failed":       return "Couldn't sync — words couldn't be timed"
    default:                       return "Couldn't sync"
    }
}

// textSyncAllReady(status) → is every chapter aligned? True when the overall stage is
// 'ready' OR the ready count reached the total (and a total is known). Pure.
function textSyncAllReady(status) {
    var s = status || {}
    var ready = Number.isFinite(s.ready) ? s.ready : 0
    var total = Number.isFinite(s.total) ? s.total : 0
    return s.stage === "ready" || (total > 0 && ready >= total)
}

// textSyncSummary(status) → the one-line honest summary. Every chapter aligned → "All N
// chapters ready". Otherwise "Syncing chapter K of N · <stage>", where K = the chapter now
// in flight (chapters completed + 1, clamped to N) and <stage> is its stage label; a PAUSED
// job says "Paused" in place of "Syncing" (never claim work is happening while it isn't).
// An unknown total (nothing discovered yet) → "Preparing text sync". Pure — reads only the
// service's own {stage, ready, total, paused}.
function textSyncSummary(status) {
    var s = status || {}
    var ready = Number.isFinite(s.ready) ? s.ready : 0
    var total = Number.isFinite(s.total) ? s.total : 0
    if (total <= 0) return "Preparing text sync"
    if (textSyncAllReady(s)) return "All " + total + " chapters ready"
    var current = ready + 1
    if (current > total) current = total
    var head = (s.paused ? "Paused" : "Syncing") + " chapter " + current + " of " + total
    var label = stageLabel(s.stage)
    return label ? (head + " · " + label) : head
}

// readyCountText(status) → "K chapters ready" (singular "1 chapter ready"). Pure.
function readyCountText(status) {
    var s = status || {}
    var ready = Number.isFinite(s.ready) ? s.ready : 0
    return ready + (ready === 1 ? " chapter ready" : " chapters ready")
}

// chapterFailed(chapter) → did this chapter's alignment fail terminally? True when its stage
// is 'couldnt_sync' or it carries a non-empty failure code. A failed chapter stays playable
// as ordinary audio — this only gates the failure copy + the Retry affordance. Pure.
function chapterFailed(chapter) {
    var c = chapter || {}
    if (c.stage === "couldnt_sync") return true
    return c.failureCode !== undefined && c.failureCode !== null && String(c.failureCode) !== ""
}

// chapterStateText(chapter) → the per-chapter row label: the plain failure line when it
// failed, "Ready" when aligned, else its stage label. Pure. Renders any of the seven states.
function chapterStateText(chapter) {
    var c = chapter || {}
    if (chapterFailed(c)) return chapterFailureCopy(c.failureCode)
    if (c.stage === "ready") return "Ready"
    return stageLabel(c.stage)
}
