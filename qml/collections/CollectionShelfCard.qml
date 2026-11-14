pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import ".."

Button {
    id: card
    required property var store
    required property var collection
    readonly property var covers: card.collection.items.slice(0, 3)
    readonly property color shelfTint: card.collection.tint || "#465265"
    objectName: "collectionCard_" + card.collection.id
    implicitHeight: 300
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: qsTr("Open %1, %2 titles").arg(card.collection.name).arg(card.collection.items.length)
    Theme { id: theme }

    background: Rectangle {
        color: "transparent"
        radius: 15
        border.width: card.visualFocus ? 2 : 0
        border.color: theme.gold
    }
    contentItem: Item {
        Rectangle {
            id: artwork
            width: parent.width
            height: 220
            radius: 14
            border.width: 1
            border.color: card.hovered ? Qt.rgba(1, 1, 1, 0.23) : Qt.rgba(1, 1, 1, 0.10)
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(card.shelfTint.r, card.shelfTint.g, card.shelfTint.b, 0.36) }
                GradientStop { position: 1; color: "#11151f" }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 24
                height: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }
            Repeater {
                model: card.covers
                delegate: Item {
                    id: jacket
                    required property string modelData
                    required property int index
                    readonly property var titleData: card.store.media(jacket.modelData)
                    width: Math.min(125, artwork.width * 0.34)
                    height: 174
                    x: artwork.width / 2 - width / 2 + (index === 1 ? -width * 0.64 : index === 2 ? width * 0.64 : 0)
                    y: index === 0 ? 19 : 27
                    rotation: index === 1 ? -8 : index === 2 ? 8 : 0
                    z: index === 0 ? 3 : 1
                    Rectangle {
                        x: 4; y: 6
                        width: parent.width; height: parent.height
                        radius: 4
                        color: "#55000000"
                    }
                    CollectionCover {
                        anchors.fill: parent
                        source: jacket.titleData.cover
                        title: jacket.titleData.title
                        radius: 4
                    }
                }
            }
            CollectionIcon {
                visible: card.covers.length === 0
                anchors.centerIn: parent
                width: 44; height: 44
                name: "collections"
                strokeColor: theme.inkDimmer
            }
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 12
                width: 28; height: 28; radius: 14
                color: "#aa11151f"
                visible: card.hovered || card.visualFocus
                CollectionIcon {
                    anchors.centerIn: parent
                    width: 15; height: 15
                    name: "arrow"
                    strokeColor: theme.ink
                }
            }
        }
        Text {
            id: collectionTitle
            x: 2; y: artwork.height + 13
            width: parent.width - 4
            text: card.collection.name
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 19
            font.weight: Font.DemiBold
            maximumLineCount: 2
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
        Text {
            x: 2; y: collectionTitle.y + collectionTitle.height + 6
            width: parent.width - 4
            text: card.collection.items.length === 0 ? qsTr("Ready for your first title") : qsTr("%1 titles  ·  %2").arg(card.collection.items.length).arg(card.store.summary(card.collection))
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 12
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
    }
    ToolTip.visible: card.hovered && collectionTitle.truncated
    ToolTip.text: card.collection.name
    ToolTip.delay: 500
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    Keys.onReturnPressed: card.clicked()
    Keys.onEnterPressed: card.clicked()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Select) {
            if (card.enabled) card.clicked()
            event.accepted = true
        }
    }

}
