// WorldTabBar — TheatreTabBar's glass pill bar, generalized to any tab set (parameterized
// tabModel). Used by the Tankoban world (Manga|Comics); Theatre keeps TheatreTabBar for now
// and can migrate to this later. Same glass look/feel: gold active pill, ghost inactive, hover tint.
import QtQuick

pragma ComponentBehavior: Bound

Item {
    id: tabs

    required property Item backdrop
    property var tabModel: []            // [{ key, label }, …]
    property string currentTab: ""
    signal tabRequested(string tab)

    width: parent ? parent.width : 900
    height: 58

    Theme { id: theme }

    Glass {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(parent.width, 160 * Math.max(1, tabs.tabModel.length))
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

                    width: (parent.width - 6 * (tabs.tabModel.length - 1)) / Math.max(1, tabs.tabModel.length)
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
