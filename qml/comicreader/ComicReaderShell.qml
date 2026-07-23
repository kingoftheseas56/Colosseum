// ComicReaderShell — the Comic Reader's orchestration spine (Task 9).
//
// This is the reader's ROOT: it exposes the exact public caller contract that qml/MangaReader.qml
// exposes today (Task 1 handoff, docs/superpowers/handoffs/2026-07-23-comicreader-public-contract.md),
// so at the Task 13 cutover MangaReader.qml collapses to `ComicReaderShell {}` and every caller —
// MangaSeries / ComicSeries / ComicSeriesPage / Main — keeps working byte-for-byte.
//
// Orchestration ONLY. It owns the entry lifecycle (load -> open, resume, crossing, close), drives
// the C++ backend + the Progress sink through injectable seams, and mounts PLACEHOLDER surfaces for
// the two reading modes. The real Long Strip / Double Page surfaces (Task 10), the Family Gradient
// HUD (Task 11) and the overlays (Task 12) mount INSIDE this shell later. No rendering, no chrome,
// no overlays here.
//
// Every pairing/crossing/completion/progress/direction/acquisition DECISION is delegated to the
// pure library ComicReaderState.js (Task 8) — the shell never re-derives that logic inline.
//
// LIFECYCLE PARITY (ground-truthed against how the three callers mount the reader: they toggle
// `visible: page.openChapterId.length > 0` — they HIDE the reader on back and SHOW it again to
// reopen; they do NOT destroy it). So: HIDE flushes progress but keeps the backend entry OPEN
// (tearing it down would blank the reader on reopen-same-entry — chapterId "" -> "ch5" while
// curChapterId is still "ch5" skips the openEntryById guard, so load() never re-runs); the backend
// entry is closed ONLY on destruction. Mirrors MangaReader.qml onVisibleChanged (flush, no
// teardown) + Component.onDestruction.
//
// INJECTABLE SEAMS (the reason this is testable offscreen AND degrades gracefully):
//   * core     — the C++ ComicReaderCore backend (a context property in the real app). The
//                harness injects a mock with the same API. Every use is guarded `if (core) ...`.
//   * progress — the Progress sink (a C++ context property in the real app; simply undefined
//                under the offscreen qml.exe runner). Every use is guarded `if (progress) ...`,
//                mirroring the old reader's `typeof Progress === "undefined"` guards.
//   * store    — resolved from the injected `pageStore` (Tankoban volumes / any lane) else the
//                app's western `Comics` / manga `Downloads` context property, exactly as the old
//                reader (contract §3). Callers of western comics pass NO pageStore and rely on this.

import QtQuick
import "ComicReaderState.js" as ComicReaderState

