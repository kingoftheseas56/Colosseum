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

    property int keyboardIndex: 0
    focusPolicy: Qt.TabFocus

    function syncKeyboardIndex() {
        for (var i = 0; i < tabs.tabModel.length; ++i) {
            if (tabs.tabModel[i].key === tabs.currentTab) {
                tabs.keyboardIndex = i
                return
            }
        }
        tabs.keyboardIndex = 0
    }

    function requestIndex(index, reason) {
        if (tabs.tabModel.length <= 0) return
        tabs.keyboardIndex = Math.max(0, Math.min(tabs.tabModel.length - 1, index))
        if (reason !== undefined) tabs.forceActiveFocus(reason)
        tabs.tabRequested(tabs.tabModel[tabs.keyboardIndex].key)
    }

    onCurrentTabChanged: tabs.syncKeyboardIndex()
    Component.onCompleted: tabs.syncKeyboardIndex()

    Keys.onPressed: (event) => {
        var next = tabs.keyboardIndex
        if (event.key === Qt.Key_Left) next--
        else if (event.key === Qt.Key_Right) next++
        else if (event.key === Qt.Key_Home) next = 0
        else if (event.key === Qt.Key_End) next = tabs.tabModel.length - 1
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            tabs.requestIndex(tabs.keyboardIndex, Qt.ShortcutFocusReason)
            event.accepted = true
            return
        } else {
            return
        }
        if (next >= 0 && next < tabs.tabModel.length && next !== tabs.keyboardIndex) {
            tabs.requestIndex(next, next < tabs.keyboardIndex ? Qt.BacktabFocusReason : Qt.TabFocusReason)
            event.accepted = true
        }
    }

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
                    required property int index
                    readonly property bool keyboardFocused: tabs.activeFocus && index === tabs.keyboardIndex

                    width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
                    height: parent.height
                    radius: 14
                    color: pill.modelData.key === tabs.currentTab ? theme.gold : ((ma.containsMouse || pill.keyboardFocused) ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
                    border.width: pill.modelData.key === tabs.currentTab ? 0 : (pill.keyboardFocused ? 2 : 1)
                    border.color: pill.keyboardFocused ? theme.gold : Qt.rgba(1, 1, 1, 0.10)

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
                        onClicked: tabs.requestIndex(pill.index, Qt.MouseFocusReason)
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
                MouseArea { anchors.fill: parent; onClicked: tabs.requestIndex(0, Qt.MouseFocusReason) }
            }
            Item {
                objectName: "theatreTab_movies"
                width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
                height: parent.height
                readonly property bool activeState: tabs.currentTab === "movies"
                MouseArea { anchors.fill: parent; onClicked: tabs.requestIndex(1, Qt.MouseFocusReason) }
            }
            Item {
                objectName: "theatreTab_shows"
                width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
                height: parent.height
                readonly property bool activeState: tabs.currentTab === "shows"
                MouseArea { anchors.fill: parent; onClicked: tabs.requestIndex(2, Qt.MouseFocusReason) }
            }
            Item {
                objectName: "theatreTab_anime"
                width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
                height: parent.height
                readonly property bool activeState: tabs.currentTab === "anime"
                MouseArea { anchors.fill: parent; onClicked: tabs.requestIndex(3, Qt.MouseFocusReason) }
            }
            Item {
                objectName: "theatreTab_library"
                width: (parent.width - (tabs.tabModel.length - 1) * 6 - 12) / tabs.tabModel.length
                height: parent.height
                readonly property bool activeState: tabs.currentTab === "library"
                MouseArea { anchors.fill: parent; onClicked: tabs.requestIndex(4, Qt.MouseFocusReason) }
            }
        }
    }

}
