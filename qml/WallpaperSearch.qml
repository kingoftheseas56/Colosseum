pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "WallpaperApi.js" as WallpaperApi

Item {
    id: root

    Theme { id: theme }

    property Item backdrop
    property string targetWorld: "Home"
    property string inheritedImageUrl: ""
    property var selectedPick: ({})
    property var results: []
    property string statusText: ""
    // Axis 1+2 (2026-07-18; Konachan dropped 2026-07-20): paged Wallhaven search.
    // One opaque state from WallpaperApi carries the cursor; Load more appends pages.
    property var searchState: null
    property string sorting: "relevance"     // "relevance" | "top" | "random"
    property bool loading: false
    readonly property bool canLoadMore: !loading && searchState !== null
                                        && WallpaperApi.hasMore(searchState)

    signal closeRequested()
    signal applyRequested(string scope, string world, var pick)

    function runSearch() {
        statusText = "Searching..."
        results = []
        searchState = WallpaperApi.freshState(searchField.text, sorting)
        fetchMore()
    }

    function fetchMore() {
        if (loading || !searchState)
            return
        loading = true
        var state = searchState
        WallpaperApi.fetchPage(state, function(rows, st, err) {
            if (state !== root.searchState) { root.loading = false; return }   // a newer search superseded this reply
            root.results = root.results.concat(rows)
            root.loading = false
            root.statusText = err || (root.results.length + " wallpapers"
                                      + (WallpaperApi.hasMore(st) ? " · more available" : ""))
        })
    }

    function setSorting(mode) {
        // Random re-rolls on every press (fresh seed) — that's its point.
        if (sorting === mode && mode !== "random")
            return
        sorting = mode
        runSearch()
    }

    Component.onCompleted: {
        searchField.text = WallpaperApi.defaultQueryFor(targetWorld)
        runSearch()
    }

    // full-bleed preview: an Image for searchable picks, the live scene for native ones
    readonly property string previewUrl: root.selectedPick.image_url || root.inheritedImageUrl
    Image {
        anchors.fill: parent
        source: WallpaperApi.isNativePick(root.previewUrl) ? "" : root.previewUrl
        visible: !WallpaperApi.isNativePick(root.previewUrl)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
    }
    Loader {
        anchors.fill: parent
        active: WallpaperApi.isNativePick(root.previewUrl)
        source: active ? WallpaperApi.nativeSceneFor(root.previewUrl) : ""
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.34)
    }

    Glass {
        id: panel
        backdrop: root.backdrop
        width: Math.min(520, root.width - 80)
        height: root.height - 120
        x: 40
        y: 60
        radius: 18

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 14

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "Wallpapers"
                    color: "#f3f0e8"
                    font.family: theme.display
                    font.pixelSize: 34
                    Layout.fillWidth: true
                }

                Text {
                    text: "x"
                    color: closeMa.containsMouse ? "#ffffff" : "#aaa7a0"
                    font.pixelSize: 20

                    MouseArea {
                        id: closeMa
                        anchors.fill: parent
                        anchors.margins: -10
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeRequested()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    radius: 999
                    color: Qt.rgba(0, 0, 0, 0.28)
                    border.width: 1
                    border.color: Qt.rgba(255, 255, 255, 0.16)

                    TextInput {
                        id: searchField
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#f7f4ee"
                        selectionColor: "#c9a44a"
                        selectedTextColor: "#101010"
                        font.pixelSize: 15
                        Keys.onReturnPressed: root.runSearch()
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 92
                    Layout.preferredHeight: 42
                    radius: 999
                    color: searchMa.containsMouse ? "#d6b357" : "#c9a44a"

                    Text {
                        anchors.centerIn: parent
                        text: "Search"
                        color: "#15110a"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        id: searchMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.runSearch()
                    }
                }
            }

            // sort pills: Relevance / Top / Random (Random re-rolls on every press)
            Row {
                spacing: 8

                Repeater {
                    model: [ { "label": "Relevance", "mode": "relevance" },
                             { "label": "Top", "mode": "top" },
                             { "label": "Random", "mode": "random" } ]
                    delegate: Rectangle {
                        id: sortPill
                        required property var modelData
                        width: sortLabel.implicitWidth + 26
                        height: 30
                        radius: 999
                        color: root.sorting === sortPill.modelData.mode ? "#c9a44a"
                             : sortMa.containsMouse ? Qt.rgba(255, 255, 255, 0.16)
                             : Qt.rgba(255, 255, 255, 0.08)
                        border.width: root.sorting === sortPill.modelData.mode ? 0 : 1
                        border.color: Qt.rgba(255, 255, 255, 0.14)

                        Text {
                            id: sortLabel
                            anchors.centerIn: parent
                            text: sortPill.modelData.label
                            color: root.sorting === sortPill.modelData.mode ? "#15110a" : "#e8e4dc"
                            font.pixelSize: 12
                            font.weight: root.sorting === sortPill.modelData.mode ? Font.DemiBold : Font.Normal
                        }

                        MouseArea {
                            id: sortMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.setSorting(sortPill.modelData.mode)
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.statusText
                color: "#b8b2a8"
                font.pixelSize: 12
            }

            // ---- Colosseum shelf: our own living wallpapers, apart from the searchable pool ----
            Text {
                text: "Colosseum"
                color: "#d8d2c4"
                font.family: theme.display
                font.pixelSize: 16
            }

            Row {
                spacing: 10

                Repeater {
                    model: WallpaperApi.nativePicks()
                    delegate: Rectangle {
                        id: nativeTile
                        required property var modelData
                        width: 144
                        height: 92
                        radius: 8
                        color: "#07070a"
                        border.width: root.selectedPick.source_id === nativeTile.modelData.source_id ? 2 : 1
                        border.color: root.selectedPick.source_id === nativeTile.modelData.source_id ? "#c9a44a" : Qt.rgba(255, 255, 255, 0.14)
                        clip: true

                        // the tile IS the wallpaper, live and miniature
                        Loader {
                            anchors.fill: parent
                            source: WallpaperApi.nativeSceneFor(nativeTile.modelData.image_url)
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 22
                            color: Qt.rgba(0, 0, 0, 0.55)

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                text: nativeTile.modelData.title
                                color: "#e8e2d4"
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selectedPick = nativeTile.modelData
                        }
                    }
                }
            }

            GridView {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: 154
                cellHeight: 104
                model: root.results
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: HouseScrollBar { flick: grid }

                delegate: Rectangle {
                    id: resultTile
                    required property var modelData
                    width: 144
                    height: 92
                    radius: 8
                    color: "#101014"
                    border.width: root.selectedPick.source_id === resultTile.modelData.source_id ? 2 : 1
                    border.color: root.selectedPick.source_id === resultTile.modelData.source_id ? "#c9a44a" : Qt.rgba(255, 255, 255, 0.14)
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: resultTile.modelData.thumb_url
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                    }

                    // source badge — the chip says whose art it is (Wallhaven, or the Colosseum shelf)
                    Rectangle {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 5
                        width: badgeText.implicitWidth + 10
                        height: 16
                        radius: 8
                        color: Qt.rgba(0, 0, 0, 0.62)

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: resultTile.modelData.source
                            color: "#d8d4cc"
                            font.pixelSize: 9
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectedPick = resultTile.modelData
                    }
                }

                // Load more — appends Wallhaven's next page
                footer: Item {
                    width: grid.width
                    height: root.canLoadMore || root.loading ? 56 : 0

                    Rectangle {
                        anchors.centerIn: parent
                        visible: root.canLoadMore || root.loading
                        width: 148
                        height: 36
                        radius: 999
                        color: root.loading ? Qt.rgba(255, 255, 255, 0.08)
                             : moreMa.containsMouse ? "#d6b357" : "#c9a44a"

                        Text {
                            anchors.centerIn: parent
                            text: root.loading ? "Loading..." : "Load more"
                            color: root.loading ? "#b8b2a8" : "#15110a"
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            id: moreMa
                            anchors.fill: parent
                            enabled: root.canLoadMore
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.fetchMore()
                        }
                    }
                }
            }
        }

        ScrollGlide { flick: grid }
    }

    Rectangle {
        visible: root.selectedPick && root.selectedPick.image_url
        width: 360
        height: 142
        radius: 18
        x: root.width - width - 52
        y: root.height - height - 52
        color: Qt.rgba(0.04, 0.04, 0.06, 0.88)
        border.width: 1
        border.color: Qt.rgba(255, 255, 255, 0.16)

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Text {
                width: parent.width
                text: root.selectedPick.title || "Selected wallpaper"
                elide: Text.ElideRight
                color: "#f6f2ea"
                font.family: theme.display
                font.pixelSize: 18
            }

            Text {
                text: root.selectedPick.spec || ""
                color: "#aaa59c"
                font.pixelSize: 12
            }

            Row {
                spacing: 10

                Repeater {
                    model: [
                        { label: "For All Worlds", scope: "all" },
                        { label: "For " + root.targetWorld, scope: "world" }
                    ]

                    delegate: Rectangle {
                        id: scopeButton
                        required property var modelData
                        width: 150
                        height: 40
                        radius: 999
                        color: buttonMa.containsMouse ? "#d6b357" : (scopeButton.modelData.scope === "all" ? "#c9a44a" : Qt.rgba(255, 255, 255, 0.10))
                        border.width: scopeButton.modelData.scope === "all" ? 0 : 1
                        border.color: Qt.rgba(255, 255, 255, 0.18)

                        Text {
                            anchors.centerIn: parent
                            text: scopeButton.modelData.label
                            color: scopeButton.modelData.scope === "all" || buttonMa.containsMouse ? "#15110a" : "#f0eee8"
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            id: buttonMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.applyRequested(scopeButton.modelData.scope, root.targetWorld, root.selectedPick)
                        }
                    }
                }
            }
        }
    }
}
