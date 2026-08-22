// DiscoverBrowser — the WORLD-NEUTRAL Discover shell (Task 3, arc 2026-08-01).
//
// This is the generic browsing surface carved out of DiscoverPage.qml: the masthead
// (type lens + named shelf/catalog picker + byline), the single active filter, the
// missing/offline notice, and the full-width poster wall with skeletons, hover reveal,
// keyboard focus ring and skip-paging. It knows NOTHING about Manga/Comics/Movies/Shows,
// Cinemeta, Extensions or any transport — every derivation and every fetch rides an
// injected `adapter` that speaks the shared contract:
//
//   types()                 -> [{ key, label }]
//   catalogs(type)          -> [{ key, title, sourceKind, section, attribution }]
//   filters(type, catalog)  -> [{ group, options: [{ key, label }] }]
//   defaultCatalog(type)    -> string key
//   resolvePin(pin)         -> { missing, type, catalogKey, filterGroup, filterKey, missingName }
//   fetchPage(state, cursor, generation, done)   // done(generation, page)
//     state = { type, catalogKey, filterGroup, filterKey }
//     page  = { items, nextCursor, exhausted, freshness, warning }
//
// Normalized card shape the wall renders:
//   { id, type, title, cover, year, rating, format, publisher, availability, explicit, raw }
//
// The visual tree is copied INTACT from the 2026-07-25 Discover overhaul — same house gold,
// same monochrome glyphs, same geometry. Only the data plumbing changed: Api.* calls became
// adapter.* calls, the N filter pickers collapsed to one active selection, and the cards read
// normalized fields. The Theatre wrapper (DiscoverPage.qml) supplies the adapter and the copy.
import QtQuick
import QtQuick.Controls
import "CatalogueVisualMetrics.js" as Metrics

