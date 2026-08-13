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
    }

    property var crumbStack: []              // [{key, displayTitle}, ...] selected root -> current level
    property string currentBrowsePath: ""    // the current level's key (folder path / show sentinel)
    property bool hiddenViewActive: false    // the reversible Hidden shelf, not a folder level
    property var contextRow: null            // the row a card's right-click context menu targets

    // ==== Slice 7: the detail sheet — opening a Film row answers "what do I physically hold"
    //      instead of routing straight to Play (design decision #11). Episode/clip rows are
    //      unaffected: their Play routing is untouched (Slice 8's business, not this one's). ====
    property bool detailSheetVisible: false
    property string detailSheetKey: ""
    property string detailSheetRowState: "" // the grid row's own state at open time (Identify/Un-identify choice)
    readonly property var detailSheetDetail: (root.detailSheetVisible && root.detailSheetKey
            && typeof VaultLibrary !== "undefined")
        ? (VaultLibrary.revision, VaultLibrary.browseDetail(root.detailSheetKey)) : ({})
    function openDetailSheet(row) {
        root.detailSheetKey = row.key || ""
        root.detailSheetRowState = row.state || ""
        root.detailSheetVisible = true
    }
    function closeDetailSheet() { root.detailSheetVisible = false }

    readonly property var displayedCrumbStack: root.hiddenViewActive
        ? [{ key: "hidden:", displayTitle: "Hidden" }] : root.crumbStack

    readonly property var browseRootsDetail: (root.populated && typeof VaultLibrary !== "undefined")
        ? (VaultLibrary.revision, VaultLibrary.rootsDetail()) : []
    readonly property var hiddenSeriesRows: (root.populated && typeof VaultLibrary !== "undefined")
        ? (VaultLibrary.revision, VaultLibrary.hiddenSeries()) : []
    readonly property var carouselArrivalRows: (root.populated && typeof VaultLibrary !== "undefined")
        ? (VaultLibrary.revision, VaultLibrary.recentArrivals(6)) : []
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
            coverRef: s.coverUrl || ""
        }
    })

    readonly property var browseGridRows: {
        if (!root.populated) return []
        if (root.hiddenViewActive) return root.hiddenRowsAsBrowse
        if (typeof VaultLibrary === "undefined" || !root.currentBrowsePath) return []
        VaultLibrary.revision // dependency: re-project on every committed publish
        return VaultLibrary.browseAt(root.currentBrowsePath)
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
    property string gridSyncedLevelKey: " __unsynced__"
    function syncGridModel(rows) {
        rows = rows || []
        const levelKey = root.hiddenViewActive ? "hidden:" : root.currentBrowsePath
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
        const key = root.hiddenViewActive ? "hidden:" : root.currentBrowsePath
        if (key && typeof grid !== "undefined" && grid) VaultBrowseState.rememberScroll(key, grid.contentY)
    }
    function restoreGridScroll() {
        const key = root.hiddenViewActive ? "hidden:" : root.currentBrowsePath
        if (typeof grid !== "undefined" && grid) grid.contentY = VaultBrowseState.scrollFor(key)
    }
    function selectRoot(path, name) {
        root.rememberCurrentScroll()
        root.hiddenViewActive = false
        root.crumbStack = [{ key: path, displayTitle: name }]
        root.currentBrowsePath = path
        root.browseSettings_setLastCrumb()
    }
    function pushCrumb(key, title) {
        root.rememberCurrentScroll()
        root.crumbStack = root.crumbStack.concat([{ key: key, displayTitle: title }])
        root.currentBrowsePath = key
        root.browseSettings_setLastCrumb()
    }
    function goToCrumb(index) {
        if (index < 0 || index >= root.crumbStack.length - 1 || root.hiddenViewActive) return
        root.rememberCurrentScroll()
        root.crumbStack = root.crumbStack.slice(0, index + 1)
        root.currentBrowsePath = root.crumbStack[root.crumbStack.length - 1].key
        root.browseSettings_setLastCrumb()
    }
    function ascendBrowse() {
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
        root.hiddenViewActive = true
    }
    function handleBrowseCardOpen(row) {
        if (!row) return
        if (root.hiddenViewActive) {
            if (typeof VaultLibrary !== "undefined") VaultLibrary.restoreGroup(row.key || "")
            return
        }
        if (row.nodeType === "folder" || row.nodeType === "show" || row.nodeType === "season") {
            root.pushCrumb(row.key, row.displayTitle)
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
        // browseAt() rows don't carry a comic/book/video `kind` (Slice 1's row contract is
        // kind-agnostic) — the book-specific synopsis/cover pre-fill identifyTile() does for
        // the old shelves is skipped here; the identify flow itself is unaffected.
        identifyDialog.kind = ""
        identifyDialog.embeddedIdentity = ({})
        identifyDialog.feedback = ""
        identifyDialog.open()
    }
    function openCardContextMenu(row) {
        root.contextRow = row
        cardContextMenu.popup()
    }
    function initBrowseState() {
        if (!root.populated || typeof VaultLibrary === "undefined") return
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
    onPopulatedChanged: if (root.populated && root.crumbStack.length === 0) root.initBrowseState()

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
    function identifyTile(data) {
        if (!data) return
        identifyDialog.groupKey = data.key || ""
        identifyDialog.titleText = data.title || ""
        identifyDialog.kind = data.kind || ""
        identifyDialog.embeddedIdentity = ({})
        if (identifyDialog.kind === "book" && typeof VaultLibrary !== "undefined") {
            var bookRows = VaultLibrary.items("book", identifyDialog.groupKey) || []
            var book = bookRows.length ? bookRows[0] : {}
            if (book.title || book.displayTitle) {
                identifyDialog.embeddedIdentity = {
                    title: book.title || book.displayTitle || data.title,
                    sourceId: "epub:" + String(book.id || identifyDialog.groupKey),
                    synopsis: book.synopsis || "",
                    coverUrl: book.coverUrl || "",
                    year: Number(book.year || 0)
                }
            }
        }
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
        if (root.populated) root.initBrowseState()
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
        // The unpopulated (no-roots) empty state ONLY — the populated Browse face (Slice 5) is
        // the sibling `browseFace` Item below, which needs its own bounded-height layout for the
        // grid's virtualization rather than living inside this unbounded outer Flickable.
        visible: !root.folderDetailOpen && !root.populated
        enabled: !root.folderDetailOpen && !root.populated
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

            // ---- header (empty/scanning states only — populated leads with the marquee panel) ----
            Column {
                visible: !root.populated
                width: col.width
                spacing: 0
                Text { text: "ON THIS MACHINE"; color: theme.inkDimmer
                       font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
                Text { text: "Vault"; color: theme.ink; topPadding: 8
                       font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
                Item { width: 1; height: 20 }
                Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }
            }

            // ---- empty state: the dashed Add-folder drop surface (shown until the Vault has content) ----
            Item { visible: !root.populated; width: 1; height: 44 }

            Rectangle {
                id: dropSurface
                visible: !root.populated
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
        visible: root.populated && !root.folderDetailOpen
        enabled: visible
        anchors.fill: parent
        focus: root.populated
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Backspace) {
                root.ascendBrowse()
                event.accepted = true
            }
        }

        FeaturedCarousel {
            id: browseCarousel
            objectName: "vaultBrowseCarousel"
            anchors.top: parent.top; anchors.topMargin: 20
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
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

        Item {
            id: browseBody
            anchors.top: browseCarousel.bottom; anchors.topMargin: 22
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
            }

            Item {
                id: mainArea
                anchors.left: browseRail.right; anchors.leftMargin: 24
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom

                VaultBrowseCrumb {
                    id: browseCrumb
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    stack: root.displayedCrumbStack
                    onSegmentClicked: (index) => root.goToCrumb(index)
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
                    anchors.top: browseCrumb.bottom; anchors.topMargin: 16
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    clip: true
                    cellWidth: root.browseGridWide ? root.wideCellWidth : root.posterCellWidth
                    cellHeight: root.browseGridWide ? root.wideCellHeight : root.posterCellHeight
                    cacheBuffer: 900   // virtualization headroom at Gintama scale (367 episodes)
                    model: gridModel
                    ScrollBar.vertical: HouseScrollBar { flick: grid }
                    delegate: root.browseGridWide ? wideDelegateComp : posterDelegateComp

                    // ---- empty states (design §4.5/§9): distinct copy per cause; Slice 9 restyles ----
                    Text {
                        visible: grid.count === 0 && !root.hiddenViewActive
                        anchors.centerIn: parent
                        text: "This folder is empty."
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
        onTriggered: root.backRequested()
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
            root.identifyBrowseRow({ key: key, displayTitle: root.detailSheetDetail.displayTitle || "" })
        }
        onUnidentifyRequested: (key) => {
            if (typeof VaultLibrary !== "undefined" && key) VaultLibrary.unidentifyGroup(key)
        }
        onHideRequested: (key) => {
            if (typeof VaultLibrary !== "undefined" && key) VaultLibrary.hideGroup(key)
            root.closeDetailSheet()
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
