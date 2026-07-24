// DiscoverPage — Stremio's Discover in house glass (Stage 1, spec 2026-07-23 §3).
// Lives as the Discover tab's content inside TheatreWorld's board. A FULL-WIDTH poster
// grid (interactive GridView, skip-paging) — no side pane (Hemanth's call 2026-06-24:
// the preview pane was a bad choice). Pickers derive PURELY from installed addon catalogs
// (DiscoverApi). A click (or Enter on the keyboard-focused card) opens the detail page.
//
// Overhaul 2026-07-25 (Agent 5, frontend-design, Hemanth-approved mock): the page gained a
// MASTHEAD — a type "lens" as underlined text tabs on the left (deliberately NOT filled pills,
// which mimicked the tab bar), and the current shelf NAMED on the right in the editorial serif,
// that name doubling as the catalog picker. Genuine filters became gold-active chips. The wall's
// cards became cinematic: hover lifts the poster and reveals year·rating over a scrim with a play
// ring. The picker/derivation logic underneath (Agent 0's) is untouched.
import QtQuick
import QtQuick.Controls
import "DiscoverApi.js" as Api

Item {
    id: disco

    // Main pushes nothing here: the page reads the Extensions context property live.
    property int registryRev: (typeof Extensions !== "undefined" ? (Extensions.changed, 0) : 0)
    property var installed: (typeof Extensions !== "undefined") ? Extensions.installed() : []

    // an optional See-all pin: { transportUrl, type, catalogId, addonName }
    property var pin: null

    property string currentType: "movie"
    property var catalogs: Api.catalogsFor(installed, currentType)
    property int catalogIndex: 0
    readonly property var currentCatalog: catalogIndex >= 0 && catalogIndex < catalogs.length
                                          ? catalogs[catalogIndex] : null
    property var extras: currentCatalog ? Api.extrasFor(currentCatalog) : []
    property var selections: ({})

    property var items: []
    property bool loading: false
    property bool exhausted: false
    property bool missingAddon: false
    property string missingName: ""
    property string missingUrl: ""
    property int fetchGen: 0                // stale-response fence
    property bool keyboardMode: false       // true once arrows are used → shows the focus ring
    property bool catalogMenuOpen: false    // the Fraunces shelf-name doubles as the catalog picker

    // catalog picker rows, sectioned: core Cinemeta first (by addon name), the rest under
    // "Your addons" — the same attribution anatomy the old catalog pill carried.
    readonly property var catalogMenuModel: {
        var out = []; var lastSec = null;
        for (var i = 0; i < catalogs.length; i++) {
            var c = catalogs[i];
            var sec = c.core ? c.addonName : "Your addons";
            if (sec !== lastSec) { out.push({ header: sec }); lastSec = sec; }
            out.push({ key: c.key, text: c.title, sub: c.addonName });
        }
        return out;
    }

    signal itemOpenRequested(var item)      // a click / Enter on a poster opens the detail page

    Theme { id: theme }

    // Stremio grid: a click (or Enter on the keyboard-focused card) opens the title directly.
    function activateIndex(i) {
        if (i < 0 || i >= items.length) return         // skeletons/out-of-range never activate
        itemOpenRequested(items[i])
    }

    onVisibleChanged: if (visible && wall) wall.forceActiveFocus()

    function applyPin(p) {
        pin = p || null
        if (!pin) return
        var res = Api.resolvePin(installed, pin)
        if (res.missing) {
            missingAddon = true; missingName = res.addonName; missingUrl = res.transportUrl
            items = []
            return
        }
        missingAddon = false
        currentType = res.catalog.type
        var cats = Api.catalogsFor(installed, currentType)
        for (var i = 0; i < cats.length; i++)
            if (cats[i].key === res.catalog.key) { setCatalog(i, cats); return }
    }

    function setType(t) {
        currentType = t
        catalogs = Api.catalogsFor(installed, t)
        setCatalog(0, catalogs)
    }

    function setCatalog(idx, cats) {
        if (cats !== undefined) catalogs = cats
        catalogIndex = idx
        extras = currentCatalog ? Api.extrasFor(currentCatalog) : []
        selections = Api.defaultSelections(extras)
        missingAddon = false
        reload()
    }

    function setSelection(name, value) {
        var next = {}
        for (var k in selections) next[k] = selections[k]
        next[name] = value
        selections = next
        reload()
    }

    function reload() {
        items = []; exhausted = false
        fetchMore()
    }

    function fetchMore() {
        if (loading || exhausted || !currentCatalog) return
        loading = true
        var gen = ++fetchGen
        var offset = items.length
        Api.loadPage(currentCatalog, selections, offset, function(metas) {
            if (gen !== disco.fetchGen) return          // a newer ask superseded this one
            disco.loading = false
            if (!metas.length) { disco.exhausted = true; return }
            var merged = disco.items.slice()
            for (var i = 0; i < metas.length; i++) merged.push(metas[i])
            disco.items = merged
        })
    }

    function refreshFromRegistry() {
        installed = (typeof Extensions !== "undefined") ? Extensions.installed() : []
        if (pin) { applyPin(pin); return }
        var keep = currentCatalog ? currentCatalog.key : ""
        catalogs = Api.catalogsFor(installed, currentType)
        for (var i = 0; i < catalogs.length; i++)
            if (catalogs[i].key === keep) { catalogIndex = i; return }
        setCatalog(0)
    }

    // Discover pickers are mutually exclusive: opening one closes the rest, so a single
    // tap never leaves multiple popups stacked over the wall (Hemanth 2026-07-24). The
    // Repeater's filter pickers land in filterRow.children too; a child without an
    // `open` property (the Repeater item itself) is skipped. The catalog menu (the
    // Fraunces shelf name) is closed alongside — it's the same family of drop-downs.
    function closePickersExcept(keep) {
        catalogMenuOpen = false
        for (var i = 0; i < filterRow.children.length; i++) {
            var c = filterRow.children[i]
            if (c !== keep && c.open !== undefined) c.open = false
        }
    }
    // opening the catalog menu closes every filter picker
    onCatalogMenuOpenChanged: if (catalogMenuOpen) {
        for (var i = 0; i < filterRow.children.length; i++) {
            var c = filterRow.children[i]
            if (c.open !== undefined) c.open = false
        }
    }

    Connections {
        target: (typeof Extensions !== "undefined") ? Extensions : null
        function onChanged() { disco.refreshFromRegistry() }
    }

    Component.onCompleted: {
        if (pin) applyPin(pin)
        else setType(Api.typesFor(installed).length ? Api.typesFor(installed)[0] : "movie")
    }

    // ═══ masthead — the missing "place": type lens (left) + named shelf (right) ═══
    // z above the wall: the catalog menu drops INTO the wall region and must paint over it.
    Item {
        id: masthead
        z: 100
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 76

        // the hairline the whole row sits on
        Rectangle {
            id: mastheadRule
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Qt.rgba(1, 1, 1, 0.09)
        }

        // ── type lens: underlined text tabs (NOT filled pills — those mimicked the tab bar) ──
        Row {
            id: typeSwitch
            anchors.left: parent.left
            anchors.bottom: mastheadRule.top
            anchors.bottomMargin: 12
            spacing: 28
            Repeater {
                model: Api.typesFor(disco.installed)
                delegate: Item {
                    id: typeTab
                    required property var modelData
                    readonly property bool active: disco.currentType === typeTab.modelData
                    width: tlabel.implicitWidth
                    height: tlabel.implicitHeight + 9
                    Text {
                        id: tlabel
                        anchors.top: parent.top
                        text: Api.typeLabel(typeTab.modelData)
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
                        onClicked: disco.setType(typeTab.modelData)
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
                    text: disco.currentCatalog ? disco.currentCatalog.title : "—"
                    color: (catMa.containsMouse || disco.catalogMenuOpen) ? "#ffffff" : theme.ink
                    font.family: theme.display; font.pixelSize: 30; font.weight: Font.DemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▾"
                    color: (catMa.containsMouse || disco.catalogMenuOpen) ? theme.gold : theme.inkDimmer
                    font.pixelSize: 14
                }
            }
            MouseArea {
                id: catMa
                anchors.fill: nameRow
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                enabled: disco.catalogs.length > 0
                onClicked: disco.catalogMenuOpen = !disco.catalogMenuOpen
            }
            Text {
                id: byline
                anchors.right: parent.right
                anchors.top: nameRow.bottom; anchors.topMargin: 7
                // honest attribution — the owning addon, plus any active filter. NO invented
                // total: the catalog is paged, its true size is never known up front.
                text: {
                    var a = disco.currentCatalog ? disco.currentCatalog.addonName : ""
                    var f = ""
                    for (var k in disco.selections)
                        if (disco.selections[k]) { f = disco.selections[k]; break }
                    return f.length ? (a + "   ·   " + f) : a
                }
                color: theme.inkDim
                font.family: theme.ui; font.pixelSize: 13
            }
        }

        // ── catalog menu — the shelf-name drop-down (sectioned, gold-active) ──
        Rectangle {
            id: catalogMenu
            visible: disco.catalogMenuOpen
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
                model: disco.catalogMenuModel
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
                        readonly property bool sel: !mopt.isHeader && !!disco.currentCatalog
                                                    && mopt.modelData.key === disco.currentCatalog.key
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
                                disco.catalogMenuOpen = false
                                for (var i = 0; i < disco.catalogs.length; i++)
                                    if (disco.catalogs[i].key === mopt.modelData.key) { disco.setCatalog(i); break }
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
        visible: disco.catalogMenuOpen
        onClicked: disco.catalogMenuOpen = false
    }

    // ═══ filter chips — only the genuine filters (Genre + any addon filter) ═══
    Row {
        id: filterRow
        z: 90
        visible: disco.extras.length > 0
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
        Repeater {
            model: disco.extras
            DiscoverPicker {
                id: extraPicker
                required property var modelData
                label: modelData.label
                clearable: !modelData.isRequired
                options: {
                    var opts = modelData.isRequired ? [] :
                        [ { key: "", text: "All", sub: "" } ]
                    return opts.concat(modelData.options.map(function(o) {
                        return { key: o, text: o, sub: "" } }))
                }
                currentKey: disco.selections[modelData.name] || ""
                onPicked: (key) => disco.setSelection(modelData.name, key.length ? key : null)
                onCleared: disco.setSelection(modelData.name, null)
                onOpenChanged: if (open) disco.closePickersExcept(extraPicker)
            }
        }
    }

    // ─── missing-addon bar (a pinned catalog whose addon is gone) ───
    Rectangle {
        id: missingBar
        visible: disco.missingAddon
        anchors.top: filterRow.visible ? filterRow.bottom : masthead.bottom
        anchors.topMargin: 14
        width: parent.width; height: visible ? 52 : 0
        radius: 12
        color: Qt.rgba(240/255, 196/255, 74/255, 0.08)
        border.width: 1; border.color: Qt.rgba(240/255, 196/255, 74/255, 0.4)
        Row {
            anchors.left: parent.left; anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14
            Text {
                text: "This catalogue needs the " + (disco.missingName || "missing") + " addon."
                color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {
                width: instTxt.implicitWidth + 26; height: 32; radius: 9
                color: theme.gold
                Text { id: instTxt; anchors.centerIn: parent; text: "Install"
                       color: "#17120a"; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: if (typeof Extensions !== "undefined") Extensions.install(disco.missingUrl)
                }
            }
        }
    }

    // ─── the wall — full-width Stremio grid (no side pane; a click opens the title) ───
    Item {
        anchors.top: missingBar.visible ? missingBar.bottom
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
            readonly property int columnCount: Math.max(3, Math.floor(width / 146))  // ~132px tiles, matching the Top-list rails (Hemanth 2026-07-25)
            cellWidth: Math.floor(width / columnCount)
            cellHeight: Math.floor(cellWidth * 1.62) + 34
            cacheBuffer: cellHeight * 2
            // in-grid skeletons reserve EXACT cell space (no layout jump when art lands):
            // fill the viewport on the first page, one trailing row while paging.
            readonly property int skelCount: !disco.loading ? 0
                : (disco.items.length === 0
                   ? columnCount * Math.max(2, Math.ceil(height / cellHeight))
                   : columnCount)
            model: disco.items.length + skelCount
            ScrollBar.vertical: HouseScrollBar { flick: wall }
            onContentYChanged: {
                if (contentHeight > height
                    && contentY > contentHeight - height * 1.6)
                    disco.fetchMore()
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                    || event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    disco.keyboardMode = true
                    event.accepted = false            // let GridView move currentIndex
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    disco.keyboardMode = true
                    disco.activateIndex(wall.currentIndex)
                    event.accepted = true
                }
            }

            delegate: Item {
                id: card
                required property int index
                readonly property bool isSkel: card.index >= disco.items.length
                readonly property var item: card.isSkel ? null : disco.items[card.index]
                readonly property bool kfocused: !card.isSkel && disco.keyboardMode
                                                 && card.index === wall.currentIndex
                readonly property string capText: card.item
                    ? (card.item.title || card.item.caption || card.item.name || "") : ""
                // honest extras — shown only when the catalog actually carries them
                readonly property string yearText: card.item
                    ? String(card.item.releaseInfo || card.item.year || "") : ""
                readonly property string ratingText: card.item
                    ? String(card.item.imdbRating || "") : ""
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
                    // hover lift — the poster rises off its title (a render transform, so the
                    // grid geometry never shifts). Keyboard focus doesn't hover, so it never lifts.
                    transform: Translate {
                        y: hov.hovered ? -4 : 0
                        Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                    }
                    // skeleton pulse (mock @keyframes pulse) — only while this is a placeholder
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
                        source: card.item ? (card.item.cover || card.item.poster || "") : ""
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
                // keyboard focus ring — a DOUBLE soft-gold halo, drawn as an overlay so it
                // reads distinctly from the solid gold selection border (mock .kfocus)
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
                        disco.keyboardMode = false
                        wall.currentIndex = card.index
                        disco.itemOpenRequested(card.item)    // a click opens the title (Stremio)
                    }
                }
            }

            // honest empty state (skeletons now live in-grid, reserving exact cells)
            Text {
                visible: !disco.loading && disco.items.length === 0 && !disco.missingAddon
                anchors.centerIn: parent
                text: disco.currentCatalog ? "This catalogue answered with nothing."
                                           : "No catalogues here — install an addon that carries some."
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
            }
        }
    }
}
