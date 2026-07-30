// Comic Reader — pure state library oracle (Task 8).
//
// Loads qml/comicreader/ComicReaderState.js (a `.pragma library`, so it has NO access to any
// QML context property or component object by name — everything it decides is a function of
// the arguments it's handed) and exercises every pure decision the Comic Reader shell (Task 9)
// will lean on for crossing/completion/progress/acquisition calls:
//   * progressKind      — the manga/comic/tankoban namespace, ground-truthed against
//                          qml/MangaReader.qml's `progressKind` (line ~64).
//   * entryIndex/nextEntry/previousEntry — newest-first array walking, ground-truthed against
//                          MangaReader's curIndex/goNextChapter/goPrevChapter (lines ~165-477).
//                          Tankoban volumes ride the SAME index math: the series page hands the
//                          reader a DESCENDING (highest-first) volume copy, so index-1 still
//                          reads as "next" (the next HIGHER volume) and index+1 as "previous"
//                          (the next LOWER volume) — no separate ascending-order branch needed.
//   * completion        — the fraction/finished flags MangaReader stamps into a Progress record.
//   * progressPayload    — MUST reproduce the Task 1 §4.1 verbatim Progress.record(...) payload
//                          shape (docs/superpowers/handoffs/2026-07-23-comicreader-public-contract.md),
//                          checked against qml/MangaReader.qml:220-233 too. This is the
//                          load-bearing assertion: Continue/resume must never break at cutover.
//   * defaultDirection   — the design's "smart default" (comicreader-design.md ruling #3):
//                          manga/tankoban open RTL, western opens LTR.
//   * shouldAcquire      — true only while a store status hasn't reached a terminal "ready"
//                          state (native/engine/MangaDownloader.cpp "done" /
//                          MangaTankobanService.cpp "ready"); everything else (missing, queued,
//                          downloading, resolving, extracting, ingesting, packing, failed) still
//                          needs acquisition.
//   * defaultMode        — default reading mode per lane. ASSUMPTION (flagged, no ruling exists
//                          in comicreader-design.md beyond "two modes only"): today's single
//                          global default (MangaReader prefs.reading_style) is "long_strip" for
//                          every lane, so this keeps that behavior until a per-lane split lands.
//
// HOUSE HARNESS PATTERN (mirrors tests/comicreader_contract_harness.qml exactly): a thrown error
// HANGS qml.exe offscreen, so `ck` never throws — it collects failures; the run prints exactly
// ONE `COMICREADER_STATE_OK` sentinel when clean, else one `COMICREADER_STATE_FAIL: <msg>` per
// failure and Qt.exit(1).

import QtQuick
import "../qml/comicreader/ComicReaderState.js" as State

