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
// Slice D7 — Lane C (Tankoban/manga/comics) activity hook, CPP-PORT-CONTRACT.md §7/§9/§10.
// ActivityLaneHelpers.js is the SAME generic begin/no-op/end session-transition module Player 1
// (qml/PlayerPage.qml) and the audiobook lane (qml/AudiobookSession.qml) already share — a
// third lane reusing it rather than re-deriving a third copy. ComicActivityHelpers.js holds
// ONLY this lane's own pure logic (identity/kind derivation, cover sanitizing, page-key shape).
import "../ActivityLaneHelpers.js" as ActivityLaneHelpers
import "../ComicActivityHelpers.js" as ComicActivityHelpers

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
    // ---- WHERE THE READER ACTUALLY IS (Task 11) ----
    // `currentPage` is where the reader has been SENT: a key press, a scrub, a go-to, a resume. This
    // pair is where the reader has been SHOWN — the page whose content (or whose honest error card)
    // reached the screen, and how far down that page the viewport centre sat.
    //
    // They are two different facts and the reader used to persist the wrong one. Flicking forward
    // wrote a Continue record on every page number the request passed through, so closing the book
    // mid-flick came back to a page that had never been on screen. The record now reads THESE, and
    // nothing else writes them: only a surface's presented() signal does, through _onPresented.
    //
    // The fraction is meaningful in Long Strip alone (a page IS the viewport's whole travel in the
    // two paged layouts, so there is no "part way down it" there); progressPayload zeroes it outside
    // long_strip for the same reason it zeroes scrollFrac.
    property int    presentedPage: 0            // 1-based; 0 = nothing shown yet in this entry
    property real   presentedPageFraction: 0    // 0..1 down THAT page (Long Strip only)

    // ================= journey observability (visibility phase 2, slice J1-Manga-Seam) =================
    // Read-only, bound directly to the state above — an isolated journey's ONLY way to know the reader
    // is genuinely ready, instead of inferring it from navigation or a wait. Nothing here is written by
    // anything but the reader's own real behavior; there is no timer, no route shadow, no test-only
    // object standing in for any of it.
    readonly property string readerSourceId: curChapterId
    readonly property int    readerPageCount: pageCount
    readonly property int    readerPageIndex: Math.max(0, currentPage - 1)   // 0-based, mirrors the
        // established pageIndex convention (ComicReaderSingleSurface.qml:118 readerPageIndex ==
        // currentPage - 1) rather than inventing a second index vocabulary.

    // "the current page is render-ready" — the one term that needed real investigation. All three
    // surfaces raise presented(page, frac) ONLY once they have genuinely put something on screen for a
    // position (Single/Double gate it on the mounted Image(s)' own `status === Image.Ready`, or an
    // explicit terminal error card — ComicReaderSingleSurface.qml:242-244, ComicReaderDoubleSurface.qml
    // :211-236; Strip reports its settled scroll position — ComicReaderStripSurface.qml:621-643), and
    // _onPresented (below) is the single place that ever receives that report — never load().
    //
    // presentedPage alone is NOT enough: load() also seeds `presentedPage = currentPage` synchronously
    // as the entry's opening progress-anchor (see load(), "THE ENTRY'S OPENING ANCHOR") so the eager
    // entry-open record has the right page BEFORE any surface has painted anything — so presentedPage
    // reads non-zero at the instant a book opens, not at the instant it is actually shown. Comparing it
    // to currentPage alone would make readerReady true one tick after load(), which is not render
    // readiness. _pageRenderConfirmed exists ONLY to close that gap: load() clears it for every new
    // entry, and only _onPresented's real, surface-driven call ever sets it — the seed assignment in
    // load() never does.
    property bool _pageRenderConfirmed: false
    readonly property bool readerReady: readerSourceId.length > 0
                                         && readerPageCount > 0
                                         && _pageRenderConfirmed
                                         && presentedPage === currentPage
                                         && presentedPage > 0

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

    // ================= AUTO-SCROLL (Task 8) =================
    // The approved rule, verbatim: "Layout and motion remain separate. Long Strip creates the
    // vertical page flow; Auto-scroll only supplies motion at the already chosen width. Starting or
    // resuming Auto-scroll must never resize the page. Manual wheel/touch/navigation, opening
    // chrome, Pages, Image, Loupe, or another temporary surface pauses it immediately. Resume is
    // explicit."
    //
    // The shell owns WHETHER it runs; the strip surface owns the pixels per frame. One owner for
    // the flag, so nothing can disagree about whether the page is moving.
    //
    // SESSION-ONLY, and it is the one dial here that is deliberately NOT persisted: nobody opens a
    // book to a moving page. It is absent from globalPrefs, absent from _saveSeriesPrefs, and
    // load() clears it — three places that would each have to be wrong before a book could reopen
    // in motion.
    property bool autoScrollRunning: false
    // ...and the SPEED is persisted, per series, seeded by the global — the same three-layer shape
    // as the strip measure, because it is taste about how you read a series, not about one chapter.
    // 0.25..3.0, default 1.0 (the range ComicReaderState.migrateReaderPrefs already clamps to).
    property real autoScrollSpeed: 1.0

    // START. Refuses outside Long Strip and refuses an empty book — there is no column to move, and
    // a flag set true with nothing running would light the menu's Pause chip over a still page.
    function startAutoScroll() {
        if (layout !== "long_strip") return
        if (max <= 0) return
        autoScrollRunning = true
    }
    function pauseAutoScroll() { autoScrollRunning = false }
    function toggleAutoScroll() { if (autoScrollRunning) pauseAutoScroll(); else startAutoScroll() }
    // THE ONE pause door every manual source comes through — the wheel, a key, a scrub, a page
    // turn, a temporary surface, the chrome coming back. Named for what it MEANS rather than what
    // it does, because the meaning is the contract: a hand touched the reader, so the machine
    // stops. Resume is explicit, never automatic; there is no counterpart to this function.
    //
    // It also disarms any in-flight strip restore. Without this, a resume door still ticking
    // (waiting for the column to finish laying out) can fire 600ms after the reader has already
    // taken the wheel and yank the column back to the entry's opening spot — a real, independent
    // bug from the resume-race fix above: a hand on the reader means the machine's own idea of
    // "where to put you" is moot from that moment on.
    function manualActivity() {
        pauseAutoScroll()
        if (_stripRestorePending) {
            stripRestore.stop()
            _stripRestoreTries = 0
            _pendingStripFrac = 0
            _pendingPageFraction = -1
        }
    }

    // Speed. `persist` mirrors setStripLayout's third argument for the same reason: replaying a
    // series' remembered speed is not the reader MAKING a choice, and persisting from the replay
    // would stamp the opened book's speed onto the global seed.
    function setAutoScrollSpeed(value, persist) {
        var s = Number(value)
        if (!isFinite(s)) return
        s = Math.max(0.25, Math.min(3.0, s))
        autoScrollSpeed = s
        if (persist === false || !_ready) return
        globalPrefs.autoScrollSpeed = s
        _saveSeriesPrefs({ autoScrollSpeed: s })
    }
    // ================= the ONE overlay coordinator (Task 5) =================
    // Hemanth's approved interaction contract: only ONE temporary surface at a time, and the comic
    // never shifts to make room for it. The chrome only RAISES intents — this is the single place
    // that decides what is open, so two surfaces can never both believe they are.
    //
    // The commands went live in Task 5, before their surfaces landed in Tasks 6-9. That was
    // deliberate and it cost Hemanth a real defect on 2026-08-01, so the correction belongs here
    // rather than in a changelog: this property is an INTENT, and for four days it was also what the
    // HUD's auto-hide asked "is a surface up?". A command whose surface did not exist yet — the
    // Loupe, between Task 5 and Task 9 — set the intent, mounted nothing, and pinned the HUD and the
    // cursor awake for the rest of the session with nothing on screen to dismiss. The hold now reads
    // `modalOpen` (below), which only a surface that really came up can raise, and the shell gate
    // asserts every name raisable here reaches it. `activeOverlay` still drives the command's GOLD —
    // that is presentation, and presentation may lead the pixels; sleep policy may not.
    property string activeOverlay: ""            // "" | pages | image | layout | loupe
    function openOverlay(name) {
        var n = String(name)
        // re-asking for the surface that is already open CLOSES it — one temporary surface, and the
        // command that raised it is also the way back out.
        var next = (activeOverlay === n) ? "" : n
        // OPENING a temporary surface stops the motion immediately (the approved rule). CLOSING one
        // does not, and the asymmetry is the whole point: Start lives inside the Layout menu, so
        // pressing Layout again to put the menu away must not also stop what you just started.
        // Pause is a real event; putting a panel away is not.
        if (next.length) pauseAutoScroll()
        activeOverlay = next
        restoreCursorAndChrome()
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
    // Task 9 adds the Loupe on the same terms, with one extra consequence worth naming: the Loupe is
    // the only temporary surface that keeps keys of its own, so ComicReaderInput carries a
    // `loupeOpen` door above the modal gate for the two the approved design names (L to close it,
    // +/- to magnify the lens). Everything else stays gated exactly as it is under the others.
    readonly property bool modalOpen: settingsSheet.opened || pagesOverlay.open || imagePopover.open
                                      || layoutPopover.open || loupe.open

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
    // Remote Progress import may switch entries without turning the imported winner
    // into a fresh local opening write. The imported record remains the authority.
    property bool _remoteResumeApplying: false
    // Replaying a series' remembered preferences is not the reader MAKING one. Without
    // this, opening a book would stamp that book's night veil onto the GLOBAL seed and
    // re-write the record it had just read — the same leak F2 closed for the strip measure.
    property bool _replayingPrefs: false
    property real _pendingStripFrac: 0          // one-shot: the saved scrollFrac awaiting layout settle
    // One-shot: the saved WITHIN-PAGE fraction awaiting the same settle. -1 means "no opinion", and
    // that is not the same as 0 — 0 says "the viewport centre sat exactly on this page's top edge",
    // which is half a screen higher than where a plain page seek lands. A record written before this
    // field existed must resume the way it always did, so absence has to be distinguishable.
    property real _pendingPageFraction: -1
    // THE HELD within-page anchor, and the page it was measured on. This is what makes "changing
    // layout preserves the visible reading anchor" true across a Strip -> Pair -> Strip round trip:
    // while a paged layout is mounted, presentedPageFraction is legitimately 0 (a page IS the whole
    // travel there), so the strip's last real fraction has to be held somewhere that a paged
    // presentation does not overwrite. Pairing it with its PAGE is what keeps it honest — a fraction
    // is only meaningful on the page it was measured on, so navigating while away invalidates it.
    // 0 = nothing held. Reset per entry in load().
    property int  _stripAnchorPage: 0
    property real _stripAnchorFraction: 0
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
        // A new entry has shown nothing yet — only a genuine _onPresented() call (never this
        // function) may set this back true. See readerReady's comment above for why this exists.
        _pageRenderConfirmed = false
        // NOBODY OPENS A BOOK TO A MOVING PAGE. autoScrollRunning is session-only, so it is never
        // read back from a record — but a CROSSING lands here too, with the previous volume's
        // motion still live, and inheriting it would drop the reader into the next chapter already
        // scrolling. Resume is explicit, per entry as well as per session.
        autoScrollRunning = false
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
                _pendingPageFraction = -1               // previous book must never restore into this one
                _pendingAtLast = false
                _applyResume()                          // a matching Continue entry overrides the start spot
                maxSeen = Math.max(maxSeen, currentPage)
                // THE ENTRY'S OPENING ANCHOR. Seeding this is not a record of a page nobody saw — it
                // IS the spot this open is putting the reader on, either page 1 or the one their last
                // record already said they were on. Without it the eager entry-open record below would
                // carry the PREVIOUS book's presented page (the anchor is what recordProgress reads
                // now), which is the crossing bug in reverse.
                presentedPage = currentPage
                presentedPageFraction = (mode === "long_strip" && _pendingPageFraction >= 0)
                                        ? _pendingPageFraction : 0
                // A RESUMED within-page anchor is held from the start, so leaving the strip and
                // coming back before ever scrolling still lands where the record said.
                _stripAnchorPage = (mode === "long_strip" && _pendingPageFraction >= 0) ? currentPage : 0
                _stripAnchorFraction = presentedPageFraction
                if (core) core.openEntry(curChapterId, _pages, order, persistedState)
                // Physically move the strip to the restored spot. ONE door (stripRestore), settle-gated:
                // the column positions its delegates a vsync later, and its heights are estimates until
                // decodes land — an immediate jump reads y=0 for unrealized delegates and lands at the top.
                if (mode === "long_strip"
                        && (_pendingPageFraction >= 0 || _pendingStripFrac > 0 || currentPage > 1))
                    _armStripRestore()
            } else {
                currentPage = 1; maxSeen = 0; stripFraction = 0; _pendingStripFrac = 0
                _pendingPageFraction = -1; _pendingAtLast = false
                presentedPage = 0; presentedPageFraction = 0
                _stripAnchorPage = 0; _stripAnchorFraction = 0
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
            // ...and the WITHIN-page anchor (Task 11), which is the one that actually lands you on
            // the same panel area. It wins over scrollFrac in _runStripRestore because it is anchored
            // to the paper rather than to the column: a page-width or gap change since you last read
            // moves every page's share of the column, so the same scrollFrac lands somewhere else,
            // while page + pageFraction still means the same spot on the same page.
            //
            // `undefined` (a record written before this field existed) stays -1 = no opinion, so
            // those records resume exactly the way they always did.
            _pendingPageFraction = (mode === "long_strip" && r.pageFraction !== undefined)
                                   ? Math.max(0, Math.min(1, Number(r.pageFraction) || 0)) : -1
        }
    }

    // Apply a validated remote Tankoban winner through the SAME load/_applyResume door
    // as a normal Continue open. No surface/page geometry is mutated here. If the remote
    // volume is not local yet, leave it pending in the bridge and keep the current book readable.
    function _applySyncedResume(target) {
        if (!target || target.valid !== true) return false
        if (reader.progressKind !== "tankoban") return false
        if (String(target.seriesId || "") !== reader.seriesId) return false
        var id = String(target.chapterId || "")
        if (!id.length || !reader.entryReady(id)) return false

        reader._resumeArmed = true
        reader._pendingAtLast = false
        if (id === reader.curChapterId) {
            reader.load()
        } else {
            reader._remoteResumeApplying = true
            try {
                reader.curChapterId = id
            } finally {
                reader._remoteResumeApplying = false
            }
        }
        syncedResumeBridge.acceptPending(target)
        return true
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
    // ---- EVERY navigation verb pauses Auto-scroll first (Task 8). ----
    // They are listed one by one rather than hooked to `currentPage` or to contentY, and that
    // distinction is load-bearing: Auto-scroll MOVES the reading position by design, so a pause
    // driven off the position changing would stop the motion on its own first tick. What pauses it
    // is a hand — a key, a click, a scrub, a page turn — never a position.
    function pageNext() {
        manualActivity()
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
        manualActivity()
        if (layout === "paired_pages") {
            var t = _unitBoundsForIndex(currentPage - 1)[0] - 1
            if (t >= 0) currentPage = _unitBoundsForIndex(t)[0] + 1
        } else if (layout === "single_page") {
            if (currentPage > 1) currentPage = currentPage - 1
        } else _stripScroll(-0.9)
    }
    function goToPageIndex(p1) {
        manualActivity()
        var p = Math.max(1, Math.min(Math.max(1, max), Math.round(p1)))
        if (mode === "double_page") p = _unitBoundsForIndex(p - 1)[0] + 1
        currentPage = p
    }
    // Keyboard scrolling GLIDES (E2). Space/PageDown feed the same drain the wheel feeds, so they
    // decelerate identically and a press mid-glide adds to the backlog instead of fighting it. The
    // surface clamps the landing itself; the old raw contentY write here is what produced
    // jump-then-slide when a key landed while a wheel glide was still running.
    function _stripScroll(screens) {
        manualActivity()
        var span = stripSurface.contentHeight - stripSurface.height
        if (span <= 0) return
        stripSurface.smoothScrollBy(screens * stripSurface.height)
    }
    // A scrub seek is INSTANT and FINAL — it must land where the thumb was released and carry no
    // leftover glide across the jump, so it takes the halt door rather than the drain.
    function scrubToFraction(frac) {
        manualActivity()
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
    function firstPageNav() { manualActivity(); currentPage = 1; if (mode === "long_strip") stripSurface.haltScrollAt(0) }
    function lastPageNav() {
        goToPageIndex(max)          // ...which pauses Auto-scroll; no second copy of that rule here
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
        // NOTE: no pause call here. Changing the layout DOES stop the motion, but the rule lives on
        // `onLayoutChanged` below — one owner, and it catches every path that moves the layout, not
        // just this setter. A second copy here would be an untested line that could never fail.
        // KEEP YOUR PAGE across the switch. Ported from the reader this replaced
        // (MangaReader.setStyle): changing how pages are laid out is not a reason to lose your
        // place, and every reader in the family gets this right.
        //
        // ...and since Task 11, keep your PLACE ON that page too, when there is one to keep. The
        // approved line is "Changing layout, order, width, or image settings preserves the visible
        // reading anchor" — in Long Strip the visible anchor is a point inside a page, not a page
        // number, so leaving the column and coming back used to lose up to a whole screen of it.
        //
        // The within-page part rides `_stripAnchor*`, which survives the excursion into a paged
        // layout (where presentedPageFraction is legitimately 0 — a page IS the whole travel there).
        // It re-arms ONLY if the reader is still on the page it was measured on: navigating while
        // away is a new decision about where to be, and resurrecting a fraction measured on a page
        // you have since left would land you in the wrong place with confidence.
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
        // Leaving the strip drops any pending within-page arm along with the column it belonged to.
        if (layout !== "long_strip") _pendingPageFraction = -1
        else if (_stripAnchorPage > 0 && _stripAnchorPage === currentPage)
            _pendingPageFraction = _stripAnchorFraction
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
    // NOTE what this deliberately does NOT do: it does not touch autoScrollRunning. Width and
    // motion are separate by the approved rule, in BOTH directions — starting the motion never
    // resizes the page, and resizing the page never stops the motion. Dragging the portrait width
    // while Auto-scroll runs reflows the column underneath it and the motion carries on.
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
        // KEEP THE VISIBLE ANCHOR ACROSS THE REFLOW (Task 11). "Changing layout, order, width, or
        // image settings preserves the visible reading anchor" — and image settings genuinely
        // reflow: rotation and auto-crop change a page's aspect, so in Long Strip every page below
        // it moves. Re-arming the ONE restore door at the held page + fraction re-lands the reader
        // on the same part of the same page once the new geometry has settled.
        //
        // The fraction is the reason this works at all: it is relative to the page's OWN height, so
        // a page that just changed size still means the same spot on the paper. Deliberately routed
        // through the existing settle-gated door rather than seeking here — the new heights only
        // exist once the re-decodes land, and an immediate seek would anchor against stale geometry.
        //
        // Not while REPLAYING a series' remembered adjustments (that runs inside load(), which owns
        // the opening anchor and has already armed whatever restore it needs).
        if (_ready && !_replayingPrefs && mode === "long_strip" && max > 0
                && _stripAnchorPage > 0 && _stripAnchorPage === currentPage) {
            _pendingPageFraction = _stripAnchorFraction
            _armStripRestore()
        }
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
        // An immediate record satisfies whatever the debounce was still holding. Without this, a
        // crossing or a close would write, and then the pending timer would write the SAME thing a
        // moment later — twice the disk sync for one position, and after shutdown() it would be
        // writing against an entry the backend has already closed.
        saveSoon.stop()
        var cov = seriesCover
        if (!cov.length) {
            var prev = progress.get(progressKind, seriesId)
            if (prev && prev.cover) cov = String(prev.cover)
        }
        // THE PRESENTED anchor, not the requested one (Task 11). Clamped, because it arrives from a
        // surface: a crossing can leave a page number from the outgoing book in flight for a beat,
        // and a record pointing past the end of the new book would resume nowhere.
        var page = Math.max(1, Math.min(max, presentedPage > 0 ? presentedPage : currentPage))
        progress.record(ComicReaderState.progressPayload({
            seriesId: seriesId, kind: progressKind, seriesTitle: seriesTitle,
            label: curLabel, cover: cov, page: page, max: max,
            chapterId: curChapterId, style: mode, scrollFrac: stripFraction,
            pageFraction: presentedPageFraction, maxSeen: maxSeen
        }))
    }
    // debounced record — QSettings syncs to disk on every record(); don't do that per page-turn.
    Timer { id: saveSoon; interval: reader.recordDebounceMs; onTriggered: reader.recordProgress() }
    function recordProgressSoon() { saveSoon.restart() }

    // ================= THE ONE progress trigger (Task 11) =================
    // All three reading surfaces raise presented(anchorPage, withinPageFraction) when they have
    // actually put something on screen for a position. This is the only thing that moves the reader's
    // recorded place. Requesting a page, decoding it, or navigating to it is NOT presentation — that
    // was the defect: flicking forward wrote a Continue record on every page number the request swept
    // through, so coming back landed on a page that had never been drawn.
    //
    // WHY IT DEBOUNCES RATHER THAN WRITING ON THE SPOT. progress.record() syncs QSettings to disk.
    // Single and Pair raise this once per page/unit, so an immediate write would be fine there — but
    // Long Strip raises it off the surface's 80ms tracking flush, which means ~12 times a second
    // while scrolling and continuously for the whole of an Auto-scroll run. Writing per presentation
    // would be a disk storm on exactly the layout that presents most.
    //
    // So the write rides the SAME debounce the old page-turn record used, at the same interval:
    // strictly fewer triggers than before (navigation no longer writes at all) and never more, which
    // is the disk-churn guarantee this task owes. The ANCHOR is updated immediately either way, so
    // anything that flushes now — a crossing, hiding the reader, shutdown — already carries the
    // newest position rather than waiting for the timer.
    function _onPresented(page, pageFraction) {
        if (_suspendRecord) return
        if (max <= 0) return
        presentedPage = Math.max(1, Math.min(max, Math.round(page)))
        presentedPageFraction = Math.max(0, Math.min(1, Number(pageFraction) || 0))
        // A REAL surface report landed (never load()'s eager seed) — readerReady may now consider
        // this entry's current page render-ready, gated further by presentedPage === currentPage.
        _pageRenderConfirmed = true
        // Hold the strip's anchor so a layout excursion can bring the reader back to the same panel
        // area, not merely the same page. Only the strip writes it: it is the only layout where a
        // fraction means anything, and a paged surface's honest 0 would wipe a real one.
        if (layout === "long_strip") {
            _stripAnchorPage = presentedPage
            _stripAnchorFraction = presentedPageFraction
        }
        // THE RESUME RACE (bug: every Long Strip reopen landed on page 1). The strip surface's
        // onActiveChanged fires Qt.callLater(_emitPresented) the INSTANT it mounts, reporting
        // whatever page the not-yet-restored contentY=0 column shows -- independent of whether
        // _runStripRestore has physically moved the column to the resumed spot yet. That report
        // used to schedule a debounced write unconditionally: 600ms later it banked page 1 over
        // a correct resume, and because the record IS the input to the next resume, one bad
        // write poisoned every open after it. presentedPage above still updates live (so display
        // logic sees the live anchor), but the write to disk waits for the restore door to be
        // done — a presentation that arrives mid-restore is, by construction, reporting a
        // position the shell has already decided is provisional.
        if (!_stripRestorePending) recordProgressSoon()
    }

    // ================= Your Colosseum activity (Lane C) ====================================
    // CPP-PORT-CONTRACT.md §7 identity, §9 Lane C, §10 fixed-page dedupe. `activity` is guarded
    // exactly like `core`/`progress` above — a missing/unbound ActivityStore can never break
    // reading (§25 fail-closed). Identity/session bookkeeping reuses the SAME generic
    // begin/no-op/end rule (ActivityLaneHelpers keyFor/decideTransition) Player 1 and the
    // audiobook lane already share (slice D5) — a third lane reusing it, not re-deriving a
    // third copy.
    property var activity: (typeof ProfileActivity !== "undefined") ? ProfileActivity : null
    property string activityActiveKey: ""
    property string activitySessionId: ""
    // Guards recordCompletion() to the FIRST beat hasFixedCoverage() turns true for this entry
    // (§9: "when it first becomes true, record media_completed"). Reset only when a genuinely
    // new session BEGINS (_activityBeginIfNeeded's begin branch) — never on a same-entry
    // reload/noop, so re-opening an already-completed volume cannot re-fire it either
    // (hasFixedCoverage already answers true on the very first check there).
    property bool _activityCoverageCompleted: false

    // Computed FRESH on every call, never cached in a bound property — this file's own
    // _currentUnit()/ComicReaderDoubleSurface.qml documents why: a var-property binding's
    // re-evaluation order versus the change handler that triggers it is not guaranteed, and
    // this is read from inside onCurChapterIdChanged, the handler for exactly the property
    // (curChapterId) the identity depends on.
    function _activityIdentity() {
        var idf = ComicActivityHelpers.identityFor(reader.seriesId, reader.curChapterId, reader.progressKind)
        if (!idf) return null
        var portable = ComicActivityHelpers.activityItemIdentity(reader.seriesId, reader.curChapterId)
        if (!portable.itemKey.length) return null
        idf.itemKey = portable.itemKey
        idf.syncable = portable.syncable
        return idf
    }

    // Start a new reading session on a fresh open or a chapter/issue/volume crossing (§9 Lane
    // C: "Start a new reading session when the user opens the item afresh or crosses to a
    // different chapter/issue/volume") — never on the hide/reshow-same-entry path (curChapterId
    // does not change there; see the shell's own LIFECYCLE PARITY note at the top of this
    // file), which is exactly what decideTransition's "noop" branch already encodes for
    // Player 1/audiobook.
    function _activityBeginIfNeeded() {
        var idf = reader._activityIdentity()
        var action = ActivityLaneHelpers.decideTransition(reader.activityActiveKey, idf)
        if (action === "noop") return
        if (action === "end") {
            reader.activityActiveKey = ""
            reader.activitySessionId = ""
            return
        }
        reader.activityActiveKey = ActivityLaneHelpers.keyFor(idf)
        reader.activitySessionId = (reader.activity && reader.activity.newSessionId) ? reader.activity.newSessionId() : ""
        reader._activityCoverageCompleted = false
    }

    // Common identity/metadata fields every reading_delta/media_completed fact for THIS entry
    // carries (§6 common fields). `itemLabel` is the CHAPTER/VOLUME label, `title` the SERIES —
    // mirrors recordProgress()'s own seriesTitle/curLabel split above. Portability comes from
    // _activityIdentity(): catalog entry ids remain syncable; filesystem-backed Vault entries use
    // their stable logical series id and stay local-only.
    function _activityCommonFields(idf) {
        return {
            world: "tankoban",
            kind: idf.kind,
            titleKey: idf.titleKey,
            itemKey: idf.itemKey,
            title: reader.seriesTitle,
            itemLabel: reader.curLabel,
            cover: ComicActivityHelpers.portableCover(reader.seriesCover),
            utcOffsetMinutes: -(new Date().getTimezoneOffset()),
            syncable: idf.syncable !== false,
            source: "comicreader-shell",
            sessionId: reader.activitySessionId
        }
    }

    // THE Lane C activity trigger: all three surfaces' activityPagesPresented (mounted below)
    // land here. `pageKeys0` is a raw 0-based physical-index list from whichever surface
    // rendered it; ComicActivityHelpers.pageKeysFor shapes it into the stable page-key strings
    // the native store/projector dedupe on (§10: sessionId+kind+itemKey+pageKey).
    function _onActivityPagesPresented(pageKeys0) {
        if (!reader.activity || !reader.activityActiveKey.length) return
        var idf = reader._activityIdentity()
        if (!idf) return
        var keys = ComicActivityHelpers.pageKeysFor(pageKeys0)
        if (!keys.length) return
        var fact = reader._activityCommonFields(idf)
        fact.readingForm = "fixed"
        fact.pageKeys = keys
        fact.atMs = Date.now()
        fact.progressMicros = 0
        reader.activity.recordReadingDelta(fact)
        reader._checkActivityCoverage(idf)
    }

    // §9 Lane C "Fixed-page completion": the entry's exact required non-terminal-broken page
    // set, asked fresh every time (a retry can un-break a page between checks). `_pageBroken`
    // is the SAME per-page verdict onCurrentPageChanged already gates maxSeen on above — one
    // owner of "is this page broken", not a second copy.
    function _activityRequiredPageKeys() {
        if (max <= 0) return []
        var broken = []
        for (var p = 0; p < max; p++) {
            if (reader._pageBroken(p + 1)) broken.push(p)
        }
        return ComicActivityHelpers.requiredPageKeys(max, broken)
    }
    // "when it first becomes true, record media_completed reason full_page_coverage" (§9). A
    // fast jump straight to the last page cannot complete the item by itself: only pages that
    // actually reached recordReadingDelta above (a real presented/activity fact, never a
    // requested-but-unrendered one) are ever covered by hasFixedCoverage.
    function _checkActivityCoverage(idf) {
        if (reader._activityCoverageCompleted) return
        var required = reader._activityRequiredPageKeys()
        if (!required.length) return
        if (!reader.activity.hasFixedCoverage(idf.kind, idf.itemKey, required)) return
        reader._activityCoverageCompleted = true
        var fact = reader._activityCommonFields(idf)
        fact.atMs = Date.now()
        fact.reason = "full_page_coverage"
        reader.activity.recordCompletion(fact)
    }

    // Is this page showing an error card rather than pixels? The BACKEND answers — one owner of
    // "is this page broken", and it self-heals (a MissingFile page that comes back reports "none"
    // again), which a locally cached failure map in the shell would not.
    //
    // A placarded page COUNTS as presented (it is genuinely where the reader is, and refusing to
    // bank it would resume them somewhere they never chose) but it must never count as READ — see
    // onCurrentPageChanged, where this gates the completion high-water mark.
    function _pageBroken(page1) {
        if (!core || !core.pageInfo || page1 < 1) return false
        var info = core.pageInfo(page1 - 1)
        if (!info || info.error === undefined) return false
        var e = String(info.error)
        return e.length > 0 && e !== "none"
    }

    // ================= the damaged page's two ways out (Task 11) =================
    // The approved design: "A damaged archive entry never mutates or extracts the book. The reader
    // shows a restrained error card in that page's place, offers Retry and Skip, and keeps
    // surrounding pages usable." The card raises; the surfaces forward; THIS decides.

    // RE-READ that one page. The archive is never touched — retryPage only clears the page's error
    // verdict and the decode coordinator's failure memo, then re-queues that page's decode in the
    // live generation (ComicReaderCore::retryPage, pinned by a SHA-256 of the fixture archive taken
    // before and after in the core harness).
    function retryPage(page) {
        if (!core || !core.retryPage || max <= 0) return
        var p = Math.round(page)
        if (p < 1 || p > max) return
        core.retryPage(p - 1)
        hud.showToast("Retrying page " + p)
    }
    // MOVE PAST it. Skip is navigation and nothing else: it does not mark the broken page as where
    // the reader is (the landing page's own presented() does that, once it is really on screen), and
    // it never touches the book.
    function skipPage(page) {
        if (max <= 0) return
        var p = Math.round(page)
        if (p < 1 || p > max) return
        manualActivity()
        if (p >= max) { _endOfVolumeToast(); return }   // nothing past the last page to skip to
        if (layout === "long_strip") {
            // The column has to physically move — currentPage is an output of the strip, not an
            // input to it. seekToPage takes the 0-BASED index of the page after the broken one,
            // which is `p` exactly (p is 1-based, so the next page's index is p).
            currentPage = p + 1
            if (!stripSurface.seekToPage(p)) _armStripRestore()   // not laid out yet: settle-gated retry
        } else {
            goToPageIndex(p + 1)     // snaps to the canonical unit in Paired Pages
        }
    }

    // Flush before the session engine captures/tears this down — mirrors the book reader's
    // goMinimize() (ReaderShell.qml: flushProgressSave() then minimized()). Minimize already ends
    // up flushed via Component.onDestruction -> shutdown(), but only once the Loader actually
    // destroys the item, which is a beat after this signal fires; the session snapshot Main.qml
    // captures in between reads live properties (not disk), so it needs no flush of its own, but
    // the disk-backed Progress record does — the process can die while parked in the taskbar,
    // between this click and that destruction, and a debounced write still pending in that
    // narrow window would never land.
    function goMinimize() {
        recordProgress()
        minimizeRequested()
    }

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
    //
    // WHY THIS SURVIVES Task 11's "no record without presentation" rule, since it looks like exactly
    // the kind of write that rule deletes: the position it banks is the entry's OPENING anchor,
    // which load() has just set (page 1, or the spot this book's own last record already said the
    // reader was on). It is not a page nobody has seen — it is where this open is putting them, and
    // for a resume it is a page they demonstrably saw last time. What the rule removed is the write
    // that fired for every page number a FLICK swept through inside an already-open entry.
    onCurChapterIdChanged: {
        load()
        if (!_remoteResumeApplying) recordProgress()
        _activityBeginIfNeeded()
    }
    onCurrentPageChanged: {
        // CONTRACT for Task 10 (double-page): maxSeen is the completion high-water mark, and
        // `currentPage` is the pair ANCHOR in double mode. MangaReader.qml bumpSeen() (lines 242-249)
        // ALSO folded in the pair PARTNER index, because a chapter that ENDS on a pair never reaches
        // `maxSeen >= max` from the anchor alone (finished stays false forever). Task 10's double
        // surface MUST drive currentPage (or bump maxSeen) with the reading-HIGHEST page of the unit,
        // not just the anchor — otherwise completion regresses for pair-terminated entries.
        //
        // A BROKEN page never advances it (Task 11). maxSeen is the "how much of this have you
        // read" mark and it is what makes a volume `finished` — so a volume whose LAST page is a
        // damaged entry must not mark itself complete merely because you navigated onto the error
        // card. A placarded page is a real POSITION (presented() banks it) and not a real READ; this
        // is the one line where those two part company.
        if (currentPage > maxSeen && !_pageBroken(currentPage)) maxSeen = currentPage
        // NO record here (Task 11). Navigating is not presentation — this handler is exactly where
        // the reader used to bank page numbers it had only been ASKED for. _onPresented is the one
        // trigger now.
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
            if (curChapterId === chapterId) { load(); recordProgress(); _activityBeginIfNeeded() }   // handler didn't fire during construction
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
        // The SPEED only — never whether it was running. Auto-scroll's running state is
        // session-only by the approved rule, so there is deliberately no key for it anywhere.
        property real   autoScrollSpeed: 1.0
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

        // ...and the Auto-scroll SPEED rides exactly the same three layers, for the same reason: a
        // dense tankobon and a webtoon want different paces. Replayed WITHOUT persisting — applying
        // memory is not forming it. The running state has no layer at all; it is always paused here.
        var sp = (rec && rec.autoScrollSpeed !== undefined) ? rec.autoScrollSpeed
                                                            : globalPrefs.autoScrollSpeed
        setAutoScrollSpeed(sp, false)

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
            _pendingPageFraction = -1
            return
        }
        var span = stripSurface.contentHeight - stripSurface.height
        if (span <= 0) {   // not laid out yet — retry; a slow decode costs a moment, never your place
            if (_stripRestoreTries < 3) { _stripRestoreTries += 1; stripRestore.restart(); return }
            // Genuinely gave up (the column never laid out in time). Same reason the not-on-strip
            // branch above clears both arms: leaving them set is a landmine for the NEXT unrelated
            // opening (a later mode switch back into Strip would take this stale fraction and jump
            // to a spot the current entry never asked for).
            _stripRestoreTries = 0
            _pendingStripFrac = 0
            _pendingPageFraction = -1
            return
        }
        _stripRestoreTries = 0
        // THE WITHIN-PAGE ANCHOR WINS (Task 11). It is anchored to the paper — page N, this far down
        // page N — so it survives a page-width or gap change since the record was written, where a
        // whole-column scrollFrac does not: change the measure and every page's share of the column
        // moves, so the same 0.41 lands somewhere else entirely.
        if (_pendingPageFraction >= 0) {
            stripSurface.seekToPageFraction(currentPage - 1, _pendingPageFraction)
            _pendingPageFraction = -1
            _pendingStripFrac = 0        // one target, one landing: never let the legacy arm fire too
        } else if (_pendingStripFrac > 0) {
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
    // "Opening chrome ... pauses it immediately" — the approved rule, taken literally. The chrome
    // coming BACK is the moment; the chrome going away is not (that is the reader being left alone,
    // which is when Auto-scroll is most useful). Reaching for the mouse is reaching for control.
    //
    // Hooked to the transition rather than to the value, so a chrome that is already up — which it
    // is when you press Start, since Start lives inside a menu — does not pause the thing being
    // started.
    onChromeVisibleChanged: if (chromeVisible) pauseAutoScroll()
    // The strip is the only surface with a column to move. Any layout that is not it has no motion
    // to be in, so the flag cannot be left true behind a paged surface.
    onLayoutChanged: if (layout !== "long_strip") pauseAutoScroll()
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
        // Auto-scroll: the shell owns WHETHER, the surface owns HOW FAST it lands per frame. Bound
        // IN only — the surface never writes either back, so there is one owner for the flag.
        autoScrollRunning: reader.autoScrollRunning
        autoScrollSpeed: reader.autoScrollSpeed
        // While the Loupe is up the wheel magnifies the LENS, so the column must not move under it.
        // The lens's own tracker swallows the wheel over the comic; this makes it structural rather
        // than dependent on which item Qt happened to deliver the event to.
        wheelLocked: reader.activeOverlay === "loupe"
        // NO resume binding in: the surface is a painter, and a bound fraction it applies itself is a
        // feedback loop (its own onScrolled writes reader.stripFraction, which re-drives the binding).
        // Restoring is a one-shot COMMAND from the shell (stripRestore -> haltScrollAt/seekToPage).
        onPageInView: function (page) { reader.currentPage = page }
        onScrolled: function (frac) { reader.stripFraction = frac }
        // A REAL wheel/trackpad gesture. The surface fires this BEFORE it applies its own movement,
        // so the motion is already stopped by the time the notch lands — the two drives are never
        // both writing contentY. This is the signal's first consumer in four tasks, and it is the
        // question it was always answering: was that a hand, or the machine?
        onManualNavigation: reader.manualActivity()
        // The column ran out of book. The surface reports; the shell clears the flag.
        onAutoScrollEnded: reader.pauseAutoScroll()
        // THE progress trigger (Task 11). The column reports the page it is really showing and how
        // far down it the viewport centre sits — the only layout where that fraction is ever
        // non-zero, and the reason the record can put you back on the same panel area.
        onPresented: function (page, frac) { reader._onPresented(page, frac) }
        onActivityPagesPresented: function (pageKeys) { reader._onActivityPagesPresented(pageKeys) }
        // a damaged page's card, from whichever ROW carries it (never the page under the centre)
        onRetryRequested: function (page) { reader.retryPage(page) }
        onSkipRequested: function (page) { reader.skipPage(page) }
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
        // THE progress trigger (Task 11): the unit is on screen, so the reader is really here. The
        // fraction is always 0 in a paged layout — the unit IS the viewport's whole travel — and it
        // rides the shared signature so there is one handler, not three.
        onPresented: function (page, frac) { reader._onPresented(page, frac) }
        onActivityPagesPresented: function (pageKeys) { reader._onActivityPagesPresented(pageKeys) }
        // a damaged half's card. The page is the HALF's, so the good side is never the one retried.
        onRetryRequested: function (page) { reader.retryPage(page) }
        onSkipRequested: function (page) { reader.skipPage(page) }
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
        onPresented: function (page, frac) { reader._onPresented(page, frac) }
        onActivityPagesPresented: function (pageKeys) { reader._onActivityPagesPresented(pageKeys) }
        onRetryRequested: function (page) { reader.retryPage(page) }
        onSkipRequested: function (page) { reader.skipPage(page) }
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

    // ---- WHAT THE READER IS DRAWING, for the Loupe (Task 9) ----
    // The lens needs one thing from the reading surfaces and only one: the boxes they are painting
    // right now, with the page each box belongs to. All three answer visiblePageRects() in the same
    // shape, so this is a lookup rather than three cases — and the LENS never reaches a surface
    // itself, which is half of why it cannot move the book.
    //
    // WHAT KEEPS IT LIVE. Almost nothing has to be declared: QML captures a binding's dependencies
    // as it EVALUATES, including every property read inside the function it calls — and
    // visiblePageRects() reads the column's contentY, each row's y/height, the paged surfaces' drawn
    // boxes and their own width/height straight from JS. So a scroll, a page turn, a zoom, a pan, a
    // width change and a resize all re-drive this binding on their own. (MEASURED, because an
    // earlier draft declared all of them explicitly and it was dead weight: dropping contentY from
    // that list changed nothing — the function body reads it, so the capture already had it.)
    //
    // The ONE thing capture CANNOT see is the decode revision. core.imageUrl() folds a per-page
    // `?rev=` that is bumped C++-side on pageReady, invisibly to QML, so the url a surface reports
    // can change with no property read changing with it — the same reason all three surfaces already
    // carry `readyRev`. That is what these three reads are, and they are read BEFORE the early
    // return because a binding that returns first subscribes to nothing.
    //
    // ...and it costs nothing while the Loupe is shut: three int reads and an empty array.
    // WHICH surface answers, named the same way `_pagedSurface` is and for the same reason: one
    // place owns the mapping, the binding below reads THIS, and a gate can assert the routing
    // without needing the surfaces to be painting (an offscreen harness roots its tree invisible, so
    // every mounted surface reads inactive there and answers with an empty list whatever the routing
    // says — a comparison of two empty lists would prove nothing at all).
    readonly property var _loupeSurface: activeSurface === "singleSurface" ? singleSurface
                                       : activeSurface === "doubleSurface" ? doubleSurface
                                       : stripSurface
    readonly property var loupePageRects: {
        var _rev = stripSurface.readyRev + doubleSurface.readyRev + singleSurface.readyRev
        if (activeOverlay !== "loupe") return []
        var s = _loupeSurface
        return (s && s.visiblePageRects) ? s.visiblePageRects() : []
    }

    // ---- the per-command ANCHOR seam (Task 8), for BOTH popovers ----
    // A temporary panel hangs under the command that raised it (Cover's shape, and the thing
    // Hemanth referenced by name). Task 7's Image panel could not do it and said why: the command
    // row is right-aligned and two of its six commands are live READOUTS whose label widths move
    // with the reader's layout and order, so the anchor has to be dynamic. The bar publishes each
    // command's centre, the HUD maps it into these coordinates, and this is where both popovers
    // read it.
    //
    // It reads `hud.commandAnchors` before delegating, and that read is what makes a caller's
    // BINDING reactive: mapToItem is a one-shot, so a binding that only called the function would
    // evaluate once and never track the row's relayout. -1 means "not laid out yet", and each
    // popover treats that as "no seam" and falls back to its own edge placement rather than
    // parking itself at x=0.
    function commandAnchorX(command) {
        var _dep = hud.commandAnchors
        return hud.commandAnchorX(command)
    }

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
        objectName: "comicInput"
        anchors.fill: parent
        // BENEATH THE PAGES (Task 11), and this one line is what makes the damaged-page card's
        // Retry and Skip actually clickable.
        //
        // This layer is one full-bleed MouseArea that accepts every left press to resolve the click
        // ZONES (turn forward, turn back, toggle the chrome). Mounted above the surfaces — which is
        // where it sat for two tasks — it wins delivery for every press in the reader, because Qt
        // offers a press to items in reverse paint order and stops at the first that accepts. The
        // card's buttons sit inside a surface, so they would have been drawn, hoverable, and
        // completely dead: a control that LIES, which is the exact defect class this arc has already
        // been bitten by.
        //
        // Lowering it is safe rather than clever, and the reason is structural: the three reading
        // surfaces accept NO pointer press at all. Their images and placeholders have no handlers,
        // and the strip's ListView is `interactive: false`, so its press event is ignored and
        // delivery continues downward. The ONLY press-accepting item in any of them is the error
        // card's TapHandler (verified by grep across all five surface/leaf files). So a click that
        // is not on a card button falls through to this layer exactly as it did before, and one that
        // IS finally reaches the button it is aimed at.
        //
        // Right-click is unaffected either way: the card's TapHandler takes the left button only,
        // and the context menu still resolves here.
        z: -1
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
        // The one surface that keeps keys of its own while it holds the keyboard (Task 9).
        loupeOpen: reader.activeOverlay === "loupe"
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
        // +/- with the lens up. It reaches the LENS and can reach nothing else — the input's own
        // Loupe branch sits above its Ctrl+zoom branch, so there is no keyboard path from an open
        // Loupe to a page zoom at all.
        onMagnifyLoupe: function (steps) { loupe.magnifySteps(steps) }
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
        onMinimizeRequested: reader.goMinimize()
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
        // Movement wakes ONLY the cursor; the HUD returns solely when the pointer reaches a top/bottom
        // reveal zone where the bars actually live (Hemanth 2026-08-01 — a mouse moved in the middle of
        // the page must NOT wake the chrome). This topmost overlay owns the hover the z:-1 input layer
        // beneath it cannot see, so it runs THAT layer's own zone test instead of an unconditional
        // reveal: in-zone emits revealRequested (-> restoreCursorAndChrome, chrome + cursor together);
        // the middle emits activity (-> _pokeCursor only; notifyActivity is a no-op while chrome hidden).
        onPositionChanged: function (mouse) { comicInput._checkRevealZone(mouse.y) }
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
        anchorX: reader.commandAnchorX("image")
        onProfileChangeRequested: function (profile) { reader.setRenderProfile(profile) }
        // Dismissal is a plain assignment, never openOverlay() — openOverlay TOGGLES, so routing a
        // dismissal through it would re-open the surface the reader just closed.
        onDismissRequested: reader.activeOverlay = ""
    }

    // The compact Layout menu (Task 8). Raised by the Layout command through the ONE overlay
    // coordinator, hanging under that command by the same anchor seam the Image panel now uses.
    //
    // It shows the three layouts always and Long Strip's contextual controls — portrait width, page
    // spacing, Auto-scroll start/pause and speed — only while Long Strip is live. It reads facts and
    // raises intents; the shell owns the width (through the anchoring setStripLayout), the
    // persistence, and the one Auto-scroll flag.
    ComicReaderLayoutPopover {
        id: layoutPopover
        objectName: "layoutPopover"
        open: reader.activeOverlay === "layout"
        anchorX: reader.commandAnchorX("layout")
        layout: reader.layout
        stripWidthPct: reader.stripWidthPct
        stripGap: reader.stripGap
        autoScrollRunning: reader.autoScrollRunning
        autoScrollSpeed: reader.autoScrollSpeed
        onLayoutRequested: function (value) { reader.setLayout(value) }
        // ONE door carrying BOTH values, straight onto the shell's anchoring setter: rescaling the
        // column moves every page, so without the anchor a width drag would silently scroll the
        // reader somewhere else in the book.
        onStripLayoutRequested: function (widthPct, gap) { reader.setStripLayout(widthPct, gap) }
        onAutoScrollStartRequested: reader.startAutoScroll()
        onAutoScrollPauseRequested: reader.pauseAutoScroll()
        onAutoScrollSpeedRequested: function (speed) { reader.setAutoScrollSpeed(speed) }
        onDismissRequested: reader.activeOverlay = ""
    }

    // The Loupe (Task 9) — the temporary full-resolution magnifier, completing the scaffold the
    // reader has carried since Task 5 (the command, the L key, the glyph, the signal, all live and
    // consumed by nothing).
    //
    // It reads ONE fact — the boxes the live surface is painting — and raises ONE intent. It has no
    // reference to the core, to a surface, to a page number, to a zoom or to a scroll position, so
    // "never changes page zoom, pan, layout, or reading position" is an absence rather than a guard.
    // Everything that makes that true at the SHELL level is here: the wheel is locked out of the
    // column, the keyboard is gated by modalOpen (minus the lens's own two keys), and Auto-scroll is
    // already paused by openOverlay before this surface ever appears.
    //
    // Mounted ABOVE the HUD, like its three siblings: the lens is a glass you are holding over the
    // page, so it occludes the chrome rather than sliding under it. Its pointer tracker is inset by
    // the chrome bands, which is what keeps the Loupe command, Back and the gold rail clickable.
    ComicReaderLoupe {
        id: loupe
        objectName: "loupe"
        open: reader.activeOverlay === "loupe"
        pages: reader.loupePageRects
        // The night veil is a composited Rectangle over the surfaces, not a render-profile field, so
        // the lens paints its own at the same opacity — otherwise it would glare bright out of a
        // dimmed page. Everything else the reader has done to the picture (brightness, contrast,
        // gamma, sharpen, rotation, auto-crop) rides the imageUrl the surface handed over and needs
        // no mirroring here.
        veilOpacity: ComicReaderState.nightVeilOpacity(reader.nightVeil)
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

    // Remote continue_progress winners have one door into an active Tankoban reader.
    // The bridge validates owner signal/kind/series and coalesces semantic duplicates;
    // this shell decides when the target is locally readable and reuses load/_applyResume.
    ComicReaderSyncedResumeBridge {
        id: syncedResumeBridge
        progress: reader.progress
        seriesId: reader.seriesId
        onResumeRequested: function(target) { reader._applySyncedResume(target) }
    }

    // an injected page store (or a future store) may not emit this exact progress/finished/failed
    // triple — don't spam "no such signal" warnings. A completed download re-runs load() so the
    // reader flips from the download path to the open pages (contract §3).
    Connections {
        target: reader.store
        ignoreUnknownSignals: true
        function onProgress(cid, done, total) { /* download line — chrome (Task 11) */ }
        function onFinished(cid) {
            var pending = syncedResumeBridge.pendingTarget
            if (pending && String(pending.chapterId || "") === String(cid)) {
                if (reader._applySyncedResume(pending)) return
            }
            if (cid === reader.curChapterId) reader.load()
        }
        function onFailed(cid, reason) { /* error placard — chrome (Task 11) */ }
    }
}
