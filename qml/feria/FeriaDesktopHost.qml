import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import ".." as Colosseum

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
    color: hostTheme.biblioWashTop
    Colosseum.Theme { id: hostTheme }
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
    // Native focus is window-local, not another writer of sidebar selection.
    // Foreground requests/selecting a space use SessionStore explicitly; a late
    // platform activation event cannot undo a newer canonical selection.
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        // Identity must remain visible even in the smallest site-created dialog.
        FeriaSiteIdentity {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: implicitHeight
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            currentUrl: host.entry && host.entry.browser ? host.entry.browser.url : "about:blank"
            loading: !!(host.entry && host.entry.browser && host.entry.browser.loadActive)
            documentLoaded: !!(host.entry && host.entry.browser && host.entry.browser.documentReady
                               && !host.entry.browser.rendererLost)
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Back"; Layout.fillWidth: true; Layout.minimumWidth: 0
                onClicked: host.deck.hostCommand(host.lease, "back", "") }
            Button { text: "Forward"; Layout.fillWidth: true; Layout.minimumWidth: 0
                onClicked: host.deck.hostCommand(host.lease, "forward", "") }
            Button { text: "Reload"; Layout.fillWidth: true; Layout.minimumWidth: 0
                onClicked: host.deck.hostCommand(host.lease, "reload", "") }
            Button { text: "Address"; Layout.fillWidth: true; Layout.minimumWidth: 0
                checkable: true; onToggled: address.visible = checked }
        }
        TextField { id: address; visible: false; Layout.fillWidth: true; placeholderText: "Address or search"
            onAccepted: host.deck.hostCommand(host.lease, "navigate", text) }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: host.entry ? host.entry.errorText : ""
            color: hostTheme.ink
            wrapMode: Text.WordWrap
        }
        Item { id: viewport; Layout.fillWidth: true; Layout.fillHeight: true }
    }
}
