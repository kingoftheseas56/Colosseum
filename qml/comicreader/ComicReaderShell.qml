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
    property string mode: "long_strip"          // "long_strip" | "double_page" (internal layout)
    property bool   rtl: false                  // reading direction (paints RTL when true; internal)
    // the ONE user-facing identity (Hemanth 2026-07-25): Manga / Comic / Strip, derived from the
    // internal layout+direction. The HUD + settings show THIS and write it via setReadingMode() —
    // there is no separate RTL/LTR toggle. manga=RTL double, comic=LTR double, strip=vertical scroll.
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
    // an overlay is up (Task 12) — swallows background input + pauses auto-hide. Aggregated off the
    // mounted overlays; more join this OR as later Task 12 slices land (navigator, thumbnails, ...).
    readonly property bool modalOpen: settingsSheet.opened

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

            // reading mode: per-series persisted override, else the lane default (manga->Manga
            // RTL double-page (MangaPlus), western->Comic LTR double-page). Layout + direction are
            // both derived from the single readingMode identity.
            var rm0 = ComicReaderState.defaultReadingMode(entryKind, western)
            mode = persistedMode.length ? persistedMode : ComicReaderState.readingModeLayout(rm0)
            var dir = persistedDirection.length ? persistedDirection
                    : (ComicReaderState.readingModeRtl(rm0) ? "rtl" : "ltr")
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
    // Continue entry (mirrors MangaReader load() resume, lines 280-290). The shell just publishes
    // stripFraction; the strip surface owns the layout settle — it applies the resume fraction once
    // its content has laid out (ComicReaderStripSurface._applyResumeFraction).
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
    function pageNext() {
        if (mode === "double_page") {
            var t = _unitBoundsForIndex(currentPage - 1)[1] + 1
            if (t < max) currentPage = _unitBoundsForIndex(t)[0] + 1
        } else _stripScroll(0.9)
    }
    function pagePrev() {
        if (mode === "double_page") {
            var t = _unitBoundsForIndex(currentPage - 1)[0] - 1
            if (t >= 0) currentPage = _unitBoundsForIndex(t)[0] + 1
        } else _stripScroll(-0.9)
    }
    function goToPageIndex(p1) {
        var p = Math.max(1, Math.min(Math.max(1, max), Math.round(p1)))
        if (mode === "double_page") p = _unitBoundsForIndex(p - 1)[0] + 1
        currentPage = p
    }
    function _stripScroll(screens) {
        var span = stripSurface.contentHeight - stripSurface.height
        if (span <= 0) return
        stripSurface.contentY = Math.max(0, Math.min(span, stripSurface.contentY + screens * stripSurface.height))
    }
    function scrubToFraction(frac) {
        var f = Math.max(0, Math.min(1, frac))
        stripFraction = f
        var span = stripSurface.contentHeight - stripSurface.height
        if (span > 0) stripSurface.contentY = f * span
    }
    function firstPageNav() { currentPage = 1; if (mode === "long_strip") stripSurface.contentY = 0 }
    function lastPageNav() {
        goToPageIndex(max)
        if (mode === "long_strip")
            stripSurface.contentY = Math.max(0, stripSurface.contentHeight - stripSurface.height)
    }
    // reading-mode changes write the PERSISTED seams (never mode/rtl directly) so a crossing's
    // load() honors the choice; the reactions below flip the visible mode/rtl live. setReadingMode
    // translates the single Manga/Comic/Strip identity into the internal layout + direction seams.
    function setReadingMode(rm) {
        persistedMode = ComicReaderState.readingModeLayout(rm)
        persistedDirection = ComicReaderState.readingModeRtl(rm) ? "rtl" : "ltr"
    }
    // M cycles the three identities Manga -> Comic -> Strip -> Manga.
    function cycleMode() {
        var order = ["manga", "comic", "strip"]
        var i = order.indexOf(readingMode)
        setReadingMode(order[(i < 0 ? 0 : (i + 1) % order.length)])
    }
    function nudgeCoupling() { if (core && core.nudgeCoupling) core.nudgeCoupling() }
    // Hand the double-page phase back to the auto-coupling probe (the settings sheet's Auto chip).
    function resetCoupling() { if (core && core.resetCoupling) core.resetCoupling() }
    // Long-strip taste: portrait page width % + inter-page gap px. The core owns the strip geometry,
    // so these read straight off it; ONE setter carries both so changing either preserves the other.
    function setStripLayout(widthPct, gap) { if (core && core.setStripLayout) core.setStripLayout(widthPct, gap) }

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
    // Task 11: a HUD/input toggle writes the PERSISTED seam; flip the visible mode/rtl live. Guarded
    // by _ready so construction-time defaults (persisted "" during createObject) never fire this —
    // load() owns the initial mode/direction. persistedMode/Direction stay the single source load()
    // reads on every crossing, so the toggle survives a chapter/volume jump.
    onPersistedModeChanged: if (_ready && persistedMode.length && mode !== persistedMode) mode = persistedMode
    onPersistedDirectionChanged: {
        if (_ready && persistedDirection.length) {
            var want = (persistedDirection === "rtl")
            if (rtl !== want) rtl = want
        }
    }
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

    // ================= reading surfaces (Task 10) =================
    // The two direction/geometry surfaces mount here, toggled by `mode`, each handed the shell's
    // `core` seam. They PAINT; the shell still owns every DECISION (page, direction, completion).
    focus: true

    // Long Strip (manga default). It drives currentPage/stripFraction as the user scrolls — but
    // ONLY on a genuine scroll gesture, so mounting it never clobbers a resumed page/fraction.
    ComicReaderStripSurface {
        id: stripSurface
        anchors.fill: parent
        visible: reader.mode === "long_strip"
        active: visible
        core: reader.core
        rtl: reader.rtl
        resumeFraction: reader.stripFraction
        onPageInView: function (page) { reader.currentPage = page }
        onScrolled: function (frac) { reader.stripFraction = frac }
    }

    // Double Page (direction-aware). It renders the canonical unit and drives the maxSeen
    // pair-anchor contract: unitShown carries the unit's reading-HIGHEST page (see onCurrentPageChanged
    // above) so a pair-terminated entry can reach `finished` from the anchor.
    ComicReaderDoubleSurface {
        id: doubleSurface
        objectName: "doubleSurface"
        anchors.fill: parent
        visible: reader.mode === "double_page"
        active: visible
        core: reader.core
        currentPage: reader.currentPage
        rtl: reader.rtl
        gutterStrength: reader.gutterStrength      // settings sheet -> live spine shadow
        onUnitShown: function (highestPage) { if (highestPage > reader.maxSeen) reader.maxSeen = highestPage }
    }
    // reflect the active double surface's zoom onto the shell for the HUD/settings (Task 11); the
    // double surface owns zoom/pan authoritatively (it resets them per unit).
    Binding { target: reader; property: "zoomPercent"; value: doubleSurface.zoomPercent; when: doubleSurface.active }

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
        mode: reader.mode
        rtl: reader.rtl
        zoomPercent: reader.zoomPercent
        modalOpen: reader.modalOpen
        chromeVisible: reader.chromeVisible
        // double-page vertical pan headroom, so Up/Down pan a too-tall spread (never flip).
        vScrollMax: doubleSurface.panYMax
        // within-entry navigation + surface control
        onNext: reader.pageNext()
        onPrevious: reader.pagePrev()
        onScrollBy: function (screens) { reader._stripScroll(screens) }
        onZoomBy: function (delta) { if (doubleSurface) doubleSurface.setZoom(doubleSurface.clampedZoom + delta) }
        onPanBy: function (dx, dy) { if (doubleSurface) doubleSurface.panBy(dx, dy) }
        // chrome + window verbs
        onToggleChrome: reader.chromeVisible = !reader.chromeVisible
        onToggleFullscreen: reader.fullscreenRequested()
        onBack: reader.backRequested()
        // mode cycle (Manga/Comic/Strip) + coupling nudge -> persisted seams. Direction is baked
        // into the mode identity now, so there is no separate direction toggle.
        onCycleMode: reader.cycleMode()
        onNudgeCoupling: reader.nudgeCoupling()
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
        onOpenContextMenu: reader.settingsRequested()
        // reveal-zone hover keeps the HUD alive
        onRevealRequested: hud.reveal()
        onActivity: hud.notifyActivity()
    }

    ComicReaderHud {
        id: hud
        anchors.fill: parent
        reader: reader
        bookmarkPages: (reader.persistedState && reader.persistedState.bookmarks) ? reader.persistedState.bookmarks : []
        // scrub -> shell navigation
        onSeekRequested: function (page) { reader.goToPageIndex(page) }
        onScrubFractionRequested: function (frac) { reader.scrubToFraction(frac) }
        // prev/next PILLS cross entries (bound to hasPrev/hasNext, per the crossing note above)
        onPrevRequested: reader.goPrev(false)
        onNextRequested: reader.goNext()
        // edge side bars turn a PAGE/unit within the entry (double-page); direction resolved in the HUD
        onAdvancePageRequested: reader.pageNext()
        onRetreatPageRequested: reader.pagePrev()
        // overlay intents
        onOpenNavigator: reader.navigatorRequested()
        onOpenThumbnails: reader.thumbnailsRequested()
        onOpenSettings: reader.settingsRequested()
        onToggleBookmark: reader.bookmarkToggleRequested()
        // window verbs -> the shell's existing session-window signals
        onBackRequested: reader.backRequested()
        onMinimizeRequested: reader.minimizeRequested()
        onFullscreenRequested: reader.fullscreenRequested()
        onCloseRequested: reader.closeRequested()
    }

    // ---- overlays (Task 12) — mounted ABOVE the HUD so they own input while open ----
    ComicReaderSettingsSheet {
        id: settingsSheet
        reader: reader   // sizes itself to the shell (explicit width/height binding, see the component)
    }
    // the settings pill / right-click / S key -> open the sheet; Escape (closeTop) -> close it
    Connections {
        target: reader
        function onSettingsRequested()  { settingsSheet.open() }
        function onCloseTopRequested()  { settingsSheet.close() }
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
