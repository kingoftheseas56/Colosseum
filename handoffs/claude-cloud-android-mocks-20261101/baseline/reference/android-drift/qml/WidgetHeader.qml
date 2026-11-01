// WidgetHeader — the header strip for a world-page widget: title (editorial serif)
// + optional hint + optional "more ›" affordance. Sits directly on the wallpaper (no panel).

import QtQuick
import QtQuick.Window

Item {
    id: head

    property string title
    property string sub: ""
    property string moreLabel: ""        // optional verb before the chevron (e.g. "Explore")
    property bool navigable: true        // show the right-aligned nav-in chevron
    property string automationId: ""
    AdaptiveLayout { id:metrics; page:head }
    readonly property bool phoneLayout: metrics.phoneLayout
    signal moreClicked()

    implicitHeight: metrics.touch ? metrics.touchTargetSize : 30
    width: parent ? parent.width : row.implicitWidth

    Theme { id: theme }

    Row {
        id: row
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        Binding on width { value:Math.max(0,head.width-(more.visible ? more.width+12 : 0)); when:head.phoneLayout; restoreMode:Binding.RestoreBindingOrValue }
        spacing: 10
        Text {
            Binding on width { value:Math.max(0,row.width-(subText.visible ? subText.width+row.spacing : 0)); when:head.phoneLayout; restoreMode:Binding.RestoreBindingOrValue }
            text: head.title; color: theme.ink
            font.family: theme.display; font.pixelSize: theme.fontSize(head.phoneLayout ? 18 : 22)
            elide: head.phoneLayout ? Text.ElideRight : Text.ElideNone
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: subText
            Binding on width { value:Math.min(subText.implicitWidth,row.width*0.35); when:head.phoneLayout; restoreMode:Binding.RestoreBindingOrValue }
            visible: head.sub !== ""
            text: head.sub; color: theme.inkDim
            font.family: theme.ui; font.pixelSize: theme.fontSize(12)
            elide: head.phoneLayout ? Text.ElideRight : Text.ElideNone
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
        implicitWidth: metrics.touch ? Math.max(metrics.touchTargetSize, moreRow.implicitWidth) : moreRow.implicitWidth
        implicitHeight: metrics.touch ? metrics.touchTargetSize : 30
        Row {
            id: moreRow
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                visible: head.moreLabel !== "" && !head.phoneLayout
                text: head.moreLabel
                color: input.interactionActive ? theme.gold : theme.ink
                font.family: theme.display; font.pixelSize: theme.fontSize(17)
                anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 120 } }
            }
            Text {
                text: "›"
                color: input.interactionActive ? theme.gold : theme.inkDim
                font.family: theme.display; font.pixelSize: 22
                anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 120 } }
            }
        }
        KeyboardAction {
            id: input
            objectName: head.automationId.length > 0 ? head.automationId : ""
            anchors.fill: parent
            anchors.margins: head.phoneLayout ? 0 : -8
            accessibleName: head.moreLabel !== "" ? head.moreLabel + " " + head.title : "Open " + head.title
            focusRadius: 7
            // A row's header link is reached by Up from its own row, never by Down (the mock's rule;
            // otherwise the right-aligned links formed a column the arrows ran along, skipping rows).
            readonly property bool keyboardHeaderLink: true
            readonly property Item keyboardHeaderRow: head.parent
            onTriggered: head.moreClicked()
        }
    }
}
