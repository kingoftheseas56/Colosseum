pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    Theme { id: theme }
    implicitHeight: 34
    hoverEnabled: true
    contentItem: Text {
        leftPadding: 12; rightPadding: 28
        text: control.displayText; color: theme.ink
        font.family: theme.ui; font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    indicator: Text { x: control.width - 22; y: (control.height - height) / 2; text: "⌄"; color: theme.inkDim; font.pixelSize: 15 }
    background: Rectangle {
        radius: theme.vaultControlRadius
        color: control.down || control.hovered ? theme.vaultSurfaceHi : theme.vaultSurface
        border.color: control.visualFocus ? theme.gold : theme.edge
        Behavior on color { ColorAnimation { duration: theme.vaultMotionDuration } }
    }
    delegate: ItemDelegate {
        id: option
        required property var modelData
        required property int index
        width: control.width; height: 36
        highlighted: control.highlightedIndex === index
        contentItem: Text { text: String(option.modelData); color: theme.ink; font.family: theme.ui; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { color: option.highlighted ? theme.vaultSurfaceHi : theme.vaultSurface; radius: 6 }
    }
    popup: Popup {
        y: control.height + 4; width: control.width
        padding: 4; implicitHeight: contentItem.implicitHeight + 8
        contentItem: ListView { clip: true; implicitHeight: contentHeight; model: control.popup.visible ? control.delegateModel : null; currentIndex: control.highlightedIndex }
        background: Rectangle { radius: theme.vaultControlRadius; color: theme.vaultSurface; border.color: theme.edge }
    }
}
