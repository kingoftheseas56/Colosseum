import QtQuick

// AF2 "More like this" — poster cards from our own catalogs. Tap opens that
// title's detail. Hides when empty; slides in per house Behavior convention.
Column {
    id: mlt
    property var cards: []   // [{id, type, title, cover}]
    signal openRequested(var item)
    visible: cards.length > 0
    spacing: 16
    opacity: cards.length > 0 ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: mlt.cards.length > 0 ? 0 : 24
        Behavior on y { NumberAnimation { duration: 620; easing.type: Easing.OutCubic } }
    }

    Theme { id: theme }

    Text {
        text: "MORE LIKE THIS"
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.5
    }
    ListView {
        width: parent.width
        height: 216
        orientation: ListView.Horizontal
        spacing: 18
        clip: true
        model: mlt.cards
        delegate: Column {
            id: card
            required property var modelData
            width: 120
            spacing: 9
            Rectangle {
                width: 120; height: 172; radius: 8
                color: Qt.rgba(1, 1, 1, 0.05)
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                clip: true
                Image {
                    anchors.fill: parent
                    source: card.modelData.cover || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mlt.openRequested({ "id": card.modelData.id, "type": card.modelData.type,
                                                   "title": card.modelData.title, "cover": card.modelData.cover })
                }
            }
            Text {
                width: parent.width
                text: card.modelData.title || ""; elide: Text.ElideRight
                color: theme.ink; font.family: theme.ui; font.pixelSize: 12
            }
        }
    }
}
