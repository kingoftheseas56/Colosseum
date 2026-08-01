.pragma library

// Comic Reader — pure decision logic (Task 8).
//
// Every function here is a pure function of its arguments only: NO reference to any QML
// context property (`Progress`, `Downloads`, `Comics`, `TankobanVolumes`, ...) or component
// object (`ComicReaderCore`, any `id`) by name — a `.pragma library` script cannot see those
// (house law: reference_pragma_library_cant_see_context_properties). The Comic Reader shell
// (Task 9) calls these for every crossing/completion/progress/acquisition decision and supplies
// whatever state each function needs as plain arguments.
//
// Ground truth: qml/MangaReader.qml (the current reader being rebuilt) and the Task 1 contract,
// docs/superpowers/handoffs/2026-07-23-comicreader-public-contract.md. Where this file
// intentionally departs from that reader's TODAY behavior (a forward-looking "smart default"
// rather than a byte-for-byte port), the function comment says so explicitly.

// --- progress namespace -----------------------------------------------------------------
// Mirrors qml/MangaReader.qml's `progressKind` (~line 64): western never sets entryKind but
// keeps its "comic" namespace; every other caller's entryKind IS the namespace ("manga"
// chapters, "tankoban" volumes). This guarantees a volume record and a chapter record for the
// same series can never overwrite each other (contract §5).
function progressKind(entryKind, western) {
    return (entryKind === "manga" && western) ? "comic" : entryKind
}

// --- newest-first crossing ---------------------------------------------------------------
// Mirrors MangaReader.qml's `curIndex` (~line 165-169): first array slot whose id matches.
function entryIndex(chapters, entryId) {
    if (!chapters) return -1
    for (var i = 0; i < chapters.length; i++) {
        if (chapters[i] && String(chapters[i].id) === String(entryId)) return i
    }
    return -1
}

// Mirrors `hasNewer`/`goNextChapter` (~lines 177, 456-461): newest-first, so index-1 is the
// next (newer) entry. Tankoban volumes ride the SAME math — the series page hands the reader a
// DESCENDING (highest-first) volume copy, so index-1 reads as the next HIGHER volume.
function nextEntry(chapters, currentIndex) {
    if (!chapters || currentIndex === null || currentIndex === undefined) return null
    if (currentIndex <= 0 || currentIndex >= chapters.length) return null
    return chapters[currentIndex - 1]
}

// Mirrors `hasOlder`/`goPrevChapter` (~lines 178, 472-477): index+1 is the previous (older)
// entry — the next LOWER tankoban volume, in the same descending-copy sense as nextEntry.
function previousEntry(chapters, currentIndex) {
    if (!chapters || currentIndex === null || currentIndex === undefined) return null
    if (currentIndex < 0 || currentIndex >= chapters.length - 1) return null
    return chapters[currentIndex + 1]
}

// --- reading completion --------------------------------------------------------------------
// Mirrors the fraction/finished values MangaReader.qml stamps into a Progress record
// (recordProgress, ~lines 210-233): fraction clamped to [0,1]; finished at the high-water mark
// (maxSeen), NOT the current page — re-reading a finished chapter must not un-finish it
// (Key_R comment, ~line 888: "leaves maxSeen alone").
function completion(page, pageCount, maxSeen) {
    var n = Number(pageCount) || 0
    var fraction = n > 0 ? Math.min(1, Math.max(0, Number(page) / n)) : 0
    var finished = n > 0 && Number(maxSeen) >= n
    return { fraction: fraction, finished: finished }
}

