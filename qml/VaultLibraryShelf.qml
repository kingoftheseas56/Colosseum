pragma ComponentBehavior: Bound
import QtQuick

FocusScope {
    id: shelf
    property string title: ""
    property string kind: ""
    property var rows: []
    property bool continueShelf: false
    property bool showSeeAll: !continueShelf
    signal activated(var row)
    signal hideRequested(var row)
    signal showKind(string kind)
    signal moveUp()
    signal moveDown()
    function focusFirst() { rail.forceActiveFocus(); rail.currentIndex = Math.max(0, rail.currentIndex) }
    implicitHeight: rows.length ? (continueShelf ? 232 : theme.vaultPosterWidth * 1.5 + 108) : 0
    height: implicitHeight
    Theme { id: theme }
    Text { text: shelf.title; color: theme.ink; font.family: theme.display; font.pixelSize: 24 }
    Text { x: heading.implicitWidth + 12; y: 10; text: String(shelf.rows.length); visible: !shelf.continueShelf; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
    Text { id: heading; text: shelf.title; visible: false; font.family: theme.display; font.pixelSize: 24 }
    Rectangle {
        visible: shelf.showSeeAll; anchors.right: parent.right; width: 88; height: 30; radius: 8
        color: seeAll.interactionActive ? theme.vaultSurfaceHi : "transparent"
        Text { anchors.centerIn: parent; text: "See all ›"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
        KeyboardAction { id: seeAll; anchors.fill: parent; accessibleName: "See all " + shelf.title; onTriggered: shelf.showKind(shelf.kind) }
    }
    ListView {
        id: rail
        y: 40; width: parent.width; height: parent.height - y
        clip: true; orientation: ListView.Horizontal; spacing: theme.vaultGap
        model: shelf.rows; boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Up) { shelf.moveUp(); event.accepted = true }
            else if (event.key === Qt.Key_Down) { shelf.moveDown(); event.accepted = true }
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (currentIndex >= 0 && currentIndex < shelf.rows.length) shelf.activated(shelf.rows[currentIndex])
                event.accepted = true
            }
        }
        delegate: Loader {
            id: delegateLoader
            required property var modelData
            required property int index
            width: shelf.continueShelf ? 244 : theme.vaultPosterWidth
            height: rail.height
            sourceComponent: shelf.continueShelf ? continueComponent : posterComponent
            Component {
                id: posterComponent
                VaultLibraryPosterCard {
                    width: parent.width; row: delegateLoader.modelData
                    keyboardFocused: rail.activeFocus && rail.currentIndex === delegateLoader.index || activeFocus
                    onActivated: row => { rail.currentIndex = delegateLoader.index; shelf.activated(row) }
                    onHideRequested: row => shelf.hideRequested(row)
                }
            }
            Component {
                id: continueComponent
                VaultContinueCard {
                    row: delegateLoader.modelData
                    keyboardFocused: rail.activeFocus && rail.currentIndex === delegateLoader.index || activeFocus
                    onActivated: row => { rail.currentIndex = delegateLoader.index; shelf.activated(row) }
                }
            }
        }
    }
}
