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
        deck.closeTab(sessionId)
    }
    onActiveChanged: if (active && !retiring && entry && !entry.retiring) deck.sessions.switchTo(sessionId)
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Back"; onClicked: deck.back(host.sessionId) }
            Button { text: "Forward"; onClicked: deck.forward(host.sessionId) }
            Button { text: "Reload"; onClicked: deck.reload(host.sessionId) }
            Button { text: "Address"; checkable: true; onToggled: address.visible = checked }
        }
        TextField { id: address; visible: false; Layout.fillWidth: true; placeholderText: "Address or search"
            onAccepted: deck.navigateInput(host.sessionId, text) }
        Item { id: viewport; Layout.fillWidth: true; Layout.fillHeight: true }
    }
}