// --- Progress.record(...) payload (LOAD-BEARING) --------------------------------------------
// Reproduces qml/MangaReader.qml:220-233 byte-for-byte (contract §4.1): same keys, the same
// literal colors, the same nested `resume` shape, the same progress clamp. `args` carries
// everything the real call reads off `reader.*` at record time:
//   seriesId, kind (progressKind result), seriesTitle, label (curLabel), cover (already
//   back-filled by the CALLER per lines 214-219 — that clobber-guard is caller policy, not this
//   pure function's job), page, max, chapterId (curChapterId), style, scrollFrac (reader.stripFrac()
//   already computed by the caller when in long_strip — this function decides WHETHER to use it),
//   pageFraction (Task 11: how far down `page` the viewport centre sat — the WITHIN-page anchor,
//   likewise only used in long_strip), maxSeen.
//
// PRECONDITION (byte-for-byte with the original — do NOT add a guard here): `args.page`,
// `args.max`, and `args.maxSeen` MUST be numbers, and the CALLER must ensure `max > 0` before
// calling, exactly as MangaReader.qml:211 does (`if (... || reader.max <= 0) return`) BEFORE
// it ever reaches the record() call this function reproduces. This function does the SAME raw
// `page / max` division the original does with no internal `max > 0` guard (unlike
// `completion()` above, which is a new pure helper free to guard); calling it with `max === 0`
// is caller error and reproduces the original's undefined-at-the-boundary behavior
// (`progress` NaN-or-1-ish, `resume.finished` true via `maxSeen >= 0`) rather than masking it —
// see the harness's pinned max=0 case for the exact documented output.
function progressPayload(args) {
    var a = args || {}
    return {
        "id": a.seriesId,
        "kind": a.kind,
        "caption": a.seriesTitle,
        "title": a.seriesTitle,
        "sub": a.label,
        "cover": a.cover,
        "c1": "#3a2f55", "c2": "#15111f",
        "progress": Math.min(1, Math.max(0, a.page / a.max)),
        "resume": {
            "chapterId": a.chapterId,
            "page": a.page,
            "scrollFrac": a.style === "long_strip" ? a.scrollFrac : 0,
            // WITHIN the page, not within the book (Task 11). The approved line is "Long Strip
            // records the current page plus position within that page. Returning to a book should
            // therefore land on the same panel area instead of only the approximate page."
            //
            // scrollFrac above cannot do that job and is kept only because records written before
            // this field existed carry it. It is a fraction of the WHOLE COLUMN, so it is only as
            // accurate as the column is stable: change the portrait width or the page gap and every
            // page's share of the column moves, so the same 0.41 lands somewhere else entirely.
            // page + pageFraction is anchored to the paper — it survives a reflow, and it is the
            // exact quantity the strip surface already measures for presented().
            //
            // Zeroed outside Long Strip for the same reason scrollFrac is: a page IS the viewport's
            // whole travel in the paged layouts, so there is no "part way down it" to record, and
            // storing a stale one would let a layout switch resume half a page off.
            "pageFraction": a.style === "long_strip" ? _clamp01(a.pageFraction) : 0,
            "maxSeen": a.maxSeen,
            "finished": a.maxSeen >= a.max
        }
    }
}
// Total: a record is persisted state, so an absent / hand-edited / future-version value must
// degrade to "no opinion" (0) rather than writing NaN into the store.
function _clamp01(v) {
    var n = Number(v)
    if (!isFinite(n)) return 0
    return Math.min(1, Math.max(0, n))
}

// --- reading direction default --------------------------------------------------------------
// Design ruling (docs/superpowers/specs/2026-07-23-comicreader-design.md #3 — "smart default +
// toggle"): kind decides the default. manga AND tankoban (both Japanese-lineage reading order)
// open RTL; western comics open LTR. NOTE: this is a forward-looking decision for the Comic
// Reader rebuild, NOT a port of MangaReader.qml's current behavior — today's reader has one
// single global default ("right_left") for every lane (prefs.reading_direction), with no
// western-specific override anywhere in the file.
// `entryKind` is reserved for a future per-lane ruling; currently unused (western alone decides).
function defaultDirection(entryKind, western) {
    return western ? "ltr" : "rtl"
}

