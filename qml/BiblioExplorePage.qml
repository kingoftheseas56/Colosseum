// BiblioExplorePage — the deep Biblio "Explore" shelf page (plan
// `2026-08-01-biblio-discover-explore.md`, Task 7). Top 10 first, extension preview rows
// second, the four house rails (Popular/Top Rated/New Releases/Trending) next in fixed order,
// then the three fixed Fiction/Nonfiction/Audience mosaics LAST, always — the mosaics are never
// part of shelf customization (BiblioExploreRules never emits a mosaic key at all). Everything
// else (top-10 + extensions + house rails) is freely reorderable/hideable via
// BiblioExploreRules.applyCustomization + BiblioExplorePreferences, exactly like Task 6 proved.
//
// Injectable seams (mirrors this codebase's existing test-seam convention — BiblioCatalog's own
// IBiblioTransport, Task 5's injected catalogue/extension fakes): `catalogSource` stands in for
// the native `BiblioCatalog` context property, `extensionsSource` for the native `Extensions`
// context property, and `pageFetcher` for `DiscoverApi.loadPage` — an offscreen harness can
// construct this page with fakes for all three and never touch real native objects or the
// network. `preferences` is injectable the same way BiblioExplorePreferences itself supports an
// injected `settingsLocation`.
//
// "Top 10" reads BiblioCatalogStore's OWN documented equivalence (BiblioCatalogStore::top10's
// header comment: "rank-1..limit rows for the Popular catalogue") as `discoverPage("popular", ...,
// 0, top10Limit)` in ranked mode — a separate, smaller, numeral-badged slice of the exact same
// Popular ranking the "Popular" house rail also renders unranked at a larger count. Its See-All
// pin therefore resolves to the same Popular catalogue.
import QtQuick
import QtQuick.Controls
import "BiblioExploreRules.js" as Rules
import "DiscoverApi.js" as DiscoverApi
import "BiblioDiscoverApi.js" as BiblioDiscoverApi
import "ExplicitContentPolicy.js" as Policy

pragma ComponentBehavior: Bound