Item {
    id: reader

    // ================= public caller contract (byte-compatible with MangaReader.qml) =================
    // --- input properties ---
    property Item  backdrop
    property string seriesTitle: ""
    property string seriesId: ""
    property string seriesCover: ""            // series cover (for the Continue card)
    property var    chapters: []               // ALL entries, newest-first — {id, number, name(, url, sizeMB)}
    property string chapterId: ""              // incoming open target
    property string chapterLabel: ""           // incoming fallback label
    property bool   western: false             // GetComics comic vs manga (Comics store + "comic" namespace)
    property var    pageStore: null            // injected page store (Tankoban volumes / any lane); overrides the default store
    property string entryKind: "manga"         // "manga" chapters | "tankoban" volumes
    property string entryLabelPrefix: ""        // e.g. "Vol. " for tankoban

    // --- signals ---
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal sourceRequested(string entryId)     // a not-ready tankoban volume needs the series page's source chooser

    // --- store resolution (contract §3): injected pageStore wins, else western=Comics / manga=Downloads ---
    readonly property var store: pageStore ? pageStore
        : (western ? (typeof Comics !== "undefined" ? Comics : null)
                   : (typeof Downloads !== "undefined" ? Downloads : null))
    // western never sets entryKind but keeps its "comic" namespace; every other caller's entryKind
    // IS the namespace ("manga" chapters, "tankoban" volumes). Delegated to the pure library.
    readonly property string progressKind: ComicReaderState.progressKind(entryKind, western)

    // ================= injectable seams =================
    property var core: (typeof ComicReaderCore !== "undefined") ? ComicReaderCore : null
    property var progress: (typeof Progress !== "undefined") ? Progress : null
    // per-series persisted overrides for mode/direction ("" = use the ComicReaderState smart
    // default for this lane). Exposed as seams so the shell resolves defaults deterministically and
    // a later task (settings sheet) can wire the persistent store into them without changing this
    // orchestration. Empty by default -> smart default per lane.
    property string persistedMode: ""
    property string persistedDirection: ""
    // the backend openEntry() persisted-state map (spread overrides + coupling + bookmarks). None
    // of that is managed by orchestration yet, so it rides as an empty seam the surfaces/overlays fill.
    property var persistedState: ({})
    // per-page-change Progress recording is DEBOUNCED: Progress.record syncs QSettings to disk, and
    // Task 10's strip surface pushes currentPage many times a second during a scroll — recording on
    // every tick would be a disk-sync storm (MangaReader.qml:236 "don't do that per page-turn").
    // Crossing + close/shutdown record IMMEDIATELY (they must not wait for the timer). Test-tunable.
    property int recordDebounceMs: 600

    // ================= internal reading state (exposed for surfaces / HUD later) =================
    property var  _pages: []                    // resolved local pages [{index,url,group}] for the open entry
    readonly property int pageCount: _pages.length
    readonly property int max: pageCount        // THE page-count property callers/harness read
    property string curChapterId: ""            // the entry we're actually reading (crossing changes it)
    property int    currentPage: 1              // 1-based current page
    property int    maxSeen: 0                  // high-water mark (finished never un-finishes on reread)
    property string mode: "long_strip"          // "long_strip" | "double_page"
    property bool   rtl: false                  // reading direction (paints RTL when true)
    property int    zoomPercent: 100            // paged zoom (surfaces, later)
    property real   stripFraction: 0            // long-strip scroll fraction (resume + HUD scrub, later)
    property bool   chromeVisible: true         // HUD visibility (Task 11)
    property bool   modalOpen: false            // an overlay is up (Task 12)

    // crossing enable-state — Task 11's prev/next pills and Task 12's end card bind THESE instead
    // of re-deriving the newest-first math (index-1 = newer/next, index+1 = older/previous).
    readonly property bool hasNext:
        ComicReaderState.nextEntry(chapters, ComicReaderState.entryIndex(chapters, curChapterId)) !== null
    readonly property bool hasPrev:
        ComicReaderState.previousEntry(chapters, ComicReaderState.entryIndex(chapters, curChapterId)) !== null

    // --- lifecycle guards ---
    property bool _ready: false                 // construction finished — runtime chapterId changes are live
    property bool _resumeArmed: false           // the next load may restore the saved Continue spot
    property bool _pendingAtLast: false         // open the entry at its last page (previous-crossing)
    property bool _suspendRecord: false         // mutating state during load() must not emit records

    // --- current entry label (mirrors MangaReader curLabel, lines 170-175) ---
    readonly property string curLabel: {
        var i = ComicReaderState.entryIndex(chapters, curChapterId)
        var c = i >= 0 ? chapters[i] : null
        if (c) return (c.name && String(c.name).length) ? String(c.name)
                      : ((entryLabelPrefix.length ? entryLabelPrefix : "Chapter ") + (c.number !== undefined ? c.number : ""))
        return chapterLabel
    }

    // ================= entry lifecycle =================
    // load(): resolve the entry's local pages; if ready, resolve mode/direction, apply any saved
    // resume spot, then hand the pages to the backend. If NOT ready the shell stays on the
    // download/acquire path — the actual per-lane routing lives in startDownload() (which the
    // download panel / end card will call), matching the old reader. The body is guarded by
    // try/finally so an exception (store.localPages / progress.get / core.openEntry) can NEVER wedge
    // _suspendRecord true and silently disable all future recording.
    function load() {
        _suspendRecord = true
        try {
            _pages = (curChapterId.length && store) ? (store.localPages(curChapterId) || []) : []

            // mode + direction: per-series persisted override, else the smart default for this lane
            mode = persistedMode.length ? persistedMode : ComicReaderState.defaultMode(entryKind, western)
            var dir = persistedDirection.length ? persistedDirection : ComicReaderState.defaultDirection(entryKind, western)
            rtl = (dir === "rtl")

            if (_pages.length > 0) {
                currentPage = _pendingAtLast ? pageCount : 1
                maxSeen = currentPage
                stripFraction = 0
                _pendingAtLast = false
                _applyResume()                          // a matching Continue entry overrides the start spot
                maxSeen = Math.max(maxSeen, currentPage)
                if (core) core.openEntry(curChapterId, _pages, dir, persistedState)
            } else {
                currentPage = 1; maxSeen = 0; stripFraction = 0; _pendingAtLast = false
                // not downloaded — the acquire routing is startDownload() (per-lane). Nothing auto-fires.
            }
        } finally {
            _suspendRecord = false
        }
    }

    // restore the saved page + strip fraction BEFORE first paint when this open matches the saved
    // Continue entry (mirrors MangaReader load() resume, lines 280-290; strip fraction needs no
    // layout settle here because the surfaces are placeholders — Task 10 re-adds the settle).
    function _applyResume() {
        if (!_resumeArmed) return
        _resumeArmed = false
        var saved = (progress && seriesId.length) ? progress.get(progressKind, seriesId) : null
        var r = (saved && saved.resume) ? saved.resume : null
        if (r && String(r.chapterId) === curChapterId) {
            currentPage = Math.max(1, Math.min(pageCount, Number(r.page) || 1))
            maxSeen = Math.max(currentPage, Number(r.maxSeen) || 0)
            stripFraction = (mode === "long_strip") ? (Number(r.scrollFrac) || 0) : 0
        }
    }

    // startDownload() — the acquire router, byte-identical to MangaReader.qml:322-337. Tankoban
    // volumes are NOT page-downloaded: they route to the series page's source chooser. Western
    // issues carry the release permalink + size; manga chapters take the plain chapter API.
    function startDownload() {
        if (!curChapterId.length) return
        if (entryKind === "tankoban") { sourceRequested(curChapterId); return }
        if (!store) return
        if (western) {
            var i = ComicReaderState.entryIndex(chapters, curChapterId)
            var c = i >= 0 ? chapters[i] : null
            store.downloadIssue(curChapterId, (c && c.url) ? c.url : "", seriesId, seriesTitle,
                                curLabel, ((c && c.sizeMB) || 0) * 1024 * 1024)
        } else {
            store.downloadChapter(curChapterId, seriesId, seriesTitle, curLabel)
        }
    }

    // "ready" == the store can hand back local pages — the SAME test load() uses to pick
    // pages-vs-download (MangaReader entryReady, lines 464-468).
    function entryReady(id) {
        if (!store || !id) return false
        var lp = store.localPages(id)
        return !!(lp && lp.length > 0)
    }

    // open a specific entry (crossing / modal). Resume applies only if the saved spot matches.
    function openEntryById(id, atLast) {
        if (!id || !String(id).length) return
        _pendingAtLast = !!atLast
        _resumeArmed = true
        curChapterId = String(id)                   // -> onCurChapterIdChanged -> load()
    }

    // ================= crossing (newest-first, delegated to ComicReaderState) =================
    // record the OUTGOING entry's progress BEFORE jumping, then open the adjacent entry. Tankoban
    // volumes that aren't ready route to the source chooser and stay put (never land on an
    // unreadable volume) — mirrors MangaReader goNextChapter/goPrevChapter (lines 456-477).
    function goNext() {
        var nx = ComicReaderState.nextEntry(chapters, ComicReaderState.entryIndex(chapters, curChapterId))
        if (!nx) return
        var id = String(nx.id)
        if (entryKind === "tankoban" && !entryReady(id)) { sourceRequested(id); return }
        recordProgress()
        openEntryById(id, false)
    }
    function goPrev(atLast) {
        var pv = ComicReaderState.previousEntry(chapters, ComicReaderState.entryIndex(chapters, curChapterId))
        if (!pv) return
        var id = String(pv.id)
        if (entryKind === "tankoban" && !entryReady(id)) { sourceRequested(id); return }
        recordProgress()
        openEntryById(id, !!atLast)
    }

    // ================= progress (Continue) =================
    // Build the payload via ComicReaderState.progressPayload (byte-identical to contract §4.1) and
    // record it — BUT ONLY when max > 0 (MangaReader.qml:211 guard: progressPayload assumes max>0).
    // The cover is back-filled from the last saved record so a cover-less save never wipes a good
    // cover (MangaReader lines 214-219). This is the IMMEDIATE record (crossing + close use it directly).
    function recordProgress() {
        if (_suspendRecord) return
        if (!progress || !seriesId.length || max <= 0) return
        var cov = seriesCover
        if (!cov.length) {
            var prev = progress.get(progressKind, seriesId)
            if (prev && prev.cover) cov = String(prev.cover)
        }
        progress.record(ComicReaderState.progressPayload({
            seriesId: seriesId, kind: progressKind, seriesTitle: seriesTitle,
            label: curLabel, cover: cov, page: currentPage, max: max,
            chapterId: curChapterId, style: mode, scrollFrac: stripFraction, maxSeen: maxSeen
        }))
    }
    // debounced record — QSettings syncs to disk on every record(); don't do that per page-turn.
    Timer { id: saveSoon; interval: reader.recordDebounceMs; onTriggered: reader.recordProgress() }
    function recordProgressSoon() { saveSoon.restart() }

    // flush a final record + close the backend entry. This is DESTRUCTION semantics only — the hide
    // path flushes without closing (see onVisibleChanged). Guarded so a null core never errors.
    function shutdown() {
        recordProgress()
        if (core) core.closeEntry()
    }

    // ================= reactions =================
    // entry change: load the entry, then EAGERLY record it (the record is suppressed DURING load(),
    // so this trailing call persists the freshly-opened/crossed spot immediately — a crash before
    // the next page-turn/close otherwise leaves Continue one entry behind). Mirrors MangaReader.qml:157.
    onCurChapterIdChanged: { load(); recordProgress() }
    onCurrentPageChanged: {
        // CONTRACT for Task 10 (double-page): maxSeen is the completion high-water mark, and
        // `currentPage` is the pair ANCHOR in double mode. MangaReader.qml bumpSeen() (lines 242-249)
        // ALSO folded in the pair PARTNER index, because a chapter that ENDS on a pair never reaches
        // `maxSeen >= max` from the anchor alone (finished stays false forever). Task 10's double
        // surface MUST drive currentPage (or bump maxSeen) with the reading-HIGHEST page of the unit,
        // not just the anchor — otherwise completion regresses for pair-terminated entries.
        if (currentPage > maxSeen) maxSeen = currentPage
        if (_suspendRecord) return
        recordProgressSoon()                         // debounced — strip scroll would storm the disk otherwise
    }
    // runtime chapterId change (a caller re-targets the reader) — construction is handled by onCompleted
    onChapterIdChanged: { if (_ready && chapterId !== curChapterId) openEntryById(chapterId, false) }
    // callers HIDE (visible:false) on back and SHOW again to reopen — flush the Continue spot but
    // KEEP the backend entry open, or reopen-same-entry would show a blank reader. The entry is torn
    // down ONLY on destruction (Component.onDestruction).
    onVisibleChanged: {
        if (!_ready) return
        if (visible) forceActiveFocus()
        else recordProgress()                        // hide = flush only, never core.closeEntry()
    }

    Component.onCompleted: {
        _ready = true
        _resumeArmed = true
        if (chapterId.length) {
            if (curChapterId === chapterId) { load(); recordProgress() }   // handler didn't fire during construction
            else curChapterId = chapterId                                  // -> onCurChapterIdChanged -> load() + record
        } else {
            load()
        }
    }
    // the ONLY teardown of the backend entry — the reader is being destroyed for good, not hidden.
    Component.onDestruction: shutdown()

    // ================= placeholder surfaces (Task 10 replaces these with the real surfaces) =================
    focus: true
    Rectangle {
        anchors.fill: parent
        visible: reader.mode === "long_strip"
        color: "#0f0d15"
        Text {
            anchors.centerIn: parent
            color: "#8a8496"
            text: "Long Strip surface — page " + reader.currentPage + " / " + reader.max
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: reader.mode === "double_page"
        color: "#0c0a12"
        Text {
            anchors.centerIn: parent
            color: "#8a8496"
            text: "Double Page surface — page " + reader.currentPage + " / " + reader.max
        }
    }

    // an injected page store (or a future store) may not emit this exact progress/finished/failed
    // triple — don't spam "no such signal" warnings. A completed download re-runs load() so the
    // reader flips from the download path to the open pages (contract §3).
    Connections {
        target: reader.store
        ignoreUnknownSignals: true
        function onProgress(cid, done, total) { /* download line — chrome (Task 11) */ }
        function onFinished(cid) { if (cid === reader.curChapterId) reader.load() }
        function onFailed(cid, reason) { /* error placard — chrome (Task 11) */ }
    }
}
