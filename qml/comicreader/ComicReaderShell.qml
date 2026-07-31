// ComicReaderShell — the Comic Reader's orchestration spine (Task 9).
//
// This is the reader's ROOT: it exposes the exact public caller contract that qml/MangaReader.qml
// exposes today (Task 1 handoff, docs/superpowers/handoffs/2026-07-23-comicreader-public-contract.md),
// so at the Task 13 cutover MangaReader.qml collapses to `ComicReaderShell {}` and every caller —
// MangaSeries / ComicSeries / ComicSeriesPage / Main — keeps working byte-for-byte.
//
// Orchestration ONLY. It owns the entry lifecycle (load -> open, resume, crossing, close), drives
// the C++ backend + the Progress sink through injectable seams, and mounts the two reading surfaces
// (Task 10: ComicReaderStripSurface / ComicReaderDoubleSurface), toggled by `mode`. The surfaces
// PAINT; the shell still owns every DECISION. The Family Gradient HUD (Task 11) and the overlays
// (Task 12) mount INSIDE this shell later. No chrome, no overlays here yet.
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
import QtCore                                   // Settings — the house persistence sink
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

    // --- Task 11 chrome intents: the HUD / input emit these; Task 12 mounts the overlays that
    // consume them. Until then they are honest seams (no overlay exists), so `modalOpen` stays false. ---
    signal navigatorRequested()
    signal thumbnailsRequested()
    signal settingsRequested()
    signal bookmarkToggleRequested()
    signal bookmarksRequested()                // the bookmarks LIST (settings tool grid), not the toggle
    signal goToPageRequested()
    signal shortcutsRequested()
    signal loupeRequested()
    signal closeTopRequested()

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
    // NOTE (Task 3, plan 2026-07-28): the old `persistedMode`/`persistedDirection` override seams
    // are GONE. `layout` and `order` below are now the live truth, and the per-series record is the
    // persistent truth — one direction of flow, resolved by _applySeriesPrefs() through the single
    // tested migration. The seams existed only because the combined readingMode identity had to be
    // re-derived on every crossing; there is nothing left to re-derive.
    // the backend openEntry() persisted-state map (spread overrides + coupling + bookmarks). None
    // of that is managed by orchestration yet, so it rides as an empty seam the surfaces/overlays fill.
    property var persistedState: ({})
    // per-page-change Progress recording is DEBOUNCED: Progress.record syncs QSettings to disk, and
    // Task 10's strip surface pushes currentPage many times a second during a scroll — recording on
    // every tick would be a disk-sync storm (MangaReader.qml:236 "don't do that per page-turn").
    // Crossing + close/shutdown record IMMEDIATELY (they must not wait for the timer). Test-tunable.
    property int recordDebounceMs: 600

    // ---- cursor auto-hide (both references: TB2 ComicReader.cpp ~424-434, Reader 1's
    // cursorIdle/cursorHidden) — neither reference leaves an arrow parked on the page. Blanked
    // after cursorIdleMs of stillness while the chrome is away; held while the chrome is up (a
    // pointer resting on a command must not vanish out from under it). Test-tunable, like
    // recordDebounceMs above.
    //
    // 2500ms, and the number is NOT arbitrary: the approved ledger says "Toolbar, title toast,
    // progress rail, and cursor sleep together after 2.5 seconds of inactivity." The HUD's
    // autoHideMs carries the same dial; they must never drift apart, so the chrome gate pins both.
    property int cursorIdleMs: 2500
    property bool _cursorIdle: false
    function _pokeCursor() { _cursorIdle = false; cursorIdleTimer.restart() }
    Timer { id: cursorIdleTimer; interval: reader.cursorIdleMs; onTriggered: reader._cursorIdle = true }

    // THE cursor+chrome wake door. There is real history here: the HUD used to come back on mouse
    // movement while the cursor stayed hidden, so you could see the controls and not your pointer.
    //
    // Note what this deliberately does NOT do. The plan sketched `cursorHideArea.cursorShape =
    // Qt.ArrowCursor` — an imperative assignment onto a bound property, which in QML DESTROYS the
    // binding permanently: the cursor could then never blank again for the rest of the session.
    // (That exact trap is already documented a few hundred lines below, on the side-scroller thumb
    // whose `y:` binding one drag wiped out.) Clearing `_cursorIdle` and raising `chromeVisible`
    // re-evaluates the SAME binding synchronously to ArrowCursor, which is the force the fix needs,
    // and leaves it able to blank again after the next 2.5s of stillness. The shell gate asserts
    // both halves: Arrow now, and still blankable afterwards.
    function restoreCursorAndChrome() {
        _cursorIdle = false
        chromeVisible = true
        cursorIdleTimer.restart()
        hud.reveal()
    }

    // ================= internal reading state (exposed for surfaces / HUD later) =================
    property var  _pages: []                    // resolved local pages [{index,url,group}] for the open entry
    readonly property int pageCount: _pages.length
    readonly property int max: pageCount        // THE page-count property callers/harness read
    property string curChapterId: ""            // the entry we're actually reading (crossing changes it)
    property int    currentPage: 1              // 1-based current page
    property int    maxSeen: 0                  // high-water mark (finished never un-finishes on reread)
    // ---- LAYOUT and ORDER: two INDEPENDENT choices (Task 3, plan 2026-07-28) ----
    // Hemanth's ruling for the overhaul: Colosseum's reading model is better than both reference
    // apps because it keeps these apart, and it must not be weakened.
    //   layout — presentation ONLY: "single_page" | "paired_pages" | "long_strip"
    //   order  — the physical page ORDERING: "ltr" (comic) | "rtl" (manga)
    // Changing the layout never changes the direction, and choosing a direction never throws you
    // out of Long Strip. Both are resolved per series by _applySeriesPrefs() and written by
    // setLayout()/setOrder(); nothing else assigns them.
    property string layout: "long_strip"
    property string order: "ltr"
    // ---- compatibility aliases, READONLY: the surfaces + input still speak mode/rtl ----
    // Single Page has no surface until Task 4, so it deliberately maps to ITSELF and matches
    // neither mounted surface rather than pretending to be a pair.
    readonly property string mode: layout === "paired_pages" ? "double_page" : layout
    readonly property bool   rtl: order === "rtl"
    // the OLD single user-facing identity (Hemanth 2026-07-25): Manga / Comic / Strip. Still read by
    // the HUD mode chips + the settings sheet, which write it back through setReadingMode(), until
    // Task 5/8 replace them with the Layout menu. A derived compatibility VIEW, never state — and
    // deliberately lossy: the old identity cannot express Single Page, so single_page reads as
    // manga/comic here (whichever the order says).
    readonly property string readingMode: ComicReaderState.readingModeFrom(mode, rtl)
    property int    zoomPercent: 100            // paged zoom (surfaces, later)
    property real   stripFraction: 0            // long-strip scroll fraction (resume + HUD scrub, later)
    property bool   chromeVisible: true         // HUD visibility (Task 11)
    // night veil — a LIVE reading-comfort dim over the page (settings surface 02). The settings
    // sheet writes this level; the veil overlay below binds its opacity to it. Not load()-derived
    // per entry (unlike mode/direction), so it survives crossings for free.
    property string nightVeil: "off"            // "off" | "low" | "high"
    // gutter shadow strength for double-page mode (settings surface 02). The sheet writes this;
    // the double surface below binds its gutter shadow to it. Presets 0/.22/.35/.55 (Off..Strong).
    property real   gutterStrength: 0.35
    // ---- backend-owned settings the sheet READS (every access guarded: the harness injects a
    // partial core, and a missing property must fall back to the real default, not to 0/"") ----
    // who owns the double-page phase: the core reports "mode:phase:confidence"; the sheet's
    // Coupling row shows the MODE ("auto" = the probe decided, "manual" = nudged by hand).
    readonly property string couplingMode:
        (core && core.couplingState) ? String(core.couplingState).split(":")[0] : "auto"
    readonly property int stripWidthPct:
        (core && core.stripWidthPct !== undefined) ? core.stripWidthPct : 78
    readonly property int stripGap:
        (core && core.stripGap !== undefined) ? core.stripGap : 0
    readonly property bool memorySaver:
        (core && core.memorySaver !== undefined) ? core.memorySaver === true : false
    // ================= the ONE overlay coordinator (Task 5) =================
    // Hemanth's approved interaction contract: only ONE temporary surface at a time, and the comic
    // never shifts to make room for it. The chrome only RAISES intents — this is the single place
    // that decides what is open, so two surfaces can never both believe they are.
    //
    // The commands are live from Task 5 even though their surfaces land in Tasks 6-9. That is
    // deliberate and it is the honest state: pressing Pages really does take ownership (the command
    // goes gold, the chrome stops sleeping, Escape gives it back). It is not a fake, and it is not
    // hidden behind a "coming soon" that would have to be removed later.
    property string activeOverlay: ""            // "" | pages | image | layout | loupe
    function openOverlay(name) {
        var n = String(name)
        // re-asking for the surface that is already open CLOSES it — one temporary surface, and the
        // command that raised it is also the way back out.
        activeOverlay = (activeOverlay === n) ? "" : n
        restoreCursorAndChrome()
        // Task 8 pauses Auto-scroll here: any temporary surface must stop the motion immediately.
    }
    // Escape, resolved ONE layer at a time. "never unexpectedly leave the book" is the rule that
    // matters most: Back is the only reader-to-library action, and Escape is not a second one.
    function closeTop() {
        var wasVisible = chromeVisible
        _cursorIdle = false                      // the pointer always comes back, whatever we close
        cursorIdleTimer.restart()
        if (settingsSheet.opened) { settingsSheet.close(); chromeVisible = true; return }
        if (activeOverlay.length) { activeOverlay = ""; chromeVisible = true; return }
        chromeVisible = !wasVisible
    }

    // an overlay is up — swallows background input + pauses auto-hide. Aggregated off the MOUNTED
    // overlays only. `activeOverlay` deliberately does NOT join this OR wholesale: gating the whole
    // keyboard behind a surface that has no pixels on screen would be a trap, not a contract. Each
    // of Tasks 6-9 adds its own mount here as it lands — Task 6 added the Pages filmstrip, which now
    // genuinely covers the comic, so an open filmstrip owns the keyboard (everything but Escape,
    // ComicReaderInput's existing law) exactly like the sheet does. Task 7 adds the Image panel on
    // the same terms: it is a smaller surface, but it carries live sliders, and a page turn landing
    // under a drag would be the same defect.
    readonly property bool modalOpen: settingsSheet.opened || pagesOverlay.open || imagePopover.open

    // The current entry's slot in `chapters` (newest-first). Public and readonly on the old reader
    // (MangaReader.qml:165) and read by tests/manga_tankoban_page_harness.qml — the Task 1 contract
    // survey captured `max` but missed this one. Exposing it also stops hasNext/hasPrev below from
    // each re-deriving the same lookup.
    readonly property int curIndex: ComicReaderState.entryIndex(chapters, curChapterId)

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
    // Replaying a series' remembered preferences is not the reader MAKING one. Without
    // this, opening a book would stamp that book's night veil onto the GLOBAL seed and
    // re-write the record it had just read — the same leak F2 closed for the strip measure.
    property bool _replayingPrefs: false
    property real _pendingStripFrac: 0          // one-shot: the saved scrollFrac awaiting layout settle
    // is a restore in flight? Consumed by the shell gate; also the honest answer to "did resume arm".
    readonly property bool _stripRestorePending: stripRestore.running || _stripRestoreTries > 0

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

            // Pull this series' identity override and this entry's remembered reader state BEFORE
            // either is read below — load() is the one place both are consumed, so it is the one
            // place they have to be fresh (a crossing lands here too, with a new entry id).
            _applySeriesPrefs()
            _applyEntryPrefs()

            // layout + order are already resolved for this series by _applySeriesPrefs() above
            // (record -> global last-choice -> lane default, through ONE tested migration). A
            // crossing lands here too, so the choice survives a chapter/volume jump for free.

            if (_pages.length > 0) {
                currentPage = _pendingAtLast ? pageCount : 1
                maxSeen = currentPage
                stripFraction = 0
                _pendingStripFrac = 0                   // one-shot, PER ENTRY — a stale fraction from the
                _pendingAtLast = false                  // previous book must never restore into this one
                _applyResume()                          // a matching Continue entry overrides the start spot
                maxSeen = Math.max(maxSeen, currentPage)
                if (core) core.openEntry(curChapterId, _pages, order, persistedState)
                // Physically move the strip to the restored spot. ONE door (stripRestore), settle-gated:
                // the column positions its delegates a vsync later, and its heights are estimates until
                // decodes land — an immediate jump reads y=0 for unrealized delegates and lands at the top.
                if (mode === "long_strip" && (_pendingStripFrac > 0 || currentPage > 1))
                    _armStripRestore()
            } else {
                currentPage = 1; maxSeen = 0; stripFraction = 0; _pendingStripFrac = 0; _pendingAtLast = false
                // not downloaded — the acquire routing is startDownload() (per-lane). Nothing auto-fires.
            }
        } finally {
            _suspendRecord = false
        }
    }

    // restore the saved page + strip fraction BEFORE first paint when this open matches the saved
    // Continue entry (mirrors MangaReader load() resume, lines 280-290). The shell publishes
    // stripFraction (the HUD reads it) and ARMS the one-shot _pendingStripFrac; load() then opens the
    // ONE restore door (stripRestore) which physically moves the column once it has settled.
    function _applyResume() {
        if (!_resumeArmed) return
        _resumeArmed = false
        var saved = (progress && seriesId.length) ? progress.get(progressKind, seriesId) : null
        var r = (saved && saved.resume) ? saved.resume : null
        if (r && String(r.chapterId) === curChapterId) {
            currentPage = Math.max(1, Math.min(pageCount, Number(r.page) || 1))
            maxSeen = Math.max(currentPage, Number(r.maxSeen) || 0)
            stripFraction = (mode === "long_strip") ? (Number(r.scrollFrac) || 0) : 0
            _pendingStripFrac = (mode === "long_strip") ? (Number(r.scrollFrac) || 0) : 0
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
    // Contract alias for the old reader's public name (MangaReader.qml:445 before the cutover).
    // qml/BakeoffStripHost.qml calls openChapterById by name; the Task 1 contract survey missed it
    // because it only read the three series pages. Kept so "callers untouched" holds for EVERY
    // caller, not just the surveyed ones — a missing method here is a runtime "not a function" that
    // only bites whoever opens that surface.
    function openChapterById(id, atLast) { openEntryById(id, atLast) }
    // Same story for the crossing pair (MangaReader.qml:456/472). The reader reads volumes as well
    // as chapters now, so the internal names dropped "Chapter" — but the old names are the published
    // ones, so they stay as aliases rather than making every consumer chase a rename.
    function goNextChapter() { goNext() }
    function goPrevChapter(atLast) { goPrev(atLast) }

    function openEntryById(id, atLast) {
        if (!id || !String(id).length) return
        // A crossing replaces the backend entry, so the OUTGOING book's record has to land before
        // curChapterId moves — otherwise the debounce fires against the next book and files the old
        // book's overrides under the new book's id.
        if (_ready && curChapterId.length && String(id) !== curChapterId) {
            entrySave.stop()
            _saveEntryBlob()
        }
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

    // ================= page navigation (Task 11 input + HUD drive these) =================
    // Within-entry page/unit navigation — DISTINCT from crossing (goNext/goPrev change the ENTRY).
    // Double mode snaps to canonical units (via the backend's unitForPage); strip mode nudges the
    // scroll by viewport-heights (the strip surface owns the smooth-wheel feel for gestures).
    function _unitBoundsForIndex(idx0) {
        // [lo0, hi0] 0-based page indices of the canonical unit containing idx0 (self if no pairing)
        if (core && core.unitForPage) {
            var u = core.unitForPage(idx0)
            if (u) {
                var a = []
                if (u.rightIndex !== undefined && u.rightIndex >= 0) a.push(u.rightIndex)
                if (u.leftIndex !== undefined && u.leftIndex >= 0) a.push(u.leftIndex)
                if (a.length) return [Math.min.apply(null, a), Math.max.apply(null, a)]
            }
        }
        return [idx0, idx0]
    }
    // F5: the end of a volume ANNOUNCES itself instead of going quiet. Pressing forward at the last
    // page used to do nothing at all, which is indistinguishable from a dropped input — you press
    // again, harder, and wonder if the reader is stuck. If there is a next entry the toast says how
    // to reach it, using the binding that actually exists (Alt+Right / the next pill), never an
    // invented one.
    function _endOfVolumeToast() {
        hud.showToast(hasNext ? "End of volume — Alt+Right for the next" : "End of volume")
    }
    function pageNext() {
        if (layout === "paired_pages") {
            var t = _unitBoundsForIndex(currentPage - 1)[1] + 1
            if (t < max) { currentPage = _unitBoundsForIndex(t)[0] + 1; return }
            _endOfVolumeToast()
        } else if (layout === "single_page") {
            // Single Page walks ONE page at a time and snaps to no unit — that is the whole point of
            // the layout. Without this branch a forward press fell through to the strip case below and
            // scrolled an unmounted ListView, so the page never turned at all.
            if (currentPage < max) { currentPage = currentPage + 1; return }
            _endOfVolumeToast()
        } else {
            // Strip only announces when the column is genuinely parked at (or gliding into) the
            // bottom, so a normal page-down mid-book stays silent.
            if (!stripSurface.atEnd) { _stripScroll(0.9); return }
            _endOfVolumeToast()
        }
    }
    function pagePrev() {
        if (layout === "paired_pages") {
            var t = _unitBoundsForIndex(currentPage - 1)[0] - 1
            if (t >= 0) currentPage = _unitBoundsForIndex(t)[0] + 1
        } else if (layout === "single_page") {
            if (currentPage > 1) currentPage = currentPage - 1
        } else _stripScroll(-0.9)
    }
    function goToPageIndex(p1) {
        var p = Math.max(1, Math.min(Math.max(1, max), Math.round(p1)))
        if (mode === "double_page") p = _unitBoundsForIndex(p - 1)[0] + 1
        currentPage = p
    }
    // Keyboard scrolling GLIDES (E2). Space/PageDown feed the same drain the wheel feeds, so they
    // decelerate identically and a press mid-glide adds to the backlog instead of fighting it. The
    // surface clamps the landing itself; the old raw contentY write here is what produced
    // jump-then-slide when a key landed while a wheel glide was still running.
    function _stripScroll(screens) {
        var span = stripSurface.contentHeight - stripSurface.height
        if (span <= 0) return
        stripSurface.smoothScrollBy(screens * stripSurface.height)
    }
    // A scrub seek is INSTANT and FINAL — it must land where the thumb was released and carry no
    // leftover glide across the jump, so it takes the halt door rather than the drain.
    function scrubToFraction(frac) {
        var f = Math.max(0, Math.min(1, frac))
        stripFraction = f
        var span = stripSurface.contentHeight - stripSurface.height
        if (span > 0) stripSurface.haltScrollAt(f * span)
    }
    // What page a scrub fraction actually lands on. In Strip that is a GEOMETRY question — pages
    // have different heights, so a linear pages*fraction estimate lies about where you'd land — so
    // ask the backend (core.stripPageAtCenter, over the real strip viewport) rather than re-derive
    // it. The HUD's scrub bubble reads THIS while hovering/dragging instead of recomputing its own
    // estimate (FIX 2).
    function pageAtFraction(frac) {
        var f = Math.max(0, Math.min(1, frac))
        if (mode === "long_strip" && core && core.stripPageAtCenter) {
            var span = Math.max(0, stripSurface.contentHeight - stripSurface.height)
            var p = core.stripPageAtCenter(f * span, stripSurface.height)
            if (p >= 0) return p + 1
        }
        return Math.max(1, Math.round(f * (Math.max(1, max) - 1)) + 1)
    }
    // Home/End are instant and final, like a scrub seek — the halt door, not the drain.
    function firstPageNav() { currentPage = 1; if (mode === "long_strip") stripSurface.haltScrollAt(0) }
    function lastPageNav() {
        goToPageIndex(max)
        if (mode === "long_strip")
            stripSurface.haltScrollAt(Math.max(0, stripSurface.contentHeight - stripSurface.height))
    }
    // ---- the two INDEPENDENT choices (Task 3) ----
    // Each setter writes ONE of them and nothing else. That is the whole point: picking Long Strip
    // must not flip a manga to left-to-right, and picking a direction must not throw you out of
    // Long Strip. Both remember the choice for THIS series AND seed the global last-choice, so a
    // series you have never opened follows the taste you keep picking (MangaReader.setDirection
    // wrote both for the same reason: re-picking per new series is a chore).
    function setLayout(value) {
        if (!ComicReaderState.layoutIsValid(value)) return   // unknown layout: refuse, never wedge
        if (value === layout) return
        // KEEP YOUR PAGE across the switch. Ported from the reader this replaced
        // (MangaReader.setStyle): changing how pages are laid out is not a reason to lose your
        // place, and every reader in the family gets this right.
        var keep = currentPage
        layout = value
        if (max > 0 && keep > 1) {
            currentPage = keep
            // Entering the strip, the seek MUST be deferred. The lineage's comment says why, and it
            // is the whole bug: the view positions its children a vsync later, so an immediate jump
            // reads y=0 for every not-yet-realized delegate and lands at the top of the book. The
            // 300ms settle is TB2's number.
            if (layout === "long_strip") _armStripRestore()
        }
        if (_ready) {
            globalPrefs.layout = value
            _saveSeriesPrefs()
        }
    }
    function setOrder(value) {
        if (!ComicReaderState.orderIsValid(value)) return
        if (value === order) return
        order = value
        if (_ready) {
            globalPrefs.order = value
            _saveSeriesPrefs()
        }
    }
    // COMPATIBILITY: the single Manga/Comic/Strip identity the HUD chips + settings sheet still
    // speak, expressed on top of the two independent choices. Manga/Comic set BOTH (the identity
    // always carried a direction); Strip sets the layout ALONE and deliberately leaves the order
    // where it is — under the old identity "strip" forced LTR, and that forcing is exactly what
    // Task 3 removes. An unknown identity is ignored outright, so a frozen or invented mode name
    // can never reach the reader through this door.
    function setReadingMode(rm) {
        if (rm === "strip") { setLayout("long_strip"); return }
        if (rm !== "manga" && rm !== "comic") return
        setLayout("paired_pages")
        setOrder(rm === "manga" ? "rtl" : "ltr")
    }
    // M cycles the three identities Manga -> Comic -> Strip -> Manga.
    function cycleMode() {
        var ring = ["manga", "comic", "strip"]      // NOT `order` — that is a shell property now
        var i = ring.indexOf(readingMode)
        setReadingMode(ring[(i < 0 ? 0 : (i + 1) % ring.length)])
    }
    // B toggles a bookmark on the CURRENT page. The HUD's scrub-bar ticks bind to `liveBookmarks`
    // (below), never to the load-time persistedState snapshot — so a toggle actually moves them.
    onBookmarkToggleRequested: {
        if (!core || !core.toggleBookmark || max <= 0) return
        core.toggleBookmark(currentPage - 1)
        var on = core.bookmarks().indexOf(currentPage - 1) >= 0
        hud.showToast(on ? "Bookmarked p." + currentPage : "Bookmark removed")
    }

    // Live bookmark list — the backend's own bookmarks(), refreshed off the core's bookmarksChanged
    // (a toggle) and entryChanged (a fresh open/crossing swaps in the NEW entry's bookmarks).
    property var liveBookmarks: []
    function _refreshBookmarks() { liveBookmarks = (core && core.bookmarks) ? core.bookmarks() : [] }

    // A hand nudge is a statement about the SERIES, not just this chapter (F3): the phase is a
    // property of how the volume was scanned, so the next chapter should open already phased.
    function nudgeCoupling() {
        if (!core || !core.nudgeCoupling) return
        core.nudgeCoupling()
        var phase = core.couplingState ? String(core.couplingState).split(":")[1] : ""
        if (phase) _saveSeriesPrefs({ cp: phase })
    }
    // Fix ONE page's pairing without re-phasing the book (that is what nudgeCoupling/P does). Cycle
    // auto -> spread -> single -> auto, matching Reader 1's cycleSpreadOverride. pageInfo reports the
    // override as absent / true / false (ComicReaderTypes.cpp PageMeta::toVariantMap), so absence IS
    // the auto state.
    function cycleSpreadOverride(page0) {
        if (!core || !core.pageInfo || !core.setSpreadOverride) return
        var info = core.pageInfo(page0)
        var cur = info.spreadOverride === true ? "spread"
                : info.spreadOverride === false ? "single" : "auto"
        var nxt = cur === "auto" ? "spread" : (cur === "spread" ? "single" : "clear")
        core.setSpreadOverride(page0, nxt)
        entrySave.restart()                          // the override is per-book memory
        hud.showToast("Page " + (page0 + 1) + " pairing: " + (nxt === "clear" ? "auto" : nxt))
    }
    // Resolve which page a right-click in double-page targets, or -1 when out of scope (routes to
    // Settings instead: long_strip, no unit, or the backend seam is absent). rightIndex sits on the
    // physical RIGHT in RTL manga and the physical LEFT in LTR comics — ground-truthed against
    // ComicReaderDoubleSurface's rightIndexX/leftIndexX (rightImg.x: rtl ? _halfW : 0), not assumed.
    function _spreadOverrideTargetPage(x) {
        if (mode !== "double_page" || max <= 0 || !core || !core.unitForPage) return -1
        var u = core.unitForPage(currentPage - 1)
        if (!u || u.rightIndex === undefined || u.rightIndex < 0) return -1
        var leftHalf = x < width / 2
        if (u.leftIndex !== undefined && u.leftIndex >= 0) {
            return leftHalf ? (rtl ? u.leftIndex : u.rightIndex)
                             : (rtl ? u.rightIndex : u.leftIndex)
        }
        return u.rightIndex
    }
    // Wired from comicInput.openContextMenu: the approved design mock is explicit — "spread override
    // stays a direct right-click on the page itself." Cycle the clicked page's override in
    // double-page over a real unit; everywhere else (and single/spread units) fall through to Settings.
    function _onContextMenu(x, y) {
        var pg = _spreadOverrideTargetPage(x)
        if (pg >= 0) cycleSpreadOverride(pg)
        else settingsRequested()
    }
    // Hand the double-page phase back to the auto-coupling probe (the settings sheet's Auto chip).
    // Reset means FORGET, including the series-wide seed (F3). Leaving `cp` behind would let the
    // next chapter re-apply the exact phase you just reset, which reads as the reset not working.
    function resetCoupling() {
        if (core && core.resetCoupling) core.resetCoupling()
        _saveSeriesPrefs({ cp: null })          // null = delete the key, see _saveSeriesPrefs
    }
    // Long-strip taste: portrait page width % + inter-page gap px. The core owns the strip geometry,
    // so these read straight off it; ONE setter carries both so changing either preserves the other.
    // Route through the strip surface while it is the live one: rescaling the column moves every
    // page, so without anchoring a Page-width tap would silently scroll you somewhere else in the
    // book. Off the strip there is no viewport to hold, so the plain call is right.
    // `persist` defaults TRUE: every caller that is a user acting on the settings sheet wants the
    // choice remembered. _applySeriesPrefs passes false because it is replaying memory, not making
    // it. The persistence lives HERE rather than in an onStripWidthPctChanged handler on purpose:
    // that handler fires for any backend change including a per-series apply, so persisting there
    // rewrote the global seed just for opening a book.
    function setStripLayout(widthPct, gap, persist) {
        if (!core || !core.setStripLayout) return
        if (mode === "long_strip" && stripSurface && stripSurface.active)
            stripSurface.applyLayout(widthPct, gap)
        else
            core.setStripLayout(widthPct, gap)
        if (persist === false || !_ready) return
        globalPrefs.stripWidthPct = widthPct       // the seed for series with no opinion yet
        globalPrefs.stripGap = gap
        _saveSeriesPrefs({ sw: widthPct, sg: gap })
    }
    function setMemorySaver(on) { if (core && core.setMemorySaver) core.setMemorySaver(on === true) }

    // ================= the Image panel's adjustments (Task 7) =================
    // The backend's normalised profile, mirrored here so the panel, the veil and
    // the per-series record all read ONE value.
    property var _coreRenderProfile: ({})
    // What the Image panel actually READS. It is the backend's map with exactly
    // one substitution: `nightFilter` is answered by the reader's real veil.
    //
    // That substitution is the whole night-filter decision, so it is worth saying
    // plainly. The night filter is NOT a render transform. It is the black veil
    // this shell already paints over the surfaces (see the nightVeil Rectangle),
    // and it stays that way for two reasons: it is a control you toggle WHILE
    // looking at a page, and a composited overlay is free where a pixel transform
    // would bump the render revision, throw away every scaled entry and re-scale
    // the visible pages just to dim them; and the reader already HAS a night
    // control (the settings sheet's Off/Low/High), so baking a second one into the
    // pixels would leave two that could disagree. One veil, one painter — the
    // panel's switch and the sheet's chips are two views of the same value, and
    // the profile merely RECORDS it so it can be remembered per series.
    readonly property var renderProfile: {
        var p = {}
        for (var k in _coreRenderProfile) p[k] = _coreRenderProfile[k]
        p.nightFilter = (nightVeil !== "off")
        return p
    }

    // ---- applying: immediate, but throttled ----
    // Hemanth approved "changes are previewed immediately", and that is exactly
    // what this does — the FIRST change of a gesture is applied on the spot, so a
    // slider shows on the page while you drag rather than on release. But
    // "immediate" and "sixty times a second" are not the same thing: every
    // pixel-affecting change bumps the render revision and drops the whole scaled
    // tier, so an untrimmed drag would ask the worker pool to re-scale every
    // visible page on every frame. This is a leading+trailing throttle: apply now,
    // then coalesce whatever else arrives inside the window and apply the LAST of
    // it when the window closes. Nothing is ever dropped — the final value of a
    // drag always lands — and the reader sees ~12 updates a second instead of 60.
    property int renderApplyMs: 80
    property var _pendingRenderProfile: null
    Timer { id: renderThrottle; interval: reader.renderApplyMs; onTriggered: reader._flushRenderProfile() }

    // THE door the Image panel calls. `profile` is always a COMPLETE map — the
    // backend's setRenderProfile REPLACES rather than merges, and the popover
    // builds the whole map from the live one for exactly that reason.
    function setRenderProfile(profile) {
        if (!profile) return
        if (renderThrottle.running) { _pendingRenderProfile = profile; return }
        _applyRenderProfile(profile)
        renderThrottle.restart()
    }
    function _flushRenderProfile() {
        if (!_pendingRenderProfile) return       // window closed with nothing waiting: stop
        var next = _pendingRenderProfile
        _pendingRenderProfile = null
        _applyRenderProfile(next)
        renderThrottle.restart()                 // a drag still in progress keeps the window open
    }
    function _applyRenderProfile(profile) {
        if (!profile) return
        // The veil is the night control; the profile records it. Never DOWNGRADE
        // an existing "high" to "low" — the sheet's three-way choice is a finer
        // statement of the same on/off the panel's switch makes.
        var wantNight = profile.nightFilter === true
        if (wantNight !== (nightVeil !== "off")) nightVeil = wantNight ? "low" : "off"
        if (core && core.setRenderProfile) core.setRenderProfile(profile)
        _coreRenderProfile = (core && core.renderProfile) ? core.renderProfile() : profile
        if (_ready && !_replayingPrefs) renderSave.restart()
    }
    // The record is per SERIES and it is written to QSettings, which syncs to
    // disk. A drag must not do that sixty times, or even twelve — so the write is
    // debounced well past the end of the gesture, exactly like recordProgress and
    // the entry blob.
    Timer { id: renderSave; interval: 500; onTriggered: reader._saveRenderProfile() }
    function _saveRenderProfile() {
        if (!_ready) return
        _saveSeriesPrefs({ renderProfile: reader.renderProfile })
    }

    // ---- the settings sheet's two danger actions (the sheet arms them; this fires) ----
    // Clear resume: forget THIS series' Continue spot. The book keeps its bookmarks, spread
    // overrides and coupling — only "where you were" is dropped, so the next open starts at page 1.
    function clearResume() {
        if (progress && seriesId.length)
            progress.forget(progressKind, seriesId)
        _resumeArmed = false
    }
    // Reset series: the bigger hammer — resume AND every remembered reader decision for this book
    // (mode/direction override, spread overrides, bookmarks, coupling), then reopen from scratch so
    // the lane default and a fresh auto-coupling probe decide again.
    function resetSeries() {
        clearResume()
        // Forget the STORED records too, not just the live values — now that these survive a
        // relaunch, clearing only memory would have the book quietly re-dress itself on next open.
        // Global taste (night veil, gutter, strip measure, memory saver) is deliberately NOT reset:
        // "Reset series" is about this book, not about your reader.
        entrySave.stop()
        entryRecords.all = ComicReaderState.storePut(entryRecords.all, curChapterId, null)
        seriesRecords.all = ComicReaderState.storePut(seriesRecords.all, seriesId, null)
        persistedState = ({})
        // no seams to clear any more: the record IS the memory, and load() -> _applySeriesPrefs()
        // now re-resolves layout/order from the (just-deleted) record -> the global last-choice ->
        // the lane default, exactly as a series you had never opened would.
        load()
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
        // Flush the book's record NOW — closeEntry() wipes the backend state the blob is read from,
        // and a pending debounce would be writing an already-cleared entry (or never fire at all).
        entrySave.stop()
        _saveEntryBlob()
        // ...and the Image panel's adjustments, for the same reason: a debounce still
        // in flight when the reader closes the book would never fire, and the last
        // thing you did to the picture would be the one thing not remembered. Flushed
        // BEFORE closeEntry() so `renderProfile` still reads the live profile.
        _flushRenderProfile()     // the last value of an in-flight drag
        renderThrottle.stop()     // ...and close the window flush() just re-opened
        renderSave.stop()
        _saveRenderProfile()
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
    // NOTE (Task 3): the old onPersistedModeChanged / onPersistedDirectionChanged reactions are
    // gone with the seams. A HUD/settings toggle now calls setLayout()/setOrder(), which write the
    // live state AND the record in one move — there is no second copy to keep in step.
    // callers HIDE (visible:false) on back and SHOW again to reopen — flush the Continue spot but
    // KEEP the backend entry open, or reopen-same-entry would show a blank reader. The entry is torn
    // down ONLY on destruction (Component.onDestruction).
    // Ownership of a temporary surface changing is one of the two moments the cursor could be left
    // blanked under a surface that now owns the pointer. Restore the arrow first, every time.
    onActiveOverlayChanged: { _cursorIdle = false; cursorIdleTimer.restart() }

    onVisibleChanged: {
        if (!_ready) return
        // Arriving and leaving are both "the pointer is fresh" moments, and they get the SAME
        // treatment — one meaning, no asymmetry to get wrong. Leaving especially: never hand the
        // library back a hidden cursor, the reader's blank-cursor state must not outlive the reader
        // being on screen. (An earlier draft STOPPED the clock on hide instead; the callers hide
        // this same instance on back and show it again to reopen, so a stop with no matching re-arm
        // meant the pointer never slept again for the rest of the session. The shell gate caught it.)
        _pokeCursor()
        if (visible) { forceActiveFocus(); return }
        // hide = flush only, never core.closeEntry() — the entry stays open so coming back is instant.
        recordProgress()
        // ...but flush the BOOK's record too (E6). Hiding is leaving: the reader goes away when you
        // navigate out, and the 800ms entrySave debounce may still be pending. If the app is closed
        // or killed inside that window, a spread override or coupling nudge you just made was only
        // ever in memory. shutdown() already does this; the hide path flushed progress alone.
        entrySave.stop()
        _saveEntryBlob()
    }

    Component.onCompleted: {
        // Globals first: the very first entry must open already wearing your night veil and strip
        // measure, not flash the defaults and correct itself a frame later.
        _applyGlobalPrefs()
        _ready = true
        _resumeArmed = true
        cursorIdleTimer.restart()
        if (chapterId.length) {
            if (curChapterId === chapterId) { load(); recordProgress() }   // handler didn't fire during construction
            else curChapterId = chapterId                                  // -> onCurChapterIdChanged -> load() + record
        } else {
            load()
        }
    }
    // the ONLY teardown of the backend entry — the reader is being destroyed for good, not hidden.
    Component.onDestruction: shutdown()

    // ================= persistence (three stores, mirroring MangaReader.qml) =================
    // The old reader keeps globals + per-series overrides + per-chapter records in three separate
    // QSettings categories. Same split here, because the three kinds of memory genuinely differ:
    //
    //   comicReader        GLOBAL taste — night veil, gutter, strip width/gap, memory saver. These
    //                      follow you to every book; nobody wants to re-dim the page per volume.
    //   comicReaderSeries  PER-SERIES — which identity a series reads as. THIS is the one that
    //                      really varies per book (One Piece is Manga, a webtoon is Strip).
    //   comicReaderEntries PER-ENTRY — the backend's own persisted blob (spread overrides, coupling
    //                      verdict, bookmarks). Per chapter/volume, not per series: a spread
    //                      override is about the pages in THAT book.
    //
    // The Settings elements are dumb sinks; every map decision is a tested pure function in
    // ComicReaderState.js. Reads are total — a corrupt store degrades to "no memory", never a throw.
    Settings {
        id: prefs
        category: "comicReader"
        property string nightVeil: "off"
        property real   gutterStrength: 0.35
        property int    stripWidthPct: 78
        property int    stripGap: 0
        property bool   memorySaver: false
        // the last LAYOUT and the last ORDER you picked anywhere become the defaults for a series
        // you've never touched (MangaReader.setDirection writes the global AND the per-series
        // override for the same reason). "" = never chosen -> the lane default decides.
        // Two keys, not one, because Task 3 made them independent.
        property string layout: ""
        property string order: ""
        // LEGACY: the combined identity this global used to hold. Kept so the first launch after
        // the update still understands a global written by the shipped reader. READ, never written
        // again — _applySeriesPrefs migrates it, setLayout/setOrder write the two keys above.
        property string readingMode: ""
    }
    Settings { id: seriesStore; category: "comicReaderSeries"; property string all: "{}" }
    Settings { id: entryStore;  category: "comicReaderEntries"; property string all: "{}" }

    // ---- injectable seams, like `core` / `progress` / `pageStore` ----
    // Everything below reads and writes through THESE, never the Settings elements directly, so a
    // harness can hand over plain objects. Without the seam a test run reads whatever a previous
    // run left in your real reader settings (non-deterministic) and writes its own scratch values
    // back into them (it changed the night veil under you). A test must not be able to do that.
    property var globalPrefs:   prefs         // needs the six preference properties above
    property var seriesRecords: seriesStore   // needs a writable `all` JSON string
    property var entryRecords:  entryStore    // needs a writable `all` JSON string

    // ---- load ----
    // Globals are applied ONCE, before the first load(), so the first entry opens already dressed.
    function _applyGlobalPrefs() {
        nightVeil = globalPrefs.nightVeil
        gutterStrength = globalPrefs.gutterStrength
        // strip layout + memory saver live in the backend. Strip layout is reader-wide there (it
        // deliberately survives entry crossings), so pushing it once here is enough.
        if (core && core.setStripLayout) core.setStripLayout(globalPrefs.stripWidthPct, globalPrefs.stripGap)
    }
    // The per-series reader preferences, resolved for the CURRENT series and applied. THREE layers,
    // most specific first: this series' own record -> the global last-choice (the layout/order you
    // last picked anywhere) -> this lane's default (manga + tankoban open Manga RTL, western opens
    // Comic LTR). Every legacy record shape is understood by ONE tested door,
    // ComicReaderState.migrateReaderPrefs, and the migration happens IN MEMORY ONLY: opening a book
    // must never rewrite the store. A read is not a write — the old record stands until the reader
    // makes a real change (setLayout/setOrder/setStripLayout/a coupling nudge), which is what
    // _saveSeriesPrefs re-writes in the new shape.
    function _applySeriesPrefs() {
        var rec = ComicReaderState.storeGet(seriesRecords.all, seriesId)

        // F2 — strip measure is PER SERIES, seeded by the global. A weekly gag strip and a
        // double-page-spread tankobon want different column widths, and re-dressing every book to
        // match the last one you touched is the behaviour this replaces. A series that has never
        // been dressed follows the global seed, so a width set anywhere still reaches every book
        // you have not given an opinion about.
        var w = (rec && rec.sw !== undefined) ? rec.sw : globalPrefs.stripWidthPct
        var g = (rec && rec.sg !== undefined) ? rec.sg : globalPrefs.stripGap
        // Pushed WITHOUT persisting: this is applying memory, not forming it. Writing here would
        // stamp this series' width onto the global seed merely because you opened the book, and the
        // next undressed series would inherit it - the exact leak per-series is meant to end.
        if (core && core.setStripLayout) setStripLayout(w, g, false)

        // Layout + order. A record that says NOTHING about either (it may exist only to hold a
        // strip measure or a coupling phase) is not an opinion, so the global last-choice answers
        // instead — and that global rides the SAME migration, so a legacy global ("strip", written
        // before the split) is understood exactly like a legacy record.
        var src = (rec && (rec.layout !== undefined || rec.order !== undefined
                           || rec.rm !== undefined || rec.readingMode !== undefined))
                ? rec
                : { layout: globalPrefs.layout, order: globalPrefs.order, rm: globalPrefs.readingMode }
        var prefsForSeries = ComicReaderState.migrateReaderPrefs(src, entryKind, western)
        layout = prefsForSeries.layout
        order = prefsForSeries.order

        // The Image panel's adjustments, replayed BEFORE core.openEntry() (load()
        // calls this first), so the very first page request is already rendered
        // the way this book was left rather than flashing an unadjusted page.
        // Same three-layer shape as the strip measure: this series' record wins,
        // and where it is silent the reader's current state answers.
        //
        // Read off the RECORD, not off `prefsForSeries`: `src` above deliberately
        // falls back to the global last-choice when the record says nothing about
        // layout or order, and a record that exists only to hold image
        // adjustments would have been thrown away by that fallback.
        //
        // The interior is NOT validated here. migrateReaderPrefs carries
        // `renderProfile` through as an opaque map (Task 3 left it that way for
        // this task) and the BACKEND is the one validator, so a hand-edited or
        // future-version blob is clamped there, once, on the way in.
        var stored = (rec && rec.renderProfile && typeof rec.renderProfile === "object"
                      && !Array.isArray(rec.renderProfile)) ? rec.renderProfile : {}
        var rp = {}
        for (var k in stored) rp[k] = stored[k]
        // Night is the ONE field with a global fallback: it is reading-comfort
        // taste that should follow you into a book you have never adjusted, not a
        // per-book decision that silently switches your veil off on every open.
        if (rp.nightFilter === undefined) rp.nightFilter = (nightVeil !== "off")
        _replayingPrefs = true
        try { _applyRenderProfile(rp) } finally { _replayingPrefs = false }
    }
    // The per-entry blob handed straight to core.openEntry(). memorySaver is a GLOBAL that merely
    // RIDES this blob (the backend round-trips it there and resets it per entry), so it is injected
    // on the way in and stripped on the way out — see _saveEntryBlob.
    function _applyEntryPrefs() {
        var blob = ComicReaderState.storeGet(entryRecords.all, curChapterId) || ({})
        // F3: a chapter you have never opened inherits the SERIES' hand-set pairing phase. Without
        // this, every new chapter re-ran the auto probe from scratch and could land on the opposite
        // phase from the one you just fixed by hand — so a series read in order made you re-nudge
        // at nearly every chapter. Only ever a SEED: an entry that already carries its own coupling
        // record keeps it, because that is a decision made about this specific book.
        if (blob.couplingMode === undefined) {
            var srec = ComicReaderState.storeGet(seriesRecords.all, seriesId)
            if (srec && srec.cp) {
                blob.couplingMode = "manual"
                blob.couplingPhase = srec.cp
            }
        }
        blob.memorySaver = globalPrefs.memorySaver
        persistedState = blob
    }
    // ---- save ----
    // MERGES rather than replaces. It used to write `{ rm }` wholesale, which was fine when the
    // record held only a reading mode; now that a series can also carry its own strip measure
    // (sw/sg), a mode change must not silently drop the width you set. `extra` is how a caller adds
    // fields — absent means "record only what the record already knew, plus the current mode", so
    // merely changing the reading mode never invents a width opinion this series did not have (and
    // therefore never stops it following the global seed).
    function _saveSeriesPrefs(extra) {
        if (!seriesId.length) return
        var rec = ComicReaderState.storeGet(seriesRecords.all, seriesId) || ({})
        rec.layout = layout
        rec.order = order
        // THIS is the "next real user change" that retires the combined identity for this series:
        // leaving `rm` behind would keep a stale answer in the record forever, saying "manga" long
        // after the reader had been switched to Long Strip. Migration is on READ; retirement is on
        // the first WRITE, never merely on opening the book.
        delete rec.rm
        // A null VALUE means "delete this key", which is how resetCoupling drops the series-wide
        // pairing seed. Storing an explicit null instead would survive the JSON round-trip and read
        // back as a real (falsy but present) value on the next open.
        if (extra) {
            for (var k in extra) {
                if (extra[k] === null || extra[k] === undefined) delete rec[k]
                else rec[k] = extra[k]
            }
        }
        seriesRecords.all = ComicReaderState.storePut(seriesRecords.all, seriesId, rec)
    }
    function _saveEntryBlob() {
        if (!curChapterId.length || !core || !core.persistedState) return
        var blob = core.persistedState()
        if (blob) delete blob.memorySaver          // a global has no business in a per-book record
        entryRecords.all = ComicReaderState.storePut(entryRecords.all, curChapterId, blob)
    }
    // Spread overrides and the coupling verdict land in bursts (a probe resolving, a P nudge), so
    // the write is debounced exactly like recordProgress — QSettings syncs to disk on every write.
    Timer { id: entrySave; interval: 800; onTriggered: reader._saveEntryBlob() }

    // THE ONE restore door (TB2's 300ms settle). Everything that has to put the strip somewhere goes
    // through here — a resumed Continue spot, a page-only record, a mode switch into the strip — so
    // there is exactly one mechanism to reason about and no second one to fight it. Retries while the
    // column still has not laid out, rather than silently leaving the reader at the top.
    property int _stripRestoreTries: 0
    Timer { id: stripRestore; interval: 300; onTriggered: reader._runStripRestore() }

    // ARM the door. The ONLY way to open it — because "arm" has to mean a FULL retry budget, not
    // whatever the previous restore happened to leave behind. Both arm sites (load(), setReadingMode())
    // come through here, so a crossing or mode switch that lands mid-retry cannot inherit a spent
    // budget and give up early on a column that is merely slow to lay out.
    function _armStripRestore() {
        _stripRestoreTries = 0
        stripRestore.restart()
    }

    // The door's body, as a named function so it can be driven deterministically by the harness
    // (a 300ms sleep in a test is a flake waiting to happen).
    function _runStripRestore() {
        if (mode !== "long_strip" || max <= 0) {
            // We are not on the strip any more: THIS opening is over. Disarm as well as close — a
            // fraction left lying here is a landmine for the NEXT, unrelated opening (a mode switch
            // back into Strip), which would take the fraction arm and jump to the entry's original
            // resume spot instead of the page the switch is contractually bound to keep.
            _stripRestoreTries = 0
            _pendingStripFrac = 0
            return
        }
        var span = stripSurface.contentHeight - stripSurface.height
        if (span <= 0) {   // not laid out yet — retry; a slow decode costs a moment, never your place
            if (_stripRestoreTries < 3) { _stripRestoreTries += 1; stripRestore.restart() }
            else _stripRestoreTries = 0
            return
        }
        _stripRestoreTries = 0
        if (_pendingStripFrac > 0) {
            stripSurface.haltScrollAt(Math.max(0, Math.min(span, _pendingStripFrac * span)))
            _pendingStripFrac = 0
        } else if (currentPage > 1) {
            stripSurface.seekToPage(currentPage - 1)   // backend-exact page top
        }
    }

    // ---- reactions: every settings write goes straight back to its store ----
    // The veil is GLOBAL taste and it follows you between books — but only when a
    // reader actually changes it. `_replayingPrefs` is what keeps merely OPENING a
    // series (which replays that series' remembered night state) from stamping it
    // onto the global seed; the same leak F2 closed for the strip measure.
    onNightVeilChanged:      if (_ready && !_replayingPrefs) globalPrefs.nightVeil = nightVeil
    onGutterStrengthChanged: if (_ready) globalPrefs.gutterStrength = gutterStrength
    // NOTE: stripWidthPct/stripGap are deliberately ABSENT here. They are readonly readbacks of the
    // backend, so they also change when _applySeriesPrefs replays a series' remembered measure —
    // persisting from that signal wrote the opened book's width onto the GLOBAL seed, which is how
    // one series' taste used to re-dress every other. Their persistence lives in setStripLayout(),
    // the one door a user actually turns. (F2, 2026-07-26.)
    onMemorySaverChanged:    if (_ready) globalPrefs.memorySaver = memorySaver
    onSeriesIdChanged:       if (_ready) _applySeriesPrefs()

    Connections {
        target: reader.core
        ignoreUnknownSignals: true
        // a spread override, a nudge or a resolved probe changed the book's pairing record
        function onPairingChanged() { if (reader._ready) entrySave.restart() }
        // a bookmark toggle: refresh the live list the HUD's ticks bind to, then persist it
        function onBookmarksChanged() { reader._refreshBookmarks(); if (reader._ready) entrySave.restart() }
        // a fresh open/crossing: the new entry's bookmarks replace the old entry's live list
        function onEntryChanged() { reader._refreshBookmarks() }
    }

    // ================= reading surfaces (Task 10; Single Page added Task 4) =================
    // The THREE layouts mount here, each handed the shell's `core` seam. They PAINT; the shell still
    // owns every DECISION (page, direction, completion).
    //
    // MOUNTED ON `layout`, NOT `mode` (Task 4 decision). `layout` is the persisted truth Task 3 made
    // authoritative; `mode` is a derived compatibility alias that happens to pass "single_page"
    // through unchanged, so keying the new surface on it would work by accident rather than by
    // contract. The two pre-existing mounts moved with it — `mode === "long_strip"` and
    // `layout === "long_strip"` are the same predicate by construction, so the switch is
    // behaviour-identical and leaves one rule for all three: the layout you persisted is the surface
    // you get. `mode` stays for the input/HUD/nav code that still speaks it.
    focus: true

    // WHICH surface answers for the current layout. ONE place owns the layout -> surface mapping and
    // all three mounts bind their `visible` to it, so two surfaces can never paint at once and there
    // is no triplicated predicate to fall out of step. An unrecognised layout falls back to the strip
    // rather than mounting nothing — setLayout() already refuses unknown values and the migration
    // degrades a corrupt record to the lane default, so this is a floor, not a route, and a floor that
    // shows a reader beats one that shows black.
    //
    // It is also the only READABLE form of the mount contract: an offscreen harness roots its tree
    // invisible, so every child's `visible` reads false there whatever the mount says, and a test
    // asserting on that would prove nothing.
    readonly property string activeSurface: layout === "single_page" ? "singleSurface"
                                          : layout === "paired_pages" ? "doubleSurface"
                                          : "stripSurface"

    // Long Strip (manga default). It drives currentPage/stripFraction as the user scrolls — but
    // ONLY on a genuine scroll gesture, so mounting it never clobbers a resumed page/fraction.
    ComicReaderStripSurface {
        id: stripSurface
        objectName: "stripSurface"
        anchors.fill: parent
        visible: reader.activeSurface === "stripSurface"
        active: visible
        core: reader.core
        rtl: reader.rtl
        // NO resume binding in: the surface is a painter, and a bound fraction it applies itself is a
        // feedback loop (its own onScrolled writes reader.stripFraction, which re-drives the binding).
        // Restoring is a one-shot COMMAND from the shell (stripRestore -> haltScrollAt/seekToPage).
        onPageInView: function (page) { reader.currentPage = page }
        onScrolled: function (frac) { reader.stripFraction = frac }
        // Pin what the reader is LOOKING at. Without this the strip pins nothing and the LRU can
        // evict the on-screen page mid-read (TB2 pins its whole zone every refresh). This also
        // promotes visible pages to the top decode priority, which the strip window alone doesn't.
        onVisiblePages: function (indices) { if (reader.core && reader.core.setVisible) reader.core.setVisible(indices) }
    }

    // Double Page (direction-aware). It renders the canonical unit and drives the maxSeen
    // pair-anchor contract: unitShown carries the unit's reading-HIGHEST page (see onCurrentPageChanged
    // above) so a pair-terminated entry can reach `finished` from the anchor.
    ComicReaderDoubleSurface {
        id: doubleSurface
        objectName: "doubleSurface"
        anchors.fill: parent
        visible: reader.activeSurface === "doubleSurface"
        active: visible
        core: reader.core
        currentPage: reader.currentPage
        rtl: reader.rtl
        gutterStrength: reader.gutterStrength      // settings sheet -> live spine shadow
        onUnitShown: function (highestPage) { if (highestPage > reader.maxSeen) reader.maxSeen = highestPage }
    }

    // Single Page (Task 4). One page, alone, on the black stage — a LAYOUT, orthogonal to order: a
    // manga in Single Page is still right-to-left, and the shell (not the surface) is what knows
    // which page comes next. No unitShown here and none needed: a single page's highest page IS
    // currentPage, which onCurrentPageChanged already folds into maxSeen.
    ComicReaderSingleSurface {
        id: singleSurface
        objectName: "singleSurface"
        anchors.fill: parent
        visible: reader.activeSurface === "singleSurface"
        active: visible
        core: reader.core
        currentPage: reader.currentPage
    }

    // The PAGED surface currently mounted (Single or Pair), or null in Long Strip. The zoom/pan verbs
    // below used to name the double surface directly; with two paged layouts that would have zoomed
    // an unmounted surface whenever Single Page was showing.
    //
    // DUCK-TYPED, and nothing enforces it: whatever this returns must carry setZoom(pct), clampedZoom,
    // panBy(dx, dy) and panYMax. Both paged surfaces do; a rename in either one fails here as a runtime
    // TypeError with no compile-time warning, so that list is the contract. (Long Strip is deliberately
    // absent: it owns scrolling, not zoom/pan, and the callers below already guard against null.)
    readonly property var _pagedSurface: activeSurface === "singleSurface" ? singleSurface
                                       : activeSurface === "doubleSurface" ? doubleSurface : null

    // Reflect the mounted paged surface's zoom onto the shell for the HUD/settings (Task 11); the
    // surfaces own zoom/pan authoritatively (they reset PAN per unit/page; zoom persists).
    //
    // ONE Binding, through _pagedSurface. An earlier draft used two with mutually exclusive `when`
    // clauses, which raised a fair question: under Qt 6's default RestoreBindingOrValue, a layout switch
    // deactivates one and activates the other in a single dependency pass, so does the deactivating
    // one's restore clobber the activating one's value? Measured against the real shape (both `when`
    // clauses driven off one predicate, flipped in one assignment): it does not — 180 -> 240 -> 180 in
    // both directions, correct every time. But binding evaluation order is not a documented guarantee,
    // and one Binding cannot have an ordering question at all, so this is the version that ships.
    Binding {
        target: reader
        property: "zoomPercent"
        value: reader._pagedSurface ? reader._pagedSurface.zoomPercent : 100
        when: reader._pagedSurface !== null
    }

    // ---- night veil (Task 12): a black page-dim over the reading surfaces, BELOW the chrome so
    // controls stay full-brightness + readable. A plain Rectangle intercepts no input (no MouseArea),
    // so it never blocks page taps/scroll. Opacity comes from the ONE tested mapping. ----
    Rectangle {
        objectName: "nightVeil"
        anchors.fill: parent
        color: "black"
        opacity: ComicReaderState.nightVeilOpacity(reader.nightVeil)
        visible: opacity > 0.001
    }

    // ================= chrome (Task 11): semantic input + Family Gradient HUD =================
    // Mount ORDER = z-order: surfaces (below) -> input (fills the reader, below the HUD) -> HUD
    // (over everything). The shell owns keyboard focus, so it forwards keys into the input's pure map.
    Keys.onPressed: function (event) {
        if (comicInput.keyAction(event.key, event.modifiers) !== "") event.accepted = true
    }

    ComicReaderInput {
        id: comicInput
        anchors.fill: parent
        // reading-state mirrors bound from the shell
        // NOT `reader.mode`: ComicReaderInput sorts input into PAGED versus STRIP, and its token for
        // the paged case is still "double_page" because it predates Single Page. Single Page IS a
        // paged layout — same page turns, same click zones, same zoom/pan, no scrolling column — so it
        // takes the paged map rather than falling through to the strip branch, where Space and the
        // side-click zones would drive a hidden ListView and do nothing at all. Renaming the token
        // itself belongs to the Task 8 input pass, not here.
        mode: reader.layout === "long_strip" ? "long_strip" : "double_page"
        rtl: reader.rtl
        zoomPercent: reader.zoomPercent
        modalOpen: reader.modalOpen
        chromeVisible: reader.chromeVisible
        // paged vertical pan headroom, so Up/Down pan a too-tall spread or a zoomed page (never flip).
        vScrollMax: reader._pagedSurface ? reader._pagedSurface.panYMax : 0
        // within-entry navigation + surface control
        onNext: reader.pageNext()
        onPrevious: reader.pagePrev()
        onScrollBy: function (screens) { reader._stripScroll(screens) }
        onZoomBy: function (delta) {
            var surf = reader._pagedSurface
            if (surf) {
                surf.setZoom(surf.clampedZoom + delta)
                hud.showToast("Zoom " + surf.clampedZoom + "%")
            }
        }
        onPanBy: function (dx, dy) { if (reader._pagedSurface) reader._pagedSurface.panBy(dx, dy) }
        // chrome + window verbs
        onToggleChrome: reader.chromeVisible = !reader.chromeVisible
        onToggleFullscreen: reader.fullscreenRequested()
        onBack: reader.backRequested()
        // mode cycle (Manga/Comic/Strip) + coupling nudge -> persisted seams. Direction is baked
        // into the mode identity now, so there is no separate direction toggle.
        onCycleMode: reader.cycleMode()
        onNudgeCoupling: {
            reader.nudgeCoupling()
            var phase = (core && core.couplingState) ? String(core.couplingState).split(":")[1] : ""
            // Sentence case is OUR house voice (see the settings sheet: "Night veil", "Page width",
            // "Memory saver"), deliberately not TB2's Title Case "Shifted Pairing". Reader 1 has no
            // toast here at all — this feedback is adopted from TB2, not restored parity, because a
            // nudge visibly reshuffles the spread while saying nothing about which phase you landed in.
            hud.showToast(phase === "shifted" ? "Shifted pairing" : "Normal pairing")
        }
        // first/last + crossing
        onFirstPage: reader.firstPageNav()
        onLastPage: reader.lastPageNav()
        onPrevEntry: reader.goPrev(false)
        onNextEntry: reader.goNext()
        // overlay intents (Task 12 wires the overlays)
        onOpenSettings: reader.settingsRequested()
        onOpenNavigator: reader.navigatorRequested()
        onOpenThumbnails: reader.thumbnailsRequested()
        onToggleBookmark: reader.bookmarkToggleRequested()
        onGoToPage: reader.goToPageRequested()
        onOpenShortcuts: reader.shortcutsRequested()
        onToggleLoupe: reader.loupeRequested()
        onCloseTop: reader.closeTopRequested()
        onOpenContextMenu: function (x, y) { reader._onContextMenu(x, y) }
        // reveal-zone hover keeps the HUD alive; both also count as cursor activity — chrome and
        // cursor wake TOGETHER, which is the whole point of the one wake door.
        onRevealRequested: reader.restoreCursorAndChrome()
        onActivity: { hud.notifyActivity(); reader._pokeCursor() }
    }

    ComicReaderHud {
        id: hud
        anchors.fill: parent
        reader: reader
        bookmarkPages: reader.liveBookmarks
        // gold rail scrub -> shell navigation
        onSeekRequested: function (page) { reader.goToPageIndex(page) }
        onScrubFractionRequested: function (frac) { reader.scrubToFraction(frac) }
        // the rail's end arrows CROSS entries (bound to hasPrev/hasNext, per the crossing note above)
        onPrevRequested: reader.goPrev(false)
        onNextRequested: reader.goNext()
        // edge side bars turn a PAGE/unit within the entry; direction resolved in the HUD
        onAdvancePageRequested: reader.pageNext()
        onRetreatPageRequested: reader.pagePrev()
        // ---- the six approved commands. Four raise a temporary surface through the ONE
        //      coordinator; two act directly. ----
        onOpenPages:  reader.openOverlay("pages")
        onOpenLoupe:  reader.openOverlay("loupe")
        onOpenImage:  reader.openOverlay("image")
        onOpenLayout: reader.openOverlay("layout")
        onToggleOrder: reader.setOrder(reader.order === "rtl" ? "ltr" : "rtl")
        onToggleBookmark: reader.bookmarkToggleRequested()
        // window verbs -> the shell's existing session-window signals
        onBackRequested: reader.backRequested()
        onMinimizeRequested: reader.minimizeRequested()
        onFullscreenRequested: reader.fullscreenRequested()
        onCloseRequested: reader.closeRequested()
    }

    // ---- cursor auto-hide overlay (FIX 1): topmost and click-transparent — it ONLY sets the
    // cursor shape, it must never eat input. acceptedButtons:Qt.NoButton means a press is never
    // grabbed here, so it falls straight through to the surfaces/input beneath (the codebase's own
    // click-swallower pattern, ComicReaderHud.qml's footer MouseArea, is the deliberate OPPOSITE:
    // real buttons + an empty onClicked). Wheel is untouched too: ComicReaderInput's own plain
    // MouseArea already sits above the strip surface with no onWheel handler and the smooth-wheel
    // strip scroll (F1) still works through it today — proof a bare MouseArea here cannot swallow
    // the wheel either.
    //
    // Keep this item enabled across the hidden→visible chrome transition. On
    // Windows, disabling the item while it owns BlankCursor can leave that
    // cursor installed even though the HUD has returned. An explicit
    // BlankCursor→ArrowCursor property transition restores the system cursor
    // immediately. Modals still disable the overlay so their own cursors win.
    MouseArea {
        objectName: "cursorHideArea"
        anchors.fill: parent
        z: 998
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        enabled: !reader.modalOpen
        cursorShape: reader.chromeVisible || !reader._cursorIdle
                     ? Qt.ArrowCursor : Qt.BlankCursor
        // "Any plain mouse movement restores HUD and cursor together." One door, so they can never
        // come back separately again.
        onPositionChanged: reader.restoreCursorAndChrome()
    }

    // ---- overlays (Task 12) — mounted ABOVE the HUD so they own input while open ----

    // The temporary Pages filmstrip (Task 6). Raised by the Pages command and by T, through the ONE
    // overlay coordinator; it draws OVER the comic and the comic never shifts to make room for it.
    //
    // It reads facts and raises intents — it cannot navigate by itself, which is what makes "Escape
    // or clicking the comic dismisses without moving" structural rather than a promise. `bookmarks`
    // is the SAME liveBookmarks list the HUD's rail ticks bind to, so a toggle moves the filmstrip's
    // marks and the rail's ticks together instead of leaving two lists to drift.
    ComicReaderPagesOverlay {
        id: pagesOverlay
        objectName: "pagesOverlay"
        core: reader.core
        pageCount: reader.max
        currentPage: reader.currentPage
        order: reader.order
        bookmarks: reader.liveBookmarks
        open: reader.activeOverlay === "pages"
        // Selecting a thumbnail is ONE move: go there, and give the screen back. goToPageIndex is
        // the shell's existing navigation door (it snaps to the canonical unit in Paired Pages), so
        // the filmstrip lands on exactly what a scrub or a Ctrl+G would.
        onJumpRequested: function (page) {
            reader.goToPageIndex(page)
            reader.activeOverlay = ""
        }
        // Dismissal is a plain assignment, never openOverlay() — openOverlay TOGGLES, so routing a
        // dismissal through it would re-open the surface the reader just closed.
        onDismissRequested: reader.activeOverlay = ""
    }

    // The compact Image panel (Task 7). Raised by the Image command through the ONE overlay
    // coordinator; it hangs from the command bar and the comic never shifts to make room for it.
    //
    // It reads the live profile and raises ONE intent carrying a COMPLETE map — the backend's
    // setRenderProfile REPLACES rather than merges, so a partial map would silently reset the
    // fields it omitted. The shell owns the throttle and the persistence; the panel owns neither.
    ComicReaderImagePopover {
        id: imagePopover
        objectName: "imagePopover"
        profile: reader.renderProfile
        open: reader.activeOverlay === "image"
        onProfileChangeRequested: function (profile) { reader.setRenderProfile(profile) }
        // Dismissal is a plain assignment, never openOverlay() — openOverlay TOGGLES, so routing a
        // dismissal through it would re-open the surface the reader just closed.
        onDismissRequested: reader.activeOverlay = ""
    }

    ComicReaderSettingsSheet {
        id: settingsSheet
        reader: reader   // sizes itself to the shell (explicit width/height binding, see the component)
    }
    // The approved chrome has NO permanent settings drawer, so the toolbar no longer carries a
    // Settings command. The sheet itself survives until Task 12 retires it, reachable by the S key
    // and by right-click outside a pairable spread — the controls it still owns move into the
    // focused Image and Layout popovers in Tasks 7 and 8.
    Connections {
        target: reader
        function onSettingsRequested()  { settingsSheet.open() }
        // Escape (from the input's one door) resolves through the shell's layered coordinator.
        function onCloseTopRequested()  { reader.closeTop() }
        // The keyboard and the toolbar must agree about what they open: T and the Pages command are
        // the same door, and so are L and Loupe.
        function onThumbnailsRequested() { reader.openOverlay("pages") }
        function onLoupeRequested()      { reader.openOverlay("loupe") }
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