Item {
    id: page

    // ── injected world seams (production defaults; a harness overrides all three) ──
    property var catalogSource: (typeof BiblioCatalog !== "undefined") ? BiblioCatalog : null
    property var extensionsSource: (typeof Extensions !== "undefined") ? Extensions : null
    property var pageFetcher: null   // (catalog, selections, skip, done) -> default DiscoverApi.loadPage
    property var preferences: null   // default: an internal BiblioExplorePreferences instance

    property bool showExplicit: false
    property int limitPerShelf: 20
    property int top10Limit: 10
    property int mosaicLimit: 14
    property bool editMode: false

    // persists/restores the page's own scroll position across an Explore -> Discover -> Explore
    // tab switch within the same running session (Task 8's BiblioWorld reads/writes this) — a
    // plain property, NOT QSettings; it does not survive an app restart, by design.
    property real contentY: 0

    signal itemRequested(var work)
    signal discoverPinRequested(var pin)

    // ── fixed mosaic facet choices ──
    // BiblioTaxonomy's genre axis has curated sub-genre keys (science-fiction, fantasy, mystery,
    // thriller, romance, horror, historical-fiction, literary-fiction, nonfiction, biography) but
    // NO generic "fiction" key — so "literary-fiction" (the catch-all, non-genre-fiction bucket
    // real bookstores use) is the closest single controlled facet to a general Fiction concept.
    // "Nonfiction" is an exact match. "Audience" uses the audience axis's "young-adult" key — the
    // most distinct, standalone-shelf-worthy audience tier (vs. the plain "adult" readership tier).
    readonly property string fictionFacetAxis: "genre"
    readonly property string fictionFacetKey: "literary-fiction"
    readonly property string nonfictionFacetAxis: "genre"
    readonly property string nonfictionFacetKey: "nonfiction"
    readonly property string audienceFacetAxis: "audience"
    readonly property string audienceFacetKey: "young-adult"

    readonly property var mosaicSpecs: [
        { key: "mosaic-fiction", title: "Fiction", axis: page.fictionFacetAxis, facetKey: page.fictionFacetKey },
        { key: "mosaic-nonfiction", title: "Nonfiction", axis: page.nonfictionFacetAxis, facetKey: page.nonfictionFacetKey },
        { key: "mosaic-audience", title: "Audience", axis: page.audienceFacetAxis, facetKey: page.audienceFacetKey }
    ]

    readonly property var houseTitles: ({
        "popular": "Popular", "top-rated": "Top Rated",
        "new-releases": "New Releases", "trending": "Trending"
    })

    Theme { id: theme }
    BiblioExplorePreferences { id: _internalPrefs }
    readonly property var _prefs: page.preferences ? page.preferences : _internalPrefs

    property int prefsRev: 0
    Connections {
        target: page._prefs
        function onChanged() { page.prefsRev += 1 }
    }

    // ── extension book-catalogue population ("From Your Extensions"): enabled, input-free,
    // non-core "book"-typed addon catalogues — the identical population BiblioDiscoverApi.js
    // (Task 5) surfaces, derived independently here through the same shared DiscoverApi.js so
    // neither task depends on the other's in-progress file. ──
    readonly property var installedExtensions: page.extensionsSource ? page.extensionsSource.installed() : []
    // The filtered, single-source-of-truth extension catalogue list: BiblioDiscoverApi.js's
    // extensionCatalogs() (Task 5) already applies every exclusion "From Your Extensions" needs
    // (core rows, isDiscoverable === false acquisition-only catalogues, Apple Books by name,
    // required extras it can't auto-answer) — reuse it here instead of re-deriving a
    // similar-but-inconsistent filter from raw DiscoverApi.catalogsFor(). Each filtered entry's
    // `key` is the SAME composite string (transportUrl+"|book|"+catalogId) BiblioDiscoverApi's
    // resolvePin() matches a See-All pin against, so DiscoverApi.catalogByKey() re-resolves the
    // FULL fetchable catalog object (transportUrl/catalogId/extra) per key — the identical
    // pattern BiblioDiscoverApi.js's own fetchExtensionPage() uses internally.
    readonly property var bookExtensionCatalogs: {
        var filtered = BiblioDiscoverApi.extensionCatalogs(page.installedExtensions);
        var out = [];
        for (var i = 0; i < filtered.length; i++) {
            var full = DiscoverApi.catalogByKey(page.installedExtensions, "book", filtered[i].key);
            if (full) out.push(full);
        }
        return out;
    }
    readonly property var catalogByExtKey: {
        var m = {};
        for (var i = 0; i < page.bookExtensionCatalogs.length; i++)
            m[page.bookExtensionCatalogs[i].key] = page.bookExtensionCatalogs[i];
        return m;
    }
    readonly property var defaultRowKeys: {
        var list = [];
        for (var i = 0; i < page.bookExtensionCatalogs.length; i++)
            list.push({ id: page.bookExtensionCatalogs[i].key, title: page.bookExtensionCatalogs[i].title });
        return Rules.defaultRows(list);
    }

    // ── house data (native, synchronous once ready — its OWN loading state, independent of
    // extension rows' async loading) ──
    readonly property bool housesLoading: !page.catalogSource || page.catalogSource.ready !== true
    readonly property var houseRowsMap: {
        var out = {};
        if (!page.catalogSource || page.catalogSource.ready !== true) return out;
        var _dep = page.catalogSource.revision;
        var rows = page.catalogSource.exploreRows(page.limitPerShelf, page.showExplicit) || [];
        for (var i = 0; i < rows.length; i++)
            out[rows[i].catalogId] = page._normalizeHouseList(rows[i].items || []);
        return out;
    }
    readonly property var top10Items: {
        if (!page.catalogSource || page.catalogSource.ready !== true) return [];
        var _dep = page.catalogSource.revision;
        var p = page.catalogSource.discoverPage("popular", "", "", page.showExplicit, 0, page.top10Limit);
        return page._normalizeHouseList((p && p.items) ? p.items : []);
    }
    readonly property var mosaicItemsByKey: {
        var out = {};
        if (!page.catalogSource || page.catalogSource.ready !== true) return out;
        var _dep = page.catalogSource.revision;
        for (var i = 0; i < page.mosaicSpecs.length; i++) {
            var spec = page.mosaicSpecs[i];
            out[spec.key] = page._normalizeHouseList(
                page.catalogSource.mosaic(spec.facetKey, page.mosaicLimit, page.showExplicit) || []);
        }
        return out;
    }

    function _normalizeHouseItem(row) {
        var r = row || {};
        var ratingObj = (r.rating && typeof r.rating === "object") ? r.rating : {};
        var avg = (ratingObj.average !== undefined && ratingObj.average !== null) ? Number(ratingObj.average) : 0;
        return {
            id: r.canonicalId || "", title: r.title || "", author: r.author || "",
            cover: r.coverUrl || "",
            rating: avg > 0 ? avg.toFixed(1) : "",
            // the native store has no per-work provenance field (provenance lives in
            // work_sources, not exposed through page()); the catalogue-level attribution is
            // constant and honest for every house/top-10 row.
            source: "Apple Books · Open Library",
            raw: r
        };
    }
    function _normalizeHouseList(list) {
        var out = [];
        for (var i = 0; i < (list || []).length; i++) out.push(page._normalizeHouseItem(list[i]));
        return out;
    }
    function _normalizeExtItem(meta, catalog) {
        var m = meta || {};
        return {
            id: (m.id !== undefined && m.id !== null) ? m.id : "",
            title: m.title || m.name || m.caption || "",
            author: m.author || m.creator || m.writer || "",
            cover: m.cover || m.poster || "",
            rating: (m.imdbRating !== undefined && m.imdbRating !== null && String(m.imdbRating).length)
                ? m.imdbRating : (m.rating || ""),
            source: catalog ? (catalog.addonName || catalog.title || "") : "",
            raw: m
        };
    }

    // ── extension rows: independent per-row async state (loading | ok | empty). An empty or
    // failed fetch (DiscoverApi.loadPage answers [] on either) collapses the row entirely —
    // never a placeholder shelf. ──
    property var extensionRowData: ({})
    function _extRowStatus(key) {
        var d = page.extensionRowData[key];
        return d ? d.status : "loading";
    }
    function _extRowItems(key) {
        var d = page.extensionRowData[key];
        return d ? d.items : [];
    }
    function _setExtRowData(key, status, items) {
        var d = {};
        for (var k in page.extensionRowData) d[k] = page.extensionRowData[k];
        d[key] = { status: status, items: items || [] };
        page.extensionRowData = d;
    }
    function _loadExtensionRow(catalog) {
        var key = catalog.key;
        page._setExtRowData(key, "loading", []);
        var sel = DiscoverApi.selectionsForFilter(catalog, "", "");
        var fetcher = page.pageFetcher ? page.pageFetcher : DiscoverApi.loadPage;
        fetcher(catalog, sel, 0, function(metas) {
            var list = metas || [];
            var items = [];
            for (var i = 0; i < list.length; i++) {
                var norm = page._normalizeExtItem(list[i], catalog);
                if (!Policy.visible("biblio", norm.raw || {}, page.showExplicit)) continue;
                items.push(norm);
            }
            page._setExtRowData(key, items.length ? "ok" : "empty", items);
        });
    }
    function reloadExtensionRows() {
        var cats = page.bookExtensionCatalogs;
        for (var i = 0; i < cats.length; i++) page._loadExtensionRow(cats[i]);
    }
    Component.onCompleted: page.reloadExtensionRows()
    // The house/mosaic rows already react to a live showExplicit flip via their declarative
    // bindings on page.showExplicit (houseRowsMap/top10Items/mosaicItemsByKey all read it
    // directly). Extension rows do NOT — _loadExtensionRow's async fetch callback captures
    // page.showExplicit only at fetch time, so a stale explicit (or stale hidden) item could
    // persist in extensionRowData forever after a live preference flip. Re-run the fetch so the
    // gate in _loadExtensionRow's callback re-evaluates against the NEW value.
    onShowExplicitChanged: page.reloadExtensionRows()

    // ── customization: default rows -> preference order/hidden -> effective render order ──
    readonly property var effectiveRows: {
        var _dep = page.prefsRev;
        return Rules.applyCustomization(page.defaultRowKeys,
            { order: page._prefs.order, hidden: page._prefs.hidden }, page.editMode);
    }
    readonly property var hiddenMap: {
        var m = {};
        for (var i = 0; i < page.effectiveRows.length; i++) m[page.effectiveRows[i].key] = page.effectiveRows[i].hidden;
        return m;
    }
    // the LIVE visible order: the persisted/customized order, unless a drag is in progress, in
    // which case the drag's TEMPORARY order wins (never written to BiblioExplorePreferences
    // until the drag completes).
    readonly property var orderedKeys: {
        if (page.dragKeys) return page.dragKeys;
        var out = [];
        for (var i = 0; i < page.effectiveRows.length; i++) out.push(page.effectiveRows[i].key);
        return out;
    }

    readonly property var displayRows: {
        var _dep = [page.prefsRev, page.editMode, page.extensionRowData, page.houseRowsMap,
                    page.top10Items, page.dragKeys, page.catalogByExtKey, page.housesLoading];
        var keys = page.orderedKeys;
        var out = [];
        for (var i = 0; i < keys.length; i++) {
            var key = keys[i];
            var hidden = page.hiddenMap[key] === true;
            if (hidden && page.editMode !== true) continue;
            var row = page._buildRow(key, hidden);
            if (row) out.push(row);
        }
        return out;
    }

    function _buildRow(key, hidden) {
        if (key === Rules.TOP_TEN_KEY) {
            return {
                key: key, hidden: hidden, kind: "top10", title: "Top 10",
                items: page.top10Items, ranked: true, loading: page.housesLoading,
                pin: { type: "book", catalogId: "popular", filterGroup: "", filterKey: "", sourceKind: "builtin" }
            };
        }
        if (Rules.isExtensionKey(key)) {
            var extKey = key.substring(Rules.EXTENSION_PREFIX.length);
            var cat = page.catalogByExtKey[extKey];
            if (!cat) return null;   // the extension was removed mid-session -> nothing to render
            var status = page._extRowStatus(extKey);
            if (status === "empty") return null;   // collapse entirely, never a placeholder shelf
            return {
                key: key, hidden: hidden, kind: "extension", title: cat.title || cat.addonName || "Extension",
                items: status === "ok" ? page._extRowItems(extKey) : [],
                ranked: false, loading: status === "loading",
                pin: { type: "book", catalogId: cat.key, filterGroup: "", filterKey: "",
                       sourceKind: "extension", extensionId: extKey,
                       transportUrl: cat.transportUrl, extCatalogId: cat.catalogId,
                       addonName: cat.addonName || "" }
            };
        }
        return {
            key: key, hidden: hidden, kind: "house", title: page.houseTitles[key] || key,
            items: page.houseRowsMap[key] || [], ranked: false, loading: page.housesLoading,
            pin: { type: "book", catalogId: key, filterGroup: "", filterKey: "", sourceKind: "builtin" }
        };
    }

    // ── reorder: pointer drag (temporary visible order, committed on release) and the
    // keyboard-equivalent Move Up/Move Down (immediate). BOTH converge on the SAME
    // BiblioExplorePreferences.move(key, toIndex) call via _commitMove — one code path, not two. ──
    readonly property var customizableRowKeys: {
        var out = [];
        for (var i = 0; i < page.displayRows.length; i++) out.push(page.displayRows[i].key);
        return out;
    }
    function canMoveUp(key) {
        var i = page.customizableRowKeys.indexOf(key);
        return i > 0;
    }
    function canMoveDown(key) {
        var i = page.customizableRowKeys.indexOf(key);
        return i >= 0 && i < page.customizableRowKeys.length - 1;
    }
    // BiblioExplorePreferences.move(key, toIndex) clamps `toIndex` against its OWN persisted
    // `order` array — which may still be sparse/empty (Rules._effectiveOrder only records
    // DEVIATIONS from the default sequence; an unlisted row simply keeps its default relative
    // slot). To give `toIndex` an unambiguous meaning ("absolute position in the full, currently
    // effective row list"), first seed `order` to match that full sequence — a call per key,
    // each a silent no-op once a key is already correctly placed — then reposition the target
    // key as the final, real write.
    function _commitMove(key, toIndex) {
        var fullKeys = page.customizableRowKeys;
        for (var i = 0; i < fullKeys.length; i++) {
            if (fullKeys[i] === key) continue;
            page._prefs.move(fullKeys[i], i);
        }
        page._prefs.move(key, toIndex);
    }
    function moveRowBy(key, delta) {
        var keys = page.customizableRowKeys;
        var idx = keys.indexOf(key);
        if (idx < 0) return;
        var to = Math.max(0, Math.min(keys.length - 1, idx + delta));
        if (to === idx) return;
        page._commitMove(key, to);
    }

    property var dragKeys: null
    property string draggingKey: ""
    readonly property real dragRowStride: 250   // approx one shelf's vertical stride

    function beginDrag(key) {
        page.draggingKey = key;
        page.dragKeys = page.customizableRowKeys.slice();
    }
    function updateDragDelta(key, deltaY) {
        if (!page.dragKeys) return;
        var arr = page.dragKeys.slice();
        var from = arr.indexOf(key);
        if (from < 0) return;
        var steps = Math.round(deltaY / page.dragRowStride);
        var to = Math.max(0, Math.min(arr.length - 1, from + steps));
        if (to !== from) {
            arr.splice(from, 1);
            arr.splice(to, 0, key);
            page.dragKeys = arr;
        }
    }
    function endDrag(key) {
        if (page.dragKeys) {
            var idx = page.dragKeys.indexOf(key);
            if (idx >= 0) page._commitMove(key, idx);
        }
        page.dragKeys = null;
        page.draggingKey = "";
    }

    // test/introspection helper (mirrors TheatreCatalogPage's mainShelfAt convention): whether
    // row `index`'s edit-mode control strip (drag handle + move up/down + hide/show) is
    // currently visible — an offscreen harness cannot simulate a real pointer, so "drag handles
    // appear only in edit mode" is asserted directly against the rendered delegate.
    function editControlsVisibleAt(index) {
        var d = rowRepeater.itemAt(index);
        return d ? d.editControlsVisible : false;
    }

    // the pin a mosaic tile activates — a single small helper so both the pointer and keyboard
    // activation paths build the IDENTICAL object (and so a harness can assert its shape without
    // simulating a click).
    function mosaicPin(spec) {
        return { type: "book", catalogId: "popular", filterGroup: spec.axis, filterKey: spec.facetKey,
                 sourceKind: "builtin" };
    }

    // ═══════════════════════════════ visual tree ═══════════════════════════════
    Flickable {
        id: mainFlick
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: mainFlick }
        Component.onCompleted: mainFlick.contentY = page.contentY
        onContentYChanged: page.contentY = mainFlick.contentY

        Column {
            id: content
            width: mainFlick.width
            spacing: 28

            Item {
                width: content.width
                height: 30
                Text {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: "Explore"
                    color: theme.ink; font.family: theme.display; font.pixelSize: 26; font.weight: Font.DemiBold
                }
                Text {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: page.editMode ? "Done" : "Customize shelves"
                    color: custMa.containsMouse ? theme.ink : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                    MouseArea {
                        id: custMa
                        anchors.fill: parent; anchors.margins: -8
                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: page.editMode = !page.editMode
                    }
                }
            }

            // ── customizable rows: Top 10, extension previews, house rails ──
            Repeater {
                id: rowRepeater
                model: page.displayRows
                delegate: Column {
                    id: rowBlock
                    required property var modelData
                    required property int index
                    readonly property string rowKey: rowBlock.modelData.key
                    readonly property bool editControlsVisible: editControls.visible

                    width: content.width
                    spacing: 8
                    opacity: rowBlock.modelData.hidden ? 0.45 : 1

                    // edit-mode control strip: drag handle + move up/down + hide/show
                    Row {
                        id: editControls
                        visible: page.editMode
                        spacing: 10
                        height: 28

                        Rectangle {
                            id: dragHandle
                            width: 28; height: 28; radius: 6
                            color: (dragHandleMa.containsMouse || page.draggingKey === rowBlock.rowKey)
                                   ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                            border.width: 1; border.color: theme.edge
                            Text { anchors.centerIn: parent; text: "⠿"; color: theme.ink; font.pixelSize: 14 }
                            MouseArea {
                                id: dragHandleMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.SizeVerCursor
                                property real pressGlobalY: 0
                                onPressed: (mouse) => {
                                    dragHandleMa.pressGlobalY = dragHandleMa.mapToItem(content, mouse.x, mouse.y).y
                                    page.beginDrag(rowBlock.rowKey)
                                }
                                onPositionChanged: (mouse) => {
                                    if (!dragHandleMa.pressed) return
                                    var nowY = dragHandleMa.mapToItem(content, mouse.x, mouse.y).y
                                    page.updateDragDelta(rowBlock.rowKey, nowY - dragHandleMa.pressGlobalY)
                                }
                                onReleased: page.endDrag(rowBlock.rowKey)
                            }
                        }
                        Rectangle {
                            width: 28; height: 28; radius: 6
                            border.width: 1; border.color: theme.edge
                            color: upMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                            opacity: page.canMoveUp(rowBlock.rowKey) ? 1 : 0.35
                            Text { anchors.centerIn: parent; text: "▲"; color: theme.ink; font.pixelSize: 12 }
                            MouseArea {
                                id: upMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                enabled: page.canMoveUp(rowBlock.rowKey)
                                onClicked: page.moveRowBy(rowBlock.rowKey, -1)
                            }
                        }
                        Rectangle {
                            width: 28; height: 28; radius: 6
                            border.width: 1; border.color: theme.edge
                            color: downMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                            opacity: page.canMoveDown(rowBlock.rowKey) ? 1 : 0.35
                            Text { anchors.centerIn: parent; text: "▼"; color: theme.ink; font.pixelSize: 12 }
                            MouseArea {
                                id: downMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                enabled: page.canMoveDown(rowBlock.rowKey)
                                onClicked: page.moveRowBy(rowBlock.rowKey, 1)
                            }
                        }
                        Rectangle {
                            width: 64; height: 28; radius: 6
                            border.width: 1; border.color: theme.edge
                            color: hideMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: rowBlock.modelData.hidden ? "Show" : "Hide"
                                color: theme.ink; font.family: theme.ui; font.pixelSize: 11
                            }
                            MouseArea {
                                id: hideMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page._prefs.setVisible(rowBlock.rowKey, rowBlock.modelData.hidden === true)
                            }
                        }
                    }

                    BiblioBookRail {
                        width: content.width
                        title: rowBlock.modelData.title
                        items: rowBlock.modelData.items
                        ranked: rowBlock.modelData.ranked === true
                        loading: rowBlock.modelData.loading === true
                        showSeeAll: true
                        onItemActivated: (item) => page.itemRequested(item)
                        onSeeAllActivated: page.discoverPinRequested(rowBlock.modelData.pin)
                    }
                }
            }

            // ── the three fixed mosaics: ALWAYS last, NEVER part of customization ──
            Column {
                width: content.width
                spacing: 20

                Repeater {
                    model: page.mosaicSpecs
                    delegate: Rectangle {
                        id: tile
                        required property var modelData
                        readonly property var mosaicItems: page.mosaicItemsByKey[tile.modelData.key] || []

                        width: content.width
                        height: 168
                        radius: 14
                        visible: tile.mosaicItems.length > 0
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.09)

                        Row {
                            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                            anchors.margins: 14
                            height: 132
                            spacing: 8
                            Repeater {
                                model: Math.min(tile.mosaicItems.length, 7)
                                delegate: Rectangle {
                                    required property int index
                                    width: 88; height: 132; radius: 6
                                    color: "#1b1b22"
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: tile.mosaicItems[index].cover || ""
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                    }
                                }
                            }
                        }
                        Text {
                            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 14
                            text: tile.modelData.title
                            color: theme.ink; font.family: theme.display; font.pixelSize: 18; font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.discoverPinRequested(page.mosaicPin(tile.modelData))
                        }
                        Keys.onReturnPressed: page.discoverPinRequested(page.mosaicPin(tile.modelData))
                    }
                }
            }
        }
    }

    ScrollGlide { id: pageGlide; flick: mainFlick }
}
