// TheatreCatalogPage — one deep catalogue tab (Movies · Shows · Anime). Begins with Top 10 and
// ends with the genre mosaic; between them sit the house shelves, the recognized service rows
// merged into their contextual slots, and a "From Your Extensions" section. NO hero, Continue,
// Next Up, award, or blurb — those belong to the Theatre landing page. Rows page the keyless
// TheatreApi, filter through ExplicitContentPolicy before rendering, and honour per-tab
// customization (move / hide / rename / reset). Stale-generation callbacks are ignored.
import QtQuick
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
    property var rowPreferences: null
    property var catalogLoader: null
    property bool editMode: false

    property var rawRows: []
    property bool loading: false
    property string errorText: ""
    property int generation: 0
    property int prefsRev: 0

    // ── viewport seam for lazy shelf residency (TheatreWorld converts board coords → page-local) ──
    property real viewportTop: 0
    property real viewportHeight: 0
    property string visualProfile: "classic"

    signal itemRequested(var item)
    signal genreRequested(string kind, string name)
    signal genreIndexRequested(string kind)
    signal seeAllRequested(var pin)

    width: parent ? parent.width : 900
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
        function onChanged() { page.load(); }
    }

    onPageKeyChanged: load()
    Component.onCompleted: load()

    function _push(payload) {
        if (!payload || payload.generation !== page.generation) return;   // stale-generation fence
        page.loading = payload.loading === true;
        page.rawRows = payload.rows || [];
        page.errorText = payload.error || "";
    }
    function load() {
        page.generation += 1;
        page.loading = true;
        page.errorText = "";
        page.rawRows = [];
        var loader = page.catalogLoader ? page.catalogLoader : TheatreApi.loadCatalogPage;
        loader(page.pageKey, { malCatalog: page.malCatalog, showExplicit: page.showExplicit,
                               generation: page.generation, explicitFilter: page._explicitFilter,
                               nowMs: Date.now() }, page._push);
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
    }

    // ── loading skeleton (only before the first rows land) — gallery poster width/gap ──
    Item {
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
        model: page.mainRows
        delegate: Column {
            id: rowBlock
            required property var modelData
            required property int index
            width: page.width
            spacing: 8
            // harness/introspection: expose the lazy host and its live state.
            readonly property var shelfRef: rowShelf
            readonly property bool shelfLoaded: rowShelf.railLoaded

            TheatreRowControls {
                visible: page.editMode
                rowKey: rowBlock.modelData.key
                label: rowBlock.modelData.title
                hidden: rowBlock.modelData.hidden === true
                renamed: page.isRenamed(rowBlock.modelData.key)
                canMoveUp: rowBlock.index > 0
                canMoveDown: rowBlock.index < page.mainRows.length - 1
                onMoveUpRequested: page._prefsStore.move(page.pageKey, page.mainKeys, rowBlock.modelData.key, -1)
                onMoveDownRequested: page._prefsStore.move(page.pageKey, page.mainKeys, rowBlock.modelData.key, 1)
                onToggleHiddenRequested: page._prefsStore.toggleHidden(page.pageKey, rowBlock.modelData.key)
                onRenameRequested: (label) => page._prefsStore.rename(page.pageKey, rowBlock.modelData.key, label)
                onResetNameRequested: page._prefsStore.rename(page.pageKey, rowBlock.modelData.key, "")
            }
            LazyPosterShelf {
                id: rowShelf
                width: page.width
                row: rowBlock.modelData
                visualProfile: page.visualProfile
                // rowBlock.y is page-local; the shelf's own y is rowBlock-local, so pass the viewport
                // in rowBlock-local coordinates. (No per-frame mapToItem — a single subtraction.)
                viewportTop: page.viewportTop - rowBlock.y
                viewportHeight: page.viewportHeight
                editMode: page.editMode
                onItemRequested: (item) => page.itemRequested(item)
                onSeeAllRequested: (pin) => page.seeAllRequested(pin)
                onRailLoadedChanged: page._liveRev += 1
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
