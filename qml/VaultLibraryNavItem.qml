import QtQuick
import QtQuick.Controls
Button {
    id: root
    property string iconPath: ""
    property string detail: ""
    property string statusKind: ""
    property string statusText: ""
    property int iconSize: 18
    property int badge: 0
    property bool active: false
    property bool collapsed: false
    property bool addIcon: false
    Theme { id: theme }
    height: theme.vaultRowHeight
    hoverEnabled: true; activeFocusOnTab: true
    Accessible.name: text + (statusText ? ", " + statusText : detail ? ", " + detail : "") + (badge ? ", " + badge : "")
    ToolTip.text: Accessible.name
    ToolTip.delay: 350
    ToolTip.visible: (collapsed || statusKind.length > 0) && (hovered || activeFocus)
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    background: Rectangle {
        radius: theme.vaultControlRadius; color: root.down || root.active ? theme.glassHi : root.hovered ? theme.glassTint : "transparent"
        border.width: root.visualFocus ? 1 : 0; border.color: theme.gold
        Rectangle { x: 0; y: 10; width: 3; height: parent.height - 20; radius: 2; color: theme.gold; visible: root.active }
        Behavior on color { ColorAnimation { duration: theme.vaultMotionDuration } }
    }
    contentItem: Item {
        Image {
            id: icon
            objectName: root.objectName + "Icon"
            x: root.collapsed ? (root.statusKind.length > 0 ? 4 : (parent.width - width) / 2) : 6
            anchors.verticalCenter: parent.verticalCenter; width: root.iconSize; height: root.iconSize
            source: root.iconPath; sourceSize.width: 36; fillMode: Image.PreserveAspectFit
            Accessible.ignored: true
            Text { visible: root.addIcon; anchors.centerIn: parent; anchors.verticalCenterOffset: 2; text: "+"; color: theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold }
        }
        Text { objectName: root.objectName + "Label"; visible: !root.collapsed; anchors.left: icon.right; anchors.leftMargin: 12; anchors.right: root.statusKind.length > 0 ? statusVisual.left : trailing.left; anchors.rightMargin: root.statusKind.length > 0 ? 4 : 8; anchors.verticalCenter: parent.verticalCenter; text: root.text; elide: Text.ElideRight; color: root.active || root.hovered ? theme.ink : theme.inkDim; font.family: theme.ui; font.pixelSize: theme.vaultBody; font.weight: root.active ? Font.DemiBold : Font.Normal }
        Rectangle {
            id: trailing; anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
            objectName: root.objectName + "Count"
            visible: !root.collapsed && root.statusKind.length === 0 && (root.detail.length > 0 || root.badge > 0)
            width: Math.max(root.badge ? 20 : 0, number.implicitWidth + (root.badge ? 12 : 0)); height: 20; radius: 10; color: root.badge ? theme.gold : "transparent"
            Text { id: number; anchors.centerIn: parent; visible: root.statusKind.length === 0; text: root.badge > 0 ? root.badge : root.detail; color: root.badge ? theme.vaultGoldInk : theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultCaption }
        }
        Item {
            id: statusVisual
            objectName: root.objectName + "Status"
            visible: root.statusKind.length > 0
            width: root.collapsed ? 13 : 18; height: width
            x: root.collapsed ? parent.width - width - 2 : parent.width - width - 4
            y: root.collapsed ? parent.height - height - 1 : (parent.height - height) / 2
            Accessible.ignored: true
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: root.statusKind === "linked"
                       ? Qt.rgba(theme.vaultHealthy.r, theme.vaultHealthy.g, theme.vaultHealthy.b, 0.18)
                       : root.statusKind === "attention"
                         ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.14) : theme.vaultSurface
                border.width: 1
                border.color: root.statusKind === "linked" ? theme.vaultHealthy
                             : root.statusKind === "attention" ? theme.gold : theme.edge
            }
            Image {
                objectName: root.objectName + "StatusGlyph"
                anchors.centerIn: parent
                width: root.collapsed ? 9 : 13; height: width
                source: root.statusKind === "linked" ? "../assets/icons/lucide/circle-check.svg"
                       : root.statusKind === "attention" ? "../assets/icons/lucide/triangle-alert.svg"
                       : "../assets/icons/link.svg"
                sourceSize.width: 26
                fillMode: Image.PreserveAspectFit
                visible: root.statusKind !== "busy"
            }
            Rectangle {
                visible: root.statusKind === "unlinked"
                anchors.centerIn: parent
                width: root.collapsed ? 10 : 15; height: 1.5; radius: 1
                rotation: -45; color: theme.inkDim
            }
            BusyIndicator {
                visible: root.statusKind === "busy"
                running: visible
                anchors.centerIn: parent
                width: root.collapsed ? 11 : 15; height: width
                palette.highlight: theme.inkDim
                palette.buttonText: theme.inkDim
                palette.windowText: theme.inkDim
            }
        }
    }
}
