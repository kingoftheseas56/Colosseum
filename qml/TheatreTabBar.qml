// TheatreTabBar - Harbor's left sidebar, translated into Colosseum's horizontal glass control.

import QtQuick

pragma ComponentBehavior: Bound

Item {
    id: tabs
    objectName: "theatreTabBar"

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
        { key: "anime", label: "Anime" },
        { key: "library", label: "Library" }
    ]

    Glass {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(parent.width, 760)
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

                    width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
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

        // Lanista automation layer: the rendered Repeater above remains the production
        // visual tree. These transparent, same-geometry hit targets keep each tab
        // addressable when Repeater delegates are not exposed consistently by the
        // structural walker, and forward to the same signal as the visible pills.
        Row {
            id: automationPills
            anchors.fill: parent
            anchors.margins: 6
            spacing: 6
            z: 100

            Item {
                objectName: "theatreTab_discover"
                width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
                height: parent.height
                readonly property bool activeState: tabs.currentTab === "discover"
                MouseArea { anchors.fill: parent; onClicked: tabs.tabRequested("discover") }
            }
            Item {
                objectName: "theatreTab_movies"
                width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
                height: parent.height
                readonly property bool activeState: tabs.currentTab === "movies"
                MouseArea { anchors.fill: parent; onClicked: tabs.tabRequested("movies") }
            }
            Item {
                objectName: "theatreTab_shows"
                width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
                height: parent.height
                readonly property bool activeState: tabs.currentTab === "shows"
                MouseArea { anchors.fill: parent; onClicked: tabs.tabRequested("shows") }
            }
            Item {
                objectName: "theatreTab_anime"
                width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
                height: parent.height
                readonly property bool activeState: tabs.currentTab === "anime"
                MouseArea { anchors.fill: parent; onClicked: tabs.tabRequested("anime") }
            }
            Item {
                objectName: "theatreTab_library"
                width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
                height: parent.height
                readonly property bool activeState: tabs.currentTab === "library"
                MouseArea { anchors.fill: parent; onClicked: tabs.tabRequested("library") }
            }
        }
    }

}
