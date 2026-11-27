pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "WallpaperApi.js" as WallpaperApi

Item {
    id: root
    objectName: "wallpaperSearch"

    Theme { id: theme }

    property Item backdrop
    property var contentStore: typeof Content !== "undefined" ? Content : null
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

    signal closeRequested()                  // the panel "x" — closes the picker, back to the world
    signal applyRequested(string scope, string world, var pick)
    // Window chrome (2026-07-20): this overlay covers the shell top bar, so it carries its
    // own minimize · fullscreen-toggle · power like every other page. closeRequested stays
    // the picker's back; quitRequested is the app-quit power button.
    signal minimizeRequested()
    signal fullscreenRequested()
    signal quitRequested()

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
    function revealResultIndex(index) {
        Qt.callLater(function() {
            if (!grid.currentItem) return
            const y = grid.y + grid.currentItem.y
            const target = y - Math.max(0, (bodyFlick.height - grid.currentItem.height) / 2)
            bodyFlick.contentY = Math.max(0, Math.min(Math.max(0, bodyFlick.contentHeight - bodyFlick.height), target))
        })
    }

    Component.onCompleted: {
        searchField.text = WallpaperApi.defaultQueryFor(targetWorld)
        runSearch()
    }

    function resolveImage(pick) {
        if (contentStore) contentStore.generation // Re-resolve when published assets change.
        return WallpaperApi.imageSource(pick, contentStore)
    }

    // The inherited source is already resolved by the shell.
    readonly property string previewUrl: root.selectedPick.image_url
                                         ? root.resolveImage(root.selectedPick) : root.inheritedImageUrl
    Image {
        objectName: "wallpaperSearchPreview"
        anchors.fill: parent
        source: root.previewUrl
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.34)
    }

    // Window chrome cluster — minimize · fullscreen-toggle · power. Top-right, over the
    // empty preview area (the search panel is on the left), matching every other page.
    Row {
        visible: Qt.platform.os !== "android"
        z: 50
        anchors.right: parent.right
        anchors.rightMargin: 40
        anchors.top: parent.top
        anchors.topMargin: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: wpMinMa.containsMouse || wpMinKey.activeFocus ? 1.0 : 0.72
            }
            MouseArea {
                id: wpMinMa
                anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.minimizeRequested()
            }
            KeyboardAction { id: wpMinKey; anchors.fill: parent; pointerEnabled: false
                accessibleName: "Minimize"; onTriggered: root.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image {
                anchors.fill: parent
                source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg"
                        : "../assets/icons/fullscreen-exit.svg"
                sourceSize.width: 22; sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: wpFsMa.containsMouse || wpFsKey.activeFocus ? 1.0 : 0.72
            }
            MouseArea {
                id: wpFsMa
                anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.fullscreenRequested()
            }
            KeyboardAction { id: wpFsKey; anchors.fill: parent; pointerEnabled: false
                accessibleName: "Toggle fullscreen"; onTriggered: root.fullscreenRequested() }
        }
        Item {
            width: 22; height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: wpPowMa.containsMouse || wpPowKey.activeFocus ? 1.0 : 0.72
            }
            MouseArea {
                id: wpPowMa
                anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.quitRequested()
            }
            KeyboardAction { id: wpPowKey; anchors.fill: parent; pointerEnabled: false
                accessibleName: "Close Colosseum"; onTriggered: root.quitRequested() }
        }
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
                    objectName: "wallpaperSearchClose"
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
                    KeyboardAction { anchors.fill: parent; anchors.margins: -10; pointerEnabled: false
                        accessibleName: "Close wallpaper picker"
                        onTriggered: root.closeRequested() }
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
                    KeyboardAction { anchors.fill: parent; pointerEnabled: false
                        accessibleName: "Search wallpapers"; onTriggered: root.runSearch() }
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
                        KeyboardAction { anchors.fill: parent; pointerEnabled: false
                            accessibleName: "Sort wallpapers by " + sortPill.modelData.label
                            onTriggered: root.setSorting(sortPill.modelData.mode) }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.statusText
                color: "#b8b2a8"
                font.pixelSize: 12
            }

            // The Colosseum shelf and Wallhaven results share one vertical scroller.
            // The shelf scrolls sideways; the results grid flows beneath it.
            Flickable {
                id: bodyFlick
                objectName: "wallpaperSearchBody"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                activeFocusOnTab: true
                Keys.onPressed: (event) => bodyKeys.handle(event)
                Keys.onReleased: (event) => bodyKeys.handleRelease(event)
                contentWidth: width
                contentHeight: bodyCol.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: HouseScrollBar { flick: bodyFlick }
                KeyboardScrollController { id: bodyKeys; flick: bodyFlick }

                ColumnLayout {
                    id: bodyCol
                    objectName: "wallpaperSearchSections"
                    width: bodyFlick.width
                    spacing: 14

            Text {
                text: "Colosseum"
                color: "#d8d2c4"
                font.family: theme.display
                font.pixelSize: 16
            }

            ListView {
                id: colosseumStrip
                objectName: "wallpaperColosseumShelf"
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                orientation: ListView.Horizontal
                spacing: 10
                clip: true
                model: WallpaperApi.colosseumPicks()
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: count > 0
                onActiveFocusChanged: if (activeFocus && currentIndex < 0 && count > 0) currentIndex = 0
                Keys.onPressed: (event) => colosseumKeys.handle(event)
                highlight: Rectangle { color: "transparent"; radius: 8; border.width: 2; border.color: "#f0c44a"; visible: colosseumStrip.activeFocus }
                KeyboardCollectionController {
                    id: colosseumKeys; view: colosseumStrip; orientation: "horizontal"
                    onActivated: (index) => { const row = colosseumStrip.model[index]; if (row) root.selectedPick = row }
                }

                // a horizontal list ignores the vertical wheel by default — map it to
                // sideways travel so the mouse wheel scrolls the strip.
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (ev) => {
                        var d = (ev.angleDelta.y !== 0 ? ev.angleDelta.y : ev.angleDelta.x)
                        colosseumStrip.contentX = Math.max(0, Math.min(
                            Math.max(0, colosseumStrip.contentWidth - colosseumStrip.width),
                            colosseumStrip.contentX - d))
                    }
                }

                delegate: Rectangle {
                    id: colosseumTile
                    required property var modelData
                    objectName: "wallpaperPick_" + colosseumTile.modelData.source_id.replace(/-/g, "_")
                    width: 144
                    height: 92
                    radius: 8
                    color: "#07070a"
                    border.width: root.selectedPick.source_id === colosseumTile.modelData.source_id ? 2 : 1
                    border.color: root.selectedPick.source_id === colosseumTile.modelData.source_id ? "#c9a44a" : Qt.rgba(255, 255, 255, 0.14)
                    clip: true

                    Image {
                        objectName: "wallpaperPickThumbnail"
                        anchors.fill: parent
                        source: root.resolveImage({ image_url: colosseumTile.modelData.thumb_url })
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                    }

                    // title + artist credit
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 32
                        color: Qt.rgba(0, 0, 0, 0.6)

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            spacing: 1

                            Text {
                                width: parent.width
                                text: colosseumTile.modelData.title
                                color: "#e8e2d4"
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: colosseumTile.modelData.artist
                                color: "#9a958c"
                                font.pixelSize: 8
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectedPick = colosseumTile.modelData
                    }
                }
            }

            GridView {
                id: grid
                objectName: "wallpaperWallhavenResults"
                Layout.fillWidth: true
                // non-interactive and sized to its content — the outer bodyFlick does the
                // scrolling, so the results flow straight on under the shelves.
                Layout.preferredHeight: grid.contentHeight
                interactive: false
                clip: false
                cellWidth: 154
                cellHeight: 104
                model: root.results
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: count > 0
                onActiveFocusChanged: if (activeFocus && currentIndex < 0 && count > 0) currentIndex = 0
                Keys.onPressed: (event) => resultKeys.handle(event)
                highlight: Rectangle { color: "transparent"; radius: 8; border.width: 2; border.color: "#f0c44a"; visible: grid.activeFocus }
                KeyboardCollectionController {
                    id: resultKeys
                    view: grid
                    orientation: "grid"
                    columns: Math.max(1, Math.floor(grid.width / grid.cellWidth))
                    positionIndexFn: (index) => root.revealResultIndex(index)
                    onActivated: (index) => { const row = root.results[index]; if (row) root.selectedPick = row }
                }

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

                    // source badge — the chip says whose art it is
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
                        KeyboardAction {
                            anchors.fill: parent
                            pointerEnabled: false
                            enabled: root.canLoadMore
                            accessibleName: "Load more wallpapers"
                            onTriggered: root.fetchMore()
                        }
                    }
                }
            }
                }
            }
        }

        ScrollGlide { flick: bodyFlick }
    }

    Rectangle {
        // Coerced: the bare && yields the image_url STRING (or undefined), and QML warns
        // "Unable to assign [undefined] to bool" on every evaluation with nothing selected.
        visible: !!(root.selectedPick && root.selectedPick.image_url)
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
                        KeyboardAction {
                            anchors.fill: parent
                            pointerEnabled: false
                            accessibleName: "Apply wallpaper " + scopeButton.modelData.label
                            onTriggered: root.applyRequested(scopeButton.modelData.scope, root.targetWorld, root.selectedPick)
                        }
                    }
                }
            }
        }
    }

    // Lanista automation layer: the rendered apply buttons above remain the production
    // controls. These transparent hit targets keep the two final actions addressable
    // without changing the picker layout or its signal path.
    Item {
        objectName: "wallpaperApply_all"
        visible: !!(root.selectedPick && root.selectedPick.image_url)
        z: 60
        x: root.width - 360 - 52 + 16
        y: root.height - 142 - 52 + 72
        width: 150; height: 40
        MouseArea {
            anchors.fill: parent
            onClicked: root.applyRequested("all", root.targetWorld, root.selectedPick)
        }
    }
    Item {
        objectName: "wallpaperApply_world"
        visible: !!(root.selectedPick && root.selectedPick.image_url)
        z: 60
        x: root.width - 360 - 52 + 176
        y: root.height - 142 - 52 + 72
        width: 150; height: 40
        MouseArea {
            anchors.fill: parent
            onClicked: root.applyRequested("world", root.targetWorld, root.selectedPick)
        }
    }
}
