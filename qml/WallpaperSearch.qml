pragma ComponentBehavior: Bound

import QtQuick
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

    signal closeRequested()
    signal applyRequested(string scope, string world, var pick)

    function runSearch() {
        statusText = "Searching Wallhaven..."
        results = []
        WallpaperApi.search(searchField.text, function(rows, err) {
            results = rows
            statusText = err || (rows.length + " wallpapers")
        })
    }

    Component.onCompleted: {
        searchField.text = WallpaperApi.defaultQueryFor(targetWorld)
        runSearch()
    }

    Image {
        anchors.fill: parent
        source: root.selectedPick.image_url || root.inheritedImageUrl
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
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

            Text {
                Layout.fillWidth: true
                text: root.statusText
                color: "#b8b2a8"
                font.pixelSize: 12
            }

            GridView {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: 154
                cellHeight: 104
                model: root.results

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

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectedPick = resultTile.modelData
                    }
                }
            }
        }
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
