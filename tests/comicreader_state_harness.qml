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
