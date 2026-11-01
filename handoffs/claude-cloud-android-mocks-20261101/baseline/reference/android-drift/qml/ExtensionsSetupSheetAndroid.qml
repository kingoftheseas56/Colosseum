// ExtensionsSetupSheetAndroid — an add-on's setup on a phone. Android ships no QtWebEngine, so
// the setup page opens in the phone's browser (Hemanth's ruling, 2026-10-04); the link it gives
// comes back through the clipboard. Same face as ExtensionsSetupSheet, so the Store drives either.
import QtQuick
import QtQuick.Controls.Basic
import "ViewportAvoidance.js" as Viewport

SettingsScaffold {
    id: sheet
    objectName: "extensionsSetupSheet"

    property var addon: null                 // the Store card being set up
    readonly property bool open: sheet.addon !== null
    property string status: ""
    property string pendingUrl: ""           // the normalized link last handed to install
    readonly property var activePhoneSheet:layout.touch ? phoneSheet : null
    onOpenChanged: {
        if(layout.touch) { if(open)Qt.callLater(function(){if(sheet.open)phoneSheet.open()});else phoneSheet.close() }
    }
    Component.onCompleted:if(open && layout.touch)Qt.callLater(function(){if(sheet.open)phoneSheet.open()})
    PhoneModalSheet {
        id:phoneSheet
        title:sheet.addon ? String(sheet.addon.name || qsTr("Set up add-on")) : ""
        hostedItem:body
        inputMethod:sheet.inputMethod
        inputMethodScale:sheet.inputMethodScale
        usableRect:sheet.availableRect
        safeInsets:({left:0,top:0,right:0,bottom:0})
        onClosed:if(sheet.open)sheet.closeRequested()
    }
    readonly property rect availableRect: {
        const keyboard = sheet.logicalKeyboardRectangle
        const origin = sheet.mapFromItem(null, keyboard.x, keyboard.y)
        const box = Viewport.available(width, height, safeInsets,
                !!(inputMethod && inputMethod.visible),
                {x: origin.x, y: origin.y, width: keyboard.width, height: keyboard.height}, 12)
        return Qt.rect(box.x, box.y, box.width, box.height)
    }
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

    function revealFocusedControl() {
        const window = sheet.Window.window
        const focused = window ? window.activeFocusItem : null
        if(layout.touch) { phoneSheet.reveal(focused);return }
        if (!focused || scroller.height <= 0) return
        let ancestor = focused
        while (ancestor && ancestor !== scroller.contentItem) ancestor = ancestor.parent
        if (!ancestor) return
        const position = focused.mapToItem(scroller.contentItem, 0, 0)
        let target = scroller.contentY
        if (position.y < target) target = position.y
        else if (position.y + focused.height > target + scroller.height)
            target = position.y + focused.height - scroller.height
        scroller.contentY = Math.max(0, Math.min(target, Math.max(0, scroller.contentHeight - scroller.height)))
    }

    function handleBack() {
        if (sheet.inputMethod && sheet.inputMethod.visible) {
            sheet.inputMethod.hide()
            return true
        }
        if (!sheet.open) return false
        sheet.closeRequested()
        return true
    }
    onAvailableRectChanged: Qt.callLater(sheet.revealFocusedControl)
    Connections {
        target: sheet.Window.window
        function onActiveFocusItemChanged() { Qt.callLater(sheet.revealFocusedControl) }
    }

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

    Rectangle { visible:!sheet.layout.touch; anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.62) }
    MouseArea { enabled:!sheet.layout.touch; anchors.fill: parent; onClicked: sheet.closeRequested() }

    Rectangle {
        id: panel
        visible:!sheet.layout.touch
        objectName: "extensionsSetupPanel"
        x: sheet.availableRect.x + (sheet.availableRect.width - width) / 2
        y: sheet.availableRect.y + (sheet.availableRect.height - height) / 2
        width: Math.min(sheet.availableRect.width, 520)
        height: Math.min(sheet.availableRect.height, body.implicitHeight + 102)
        radius: 24
        color: "#0e1016"
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12)
        MouseArea { anchors.fill: parent }      // taps on the panel never close it

        Item {
            id: header
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 18
            height: 48
                Text {
                    anchors.left: parent.left; anchors.right: closeBox.left; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: sheet.addon ? "Set up " + (sheet.addon.title || sheet.addon.name) : ""
                    color: theme.ink
                    font.family: theme.display; font.pixelSize: theme.fontSize(22)
                }
                Rectangle {
                    id: closeBox
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 48; height: 48; radius: 24
                    color: closeAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                    // "×", not the desktop sheet's "✕": Android's system font has no glyph for that one.
                    Text { anchors.centerIn: parent; text: "×"; color: theme.inkDim; font.pixelSize: 22 }
                    KeyboardAction {
                        id: closeAction
                        anchors.fill: parent
                        accessibleName: "Close setup"
                        focusRadius: 24
                        onTriggered: sheet.closeRequested()
                    }
                }
        }

        Flickable {
            id: scroller
            objectName: "extensionsSetupScroll"
            x: 18; y: header.y + header.height + 12
            width: Math.max(0, panel.width - 36)
            height: Math.max(0, panel.height - y - 18)
            contentWidth: width
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            onHeightChanged: Qt.callLater(sheet.revealFocusedControl)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
            id: body
            width: scroller.width
            spacing: 12

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Setup opens in your browser. Finish it there, copy the add-on link it gives you, then come back and paste it."
                color: theme.inkDim
                font.family: theme.ui; font.pixelSize: theme.fontSize(14)
            }

            Rectangle {
                width: parent.width; height: 48; radius: 24
                color: "#f5f3ee"
                Text {
                    anchors.centerIn: parent
                    text: "Open setup page"
                    color: "#111217"
                    font.family: theme.ui; font.pixelSize: theme.fontSize(14); font.weight: Font.DemiBold
                }
                KeyboardAction {
                    anchors.fill: parent
                    accessibleName: "Open the setup page in the browser"
                    focusRadius: 22
                    onTriggered: Qt.openUrlExternally(sheet.setupUrl)
                }
            }

            TouchTextField {
                id: paste
                objectName: "extensionsSetupLink"
                width: parent.width; height: 48
                leftPadding: 16; rightPadding: 16
                placeholderText: "Paste the add-on link the page gave you"
                color: theme.ink
                placeholderTextColor: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: theme.fontSize(13)
                background: Rectangle { radius: 16; color: Qt.rgba(1, 1, 1, 0.06); border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12) }
                onAccepted: if (text.trim().length) sheet.finish(text.trim())
            }

            Rectangle {
                objectName: "extensionsSetupInstall"
                width: parent.width; height: 48; radius: 24
                color: clipAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.16)
                Text {
                    anchors.centerIn: parent
                    text: paste.text.trim().length ? "Install" : "Paste link"
                    color: theme.ink
                    font.family: theme.ui; font.pixelSize: theme.fontSize(14); font.weight: Font.DemiBold
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
                width: parent.width; height: 48; radius: 24
                color: skipAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.16)
                Text {
                    anchors.centerIn: parent
                    text: "Install without setup"
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: theme.fontSize(14)
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
                font.family: theme.ui; font.pixelSize: theme.fontSize(13)
            }
            }
        }
    }

    onAddonChanged: {
        sheet.status = ""; paste.text = ""; sheet.pendingUrl = ""
        scroller.contentY = 0
        if (sheet.open) Qt.callLater(function() {
            if (!sheet.open) return
            if (sheet.inputMethod) sheet.inputMethod.hide()
            closeAction.forceActiveFocus(Qt.OtherFocusReason)
        })
    }
}
