// WidgetHeader — the header strip for a world-page widget: title (editorial serif)
// + optional hint + optional "more ›" affordance. Sits directly on the wallpaper (no panel).

import QtQuick

Item {
    id: head

    property string title
    property string sub: ""
    property string moreLabel: ""        // optional verb before the chevron (e.g. "Explore")
    property bool navigable: true        // show the right-aligned nav-in chevron
    signal moreClicked()

    implicitHeight: 30
    width: parent ? parent.width : row.implicitWidth

    Theme { id: theme }

    Row {
        id: row
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        spacing: 10
        Text {
            text: head.title; color: theme.ink
            font.family: theme.display; font.pixelSize: 22
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: head.sub !== ""
            text: head.sub; color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 12
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    // nav-in affordance — a right-aligned serif chevron (ink; gold only on HOVER, keeping gold
    // sparing), optionally led by a verb (moreLabel). The row's own title is the label; the chevron
    // is the consistent "go deeper" cue across every widget. Wrapped in an Item so the fill
    // MouseArea's anchors are legal inside the header.
    Item {
        id: more
        visible: head.navigable
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        implicitWidth: moreRow.implicitWidth; implicitHeight: 30
        Row {
            id: moreRow
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                visible: head.moreLabel !== ""
                text: head.moreLabel
                color: moreMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.display; font.pixelSize: 17
                anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 120 } }
            }
            Text {
                text: "›"
                color: moreMa.containsMouse ? theme.gold : theme.inkDim
                font.family: theme.display; font.pixelSize: 22
                anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 120 } }
            }
        }
        MouseArea {
            id: moreMa
            anchors.fill: parent; anchors.margins: -8
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: head.moreClicked()
        }
    }
}
