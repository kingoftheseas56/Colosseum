// Top Comics Explore — the complete RCO-ranked, LOCG-resolved comics catalog.
import QtQuick
import QtQuick.Controls
import "ComicCatalogModel.js" as CatalogModel
import "ComicsDb.js" as ComicsDb

Item {
    id: root
    anchors.fill: parent

    property Item backdrop: null
    property var rows: []
    property var catalogRows: CatalogModel.prepare(rows, ComicsDb.hasDownloadableEdition)
    property string query: ""
    property bool downloadableOnly: false
    property string genre: ""            // genre shelf scope — set by the Explore-by-Genre mosaic
    property var visibleRows: CatalogModel.filter(catalogRows, query, downloadableOnly, genre)
    property real savedAllContentY: 0
    readonly property bool filterViewActive: query.trim().length > 0 || downloadableOnly

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal seriesRequested(var series)

    Theme { id: theme }

    function applyView(nextQuery, nextDownloadableOnly) {
        var wasFiltered = root.filterViewActive
        var cleanQuery = String(nextQuery || "")
        var willFilter = cleanQuery.trim().length > 0 || !!nextDownloadableOnly
        if (!wasFiltered && willFilter)
            savedAllContentY = catalogGrid.contentY
        root.query = cleanQuery
        root.downloadableOnly = !!nextDownloadableOnly
        if (searchField.text !== cleanQuery)
            searchField.text = cleanQuery
        if (wasFiltered && !willFilter) {
            Qt.callLater(function() {
                catalogGrid.contentY = Math.max(0, savedAllContentY)
            })
        } else if (willFilter) {
            Qt.callLater(function() { catalogGrid.positionViewAtBeginning() })
        }
    }

    function clearView() {
        applyView("", false)
    }

    component FilterPill: Rectangle {
        id: pill
        property string text: ""
        property bool selected: false
        signal triggered()
        width: pillLabel.implicitWidth + 28
        height: 32
        radius: 16
        color: selected ? Qt.rgba(0.94, 0.77, 0.29, 0.18) : Qt.rgba(0.04, 0.05, 0.07, 0.46)
        border.width: 1
        border.color: selected ? theme.gold : theme.edge

        Text {
            id: pillLabel
            anchors.centerIn: parent
            text: pill.text
            color: pill.selected ? theme.gold : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        HoverHandler { id: pillHover }
        scale: pillHover.hovered ? 1.03 : 1
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.triggered()
        }
    }

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
        Image {
            anchors.fill: parent
            visible: root.backdrop === null
            source: "../assets/wallpaper/captured-motion.jpg"
            fillMode: Image.PreserveAspectCrop
            cache: true
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
        border.width: 0

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
                text: "TANKOBAN · COMICS"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
                font.letterSpacing: 2.6
                font.weight: Font.DemiBold
            }
            Text {
                text: root.genre.length ? root.genre + " — Comics" : "Top Comics"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 48
                font.letterSpacing: -1
            }
            Text {
                text: root.visibleRows.length === root.catalogRows.length
                      ? root.catalogRows.length + " ranked series"
                      : root.visibleRows.length + " of " + root.catalogRows.length + " ranked series"
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 14
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Math.max(54, theme.margin)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 32
            spacing: 10

            TextField {
                id: searchField
                width: 260
                height: 38
                leftPadding: 16
                rightPadding: 16
                placeholderText: "Search comics"
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
                onTextEdited: root.applyView(text, root.downloadableOnly)
                Keys.onEscapePressed: root.clearView()
            }

            FilterPill {
                text: "All"
                selected: !root.downloadableOnly
                onTriggered: root.applyView(root.query, false)
            }
            FilterPill {
                text: "Downloadable"
                selected: root.downloadableOnly
                onTriggered: root.applyView(root.query, true)
            }
        }
    }

    GridView {
        id: catalogGrid
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Math.max(48, theme.margin)
        anchors.rightMargin: Math.max(38, theme.margin - 10)
        anchors.topMargin: 28
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
                        text: card.modelData.title || card.modelData.caption || "Untitled"
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
                Rectangle {
                    visible: !!card.modelData.downloadable
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 11
                    width: 9
                    height: 9
                    radius: 5
                    color: theme.gold
                    border.width: 1
                    border.color: Qt.rgba(0, 0, 0, 0.42)
                }
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: coverFrame.bottom
                anchors.topMargin: 9
                text: card.modelData.title || card.modelData.caption || "Untitled"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 16
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: text.length > 0
                text: card.modelData.publisher || ""
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                elide: Text.ElideRight
            }

            HoverHandler { id: cardHover }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.seriesRequested({
                    id: card.modelData.locgId,
                    title: card.modelData.title || card.modelData.caption || "",
                    cover: card.modelData.cover || ""
                })
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
            text: "The comics shelf could not be loaded."
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
        }
        FilterPill {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Back to Tankoban"
            selected: true
            onTriggered: root.backRequested()
        }
    }

    Column {
        anchors.centerIn: catalogGrid
        spacing: 14
        visible: root.catalogRows.length > 0 && root.visibleRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "No comics match this view"
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 30
        }
        FilterPill {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Clear search and filters"
            selected: true
            onTriggered: root.clearView()
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
