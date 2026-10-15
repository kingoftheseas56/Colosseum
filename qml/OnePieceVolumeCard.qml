pragma ComponentBehavior: Bound
import QtQuick

// Story-catalogue volume tile using Tankoban's real cover-flow geometry.
// Tankoban tops its physical book at 276px high, 2:3 aspect, and lifts the
// active cover to 110%. Do not route this back through the 148px gallery card.
Item {
    id: root
    required property var entry
    property bool colorEdition: false
    signal activated()

    readonly property int bookHeight: 276
    readonly property int bookWidth: 184
    readonly property int captionHeight: 54

    width: root.bookWidth + 18
    height: root.bookHeight + root.captionHeight
    activeFocusOnTab: true
    scale: root.activeFocus || hover.hovered ? 1.10 : 1.0
    transformOrigin: Item.Bottom
    Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    Theme { id: theme }

    Rectangle {
        id: coverBox
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.bookWidth
        height: root.bookHeight
        radius: 6
        clip: true
        color: theme.glassTint
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? theme.gold
            : (hover.hovered ? Qt.rgba(1, 1, 1, 0.34) : theme.edge)

        Image {
            id: coverImage
            anchors.fill: parent
            source: root.entry.cover || ""
            sourceSize: Qt.size(Math.ceil(width * 1.6), Math.ceil(height * 1.6))
            asynchronous: true
            cache: true
            retainWhileLoading: true
            fillMode: Image.PreserveAspectCrop
            visible: status === Image.Ready
        }

        Text {
            anchors.centerIn: parent
            visible: coverImage.status !== Image.Ready
            text: "VOL. " + root.entry.number
            color: theme.inkDimmer
            font.family: theme.display
            font.pixelSize: 28
            font.bold: true
        }
    }
    Text {
        anchors.top: coverBox.bottom
        anchors.topMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.bookWidth
        text: "Volume " + root.entry.number
        color: theme.ink
        font.family: theme.ui
        font.pixelSize: 13
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: root.activated() }
    Keys.onReturnPressed: root.activated()
    Keys.onEnterPressed: root.activated()
}
