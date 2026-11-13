import QtQuick
import QtQuick.Controls
Button {
    id: root
    property string iconPath: ""
    property string detail: ""
    property int badge: 0
    property bool active: false
    Theme { id: theme }
    height: theme.vaultRowHeight
    hoverEnabled: true; activeFocusOnTab: true
    Accessible.name: text + (detail ? ", " + detail : "") + (badge ? ", " + badge : "")
    background: Rectangle {
        radius: theme.vaultControlRadius; color: root.down || root.active ? theme.glassHi : root.hovered ? theme.glassTint : "transparent"
        border.width: root.visualFocus ? 1 : 0; border.color: theme.gold
        Rectangle { x: 0; y: 10; width: 3; height: parent.height - 20; radius: 2; color: theme.gold; visible: root.active }
        Behavior on color { ColorAnimation { duration: theme.vaultMotionDuration } }
    }
    contentItem: Item {
        Image { id: icon; x: 6; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; source: root.iconPath; fillMode: Image.PreserveAspectFit }
        Text { anchors.left: icon.right; anchors.leftMargin: 12; anchors.right: trailing.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: root.text; elide: Text.ElideRight; color: root.active || root.hovered ? theme.ink : theme.inkDim; font.family: theme.ui; font.pixelSize: theme.vaultBody; font.weight: root.active ? Font.DemiBold : Font.Normal }
        Rectangle {
            id: trailing; anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
            width: Math.max(root.badge ? 20 : 0, number.implicitWidth + (root.badge ? 12 : 0)); height: 20; radius: 10; color: root.badge ? theme.gold : "transparent"
            Text { id: number; anchors.centerIn: parent; text: root.badge > 0 ? root.badge : root.detail; color: root.badge ? theme.vaultGoldInk : theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultCaption }
        }
    }
}
