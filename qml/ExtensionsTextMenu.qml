pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: menu
    required property TextInput editor
    objectName: "extensionsTextMenu"
    popupType: Popup.Item
    width: 244
    padding: 7
    margins: 8

    Theme { id: theme }

    background: Rectangle {
        radius: 14
        color: "#f5161a22"
        border.color: theme.edge
        border.width: 1
    }

    component MenuAction: MenuItem {
        id: item
        property string shortcutLabel: ""
        implicitHeight: 40
        leftPadding: 12
        rightPadding: 12
        contentItem: Item {
            Text {
                anchors.left: parent.left
                anchors.right: shortcut.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: item.text
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: item.enabled ? theme.ink : theme.inkDimmer
                opacity: item.enabled ? 1 : 0.5
                font.family: theme.ui
                font.pixelSize: 13
            }
            Text {
                id: shortcut
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: item.shortcutLabel
                color: item.enabled ? theme.inkDimmer : theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
            }
        }
        background: Rectangle {
            radius: 8
            color: item.highlighted || item.activeFocus ? theme.glassHi : "transparent"
            border.width: item.highlighted || item.activeFocus ? 1 : 0
            border.color: theme.gold
        }
        HoverHandler { cursorShape: item.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    }

    MenuAction {
        text: qsTr("Cut")
        shortcutLabel: "Ctrl+X"
        enabled: !menu.editor.readOnly && menu.editor.selectedText.length > 0
        onTriggered: menu.editor.cut()
    }
    MenuAction {
        text: qsTr("Copy")
        shortcutLabel: "Ctrl+C"
        enabled: menu.editor.selectedText.length > 0
        onTriggered: menu.editor.copy()
    }
    MenuAction {
        text: qsTr("Paste")
        shortcutLabel: "Ctrl+V"
        enabled: !menu.editor.readOnly && menu.editor.canPaste
        onTriggered: menu.editor.paste()
    }
    MenuAction {
        text: qsTr("Select all")
        shortcutLabel: "Ctrl+A"
        enabled: menu.editor.text.length > 0
        onTriggered: menu.editor.selectAll()
    }

    onClosed: editor.forceActiveFocus()
}
