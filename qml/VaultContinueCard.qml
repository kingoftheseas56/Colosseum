import QtQuick
import QtQuick.Effects

FocusScope {
    id: card
    property var row: ({})
    property bool keyboardFocused: activeFocus
    readonly property bool reading: row.kind === "book" || row.kind === "comic"
    signal activated(var row)
    width: 244; implicitHeight: 188; height: implicitHeight
    Theme { id: theme }
    Item {
        width: parent.width; height: 137
        y: action.hovered || card.keyboardFocused ? -4 : 0
        scale: action.pressed ? 0.98 : 1
        opacity: card.row.offline ? 0.48 : 1
        Behavior on y { NumberAnimation { duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic } }
        VaultArt { anchors.fill: parent; row: card.row; landscape: true; radius: 12; hovered: action.hovered; opacity: card.reading ? 0.55 : 1
            layer.enabled: card.reading && card.GraphicsInfo.api !== GraphicsInfo.Software
            layer.effect: MultiEffect { blurEnabled: true; blurMax: 24; blur: 0.8; brightness: -0.25 }
        }
        VaultArt { visible: card.reading; x: 16; y: 14; width: 73; height: 109; row: card.row; radius: 4 }
        Text {
            visible: card.reading; anchors { top: parent.top; right: parent.right; margins: 12 }
            text: card.row.kind === "book" ? "BOOK" : "COMIC"; font.family: theme.ui; font.pixelSize: 10
            font.letterSpacing: 1.4; color: theme.inkDim
        }
        Rectangle {
            anchors.bottom: parent.bottom; width: parent.width; height: 3; color: theme.edge
            Rectangle { width: parent.width * Math.max(0, Math.min(1, Number(card.row.progressFraction || 0))); height: 3; color: theme.gold }
        }
        FocusRing { anchors.fill: parent; radius: 12; shown: card.keyboardFocused }
    }
    Text { y: 146; width: parent.width; text: String(card.row.title || ""); color: theme.ink; font.family: theme.ui; font.pixelSize: 14; elide: Text.ElideRight }
    Text { y: 166; width: parent.width; text: card.row.offline ? "Source not connected" : String(card.row.progressText || card.row.subtitle || "In progress"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; elide: Text.ElideRight }
    KeyboardAction { id: action; anchors.fill: parent; showFocusFrame: false; accessibleName: "Continue " + String(card.row.title || ""); onTriggered: card.activated(card.row) }
}
