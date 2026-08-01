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

Item {
    id: browser

    // ── injected world seam ──
    property var adapter: null
    // the type to fall back to when the adapter offers none yet (bare construction / empty registry)
    property string fallbackType: ""

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

    function saveTypeState() {
        if (!currentType.length) return
        var s = {}
        for (var k in typeStates) s[k] = typeStates[k]
        s[currentType] = { catalogKey: currentCatalogKey, filterGroup: filterGroup, filterKey: filterKey,
                           items: items, cursor: cursor, exhausted: exhausted,
                           warning: warning, freshness: freshness, noticeText: noticeText }
        typeStates = s
    }

    function restoreTypeState(st) {
        currentCatalogKey = st.catalogKey
        filterGroup = st.filterGroup; filterKey = st.filterKey
        items = st.items; cursor = st.cursor; exhausted = st.exhausted
        warning = st.warning || ""; freshness = st.freshness || ""; noticeText = st.noticeText || ""
        loading = false
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
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top; anchors.bottom: parent.bottom
            clip: true
            interactive: true
            boundsBehavior: Flickable.StopAtBounds
            focus: true
            keyNavigationEnabled: true
            readonly property int columnCount: Math.max(3, Math.floor(width / 146))  // ~132px tiles, matching the Top-list rails
            cellWidth: Math.floor(width / columnCount)
            cellHeight: Math.floor(cellWidth * 1.62) + 34
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
                }
            }

            delegate: Item {
                id: card
                required property int index
                readonly property bool isSkel: card.index >= browser.items.length
                readonly property var item: card.isSkel ? null : browser.items[card.index]
                readonly property bool kfocused: !card.isSkel && browser.keyboardMode
                                                 && card.index === wall.currentIndex
                readonly property string capText: card.item ? (card.item.title || "") : ""
                readonly property string yearText: card.item ? String(card.item.year || "") : ""
                readonly property string ratingText: card.item ? String(card.item.rating || "") : ""
                width: wall.cellWidth - 14
                height: wall.cellHeight - 14

                Rectangle {
                    id: frame
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    height: Math.floor(width * 1.5)
                    radius: 8; clip: true
                    color: card.isSkel ? Qt.rgba(1, 1, 1, 0.06) : "#181a20"
                    border.width: 1
                    border.color: card.isSkel ? Qt.rgba(1, 1, 1, 0.09)
                                 : hov.hovered ? Qt.rgba(1, 1, 1, 0.42) : theme.edge
                    // hover lift — a render transform, so the grid geometry never shifts.
                    transform: Translate {
                        y: hov.hovered ? -4 : 0
                        Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                    }
                    // skeleton pulse — only while this is a placeholder
                    SequentialAnimation on opacity {
                        running: card.isSkel
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.5; to: 0.9; duration: 800; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 0.9; to: 0.5; duration: 800; easing.type: Easing.InOutSine }
                    }
                    Rectangle {
                        visible: !card.isSkel
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#343d52" }
                            GradientStop { position: 1; color: "#121620" }
                        }
                        Text {
                            anchors.centerIn: parent; width: parent.width - 20
                            text: card.capText
                            color: Qt.rgba(1, 1, 1, 0.66)
                            font.family: theme.display; font.pixelSize: 15; font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap; maximumLineCount: 4; elide: Text.ElideRight
                        }
                    }
                    Image {
                        visible: !card.isSkel
                        anchors.fill: parent
                        source: card.item ? (card.item.cover || "") : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                    }
                    // ── hover reveal: scrim + year·rating rise, a gold play ring appears ──
                    Item {
                        id: reveal
                        anchors.fill: parent
                        visible: !card.isSkel
                        opacity: hov.hovered ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 0.55; color: Qt.rgba(6/255, 5/255, 12/255, 0.30) }
                                GradientStop { position: 1.0; color: Qt.rgba(6/255, 5/255, 12/255, 0.92) }
                            }
                        }
                        Rectangle {                       // centered play ring
                            anchors.centerIn: parent
                            width: 46; height: 46; radius: 23
                            color: Qt.rgba(8/255, 7/255, 14/255, 0.34)
                            border.width: 1.5; border.color: theme.gold
                            Text {
                                anchors.centerIn: parent
                                anchors.horizontalCenterOffset: 2
                                text: "▶"; color: theme.gold; font.pixelSize: 16
                            }
                        }
                        Row {                             // meta, bottom-left
                            anchors.left: parent.left; anchors.leftMargin: 11
                            anchors.right: parent.right; anchors.rightMargin: 11
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 11
                            spacing: 9
                            Text {
                                visible: card.yearText.length > 0
                                text: card.yearText
                                color: theme.ink; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                            }
                            Text {
                                visible: card.ratingText.length > 0
                                text: "★ " + card.ratingText
                                color: theme.gold; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                            }
                        }
                    }
                }
                // keyboard focus ring — a DOUBLE soft-gold halo overlay
                Rectangle {
                    anchors.fill: frame; radius: 8
                    visible: card.kfocused
                    color: "transparent"
                    border.width: 2; border.color: Qt.rgba(240/255, 196/255, 74/255, 0.55)
                    Rectangle {
                        anchors.fill: parent; anchors.margins: -3
                        radius: 10; color: "transparent"
                        border.width: 3; border.color: Qt.rgba(240/255, 196/255, 74/255, 0.18)
                    }
                }
                Text {
                    visible: !card.isSkel
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: frame.bottom; anchors.topMargin: 8
                    text: card.capText
                    color: hov.hovered ? theme.ink : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                // skeleton title bar (reserves the title row's space too)
                Rectangle {
                    visible: card.isSkel
                    anchors.left: parent.left; anchors.top: frame.bottom; anchors.topMargin: 8
                    width: parent.width * 0.7; height: 12; radius: 5
                    color: Qt.rgba(1, 1, 1, 0.08)
                }
                HoverHandler { id: hov; enabled: !card.isSkel }
                MouseArea {
                    anchors.fill: parent
                    enabled: !card.isSkel
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        wall.forceActiveFocus()
                        browser.keyboardMode = false
                        wall.currentIndex = card.index
                        browser.itemOpenRequested(card.item)    // a click opens the title (Stremio)
                    }
                }
            }

            // honest empty state (skeletons now live in-grid, reserving exact cells)
            Text {
                visible: !browser.loading && browser.items.length === 0
                anchors.centerIn: parent
                text: browser.emptyMessage
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
            }
        }
    }
}