// --- default reading mode per lane -----------------------------------------------------------
// ASSUMPTION (flag for confirmation): docs/superpowers/specs/2026-07-23-comicreader-design.md
// rules "two modes only: Long Strip and Double Page" but does NOT rule a per-lane default the
// way it does for direction (#3). Today's MangaReader.qml has exactly one global default
// (prefs.reading_style: "long_strip") shared by every lane — manga, western, tankoban alike —
// so this keeps that single default until a per-lane ruling lands. Revisit if Hemanth calls a
// split (e.g., western issues defaulting to Double Page, book-like tankoban volumes too).
// Both `entryKind` and `western` are reserved for that future per-lane ruling; currently unused.
function defaultMode(entryKind, western) {
    return "long_strip"
}

// --- acquisition gate ------------------------------------------------------------------------
// True only while the entry hasn't reached a terminal "ready" state. The two page stores use
// different literal state strings for "ready": native/engine/MangaDownloader.cpp (manga
// chapters + western comics) returns "done"; native/engine/MangaTankobanService.cpp (Tankoban
// volumes) returns "ready". Every other state — missing status, "none", "queued",
// "downloading", "resolving", "extracting", "ingesting", "packing", "failed" — still needs
// acquisition (matches the in-flight-state set MangaReader.qml treats as downloading, contract
// §3, plus the terminal-failure and not-yet-started cases it doesn't enumerate there).
function shouldAcquire(status) {
    if (!status) return true
    var s = status.state
    return s !== "done" && s !== "ready"
}

// --- reading mode (Hemanth ruling 2026-07-25) ------------------------------------------------
// ONE user-facing identity replaces the old orthogonal mode(double/strip) + direction(rtl/ltr)
// toggles. Three peers, each baking in BOTH its page layout AND its reading direction:
//   "manga" -> RTL double-page (the MangaPlus manga default)
//   "comic" -> LTR double-page
//   "strip" -> vertical continuous scroll (direction-neutral)
// The shell keeps the low-level `mode` + `rtl` the surfaces/input consume, and DERIVES them from
// the readingMode via readingModeLayout()/readingModeRtl(). Coupling stays user-nudgeable (P).
// Everything fails SAFE to comic-like (double-page, LTR) so a bad persisted value never wedges.
function defaultReadingMode(entryKind, western) {
    return western ? "comic" : "manga"   // manga chapters + tankoban volumes open as Manga
}
function readingModeLayout(rm) {
    return rm === "strip" ? "long_strip" : "double_page"
}
function readingModeRtl(rm) {
    return rm === "manga"                 // only manga reads right-to-left
}
// reverse map, so a legacy (mode, rtl) pair — or a HUD that flipped mode/rtl — resolves back to
// the single identity. long_strip is always "strip" regardless of direction.
function readingModeFrom(layout, rtl) {
    if (layout === "long_strip") return "strip"
    return rtl ? "manga" : "comic"
}

// --- layout + order: two INDEPENDENT choices (Task 3, plan 2026-07-28) -----------------------
// Hemanth's ruling for the overhaul: Colosseum's reading model is stronger than both reference
// apps' precisely because it keeps these apart, and it must not be weakened.
//   ORDER  — the physical page ORDERING: comic = "ltr", manga = "rtl".
//   LAYOUT — presentation ONLY: "single_page" | "paired_pages" | "long_strip".
// Changing the layout must never change the direction, and choosing a direction must never throw
// you out of Long Strip. The combined readingMode identity above (manga/comic/strip) could not
// express that: "strip" meant LTR by construction, and there was no way to say "single page" at
// all. These two fields are the persisted truth from here on; readingMode* survive only as the
// compatibility mapping for the surfaces/chrome that have not moved to the Layout menu yet.
var LAYOUTS = ["single_page", "paired_pages", "long_strip"]

