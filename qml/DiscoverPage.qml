// DiscoverPage — Stremio's Discover in house glass (Stage 1, spec 2026-07-23 §3).
// Lives as the Discover tab's content inside TheatreWorld's board. A FULL-WIDTH poster
// grid (interactive GridView, skip-paging) — no side pane (Hemanth's call 2026-06-24:
// the preview pane was a bad choice). Pickers derive PURELY from installed addon catalogs
// (DiscoverApi). A click (or Enter on the keyboard-focused card) opens the detail page.
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

    Connections {
        target: (typeof Extensions !== "undefined") ? Extensions : null
        function onChanged() { disco.refreshFromRegistry() }
    }

    Component.onCompleted: {
        if (pin) applyPin(pin)
        else setType(Api.typesFor(installed).length ? Api.typesFor(installed)[0] : "movie")
    }

    // ─── selector row ───
    Row {
        id: selectorRow
        spacing: 10
        DiscoverPicker {
            options: Api.typesFor(disco.installed).map(function(t) {
                return { key: t, text: Api.typeLabel(t), sub: "" } })
            currentKey: disco.currentType
            onPicked: (key) => disco.setType(key)
        }
        DiscoverPicker {
            // catalog rows split into sections: core Cinemeta first (by addon name),
            // everything else under "Your addons" — the mock's attribution anatomy.
            options: {
                var out = []; var lastSec = null;
                for (var i = 0; i < disco.catalogs.length; i++) {
                    var c = disco.catalogs[i];
                    var sec = c.core ? c.addonName : "Your addons";
                    if (sec !== lastSec) { out.push({ header: sec }); lastSec = sec; }
                    out.push({ key: c.key, text: c.title, sub: c.addonName });
                }
                return out;
            }
            currentKey: disco.currentCatalog ? disco.currentCatalog.key : ""
            onPicked: (key) => {
                for (var i = 0; i < disco.catalogs.length; i++)
                    if (disco.catalogs[i].key === key) { disco.setCatalog(i); break }
            }
        }
        Repeater {
            model: disco.extras
            DiscoverPicker {
                required property var modelData
                label: modelData.label
                options: {
                    var opts = modelData.isRequired ? [] :
                        [ { key: "", text: "All", sub: "" } ]
                    return opts.concat(modelData.options.map(function(o) {
                        return { key: o, text: o, sub: "" } }))
                }
                currentKey: disco.selections[modelData.name] || ""
                onPicked: (key) => disco.setSelection(modelData.name, key.length ? key : null)
            }
        }
    }

    // ─── missing-addon bar (a pinned catalog whose addon is gone) ───
    Rectangle {
        id: missingBar
        visible: disco.missingAddon
        anchors.top: selectorRow.bottom; anchors.topMargin: 14
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
        anchors.top: missingBar.visible ? missingBar.bottom : selectorRow.bottom
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
            readonly property int columnCount: Math.max(3, Math.floor(width / 168))
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
                width: wall.cellWidth - 14
                height: wall.cellHeight - 14

                Rectangle {
                    id: frame
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    height: Math.floor(width * 1.5)
                    radius: 6; clip: true
                    color: card.isSkel ? Qt.rgba(1, 1, 1, 0.06) : "#181a20"
                    border.width: 1
                    border.color: card.isSkel ? Qt.rgba(1, 1, 1, 0.09)
                                 : hov.hovered ? Qt.rgba(1, 1, 1, 0.42) : theme.edge
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
                }
                // keyboard focus ring — a DOUBLE soft-gold halo, drawn as an overlay so it
                // reads distinctly from the solid gold selection border (mock .kfocus)
                Rectangle {
                    anchors.fill: frame; radius: 6
                    visible: card.kfocused
                    color: "transparent"
                    border.width: 2; border.color: Qt.rgba(240/255, 196/255, 74/255, 0.55)
                    Rectangle {
                        anchors.fill: parent; anchors.margins: -3
                        radius: 8; color: "transparent"
                        border.width: 3; border.color: Qt.rgba(240/255, 196/255, 74/255, 0.18)
                    }
                }
                Text {
                    visible: !card.isSkel
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: frame.bottom; anchors.topMargin: 7
                    text: card.capText
                    color: hov.hovered ? theme.ink : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                // skeleton title bar (reserves the title row's space too)
                Rectangle {
                    visible: card.isSkel
                    anchors.left: parent.left; anchors.top: frame.bottom; anchors.topMargin: 7
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
