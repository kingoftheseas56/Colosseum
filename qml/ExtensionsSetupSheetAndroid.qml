// ExtensionsSetupSheetAndroid — an add-on's setup on a phone. Android ships no QtWebEngine, so
// the setup page opens in the phone's browser (Hemanth's ruling, 2026-10-04); the link it gives
// comes back through the clipboard. Same face as ExtensionsSetupSheet, so the Store drives either.
import QtQuick
import QtQuick.Controls

Item {
    id: sheet
    objectName: "extensionsSetupSheet"

    property var addon: null                 // the Store card being set up
    readonly property bool open: sheet.addon !== null
    property string status: ""
    property string pendingUrl: ""           // the normalized link last handed to install
    // Setup is optional (configurable, not configurationRequired): the plain link also works.
    readonly property bool canSkip: sheet.open && sheet.addon.setupRequired !== true
                                    && typeof Extensions !== "undefined"
                                    && !sheet.switchedOnAt(sheet.addon.manifestUrl)
    readonly property string setupUrl: sheet.open
        ? String(sheet.addon.configureUrl
                 || String(sheet.addon.manifestUrl).replace(/manifest\.json$/, "configure")) : ""
    // A built-in add-on that ships switched off (Torrentio) is present but not on: the plain
    // link must stay one tap away instead of forcing the browser setup round trip.
    function switchedOnAt(url) {
        var u = Extensions.normalizeUrl(url)
        var rows = Extensions.installed()
        for (var i = 0; i < rows.length; ++i)
            if (rows[i].transportUrl === u && rows[i].enabled !== false)
                return true
        return false
    }
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
    function pasteAndInstall() {
        paste.clear(); paste.paste()
        var t = paste.text.trim()
        if (sheet.isAddonLink(t)) sheet.finish(t)
        else sheet.status = t.length ? "That isn't an add-on link." : "The clipboard is empty."
    }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.62) }
    MouseArea { anchors.fill: parent; onClicked: sheet.closeRequested() }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 520)
        height: body.implicitHeight + 44
        radius: 24
        color: "#0e1016"
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12)
        MouseArea { anchors.fill: parent }      // taps on the panel never close it

        Column {
            id: body
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 22
            spacing: 16

            Item {
                width: parent.width; height: 34
                Text {
                    anchors.left: parent.left; anchors.right: closeBox.left; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: sheet.addon ? "Set up " + (sheet.addon.title || sheet.addon.name) : ""
                    color: theme.ink
                    font.family: theme.display; font.pixelSize: 22
                }
                Rectangle {
                    id: closeBox
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34; height: 34; radius: 17
                    color: closeAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                    // "×", not the desktop sheet's "✕": Android's system font has no glyph for that one.
                    Text { anchors.centerIn: parent; text: "×"; color: theme.inkDim; font.pixelSize: 22 }
                    KeyboardAction {
                        id: closeAction
                        anchors.fill: parent
                        accessibleName: "Close setup"
                        focusRadius: 17
                        onTriggered: sheet.closeRequested()
                    }
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Setup opens in your browser. Finish it there, copy the add-on link it gives you, then come back and paste it."
                color: theme.inkDim
                font.family: theme.ui; font.pixelSize: 14
            }

            Rectangle {
                width: parent.width; height: 44; radius: 22
                color: "#f5f3ee"
                Text {
                    anchors.centerIn: parent
                    text: "Open setup page"
                    color: "#111217"
                    font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                }
                KeyboardAction {
                    anchors.fill: parent
                    accessibleName: "Open the setup page in the browser"
                    focusRadius: 22
                    onTriggered: Qt.openUrlExternally(sheet.setupUrl)
                }
            }

            TextField {
                id: paste
                width: parent.width; height: 44
                leftPadding: 16; rightPadding: 16
                placeholderText: "Paste the add-on link the page gave you"
                color: theme.ink
                placeholderTextColor: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 13
                background: Rectangle { radius: 16; color: Qt.rgba(1, 1, 1, 0.06); border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12) }
                onAccepted: if (text.trim().length) sheet.finish(text.trim())
            }

            Rectangle {
                width: parent.width; height: 44; radius: 22
                color: clipAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.16)
                Text {
                    anchors.centerIn: parent
                    text: paste.text.trim().length ? "Install" : "Paste link"
                    color: theme.ink
                    font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                }
                KeyboardAction {
                    id: clipAction
                    anchors.fill: parent
                    accessibleName: "Paste link from clipboard and install"
                    focusRadius: 22
                    onTriggered: {
                        if (paste.text.trim().length) sheet.finish(paste.text.trim())
                        else sheet.pasteAndInstall()
                    }
                }
            }

            Rectangle {
                visible: sheet.canSkip
                width: parent.width; height: 44; radius: 22
                color: skipAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.16)
                Text {
                    anchors.centerIn: parent
                    text: "Install without setup"
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 14
                }
                KeyboardAction {
                    id: skipAction
                    anchors.fill: parent
                    accessibleName: "Install without setup"
                    focusRadius: 22
                    onTriggered: sheet.finish(sheet.addon.manifestUrl)
                }
            }

            Text {
                visible: sheet.status.length > 0
                width: parent.width
                wrapMode: Text.WordWrap
                text: sheet.status
                color: theme.gold
                font.family: theme.ui; font.pixelSize: 13
            }
        }
    }

    onAddonChanged: { sheet.status = ""; paste.text = ""; sheet.pendingUrl = "" }
}
