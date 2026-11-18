// Small house control shared by the new Vault surfaces.
import QtQuick
import QtQuick.Controls
Button {
    id: control
    property bool primary: false
    property bool quiet: false
    property bool selected: false
    property string iconPath: ""
    property string accessibleName: text
    Theme { id: skin }
    implicitWidth: Math.max(36, content.implicitWidth + 28)
    implicitHeight: 38
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.name: accessibleName
    opacity: enabled ? 1 : 0.45
    contentItem: Row {
        id: content
        spacing: 8
        Image { visible: control.iconPath.length > 0; source: control.iconPath; width: visible ? 18 : 0; height: 18; anchors.verticalCenter: parent.verticalCenter }
        Text { text: control.text; anchors.verticalCenter: parent.verticalCenter; color: control.primary ? skin.vaultGoldInk : skin.ink; font.family: skin.ui; font.pixelSize: skin.vaultBody; elide: Text.ElideRight }
    }
    background: Rectangle {
        radius: skin.vaultControlRadius
        color: control.primary ? skin.gold : control.down ? skin.glassHi : control.selected || control.hovered ? skin.glassHi : control.quiet ? "transparent" : skin.glassTint
        border.width: control.visualFocus || !control.quiet ? 1 : 0
        border.color: control.visualFocus || control.selected ? skin.gold : skin.edge
        scale: control.down ? 0.97 : 1
        Behavior on color { ColorAnimation { duration: skin.vaultMotionDuration } }
        Behavior on scale { NumberAnimation { duration: skin.vaultMotionDuration; easing.type: Easing.OutCubic } }
    }
}