Item {
    id: harness
    width: 8; height: 8
    visible: false

    property var failures: []
    function ck(cond, msg) { if (!cond) failures.push(msg) }

    // small structural deep-equal — good enough for the plain-object/array payload shapes here
    function deepEqual(a, b) {
        if (a === b) return true
        if (typeof a !== typeof b) return false
        if (a === null || b === null) return false
        if (typeof a !== "object") return false
        var ak = Object.keys(a), bk = Object.keys(b)
        if (ak.length !== bk.length) return false
        for (var i = 0; i < ak.length; i++) {
            var k = ak[i]
            if (!b.hasOwnProperty(k)) return false
            if (!deepEqual(a[k], b[k])) return false
        }
        return true
    }

    function report() {
        if (failures.length === 0) {
            console.log("COMICREADER_STATE_OK")
            Qt.exit(0)
        } else {
            for (var i = 0; i < failures.length; i++)
                console.log("COMICREADER_STATE_FAIL: " + failures[i])
            Qt.exit(1)
        }
    }

    function runChecks() {
        try {
            // ---- 0. deepEqual comparator sanity — must actually reject unequal input, not
            // just agree on equal input (a comparator that always returns true would still
            // make every deepEqual() call below pass silently). ----
            ck(!deepEqual({ a: 1 }, { a: 2 }), "deepEqual must reject unequal objects, not just accept equal ones")

            // ---- 1. progressKind — manga / comic / tankoban namespace ----
            ck(State.progressKind("manga", false) === "manga",
               "progressKind(manga, western=false) must be 'manga'")
            ck(State.progressKind("manga", true) === "comic",
               "progressKind(manga, western=true) must be 'comic'")
            ck(State.progressKind("tankoban", false) === "tankoban",
               "progressKind(tankoban, western=false) must be 'tankoban'")
            ck(State.progressKind("tankoban", true) === "tankoban",
               "progressKind(tankoban, western=true) must stay 'tankoban' (only entryKind==='manga' flips on western)")

            // ---- 2. entryIndex/nextEntry/previousEntry — newest-first chapters ----
            var chapters = [
                { id: "c3", number: "3", name: "" },   // newest — index 0
                { id: "c2", number: "2", name: "" },   // index 1
                { id: "c1", number: "1", name: "" }    // oldest — index 2
            ]
            ck(State.entryIndex(chapters, "c2") === 1, "entryIndex must find 'c2' at index 1")
            ck(State.entryIndex(chapters, "c3") === 0, "entryIndex must find 'c3' at index 0")
            ck(State.entryIndex(chapters, "nope") === -1, "entryIndex must return -1 for a missing id")
            ck(State.entryIndex([], "c1") === -1, "entryIndex on an empty array must return -1")

            var nxt = State.nextEntry(chapters, 1)
            ck(nxt && nxt.id === "c3", "nextEntry(1) must be the newer chapter c3 (index-1)")
            var prv = State.previousEntry(chapters, 1)
            ck(prv && prv.id === "c1", "previousEntry(1) must be the older chapter c1 (index+1)")
            ck(State.nextEntry(chapters, 0) === null, "nextEntry at the newest index (0) must be null")
            ck(State.previousEntry(chapters, 2) === null, "previousEntry at the oldest index (last) must be null")
            ck(State.nextEntry(chapters, -1) === null, "nextEntry with an out-of-range (-1) index must be null")
            ck(State.previousEntry(chapters, -1) === null, "previousEntry with an out-of-range (-1) index must be null")
            ck(State.nextEntry(chapters, 99) === null, "nextEntry with an out-of-range (99) index must be null")
            ck(State.previousEntry(chapters, 99) === null, "previousEntry with an out-of-range (99) index must be null")

            // Tankoban volumes: the series page hands the reader a DESCENDING (highest-first)
            // copy — same index math, "next" reads as the next HIGHER volume, "previous" as the
            // next LOWER volume (MangaReader.qml goNextChapter/goPrevChapter comments, ~451-455).
            var volumes = [
                { id: "v5", number: 5 },   // highest — index 0
                { id: "v4", number: 4 },   // index 1
                { id: "v3", number: 3 }    // lowest — index 2
            ]
            var nv = State.nextEntry(volumes, 1)
            ck(nv && nv.id === "v5", "nextEntry over a descending volume copy must be the next HIGHER volume (v5)")
            var pv = State.previousEntry(volumes, 1)
            ck(pv && pv.id === "v3", "previousEntry over a descending volume copy must be the next LOWER volume (v3)")

            // ---- 3. completion — fraction + finished-at-last-unit ----
            var c1 = State.completion(3, 10, 3)
            ck(Math.abs(c1.fraction - 0.3) < 1e-9 && c1.finished === false,
               "completion(3,10,3) must be {fraction:0.3, finished:false}, got " + JSON.stringify(c1))
            var c2 = State.completion(10, 10, 10)
            ck(c2.fraction === 1 && c2.finished === true,
               "completion(10,10,10) must be {fraction:1, finished:true}, got " + JSON.stringify(c2))
            // re-reading a finished chapter (page back at 1) must NOT un-finish it — maxSeen is
            // the high-water mark (MangaReader.qml's Key_R comment: "leaves maxSeen alone").
            var c3 = State.completion(1, 5, 5)
            ck(c3.finished === true, "completion must stay finished when maxSeen already reached pageCount, even mid-reread")
            var c4 = State.completion(1, 5, 1)
            ck(c4.finished === false, "completion must not be finished when maxSeen hasn't reached pageCount")

            // ---- 4. progressPayload — MUST match the Task 1 §4.1 recorded shape EXACTLY ----
            var payloadArgs = {
                seriesId: "s1", kind: "manga", seriesTitle: "My Series", label: "Chapter 5",
                cover: "file:///cover.png", page: 4, max: 8, chapterId: "ch5",
                style: "long_strip", scrollFrac: 0.42, maxSeen: 6
            }
            var expectedPayload = {
                "id": "s1", "kind": "manga", "caption": "My Series", "title": "My Series",
                "sub": "Chapter 5", "cover": "file:///cover.png",
                "c1": "#3a2f55", "c2": "#15111f",
                "progress": 0.5,
                "resume": { "chapterId": "ch5", "page": 4, "scrollFrac": 0.42, "maxSeen": 6, "finished": false }
            }
            var gotPayload = State.progressPayload(payloadArgs)
            ck(deepEqual(gotPayload, expectedPayload),
               "progressPayload must deep-equal the Task 1 §4.1 shape, got " + JSON.stringify(gotPayload)
               + " expected " + JSON.stringify(expectedPayload))
            // exact key set — a stray/missing key would silently break resume even if the
            // sampled values above happened to line up.
            ck(JSON.stringify(Object.keys(gotPayload).sort())
                   === JSON.stringify(Object.keys(expectedPayload).sort()),
               "progressPayload top-level key set must match exactly, got " + JSON.stringify(Object.keys(gotPayload)))
            ck(JSON.stringify(Object.keys(gotPayload.resume).sort())
                   === JSON.stringify(Object.keys(expectedPayload.resume).sort()),
               "progressPayload.resume key set must match exactly, got " + JSON.stringify(Object.keys(gotPayload.resume)))

            // progress clamp: page beyond max clamps to 1 (Math.min(1, Math.max(0, page/max)))
            var clamped = State.progressPayload({
                seriesId: "s1", kind: "manga", seriesTitle: "S", label: "L", cover: "",
                page: 20, max: 8, chapterId: "ch5", style: "long_strip", scrollFrac: 0, maxSeen: 8
            })
            ck(clamped.progress === 1, "progressPayload must clamp progress to 1 when page > max, got " + clamped.progress)
            ck(clamped.resume.finished === true, "progressPayload finished must be maxSeen>=max")

            // scrollFrac is zeroed OUTSIDE long_strip, regardless of the raw value passed in
            var paged = State.progressPayload({
                seriesId: "s1", kind: "manga", seriesTitle: "S", label: "L", cover: "",
                page: 2, max: 8, chapterId: "ch5", style: "double_page", scrollFrac: 0.77, maxSeen: 2
            })
            ck(paged.resume.scrollFrac === 0, "progressPayload must zero scrollFrac outside long_strip, got " + paged.resume.scrollFrac)

            // cover pass-through — the "never clobber a saved cover" back-fill is the CALLER's
            // job (MangaReader.qml lines 214-219); this pure function just carries the value in.
            ck(gotPayload.cover === "file:///cover.png", "progressPayload must pass the cover value straight through")

            // ---- 5. defaultDirection — smart default (design ruling #3) ----
            ck(State.defaultDirection("manga", false) === "rtl", "defaultDirection(manga) must be 'rtl'")
            ck(State.defaultDirection("tankoban", false) === "rtl", "defaultDirection(tankoban) must be 'rtl'")
            ck(State.defaultDirection("manga", true) === "ltr", "defaultDirection(western=true) must be 'ltr' regardless of entryKind")

            // ---- 6. shouldAcquire — true only for a missing/incomplete next entry ----
            ck(State.shouldAcquire({ state: "ready" }) === false, "shouldAcquire must be false for a ready tankoban volume")
            ck(State.shouldAcquire({ state: "done" }) === false, "shouldAcquire must be false for a done manga/comic chapter")
            ck(State.shouldAcquire({ state: "none" }) === true, "shouldAcquire must be true for state 'none'")
            ck(State.shouldAcquire({ state: "downloading" }) === true, "shouldAcquire must be true while downloading")
            ck(State.shouldAcquire({ state: "queued" }) === true, "shouldAcquire must be true while queued")
            ck(State.shouldAcquire({ state: "resolving" }) === true, "shouldAcquire must be true while resolving")
            ck(State.shouldAcquire({ state: "extracting" }) === true, "shouldAcquire must be true while extracting")
            ck(State.shouldAcquire({ state: "ingesting" }) === true, "shouldAcquire must be true while ingesting")
            ck(State.shouldAcquire({ state: "packing" }) === true, "shouldAcquire must be true while packing")
            ck(State.shouldAcquire({ state: "failed" }) === true, "shouldAcquire must be true when failed")
            ck(State.shouldAcquire(null) === true, "shouldAcquire must be true (needs acquire) for a missing status")
            ck(State.shouldAcquire(undefined) === true, "shouldAcquire must be true (needs acquire) for an undefined status")

            // ---- 7. defaultMode — valid mode per lane ----
            var validModes = ["long_strip", "double_page"]
            var mdManga = State.defaultMode("manga", false)
            var mdWestern = State.defaultMode("manga", true)
            var mdTankoban = State.defaultMode("tankoban", false)
            ck(validModes.indexOf(mdManga) >= 0, "defaultMode(manga) must return a valid mode, got " + mdManga)
            ck(validModes.indexOf(mdWestern) >= 0, "defaultMode(western) must return a valid mode, got " + mdWestern)
            ck(validModes.indexOf(mdTankoban) >= 0, "defaultMode(tankoban) must return a valid mode, got " + mdTankoban)
            // deterministic — the same lane must always resolve to the same default
            ck(State.defaultMode("manga", false) === mdManga, "defaultMode must be deterministic for the same lane")

            // ---- 8. defensive-branch coverage (previously-uncovered guard paths) ----
            // completion with a zero pageCount must read 0/false, never NaN/garbage.
            var c0 = State.completion(3, 0, 3)
            ck(c0.fraction === 0 && c0.finished === false,
               "completion(_, pageCount=0, _) must be {fraction:0, finished:false}, got " + JSON.stringify(c0))
            // entryIndex must fail closed (-1), never throw, on a missing/null chapters array.
            ck(State.entryIndex(null, "c1") === -1, "entryIndex(null chapters, id) must be -1")
            ck(State.entryIndex(undefined, "c1") === -1, "entryIndex(undefined chapters, id) must be -1")
            // nextEntry/previousEntry must fail closed (null) on a null/undefined currentIndex,
            // not just an out-of-range numeric one (the explicit null/undefined guard branch).
            ck(State.nextEntry(chapters, null) === null, "nextEntry(chapters, null index) must be null")
            ck(State.previousEntry(chapters, null) === null, "previousEntry(chapters, null index) must be null")
            ck(State.nextEntry(chapters, undefined) === null, "nextEntry(chapters, undefined index) must be null")
            ck(State.previousEntry(chapters, undefined) === null, "previousEntry(chapters, undefined index) must be null")

            // progressPayload's max=0 precondition (documented in the JS comment, NOT internally
            // guarded — this pins the CURRENT, deliberately-unsafe output as an explicit contract
            // so the caller-must-guard-max>0 rule, mirroring MangaReader.qml:211, is tested, not
            // just asserted in prose. page=1/maxSeen=0 makes the corruption unambiguous: a
            // fresh, unstarted entry would read back as "finished".
            var unsafeZeroMax = State.progressPayload({
                seriesId: "s1", kind: "manga", seriesTitle: "S", label: "L", cover: "",
                page: 1, max: 0, chapterId: "ch0", style: "long_strip", scrollFrac: 0, maxSeen: 0
            })
            ck(unsafeZeroMax.progress === 1,
               "DOCUMENTED unsafe contract: progressPayload with max=0 clamps progress to 1 (1/0=Infinity), got " + unsafeZeroMax.progress)
            ck(unsafeZeroMax.resume.finished === true,
               "DOCUMENTED unsafe contract: progressPayload with max=0 reports finished=true (0>=0) even though nothing was read — CALLERS MUST guard max>0 before calling, got " + unsafeZeroMax.resume.finished)

            // ---- 9. nightVeilOpacity — Night-veil level -> page-dim overlay opacity (design ruling,
            // comicreader-design.md surface 02 + plan Task 12: Off/Low/High -> 0/.12/.26). The sheet
            // writes the LEVEL string; this pure mapping is what the shell's veil overlay binds its
            // opacity to. Fails CLOSED (no dim) for an unknown/empty level so a bad persisted value
            // never blacks out the page. ----
            ck(State.nightVeilOpacity("off")  === 0,    "nightVeilOpacity('off') must be 0")
            ck(State.nightVeilOpacity("low")  === 0.12, "nightVeilOpacity('low') must be 0.12, got " + State.nightVeilOpacity("low"))
            ck(State.nightVeilOpacity("high") === 0.26, "nightVeilOpacity('high') must be 0.26, got " + State.nightVeilOpacity("high"))
            ck(State.nightVeilOpacity("")     === 0,    "nightVeilOpacity('') must fail closed to 0 (no dim)")
            ck(State.nightVeilOpacity("bogus")=== 0,    "nightVeilOpacity(unknown) must fail closed to 0 (no dim)")
            ck(State.nightVeilOpacity(null)   === 0,    "nightVeilOpacity(null) must fail closed to 0 (no dim)")
            ck(State.nightVeilOpacity(undefined) === 0, "nightVeilOpacity(undefined) must fail closed to 0 (no dim)")

            // ---- 10. readingMode — the single user-facing identity (Hemanth ruling 2026-07-25):
            // "manga" | "comic" | "strip" bakes in BOTH layout and direction, replacing the separate
            // mode(double/strip) + direction(rtl/ltr) toggles. manga = RTL double-page (MangaPlus),
            // comic = LTR double-page, strip = vertical scroll (direction-neutral). ----
            // defaults per lane: manga chapters + tankoban volumes open as Manga; western opens as Comic.
            ck(State.defaultReadingMode("manga", false) === "manga", "defaultReadingMode(manga) must be 'manga'")
            ck(State.defaultReadingMode("tankoban", false) === "manga", "defaultReadingMode(tankoban) must be 'manga'")
            ck(State.defaultReadingMode("manga", true) === "comic", "defaultReadingMode(western) must be 'comic' regardless of entryKind")
            ck(State.defaultReadingMode("tankoban", true) === "comic", "defaultReadingMode(western) must be 'comic'")
            // layout: manga + comic are double-page; strip is the vertical scroll layout
            ck(State.readingModeLayout("manga") === "double_page", "readingModeLayout(manga) must be 'double_page'")
            ck(State.readingModeLayout("comic") === "double_page", "readingModeLayout(comic) must be 'double_page'")
            ck(State.readingModeLayout("strip")  === "long_strip",  "readingModeLayout(strip) must be 'long_strip'")
            // direction baked in: ONLY manga is RTL; comic + strip are LTR/neutral
            ck(State.readingModeRtl("manga") === true,  "readingModeRtl(manga) must be true (RTL)")
            ck(State.readingModeRtl("comic") === false, "readingModeRtl(comic) must be false (LTR)")
            ck(State.readingModeRtl("strip") === false, "readingModeRtl(strip) must be false (neutral)")
            // fail-closed: an unknown/empty readingMode resolves to a safe double-page LTR (comic-like)
            ck(State.readingModeLayout("bogus") === "double_page", "readingModeLayout(unknown) must fail safe to 'double_page'")
            ck(State.readingModeRtl("") === false, "readingModeRtl('') must fail safe to false")
            // reverse map: (layout, rtl) -> readingMode, so a legacy mode+direction still resolves
            ck(State.readingModeFrom("double_page", true)  === "manga", "readingModeFrom(double_page, rtl) must be 'manga'")
            ck(State.readingModeFrom("double_page", false) === "comic", "readingModeFrom(double_page, ltr) must be 'comic'")
            ck(State.readingModeFrom("long_strip", false)  === "strip", "readingModeFrom(long_strip, _) must be 'strip'")
            ck(State.readingModeFrom("long_strip", true)   === "strip", "readingModeFrom(long_strip, rtl) must still be 'strip'")

            // ---- 10b. LAYOUT + ORDER are INDEPENDENT (Task 3, plan 2026-07-28) ----------------
            // Hemanth's ruling: layout (Single Page / Paired Pages / Long Strip) is presentation
            // only; order (comic LTR / manga RTL) is the physical page ordering. Neither moves the
            // other. migrateReaderPrefs is the ONE door every stored record comes through, so these
            // assertions are about a REAL SAVED BOOK reopening the way its reader left it — not
            // about what the function happens to do today.
            ck(State.layoutIsValid("single_page") && State.layoutIsValid("paired_pages")
               && State.layoutIsValid("long_strip"), "all three approved layouts must be valid")
            ck(!State.layoutIsValid("double_page"), "the INTERNAL alias 'double_page' is not a persistable layout")
            ck(!State.layoutIsValid("guided") && !State.layoutIsValid("") && !State.layoutIsValid(null),
               "an unknown/empty layout must not validate (GUIDED stays frozen)")
            ck(State.orderIsValid("ltr") && State.orderIsValid("rtl"), "ltr + rtl must be valid orders")
            ck(!State.orderIsValid("right_left") && !State.orderIsValid("") && !State.orderIsValid(null),
               "an unknown/empty order must not validate")
            ck(State.layoutFromReadingMode("strip") === "long_strip", "legacy 'strip' is the long strip")
            ck(State.layoutFromReadingMode("manga") === "paired_pages", "legacy 'manga' was double-page")
            ck(State.layoutFromReadingMode("comic") === "paired_pages", "legacy 'comic' was double-page")

            // --- the shipped record shape: {rm} is what _saveSeriesPrefs has been writing ---
            // THE load-bearing case. A manga series someone is reading right now is stored as
            // {"rm":"manga"} — it MUST come back right-to-left. If this reads 'ltr' the reader has
            // silently flipped every saved manga book, which is the one migration failure a reader
            // cannot miss and cannot fix (there is no direction toggle any more).
            var legacyManga = State.migrateReaderPrefs({ rm: "manga" }, "manga", false)
            ck(legacyManga.layout === "paired_pages" && legacyManga.order === "rtl",
               "legacy {rm:'manga'} must reopen as paired pages, RIGHT-TO-LEFT, got "
               + JSON.stringify(legacyManga))
            var legacyComic = State.migrateReaderPrefs({ rm: "comic" }, "manga", true)
            ck(legacyComic.layout === "paired_pages" && legacyComic.order === "ltr",
               "legacy {rm:'comic'} must reopen as paired pages, left-to-right, got " + JSON.stringify(legacyComic))
            // A legacy strip record carries NO direction of its own (the old identity made strip
            // imply LTR by construction — that is the conflation being undone), so the LANE decides.
            // A manga read in Long Strip must therefore come back RTL, not LTR: the moment its
            // reader switches to Paired Pages the pages have to fall in manga order.
            var legacyStripManga = State.migrateReaderPrefs({ rm: "strip" }, "manga", false)
            ck(legacyStripManga.layout === "long_strip" && legacyStripManga.order === "rtl",
               "legacy {rm:'strip'} on a MANGA series must stay long strip and take the lane's RTL, got "
               + JSON.stringify(legacyStripManga))
            var legacyStripWestern = State.migrateReaderPrefs({ rm: "strip" }, "manga", true)
            ck(legacyStripWestern.layout === "long_strip" && legacyStripWestern.order === "ltr",
               "legacy {rm:'strip'} on a WESTERN series must stay long strip and take the lane's LTR, got "
               + JSON.stringify(legacyStripWestern))
            // the long-form identity key resolves identically to the terse one
            var mangaLong = State.migrateReaderPrefs({ readingMode: "manga" }, "manga", false)
            ck(mangaLong.layout === "paired_pages" && mangaLong.order === "rtl",
               "legacy {readingMode:'manga'} must resolve exactly like {rm:'manga'}, got " + JSON.stringify(mangaLong))
            var stripLong = State.migrateReaderPrefs({ readingMode: "strip" }, "tankoban", false)
            ck(stripLong.layout === "long_strip" && stripLong.order === "rtl",
               "legacy {readingMode:'strip'} on a tankoban volume must be long strip + RTL, got " + JSON.stringify(stripLong))

            // --- a raw (mode, rtl) pair: an EXPLICIT stored direction beats the lane default ---
            var rawStrip = State.migrateReaderPrefs({ mode: "long_strip", rtl: true }, "tankoban", false)
            ck(rawStrip.layout === "long_strip" && rawStrip.order === "rtl",
               "legacy {mode:'long_strip', rtl:true} must be long strip + RTL, got " + JSON.stringify(rawStrip))
            // ...and this is the version that PROVES it, because here the lane default disagrees:
            // a stored rtl:true on a WESTERN series must survive, not be overwritten with LTR.
            var rawStripWestern = State.migrateReaderPrefs({ mode: "long_strip", rtl: true }, "manga", true)
            ck(rawStripWestern.order === "rtl",
               "an EXPLICIT stored rtl:true must beat the western lane's LTR default, got " + rawStripWestern.order)
            var rawDouble = State.migrateReaderPrefs({ mode: "double_page", rtl: false }, "manga", false)
            ck(rawDouble.layout === "paired_pages" && rawDouble.order === "ltr",
               "legacy {mode:'double_page', rtl:false} must be paired pages + LTR even in the manga lane, got "
               + JSON.stringify(rawDouble))

            // --- a FRESH series (nothing stored) opens exactly the way it opens today ---
            // NOTE (deliberate departure from the plan's Step-1 fixture, which asserted
            // layout === "long_strip" here): the plan's own Step-3 implementation resolves a fresh
            // record to paired pages, and Hemanth's standing 2026-07-25 ruling is that manga opens
            // as Manga (RTL double-page, MangaPlus) and western opens as Comic (LTR double-page) —
            // pinned by the shell gate's "mode must default to 'double_page'" assertion. Reversing
            // which layout a book opens in is a product decision, not something a migration
            // function should smuggle in, so the lane default stands and the plan line is reported.
            var freshWestern = State.migrateReaderPrefs({}, "comic", true)
            ck(freshWestern.layout === "paired_pages" && freshWestern.order === "ltr",
               "a fresh WESTERN series must open paired pages + LTR (the lane default), got "
               + JSON.stringify(freshWestern))
            ck(freshWestern.stripWidthPct === 78 && freshWestern.autoScrollSpeed === 1.0,
               "approved strip defaults: 78% portrait width + 1.0x auto-scroll, got "
               + freshWestern.stripWidthPct + " / " + freshWestern.autoScrollSpeed)
            // THE anti-regression: an empty record must NOT be read through readingModeFrom()'s
            // comic-like fail-safe, or every fresh manga series would open left-to-right.
            var freshManga = State.migrateReaderPrefs({}, "manga", false)
            ck(freshManga.order === "rtl",
               "a fresh MANGA series must open RIGHT-TO-LEFT (lane default), got " + freshManga.order)
            ck(freshManga.layout === "paired_pages",
               "a fresh manga series must open paired pages (Hemanth 2026-07-25: MangaPlus double), got "
               + freshManga.layout)
            var freshTankoban = State.migrateReaderPrefs({}, "tankoban", false)
            ck(freshTankoban.order === "rtl", "a fresh TANKOBAN volume must open RTL, got " + freshTankoban.order)

            // --- the NEW fields are the truth: they beat any legacy key left in the record ---
            var mixed = State.migrateReaderPrefs({ layout: "single_page", order: "rtl", rm: "comic" }, "manga", true)
            ck(mixed.layout === "single_page" && mixed.order === "rtl",
               "the new layout/order fields must beat a stale legacy rm in the same record, got " + JSON.stringify(mixed))
            // Single Page is a first-class PERSISTABLE layout even though no surface paints it until
            // Task 4 — the state model has to be able to express it before the surface exists.
            var single = State.migrateReaderPrefs({ layout: "single_page" }, "manga", false)
            ck(single.layout === "single_page", "single_page must survive a round trip, got " + single.layout)
            ck(single.order === "rtl", "single_page on a manga series must still take the lane's RTL, got " + single.order)

            // --- corrupt / hostile records: lane defaults, never a throw, never a direction flip ---
            var badLayout = State.migrateReaderPrefs({ layout: "guided", order: "rtl" }, "manga", false)
            ck(badLayout.layout === "paired_pages" && badLayout.order === "rtl",
               "an unknown layout must fall back to the lane default WITHOUT touching a good order, got "
               + JSON.stringify(badLayout))
            var badOrder = State.migrateReaderPrefs({ layout: "long_strip", order: "sideways" }, "manga", false)
            ck(badOrder.layout === "long_strip" && badOrder.order === "rtl",
               "an unknown order must fall back to the lane default WITHOUT touching a good layout, got "
               + JSON.stringify(badOrder))
            var badIdentity = State.migrateReaderPrefs({ rm: "guided" }, "manga", false)
            ck(badIdentity.layout === "paired_pages" && badIdentity.order === "rtl",
               "an unknown legacy identity must resolve to the lane default (GUIDED stays frozen), got "
               + JSON.stringify(badIdentity))
            ck(State.migrateReaderPrefs(null, "manga", false).order === "rtl",
               "migrateReaderPrefs(null) must be total (lane default), not a throw")
            ck(State.migrateReaderPrefs(undefined, "manga", true).order === "ltr",
               "migrateReaderPrefs(undefined) must be total (lane default), not a throw")
            ck(State.migrateReaderPrefs("not a record", "manga", false).layout === "paired_pages",
               "migrateReaderPrefs(non-object) must be total, not a throw")

            // --- numeric ranges (design 2026-07-28) ---
            var clamped = State.migrateReaderPrefs({ zoomPercent: 9000, stripWidthPct: 5,
                                                     stripGap: 900, autoScrollSpeed: 9 }, "manga", false)
            ck(clamped.zoomPercent === 260, "zoomPercent must clamp high to 260, got " + clamped.zoomPercent)
            ck(clamped.stripWidthPct === 40, "stripWidthPct must clamp low to 40, got " + clamped.stripWidthPct)
            ck(clamped.stripGap === 80, "stripGap must clamp high to 80, got " + clamped.stripGap)
            ck(clamped.autoScrollSpeed === 3.0, "autoScrollSpeed must clamp high to 3.0, got " + clamped.autoScrollSpeed)
            var clampedLow = State.migrateReaderPrefs({ zoomPercent: 10, stripWidthPct: 500,
                                                        stripGap: -40, autoScrollSpeed: 0.01 }, "manga", false)
            ck(clampedLow.zoomPercent === 100, "zoomPercent must clamp low to 100, got " + clampedLow.zoomPercent)
            ck(clampedLow.stripWidthPct === 100, "stripWidthPct must clamp high to 100, got " + clampedLow.stripWidthPct)
            ck(clampedLow.stripGap === 0, "stripGap must clamp low to 0, got " + clampedLow.stripGap)
            ck(clampedLow.autoScrollSpeed === 0.25, "autoScrollSpeed must clamp low to 0.25, got " + clampedLow.autoScrollSpeed)
            var garbageNums = State.migrateReaderPrefs({ zoomPercent: "wide", stripWidthPct: null,
                                                         stripGap: "none", autoScrollSpeed: {} }, "manga", false)
            ck(garbageNums.zoomPercent === 100 && garbageNums.stripWidthPct === 78
               && garbageNums.stripGap === 0 && garbageNums.autoScrollSpeed === 1.0,
               "unreadable numbers must take the approved defaults, got " + JSON.stringify(garbageNums))
            // the SHIPPED terse measure keys are the ones real records carry
            var terse = State.migrateReaderPrefs({ rm: "strip", sw: 55, sg: 12 }, "manga", false)
            ck(terse.stripWidthPct === 55 && terse.stripGap === 12,
               "the shipped terse sw/sg keys must migrate into stripWidthPct/stripGap, got " + JSON.stringify(terse))
            ck(terse.layout === "long_strip", "the terse record's identity must still resolve, got " + terse.layout)

            // --- renderProfile is carried through UNTOUCHED (Task 7 owns its contents) ---
            var rp = State.migrateReaderPrefs({ renderProfile: { brightness: 20, quality: "best" } }, "manga", false)
            ck(rp.renderProfile.brightness === 20 && rp.renderProfile.quality === "best",
               "renderProfile must pass through intact, got " + JSON.stringify(rp.renderProfile))
            ck(JSON.stringify(State.migrateReaderPrefs({ renderProfile: "junk" }, "manga", false).renderProfile) === "{}",
               "a non-object renderProfile must degrade to an empty map, not leak a string through")
            ck(JSON.stringify(State.migrateReaderPrefs({}, "manga", false).renderProfile) === "{}",
               "an absent renderProfile must be an empty map")

            // --- IDEMPOTENT: migrating an already-migrated record must not drift ---
            // The record written after a user change is the record read on the next launch, so a
            // second pass has to be a no-op or the reader would walk a book's settings over time.
            var once = State.migrateReaderPrefs({ rm: "manga", sw: 62, sg: 8 }, "manga", false)
            var twice = State.migrateReaderPrefs(once, "manga", false)
            ck(deepEqual(once, twice),
               "migrateReaderPrefs must be idempotent, got " + JSON.stringify(once) + " then " + JSON.stringify(twice))
            // ...and the western lane must not re-flip a book whose stored order is RTL.
            var rtlUnderWestern = State.migrateReaderPrefs(once, "manga", true)
            ck(rtlUnderWestern.order === "rtl",
               "a migrated record's explicit order must survive a lane it disagrees with, got " + rtlUnderWestern.order)

            // --- persistence map helpers (the Settings elements are thin sinks; THIS is the logic) ---
            // Both stores are one JSON string holding a map keyed by series/entry id, exactly like
            // MangaReader's seriesStore/chapterStore. Reads must survive garbage without throwing —
            // a corrupt store must degrade to "no memory", never take the reader down on open.
            ck(State.storeGet("", "s1") === null, "storeGet on an EMPTY string must be null, not a throw")
            ck(State.storeGet("not json{", "s1") === null, "storeGet on CORRUPT json must be null, not a throw")
            ck(State.storeGet("{}", "s1") === null, "storeGet on an empty map must be null")
            ck(State.storeGet('{"s1":{"rm":"strip"}}', "s1").rm === "strip", "storeGet must return the record for the id")
            ck(State.storeGet('{"s1":{"rm":"strip"}}', "s2") === null, "storeGet must return null for an absent id")
            ck(State.storeGet('{"s1":{"rm":"strip"}}', "") === null, "storeGet with an EMPTY id must be null (never a blind hit)")

            // writes round-trip and leave the other ids untouched
            var w1 = State.storePut('{"a":{"rm":"manga"}}', "b", { rm: "strip" })
            ck(State.storeGet(w1, "b").rm === "strip", "storePut must store the new record")
            ck(State.storeGet(w1, "a").rm === "manga", "storePut must NOT disturb other ids")
            var w2 = State.storePut(w1, "b", { rm: "comic" })
            ck(State.storeGet(w2, "b").rm === "comic", "storePut must overwrite an existing record")

            // an EMPTY record PRUNES its key instead of accumulating dead entries forever (the
            // lineage's `delete m[curChapterId]` — these maps are written on every book you open).
            var p1 = State.storePut('{"a":{"rm":"manga"},"b":{"rm":"strip"}}', "b", {})
            ck(State.storeGet(p1, "b") === null, "storePut of an EMPTY record must prune the key")
            ck(State.storeGet(p1, "a") !== null, "pruning one key must leave the others")
            ck(State.storePut('{"a":{"rm":"manga"}}', "a", null).indexOf("a") < 0, "storePut(null) must prune too")
            // a record that is only default-ish noise still counts as empty
            ck(State.storeGet(State.storePut("{}", "x", { spreadOverrides: {}, bookmarks: [] }), "x") === null,
               "a record of only empty collections must prune, not persist as clutter")
            // corrupt store + a write => a clean single-entry store (self-healing, not a throw)
            var heal = State.storePut("not json{", "z", { rm: "comic" })
            ck(State.storeGet(heal, "z").rm === "comic", "storePut must self-heal a corrupt store")

            // --- what counts as a non-empty entry blob ---
            ck(State.blobIsEmpty(null) === true, "blobIsEmpty(null)")
            ck(State.blobIsEmpty({}) === true, "blobIsEmpty({})")
            ck(State.blobIsEmpty({ bookmarks: [], spreadOverrides: {} }) === true, "empty collections are still empty")
            ck(State.blobIsEmpty({ bookmarks: [3] }) === false, "a bookmark makes the blob worth keeping")
            ck(State.blobIsEmpty({ spreadOverrides: { "4": true } }) === false, "a spread override makes it worth keeping")
            // an AUTO+unresolved coupling is the default state — not worth a record on its own
            ck(State.blobIsEmpty({ couplingMode: "auto", couplingResolved: false }) === true,
               "an unresolved auto coupling is the default — must NOT create a record")
            ck(State.blobIsEmpty({ couplingMode: "manual", couplingPhase: "shifted" }) === false,
               "a MANUAL coupling is a real user decision — must be kept")
            ck(State.blobIsEmpty({ couplingMode: "auto", couplingResolved: true }) === false,
               "a RESOLVED auto coupling is a probe verdict worth not re-running")
            // memorySaver is a GLOBAL that merely rides the per-entry blob — it must never, on its
            // own, cause a per-book record to be written.
            ck(State.blobIsEmpty({ memorySaver: true }) === true,
               "memorySaver alone must NOT create a per-entry record (it is a global)")
        } catch (e) {
            failures.push("exception during checks: " + e.message)
        }
        report()
    }

    Component.onCompleted: {
        try { runChecks() }
        catch (e) { console.log("COMICREADER_STATE_FAIL: setup: " + e.message); Qt.exit(1) }
    }

    // safety net — a hang (not a thrown error) still fails loudly instead of stalling CI
    Timer {
        interval: 8000; running: true
        onTriggered: { console.log("COMICREADER_STATE_FAIL: timeout"); Qt.exit(1) }
    }
}
