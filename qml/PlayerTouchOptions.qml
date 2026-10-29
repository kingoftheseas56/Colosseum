import QtQuick

Rectangle {
    id: root
    property bool adjustmentsEnabled: false
    property bool fitAvailable: false
    signal closeRequested()
    signal toggleAdjustments()
    signal lockRequested()
    color: "#aa000000"
    Theme { id: theme }
    MouseArea { anchors.fill: parent; onClicked: root.closeRequested() }
    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(440,parent.width - Math.max(24, SafeArea.margins.left + SafeArea.margins.right))
        height: Math.min(parent.height - Math.max(24,SafeArea.margins.top + SafeArea.margins.bottom), content.implicitHeight + 32)
        radius: 18; color: "#20202a"; border.width: 1; border.color: theme.gold
        MouseArea { anchors.fill: parent }
        Flickable {
            anchors.fill: parent; anchors.margins: 16
            contentHeight: content.implicitHeight; contentWidth: width
            clip: true; boundsBehavior: Flickable.StopAtBounds
            Column {
                id: content
                width: parent.width; spacing: 12
                Text { width: parent.width; text: "Touch controls"; color: theme.gold; font.family: theme.ui; font.pixelSize: theme.fontSize(20) }
                Text {
                    width: parent.width
                    text: "Tap once to show or hide controls. Double-tap the left or right side to seek.\n\nOptional adjustments: swipe up or down on the left for brightness, or on the right for volume."
                        + (root.fitAvailable ? " Pinch out to fill the screen, or in to fit." : "")
                    wrapMode: Text.WordWrap; color: theme.inkDim; font.family: theme.ui; font.pixelSize: theme.fontSize(15)
                }
                Action { objectName: "playerTouchAdjustments"; label: "Swipe / pinch adjustments: " + (root.adjustmentsEnabled ? "On" : "Off"); onTriggered: root.toggleAdjustments() }
                Action { objectName: "playerTouchLock"; label: "Lock controls"; onTriggered: root.lockRequested() }
                Action { label: "Close"; onTriggered: root.closeRequested() }
            }
        }
    }
    component Action: Rectangle {
        id: action
        property string label
        signal triggered()
        width: parent.width; height: Math.max(48,text.implicitHeight + 16); radius: 12
        color: input.interactionActive ? "#51442a" : "#30303b"
        Text { id: text; anchors.centerIn: parent; width: parent.width-24; text: action.label; color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(15); wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
        KeyboardAction { id: input; anchors.fill: parent; accessibleName: action.label; onTriggered: action.triggered() }
    }
}
