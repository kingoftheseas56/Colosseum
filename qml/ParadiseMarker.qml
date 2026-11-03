pragma ComponentBehavior: Bound
import QtQuick

FocusScope {
    id: root
    required property string markerId
    required property string title
    property url badgeSource: ""
    property bool badgeFailed: false
    onBadgeSourceChanged: badgeFailed = false
    property string sealText: ""
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
    objectName: "paradiseMarker-" + markerId

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
    // A tap opens the preview; dragging remains available to the map viewport.
    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        onTapped: root.previewRequested(root.markerId)
    }

    Rectangle {
        anchors.centerIn: parent
        width: root.diameter + 9
        height: width
        radius: width / 2
        color: "transparent"
        border.width: root.activeFocus ? 2 : root.selected ? 1 : 0
        border.color: root.activeFocus ? "#523921" : "#987847"
    }
    Item {
        id: badgeArt
        anchors.centerIn: parent
        width: root.diameter
        height: width
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: root.activeFocus ? "#f3e3b9" : root.hovered ? "#eddbac" : "#dbc18d"
            border.width: 1.3
            border.color: "#8a683e"
            Behavior on color {
                enabled: !root.reducedMotion
                ColorAnimation { duration: 100 }
            }
        }
        OnePieceRoundBadge {
            id: badgeImage
            objectName: "paradiseBadge-" + root.markerId
            anchors.fill: parent
            source: root.badgeSource
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            smooth: true
            mipmap: true
            sourceSize.width: Math.max(1, Math.ceil(root.diameter * 2))
            sourceSize.height: Math.max(1, Math.ceil(root.diameter * 2))
            opacity: status === Image.Ready && !root.badgeFailed ? 1 : 0
            onStatusChanged: if (status === Image.Error) root.badgeFailed = true
        }
        Text {
            anchors.centerIn: parent
            objectName: "paradiseSealFallback-" + root.markerId
            visible: badgeImage.status !== Image.Ready || root.badgeFailed
            text: root.sealText
            color: "#65472a"
            font.family: root.fontFamily
            font.pixelSize: Math.max(13, root.diameter * 0.39)
        }
    }
    Text {
        visible: root.additionalVisits
        anchors.right: badgeArt.right
        anchors.top: badgeArt.top
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
