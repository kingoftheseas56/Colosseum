import QtQuick

Item {
    id: root

    width: 268
    height: 50

    property string offerState: "unknown"
    property string label: ""
    property string meta: ""
    property real progress: 0
    property var theme: null

    signal activated()

    readonly property bool quietState: offerState === "checking" || offerState === "none"
    readonly property bool readyState: offerState === "ready"
    readonly property bool gettingState: offerState === "getting"
    readonly property bool activeState: offerState === "available"
                                        || offerState === "choose"
                                        || offerState === "failed"
                                        || offerState === "ready"
    readonly property color goldColor: theme ? theme.gold : "#f0c44a"
    readonly property color inkColor: theme ? theme.ink : "#f7f7f5"
    readonly property color inkDimColor: theme ? theme.inkDim : "#c9c8d0"
    readonly property color inkFaintColor: Qt.rgba(1, 1, 1, 0.40)
    readonly property string uiFont: theme ? theme.ui : "Segoe UI"

    visible: offerState !== "unknown"

    Rectangle {
        id: surface
        objectName: "audiobookOfferSurface"
        anchors.fill: parent
        radius: 13
        clip: true
        visible: !root.quietState
        color: Qt.rgba(1, 1, 1, 0.07)

        Rectangle {
            id: progressFill
            objectName: "audiobookOfferProgressFill"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: root.gettingState
                   ? parent.width * Math.max(0, Math.min(1, root.progress))
                   : 0
            color: Qt.rgba(root.goldColor.r, root.goldColor.g, root.goldColor.b, 0.20)
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 13
        visible: !root.quietState
        color: "transparent"
        border.width: 1
        border.color: root.readyState
                      ? Qt.rgba(root.goldColor.r, root.goldColor.g, root.goldColor.b, 0.45)
                      : Qt.rgba(1, 1, 1, 0.12)
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 9
        width: Math.min(root.width - 32, iconBox.width + spacing + Math.max(labelText.implicitWidth, metaText.implicitWidth))
        visible: !root.quietState

        Item {
            id: iconBox
            width: 18
            height: 18
            anchors.verticalCenter: parent.verticalCenter

            Canvas {
                id: iconSource
                anchors.fill: parent
                property url asset: root.gettingState ? Qt.resolvedUrl("../assets/icons/download.svg") : Qt.resolvedUrl("../assets/icons/reader2/headphones.svg")
                property color ink: root.readyState ? root.goldColor : root.inkColor
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
                x: 2.3; y: 10.4; width: 3.4; height: 4.9; radius: 1.1
                visible: root.readyState
                color: root.goldColor
            }
            Rectangle {
                x: 12.3; y: 10.4; width: 3.4; height: 4.9; radius: 1.1
                visible: root.readyState
                color: root.goldColor
            }
        }

        Column {
            width: content.width - iconBox.width - content.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                id: labelText
                objectName: "audiobookOfferLabel"
                width: parent.width
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                text: root.label
                color: root.readyState ? root.goldColor : root.inkColor
                font.family: root.uiFont
                font.pixelSize: root.offerState === "failed" ? 12 : 15
                font.weight: Font.DemiBold
            }

            Text {
                id: metaText
                objectName: "audiobookOfferMeta"
                visible: text.length > 0
                width: parent.width
                elide: Text.ElideRight
                text: root.meta
                color: root.inkDimColor
                font.family: root.uiFont
                font.pixelSize: 11
                font.weight: Font.Medium
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 2
        visible: root.quietState

        Text {
            text: root.label
            color: root.inkDimColor
            font.family: root.uiFont
            font.pixelSize: 13
            font.weight: Font.Medium
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            visible: text.length > 0
            text: root.meta
            color: root.inkFaintColor
            font.family: root.uiFont
            font.pixelSize: 11
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    KeyboardAction {
        id: action
        objectName: "audiobookOfferAction"
        anchors.fill: parent
        enabled: root.activeState
        focusEnabled: root.activeState
        accessibleName: root.label
        accessibleDescription: root.meta
        focusRadius: 13
        onTriggered: root.activated()
    }
}
