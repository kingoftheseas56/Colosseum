pragma ComponentBehavior: Bound
import QtQuick
import ".."

Item {
    id: root
    property var media: ({})
    property string activeTab: availableTabs().length ? availableTabs()[0].key : ""
    implicitHeight: 260
    height: implicitHeight

    HarborTheme { id: theme }

    function listFor(key) {
        var value = media && media[key] ? media[key] : []
        return Array.isArray(value) ? value : []
    }

    function availableTabs() {
        var defs = [
            { key: "videos", label: "Videos" },
            { key: "backdrops", label: "Backdrops" },
            { key: "posters", label: "Posters" },
            { key: "logos", label: "Logos" }
        ]
        var out = []
        for (var i = 0; i < defs.length; ++i) {
            var rows = listFor(defs[i].key)
            if (rows.length) out.push({ key: defs[i].key, label: defs[i].label, count: rows.length })
        }
        return out
    }

    function activeRows() { return listFor(activeTab) }

    onMediaChanged: {
        var tabs = availableTabs()
        if (!tabs.length) activeTab = ""
        else {
            var found = false
            for (var i = 0; i < tabs.length; ++i) if (tabs[i].key === activeTab) found = true
            if (!found) activeTab = tabs[0].key
        }
    }

    Column {
        x: 9
        width: Math.max(0, parent.width - 9)
        spacing: 20

        Row {
            spacing: 10

            Text {
                text: "Media"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 17
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }

            Flow {
                width: Math.max(0, root.width - 90)
                spacing: 6

                Repeater {
                    model: root.availableTabs()

                    delegate: Rectangle {
                        id: tab
                        required property var modelData
                        width: tabRow.implicitWidth + 24
                        height: 28
                        radius: 14
                        color: root.activeTab === modelData.key ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                        border.width: root.activeTab === modelData.key ? 1 : 0
                        border.color: theme.edge

                        Row {
                            id: tabRow
                            anchors.centerIn: parent
                            spacing: 6

                            Text {
                                text: tab.modelData.label
                                color: root.activeTab === tab.modelData.key ? theme.ink : theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: tab.modelData.count
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                            }
                        }

                        KeyboardAction {
                            anchors.fill: parent
                            accessibleName: tab.modelData.label
                            focusRadius: parent.radius
                            onTriggered: root.activeTab = tab.modelData.key
                        }
                    }
                }
            }
        }

        Flickable {
            id: rail
            width: parent.width
            height: root.activeTab === "logos" ? 140 : 202
            contentWidth: tiles.implicitWidth
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: tiles
                spacing: 16

                Repeater {
                    model: root.activeRows()

                    delegate: Item {
                        id: tile
                        required property var modelData
                        readonly property bool portrait: root.activeTab === "posters"
                        readonly property bool logo: root.activeTab === "logos"
                        readonly property bool video: root.activeTab === "videos"
                        width: logo ? 220 : (portrait ? 160 : 300)
                        height: logo ? 120 : (portrait ? 240 : (video ? 202 : 169))

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: tile.logo ? 120 : (tile.portrait ? 240 : 169)
                            radius: 12
                            color: Qt.rgba(1, 1, 1, tile.logo ? 0.025 : 0.06)
                            border.width: tile.logo ? 1 : 0
                            border.color: theme.edge
                            clip: true

                            Image {
                                anchors.fill: parent
                                anchors.margins: tile.logo ? 20 : 0
                                source: tile.video ? (tile.modelData.image || "") : String(tile.modelData || "")
                                fillMode: tile.logo ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                            }

                            Rectangle {
                                visible: tile.video
                                anchors.centerIn: parent
                                width: 48
                                height: 48
                                radius: 24
                                color: theme.ink
                                opacity: tileAction.interactionActive ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: 150 } }

                                HarborIcon {
                                    anchors.centerIn: parent
                                    width: 18
                                    height: 18
                                    iconSize: 18
                                    kind: "play"
                                    darkGlyph: true
                                }
                            }
                        }

                        Column {
                            visible: tile.video
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            spacing: 2
                            Text {
                                width: parent.width
                                text: tile.modelData.name || "Trailer"
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: tile.modelData.type || "Video"
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 12
                            }
                        }

                        KeyboardAction {
                            id: tileAction
                            anchors.fill: parent
                            accessibleName: tile.video ? (tile.modelData.name || "Video") : (root.activeTab + " media")
                            focusRadius: 12
                            onTriggered: console.log("[arc56] media", root.activeTab)
                        }
                    }
                }
            }
        }
    }
}
