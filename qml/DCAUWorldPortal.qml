import QtQuick
import QtQuick.Effects

FocusScope {
    id: root
    objectName: "dcauPortal_" + String(root.title).toLowerCase().replace(/ /g, "-")
    property string title: ""
    property string ordinal: ""
    property var imageSources: []
    property bool selected: false
    signal activated()
    signal selectionRequested()
    signal previousRequested()
    signal nextRequested()

    Theme { id: theme }

    width: 300
    height: Math.round(width * 8 / 5)
    activeFocusOnTab: true
    opacity: root.selected ? 1.0 : 0.56
    scale: root.selected ? 1.0 : 0.91
    transformOrigin: Item.Center
    Behavior on opacity { NumberAnimation { duration: 250 } }
    Behavior on scale { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

    Rectangle {
        x: -8; y: 24
        width: root.width + 16; height: root.height + 18
        radius: 30
        color: Qt.rgba(0, 0, 0, 0.20)
    }
    Item {
        id: artPlane
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: portalMask
            maskThresholdMin: 0.5
        }

        Image {
            anchors.fill: parent
            source: root.imageSources && root.imageSources.length ? root.imageSources[0] : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            smooth: true
            mipmap: true
            sourceSize.width: Math.ceil(root.width * 2)
            sourceSize.height: Math.ceil(root.height * 2)
            scale: hover.hovered ? 1.018 : 1.0
            Behavior on scale { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.68; color: "transparent" }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.73) }
            }
        }
    }
    Item {
        id: portalMask
        anchors.fill: parent
        visible: false
        layer.enabled: true
        Rectangle { anchors.fill: parent; radius: 22; color: "black" }
    }

    Rectangle {
        anchors.fill: parent
        radius: 22
        color: "transparent"
        border.width: 1
        border.color: root.selected || root.activeFocus
                      ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.62)
                      : Qt.rgba(1, 1, 1, 0.13)
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 22
        anchors.rightMargin: 22
        anchors.bottomMargin: 18
        spacing: 5

        Text {
            text: root.ordinal
            color: Qt.rgba(247 / 255, 247 / 255, 245 / 255, 0.55)
            font.family: theme.ui
            font.pixelSize: 9
            font.letterSpacing: 1.62
        }
        Text {
            width: parent.width
            text: root.title
            color: root.selected ? theme.gold : theme.ink
            font.family: theme.display
            font.pixelSize: 24
            font.weight: Font.DemiBold
            font.letterSpacing: -0.6
            lineHeightMode: Text.FixedHeight
            lineHeight: 24
            wrapMode: Text.WordWrap
        }
    }

    HoverHandler { id: hover }
    TapHandler {
        onTapped: {
            root.forceActiveFocus()
            root.selectionRequested()
            root.activated()
        }
    }
    onActiveFocusChanged: if (activeFocus) root.selectionRequested()
    Keys.onReturnPressed: root.activated()
    Keys.onEnterPressed: root.activated()
    Keys.onLeftPressed: root.previousRequested()
    Keys.onRightPressed: root.nextRequested()
}
