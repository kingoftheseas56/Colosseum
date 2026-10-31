import QtQuick

Item {
    id: root
    property bool gestureActive: false
    property real progress: 0
    property int edge: 0
    visible: gestureActive
    enabled: false
    Accessible.ignored: true
    Theme { id: theme }
    Rectangle {
        objectName: "androidBackGestureFeedback"
        width: 48; height: 48; radius: 24
        x: root.edge === 0 ? Math.max(12, SafeArea.margins.left) + 16 * root.progress
            : root.width - width - Math.max(12, SafeArea.margins.right) - 16 * root.progress
        anchors.verticalCenter: parent.verticalCenter
        color: "#15151d"
        border.width: 1 + root.progress * 2
        border.color: theme.gold
        opacity: 0.6 + 0.4 * root.progress
        BackAction {
            anchors.centerIn: parent
            variant: "immersive"
            tip: "Back"
            idleColor: theme.gold
            Accessible.ignored: true
        }
    }
}
