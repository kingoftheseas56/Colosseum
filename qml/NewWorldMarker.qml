pragma ComponentBehavior: Bound
import QtQuick

FocusScope {
    id: root
    required property string markerId
    required property string title
    property string sealText: ""
    property url markerSource: ""
    property string fontFamily: "Georgia"
    property real diameter: 36
    property bool selected: false
    property bool additionalVisits: false
    property bool reducedMotion: false
    readonly property bool hovered: pointerHover.hovered
    width: Math.max(44, diameter)
    height: Math.max(44, diameter)
    activeFocusOnTab: true
    focusPolicy: Qt.TabFocus
    objectName: "newWorldMarker-" + markerId

    signal previewRequested(string markerId)
    signal focusEntered(string markerId)
    signal focusLeft(string markerId)
    signal hoverEntered(string markerId)
    signal hoverLeft(string markerId)
    signal moveRequested(string markerId, string direction)

    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.description: "Focus or hover to preview. Use OPEN ARC in the preview to continue."
    Accessible.onPressAction: root.previewRequested(root.markerId)

    onActiveFocusChanged: {
        if (activeFocus) root.focusEntered(root.markerId)
        else root.focusLeft(root.markerId)
    }
    Keys.onReturnPressed: root.previewRequested(root.markerId)
    Keys.onEnterPressed: root.previewRequested(root.markerId)
    Keys.onPressed: function(event) {
        var direction = ""
        if (event.key === Qt.Key_Left) direction = "left"
        else if (event.key === Qt.Key_Right) direction = "right"
        else if (event.key === Qt.Key_Up) direction = "up"
        else if (event.key === Qt.Key_Down) direction = "down"
        if (direction.length) {
            root.moveRequested(root.markerId, direction)
            event.accepted = true
        }
    }
    HoverHandler {
        id: pointerHover
        onHoveredChanged: {
            if (hovered) root.hoverEntered(root.markerId)
            else root.hoverLeft(root.markerId)
        }
    }
    // A pointer click does not activate or focus the seal. Dragging may pan the map.
    TapHandler { acceptedButtons: Qt.NoButton }

    Rectangle {
        anchors.centerIn: parent
        width: root.diameter + 9
        height: width
        radius: width / 2
        color: "transparent"
        border.width: root.activeFocus ? 2 : root.selected ? 1 : 0
        border.color: root.activeFocus ? "#523921" : "#987847"
    }
    Rectangle {
        anchors.centerIn: parent
        width: root.diameter
        height: width
        radius: width / 2
        color: root.activeFocus ? "#f3e3b9" : root.hovered ? "#eddbac" : "#dbc18d"
        border.width: 1.3
        border.color: "#8a683e"
        Behavior on color {
            enabled: !root.reducedMotion
            ColorAnimation { duration: 100 }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            color: "transparent"
            border.color: "#858a683e"
            border.width: 0.6
            radius: width / 2
        }
        Item {
            anchors.fill: parent
            anchors.margins: 3
            OnePieceRoundBadge {
                id: badgeImage
                objectName: "newWorldBadge-" + root.markerId
                anchors.fill: parent
                source: root.markerSource
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                mipmap: true
                sourceSize.width: Math.max(1, Math.ceil(root.diameter * 2))
                sourceSize.height: Math.max(1, Math.ceil(root.diameter * 2))
                visible: status === Image.Ready
            }
        }
        Text {
            anchors.centerIn: parent
            visible: badgeImage.status !== Image.Ready
            text: root.sealText
            color: "#65472a"
            font.family: root.fontFamily
            font.pixelSize: Math.max(13, root.diameter * 0.39)
        }
        Text {
            visible: root.additionalVisits
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: -3
            anchors.topMargin: -6
            text: "+"
            font.family: root.fontFamily
            font.pixelSize: 15
            color: "#65472a"
            style: Text.Outline
            styleColor: "#e2cc9b"
        }
    }
}