function layoutIsValid(value) { return LAYOUTS.indexOf(value) >= 0 }
function orderIsValid(value)  { return value === "ltr" || value === "rtl" }
// The layout half of a legacy combined identity. "strip" is the vertical flow; manga and comic
// were BOTH double-page, so both land on paired pages — the identity never carried single page.
function layoutFromReadingMode(rm) { return rm === "strip" ? "long_strip" : "paired_pages" }

// One numeric persisted value, clamped into its approved range, with the approved default for
// anything missing or unreadable. `Number(...) || fallback` deliberately catches undefined, null,
// "", NaN and 0 — a stored 0 is out of range for every field here, so the default is the honest
// answer rather than the low clamp.
function _clampNumber(value, lo, hi, fallback) {
    var n = Number(value) || fallback
    return Math.max(lo, Math.min(hi, n))
}

// TOTAL migration of ONE persisted reader record into the layout/order model.
//
// "Total" means: every input — {}, null, a record written by the shipped reader, a half-written or
// hand-edited one — comes back as a complete, in-range preference set, and NEVER throws. A settings
// blob survives crashes, upgrades and hand edits; it is not a trustworthy input.
//
// Legacy shapes understood, most specific first:
//   * the new fields            {layout, order}
//   * the SHIPPED series record {rm: "manga"|"comic"|"strip"}  (_saveSeriesPrefs's terse key)
//   * the same identity, long   {readingMode: ...}
//   * a raw layout+direction    {mode: "long_strip"|"double_page", rtl: true|false}
//   * nothing at all            -> this lane's default (defaultReadingMode + defaultDirection)
// Unknown/corrupt values fall back to the LANE default, never to a crash and never to a silent
// direction flip: a manga that opens left-to-right is the failure a reader notices in one glance.
//
// Reading is NOT writing: the caller migrates in memory and leaves the stored JSON exactly as it
// is until the reader's next real user change re-writes it. Opening a book must not rewrite it.
function migrateReaderPrefs(rec, entryKind, western) {
    var r = (rec && typeof rec === "object") ? rec : {}

    // the legacy combined identity, IF this record carries one at all
    var legacy = r.readingMode || r.rm
    if (!legacy && r.mode !== undefined) legacy = readingModeFrom(r.mode, r.rtl === true)
    // An absent or unrecognised identity resolves to the LANE default — NOT through
    // readingModeFrom(), whose fail-safe for an unknown layout is comic-like (LTR double). Routing
    // an EMPTY record through it would hand a fresh manga series left-to-right pages.
    if (legacy !== "manga" && legacy !== "comic" && legacy !== "strip")
        legacy = defaultReadingMode(entryKind, western)

    var layout = layoutIsValid(r.layout) ? r.layout : layoutFromReadingMode(legacy)

    var order = r.order
    if (!orderIsValid(order)) {
        if (legacy === "manga")      order = "rtl"
        else if (legacy === "comic") order = "ltr"
        // A legacy STRIP record carries no direction of its own — the old identity made strip mean
        // LTR by construction, which is the conflation being undone here, so a strip record must
        // NOT be read as "this reader chose left-to-right". An explicit stored rtl flag IS a real
        // answer and is honoured; otherwise the lane decides (manga/tankoban RTL, western LTR).
        else if (r.rtl === true)     order = "rtl"
        else if (r.rtl === false)    order = "ltr"
        else                         order = defaultDirection(entryKind, western)
    }

    return {
        layout: layout,
        order: order,
        zoomPercent: _clampNumber(r.zoomPercent, 100, 260, 100),
        // 78% portrait width is the approved default (design 2026-07-28: range 40-100, default 78,
        // per series). `sw`/`sg` are the shipped record's terse keys for the same two values.
        stripWidthPct: _clampNumber(r.stripWidthPct !== undefined ? r.stripWidthPct : r.sw, 40, 100, 78),
        stripGap: _clampNumber(r.stripGap !== undefined ? r.stripGap : r.sg, 0, 80, 0),
        autoScrollSpeed: _clampNumber(r.autoScrollSpeed, 0.25, 3.0, 1.0),
        // Task 7 owns what is INSIDE the render profile; this migration only carries it through
        // intact, so an image adjustment made in a later build survives a read by this one.
        renderProfile: (r.renderProfile && typeof r.renderProfile === "object" && !Array.isArray(r.renderProfile))
                       ? r.renderProfile : {}
    }
}

