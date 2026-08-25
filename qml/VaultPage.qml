// VaultPage — "On this machine": the local-media Vault as a host-owned full page, entered from
// the taskbar folder door. Slice 10 lands the permanent door + this page's EMPTY state (nothing
// indexed yet): eyebrow, title, and a dashed Add-folder drop surface. It paints from the
// VaultLibrary read-model (itemCount/scanning); the shelves that fill a populated Vault, and the
// folder-scan ingest behind Add folder, land in Slice 11. Same chrome vocabulary as
// Settings/Downloads (back · minimize · fullscreen · power) so it reads as one of the house's pages.
import QtQuick
import QtQuick.Controls
import QtCore
import "VaultApi.js" as VaultApi
import "TheatreApi.js" as TheatreApi
import "VaultBrowseState.js" as VaultBrowseState

Item {
    id: root
    objectName: "vaultPage"
    property Item backdrop: null
    signal backRequested()
    signal addFolderRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    // Slice 14: a folder-view row / preview door asked to open a file. Carries only the path —
    // LocalLaunch (C++) re-derives family + vault id + title, so routing/identity has one owner
    // (the same path the picker, drag-drop, and Open Recent funnel through win.openLocalMedia).
    signal openMediaRequested(string path)
    signal viewWorldRequested(var identity)

    Theme { id: theme }

    // ---- read-model: the Vault's published truth (revision-driven refresh) ----
    // Touch revision so every shelf/count re-reads on a committed publish; itemCount/scanning drive
    // the empty vs scanning-empty state (a populated Vault + its shelves arrive in Slice 11).
    readonly property int itemCount:
        (typeof VaultLibrary !== "undefined") ? (VaultLibrary.revision, VaultLibrary.itemCount) : 0
    readonly property bool scanning:
        (typeof VaultLibrary !== "undefined") ? VaultLibrary.scanning : false
    readonly property bool scanningEmpty: itemCount === 0 && scanning
    // Keep existing shelves instantiated during a background rescan. A returned drive remains
    // visibly unavailable until the successful publish replaces its away rows with fresh facts.
    readonly property bool populated: itemCount > 0
    // Lanista/plan state contract (vaultPage.vaultState / itemCount / cardVisible).
    readonly property string vaultState: scanning ? "scanning" : (itemCount > 0 ? "populated" : "empty")
    // Slice 9 — the browse FACE's own occupancy fact, distinct from `populated` (which is a
    // LIBRARY-WIDE item count, not "is there anywhere to browse"). A confirmed root that is
    // genuinely empty, or away, has itemCount 0 too — under the old `populated` gate the whole
    // browse face (rail/crumb/grid) stayed hidden and the pre-Vault onboarding screen showed
    // instead, which is right for "no storage configured" but WRONG for "a real root exists and
    // is empty/away" (design §4.5's occupancy table wants the grid itself to say why, not the
    // page to fall back to the add-a-folder invitation). The pre-Vault onboarding screen
    // (`vaultDropSurface` below) keeps owning the true "no storage at all" case — it already
    // carries real drag-drop, which the compact plate-6 empty-state treatment does not
    // replicate and must not regress (design acceptance #12, "not demoted").
    readonly property bool hasConfirmedStorage: root.browseRootsDetail.length > 0
    readonly property bool cardVisible: (typeof VaultLibrary !== "undefined") ? VaultLibrary.cardVisible : false
    // Read-only { id -> admissionVerdict } for video rows, re-read on the same revision clock. The
    // Continue rail gates on this so only durably-Admitted local videos ever resume.
    readonly property var admissionById:
        (typeof VaultLibrary !== "undefined")
            ? (VaultLibrary.revision, VaultLibrary.admissionById())
            : ({})
    property var identityCeremonies:
        (typeof VaultLibrary !== "undefined") ? VaultLibrary.identityCeremonies : []
    property bool identityCeremonyDismissed: false

    // ==== Slice 5: the assembled Browse face — carousel -> collapsible rail -> breadcrumb ->
    //      media-faced grid, over VaultLibrary's browse projection spine (Slices 1-4). ====

    // Current folder + rail-expanded survive an app restart (design §4.8); registry-backed,
    // isolated per COLOSSEUM_APPDATA_TAG the same way every other Colosseum store is (no
    // explicit `location:` — default backend, keyed by applicationName, exactly the pattern
    // ContentPreferences.qml already uses). `lastCrumbJson` carries the whole crumb trail (not
    // just the leaf path) so a restart restores the SAME folder with real breadcrumb titles,
    // not just its root.
    Settings {
        id: browseSettings
        category: "vaultBrowseV1"
        property string lastCrumbJson: "[]"
        property bool railExpanded: false
        // Vault ux uplift S12 — per-level sort choices ({levelKey: mode}; natural levels
        // store nothing — it is the default).
        property string sortPerLevel: "{}"
    }

    property var crumbStack: []              // [{key, displayTitle}, ...] selected root -> current level
    property string currentBrowsePath: ""    // the current level's key (folder path / show sentinel)
    property bool hiddenViewActive: false    // the reversible Hidden shelf, not a folder level
    // Vault ux uplift S14 — the in-vault search pseudo-level (the Hidden shelf's own pattern:
    // a view state that replaces the grid's population while the crumb trail beneath stays
    // intact, so leaving returns exactly where the user was). Escape leaves it FIRST (the
    // Main.qml Escape chain's own precedence), Enter opens the first hit.
    property bool searchViewActive: false
    property string searchQuery: ""
    function openSearch() {
        root.rememberCurrentScroll()
        root.hiddenViewActive = false
        root.searchViewActive = true
        root.searchQuery = ""
        browseSearchField.text = ""
        browseSearchField.forceActiveFocus()
    }
    function leaveSearchView() {
        if (!root.searchViewActive) return
        root.rememberCurrentScroll()
        root.searchViewActive = false
        root.searchQuery = ""
    }
    property var contextRow: null            // the row a card's right-click context menu targets

    // ==== Slice 7: the detail sheet — opening a Film row answers "what do I physically hold"
    //      instead of routing straight to Play (design decision #11). Episode/clip rows are
    //      unaffected: their Play routing is untouched (Slice 8's business, not this one's). ====
    property bool detailSheetVisible: false
    property string detailSheetKey: ""
    property string detailSheetRowState: "" // the grid row's own state at open time (Identify/Un-identify choice)
    // The opening row's STORED kind, remembered for the same reason its state is: the sheet's own
    // projection (VaultLibrary.browseDetail) answers "what do I physically hold", not "what medium
    // is this", so the sheet's Identify action has nothing else to hand the identify dialog — and
    // handing it nothing is what sent a film to the comic catalogues. Both openers (a grid Film
    // tile and a carousel slide's Details) carry kind now.
    property string detailSheetRowKind: ""
    // Vault ux uplift S7 — the opened row's vault id + its live watched mark, the sheet's two
    // watched-verb inputs. The id is the S6 join key browseAt() carries on Film/Episode/Clip
    // rows ("vault:"-prefixed; anything else resolves to "" and both verbs hide — the same
    // catalogue-id guard the context menu's verbs hold). The mark reads the store on the
    // Progress revision clock (reading Q_INVOKABLE watchers with NO named revision dependency
    // stale — the S6 join's own rule), so a mark made in one place re-opens the pair anywhere.
    property string detailSheetRowVaultId: ""
    readonly property bool detailSheetRowIsWatched: {
        if (root.detailSheetRowVaultId.length === 0 || typeof Progress === "undefined")
            return false
        Progress.revision
        return Progress.watchedMark(root.detailSheetRowVaultId) === 1
    }
    readonly property var detailSheetDetail: (root.detailSheetVisible && root.detailSheetKey
            && typeof VaultLibrary !== "undefined")
        ? (VaultLibrary.revision, VaultLibrary.browseDetail(root.detailSheetKey)) : ({})
    function openDetailSheet(row) {
        root.detailSheetKey = row.key || ""
        root.detailSheetRowState = row.state || ""
        root.detailSheetRowKind = row.kind || ""
        const rowId = row.id ? String(row.id) : ""
        root.detailSheetRowVaultId = rowId.indexOf("vault:") === 0 ? rowId : ""
        root.detailSheetVisible = true
    }
    function closeDetailSheet() { root.detailSheetVisible = false }

    readonly property var displayedCrumbStack: root.searchViewActive
        ? [{ key: "search:", displayTitle: "Search" }]
        : root.hiddenViewActive
        ? [{ key: "hidden:", displayTitle: "Hidden" }] : root.crumbStack

    // S14 — the search results, the third grid population. Group-shaped browse rows from
    // VaultLibrary.searchLibrary (full browseAt contract incl. the crumb-tag physicalFact and
    // the S6 join id), newest-hit first, re-joined with live Progress so the gold hairline and
    // the watched tick survive on search tiles. Revision-gated like every other projection.
    readonly property var searchRowsJoined: (root.searchViewActive
            && typeof VaultLibrary !== "undefined" && root.searchQuery.length > 0)
        ? (VaultLibrary.revision, root.joinProgressRows(
               VaultLibrary.searchLibrary(root.searchQuery))) : []

    // ==== Vault ux uplift S17 — the show page. A show folder's grid is season tiles; the
    //      seasonFacts structure (derived C++, progress-free) is joined against the durable
    //      watched marks here, producing per-season watched/unwatched counts (painted on the
    //      season tiles) and the page's next-up (the first unwatched episode in the derived
    //      natural order). The level is a show page when the current crumb's parent grid
    //      contained its SHOW tile — cheap proxy: currentBrowsePath is the show folder, its
    //      own rows are season nodes. ====
    readonly property bool showPageActive: root.crumbStack.length > 0
            && root.crumbStack[root.crumbStack.length - 1].nodeType === "show"
    // The structure through the revision clock (a publish re-derives; a rescan of the show).
    readonly property var currentShowFacts: (root.showPageActive
            && typeof VaultLibrary !== "undefined")
        ? (VaultLibrary.revision,
           VaultLibrary.seasonFactsForShow(root.currentBrowsePath)) : ({})
    // The joined form: counts + next-up, with Progress feeding the mark probe.  the silent 5s
    // tick bumps neither clock; a watch-mark write bumps Progress (the mark probe re-reads).
    readonly property var currentShowState: {
        if (root.showPageActive && typeof Progress !== "undefined") {
            Progress.revision
            return VaultApi.deriveSeasonState(root.currentShowFacts, function (id) {
                return Progress.watchedMark(id)
            })
        }
        return ({ seasons: [], nextUp: null })
    }
    // S17 — the next-up row's data model (vaultContinueTileComp's own shape): the first
    // unwatched episode of the derived sequence. A finished show (nextUp null) hides the row.
    readonly property var showNextUpRows: {
        const n = root.currentShowState.nextUp
        if (!n) return []
        return [{
            id: n.id || "", kind: "video", path: n.path || "", title: n.title || "",
            cover: "", progressFraction: 0
        }]
    }

    // ==== Vault ux uplift S12 — the browse sort. Natural order stays the default (spec §6
    //      law); the other four orderings are per-LEVEL choices persisted in
    //      browseSettings.sortPerLevel (a {levelKey: mode} JSON map — the same vaultBrowseV1
    //      home the crumb trail already uses). "recent" (Recently played) sorts QML-side over
    //      the joined rows — its key, lastReadMs, lives in the Progress store, not the index
    //      (VaultApi.sortRowsRecentlyPlayed); the other three are browseAt()'s `sort` param.
    property string sortMode: "natural"   // natural | title | newest | recent | size
    readonly property var sortVocabulary: [
        { mode: "natural", label: "Natural order" },
        { mode: "title", label: "Title" },
        { mode: "newest", label: "Newest arrival" },
        { mode: "recent", label: "Recently played" },
        { mode: "size", label: "Size" }
    ]
    function sortLabelOf(mode) {
        for (let i = 0; i < root.sortVocabulary.length; ++i)
            if (root.sortVocabulary[i].mode === mode) return root.sortVocabulary[i].label
        return "Natural order"
    }
    // what browseAt() gets: "recent" is QML-side, so the engine sees natural there
    readonly property string browseSortParam: root.sortMode === "title" ? "title"
        : root.sortMode === "newest" ? "newest"
        : root.sortMode === "size" ? "size" : "natural"
    function browseSettings_loadSort() {
        const key = root.currentBrowsePath || ""
        if (!key) { root.sortMode = "natural"; return }
        let map = {}
        try { map = JSON.parse(browseSettings.sortPerLevel || "{}") } catch (e) { map = {} }
        const mode = map[key]
        const next = (mode === "title" || mode === "newest" || mode === "recent"
                      || mode === "size") ? mode : "natural"
        // Only touch sortMode when it actually differs: browseGridRows reads it, and an
        // unconditional same-value write would still re-derive the level a second time per
        // navigation (two filesystem walks where there was one).
        if (root.sortMode !== next) root.sortMode = next
    }
    function browseSettings_setSort(mode) {
        root.sortMode = mode
        const key = root.currentBrowsePath || ""
        if (!key) return
        let map = {}
        try { map = JSON.parse(browseSettings.sortPerLevel || "{}") } catch (e) { map = {} }
        if (mode === "natural") delete map[key]   // natural is the default — store nothing
        else map[key] = mode
        browseSettings.sortPerLevel = JSON.stringify(map)
    }
    // A level change reloads THAT level's remembered sort (never inherits the prior level's)
    // AND resets the filters (the plan's predictability-over-stickiness law — one handler,
    // one place, both level-arrival behaviors).
    onCurrentBrowsePathChanged: {
        root.browseSettings_loadSort()
        root.resetFilters()
    }

    // ==== Vault ux uplift S13 — the browse filters. Predicates: kind (stored comic|book|video),
    //      identification state (uncertain-only — directly serves the identify workflow),
    //      presence (here / on other drives), and watched state (video — its fact lives in the
    //      Progress store, so it filters QML-side over the joined rows, VaultApi
    //      .filterRowsByWatched; the other three ride browseAt()'s `filter` param). Filters
    //      RESET on level change (predictability over stickiness — the plan's own law); nothing
    //      persists. The Hidden view stays unfiltered (its own order and population are the
    //      shelf's contract). ====
    property string filterKind: ""       // "" | video | comic | book
    property string filterWatched: ""    // "" | unwatched | watched
    property string filterIdent: ""      // "" | uncertain
    property string filterPresence: ""   // "" | present | away
    // The C++ half of the active predicates (index facts only — watched never rides this map).
    readonly property var cxxFilterMap: ({
        kind: root.filterKind,
        identState: root.filterIdent,
        presence: root.filterPresence
    })
    readonly property int activeFilterCount: (root.filterKind !== "" ? 1 : 0)
        + (root.filterWatched !== "" ? 1 : 0)
        + (root.filterIdent !== "" ? 1 : 0)
        + (root.filterPresence !== "" ? 1 : 0)
    function resetFilters() {
        root.filterKind = ""
        root.filterWatched = ""
        root.filterIdent = ""
        root.filterPresence = ""
    }
    // The projection both the grid property and the art-resolved re-projection share: the
    // engine sort, the Progress join, and the QML-side recent ordering in one place — an art
    // landing must re-project the SAME order the grid holds or the in-place ListModel diff
    // would see a reorder as a structural change and rebuild the grid.
    function projectBrowseRows() {
        const rows = VaultLibrary.browseAt(root.currentBrowsePath, root.browseSortParam,
                                           root.cxxFilterMap)
        if (typeof Progress === "undefined") return rows
        const joined = root.joinProgressRows(rows)
        // S13: the watched filter runs over the JOINED rows (the mark is a Progress fact);
        // the count before it feeds the QML-side "filtered" empty-cause override below.
        root.browseRowsBeforeWatchedFilter = joined.length
        const watchedFiltered = root.filterWatched !== ""
            ? VaultApi.filterRowsByWatched(joined, root.filterWatched) : joined
        let final = root.sortMode === "recent" ? VaultApi.sortRowsRecentlyPlayed(watchedFiltered)
                                               : watchedFiltered
        // S17: on the show page, each season tile gains its watched fact (the derived season
        // mathematics, joined — the tile itself never recomputes it). Tiles are keyed by their
        // displayTitle's ordinal; a tile whose ordinal has no season entry stays undecorated.
        if (root.showPageActive && root.currentShowState.seasons.length) {
            for (let i = 0; i < final.length; i++) {
                if (final[i].nodeType !== "season") continue
                const m = /(\d+)/.exec(String(final[i].displayTitle || ""))
                if (!m) continue
                const ord = Number(m[1])
                for (const s of root.currentShowState.seasons) {
                    if (s.season === ord) {
                        final[i].watchedFact = s.watched + "/" + s.total
                        final[i].unwatchedFact = s.unwatched
                        // The one physical-fact law (TB2) still holds: ONE factual line. The
                        // season tile's line becomes the count it owns — the card's own
                        // "N episodes" is the total, so the fact line gains only the watched
                        // fraction (never a synopsis, never an inference).
                        const base = String(final[i].physicalFact || "")
                        final[i].physicalFact = base ? (base + " · " + final[i].watchedFact
                                                        + " watched") : (final[i].watchedFact
                                                          + " watched")
                        break
                    }
                }
            }
        }
        return final
    }
    // S13: the joined-row count BEFORE the watched filter — the QML half of the "filtered"
    // empty-cause trigger (the C++ half knows only the index-fact predicates).
    property int browseRowsBeforeWatchedFilter: 0
    // The menu's open state + position (mapped imperatively at open — the rail menu's own
    // pattern; a publish/sort change can never invalidate what anchors would have held).
    property bool sortMenuOpen: false
    property real sortMenuX: 0
    property real sortMenuY: 0
    function toggleSortMenu() {
        if (root.sortMenuOpen) { root.sortMenuOpen = false; return }
        const p = browseSortControl.mapToItem(root, 0, browseSortControl.height)
        root.sortMenuX = Math.max(10, Math.min(p.x, root.width - 220))
        root.sortMenuY = p.y + 6
        root.sortMenuOpen = true
    }
    // S13 — the filter panel's open state + position (the sort menu's own pattern).
    property bool filterMenuOpen: false
    property real filterMenuX: 0
    property real filterMenuY: 0
    function toggleFilterMenu() {
        if (root.filterMenuOpen) { root.filterMenuOpen = false; return }
        const p = browseFilterControl.mapToItem(root, 0, browseFilterControl.height)
        root.filterMenuX = Math.max(10, Math.min(p.x - 120, root.width - 260))
        root.filterMenuY = p.y + 6
        root.filterMenuOpen = true
    }
    // One chip row per predicate axis: label + mutually exclusive options ("" = the axis is
    // off). Clicking the active option turns the axis off again (a chip toggle, not a lock).
    function setFilterAxis(axis, value) {
        const current = axis === "kind" ? root.filterKind
                       : axis === "watched" ? root.filterWatched
                       : axis === "ident" ? root.filterIdent : root.filterPresence
        const next = current === value ? "" : value
        if (axis === "kind") root.filterKind = next
        else if (axis === "watched") root.filterWatched = next
        else if (axis === "ident") root.filterIdent = next
        else root.filterPresence = next
    }

    readonly property var browseRootsDetail: (typeof VaultLibrary !== "undefined")
        ? (VaultLibrary.revision, VaultLibrary.rootsDetail()) : []
    // S9 (vault ux uplift) — the rail marquee's two data sources. rootCount() is the "· N
    // folders" count (revision-driven: a confirm/remove publishes, so the count follows);
    // downloadsRootPath() is static after boot (main.cpp wires the synthetic root before QML
    // loads — a remove only HIDES the root, the path itself never changes mid-run), so it
    // needs no revision gate.
    readonly property int vaultRootCount: (typeof VaultLibrary !== "undefined")
        ? (VaultLibrary.revision, VaultLibrary.rootCount()) : 0
    readonly property string vaultDownloadsRootPath: (typeof VaultLibrary !== "undefined")
        ? VaultLibrary.downloadsRootPath() : ""
    readonly property var hiddenSeriesRows: (root.populated && typeof VaultLibrary !== "undefined")
        ? (VaultLibrary.revision, VaultLibrary.hiddenSeries()) : []
    readonly property var carouselArrivalRows: (root.hasConfirmedStorage && typeof VaultLibrary !== "undefined")
        ? (VaultLibrary.revision, VaultLibrary.recentArrivals(6)) : []
    // Slice 9 — the grid's own empty-cause projection (design §4.5's four causes), keyed off
    // the CURRENT level so the component never has to infer anything in QML. Guarded on
    // currentBrowsePath (not yet set for one frame around initBrowseState()) so the component
    // stays invisible rather than flashing a wrong cause, AND on `browseGridRows` already being
    // empty — a real bug found live driving the Gintama-scale fixture: without this second
    // guard, both C++ calls (each re-deriving rootsDetail()/browseAt() from scratch) fired on
    // EVERY navigation regardless of whether the grid had anything to show, doubling the cost of
    // walking a 300-episode directory and pushing a "redrill" past vault_browse_smoke.json's own
    // 15s wait. The overwhelming majority of levels are non-empty, so this guard is the
    // difference that matters.
    // S13: the active filter rides the call (the C++ half of the "filtered" trigger), and the
    // QML half fires FIRST — a watched-filter that emptied a level which HAD joined rows reads
    // as "filtered" without a second filesystem walk (browseRowsBeforeWatchedFilter is the
    // joined count projectBrowseRows recorded).
    readonly property string browseEmptyCause: (!root.searchViewActive
            && root.browseGridRows.length === 0
            && typeof VaultLibrary !== "undefined" && root.currentBrowsePath)
        ? (VaultLibrary.revision,
           (root.filterWatched !== "" && root.browseRowsBeforeWatchedFilter > 0)
               ? "filtered"
               : VaultLibrary.browseEmptyCause(root.currentBrowsePath, root.cxxFilterMap)) : ""
    readonly property int browseEmptyAwayCount: (root.browseGridRows.length === 0
            && typeof VaultLibrary !== "undefined" && root.currentBrowsePath)
        ? (VaultLibrary.revision, VaultLibrary.browseEmptyAwayCount(root.currentBrowsePath)) : 0
    // Two deliberate translations from the shipped slide (locked design §4.10): the blurb slot
    // carries the PHYSICAL FACT only (a descriptive blurb is a tagline, banned), and the
    // gradient is neutral house-token white-alpha, never a per-slide colour.
    readonly property var carouselSlides: root.carouselArrivalRows.map(function (r) {
        return {
            title: r.displayTitle || "",
            blurb: r.physicalFact || "",
            ghost: r.nodeType === "film" ? "FILM" : (r.nodeType === "show" || r.nodeType === "season") ? "TV" : "",
            c1: Qt.rgba(1, 1, 1, 0.10),
            c2: Qt.rgba(1, 1, 1, 0.025),
            art: r.coverRef || "",
            artKind: "poster",
            __row: r
        }
    })

    // Hidden rows (series()-shaped) translated into the browseAt() row contract so the SAME
    // Slice-4 cards render them — the Hidden shelf is reachable from the rail (design §0
    // acceptance), not a second card language.
    readonly property var hiddenRowsAsBrowse: root.hiddenSeriesRows.map(function (s) {
        const count = Number(s.count || 0)
        return {
            key: s.key, nodeType: "folder",
            displayTitle: s.title || "",
            physicalFact: count + (count === 1 ? " item" : " items"),
            path: s.subtreePath || "",
            state: "identified",
            away: Number(s.awayCount || 0) > 0,
            counts: { items: count },
            coverRef: s.coverUrl || "",
            // hiddenSeries() rows already carry the group's stored kind — passing it through keeps
            // this translation a FULL browseAt() row, now that `kind` is part of that contract.
            kind: s.kind || ""
        }
    })

    readonly property var browseGridRows: {
        // Slice 9: gated on hasConfirmedStorage, not populated — a freshly-confirmed root whose
        // first census hasn't published yet (itemCount still 0) has REAL resolving-state rows
        // from browseAt()'s own live filesystem walk (design §4.6's signature "resolve in
        // place" moment); the old `populated` gate silently suppressed this until the very
        // first publish landed. (Never reachable before this slice anyway, since the whole
        // browse face was itself gated on `populated` — see that gate's own comment above.)
        if (!root.hasConfirmedStorage) return []
        if (root.searchViewActive) return root.searchRowsJoined
        if (root.hiddenViewActive) return root.hiddenRowsAsBrowse
        if (typeof VaultLibrary === "undefined" || !root.currentBrowsePath) return []
        VaultLibrary.revision // dependency: re-project on every committed publish
        // Vault ux uplift S6: the live Progress join so the cards can paint the spec's gold
        // resume hairline and S3's durable watched tick. The two revision clocks are named
        // separately and honestly: a committed publish re-projects the level, a Progress
        // lifecycle write (open/close/minimize/mark) re-joins it — and the silent 5s video
        // tick bumps NEITHER, so the join can never reintroduce the Continue-repaint stutter
        // cascade (the same discipline folderDetailRows above already follows).
        // Vault ux uplift S12: the same projection now carries the level's sort —
        // projectBrowseRows() = browseAt(path, browseSortParam) + join + the QML-side
        // "recent" ordering. root.sortMode is itself a dependency: choosing a sort
        // re-derives the level through this same path (the grid's in-place diff then sees
        // a deliberate reorder as the structural change it is).
        // S13: the filter state is a dependency the same way — toggling a predicate
        // re-derives the level through the same single funnel.
        root.sortMode
        root.filterKind; root.filterWatched; root.filterIdent; root.filterPresence
        if (typeof Progress === "undefined")
            return VaultLibrary.browseAt(root.currentBrowsePath, root.browseSortParam,
                                         root.cxxFilterMap)
        Progress.revision
        return root.projectBrowseRows()
    }
    // Vault ux uplift S6: decorate browseAt() rows with live Progress facts. VaultApi's
    // joinRow supplies progressFraction/lastReadMs/progressed STRAIGHT from the store (this
    // never recomputes a position — surfacing, not deriving); the `watched` flag adds S3's
    // durable mark (ProgressStore.watchedMark), which lives on in the store AFTER the resume
    // record itself is retired at ≥90% — exactly why the tick needs its own lookup rather
    // than trusting the join's hasProgress. Vault ids only: a catalogue id must never gain a
    // Vault tick from here.
    function joinProgressRows(rows) {
        var joined = VaultApi.joinRows(Progress, rows || [])
        for (var i = 0; i < joined.length; ++i) {
            var id = String(joined[i].id || "")
            joined[i].watched = id.indexOf("vault:") === 0 && Progress.watchedMark(id) === 1
        }
        return joined
    }
    // ==== Slice 6: living tile states — re-project WITHOUT rebuilding the grid. ====
    // `browseGridRows` recomputes to a BRAND NEW array every time (a fresh publish, a root
    // going away, an identify-in-place settling) — binding `GridView.model` straight to it (as
    // Slice 5 did) would hand the view a different array object on every recompute, which Qt
    // Quick treats as a wholesale model reset: every delegate destroyed and recreated, scroll
    // position gone, the crossfade Behavior on VaultPosterCard/VaultWideCard never gets to run
    // because there is no "before" instance left to animate FROM. `gridModel` is the one stable
    // thing the GridView binds to; `syncGridModel` below is the only place allowed to touch it.
    onBrowseGridRowsChanged: root.syncGridModel(root.browseGridRows)
    // The level `gridModel` currently reflects (a real path, a show-sentinel key, or "hidden:") —
    // compared against the CURRENT level on every sync so genuine navigation (which legitimately
    // resets scroll) is told apart from an in-place content update (which must NOT touch scroll:
    // touching it here would yank a live in-progress scroll back to the remembered position on
    // every unrelated background repaint, e.g. a resolve tick landing while the user scrolls).
    property string gridSyncedLevelKey: "//__unsynced__//"
    function syncGridModel(rows) {
        rows = rows || []
        const levelKey = root.searchViewActive ? "search:"
            : root.hiddenViewActive ? "hidden:" : root.currentBrowsePath
        const levelChanged = levelKey !== root.gridSyncedLevelKey
        root.gridSyncedLevelKey = levelKey

        var structurallySame = !levelChanged && gridModel.count === rows.length
        if (structurallySame) {
            for (var i = 0; i < rows.length; ++i) {
                if (gridModel.get(i).key !== (rows[i].key || "")) { structurallySame = false; break }
            }
        }
        if (structurallySame) {
            // Same folder, same rows in the same order: update each row's DATA in place.
            // ListModel.set() rewrites a role's value without destroying the delegate it feeds —
            // the delegate stays the SAME Item, its `row` property binding re-evaluates to the
            // new object, and VaultPosterCard/VaultWideCard's own `settledOpacity` Behavior does
            // the crossfade (design §4.4: "the tile animates... rather than teleporting").
            for (var k = 0; k < rows.length; ++k)
                gridModel.set(k, { key: rows[k].key || "", modelData: rows[k] })
        } else {
            // A genuine structural change (a different folder, a different row SET, the
            // hidden-view toggle) — clear + repopulate is correct here: these are not the same
            // tiles, so there is nothing to preserve identity FOR.
            gridModel.clear()
            for (var j = 0; j < rows.length; ++j)
                gridModel.append({ key: rows[j].key || "", modelData: rows[j] })
        }
        if (levelChanged) Qt.callLater(root.restoreGridScroll)
    }
    // Browse-artwork execution plan, Slice 3 part 2 — the resolver's re-projection hook. A poster/
    // frame-grab landing async (VaultArtworkResolver::artResolved, VaultLibrary.h's own doc on
    // browseArtResolved) fires this NARROW signal — never VaultLibrary.revision/changed(), which
    // browseGridRows above depends on for its OWN re-derivation and which every other
    // revision-gated property in this file (rootsDetail/hiddenSeries/admissionById/browseDetail/…)
    // ALSO depends on; routing artResolved through revision would force every one of those to
    // redo its own SQL/filesystem work on every single tile's art landing, exactly the "doubling
    // the cost" hazard `browseEmptyCause`'s own comment above already names for a different call.
    // Re-deriving browseAt() and handing it straight to syncGridModel() reuses the SAME in-place
    // ListModel diff identify-in-place's revision-driven path already relies on (structurallySame
    // key-set → ListModel.set() updates each row's DATA without destroying the delegate, so the
    // settledOpacity crossfade still runs) — just reached without touching browseGridRows or any
    // other revision-gated property.
    Connections {
        target: (typeof VaultLibrary !== "undefined") ? VaultLibrary : null
        function onBrowseArtResolved(rowKey) {
            if (!root.hasConfirmedStorage || root.hiddenViewActive || root.searchViewActive
                || !root.currentBrowsePath) return
            // Vault ux uplift S6: through the same Progress join browseGridRows uses — a bare
            // browseAt() here would hand syncGridModel un-joined rows and the in-place
            // ListModel.set would silently STRIP every tile's progressFraction/watched facts
            // each time a poster lands. S12: through projectBrowseRows() for the same reason
            // PLUS the sort — an unsorted re-projection would read as a structural reorder
            // and rebuild the grid every time art lands on a sorted level.
            root.syncGridModel(root.projectBrowseRows())
        }
    }
    readonly property bool browseGridWide: root.browseGridRows.length > 0
        && (root.browseGridRows[0].nodeType === "episode" || root.browseGridRows[0].nodeType === "clip")
    readonly property int posterCellWidth: 170
    readonly property int posterCellHeight: 300
    readonly property int wideCellWidth: 320
    readonly property int wideCellHeight: 250

    function browseSettings_setLastCrumb() {
        browseSettings.lastCrumbJson = JSON.stringify(root.crumbStack)
    }
    function rememberCurrentScroll() {
        const key = root.searchViewActive ? "search:"
            : root.hiddenViewActive ? "hidden:" : root.currentBrowsePath
        if (key && typeof grid !== "undefined" && grid) VaultBrowseState.rememberScroll(key, grid.contentY)
    }
    function restoreGridScroll() {
        const key = root.searchViewActive ? "search:"
            : root.hiddenViewActive ? "hidden:" : root.currentBrowsePath
        if (typeof grid !== "undefined" && grid) grid.contentY = VaultBrowseState.scrollFor(key)
    }
    function selectRoot(path, name) {
        root.rememberCurrentScroll()
        root.leaveSearchView()
        root.hiddenViewActive = false
        // S17: the crumb carries the node type it was opened FROM ("" for a root), so a page
        // under a "show" tile knows it is the show page without re-walking the grid.
        root.crumbStack = [{ key: path, displayTitle: name, nodeType: "" }]
        root.currentBrowsePath = path
        root.browseSettings_setLastCrumb()
    }
    function pushCrumb(key, title, nodeType) {
        root.rememberCurrentScroll()
        root.crumbStack = root.crumbStack.concat(
            [{ key: key, displayTitle: title, nodeType: nodeType || "" }])
        root.currentBrowsePath = key
        root.browseSettings_setLastCrumb()
    }
    function goToCrumb(index) {
        if (index < 0 || index >= root.crumbStack.length - 1 || root.hiddenViewActive
            || root.searchViewActive) return
        root.rememberCurrentScroll()
        root.crumbStack = root.crumbStack.slice(0, index + 1)
        root.currentBrowsePath = root.crumbStack[root.crumbStack.length - 1].key
        root.browseSettings_setLastCrumb()
    }
    function ascendBrowse() {
        if (root.searchViewActive) {
            root.leaveSearchView()
            return
        }
        if (root.hiddenViewActive) {
            root.rememberCurrentScroll()
            root.hiddenViewActive = false
            root.currentBrowsePath = root.crumbStack.length ? root.crumbStack[root.crumbStack.length - 1].key : ""
            return
        }
        if (root.crumbStack.length > 1) root.goToCrumb(root.crumbStack.length - 2)
    }
    function openHidden() {
        root.rememberCurrentScroll()
        root.leaveSearchView()
        root.hiddenViewActive = true
    }
    function handleBrowseCardOpen(row) {
        if (!row) return
        if (root.searchViewActive) {
            // S14: a search hit drills into its folder — leave the search pseudo-level first
            // so the crumb trail it pushes is the real browse stack again. A film opens the
            // detail sheet as an overlay (search stays beneath; Escape closes the sheet first
            // per the window chain). An episode/clip plays straight through.
            if (row.nodeType === "folder" || row.nodeType === "show" || row.nodeType === "season") {
                root.leaveSearchView()
                root.pushCrumb(row.key, row.displayTitle, row.nodeType)
                return
            }
            if (row.nodeType === "film") {
                root.openDetailSheet(row)
                return
            }
            if (row.path) root.openMediaRequested(row.path)
            return
        }
        if (root.hiddenViewActive) {
            if (typeof VaultLibrary !== "undefined") VaultLibrary.restoreGroup(row.key || "")
            return
        }
        if (row.nodeType === "folder" || row.nodeType === "show" || row.nodeType === "season") {
            root.pushCrumb(row.key, row.displayTitle, row.nodeType)
            return
        }
        if (row.nodeType === "film") {
            // Opening a film answers "what do I physically hold" (design decision #11) — the
            // detail sheet, not a direct Play. Episode/clip Play routing is untouched.
            root.openDetailSheet(row)
            return
        }
        // episode / clip -> Play routes as today.
        if (row.path) root.openMediaRequested(row.path)
    }
    // Slice 9 — Enter opens whichever card the grid's OWN keyboard traversal currently has
    // focused (`grid.currentIndex`), through the exact same routing `handleBrowseCardOpen`
    // already gives a mouse click: drill for folder/show/season, the detail sheet for a film,
    // Play for episode/clip. `gridModel`/`grid` are declared inside `mainArea` below this
    // function's own declaration point — same document, forward id reference is valid.
    function openFocusedGridCard() {
        if (typeof grid === "undefined" || grid.currentIndex < 0) return
        if (grid.currentIndex >= gridModel.count) return
        const rec = gridModel.get(grid.currentIndex)
        if (rec && rec.modelData) root.handleBrowseCardOpen(rec.modelData)
    }
    // The carousel's own Play affordance — unlike a grid card's click, this is unconditional
    // "start watching/drilling" (its label says Play, not Open) and stays exactly as Slice 5
    // wired it. The carousel's "Details" secondary action (a deliberate no-op until this slice)
    // is what opens the detail sheet for a film slide, below.
    function handleCarouselPrimary(row) {
        if (!row) return
        if (row.nodeType === "folder" || row.nodeType === "show" || row.nodeType === "season") {
            root.pushCrumb(row.key, row.displayTitle)
            return
        }
        if (row.path) root.openMediaRequested(row.path)
    }
    function identifyBrowseRow(row) {
        if (!row) return
        identifyDialog.groupKey = row.key || ""
        identifyDialog.titleText = row.displayTitle || ""
        // The row's STORED kind, straight from the C++ projection (VaultLibrary::browseAt now
        // carries comic|book|video per node). Passing "" here — which this function used to do,
        // back when the browse row contract was kind-agnostic — sent every identify from the
        // browse face down VaultIdentifyDialog.searchNow()'s trailing branch (ComicsCatalog then
        // MalCatalog), so identifying a MOVIE searched comic and manga catalogues and never IMDb.
        // Never guessed from row.nodeType: nodeType is a structural verdict about the filesystem
        // ("film" is folder shape, not medium), kind is the scanner's stored classification.
        // A row whose kind the index genuinely does not know yet stays "" and keeps the old
        // behavior rather than picking a catalogue on its behalf.
        identifyDialog.kind = row.kind || ""
        // Now that a browse row can legitimately BE a book, it needs the same EPUB pre-fill the
        // old shelves' identifyTile() does — the dialog's "book" branch has no catalogue to search
        // and would otherwise open on "No catalogue candidates yet."
        identifyDialog.embeddedIdentity = identifyDialog.kind === "book"
            ? root.bookEmbeddedIdentity(identifyDialog.groupKey, identifyDialog.titleText) : ({})
        identifyDialog.feedback = ""
        identifyDialog.open()
    }
    function openCardContextMenu(row) {
        root.contextRow = row
        cardContextMenu.popup()
    }
    // Vault ux uplift S7: the vault id on the menu's target row — the live Progress join key
    // browseAt() carries on Film/Episode/Clip rows since S6. The watched verbs are Vault verbs:
    // a catalogue id (or an id-less container row/away fallback row) must never be marked here,
    // so anything non-"vault:" resolves to "" and both menu items hide.
    readonly property string contextRowVaultId: {
        if (!root.contextRow) return ""
        const id = root.contextRow.id
        return (id && String(id).indexOf("vault:") === 0) ? String(id) : ""
    }
    // Vault ux uplift S7: the two verbs' single writer — "Mark watched" writes the durable
    // 1 mark (setWatchedMark(id, true)); "Mark unwatched" clears it (clearWatchedMark — the
    // no-mark state lets the auto rules resume, unlike setWatchedMark(id, false) which pins a
    // manual -1). vault ids pass seriesRootId() through unchanged, so both are exactly the
    // catalogue-id paths the rest of the app already uses.
    function markContextRowWatched(on) {
        if (root.contextRowVaultId.length === 0 || typeof Progress === "undefined") return
        if (on) Progress.setWatchedMark(root.contextRowVaultId, true)
        else Progress.clearWatchedMark(root.contextRowVaultId)
    }
    function initBrowseState() {
        if (!root.hasConfirmedStorage || typeof VaultLibrary === "undefined") return
        const roots = VaultLibrary.rootsDetail()
        if (!roots.length) return
        let restored = []
        try { restored = JSON.parse(browseSettings.lastCrumbJson || "[]") } catch (e) { restored = [] }
        let rootAvailable = false
        if (restored && restored.length) {
            const savedRootPath = restored[0].key
            for (let i = 0; i < roots.length; ++i) {
                if (roots[i].path === savedRootPath && roots[i].available) { rootAvailable = true; break }
            }
        }
        if (restored && restored.length && rootAvailable) {
            root.crumbStack = restored
            root.currentBrowsePath = restored[restored.length - 1].key
            return
        }
        // Stale or first-run: fall back to the first AVAILABLE root (else the first root at
        // all) — a full per-ancestor existence walk would need a path-exists C++ probe this
        // slice does not add (named honestly in the report, not silently assumed).
        let avail = null
        for (let i = 0; i < roots.length; ++i) { if (roots[i].available) { avail = roots[i]; break } }
        if (!avail) avail = roots[0]
        root.crumbStack = [{ key: avail.path, displayTitle: avail.name }]
        root.currentBrowsePath = avail.path
    }
    onHasConfirmedStorageChanged: if (root.hasConfirmedStorage && root.crumbStack.length === 0) root.initBrowseState()

    // ---- Slice 12 dress: the in-world tab bar (All · Comics · Books · Video · Folders) ----
    property string currentTab: "all"
    readonly property var tabModel: [
        { key: "all", label: "All" }, { key: "comic", label: "Comics" },
        { key: "book", label: "Books" }, { key: "video", label: "Video" },
        { key: "folders", label: "Folders" }, { key: "hidden", label: "Hidden" }
    ]
    property var autoFilmEnrichmentRequested: ({})
    function requestAutoFilmEnrichment(list) {
        if (!list) return list
        for (var i = 0; i < list.length; i++) {
            var tile = list[i]
            if (!tile || tile.identSource !== "IMDB" || !tile.identityId) continue
            var key = String(tile.key || tile.identityId)
            if (root.autoFilmEnrichmentRequested[key]) continue
            root.autoFilmEnrichmentRequested[key] = true
            root.requestProgressiveFilmIdentity(tile)
        }
        return list
    }
    function seriesFor(kind) {
        var list = (typeof VaultLibrary !== "undefined")
            ? (VaultLibrary.revision, VaultLibrary.series(kind)) : []
        return kind === "video" ? root.requestAutoFilmEnrichment(list) : list
    }
    // Kinds whose shelf shows under the current tab; Folders is a flat all-kinds gallery instead.
    function shelfKinds() {
        if (root.currentTab === "comic" || root.currentTab === "book" || root.currentTab === "video")
            return [root.currentTab]
        if (root.currentTab === "folders") return []
        if (root.currentTab === "hidden") return []
        return ["comic", "book", "video"]
    }
    function allSeries() {
        return root.seriesFor("comic").concat(root.seriesFor("book")).concat(root.seriesFor("video"))
    }
    function hiddenSeries() {
        return (typeof VaultLibrary !== "undefined") ? (VaultLibrary.revision, VaultLibrary.hiddenSeries()) : []
    }
    function revealTile(data) {
        if (typeof VaultLibrary !== "undefined" && data && data.subtreePath)
            VaultLibrary.revealInExplorer(data.subtreePath)
    }
    // An EPUB's identity comes from the file itself — there is no offline book catalogue for
    // VaultIdentifyDialog's "book" branch to search, so it renders this pre-filled candidate or
    // nothing at all. Extracted from identifyTile() (unchanged logic) so the browse face's
    // identifyBrowseRow(), which can now carry kind === "book" too, uses the identical rule
    // instead of a second copy of it. Returns ({}) when the index knows no book row here.
    function bookEmbeddedIdentity(groupKey, fallbackTitle) {
        if (typeof VaultLibrary === "undefined" || !groupKey) return ({})
        var bookRows = VaultLibrary.items("book", groupKey) || []
        var book = bookRows.length ? bookRows[0] : {}
        if (!book.title && !book.displayTitle) return ({})
        return {
            title: book.title || book.displayTitle || fallbackTitle,
            sourceId: "epub:" + String(book.id || groupKey),
            synopsis: book.synopsis || "",
            coverUrl: book.coverUrl || "",
            year: Number(book.year || 0)
        }
    }
    function identifyTile(data) {
        if (!data) return
        identifyDialog.groupKey = data.key || ""
        identifyDialog.titleText = data.title || ""
        identifyDialog.kind = data.kind || ""
        identifyDialog.embeddedIdentity = identifyDialog.kind === "book"
            ? root.bookEmbeddedIdentity(identifyDialog.groupKey, data.title) : ({})
        identifyDialog.feedback = ""
        identifyDialog.open()
    }
    function requestProgressiveFilmIdentity(tile) {
        if (!tile || tile.identSource !== "IMDB" || !tile.identityId) return
        var imdbId = String(tile.identityId).replace(/^imdb:/, "")
        function applyMeta(meta) {
            if (!meta) return
            var synopsis = String(meta.description || meta.overview || meta.plot || "")
            var poster = TheatreApi.normalizeArtUrl(meta.poster || meta.cover || "")
            if (typeof VaultLibrary !== "undefined")
                VaultLibrary.enrichIdentity(tile.key || "", synopsis, poster)
            var facts = root.folderDetailFacts || ({})
            facts.synopsis = synopsis
            facts.synopsisSource = synopsis.length ? "Cinemeta" : (facts.synopsisSource || "IMDB")
            facts.coverUrl = poster || facts.coverUrl || ""
            root.folderDetailFacts = facts
            if (folderLayer.item) {
                folderLayer.item.synopsis = facts.synopsis || ""
                folderLayer.item.synopsisSource = facts.synopsisSource || ""
                if (poster) folderLayer.item.coverUrl = poster
            }
        }
        TheatreApi.loadMeta("movie", imdbId, function(meta) {
            if (meta) applyMeta(meta)
            else TheatreApi.loadMeta("series", imdbId, applyMeta)
        })
    }

    // ---- Slice 14: the Vault Continue rail — the app's own local reads/watches, resumable. Live
    //      from Progress.recent filtered to vault: ids (catalogue recents keep their own rails, §9).
    //      Re-derives on Progress.revision (a lifecycle write), never the silent 5s video tick. ----
    readonly property var continueItems: (root.populated && typeof Progress !== "undefined")
        ? VaultApi.continueRail(Progress, (Progress.revision, 18), root.admissionById)
        : []

    // ---- Slice 13: the folder detail overlay. Vault-local — the shelves stay instantiated
    //      underneath (hidden), so their scroll position survives open → Back for free. A row
    //      snapshot is seeded on open (not re-queried while a background scan runs). ----
    property bool folderDetailOpen: false
    property var folderDetailFacts: ({})
    // The static index snapshot (files as they sit on disk); seeded on open, NOT re-queried while a
    // background scan runs — the S13 snapshot discipline. The live read-state join happens below.
    property var folderDetailBaseRows: []
    // Rows the folder view actually renders: the index snapshot joined against live Progress so the
    // read tick, gold hairline, and last-read sort reflect real reads. Re-joins on Progress.revision
    // — a lifecycle write (open/close/minimize) — so a comic read then Back updates the tick; it does
    // NOT re-join on the silent 5s video tick (recordSilent bumps no revision), so the join can never
    // reintroduce the Continue-repaint stutter cascade (Preflight's reactivity hazard).
    readonly property var folderDetailRows: (root.folderDetailOpen && typeof Progress !== "undefined")
        ? VaultApi.joinRows(Progress, (Progress.revision, root.folderDetailBaseRows))
        : root.folderDetailBaseRows
    function openFolder(tile) {
        if (!tile) return
        root.folderDetailFacts = tile
        root.folderDetailBaseRows = (typeof VaultLibrary !== "undefined")
            ? VaultLibrary.items(tile.kind, tile.key) : []
        root.folderDetailOpen = true
        root.requestProgressiveFilmIdentity(tile)
    }
    function closeFolder() { root.folderDetailOpen = false }
    // Push the re-joined rows into the live folder view when Progress changes under it (e.g. after a
    // read while the folder view sits occluded beneath the reader). onLoaded seeds the first model.
    onFolderDetailRowsChanged: if (folderLayer.item) folderLayer.item.model = root.folderDetailRows

    // Shared shelf tile: a real comic cover when the row carries one (image://comiccover), else the
    // honest kind-icon on a gradient (book/video art is a later slice). Reused by every shelf + Folders.
    Component {
        id: vaultTileLegacyComp
        Column {
            id: tile
            required property var modelData
            objectName: "vaultTile_" + (modelData.key || "")
            readonly property bool away: Number(modelData.awayCount || 0) > 0
            readonly property bool hasErrors: Number(modelData.errorCount || 0) > 0
            spacing: 8
            Rectangle {
                id: coverBox
                width: 150; height: 208; radius: 12; clip: true
                border.width: 1; border.color: theme.edge
                opacity: tile.away ? 0.48 : 1.0
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0.16, 0.14, 0.20, 1) }
                    GradientStop { position: 1.0; color: Qt.rgba(0.055, 0.060, 0.090, 1) }
                }
                Image { // real cover art (comics after enrichment) — filling the whole tile
                    anchors.fill: parent
                    visible: !!modelData.coverUrl
                    source: modelData.coverUrl || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true; cache: true
                }
                Image { // honest kind icon when there is no cover yet (book/video, un-enriched comics)
                    anchors.centerIn: parent; width: 34; height: 34; opacity: 0.4
                    visible: !modelData.coverUrl
                    source: modelData.kind === "book" ? "../assets/icons/book-library.svg"
                          : modelData.kind === "video" ? "../assets/icons/projector-theatre.svg"
                          : "../assets/icons/comic-book.svg"
                    fillMode: Image.PreserveAspectFit
                }
                Rectangle {
                    anchors.fill: parent
                    visible: tile.away || tile.hasErrors
                    color: Qt.rgba(0.04, 0.04, 0.04, tile.away ? 0.54 : 0.38)
                    Text {
                        anchors.centerIn: parent
                        text: tile.away ? "Unavailable" : "Needs attention"
                        color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                    }
                }
                // kind badge, top-left
                Rectangle {
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 8
                    radius: 99; height: 20; width: badgeT.implicitWidth + 16
                    color: Qt.rgba(0, 0, 0, 0.62); border.width: 1; border.color: theme.edge
                    Text {
                        id: badgeT; anchors.centerIn: parent
                        text: modelData.kind === "comic" ? "COMIC" : modelData.kind === "book" ? "BOOK" : "VIDEO"
                        color: theme.gold; font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.6
                    }
                }
                // scrim so the overlaid title reads over any art
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 76
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.82) }
                    }
                }
                // title, overlaid at the foot of the cover
                Text {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.leftMargin: 9; anchors.rightMargin: 9; anchors.bottomMargin: 9
                    text: modelData.title || ""
                    color: "#f2f2f0"; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                    elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap
                    style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.9)
                }
                MouseArea {   // open the folder detail (Slice 13)
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: !tile.away
                    onClicked: root.openFolder(modelData)
                }
            }
            Text {
                text: (modelData.count || 0) + ((modelData.count === 1) ? " item" : " items")
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.4
            }
        }
    }

    // The extracted tile is the production delegate; the legacy component above remains inert as
    // a short-lived source reference while the shelf transition is review-gated.
    Component {
        id: vaultTileComp
        VaultTile {
            onFolderRequested: (data) => root.openFolder(data)
            onOpenRequested: (data) => root.openFolder(data)
            onRevealRequested: (data) => root.revealTile(data)
            onIdentifyRequested: (data) => root.identifyTile(data)
            onUnidentifyRequested: (data) => {
                if (typeof VaultLibrary !== "undefined" && data) VaultLibrary.unidentifyGroup(data.key || "")
            }
            onReshelveRequested: (kind, data) => {
                if (typeof VaultLibrary !== "undefined" && data) VaultLibrary.reshelveGroup(data.key || "", kind)
            }
            onHideRequested: (data) => {
                if (typeof VaultLibrary !== "undefined" && data) VaultLibrary.hideGroup(data.key || "")
            }
            onRestoreRequested: (data) => {
                if (typeof VaultLibrary !== "undefined" && data) VaultLibrary.restoreGroup(data.key || "")
            }
        }
    }

    // A Vault Continue tile: cover (or honest kind-icon on a gradient), title, a gold resume
    // hairline, and a click that reopens through the shared LocalLaunch path (openMediaRequested).
    // Shape from VaultApi.continueRail: { id, kind, path, title, cover, progressFraction }.
    Component {
        id: vaultContinueTileComp
        Column {
            required property var modelData
            spacing: 8
            Rectangle {
                width: 150; height: 208; radius: 12; clip: true
                border.width: 1; border.color: theme.edge
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0.16, 0.14, 0.20, 1) }
                    GradientStop { position: 1.0; color: Qt.rgba(0.055, 0.060, 0.090, 1) }
                }
                Image {
                    anchors.fill: parent
                    visible: !!modelData.cover
                    source: modelData.cover || ""
                    fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                }
                Image {
                    anchors.centerIn: parent; width: 34; height: 34; opacity: 0.4
                    visible: !modelData.cover
                    source: modelData.kind === "book" ? "../assets/icons/book-library.svg"
                          : modelData.kind === "video" ? "../assets/icons/projector-theatre.svg"
                          : "../assets/icons/comic-book.svg"
                    fillMode: Image.PreserveAspectFit
                }
                Rectangle {   // kind badge, top-left
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 8
                    radius: 99; height: 20; width: contBadgeT.implicitWidth + 16
                    color: Qt.rgba(0, 0, 0, 0.62); border.width: 1; border.color: theme.edge
                    Text {
                        id: contBadgeT; anchors.centerIn: parent
                        text: modelData.kind === "comic" ? "COMIC" : modelData.kind === "book" ? "BOOK" : "VIDEO"
                        color: theme.gold; font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.6
                    }
                }
                Rectangle {   // scrim behind the title
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 76
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.82) }
                    }
                }
                Text {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.leftMargin: 9; anchors.rightMargin: 9; anchors.bottomMargin: 12
                    text: modelData.title || ""
                    color: "#f2f2f0"; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                    elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap
                    style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.9)
                }
                Rectangle {   // gold resume hairline — the real read/watch position
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 3; color: Qt.rgba(1, 1, 1, 0.14)
                    Rectangle {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: parent.width * Math.max(0, Math.min(1, modelData.progressFraction || 0))
                        color: theme.gold
                    }
                }
                MouseArea {
                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: if (modelData.path) root.openMediaRequested(modelData.path)
                }
            }
            Text {
                text: modelData.kind === "comic" ? "Comic" : modelData.kind === "book" ? "Book" : "Video"
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.4
            }
        }
    }

    // On open (the vaultLayer Loader recreates this page each time), resume the founding card
    // for a folder added-but-never-confirmed. C++ dedups to once per app run (Slice 11 Thread D).
    Component.onCompleted: {
        if (typeof VaultLibrary !== "undefined") {
            VaultLibrary.offerUnconfirmedRoots()
            VaultLibrary.rescanDegradedRoots()   // Slice 15: watcher-failure fallback, silently
        }
        if (root.hasConfirmedStorage) root.initBrowseState()
    }
    // Leaving Vault (the taskbar door) deactivates vaultLayer's Loader, destroying this whole
    // page — a plain scroll (ui-scroll / a mouse drag) never runs through pushCrumb/goToCrumb/
    // selectRoot, so it is never remembered by those calls alone. This is the one place that
    // catches "the user just scrolled and then left" (design §4.8 session persistence).
    Component.onDestruction: root.rememberCurrentScroll()

    // swallow clicks so nothing behind this page receives them
    MouseArea { anchors.fill: parent }
    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- live shell wallpaper (the same backdrop sampling the other full pages use) ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03, 0.04, 0.07, 0.86) }
    }

    Flickable {
        id: page
        // The true "no storage configured at all" state ONLY (Slice 9: gated on
        // hasConfirmedStorage, not populated — see that property's own comment) — the Browse
        // face (Slice 5) is the sibling `browseFace` Item below, which needs its own
        // bounded-height layout for the grid's virtualization rather than living inside this
        // unbounded outer Flickable.
        visible: !root.folderDetailOpen && !root.hasConfirmedStorage
        enabled: !root.folderDetailOpen && !root.hasConfirmedStorage
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 150
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Column {
            id: col
            x: theme.margin
            width: root.width - theme.margin * 2
            topPadding: 14
            spacing: 0

            // ---- header (no-storage/scanning states only — the browse face leads with the carousel) ----
            Column {
                visible: !root.hasConfirmedStorage
                width: col.width
                spacing: 0
                Text { text: "ON THIS MACHINE"; color: theme.inkDimmer
                       font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
                Text { text: "Vault"; color: theme.ink; topPadding: 8
                       font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
                Item { width: 1; height: 20 }
                Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }
            }

            // ---- empty state: the dashed Add-folder drop surface (shown until any storage is confirmed) ----
            Item { visible: !root.hasConfirmedStorage; width: 1; height: 44 }

            Rectangle {
                id: dropSurface
                visible: !root.hasConfirmedStorage
                objectName: "vaultDropSurface"
                width: col.width
                height: 320
                radius: 20
                color: dropHover.containsDrag ? Qt.rgba(0.94, 0.77, 0.29, 0.06)
                                              : Qt.rgba(0.04, 0.045, 0.065, 0.42)

                // QML has no dashed Rectangle border, so paint one — brightens to gold on drag-over.
                Canvas {
                    id: dashes
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset()
                        ctx.strokeStyle = dropHover.containsDrag ? "rgba(240,196,74,0.85)" : "rgba(255,255,255,0.22)"
                        ctx.lineWidth = 1.6
                        ctx.setLineDash([9, 7])
                        ctx.strokeRect(1, 1, width - 2, height - 2)
                    }
                }
                Connections { target: dropHover; function onContainsDragChanged() { dashes.requestPaint() } }

                Column {
                    anchors.centerIn: parent
                    width: parent.width - 96
                    spacing: 16

                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 48; height: 48; opacity: 0.7
                        source: "../assets/icons/vault-folder.svg"
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        text: root.scanningEmpty ? "Looking through your folder…"
                                                 : "Add a folder of comics, books, or videos"
                        color: theme.ink
                        font.family: theme.ui; font.pixelSize: 18; font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        width: parent.width
                        wrapMode: Text.WordWrap
                        visible: !root.scanningEmpty
                        text: "The Vault reads what is already on this machine and keeps it here — nothing is downloaded or moved."
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 13; lineHeight: 1.3
                    }

                    // Add folder — opens the native folder picker (Slice 10). The ingest behind it
                    // (canonicalize · add as a Vault root · scan · shelve) lands in Slice 11.
                    Rectangle {
                        objectName: "vaultAddFolderButton"
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !root.scanningEmpty
                        width: addLabel.implicitWidth + 44; height: 44; radius: 12
                        color: addMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.9) : Qt.rgba(1, 1, 1, 0.08)
                        border.width: 1
                        border.color: addMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.6) : theme.edge
                        Text {
                            id: addLabel
                            anchors.centerIn: parent
                            text: "Add folder"
                            color: addMa.containsMouse ? "#141207" : theme.ink
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: addMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.addFolderRequested()
                        }
                    }
                }

                // A folder dropped on THIS Vault-specific surface is an Add-folder gesture (Slice 10
                // opens the picker path; ingest is Slice 11). This is NOT the app-wide file-open drop.
                DropArea {
                    id: dropHover
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: (drop) => {
                        drop.accepted = true
                        root.addFolderRequested()
                    }
                }
            }
        }
    }

    // ==== Slice 5: the assembled Browse face — carousel, collapsible rail, breadcrumb, grid.
    //      Bounded-height layout (not the outer page Flickable above): the grid needs its own
    //      viewport to virtualize (Gintama-scale: 367 episodes is real). ====
    Item {
        id: browseFace
        objectName: "vaultBrowseFace"
        visible: root.hasConfirmedStorage && !root.folderDetailOpen
        enabled: visible
        anchors.fill: parent
        focus: root.hasConfirmedStorage
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Backspace) {
                root.ascendBrowse()
                event.accepted = true
                return
            }
            // S14: "/" or Ctrl+F opens the in-vault search from anywhere in the browse face
            // (the grid's own key handling doesn't consume them, so they bubble here; a
            // focused text field consumes "/" itself, so the shortcut never re-triggers
            // while typing). Multi-select/bulk actions stay out of scope.
            if (!root.searchViewActive && !root.folderDetailOpen
                && ((event.key === Qt.Key_Slash && !(event.modifiers & ~Qt.KeypadModifier))
                    || ((event.modifiers & Qt.ControlModifier)
                        && event.key === Qt.Key_F))) {
                root.openSearch()
                event.accepted = true
            }
        }

        FeaturedCarousel {
            id: browseCarousel
            objectName: "vaultBrowseCarousel"
            anchors.top: parent.top; anchors.topMargin: 20
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
            // S14: the search results view replaces the browse face's level chrome — the
            // carousel and the Continue rail collapse (the rail's own empty-state pattern:
            // height follows visible) so the flat results grid owns the viewport.
            implicitHeight: root.searchViewActive ? 0 : 330
            visible: !root.searchViewActive
            slides: root.carouselSlides
            kicker: "Just arrived"
            primaryLabel: "Play"
            secondaryLabel: "Details"
            onPrimaryClicked: (idx) => {
                const s = root.carouselSlides[idx]
                if (s && s.__row) root.handleCarouselPrimary(s.__row)
            }
            // Slice 7: Details opens the detail sheet for a film slide (design decision #11).
            // A show/season slide has no sheet in this slice (series drill is Slice 8's
            // business) — Details stays a no-op for those, named honestly rather than faked.
            onSecondaryClicked: (idx) => {
                const s = root.carouselSlides[idx]
                if (s && s.__row && s.__row.nodeType === "film") root.openDetailSheet(s.__row)
            }
        }

        // ---- Vault ux uplift S6: the Continue rail — the mid-way locals (spec §4.5 places it
        //      here, between "Just arrived" and the grid). Data from root.continueItems
        //      (VaultApi.continueRail over the live Progress store: admitted vault videos
        //      with a resumable path only); tiles are the existing vaultContinueTileComp
        //      (cover, kind badge, gold resume hairline — written with Slice 14's rail, never
        //      instantiated until now). Entirely absent while there is nothing to resume —
        //      the carousel leads an empty Vault. ----
        Item {
            id: continueRail
            objectName: "vaultContinueRail"
            visible: root.continueItems.length > 0 && !root.searchViewActive
            anchors.top: browseCarousel.bottom; anchors.topMargin: 18
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
            height: visible ? 270 : 0

            Text {
                objectName: "vaultContinueRailKicker"
                anchors.top: parent.top; anchors.left: parent.left
                text: "CONTINUE"
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.5
                font.weight: Font.DemiBold
            }
            // PosterRail's own house shape: a clipped horizontal ListView, StopAtBounds,
            // no scrollbar of its own (drag to move; 18 tiles is the cap VaultApi is fed).
            ListView {
                id: continueList
                objectName: "vaultContinueRailList"
                anchors.top: parent.top; anchors.topMargin: 28
                anchors.left: parent.left; anchors.right: parent.right
                height: 242
                orientation: ListView.Horizontal
                spacing: 14
                clip: true
                cacheBuffer: width * 0.5
                boundsBehavior: Flickable.StopAtBounds
                model: root.continueItems
                delegate: vaultContinueTileComp
            }
        }

        Item {
            id: browseBody
            anchors.top: continueRail.bottom; anchors.topMargin: continueRail.visible ? 20 : 4
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom; anchors.bottomMargin: 24
            anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin

            VaultBrowseRail {
                id: browseRail
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                roots: root.browseRootsDetail
                expanded: browseSettings.railExpanded
                selectedRootPath: root.crumbStack.length ? root.crumbStack[0].key : ""
                hiddenActive: root.hiddenViewActive
                hiddenCount: root.hiddenSeriesRows.length
                // S9 (vault ux uplift): the marquee count + the synthetic downloads root's
                // quiet/last/remove treatment, all fed from the façade's own invokables.
                rootFolderCount: root.vaultRootCount
                downloadsRootPath: root.vaultDownloadsRootPath
                onRemoveDownloadsRequested: {
                    if (typeof VaultLibrary !== "undefined") VaultLibrary.removeDownloadsRoot()
                }
                // S10 (vault ux uplift): the row overflow menu's verbs + the needle editor,
                // all straight façade calls (QML paints, C++ decides — the union-republish
                // and never-delete laws live in VaultLibrary, not here).
                scanIgnore: (typeof VaultLibrary !== "undefined")
                            ? (VaultLibrary.revision, VaultLibrary.scanIgnore()) : []
                onRescanRequested: (path) => {
                    if (typeof VaultLibrary !== "undefined" && path) VaultLibrary.rescanRoot(path)
                }
                onForgetConfirmed: (path) => {
                    if (typeof VaultLibrary !== "undefined" && path) VaultLibrary.forgetRoot(path)
                }
                onScanIgnoreSaved: (needles) => {
                    if (typeof VaultLibrary !== "undefined") VaultLibrary.setScanIgnore(needles)
                }
                onRootSelected: (path) => {
                    let name = path
                    for (let i = 0; i < root.browseRootsDetail.length; ++i) {
                        if (root.browseRootsDetail[i].path === path) { name = root.browseRootsDetail[i].name; break }
                    }
                    root.selectRoot(path, name)
                }
                onHiddenRequested: root.openHidden()
                onAddRequested: root.addFolderRequested()
                onToggleRequested: browseSettings.railExpanded = !browseSettings.railExpanded
                // Slice 9 (design §4.9): Tab from the grid reaches the rail; Shift+Tab returns.
                // `grid` is declared further down in this same file — QML resolves ids
                // document-wide, so the forward reference is valid.
                KeyNavigation.backtab: grid
            }

            Item {
                id: mainArea
                anchors.left: browseRail.right; anchors.leftMargin: 24
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom

                VaultBrowseCrumb {
                    id: browseCrumb
                    anchors.top: parent.top; anchors.left: parent.left
                    // S12/S13: the sort + filter controls own the row's right edge now.
                    anchors.right: browseFilterControl.left; anchors.rightMargin: 14
                    visible: !root.searchViewActive
                    stack: root.displayedCrumbStack
                    onSegmentClicked: (index) => root.goToCrumb(index)
                }

                // ── Vault ux uplift S14 — the search field, replacing the crumb row while the
                //    search pseudo-level is up (live results; Enter opens the first hit; the
                //    window-level Escape chain leaves the view — a field-local Escape handler
                //    would never fire, the S2 Shortcut-precedence lesson). Hand-rolled like the
                //    rail's needle editor: Rectangle + TextInput, no Quick Controls styling. ──
                Item {
                    id: browseSearchField
                    objectName: "vaultBrowseSearchField"
                    visible: root.searchViewActive
                    anchors.top: parent.top; anchors.left: parent.left
                    anchors.right: browseFilterControl.left; anchors.rightMargin: 14
                    height: browseCrumb.height

                    Rectangle {
                        anchors.fill: parent
                        radius: 9
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.width: 1
                        border.color: searchInput.activeFocus ? theme.inkDimmer : theme.edge
                    }
                    TextInput {
                        id: searchInput
                        objectName: "vaultBrowseSearchInput"
                        anchors.fill: parent
                        anchors.leftMargin: 12; anchors.rightMargin: 12
                        clip: true
                        color: theme.ink
                        selectionColor: theme.gold
                        selectedTextColor: "#141207"
                        font.family: theme.ui; font.pixelSize: 13
                        verticalAlignment: TextInput.AlignVCenter
                        // live results — the plan's "≤3 keystrokes" law
                        onTextChanged: root.searchQuery = text
                        onAccepted: {
                            if (root.searchRowsJoined.length > 0)
                                root.handleBrowseCardOpen(root.searchRowsJoined[0])
                        }
                    }
                    Text {
                        visible: searchInput.length === 0
                        anchors.fill: parent
                        anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: Text.AlignVCenter
                        text: "Search the Vault — titles, identities, filenames"
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 13
                        elide: Text.ElideRight
                        MouseArea { anchors.fill: parent; onClicked: searchInput.forceActiveFocus() }
                    }
                    Text {
                        objectName: "vaultBrowseSearchCount"
                        visible: root.searchQuery.length > 0
                        anchors.right: parent.right; anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.searchRowsJoined.length === 60 ? "60+"
                              : String(root.searchRowsJoined.length)
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 11
                    }
                }

                // ── Vault ux uplift S14 — the search open button (the "/" and Ctrl+F
                //    shortcuts' mouse twin): a quiet glyph left of the filter control. ──
                Item {
                    id: browseSearchOpenBtn
                    objectName: "vaultBrowseSearchOpen"
                    visible: !root.hiddenViewActive && !root.searchViewActive
                    anchors.top: parent.top
                    anchors.right: browseFilterControl.left; anchors.rightMargin: 16
                    width: searchOpenGlyph.implicitWidth + 6
                    height: browseCrumb.height
                    Text {
                        id: searchOpenGlyph
                        anchors.centerIn: parent
                        text: "⌕"
                        color: searchOpenMa.containsMouse ? theme.ink : theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 14
                    }
                    MouseArea {
                        id: searchOpenMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openSearch()
                    }
                }

                // ── Vault ux uplift S13 — the filter control: a quiet pill left of the sort
                //    control ("⧩ Filter", with a count when predicates are active), opening a
                //    chip panel. Hidden in the Hidden view (that shelf is never filtered) and
                //    in search (results carry their own newest-first order). ──
                Item {
                    id: browseFilterControl
                    objectName: "vaultBrowseFilterControl"
                    visible: !root.hiddenViewActive && !root.searchViewActive
                    anchors.top: parent.top
                    anchors.right: browseSortControl.left; anchors.rightMargin: 18
                    width: filterFaceRow.implicitWidth + 6
                    height: browseCrumb.height

                    Row {
                        id: filterFaceRow
                        anchors.centerIn: parent
                        spacing: 7
                        Text {
                            text: "⧩"
                            color: root.filterMenuOpen || filterFaceMa.containsMouse ? theme.ink : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 13
                        }
                        Text {
                            objectName: "vaultBrowseFilterLabel"
                            text: root.activeFilterCount > 0
                                  ? ("Filter · " + root.activeFilterCount) : "Filter"
                            color: root.filterMenuOpen || filterFaceMa.containsMouse ? theme.ink : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 12
                        }
                    }
                    MouseArea {
                        id: filterFaceMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleFilterMenu()
                    }
                }

                // ── Vault ux uplift S12 — the sort control: a quiet label button right of the
                //    breadcrumb ("⇅ Newest arrival"), natural order by default. Clicking opens
                //    the vocabulary menu (hand-rolled like every house popup). Hidden in the
                //    Hidden view — that shelf keeps its own fixed order — and in search. ──
                Item {
                    id: browseSortControl
                    objectName: "vaultBrowseSortControl"
                    visible: !root.hiddenViewActive && !root.searchViewActive
                    anchors.top: parent.top; anchors.right: parent.right
                    width: sortFaceRow.implicitWidth + 6
                    height: browseCrumb.height

                    Row {
                        id: sortFaceRow
                        anchors.centerIn: parent
                        spacing: 7
                        Text {
                            text: "⇅"
                            color: root.sortMenuOpen || sortFaceMa.containsMouse ? theme.ink : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 13
                        }
                        Text {
                            objectName: "vaultBrowseSortLabel"
                            text: root.sortLabelOf(root.sortMode)
                            color: root.sortMenuOpen || sortFaceMa.containsMouse ? theme.ink : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 12
                        }
                    }
                    MouseArea {
                        id: sortFaceMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleSortMenu()
                    }
                }

                // ── Vault ux uplift S17 — the NEXT UP row on the show page: the first
                //    unwatched episode of the derived season sequence, one tile (the Continue
                //    rail's own tile component — it degrades to a plain open tile when a
                //    progress hairline has nothing to paint). Finished show → absent.
                Item {
                    id: showNextUp
                    objectName: "vaultShowNextUp"
                    visible: root.showPageActive && root.showNextUpRows.length > 0
                    anchors.top: browseCrumb.bottom; anchors.topMargin: 16
                    anchors.left: parent.left; anchors.right: parent.right
                    height: visible ? 158 : 0

                    Text {
                        objectName: "vaultShowNextUpKicker"
                        anchors.top: parent.top; anchors.left: parent.left
                        text: "NEXT UP"
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.5
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        id: nextUpList
                        anchors.top: parent.top; anchors.topMargin: 20
                        anchors.left: parent.left; anchors.right: parent.right
                        height: 126
                        orientation: ListView.Horizontal
                        spacing: 14
                        model: root.showNextUpRows
                        delegate: vaultContinueTileComp
                    }
                }

                Component {
                    id: posterDelegateComp
                    VaultPosterCard {
                        required property var modelData
                        row: modelData
                        onOpenRequested: (r) => root.handleBrowseCardOpen(r)
                        onIdentifyRequested: (r) => root.identifyBrowseRow(r)
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: (mouse) => { if (mouse.button === Qt.RightButton) root.openCardContextMenu(parent.row) }
                        }
                    }
                }
                Component {
                    id: wideDelegateComp
                    VaultWideCard {
                        required property var modelData
                        row: modelData
                        onOpenRequested: (r) => root.handleBrowseCardOpen(r)
                        onIdentifyRequested: (r) => root.identifyBrowseRow(r)
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: (mouse) => { if (mouse.button === Qt.RightButton) root.openCardContextMenu(parent.row) }
                        }
                    }
                }

                // Slice 6: the grid's one stable model — root.syncGridModel() is the only writer.
                // See the `browseGridRows`/`syncGridModel` block above for why a plain array
                // binding (Slice 5's original `model: root.browseGridRows`) can't stay key-stable.
                ListModel { id: gridModel }

                GridView {
                    id: grid
                    objectName: "vaultBrowseGrid"
                    anchors.top: browseCrumb.bottom
                    anchors.topMargin: root.showNextUp.visible ? (16 + root.showNextUp.height) : 16
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    clip: true
                    cellWidth: root.browseGridWide ? root.wideCellWidth : root.posterCellWidth
                    cellHeight: root.browseGridWide ? root.wideCellHeight : root.posterCellHeight
                    cacheBuffer: 900   // virtualization headroom at Gintama scale (367 episodes)
                    model: gridModel
                    ScrollBar.vertical: HouseScrollBar { flick: grid }
                    delegate: root.browseGridWide ? wideDelegateComp : posterDelegateComp

                    // ---- Slice 9: keyboard reach (design §4.9) — arrow keys move within the
                    // grid (GridView's own built-in key handling, matching model/visual order),
                    // Enter opens the keyboard-focused card, Backspace ascends (bubbles up to
                    // browseFace's own Keys.onPressed below — this item deliberately does not
                    // accept it), Tab reaches the rail (KeyNavigation, browseRail's own
                    // activeFocusOnTab). A visible focus ring appears ONLY on keyboard focus
                    // (`grid.activeFocus`) — a mouse hover/click never sets it, since no card's
                    // MouseArea ever requests focus, keeping the ring and the hover play-glyph
                    // structurally independent, exactly as the design requires. ----
                    focus: true
                    activeFocusOnTab: true
                    KeyNavigation.tab: browseRail
                    onActiveFocusChanged: if (grid.activeFocus && grid.currentIndex < 0 && grid.count > 0)
                                              grid.currentIndex = 0
                    highlight: Rectangle {
                        color: "transparent"
                        radius: 8
                        border.width: 2
                        border.color: theme.inkDim
                        visible: grid.activeFocus
                    }
                    Keys.onReturnPressed: (event) => { root.openFocusedGridCard(); event.accepted = true }
                    Keys.onEnterPressed: (event) => { root.openFocusedGridCard(); event.accepted = true }

                    // ---- empty states (design §4.5/§9): distinct copy per cause, keyed off the
                    // C++ projection (VaultLibrary::browseEmptyCause) — this QML never infers the
                    // cause itself. "Nothing is hidden" is a separate, pre-existing state for the
                    // Hidden shelf's own browsing mode — not one of the design's four causes, so
                    // it keeps its own plain text rather than borrowing this component. ----
                    VaultBrowseEmpty {
                        id: gridEmptyState
                        objectName: "vaultBrowseGridEmpty"
                        anchors.fill: parent
                        visible: grid.count === 0 && !root.hiddenViewActive
                                 && !root.searchViewActive
                        cause: root.browseEmptyCause
                        itemsCount: root.browseEmptyAwayCount
                        onAddStorageRequested: root.addFolderRequested()
                        // S13: the filtered cause's own next step, in the component's own copy.
                        onClearFilterRequested: root.resetFilters()
                    }
                    // S14: the search view's own quiet empty — not one of the design's four
                    // causes (a miss is not a storage/away/filter problem), so it keeps a
                    // plain text like the Hidden shelf's own "Nothing is hidden."
                    Text {
                        visible: grid.count === 0 && root.searchViewActive
                                 && root.searchQuery.length > 0
                        anchors.centerIn: parent
                        text: "No matches in the Vault."
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 14
                    }
                    Text {
                        visible: grid.count === 0 && root.hiddenViewActive
                        anchors.centerIn: parent
                        text: "Nothing is hidden."
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 14
                    }
                }
            }
        }
    }

    // Card right-click context menu — Reveal in Explorer (always, when the row carries a real
    // path) plus the reachable identify/hide affordances the old shelves also exposed.
    // Slice 6: every MenuItem gets its own automation name — like the uncertain mark, this
    // menu had none, so the bridge could not drive Un-identify (or any other action here) at
    // all. One context menu instance for the whole grid (targets root.contextRow), so a
    // bare action name is enough — no per-key suffix needed.
    Menu {
        id: cardContextMenu
        objectName: "vaultBrowseContextMenu"
        MenuItem {
            objectName: "vaultBrowseContextReveal"
            text: "Reveal in Explorer"
            enabled: !!(root.contextRow && root.contextRow.path)
            onTriggered: if (typeof VaultLibrary !== "undefined" && root.contextRow)
                             VaultLibrary.revealInExplorer(root.contextRow.path)
        }
        MenuItem {
            objectName: "vaultBrowseContextIdentify"
            text: "Identify…"
            visible: !root.hiddenViewActive && !!(root.contextRow && root.contextRow.state === "uncertain")
            onTriggered: root.identifyBrowseRow(root.contextRow)
        }
        MenuItem {
            objectName: "vaultBrowseContextUnidentify"
            text: "Un-identify"
            visible: !root.hiddenViewActive && !!(root.contextRow && root.contextRow.state === "identified")
            onTriggered: if (typeof VaultLibrary !== "undefined" && root.contextRow)
                             VaultLibrary.unidentifyGroup(root.contextRow.key || "")
        }
        MenuItem {
            objectName: "vaultBrowseContextHide"
            text: "Hide"
            visible: !root.hiddenViewActive && !!root.contextRow
            onTriggered: if (typeof VaultLibrary !== "undefined" && root.contextRow)
                             VaultLibrary.hideGroup(root.contextRow.key || "")
        }
        MenuItem {
            objectName: "vaultBrowseContextRestore"
            text: "Restore"
            visible: root.hiddenViewActive && !!root.contextRow
            onTriggered: if (typeof VaultLibrary !== "undefined" && root.contextRow)
                             VaultLibrary.restoreGroup(root.contextRow.key || "")
        }
        // Vault ux uplift S7 — the watched verbs, single-item only (bulk/season marking waits
        // on Phase-4's multi-select ruling). Vault ids only: contextRowVaultId is "" for a
        // catalogue/container/away row, and the revision clock is named so a mark (or the S3
        // retire path) re-opens the pair live. "Mark unwatched" is the CLEAR verb — the record
        // may then auto-retire on its own; it never pins -1.
        MenuSeparator {
            visible: !root.hiddenViewActive && root.contextRowVaultId.length > 0
        }
        MenuItem {
            objectName: "vaultBrowseContextMarkWatched"
            text: "Mark watched"
            visible: !root.hiddenViewActive && root.contextRowVaultId.length > 0
                     && (typeof Progress !== "undefined")
                     && (Progress.revision, Progress.watchedMark(root.contextRowVaultId) !== 1)
            onTriggered: root.markContextRowWatched(true)
        }
        MenuItem {
            objectName: "vaultBrowseContextMarkUnwatched"
            text: "Mark unwatched"
            visible: !root.hiddenViewActive && root.contextRowVaultId.length > 0
                     && (typeof Progress !== "undefined")
                     && (Progress.revision, Progress.watchedMark(root.contextRowVaultId) === 1)
            onTriggered: root.markContextRowWatched(false)
        }
    }

    // ── Vault ux uplift S12 — the sort vocabulary menu (hand-rolled like every house popup:
    //    a click-away backing over the page plus one glass panel under the control). One
    //    instance for the whole grid; the current order carries the ✓. ──
    Item {
        objectName: "vaultBrowseSortMenu"
        visible: root.sortMenuOpen
        anchors.fill: parent
        z: 55

        MouseArea { anchors.fill: parent; onClicked: root.sortMenuOpen = false }

        Rectangle {
            x: root.sortMenuX; y: root.sortMenuY
            width: 204
            height: sortMenuColumn.implicitHeight + 16
            radius: 12
            color: Qt.rgba(0.055, 0.06, 0.09, 0.98)
            border.width: 1
            border.color: theme.edge

            Column {
                id: sortMenuColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                Repeater {
                    model: root.sortVocabulary
                    delegate: Item {
                        id: sortMenuItem
                        required property var modelData
                        objectName: "vaultBrowseSortItem_" + sortMenuItem.modelData.mode
                        width: parent.width; height: 32
                        Row {
                            anchors.left: parent.left; anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Text {
                                text: root.sortMode === sortMenuItem.modelData.mode ? "✓" : ""
                                color: theme.gold
                                font.family: theme.ui; font.pixelSize: 12
                                width: 12
                            }
                            Text {
                                text: sortMenuItem.modelData.label
                                color: sortItemMa.containsMouse ? theme.ink : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 13
                            }
                        }
                        MouseArea {
                            id: sortItemMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.browseSettings_setSort(sortMenuItem.modelData.mode)
                                root.sortMenuOpen = false
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Vault ux uplift S13 — the filter chip panel (the sort menu's own hand-rolled shape).
    //    Four predicate axes, one chip row each; an active chip is gold-texted; clicking it
    //    again clears the axis. "Clear all" at the bottom. Nothing persists — a level change
    //    resets everything (the reset law lives in onCurrentBrowsePathChanged). ──
    Item {
        objectName: "vaultBrowseFilterMenu"
        visible: root.filterMenuOpen
        anchors.fill: parent
        z: 55

        MouseArea { anchors.fill: parent; onClicked: root.filterMenuOpen = false }

        Rectangle {
            x: root.filterMenuX; y: root.filterMenuY
            width: 252
            height: filterMenuColumn.implicitHeight + 16
            radius: 12
            color: Qt.rgba(0.055, 0.06, 0.09, 0.98)
            border.width: 1
            border.color: theme.edge

            Column {
                id: filterMenuColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                component FilterChip : Item {
                    id: chip
                    property string label: ""
                    property bool active: false
                    signal picked()
                    width: chipText.implicitWidth + 20; height: 26
                    Rectangle {
                        anchors.fill: parent; radius: 13
                        color: chip.active ? Qt.rgba(0.94, 0.77, 0.29, 0.16)
                                           : (chipMa.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.035))
                        border.width: 1
                        border.color: chip.active ? Qt.rgba(0.94, 0.77, 0.29, 0.55) : theme.edge
                    }
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: chip.label
                        color: chip.active ? theme.gold : (chipMa.containsMouse ? theme.ink : theme.inkDim)
                        font.family: theme.ui; font.pixelSize: 12
                    }
                    MouseArea {
                        id: chipMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: chip.picked()
                    }
                }

                // Kind — the stored classification the identify gesture already trusts.
                Column {
                    width: parent.width; spacing: 6
                    Text { text: "KIND"; color: theme.inkDimmer; font.family: theme.ui
                           font.pixelSize: 10; font.letterSpacing: 1.4; font.weight: Font.DemiBold }
                    Row {
                        spacing: 6
                        FilterChip { label: "Video"; active: root.filterKind === "video"
                                     objectName: "vaultBrowseFilterKindVideo"
                                     onPicked: root.setFilterAxis("kind", "video") }
                        FilterChip { label: "Comics"; active: root.filterKind === "comic"
                                     objectName: "vaultBrowseFilterKindComic"
                                     onPicked: root.setFilterAxis("kind", "comic") }
                        FilterChip { label: "Books"; active: root.filterKind === "book"
                                     objectName: "vaultBrowseFilterKindBook"
                                     onPicked: root.setFilterAxis("kind", "book") }
                    }
                }
                // Watched — video-only, the Progress-mark half (QML-side predicate).
                Column {
                    width: parent.width; spacing: 6
                    Text { text: "WATCHED"; color: theme.inkDimmer; font.family: theme.ui
                           font.pixelSize: 10; font.letterSpacing: 1.4; font.weight: Font.DemiBold }
                    Row {
                        spacing: 6
                        FilterChip { label: "Unwatched"; active: root.filterWatched === "unwatched"
                                     objectName: "vaultBrowseFilterWatchedUnwatched"
                                     onPicked: root.setFilterAxis("watched", "unwatched") }
                        FilterChip { label: "Watched"; active: root.filterWatched === "watched"
                                     objectName: "vaultBrowseFilterWatchedWatched"
                                     onPicked: root.setFilterAxis("watched", "watched") }
                    }
                }
                // Identification — uncertain-only, the identify workflow's own lane.
                Column {
                    width: parent.width; spacing: 6
                    Text { text: "IDENTIFICATION"; color: theme.inkDimmer; font.family: theme.ui
                           font.pixelSize: 10; font.letterSpacing: 1.4; font.weight: Font.DemiBold }
                    Row {
                        spacing: 6
                        FilterChip { label: "Needs identifying"; active: root.filterIdent === "uncertain"
                                     objectName: "vaultBrowseFilterIdentUncertain"
                                     onPicked: root.setFilterAxis("ident", "uncertain") }
                    }
                }
                // Presence — here vs on other drives.
                Column {
                    width: parent.width; spacing: 6
                    Text { text: "PRESENCE"; color: theme.inkDimmer; font.family: theme.ui
                           font.pixelSize: 10; font.letterSpacing: 1.4; font.weight: Font.DemiBold }
                    Row {
                        spacing: 6
                        FilterChip { label: "Here"; active: root.filterPresence === "present"
                                     objectName: "vaultBrowseFilterPresencePresent"
                                     onPicked: root.setFilterAxis("presence", "present") }
                        FilterChip { label: "On other drives"; active: root.filterPresence === "away"
                                     objectName: "vaultBrowseFilterPresenceAway"
                                     onPicked: root.setFilterAxis("presence", "away") }
                    }
                }
                Rectangle { width: parent.width; height: 1; color: theme.edge; opacity: 0.6 }
                Item {
                    objectName: "vaultBrowseFilterClearAll"
                    width: parent.width; height: 30
                    visible: root.activeFilterCount > 0
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Clear all"
                        color: clearMa.containsMouse ? theme.ink : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 12
                    }
                    MouseArea {
                        id: clearMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.resetFilters()
                    }
                }
            }
        }
    }

    // ---- scan pill (Slice 11): a folder census is running; cancelable. Shows the folder name;
    //      the live "N of M" count fills in once the scanner emits per-file progress. ----
    Rectangle {
        id: scanPill
        objectName: "vaultScanPill"
        property bool scanning: (typeof VaultLibrary !== "undefined") ? VaultLibrary.scanning : false
        property int doneCount: (typeof VaultLibrary !== "undefined")
                                ? (VaultLibrary.scanProgressChanged, VaultLibrary.scanDone) : 0
        property int totalCount: (typeof VaultLibrary !== "undefined")
                                 ? (VaultLibrary.scanProgressChanged, VaultLibrary.scanTotal) : 0
        property string rootPath: (typeof VaultLibrary !== "undefined")
                                  ? (VaultLibrary.scanProgressChanged, VaultLibrary.scanningRoot) : ""
        visible: scanning && !root.folderDetailOpen
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 44
        width: pillRow.implicitWidth + 40
        height: 52
        radius: 26
        color: Qt.rgba(0.04, 0.045, 0.065, 0.94)
        border.width: 1
        border.color: theme.edge

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 14

            Item {
                width: 16; height: 16
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    id: spinner
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: Qt.rgba(1, 1, 1, 0.14)
                    Rectangle {
                        width: 4; height: 4; radius: 2; color: theme.gold
                        anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
                    }
                    RotationAnimator on rotation {
                        running: scanPill.scanning; from: 0; to: 360
                        duration: 900; loops: Animation.Infinite
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    var name = scanPill.rootPath.split(/[\\/]/).pop()
                    var base = "Scanning " + (name || "folder")
                    return (scanPill.totalCount > 0)
                        ? (base + " — " + scanPill.doneCount + " of " + scanPill.totalCount)
                        : (base + "…")
                }
                color: theme.ink
                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
            }

            Rectangle {
                objectName: "vaultScanCancel"
                width: cancelLabel.implicitWidth + 22; height: 30; radius: 15
                anchors.verticalCenter: parent.verticalCenter
                color: cancelMa.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.07)
                Text {
                    id: cancelLabel
                    anchors.centerIn: parent
                    text: "Cancel"
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 13
                }
                MouseArea {
                    id: cancelMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (typeof VaultLibrary !== "undefined") VaultLibrary.cancelScan()
                }
            }
        }
    }

    // The in-world tab bar (All · Comics · Books · Video · Folders · Hidden) is retired from the
    // populated face by Slice 5 — the rail + breadcrumb + grid replace it. `WorldTabBar` itself
    // is untouched (other pages still use it); `currentTab`/`tabModel` stay declared above
    // (dead but harmless) since `seriesFor`/`shelfKinds`/`allSeries` are also unused-but-kept.

    // ---- top chrome: minimize · fullscreen · power (same vocabulary as Settings/Downloads) ----
    // z above the folder overlay so the window controls stay usable inside the detail view.
    Item {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.rightMargin: theme.margin
        z: 60
        width: chromeRow.implicitWidth
        height: 30
        Row {
            id: chromeRow
            spacing: 22
            Text { text: "—"; color: mMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: mMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Text { text: "⛶"; color: fMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: fMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() } }
            Text { text: "⏻"; color: pMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: pMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }
    BackAction {
        variant: "capsule"; tip: "Back"
        visible: !root.folderDetailOpen   // the folder detail owns Back while it is up
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 21
        anchors.leftMargin: theme.margin - 10
        // The top-left Back steps UP one browse level first (or out of the hidden view), and only
        // leaves the Vault entirely once there is nowhere left to ascend — matching the crumb trail
        // and Backspace's own ascendBrowse(). Previously it always emitted backRequested() →
        // closeVaultPage(), so one click from any depth jumped straight out to the library.
        onTriggered: {
            if (root.hiddenViewActive || root.crumbStack.length > 1) root.ascendBrowse()
            else root.backRequested()
        }
    }

    // ── the founding-ceremony confirmation card: a modal over the Vault once a census yields a
    //    candidate. Seedable component; VaultPage wires it to the VaultLibrary façade. ──
    VaultConfirmCard {
        objectName: "vaultCard"
        anchors.fill: parent
        z: 30
        visible: ((typeof VaultLibrary !== "undefined") ? VaultLibrary.cardVisible : false) && !root.folderDetailOpen
        model: (typeof VaultLibrary !== "undefined") ? (VaultLibrary.candidateChanged, VaultLibrary.candidate) : []
        rootPath: (typeof VaultLibrary !== "undefined") ? VaultLibrary.candidateRoot : ""
        onShelveRequested: (ov) => { if (typeof VaultLibrary !== "undefined") VaultLibrary.confirmRoot(rootPath, ov) }
        onDismissRequested: { if (typeof VaultLibrary !== "undefined") VaultLibrary.dismissCard() }
    }

    // ── Slice 7: the detail sheet — a same-window overlay (never a Window/Popup; the Lanista
    //    bridge cannot see a secondary window). z above the browse face, below the window chrome
    //    so minimize/fullscreen/close stay reachable while the sheet is up. ──
    VaultDetailSheet {
        anchors.fill: parent
        z: 45
        visible: root.detailSheetVisible
        enabled: visible
        detail: root.detailSheetDetail
        identityStateOfRow: root.detailSheetRowState
        onBackRequested: root.closeDetailSheet()
        onPlayRequested: (path) => {
            root.closeDetailSheet()
            if (path) root.openMediaRequested(path)
        }
        onRevealRequested: (path) => {
            if (typeof VaultLibrary !== "undefined" && path) VaultLibrary.revealInExplorer(path)
        }
        onIdentifyRequested: (key) => {
            root.identifyBrowseRow({
                key: key,
                displayTitle: root.detailSheetDetail.displayTitle || "",
                kind: root.detailSheetRowKind
            })
        }
        onUnidentifyRequested: (key) => {
            if (typeof VaultLibrary !== "undefined" && key) VaultLibrary.unidentifyGroup(key)
        }
        // Vault ux uplift S8's one deferred wire (landed with S6, which owned this file): the
        // sheet's "Identify again" — a one-shot re-run of the conservative auto gate for this
        // group. VaultLibrary::identifyGroup adopts on a single catalogue match and durably
        // records the ambiguity when several remain, so the verb is one honest call.
        onIdentifyAgainRequested: (key) => {
            if (typeof VaultLibrary !== "undefined" && key) VaultLibrary.identifyGroup(key)
        }
        onHideRequested: (key) => {
            if (typeof VaultLibrary !== "undefined" && key) VaultLibrary.hideGroup(key)
            root.closeDetailSheet()
        }
        // Vault ux uplift S7 — the sheet's watched verbs. The sheet stays seedable (it never
        // calls Progress itself; see its own markWatchedRequested comment), so the Progress
        // calls are VaultPage's — the same owner discipline the identify-again handler above
        // follows. The mark is the durable store mark; "Mark unwatched" CLEARS it (-1 is only
        // set by explicitly marking unwatched again, which re-visibility handles).
        rowVaultId: root.detailSheetRowVaultId
        rowIsWatched: root.detailSheetRowIsWatched
        onMarkWatchedRequested: (vaultId, watched) => {
            if (typeof Progress === "undefined" || !vaultId) return
            if (watched) Progress.setWatchedMark(vaultId, true)
            else Progress.clearWatchedMark(vaultId)
        }
    }

    // ── Slice 13: the folder detail overlay (z above the shelves + card, below the window chrome).
    //    The shelves stay instantiated (hidden) underneath so Back returns to the same scroll spot. ──
    Loader {
        id: folderLayer
        anchors.fill: parent
        z: 40
        active: root.folderDetailOpen
        source: "VaultFolderView.qml"
        onLoaded: {
            item.backdrop = root.backdrop
            item.title = root.folderDetailFacts.title || ""
            item.kind = root.folderDetailFacts.kind || "comic"
            item.coverUrl = root.folderDetailFacts.coverUrl || ""
            item.rootPath = root.folderDetailFacts.subtreePath || ""
            item.identityId = root.folderDetailFacts.identityId || ""
            item.identitySource = root.folderDetailFacts.identSource || ""
            item.identityWorld = root.folderDetailFacts.identityWorld || ""
            item.synopsis = root.folderDetailFacts.synopsis || ""
            item.synopsisSource = root.folderDetailFacts.synopsisSource || ""
            item.model = root.folderDetailRows
            item.viewWorldRequested.connect(function(identity) { root.viewWorldRequested(identity) })
        }
    }
    Connections {
        target: folderLayer.item
        function onBackRequested() { root.closeFolder() }
        function onRevealRequested(path) {
            if (typeof VaultLibrary !== "undefined") VaultLibrary.revealInExplorer(path)
        }
        // Slice 14 (open half): a row click opens that file; the preview "Continue" door opens the
        // first file that already carries progress (the reader resumes itself at the saved page —
        // the Vault-side read tick / hairline / rail join is the seam-map half, not this one).
        function onOpenRequested(row) {
            if (row && row.path) root.openMediaRequested(row.path)
        }
        function onContinueRequested() {
            // Resume the file with the freshest real Progress; fall back to the first row only if
            // nothing carries progress (defensive — the door reads "Continue" only when some does).
            var rows = root.folderDetailRows || []
            var target = VaultApi.resumeTarget(rows)
            if (!target && rows.length) target = rows[0]
            if (target && target.path) root.openMediaRequested(target.path)
        }
    }

    VaultIdentifyDialog {
        id: identifyDialog
        anchors.centerIn: parent
        z: 80
        onIdentityChosen: (groupKey, identity) => {
            if (typeof VaultLibrary === "undefined") return
            if (VaultLibrary.identifyGroupWith(groupKey, identity)) {
                close()
            } else {
                feedback = "That identity could not be applied. The folder stays filename-honest."
            }
        }
    }

    // Slice 21: the same identity ceremony surface is used by Vault and the launch door;
    // VaultLibrary owns the durable relationship decision, not this presentation layer.
    VaultIdentityCeremonyDialog {
        id: identityCeremonyDialog
        anchors.centerIn: parent
        z: 90
        visible: root.identityCeremonies.length > 0 && !root.identityCeremonyDismissed
        ceremony: root.identityCeremonies.length ? root.identityCeremonies[0] : ({})
        onChoiceMade: (relationship, choice) => {
            if (typeof VaultLibrary !== "undefined"
                    && VaultLibrary.decideIdentityCeremony(relationship, choice)) {
                root.identityCeremonyDismissed = true
                close()
            }
        }
    }
    Connections {
        target: (typeof VaultLibrary !== "undefined") ? VaultLibrary : null
        function onIdentityCeremoniesChanged() { root.identityCeremonyDismissed = false }
    }
}
