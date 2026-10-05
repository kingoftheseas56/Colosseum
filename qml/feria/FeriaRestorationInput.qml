// Observes deliberate input without consuming it or interpreting navigation.
import QtQuick

Item {
    id: root
    anchors.fill: parent
    z: 1000 // Passive handlers observe before child controls; no pointer grab/consumption.
    signal userInput()
    property point previousPointer: Qt.point(-1, -1)
    Keys.onPressed: function(event) {
        root.userInput()
        event.accepted = false
    }
    PointHandler {
        acceptedButtons: Qt.AllButtons
        onActiveChanged: if (active) root.userInput()
    }
    WheelHandler {
        target: null
        blocking: false
        onWheel: function(event) {
            root.userInput()
            event.accepted = false
        }
    }
    HoverHandler {
        onPointChanged: {
            var next = point.scenePosition
            if (root.previousPointer.x >= 0
                    && (next.x !== root.previousPointer.x || next.y !== root.previousPointer.y))
                root.userInput()
            root.previousPointer = next
        }
    }
}
