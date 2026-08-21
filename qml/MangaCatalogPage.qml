// Top Manga — the complete MAL score-ranked manga wall (topmanga.php), rebuilt in
// the house glass of the retired ~1,100 Top Comics catalogue (057e109^). Data is
// the BAKED MyAnimeList dump via MalCatalog.topManga() — offline, never a live
// Jikan call, ranked by weighted score with a vote floor (~1,254 titles).
import QtQuick
import QtQuick.Controls

Item {
    id: root
    anchors.fill: parent

    property Item backdrop: null
    property var rows: []
    property var catalogRows: prepare(rows)
    property string query: ""
    property var visibleRows: filter(catalogRows, query)
    property real savedAllContentY: 0
    readonly property bool filterViewActive: query.trim().length > 0

    // MAL Top Manga filters — mirrors topmanga.php. sortMode picks the ranking,
    // typeFilter narrows by publication type. Both re-query the baked catalog.
    property string sortMode: "score"     // "score" (Top Rated) | "members" (Most Popular)
    property string typeFilter: ""        // "" (All) | Manga | Manhwa | Manhua | Novel | One-shot
    readonly property var sortModel: [
        { key: "score",   label: "Top Rated" },
        { key: "members", label: "Most Popular" }
    ]
    readonly property var typeModel: [
        { key: "",         label: "All" },
        { key: "Manga",    label: "Manga" },
        { key: "Manhwa",   label: "Manhwa" },
        { key: "Manhua",   label: "Manhua" },
        { key: "Novel",    label: "Novel" },
        { key: "One-shot", label: "One-shot" }
    ]

    onSortModeChanged: reload()
    onTypeFilterChanged: reload()

    // Pull the ranked catalog for the current sort+type straight from the baked
    // MalCatalog (the page's own scope DOES see the context property here). Search
    // stays a client-side filter over whatever the active tabs returned.
    function reload() {
        if (typeof MalCatalog === "undefined" || !MalCatalog.ready()) { root.rows = []; return }
        root.rows = MalCatalog.topManga(root.sortMode, root.typeFilter, 2000)
        catalogGrid.positionViewAtBeginning()
    }

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal seriesRequested(string title)

    Theme { id: theme }

    // The page owns its own query from the active tabs (Main's onLoaded may also
    // seed rows; reload() is authoritative and re-runs on every filter change).
    Component.onCompleted: reload()

    // rank each row by its position in the already-score-sorted list.
    // NOTE: a C++ QVariantList (MalCatalog.topManga) marshals into QML as an
    // array-LIKE sequence — it has .length and is indexable, but Array.isArray()
    // reports FALSE for it. Gating on Array.isArray silently drops every baked
    // row (the "0 ranked" bug). The working genre path (GenreApi.js) never gates
    // on Array.isArray for exactly this reason — it reads .length and iterates.
    function prepare(src) {
        var n = (src && src.length) ? src.length : 0
        var out = []
        for (var i = 0; i < n; i++) {
            var row = src[i]
            var o = {}
            for (var k in row) o[k] = row[k]
            o.displayRank = i + 1
            out.push(o)
        }
        return out
    }
    function filter(src, q) {
        var needle = String(q || "").trim().toLowerCase()
        var arr = Array.isArray(src) ? src : []
        if (!needle.length) return arr
        return arr.filter(function(row) {
            var t = String(row.title || "").toLowerCase()
            var e = String(row.title_english || "").toLowerCase()
            return t.indexOf(needle) >= 0 || e.indexOf(needle) >= 0
        })
    }

    function applyView(nextQuery) {
        var wasFiltered = root.filterViewActive
        var cleanQuery = String(nextQuery || "")
        var willFilter = cleanQuery.trim().length > 0
        if (!wasFiltered && willFilter)
            savedAllContentY = catalogGrid.contentY
        root.query = cleanQuery
        if (searchField.text !== cleanQuery)
            searchField.text = cleanQuery
        if (wasFiltered && !willFilter)
            Qt.callLater(function() { catalogGrid.contentY = Math.max(0, savedAllContentY) })
        else if (willFilter)
            Qt.callLater(function() { catalogGrid.positionViewAtBeginning() })
    }
    function clearView() { applyView("") }

    Item {
        id: wallpaper
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.025, 0.03, 0.045, 0.82) }
    }

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 198
        z: 20
        color: Qt.rgba(0.025, 0.03, 0.045, 0.82)

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.edge
        }

        Column {
            anchors.left: parent.left
            anchors.leftMargin: Math.max(64, theme.margin)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 27
            spacing: 4
            Text {
                text: "TANKOBAN · MANGA"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
                font.letterSpacing: 2.6
                font.weight: Font.DemiBold
            }
            Text {
                text: "Top Manga"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 48
                font.letterSpacing: -1
            }
            Text {
                text: root.visibleRows.length === root.catalogRows.length
                      ? root.catalogRows.length + " ranked"
                      : root.visibleRows.length + " of " + root.catalogRows.length + " ranked"
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 14
            }
        }

        TextField {
            id: searchField
            anchors.right: parent.right
            anchors.rightMargin: Math.max(54, theme.margin)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 32
            width: 260
            height: 38
            leftPadding: 16
            rightPadding: 16
            placeholderText: "Search manga"
            placeholderTextColor: theme.inkDimmer
            color: theme.ink
            selectionColor: theme.gold
            selectedTextColor: "#111111"
            font.family: theme.ui
            font.pixelSize: 14
            background: Rectangle {
                radius: 19
                color: Qt.rgba(0.03, 0.04, 0.06, 0.62)
                border.width: 1
                border.color: searchField.activeFocus ? theme.gold : theme.edge
            }
            onTextEdited: root.applyView(text)
            Keys.onEscapePressed: root.clearView()
        }
    }

    // Filter row — MAL topmanga.php: ranking pills (Top Rated / Most Popular) then
    // a divider then type pills (All / Manga / Manhwa / Manhua / Novel / One-shot).
    Item {
        id: filterBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        height: 58
        z: 19

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0.025, 0.03, 0.045, 0.66)
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.edge
        }

        // one pill delegate reused by both groups
        component FilterPill: Rectangle {
            property string label: ""
            property bool active: false
            signal picked()
            implicitWidth: pillText.implicitWidth + 30
            height: 34
            radius: 14
            color: active ? theme.gold : (pillHover.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
            border.width: active ? 0 : 1
            border.color: Qt.rgba(1, 1, 1, 0.14)
            Behavior on color { ColorAnimation { duration: 130 } }
            Text {
                id: pillText
                anchors.centerIn: parent
                text: parent.label
                color: parent.active ? "#17120a" : theme.ink
                font.family: theme.ui
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            HoverHandler { id: pillHover }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: parent.picked()
            }
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Math.max(64, theme.margin)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Repeater {
                model: root.sortModel
                delegate: FilterPill {
                    required property var modelData
                    label: modelData.label
                    active: root.sortMode === modelData.key
                    onPicked: root.sortMode = modelData.key
                }
            }

            Rectangle {
                width: 1
                height: 22
                anchors.verticalCenter: parent.verticalCenter
                color: theme.edge
            }

            Repeater {
                model: root.typeModel
                delegate: FilterPill {
                    required property var modelData
                    label: modelData.label
                    active: root.typeFilter === modelData.key
                    onPicked: root.typeFilter = modelData.key
                }
            }
        }
    }

    GridView {
        id: catalogGrid
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: filterBar.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Math.max(48, theme.margin)
        anchors.rightMargin: Math.max(38, theme.margin - 10)
        anchors.topMargin: 22
        anchors.bottomMargin: 18
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.visibleRows
        readonly property int columnCount: Math.max(4, Math.floor(width / 188))
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.floor(cellWidth * 1.72)
        cacheBuffer: cellHeight * 2
        ScrollBar.vertical: HouseScrollBar { flick: catalogGrid }

        delegate: Item {
            id: card
            required property var modelData
            required property int index
            width: catalogGrid.cellWidth - 16
            height: catalogGrid.cellHeight - 18
            x: 8

            property real coverHeight: Math.min(height - 54, width * 1.43)

            Rectangle {
                id: coverFrame
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: card.coverHeight
                radius: 5
                clip: true
                color: "#1b1d22"
                border.width: 1
                border.color: cardHover.hovered ? theme.gold : theme.edge
                scale: cardHover.hovered ? 1.018 : 1
                transformOrigin: Item.Bottom
                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#3c4658" }
                        GradientStop { position: 1; color: "#141820" }
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 24
                        text: card.modelData.title || "Untitled"
                        color: Qt.rgba(1, 1, 1, 0.72)
                        font.family: theme.display
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                    }
                }

                Image {
                    id: coverImage
                    anchors.fill: parent
                    source: card.modelData.cover || ""
                    fillMode: Image.PreserveAspectCrop
                    verticalAlignment: Image.AlignTop
                    asynchronous: true
                    cache: true
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 180 } }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 76
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.58) }
                        GradientStop { position: 1; color: "transparent" }
                    }
                }
                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 10
                    anchors.topMargin: 2
                    text: card.modelData.displayRank
                    color: Qt.rgba(1, 1, 1, 0.48)
                    font.family: theme.display
                    font.pixelSize: 48
                    font.weight: Font.Bold
                }
                Row {
                    visible: !!card.modelData.score
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    spacing: 3
                    Text { text: "★"; color: theme.gold; font.pixelSize: 12 }
                    Text {
                        text: Number(card.modelData.score).toFixed(2)
                        color: "#ffffff"
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: coverFrame.bottom
                anchors.topMargin: 9
                text: card.modelData.title || "Untitled"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 16
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            HoverHandler { id: cardHover }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.seriesRequested(card.modelData.title || "")
            }
        }
    }

    ScrollGlide { flick: catalogGrid }

    Column {
        anchors.centerIn: catalogGrid
        spacing: 14
        visible: root.catalogRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Catalog unavailable"
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 30
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "The manga catalog could not be loaded."
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
        }
    }

    Column {
        anchors.centerIn: catalogGrid
        spacing: 14
        visible: root.catalogRows.length > 0 && root.visibleRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "No manga match this search"
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 30
        }
    }

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 52
        z: 30
        BackAction {
            variant: "capsule"
            tip: "Back"
            anchors.left: parent.left
            anchors.leftMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: root.backRequested()
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 25
            anchors.verticalCenter: parent.verticalCenter
            spacing: 18
            Item {
                width: 17
                height: 17
                Image {
                    anchors.fill: parent
                    source: "../assets/icons/minimize.svg"
                    opacity: minimizeHover.hovered ? 1 : 0.7
                }
                HoverHandler { id: minimizeHover }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.minimizeRequested()
                }
            }
            Item {
                width: 17
                height: 17
                Image {
                    anchors.fill: parent
                    source: "../assets/icons/power.svg"
                    opacity: powerHover.hovered ? 1 : 0.7
                }
                HoverHandler { id: powerHover }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeRequested()
                }
            }
        }
    }
}
