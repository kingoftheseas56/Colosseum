import QtQuick
import QtQuick.Controls

// App Max is an Item in the existing app window, not native maximization and
// not website fullscreen. Restore chrome sits above the page, never over it.
Item {
    id: host
    required property var deck
    required property string sessionId
    readonly property alias viewport: viewport
    anchors.fill: parent
    Rectangle {
        id: chrome
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 44; color: "#202936"
        Button { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            text: "Restore"; onClicked: host.deck.restoreSession(host.sessionId) }
    }
    Item { id: viewport; anchors.top: chrome.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom }
}
