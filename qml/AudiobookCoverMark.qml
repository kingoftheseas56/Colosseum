import QtQuick

Item {
    id: root

    width: 26
    height: 26
    anchors.top: parent.top
    anchors.right: parent.right
    anchors.margins: 7

    property bool downloaded: false
    visible: downloaded

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: theme.gold
        border.width: 1
        border.color: theme.gold
    }

    Item {
        id: iconBox
        width: 14
        height: 14
        anchors.centerIn: parent

        Canvas {
            id: iconSource
            anchors.fill: parent
            property url asset: Qt.resolvedUrl("../assets/icons/reader2/headphones.svg")
            property color ink: "#241a05"
            onAssetChanged: { loadImage(asset); requestPaint() }
            onInkChanged: requestPaint()
            onImageLoaded: requestPaint()
            Component.onCompleted: loadImage(asset)
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                if (!isImageLoaded(asset)) return
                ctx.drawImage(asset, 0, 0, width, height)
                ctx.globalCompositeOperation = "source-in"
                ctx.fillStyle = ink
                ctx.fillRect(0, 0, width, height)
                ctx.globalCompositeOperation = "source-over"
            }
        }

        Rectangle {
            x: 1.8; y: 8.1; width: 2.7; height: 3.8; radius: 0.9
            color: "#241a05"
        }
        Rectangle {
            x: 9.6; y: 8.1; width: 2.7; height: 3.8; radius: 0.9
            color: "#241a05"
        }
    }
}
