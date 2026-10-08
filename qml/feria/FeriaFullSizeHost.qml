import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// App Max is an Item in the existing app window, not native maximization and
// not website fullscreen. Restore chrome sits above the page, never over it.
Item {
    id: host
    required property var deck
    required property string sessionId
    property var lease: null
    readonly property var entry: { deck.presentationRevision; return deck.entries[sessionId] || null }
    readonly property var pageTools: entry && entry.browser ? entry.browser.pageTools : null
    readonly property alias viewport: viewport
    Binding {
        target: host.entry ? host.entry.browser : null
        property: "saveContextControlsAvailable"; value: true
        when: host.visible && host.entry !== null && host.entry.fullSizeHost === host && host.entry.browser !== null
        restoreMode: Binding.RestoreNone
    }
    anchors.fill: parent
    visible: deck.displayed && !deck.suppressed && deck.sessions.activeId === sessionId
    Rectangle {
        id: chrome
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 44 + saveContextBar.height; color: "#202936"
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 44
            Button { text: "Save page"; enabled: host.pageTools && host.pageTools.canSavePage
                onClicked: if (host.deck.hostLeaseCurrent(host.lease)) host.pageTools.savePage() }
            Item { Layout.fillWidth: true }
            Button { text: "Restore"; onClicked: host.deck.hostCommand(host.lease, "restore", "") }
        }
        FeriaSaveContextBar { id: saveContextBar; anchors.left: parent.left; anchors.right: parent.right; y: 44; tools: host.pageTools }
    }
    Item { id: viewport; anchors.top: chrome.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom }
}
