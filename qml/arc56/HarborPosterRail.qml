pragma ComponentBehavior: Bound
import QtQuick
import ".."

Item {
    id: rail
    property string title: ""
    property var items: []
    property real cardWidth: 164
    signal itemRequested(var item)

    implicitHeight: titleText.implicitHeight + 20 + 286
    height: implicitHeight

    Theme { id: theme }

    Text {
        id: titleText
        anchors.left: parent.left
        anchors.top: parent.top
        text: rail.title
        color: theme.ink
        font.family: theme.ui
        font.pixelSize: 22
        font.weight: Font.Medium
    }

    Flickable {
        id: strip
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleText.bottom
        anchors.topMargin: 20
        height: 286
        contentWidth: cards.implicitWidth
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: cards
            spacing: 16

            Repeater {
                model: rail.items

                delegate: Item {
                    id: card
                    required property var modelData
                    width: rail.cardWidth
                    height: 282

                    Rectangle {
                        id: poster
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: rail.cardWidth * 1.5
                        radius: 12
                        clip: true
                        color: Qt.rgba(1, 1, 1, 0.06)
                        border.width: cardAction.activeFocus ? 2 : 0
                        border.color: theme.gold
                        y: cardAction.hovered ? -6 : 0

                        Behavior on y {
                            NumberAnimation { duration: 260; easing.type: Easing.OutCubic }
                        }

                        Image {
                            anchors.fill: parent
                            source: card.modelData.poster || ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            opacity: status === Image.Ready ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 220 } }
                        }

                        Rectangle {
                            anchors.fill: parent
                            color: Qt.rgba(0, 0, 0, cardAction.hovered ? 0.06 : 0)
                            Behavior on color { ColorAnimation { duration: 160 } }
                        }
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: poster.bottom
                        anchors.topMargin: 10
                        text: card.modelData.title || ""
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        text: card.modelData.year || ""
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    KeyboardAction {
                        id: cardAction
                        anchors.fill: parent
                        accessibleName: card.modelData.title || "Title"
                        focusRadius: 12
                        onTriggered: rail.itemRequested(card.modelData)
                    }
                }
            }
        }
    }
}
