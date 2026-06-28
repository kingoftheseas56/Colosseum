// TheatreTabBar - Harbor's left sidebar, translated into Colosseum's horizontal glass control.

import QtQuick

pragma ComponentBehavior: Bound

Item {
    id: tabs

    required property Item backdrop
    property string currentTab: "discover"
    signal tabRequested(string tab)

    width: parent ? parent.width : 900
    height: 58

    Theme { id: theme }

    readonly property var tabModel: [
        { key: "discover", label: "Discover" },
        { key: "movies", label: "Movies" },
        { key: "shows", label: "Shows" },
        { key: "anime", label: "Anime" }
    ]

    Glass {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(parent.width, 620)
        height: 54
        backdrop: tabs.backdrop
        radius: 18
        tint: 0.08
        scrim: 0.18

        Row {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 6

            Repeater {
                model: tabs.tabModel
                delegate: Rectangle {
                    id: pill
                    required property var modelData

                    width: (parent.width - 18) / 4
                    height: parent.height
                    radius: 14
                    color: pill.modelData.key === tabs.currentTab ? theme.gold : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
                    border.width: pill.modelData.key === tabs.currentTab ? 0 : 1
                    border.color: Qt.rgba(1, 1, 1, 0.10)

                    Text {
                        anchors.centerIn: parent
                        text: pill.modelData.label
                        color: pill.modelData.key === tabs.currentTab ? "#17120a" : theme.ink
                        font.family: theme.ui
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        id: ma
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: tabs.tabRequested(pill.modelData.key)
                    }

                    Behavior on color { ColorAnimation { duration: 140 } }
                }
            }
        }
    }

}