Item {
    id: browser

    // ── injected world seam ──
    property var adapter: null
    // the type to fall back to when the adapter offers none yet (bare construction / empty registry)
    property string fallbackType: ""
    // poster visual profile passed straight through to the shared card. Default classic; a wrapper
    // opts into "gallery" only after its own eyes-on gate (Discover/Tankoban/Comics via Task 9).
    property string posterVisualProfile: "classic"
    readonly property bool _galleryPosters: browser.posterVisualProfile === "gallery"
    readonly property var _galleryMetrics: Metrics.gallery

    // ── optional per-world card hooks (Biblio, Task 5, arc 2026-08-01) ──
    // Every hook defaults OFF so a shared-shell edit never silently restyles a world that
    // hasn't opted in (Tankoban/Theatre render byte-identical unless they flip these).
    property bool showAuthorAtRest: false     // render item.author on the card AT REST, not just on reveal
    property bool showSourceOnReveal: false   // render item.source in the reveal, on hover OR keyboard focus
    property bool showBackAction: false       // render a back affordance in the masthead
    signal backRequested()                    // "user wants to go back" — the shell never acts on this itself
    // Pin the gallery delegate to EXACTLY _galleryMetrics.posterWidth instead of stretching to fill
    // residual column width (the bug behind Biblio's oversized/blurry cards, 2026-08-06). OFF by
    // default: Theatre/Tankoban keep today's fill-to-width gallery layout unless they opt in too.
    // No effect outside the gallery profile (classic is untouched either way).
    property bool fixedGalleryWidth: false
    // Test-only introspection: the actual rendered delegate box, so an offscreen harness can prove
    // the geometry contract without a screenshot or a live pointer. Production code never reads these.
    readonly property int _galleryDelegateWidthForTest: wall ? wall.cellWidth - 14 : 0
    readonly property int _galleryColumnCountForTest: wall ? wall.columnCount : 0

    // ── generic browsing state ──
    property string currentType: ""
    property string currentCatalogKey: ""
    property string filterGroup: ""
    property string filterKey: ""
    property var cursor: null
    property int fetchGen: 0                 // stale-response fence
    property var typeStates: ({})            // per-type session memory {type: {...}}
    property int adapterRev: 0               // bump to force adapter-derived bindings to re-evaluate

    property var items: []
    property bool loading: false
    property bool exhausted: false
    property string warning: ""
    property string freshness: ""
    property string noticeText: ""           // a missing-catalogue explanation, when any
    property var pinValue: null

    property bool keyboardMode: false        // true once arrows are used -> shows the focus ring
    property bool catalogMenuOpen: false     // the Fraunces shelf-name doubles as the catalog picker

    // ── injectable copy (world-neutral defaults; the wrapper sets its world's exact words) ──
    property string textNoCatalogue: "Nothing to browse here yet."
    property string textCatalogueEmpty: "This catalogue answered with nothing."
    property string textFilterEmpty: "Nothing here matches this filter."
    property string noticeMissingFormat: "%1 is no longer available — showing the built-in catalogue instead."
    property string offlineWarning: "Showing offline catalogue"

    // Data-vault Slice 3 (2026-08-22): world-neutral seam a wrapper sets true when the wall is
    // empty ONLY because its backing catalogue db has not landed yet (CatalogVault still
    // fetching). Default false — a wrapper that never sets it renders byte-identical to before
    // this slice. When true, the honest empty state below shows this ONE quiet line instead of
    // the normal empty-catalogue copy; nothing else about the wall changes.
    property bool catalogueDownloading: false
    readonly property string _downloadingText: "Catalogue downloading…"

    readonly property string _filterSep: "|"   // encodes group+key in one picker option key

    signal itemOpenRequested(var item)       // a click / Enter on a poster opens the detail page

    Theme { id: theme }

    // ── derived reads (adapterRev is a dependency so a registry change re-derives them) ──
    readonly property var currentCatalog: {
        var _ = adapterRev;
        if (!adapter || !currentType.length) return null;
        var cats = adapter.catalogs(currentType);
        for (var i = 0; i < cats.length; i++)
            if (cats[i].key === currentCatalogKey) return cats[i];
        return null;
    }

    // catalog picker rows, sectioned by descriptor `section` (the shell never invents sections)
    readonly property var catalogMenuModel: {
        var _ = adapterRev;
        var out = []; var lastSec = null;
        var cats = (adapter && currentType.length) ? adapter.catalogs(currentType) : [];
        for (var i = 0; i < cats.length; i++) {
            var c = cats[i];
            var sec = c.section || "";
            if (sec !== lastSec) { out.push({ header: sec }); lastSec = sec; }
            out.push({ key: c.key, text: c.title, sub: c.attribution || "" });
        }
        return out;
    }
    readonly property bool hasCatalogs: catalogMenuModel.length > 0

    // the filter groups for the current catalogue, concatenated into ONE picker menu:
    // a leading "All" (clears), then each group's options — with a section header only when
    // more than one group exists (a single group reads exactly like the old genre picker).
    readonly property var filterGroups: {
        var _ = adapterRev;
        return (adapter && currentCatalogKey.length) ? adapter.filters(currentType, currentCatalogKey) : [];
    }
    readonly property var filterMenuModel: {
        var groups = filterGroups;
        var out = [{ key: "", text: "All", sub: "" }];
        var multi = groups.length > 1;
        for (var g = 0; g < groups.length; g++) {
            if (multi) out.push({ header: groups[g].group });
            var opts = groups[g].options || [];
            for (var o = 0; o < opts.length; o++)
                out.push({ key: groups[g].group + browser._filterSep + opts[o].key, text: opts[o].label, sub: "" });
        }
        return out;
    }
    readonly property bool filterHasOptions: filterMenuModel.length > 1
    readonly property string filterPickerLabel: filterGroups.length === 1 ? filterGroups[0].group : "Filter"
    readonly property var filterSelection: ({ group: filterGroup, key: filterKey })

    // the active filter's human label (for the byline) — key equals label in the flat worlds,
    // but resolve it honestly through the descriptor so multi-option worlds read right too.
    readonly property string activeFilterLabel: {
        if (!filterKey.length) return "";
        var groups = filterGroups;
        for (var g = 0; g < groups.length; g++)
            if (groups[g].group === filterGroup)
                for (var o = 0; o < groups[g].options.length; o++)
                    if (groups[g].options[o].key === filterKey) return groups[g].options[o].label;
        return filterKey;
    }

    readonly property string emptyMessage: !currentCatalog ? textNoCatalogue
        : (filterKey.length ? textFilterEmpty : textCatalogueEmpty)
    readonly property bool showOfflineNotice: offlineWarning.length > 0 && warning === offlineWarning
    readonly property string bannerText: showOfflineNotice ? warning : noticeText
    readonly property bool bannerVisible: showOfflineNotice || noticeText.length > 0

    // ─────────────────────────────── behaviour ───────────────────────────────

    function activateIndex(i) {
        if (i < 0 || i >= items.length) return           // skeletons / out-of-range never activate
        itemOpenRequested(items[i])
    }

    onVisibleChanged: if (visible && wall) wall.forceActiveFocus()

    Component.onCompleted: init()

    function init() {
        if (!adapter) { if (fallbackType.length) currentType = fallbackType; return }
        if (pinValue) { applyPin(pinValue); return }
        var ts = adapter.types()
        var t = ts.length ? ts[0].key : fallbackType
        currentType = t
        currentCatalogKey = adapter.defaultCatalog(t)
        filterGroup = ""; filterKey = ""; noticeText = ""
        reloadForCatalog()
    }

    function selectType(t) {
        if (t === currentType) return
        fetchGen++                       // fence any in-flight fetch bound to the leaving type
        saveTypeState()
        currentType = t
        catalogMenuOpen = false
        if (typeStates[t]) {
            restoreTypeState(typeStates[t])
        } else {
            currentCatalogKey = adapter ? adapter.defaultCatalog(t) : ""
            filterGroup = ""; filterKey = ""; noticeText = ""
            reloadForCatalog()
        }
    }

    function selectCatalog(key) {
        fetchGen++
        currentCatalogKey = key
        filterGroup = ""; filterKey = ""; noticeText = ""
        catalogMenuOpen = false
        reloadForCatalog()
    }

    function setFilter(group, key) {
        fetchGen++
        if (key && key.length) { filterGroup = group; filterKey = key }
        else { filterGroup = ""; filterKey = "" }
        reloadForCatalog()
    }

    function clearFilter() {
        fetchGen++
        filterGroup = ""; filterKey = ""
        reloadForCatalog()
    }

    // split a picker option key (group + SEP + key) back into a filter selection
    function _applyFilterKey(encoded) {
        if (!encoded || !encoded.length) { clearFilter(); return }
        var i = encoded.indexOf(_filterSep)
        if (i < 0) { setFilter("", encoded); return }
        setFilter(encoded.substring(0, i), encoded.substring(i + _filterSep.length))
    }

    function applyPin(pin) {
        pinValue = pin || null
        if (!pin || !adapter) return
        fetchGen++
        var res = adapter.resolvePin(pin)
        catalogMenuOpen = false
        if (res.missing) {
            // a missing pinned extension/catalogue -> the same type's built-in default,
            // an invalid filter cleared, and one explanatory notice.
            currentType = res.type
            currentCatalogKey = adapter.defaultCatalog(res.type)
            filterGroup = ""; filterKey = ""
            noticeText = noticeMissingFormat.arg(
                (res.missingName && res.missingName.length) ? res.missingName : "That source")
            reloadForCatalog()
            return
        }
        noticeText = ""
        currentType = res.type
        currentCatalogKey = res.catalogKey
        filterGroup = res.filterGroup || ""
        filterKey = res.filterKey || ""
        reloadForCatalog()
    }

    // re-derive after the adapter's data changed (e.g. an extension installed/removed).
    function refresh() {
        if (!adapter) return
        adapterRev++                     // force the adapter-derived bindings to re-evaluate
        if (pinValue) { applyPin(pinValue); return }
        var cats = adapter.catalogs(currentType)
        for (var i = 0; i < cats.length; i++)
            if (cats[i].key === currentCatalogKey) return   // still present -> keep the wall as-is
        // the current catalogue is gone -> fall to the default
        fetchGen++
        currentCatalogKey = adapter.defaultCatalog(currentType)
        filterGroup = ""; filterKey = ""; noticeText = ""
        reloadForCatalog()
    }

    // Force a genuine re-fetch of the CURRENT catalogue/filter selection, unconditionally —
    // unlike refresh(), which only reloads when the current catalogue disappeared from the
    // adapter's list. A world calls this when the adapter's ANSWER for an unchanged
    // catalogue/filter selection has changed shape (e.g. Biblio's global Explicit Content
    // preference flips, and the same catalogue must be asked again with the new value baked
    // into fetchPage). Keeps the catalogue/filter selection exactly as-is; reloadForCatalog()
    // already resets items/cursor/exhausted, so a flip never strands stale paging state
    // alongside fresh items.
    function reloadCurrent() {
        if (!adapter) return
        fetchGen++
        reloadForCatalog()
    }

    function saveTypeState() {
        if (!currentType.length) return
        var s = {}
        for (var k in typeStates) s[k] = typeStates[k]
        s[currentType] = { catalogKey: currentCatalogKey, filterGroup: filterGroup, filterKey: filterKey,
                           items: items, cursor: cursor, exhausted: exhausted,
                           warning: warning, freshness: freshness, noticeText: noticeText,
                           contentY: (wall ? wall.contentY : 0) }   // remember scroll for restore
        typeStates = s
    }

    property real _pendingScrollY: -1        // >= 0 while a restored scroll waits to be reapplied
    function applyPendingScroll() {
        if (_pendingScrollY < 0 || !wall) return
        // clamp so a stale scroll never lands past a now-shorter wall
        var maxY = Math.max(0, wall.contentHeight - wall.height)
        wall.contentY = Math.min(_pendingScrollY, maxY)
        _pendingScrollY = -1
    }

    function restoreTypeState(st) {
        currentCatalogKey = st.catalogKey
        filterGroup = st.filterGroup; filterKey = st.filterKey
        items = st.items; cursor = st.cursor; exhausted = st.exhausted
        warning = st.warning || ""; freshness = st.freshness || ""; noticeText = st.noticeText || ""
        loading = false
        // A type LEFT during its first-page fetch saved {items:[], exhausted:false}: the generation
        // fence dropped that in-flight reply, and onContentYChanged can't re-page an empty wall — so
        // the type would strand on the empty state. Re-issue a page when the restored wall is empty
        // and NOT exhausted. A legitimately-empty catalogue saved exhausted:true (and requestPage
        // guards on exhausted too), so this never double-fetches a settled, empty catalogue.
        if (!items.length && !exhausted) { requestPage(); return }
        // restore the wall's scroll best-effort, after the GridView re-lays out the restored model.
        _pendingScrollY = (st.contentY || 0)
        Qt.callLater(applyPendingScroll)
    }

    function reloadForCatalog() {
        items = []; cursor = null; exhausted = false; loading = false
        warning = ""; freshness = ""
        requestPage()
    }

    function requestPage() {
        if (!adapter || loading || exhausted || currentCatalogKey.length === 0) return
        loading = true
        var gen = ++fetchGen
        var st = { type: currentType, catalogKey: currentCatalogKey,
                   filterGroup: filterGroup, filterKey: filterKey }
        adapter.fetchPage(st, cursor, gen, function(replyGen, page) {
            if (replyGen !== browser.fetchGen) return      // a newer ask superseded this one
            browser.acceptPage(page)
        })
    }

    function acceptPage(page) {
        loading = false
        if (!page) { exhausted = true; return }
        warning = page.warning || ""
        freshness = page.freshness || ""
        var incoming = page.items || []
        if (incoming.length) {
            var merged = items.slice()
            for (var i = 0; i < incoming.length; i++) merged.push(incoming[i])
            items = merged
        }
        cursor = (page.nextCursor !== undefined) ? page.nextCursor : null
        if (page.exhausted) exhausted = true
    }

    // opening the catalog menu closes the filter picker (one drop-down family)
    onCatalogMenuOpenChanged: if (catalogMenuOpen) filterPicker.open = false

    // ═══ masthead — the type lens (left) + named shelf/catalog picker (right) ═══
    // z above the wall: the catalog menu drops INTO the wall region and must paint over it.
    Item {
        id: masthead
        z: 100
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 76

        Rectangle {
            id: mastheadRule
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Qt.rgba(1, 1, 1, 0.09)
        }

        // ── optional back affordance (Biblio's Explore-return, Task 5) — top-left, above
        // the type lens; invisible/no-op for every world that leaves showBackAction off. ──
        Text {
            id: backAction
            visible: browser.showBackAction
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.topMargin: 6
            text: "‹ Back"
            color: backMa.containsMouse ? theme.gold : theme.inkDim
            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
            MouseArea {
                id: backMa
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: browser.backRequested()
            }
        }

        // ── type lens: underlined text tabs (NOT filled pills) ──
        Row {
            id: typeSwitch
            anchors.left: parent.left
            anchors.bottom: mastheadRule.top
            anchors.bottomMargin: 12
            spacing: 28
            Repeater {
                model: (browser.adapterRev, browser.adapter ? browser.adapter.types() : [])
                delegate: Item {
                    id: typeTab
                    required property var modelData
                    readonly property bool active: browser.currentType === typeTab.modelData.key
                    width: tlabel.implicitWidth
                    height: tlabel.implicitHeight + 9
                    Text {
                        id: tlabel
                        anchors.top: parent.top
                        text: typeTab.modelData.label
                        color: typeTab.active ? theme.ink
                             : (tHov.hovered ? theme.inkDim : theme.inkDimmer)
                        font.family: theme.ui; font.pixelSize: 17; font.weight: Font.DemiBold
                    }
                    Rectangle {                       // gold underline marks the active lens
                        visible: typeTab.active
                        anchors.left: tlabel.left; anchors.right: tlabel.right
                        anchors.bottom: parent.bottom
                        height: 3; radius: 2; color: theme.gold
                    }
                    HoverHandler { id: tHov }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: browser.selectType(typeTab.modelData.key)
                    }
                }
            }
        }

        // ── named shelf (right): kicker + Fraunces catalog name (= the picker) + byline ──
        Item {
            id: shelf
            anchors.right: parent.right
            anchors.bottom: mastheadRule.top
            anchors.bottomMargin: 9
            width: Math.max(nameRow.width, byline.implicitWidth, kicker.implicitWidth)
            height: kicker.implicitHeight + 4 + nameRow.height + 7 + byline.implicitHeight

            Text {
                id: kicker
                anchors.right: parent.right; anchors.top: parent.top
                text: "NOW BROWSING"
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 10
                font.letterSpacing: 2.5; font.capitalization: Font.AllUppercase
            }
            Row {
                id: nameRow
                anchors.right: parent.right
                anchors.top: kicker.bottom; anchors.topMargin: 4
                spacing: 10
                Text {
                    id: catName
                    anchors.verticalCenter: parent.verticalCenter
                    text: browser.currentCatalog ? browser.currentCatalog.title : "—"
                    color: (catMa.containsMouse || browser.catalogMenuOpen) ? "#ffffff" : theme.ink
                    font.family: theme.display; font.pixelSize: 30; font.weight: Font.DemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▾"
                    color: (catMa.containsMouse || browser.catalogMenuOpen) ? theme.gold : theme.inkDimmer
                    font.pixelSize: 14
                }
            }
            MouseArea {
                id: catMa
                anchors.fill: nameRow
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                enabled: browser.hasCatalogs
                onClicked: browser.catalogMenuOpen = !browser.catalogMenuOpen
            }
            Text {
                id: byline
                anchors.right: parent.right
                anchors.top: nameRow.bottom; anchors.topMargin: 7
                // honest attribution — the owning source, plus any active filter. NO invented total.
                text: {
                    var a = browser.currentCatalog ? browser.currentCatalog.attribution : ""
                    var f = browser.activeFilterLabel
                    return f.length ? (a + "   ·   " + f) : a
                }
                color: theme.inkDim
                font.family: theme.ui; font.pixelSize: 13
            }
        }

        // ── catalog menu — the shelf-name drop-down (sectioned, gold-active) ──
        Rectangle {
            id: catalogMenu
            visible: browser.catalogMenuOpen
            anchors.right: parent.right
            anchors.top: parent.bottom
            anchors.topMargin: 6
            width: 300
            height: Math.min(392, menuList.contentHeight + 12)
            radius: 13
            z: 60
            color: Qt.rgba(0.045, 0.05, 0.075, 0.98)
            border.width: 1; border.color: theme.edge
            MouseArea { anchors.fill: parent }   // swallow taps inside the menu

            ListView {
                id: menuList
                anchors.fill: parent; anchors.margins: 6
                clip: true
                model: browser.catalogMenuModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: HouseScrollBar { flick: menuList }
                delegate: Item {
                    id: mopt
                    required property var modelData
                    readonly property bool isHeader: modelData.header !== undefined
                    width: menuList.width
                    height: isHeader ? 27 : 38

                    Text {
                        visible: mopt.isHeader
                        text: mopt.isHeader ? mopt.modelData.header : ""
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 10
                        font.letterSpacing: 1.6; font.capitalization: Font.AllUppercase
                        anchors.left: parent.left; anchors.leftMargin: 12
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                    }
                    Rectangle {
                        id: mrow
                        visible: !mopt.isHeader
                        anchors.fill: parent
                        radius: 9
                        readonly property bool sel: !mopt.isHeader && !!browser.currentCatalog
                                                    && mopt.modelData.key === browser.currentCatalog.key
                        color: mrow.sel ? Qt.rgba(240/255, 196/255, 74/255, 0.16)
                             : mrowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                        Text {
                            id: mcat
                            text: mopt.isHeader ? "" : mopt.modelData.text
                            color: mrow.sel ? theme.gold : theme.ink
                            font.family: theme.ui; font.pixelSize: 13
                            font.weight: mrow.sel ? Font.DemiBold : Font.Normal
                            anchors.left: parent.left; anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: !!mopt.modelData.sub
                            text: mopt.modelData.sub || ""
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                            anchors.right: parent.right; anchors.rightMargin: 12
                            anchors.left: mcat.right; anchors.leftMargin: 8
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        MouseArea {
                            id: mrowMa
                            anchors.fill: parent
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                browser.catalogMenuOpen = false
                                browser.selectCatalog(mopt.modelData.key)
                            }
                        }
                    }
                }
            }
        }
    }

    // click-off catcher — a tap anywhere outside the open catalog menu closes it
    MouseArea {
        anchors.fill: parent
        z: 95
        visible: browser.catalogMenuOpen
        onClicked: browser.catalogMenuOpen = false
    }

    // ═══ filter chips — the genuine filters collapsed to ONE active selection ═══
    Row {
        id: filterRow
        z: 90
        visible: browser.filterHasOptions
        anchors.top: masthead.bottom
        anchors.topMargin: 16
        anchors.left: parent.left
        spacing: 10
        Text {
            text: "FILTER"
            height: 40; verticalAlignment: Text.AlignVCenter
            color: theme.inkDimmer
            font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.5
            font.capitalization: Font.AllUppercase
        }
        DiscoverPicker {
            id: filterPicker
            label: browser.filterPickerLabel
            clearable: true
            options: browser.filterMenuModel
            currentKey: browser.filterKey.length ? (browser.filterGroup + browser._filterSep + browser.filterKey) : ""
            onPicked: (key) => browser._applyFilterKey(key)
            onCleared: browser.clearFilter()
            onOpenChanged: if (open) browser.catalogMenuOpen = false
        }
    }

    // ─── notice bar — a missing-catalogue explanation or the offline-catalogue notice ───
    Rectangle {
        id: noticeBar
        visible: browser.bannerVisible
        anchors.top: filterRow.visible ? filterRow.bottom : masthead.bottom
        anchors.topMargin: 14
        width: parent.width; height: visible ? 52 : 0
        radius: 12
        color: Qt.rgba(240/255, 196/255, 74/255, 0.08)
        border.width: 1; border.color: Qt.rgba(240/255, 196/255, 74/255, 0.4)
        Text {
            anchors.left: parent.left; anchors.leftMargin: 16
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: browser.bannerText
            color: theme.ink; font.family: theme.ui; font.pixelSize: 13
            elide: Text.ElideRight
        }
    }

    // ─── the wall — full-width Stremio grid (no side pane; a click opens the title) ───
    Item {
        anchors.top: noticeBar.visible ? noticeBar.bottom
                   : filterRow.visible ? filterRow.bottom
                   : masthead.bottom
        anchors.topMargin: 18
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom

        GridView {
            id: wall
            anchors.top: parent.top; anchors.bottom: parent.bottom
            // Always left+right anchored (never swapped for horizontalCenter — mixing anchor
            // TYPES on a toggle is a real Qt anchor conflict, confirmed at runtime, not just a
            // style choice). Default: zero margins, fills the host's full width exactly as
            // before (cellWidth stretches to consume any residual — the source of Biblio's
            // oversized/blurry cards). fixedGalleryWidth (gallery profile only): symmetric
            // margins eat the residual instead, so cellWidth can land on the exact token and
            // the grid is centered rather than left-packed.
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: (browser.fixedGalleryWidth && browser._galleryPosters)
                ? Math.max(0, Math.floor((parent.width - columnCount * cellWidth) / 2)) : 0
            anchors.rightMargin: (browser.fixedGalleryWidth && browser._galleryPosters)
                ? Math.max(0, Math.ceil((parent.width - columnCount * cellWidth) / 2)) : 0
            clip: true
            interactive: true
            pixelAligned: false
            boundsBehavior: Flickable.StopAtBounds
            focus: true
            keyNavigationEnabled: true
            // Classic keeps its exact prior tuning (~132px tiles, matching the Top-list rails);
            // gallery derives its stride/height from the gallery tokens (wider tiles + two-line title).
            // columnCount/cellWidth read the HOST's width (parent, not wall's own width) so they
            // stay well-defined when wall.width above is itself derived from columnCount*cellWidth.
            readonly property int columnCount: Math.max(3, Math.floor(parent.width / (browser._galleryPosters
                ? (browser._galleryMetrics.posterWidth + browser._galleryMetrics.cardGap) : 146)))
            cellWidth: (browser.fixedGalleryWidth && browser._galleryPosters)
                // "-14" below is the existing, unchanged delegate-inset convention (see the
                // delegate's width binding) — adding it back here is what makes the delegate land
                // on EXACTLY posterWidth, not floor(width/columnCount)'s residual-inflated value.
                ? (browser._galleryMetrics.posterWidth + 14)
                : Math.floor(parent.width / columnCount)
            cellHeight: browser._galleryPosters
                ? (Math.floor((cellWidth - 14) * browser._galleryMetrics.posterRatio)
                   + 10 + browser._galleryMetrics.titleMinHeight + 14 + 6)
                : (Math.floor(cellWidth * 1.62) + 34)
            cacheBuffer: cellHeight * 2
            // in-grid skeletons reserve EXACT cell space (no layout jump when art lands):
            // fill the viewport on the first page, one trailing row while paging.
            readonly property int skelCount: !browser.loading ? 0
                : (browser.items.length === 0
                   ? columnCount * Math.max(2, Math.ceil(height / cellHeight))
                   : columnCount)
            model: browser.items.length + skelCount
            ScrollBar.vertical: HouseScrollBar { flick: wall }
            onContentYChanged: {
                if (contentHeight > height
                    && contentY > contentHeight - height * 1.6)
                    browser.requestPage()
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                    || event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    browser.keyboardMode = true
                    event.accepted = false            // let GridView move currentIndex
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    browser.keyboardMode = true
                    browser.activateIndex(wall.currentIndex)
                    event.accepted = true
                } else if (event.key === Qt.Key_PageUp) {
                    wallGlide.pageUp(); event.accepted = true
                } else if (event.key === Qt.Key_PageDown) {
                    wallGlide.pageDown(); event.accepted = true
                } else if (event.key === Qt.Key_Home) {
                    wallGlide.toTop(); event.accepted = true
                } else if (event.key === Qt.Key_End) {
                    wallGlide.toBottom(); event.accepted = true
                }
            }

            // The poster tile is now the shared CataloguePosterCard (identical rendering);
            // the wall keeps ownership of skeleton math, keyboard focus, and activation.
            delegate: CataloguePosterCard {
                id: card
                required property int index
                readonly property bool isSkel: card.index >= browser.items.length
                // Automation identity (Lanista): a stable per-item name so the bridge can address
                // a specific card across delegate recycling. Derived from the item's own id (or
                // title as fallback), NEVER the row index — recycling reuses indices. Skeletons
                // stay unnamed. World-neutral by construction: whatever items the adapter serves.
                //
                // Manga cards (item.type === "manga", TankobanDiscoverApi.js normalizeManga) get
                // a WORLD-NAMESPACED "mangaDiscoverCard_"+malId name additionally (2026-08-14,
                // manga series bookshelf rebuild): this shell is instantiated separately by
                // Tankoban/Theatre/Biblio, so a bare "discoverCard_<id>" stem can resolve
                // DFS-first into an occluded card in another world's tree — the same trap
                // documented in MangaTankobanSourcesPage.qml's naming convention note. item.id
                // IS the MAL id for a manga card (normalizeManga: id = String(mal_id)), which is
                // exactly what MangaSeries.malId / TankobanWorld.mangaOpenById expect. Every
                // other card kind keeps its existing "discoverCard_" name unchanged.
                objectName: (!card.isSkel && card.item && (card.item.id || card.item.title))
                            ? ((card.item.type === "manga" ? "mangaDiscoverCard_" : "discoverCard_")
                               + String(card.item.id !== undefined && String(card.item.id).length > 0
                                        ? card.item.id : card.item.title))
                            : ""
                width: wall.cellWidth - 14
                height: wall.cellHeight - 14
                visualProfile: browser.posterVisualProfile
                showAuthorAtRest: browser.showAuthorAtRest
                hoverSourceText: (browser.showSourceOnReveal && card.item && card.item.source) ? card.item.source : ""
                revealOnFocus: browser.showSourceOnReveal
                skeleton: card.isSkel
                item: card.isSkel ? null : browser.items[card.index]
                keyboardFocused: !card.isSkel && browser.keyboardMode && card.index === wall.currentIndex
                onActivated: (it) => {
                    wall.forceActiveFocus()
                    browser.keyboardMode = false
                    wall.currentIndex = card.index
                    browser.itemOpenRequested(it)               // a click opens the title (Stremio)
                }
            }

            // honest empty state (skeletons now live in-grid, reserving exact cells). Data-vault
            // Slice 3: when the wall is empty because its catalogue db is still downloading, the
            // one quiet downloading line replaces the normal empty copy — same font/color, no
            // other visual change.
            Text {
                visible: !browser.loading && browser.items.length === 0
                anchors.centerIn: parent
                text: browser.catalogueDownloading ? browser._downloadingText : browser.emptyMessage
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
            }
        }

        // Shared wheel controller — unifies the wall's scroll feel with every landing page and
        // the See-all grids (fast accumulator drain, no double-scroll with GridView's wheel).
        ScrollGlide { id: wallGlide; flick: wall }
    }
}
