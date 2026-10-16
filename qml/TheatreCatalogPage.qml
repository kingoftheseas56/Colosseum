// TheatreCatalogPage — one deep catalogue tab (Movies · Shows · Anime). Begins with Top 10 and
// ends with the genre mosaic; between them sit the house shelves, the recognized service rows
// merged into their contextual slots, and a "From Your Extensions" section. NO hero, Continue,
// Next Up, award, or blurb — those belong to the Theatre landing page. Rows page the keyless
// TheatreApi, filter through ExplicitContentPolicy before rendering, and honour per-tab
// customization (move / hide / rename / reset). Stale-generation callbacks are ignored.
import QtQuick
import "CatalogDelivery.js" as CatalogDelivery
import "TheatreApi.js" as TheatreApi
import "TheatreGenreApi.js" as TheatreGenreApi
import "TheatreCatalogRules.js" as Rules
import "ExplicitContentPolicy.js" as ExplicitPolicy

pragma ComponentBehavior: Bound

Column {
    id: page

    property string pageKey: "movies"
    // injected wiring: the one global content preference, the bundled MAL catalogue, the per-tab
    // row-preference store, and (test-only) a catalog-loader override.
    property var contentPreferences: null
    property var malCatalog: null
    property var imdbCatalog: null
    property var rowPreferences: null
    property var catalogLoader: null
    property Flickable pageFlick: null
    property Item backdrop: null
    property var parkRowHandler: null
    property bool editMode: false
    // TheatreWorld retains this page for fast tab switches, but catalogue transport should wait
    // until the world/tab is active.
    property bool active: true
    property bool _ready: false
    property bool _loadedOnce: false
    property bool _hasSettled: false
    property var _loadCancel: null
    property var _factsController: null

    property var rawRows: []
    onRawRowsChanged: {
        page._scheduleFactsDemand()
        if (typeof CatalogueTraceEnabled !== "undefined" && CatalogueTraceEnabled) {
            var catalog=page.pageKey === "anime" ? page.malCatalog : page.imdbCatalog
            var state=catalog && typeof catalog.landingState === "function" ? catalog.landingState(page.pageKey) : ({})
            console.info("CATALOGUE_TRACE " + JSON.stringify({event:"qml_model_updated",epochMs:Date.now(),
                component:"TheatreCatalogPage",dataset:page.pageKey === "anime" ? "mal" : "imdb",
                method:"landing",queryKey:state.key || "",tab:page.pageKey,generation:page.generation,
                shelves:rawRows.length,rows:rawRows.reduce(function(n,r){return n+(r.items||[]).length},0)}))
        }
    }
    property bool loading: false
    property string errorText: ""
    property int generation: 0
    property int prefsRev: 0

    // ── viewport seam for lazy shelf residency (TheatreWorld converts board coords → page-local) ──
    property real viewportTop: 0
    property real viewportHeight: 0
    onViewportTopChanged: page._scheduleFactsDemand()
    onViewportHeightChanged: page._scheduleFactsDemand()
    property string visualProfile: "classic"

    Timer {
        id: factsDemandTimer
        interval: 0; repeat: false
        onTriggered: page._updateFactsDemand()
    }
    function _scheduleFactsDemand() {
        if (page.active && page._factsController) factsDemandTimer.restart()
    }
    function _updateFactsDemand() {
        var controller = page._factsController
        if (!controller || !page.active || page.pageKey === "anime") return
        var keys = [], vh = page.viewportHeight
        for (var i = 0; i < mainShelfRepeater.count; ++i) {
            var d = mainShelfRepeater.itemAt(i), r = page.mainRows[i]
            if (!d || !r || r.liveFacts !== true) continue
            if (vh <= 0) { if (r.key === "top-10") keys.push(r.key); continue }
            var top = d.y, bottom = top + d.height
            // Match LazyPosterShelf's one-viewport activation and reserve the
            // two-viewport margin for demand without waking far-away rows.
            if (top <= page.viewportTop + 2 * vh &&
                bottom >= page.viewportTop - vh) keys.push(r.key)
        }
        controller.setVisible(keys)
    }
    function retryFacts() {
        return !!(page._factsController && page._factsController.retry())
    }

    signal itemRequested(var item)
    signal genreRequested(string kind, string name)
    signal genreIndexRequested(string kind)
    signal seeAllRequested(var pin)

    readonly property real contentLeft: rowIndex.contentLeft
    readonly property real _availableWidth: parent ? parent.width : 900
    x: page.contentLeft
    width: Math.max(0, page._availableWidth - page.contentLeft)
    // gallery uses the approved 46px shelf-to-shelf rhythm; the lazy host reserves no extra gap so
    // the effective distance is exactly the page spacing, never host-gap + page-gap.
    spacing: page.visualProfile === "gallery" ? 46 : 26

    // ── live PosterRail count, for deterministic harness inspection of lazy residency ──
    property int _liveRev: 0
    readonly property int liveShelfCount: { page._liveRev; return page._recountLive(); }
    function _recountLive() {
        var c = 0;
        for (var i = 0; i < mainShelfRepeater.count; i++) {
            var d = mainShelfRepeater.itemAt(i);
            if (d && d.shelfLoaded) c++;
        }
        for (var j = 0; j < extShelfRepeater.count; j++) {
            var e = extShelfRepeater.itemAt(j);
            if (e && e.railLoaded) c++;
        }
        return c;
    }
    // harness introspection: the shelf hosts and the extension-heading / genre-mosaic positions.
    function mainShelfAt(i) { var d = mainShelfRepeater.itemAt(i); return d ? d.shelfRef : null; }
    function extShelfAt(i) { return extShelfRepeater.itemAt(i); }
    readonly property real genreMosaicY: genreMosaic.y
    readonly property real extensionHeadingY: extHeading.visible ? extHeading.y : -1

    Theme { id: theme }
    TheatreRowPreferences { id: internalPrefs }
    readonly property var _prefsStore: page.rowPreferences ? page.rowPreferences : internalPrefs

    RowIndexSidebar {
        id: rowIndex
        parent: page.parent ? page.parent.parent : null
        z: 2
        x: page.mapToItem(parent, -page.contentLeft, 0).x
        pageFlick: page.pageFlick
        flowHost: page
        backdrop: page.backdrop
        worldName: "Theatre"
        automationPrefix: "theatreRowIndex"
        rows: page.rowIndexRows
        onRowRequested: (target) => { if (page.parkRowHandler) page.parkRowHandler(target) }
    }

    readonly property string mediaKind: pageKey === "movies" ? "movie"
                                       : pageKey === "shows" ? "series" : "anime"
    readonly property string genreBoxTitle: pageKey === "movies" ? "Movie Genres"
                                           : pageKey === "shows" ? "Show Genres" : "Anime Genres"
    readonly property bool showExplicit: page.contentPreferences ? page.contentPreferences.showExplicit === true : false

    // rows arrive flat; split house/service (customizable main list) from unknown extensions.
    readonly property var rawMainRows: {
        var out = []; for (var i = 0; i < rawRows.length; i++)
            if (rawRows[i].sourceKind !== "extension") out.push(rawRows[i]);
        return out;
    }
    readonly property var extensionRows: {
        var out = []; for (var i = 0; i < rawRows.length; i++)
            if (rawRows[i].sourceKind === "extension") out.push(rawRows[i]);
        return out;
    }
    readonly property bool hasExtensionSection: extensionRows.length > 0

    // customized main rows — re-derived when the store changes (prefsRev), the tab reloads, or
    // edit mode toggles (edit mode reveals hidden rows).
    readonly property var mainRows: {
        var _ = page.prefsRev;
        var prefs = page._prefsStore.valueFor(page.pageKey);
        return Rules.applyCustomization(rawMainRows, prefs, page.editMode);
    }
    readonly property var mainKeys: {
        var out = []; for (var i = 0; i < mainRows.length; i++) out.push(mainRows[i].key); return out;
    }

    // A keyed, disposable presentation mirror of mainRows (never a catalogue authority).
    // A JavaScript-array Repeater recreates every delegate when ANY row changes. Apply
    // only the affected model operation so other LazyPosterShelf instances keep their
    // focus, horizontal scroll position, and viewport residency across live-facts pushes.
    ListModel { id: mainPresentationModel; dynamicRoles: true }
    property var _presentedKeys: []
    property var _presentedSignatures: []
    onMainRowsChanged: page._syncMainPresentation()
    function _syncMainPresentation() {
        var traceSync = typeof CatalogueTraceEnabled !== "undefined" && CatalogueTraceEnabled
        var traceStart = traceSync ? Date.now() : 0
        var signatureMs = 0, insertMs = 0, updateMs = 0, moveMs = 0, removeMs = 0
        var inserted = 0, updated = 0, moved = 0, removed = 0
        var desired = page.mainRows || []
        var keys = page._presentedKeys.slice()
        var signatures = page._presentedSignatures.slice()
        var wanted = Object.create(null)
        for (var n = 0; n < desired.length; ++n) wanted[String(desired[n].key)] = true

        // Remove only rows no longer present, backward so the existing indices survive.
        for (var i = keys.length - 1; i >= 0; --i) {
            if (wanted[keys[i]]) continue
            var removeStart = traceSync ? Date.now() : 0
            mainPresentationModel.remove(i)
            if (traceSync) { removeMs += Date.now() - removeStart; removed++ }
            keys.splice(i, 1)
            signatures.splice(i, 1)
        }
        for (var j = 0; j < desired.length; ++j) {
            var row = desired[j], key = String(row.key)
            // Page rows are plain data descriptors, with validated facts coming only
            // from TheatreApi. The signature detects semantically identical new arrays.
            var signature
            var signatureStart = traceSync ? Date.now() : 0
            try { signature = JSON.stringify(row) }
            catch (error) { signature = null } // unsupported payload: update, never hide a change
            if (traceSync) signatureMs += Date.now() - signatureStart
            var at = keys.indexOf(key, j)
            if (at < 0) {
                var insertStart = traceSync ? Date.now() : 0
                mainPresentationModel.insert(j, { rowKey: key, rowData: row })
                if (traceSync) { insertMs += Date.now() - insertStart; inserted++ }
                keys.splice(j, 0, key)
                signatures.splice(j, 0, signature)
                continue
            }
            if (at !== j) {
                var moveStart = traceSync ? Date.now() : 0
                mainPresentationModel.move(at, j, 1)
                if (traceSync) { moveMs += Date.now() - moveStart; moved++ }
                keys.splice(at, 1)
                keys.splice(j, 0, key)
                var movedSignature = signatures.splice(at, 1)[0]
                signatures.splice(j, 0, movedSignature)
            }
            if (signature === null || signature === undefined || signatures[j] !== signature) {
                var updateStart = traceSync ? Date.now() : 0
                mainPresentationModel.setProperty(j, "rowData", row)
                if (traceSync) { updateMs += Date.now() - updateStart; updated++ }
                signatures[j] = signature
            }
        }
        page._presentedKeys = keys
        page._presentedSignatures = signatures
        if (traceSync) {
            console.info("CATALOGUE_TRACE " + JSON.stringify({event: "qml_presentation_sync",
                component: "TheatreCatalogPage", epochMs: Date.now(), tab: page.pageKey,
                generation: page.generation, durationMs: Date.now() - traceStart,
                signatureMs: signatureMs, insertMs: insertMs, updateMs: updateMs,
                moveMs: moveMs, removeMs: removeMs, inserted: inserted, updated: updated,
                moved: moved, removed: removed, shelves: desired.length,
                viewportHeight: page.viewportHeight, viewportTop: page.viewportTop,
                pageWidth: page.width, active: page.active}))
        }
    }

    readonly property var rowIndexRows: {
        page.mainRows
        page.extensionRows
        mainShelfRepeater.count
        extShelfRepeater.count
        var out = []
        if (loadingSkeleton.visible)
            out.push({ key: "loading", title: "Loading", target: loadingSkeleton })
        for (var i = 0; i < mainShelfRepeater.count; i++) {
            var main = mainShelfRepeater.itemAt(i)
            var mainRow = page.mainRows[i]
            if (main && main.visible && mainRow && mainRow.hidden !== true)
                out.push({ key: mainRow.key, title: mainRow.title, target: main })
        }
        for (var j = 0; j < extShelfRepeater.count; j++) {
            var extension = extShelfRepeater.itemAt(j)
            var extensionRow = page.extensionRows[j]
            if (extension && extension.visible && extensionRow)
                out.push({ key: extensionRow.key || ("extension-" + j),
                           title: extensionRow.title, target: extension })
        }
        if (genreMosaic.visible)
            out.push({ key: "genres", title: page.genreBoxTitle, target: genreMosaic })
        return out
    }

    // the source-aware explicit gate, applied by the API before ranking AND See-all paging.
    property var _explicitFilter: function(item, showExplicit) {
        return ExplicitPolicy.visible("theatre", item, showExplicit);
    }

    readonly property var genreTiles: TheatreGenreApi.mosaicGenres(mediaKind)
    readonly property var coverPool: {
        var out = [];
        for (var i = 0; i < mainRows.length; i++) {
            var items = mainRows[i].items || [];
            for (var j = 0; j < items.length; j++) if (items[j].cover) out.push(items[j].cover);
        }
        return out;
    }

    Connections {
        target: page._prefsStore
        function onChanged(pk) { if (pk === page.pageKey) page.prefsRev += 1; }
    }
    Connections {
        target: page.contentPreferences
        ignoreUnknownSignals: true
        function onChanged() { if (page.active) page.load(); }
    }

    property bool _remoteReloadPending: false
    function remoteCatalogChanged() {
        page._remoteReloadPending = true;
        if (page.active) remoteCatalogueTimer.restart();
    }
    Timer {
        id: remoteCatalogueTimer
        interval: 75
        onTriggered: {
            var catalog = page.pageKey === "anime" ? page.malCatalog : page.imdbCatalog;
            if (catalog && catalog.remotePending && catalog.remotePending()) return;
            page._remoteReloadPending = false;
            page.load();
        }
    }
    Connections {
        target: page.imdbCatalog
        ignoreUnknownSignals: true
        // Only a real owner readiness/source switch warrants a new generation.
        function onReadyChanged() { if (page.pageKey !== "anime" && page.active) page.load(); }
    }
    Connections {
        target: page.malCatalog
        ignoreUnknownSignals: true
        function onRevisionChanged() { if (page.pageKey === "anime") page.remoteCatalogChanged(); }
        function onRequestFailed() { if (page.pageKey === "anime") page.remoteCatalogChanged(); }
    }
    onPageKeyChanged: if (page.active) load()
    Component.onCompleted: {
        page._ready = true
        if (page.active) page.load()
    }
    Component.onDestruction: page.cancelLoad()
    onActiveChanged: {
        if (!page.active) {
            // The native cache persists, but this tab's ephemeral demand
            // subscription is canceled and must be recreated on return.
            if (page._factsController) page._hasSettled = false
            page.cancelLoad()
            page.generation += 1
            page.loading = false
            return
        }
        if (page._ready && (!page._hasSettled || page._remoteReloadPending)) { page._remoteReloadPending = false; page.load(); }
    }

    function _push(payload) {
        if (!payload || payload.generation !== page.generation) return;   // stale-generation fence
        if (payload.loading !== true)
            page._hasSettled = true;
        // Keep the cancellation handle after index settlement: active live
        // facts may finish later, and page leave must still unsubscribe them.
        page.loading = payload.loading === true;
        page.rawRows = payload.rows || [];
        page.errorText = payload.error || "";
    }
    function load() {
        if (!page.active) return
        page.cancelLoad()
        page._factsController = null
        page._loadedOnce = true
        page._hasSettled = false
        page.generation += 1;
        page.loading = true;
        page.errorText = "";
        page.rawRows = [];
        var loader = page.catalogLoader ? page.catalogLoader : TheatreApi.loadCatalogPage;
        var gen = page.generation;
        var handle = loader(page.pageKey, { malCatalog: page.malCatalog, imdbCatalog: page.imdbCatalog,
                                            showExplicit: page.showExplicit,
                                            generation: gen, explicitFilter: page._explicitFilter,
                                            nowMs: Date.now(),
                                            onFactsController: function(controller) {
                                                if (!page.active || gen !== page.generation) {
                                                    controller.cancel(); return;
                                                }
                                                page._factsController = controller;
                                                page._scheduleFactsDemand();
                                            } }, page._push);
        if (page.active && page.generation === gen)
            page._loadCancel = (typeof handle === "function") ? handle : null;
        else if (typeof handle === "function")
            handle();
    }
    function cancelLoad() {
        factsDemandTimer.stop()
        var handle = page._loadCancel
        page._loadCancel = null
        if (typeof handle === "function") handle()
        if (page._factsController) page._factsController.cancel()
        page._factsController = null
    }
    function isRenamed(key) {
        var _ = page.prefsRev;
        return page._prefsStore.valueFor(page.pageKey).renamed[key] !== undefined;
    }

    // ── quiet Customize control (never consumes a content row) ──
    Item {
        width: parent.width
        height: 24
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: page.editMode ? "Done" : "Customize rows"
            color: custMa.containsMouse ? theme.ink : theme.inkDim
            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
            MouseArea {
                id: custMa; anchors.fill: parent; anchors.margins: -8
                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.editMode = !page.editMode
            }
        }
        KeyboardAction {
            id: customizeKeyboard
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 130; height: 28; pointerEnabled: false
            accessibleName: page.editMode ? "Done customizing rows" : "Customize rows"
            onTriggered: page.editMode = !page.editMode
        }
    }

    // ── loading skeleton (only before the first rows land) — gallery poster width/gap ──
    Item {
        id: loadingSkeleton
        visible: page.loading && page.mainRows.length === 0
        width: parent.width; height: page.visualProfile === "gallery" ? 267 : 236
        Row {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            spacing: page.visualProfile === "gallery" ? 20 : 18
            Repeater {
                model: 7
                Rectangle {
                    width: page.visualProfile === "gallery" ? 148 : 132
                    height: page.visualProfile === "gallery" ? 222 : 196
                    radius: 12
                    color: Qt.rgba(1, 1, 1, 0.08); border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.10); opacity: 0.65
                }
            }
        }
    }

    Text {
        visible: !page.loading && page.mainRows.length === 0 && page.extensionRows.length === 0
        text: page.errorText.length ? page.errorText : "Nothing loaded here yet."
        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
    }

    // ── the main shelves (Top 10 first, service rows in their contextual slots) ──
    // Row descriptors stay in this Repeater, but the live PosterRail is mounted lazily by
    // LazyPosterShelf only around the WorldPage viewport, so a long page's live rails plateau.
    Repeater {
        id: mainShelfRepeater
        model: mainPresentationModel
        delegate: Column {
            id: rowBlock
            required property var rowData
            required property int index
            width: page.width
            spacing: 8
            // harness/introspection: expose the lazy host and its live state.
            readonly property var shelfRef: rowShelf
            readonly property bool shelfLoaded: rowShelf.railLoaded

            TheatreRowControls {
                visible: page.editMode
                rowKey: rowBlock.rowData.key
                label: rowBlock.rowData.title
                hidden: rowBlock.rowData.hidden === true
                renamed: page.isRenamed(rowBlock.rowData.key)
                canMoveUp: rowBlock.index > 0
                canMoveDown: rowBlock.index < page.mainRows.length - 1
                onMoveUpRequested: page._prefsStore.move(page.pageKey, page.mainKeys, rowBlock.rowData.key, -1)
                onMoveDownRequested: page._prefsStore.move(page.pageKey, page.mainKeys, rowBlock.rowData.key, 1)
                onToggleHiddenRequested: page._prefsStore.toggleHidden(page.pageKey, rowBlock.rowData.key)
                onRenameRequested: (label) => page._prefsStore.rename(page.pageKey, rowBlock.rowData.key, label)
                onResetNameRequested: page._prefsStore.rename(page.pageKey, rowBlock.rowData.key, "")
            }
            LazyPosterShelf {
                id: rowShelf
                width: page.width
                row: rowBlock.rowData
                visualProfile: page.visualProfile
                // rowBlock.y is page-local; the shelf's own y is rowBlock-local, so pass the viewport
                // in rowBlock-local coordinates. (No per-frame mapToItem — a single subtraction.)
                viewportTop: page.viewportTop - rowBlock.y
                viewportHeight: page.viewportHeight
                editMode: page.editMode
                onItemRequested: (item) => page.itemRequested(item)
                onSeeAllRequested: (pin) => page.seeAllRequested(pin)
                onRailLoadedChanged: { page._liveRev += 1; page._scheduleFactsDemand() }
            }
            Item {
                // Pending live rows occupy the same shelf footprint; no poster
                // is shown until facts validate the candidate's eligibility.
                height: rowBlock.rowData.pendingFacts === true && !rowShelf._hasItems
                        ? (30 + rowShelf._m.headerGap + rowShelf._railListHeight) : 0
                width: 1
                visible: height > 0
            }
        }
    }

    // ── From Your Extensions (before the genre mosaic; hidden when empty) ──
    Text {
        id: extHeading
        visible: page.hasExtensionSection
        text: "From Your Extensions"
        color: theme.ink; font.family: theme.display; font.pixelSize: 18; font.weight: Font.DemiBold
    }
    Repeater {
        id: extShelfRepeater
        model: page.extensionRows
        delegate: LazyPosterShelf {
            required property var modelData
            width: page.width
            row: modelData
            visualProfile: page.visualProfile
            // the extension shelf is a direct child of this Column, so its y is already page-local.
            viewportTop: page.viewportTop
            viewportHeight: page.viewportHeight
            editMode: page.editMode
            onItemRequested: (item) => page.itemRequested(item)
            onSeeAllRequested: (pin) => page.seeAllRequested(pin)
            onRailLoadedChanged: page._liveRev += 1
        }
    }

    // ── genres last ──
    GenreMosaic {
        id: genreMosaic
        width: parent.width
        title: page.genreBoxTitle
        genres: page.genreTiles
        covers: page.coverPool
        onGenreClicked: (i) => page.genreRequested(page.mediaKind, page.genreTiles[i].name)
        onExploreClicked: page.genreIndexRequested(page.mediaKind)
    }
}
