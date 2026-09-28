// ExtensionsSetupSheet — an add-on's own setup page, inside Colosseum (never the outside browser).
// The page's "Install" button hands back a stremio:// (or …/manifest.json) link; the sheet catches
// that navigation and installs it. A paste box covers pages that only show the link to copy.
import QtQuick
import QtQuick.Controls
import QtWebEngine

Item {
    id: sheet

    property var addon: null                 // the Store card being set up
    readonly property bool open: sheet.addon !== null
    property string status: ""
    signal closeRequested()
    signal installRequested(string url)

    visible: sheet.open
    Theme { id: theme }

    function isAddonLink(url) {
        var u = String(url)
        return u.indexOf("stremio://") === 0 || /\/manifest\.json(\?.*)?$/i.test(u)
    }
    function finish(url) {
        sheet.status = "Adding…"
        sheet.installRequested(String(url))
    }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.62) }
    MouseArea { anchors.fill: parent; onClicked: sheet.closeRequested() }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(parent.width - 120, 1180)
        height: parent.height - 120
        radius: 24
        color: "#0e1016"
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12)
        clip: true
        MouseArea { anchors.fill: parent }      // clicks on the panel never close it

        Item {
            id: head
            width: parent.width; height: 64
            Text {
                anchors.left: parent.left; anchors.leftMargin: 26
                anchors.verticalCenter: parent.verticalCenter
                text: sheet.addon ? "Set up " + (sheet.addon.title || sheet.addon.name) : ""
                color: theme.ink
                font.family: theme.display; font.pixelSize: 22
            }
            Text {
                anchors.right: closeBox.left; anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                text: sheet.status
                color: theme.gold
                font.family: theme.ui; font.pixelSize: 13
            }
            Rectangle {
                id: closeBox
                anchors.right: parent.right; anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                width: 34; height: 34; radius: 17
                color: closeAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                Text { anchors.centerIn: parent; text: "✕"; color: theme.inkDim; font.pixelSize: 15 }
                KeyboardAction {
                    id: closeAction
                    anchors.fill: parent
                    accessibleName: "Close setup"
                    focusRadius: 17
                    onTriggered: sheet.closeRequested()
                }
            }
        }

        WebEngineView {
            id: web
            anchors.top: head.bottom; anchors.bottom: foot.top
            width: parent.width
            backgroundColor: "#0e1016"
            url: sheet.open ? (sheet.addon.configureUrl
                              || String(sheet.addon.manifestUrl).replace(/manifest\.json$/, "configure")) : "about:blank"
            onNavigationRequested: function(request) {
                if (sheet.isAddonLink(request.url)) {
                    request.action = WebEngineNavigationRequest.IgnoreRequest
                    sheet.finish(request.url)
                }
            }
            // Setup pages that open a new tab stay in this view.
            onNewWindowRequested: function(request) {
                if (sheet.isAddonLink(request.requestedUrl)) sheet.finish(request.requestedUrl)
                else web.url = request.requestedUrl
            }
        }

        Rectangle {
            id: foot
            anchors.bottom: parent.bottom
            width: parent.width; height: 62
            color: "#0b0c11"
            Row {
                anchors.fill: parent; anchors.leftMargin: 26; anchors.rightMargin: 18
                spacing: 12
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Link only?"
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 13
                }
                TextField {
                    id: paste
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 260
                    placeholderText: "Paste the add-on link the page gave you"
                    color: theme.ink
                    placeholderTextColor: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 13
                    background: Rectangle { radius: 16; color: Qt.rgba(1, 1, 1, 0.06); border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12) }
                    onAccepted: if (text.trim().length) sheet.finish(text.trim())
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 96; height: 34; radius: 17
                    color: paste.text.trim().length ? "#f5f3ee" : Qt.rgba(1, 1, 1, 0.08)
                    Text {
                        anchors.centerIn: parent
                        text: "Install"
                        color: paste.text.trim().length ? "#111217" : theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                    }
                    KeyboardAction {
                        anchors.fill: parent
                        enabled: paste.text.trim().length > 0
                        accessibleName: "Install pasted link"
                        focusRadius: 17
                        onTriggered: sheet.finish(paste.text.trim())
                    }
                }
            }
        }
    }

    onAddonChanged: { sheet.status = ""; paste.text = "" }
}