// --- night veil ------------------------------------------------------------------------------
// The Night-veil level (settings surface 02) -> the opacity of the black page-dim overlay the
// shell paints over the reading surfaces. Design ruling (comicreader-design.md surface 02 +
// plan Task 12): Off/Low/High -> 0 / 0.12 / 0.26. The settings sheet writes the LEVEL string;
// the shell's veil overlay binds its opacity here so the mapping lives in ONE tested place.
// Fails CLOSED (0 = no dim) for any unknown/empty/null level, so a corrupt persisted value can
// never black out the page.
function nightVeilOpacity(level) {
    if (level === "low")  return 0.12
    if (level === "high") return 0.26
    return 0
}

// --- persisted stores ---------------------------------------------------------------------
// Two of the reader's three Settings stores hold ONE JSON string each, holding a map keyed by
// series id / entry id — the shape MangaReader.qml uses for its seriesStore + chapterStore. The
// Settings elements are dumb sinks; all the map logic lives here so it is testable headless.
//
// Every read is total: an empty, corrupt or half-written store degrades to "no memory" and
// NEVER throws. A settings blob is not a trustworthy input — it survives crashes, upgrades and
// hand-edits — and taking the reader down on open because a string went bad is not a trade
// worth making. A write over a corrupt store self-heals it to a clean single-entry map.

function _parseStore(json) {
    if (!json) return {}
    try {
        var m = JSON.parse(json)
        return (m && typeof m === "object" && !Array.isArray(m)) ? m : {}
    } catch (e) {
        return {}
    }
}

// The record stored under `id`, or null. An empty id never matches (a blank series/entry id is
// "we don't know what we're reading", not a key).
function storeGet(json, id) {
    if (!id) return null
    var rec = _parseStore(json)[id]
    return (rec && typeof rec === "object") ? rec : null
}

// Store `rec` under `id` and return the new JSON string. An empty/null record PRUNES the key
// instead of leaving a husk — these maps get a write for every book opened, so without pruning
// they grow forever with records that say nothing (the lineage's `delete m[curChapterId]`).
function storePut(json, id, rec) {
    if (!id) return json
    var m = _parseStore(json)
    if (blobIsEmpty(rec)) delete m[id]
    else                  m[id] = rec
    return JSON.stringify(m)
}

// Is this record worth a line in the store? Only REAL user/probe decisions count:
//   * any bookmark or spread override
//   * a MANUAL coupling (you nudged it) or a RESOLVED auto one (the probe paid to decide; a
//     record spares the next open from re-running it)
//   * any other non-empty own value
// Deliberately NOT counted: memorySaver, which is a GLOBAL preference that merely rides this
// per-entry blob because the backend round-trips it there. Counting it would write a per-book
// record for every book you ever open, purely to restate a machine-wide setting.
function blobIsEmpty(rec) {
    if (!rec || typeof rec !== "object") return true
    for (var k in rec) {
        var v = rec[k]
        if (k === "memorySaver") continue
        if (k === "couplingMode") {
            if (v === "manual") return false
            continue
        }
        if (k === "couplingResolved") {
            if (v === true) return false
            continue
        }
        // couplingPhase/Confidence alone describe an undecided default — they only matter
        // alongside a manual mode or a resolved flag, both handled above.
        if (k === "couplingPhase" || k === "couplingConfidence") continue
        if (v === null || v === undefined) continue
        if (Array.isArray(v)) { if (v.length) return false; continue }
        if (typeof v === "object") {
            for (var kk in v) return false
            continue
        }
        if (v === "" || v === false) continue
        return false
    }
    return true
}
