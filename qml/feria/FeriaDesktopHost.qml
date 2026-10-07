import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

// Disposable native presentation only. The deck retains QObject ownership and
// creation context of every live entry/view; this host never owns WebContents.
Window {
    id: host
    required property var deck
    required property string sessionId
    property bool dialogWindow: false
    property bool retiring: false
    property var lease: null
    readonly property var entry: { deck.presentationRevision; return deck.entries[sessionId] || null }
    readonly property alias viewport: viewport
    title: entry && entry.browser ? entry.browser.title : "Feria"
    flags: dialogWindow ? Qt.Dialog : Qt.Window
    width: 900; height: 700
    function applyGeometry(rect) {
        var bounded = deck.policy.boundedWindowGeometry(rect, host)
        if (bounded.width <= 0 || bounded.height <= 0) return false
        x = bounded.x; y = bounded.y; width = bounded.width; height = bounded.height
        return true
    }
    onClosing: function(event) {
        if (retiring) return
        event.accepted = false
        deck.hostCommand(lease, "close", "")
    }
    onActiveChanged: if (active && !retiring && entry && !entry.retiring) deck.hostCommand(lease, "activate", "")
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Back"; onClicked: host.deck.hostCommand(host.lease, "back", "") }
            Button { text: "Forward"; onClicked: host.deck.hostCommand(host.lease, "forward", "") }
            Button { text: "Reload"; onClicked: host.deck.hostCommand(host.lease, "reload", "") }
            Button { text: "Address"; checkable: true; onToggled: address.visible = checked }
        }
        TextField { id: address; visible: false; Layout.fillWidth: true; placeholderText: "Address or search"
            onAccepted: host.deck.hostCommand(host.lease, "navigate", text) }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: host.entry ? host.entry.errorText : ""
            wrapMode: Text.WordWrap
        }
        Item { id: viewport; Layout.fillWidth: true; Layout.fillHeight: true }
    }
}
