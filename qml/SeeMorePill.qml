// SeeMorePill — the "See more · N / Show less" toggle under a capped result grid.
// Mirrors SearchSurface's group-expand pill so Biblio search feels the same.
import QtQuick

Item {
    id: root
    property int extra: 0          // how many more beyond the one shown row
    property bool expanded: false
    signal toggled()
    visible: extra > 0
    implicitWidth: pillRow.implicitWidth + 30
    implicitHeight: 34
    width: implicitWidth; height: implicitHeight

    Theme { id: theme }
    Rectangle {
        anchors.fill: parent; radius: 17
        color: ma.containsMouse ? theme.glassHi : theme.glassTint
        border.width: 1
        border.color: ma.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.55) : theme.edge
        Row {
            id: pillRow; anchors.centerIn: parent; spacing: 7
            Text {
                text: root.expanded ? "Show less" : ("See more · " + root.extra)
                color: ma.containsMouse ? theme.gold : theme.inkDim
                font.family: theme.ui; font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: root.expanded ? "▴" : "▾"
                color: ma.containsMouse ? theme.gold : theme.inkDimmer
                font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter
            }
        }
        MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: root.toggled() }
    }
}
