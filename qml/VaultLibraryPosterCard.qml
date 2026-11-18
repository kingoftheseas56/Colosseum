import QtQuick
import QtQuick.Controls

// Compose the catalogue's title/activation grammar with Vault-owned state and cover treatment.
FocusScope {
    id: card
    property var row: ({})
    property bool keyboardFocused: activeFocus
    property bool hideAllowed: true
    property bool dimmed: row.dimmed === true || row.offline === true
    readonly property bool reading: row.kind === "book" || row.kind === "comic"
    readonly property real progress: Math.max(0, Math.min(1, Number(row.progressFraction || 0)))
    signal activated(var row)
    signal hideRequested(var row)
    width: theme.vaultPosterWidth
    implicitHeight: width * 1.5 + 64
    height: implicitHeight
    opacity: dimmed ? 0.48 : 1
    Theme { id: theme }
    CataloguePosterCard {
        id: catalogue
        anchors.fill: parent
        item: card.row
        visualProfile: "classic"
        keyboardFocused: card.keyboardFocused
        onActivated: card.activated(card.row)
    }
    Item {
        width: parent.width; height: width * 1.5
        y: hover.hovered || card.keyboardFocused ? -4 : 0
        scale: action.pressed ? 0.98 : 1
        Behavior on y { NumberAnimation { duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic } }
        VaultArt { anchors.fill: parent; row: card.row; radius: card.reading ? 5 : 10; hovered: hover.hovered }
        Rectangle {
            visible: card.reading; x: 4; width: 4; height: parent.height - 4; y: 2
            color: theme.vaultScrim; opacity: 0.65
        }
        Rectangle {
            visible: card.row.isNew === true; x: 8; y: 8; width: 36; height: 19; radius: 4; color: theme.gold
            Text { anchors.centerIn: parent; text: "NEW"; color: theme.vaultBackdrop; font.family: theme.ui; font.pixelSize: 9; font.bold: true }
        }
        Rectangle {
            visible: card.row.completed === true; anchors { right: parent.right; top: parent.top; margins: 8 }
            width: 22; height: 22; radius: 11; color: theme.vaultScrim
            Text { anchors.centerIn: parent; text: "✓"; color: theme.ink; font.pixelSize: 13 }
        }
        Rectangle {
            visible: card.progress > 0 && !card.row.completed
            x: 8; y: parent.height - 11; width: parent.width - 16; height: 3; radius: 2; color: theme.vaultScrim
            Rectangle { width: parent.width * card.progress; height: parent.height; radius: 2; color: theme.gold }
        }
        FocusRing { anchors.fill: parent; shown: card.keyboardFocused; radius: card.reading ? 5 : 10 }
    }
    Text {
        objectName: "vaultPosterSubtitle"
        x: 0; y: card.width * 1.5 + 28; width: parent.width
        text: card.row.offline ? (card.row.serverId ? "Server unavailable" : "Drive not connected") : String(card.row.subtitle || card.row.author || card.row.year || "")
        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; elide: Text.ElideRight
    }
    HoverHandler { id: hover }
    KeyboardAction {
        id: action; objectName: "vaultPosterAction"; anchors.fill: parent; showFocusFrame: false
        accessibleName: String(card.row.title || "Open title")
        contextEnabled: card.hideAllowed
        onTriggered: card.activated(card.row)
        onContextRequested: if (card.hideAllowed) menu.popup()
    }
    Menu { id: menu; objectName: "vaultPosterMenu"; MenuItem { visible: card.hideAllowed; enabled: card.hideAllowed; text: "Hide from Vault"; onTriggered: card.hideRequested(card.row) } }
}
