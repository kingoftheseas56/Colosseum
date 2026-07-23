// DiscoverPage — Stremio's Discover in house glass (Stage 1, spec 2026-07-23 §3).
// Lives as the Discover tab's content inside TheatreWorld's board. The region takes
// ~viewport height: wall (interactive GridView, skip-paging) left, preview pane right.
// Pickers derive PURELY from installed addon catalogs (DiscoverApi). First click on a
// poster fills the pane; second click (or ▶ Show) opens the detail page.
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
    property int selectedIndex: -1
    property int fetchGen: 0                // stale-response fence

    signal itemOpenRequested(var item)      // second click / Show — up to TheatreWorld

    Theme { id: theme }

    function applyPin(p) {
        pin = p || null
        if (!pin) return
        var res = Api.resolvePin(installed, pin)
        if (res.missing) {
            missingAddon = true; missingName = res.addonName; missingUrl = res.transportUrl
            items = []; selectedIndex = -1
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
        items = []; selectedIndex = -1; exhausted = false
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
            if (disco.selectedIndex < 0 && merged.length) disco.selectedIndex = 0
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
            options: disco.catalogs.map(function(c) {
                return { key: c.key, text: c.title, sub: c.addonName } })
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

    // ─── wall + pane ───
    Item {
        anchors.top: missingBar.visible ? missingBar.bottom : selectorRow.bottom
        anchors.topMargin: 18
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom

        GridView {
            id: wall
            anchors.left: parent.left
            anchors.right: pane.left; anchors.rightMargin: 24
            anchors.top: parent.top; anchors.bottom: parent.bottom
            clip: true
            interactive: true
            boundsBehavior: Flickable.StopAtBounds
            model: disco.items
            readonly property int columnCount: Math.max(3, Math.floor(width / 168))
            cellWidth: Math.floor(width / columnCount)
            cellHeight: Math.floor(cellWidth * 1.62) + 34
            cacheBuffer: cellHeight * 2
            ScrollBar.vertical: HouseScrollBar { flick: wall }
            onContentYChanged: {
                if (contentHeight > height
                    && contentY > contentHeight - height * 1.6)
                    disco.fetchMore()
            }

            delegate: Item {
                id: card
                required property var modelData
                required property int index
                width: wall.cellWidth - 14
                height: wall.cellHeight - 14

                Rectangle {
                    id: frame
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    height: Math.floor(width * 1.5)
                    radius: 6; clip: true
                    color: "#181a20"
                    border.width: 1
                    border.color: card.index === disco.selectedIndex ? theme.gold
                                 : hov.hovered ? Qt.rgba(1, 1, 1, 0.4) : theme.edge
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#343d52" }
                            GradientStop { position: 1; color: "#121620" }
                        }
                        Text {
                            anchors.centerIn: parent; width: parent.width - 20
                            text: card.modelData.title || card.modelData.caption || card.modelData.name || ""
                            color: Qt.rgba(1, 1, 1, 0.66)
                            font.family: theme.display; font.pixelSize: 15; font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap; maximumLineCount: 4; elide: Text.ElideRight
                        }
                    }
                    Image {
                        anchors.fill: parent
                        source: card.modelData.cover || card.modelData.poster || ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                    }
                }
                Text {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: frame.bottom; anchors.topMargin: 7
                    text: card.modelData.title || card.modelData.caption || card.modelData.name || ""
                    color: card.index === disco.selectedIndex ? theme.ink : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                HoverHandler { id: hov }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (disco.selectedIndex === card.index)
                            disco.itemOpenRequested(card.modelData)    // second click = the door
                        else
                            disco.selectedIndex = card.index           // first click = preview
                    }
                }
            }

            // skeletons while the first page loads
            Row {
                visible: disco.loading && disco.items.length === 0
                spacing: 16
                Repeater {
                    model: 5
                    Rectangle {
                        width: 150; height: 225; radius: 6
                        color: Qt.rgba(1, 1, 1, 0.07)
                        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.10)
                    }
                }
            }
            // honest empty state
            Text {
                visible: !disco.loading && disco.items.length === 0 && !disco.missingAddon
                anchors.centerIn: parent
                text: disco.currentCatalog ? "This catalogue answered with nothing."
                                           : "No catalogues here — install an addon that carries some."
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
            }
        }

        DiscoverPreview {
            id: pane
            anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: Math.min(380, parent.width * 0.34)
            meta: disco.selectedIndex >= 0 && disco.selectedIndex < disco.items.length
                  ? disco.items[disco.selectedIndex] : null
            onShowRequested: (item) => disco.itemOpenRequested(item)
        }
    }
}
