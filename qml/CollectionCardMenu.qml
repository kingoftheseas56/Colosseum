import QtQuick

TouchTarget {
    id: button
    property bool highlighted: false
    visualWidth: touch ? minimumTargetSize : 26
    visualHeight: visualWidth
    accessibleName: qsTr("More actions")
    focusEnabled: false
    focusOnPointer: false
    focusPolicy: Qt.NoFocus
    Theme { id: theme }
    Rectangle {
        anchors.fill: parent
        radius: 8
        color: Qt.rgba(0.04, 0.04, 0.075, 0.82)
        border.width: 1
        border.color: button.highlighted ? theme.gold : theme.edge
        Text { anchors.centerIn: parent; text: "⋮"; color: theme.ink; font.pixelSize: 15 }
    }
}
