// ExtensionsSetupSheet — an add-on's own setup page, inside Colosseum (never the outside browser).
// The page's "Install" button hands back a stremio:// (or …/manifest.json) link; the sheet catches
// that navigation and installs it. A paste box covers pages that only show the link to copy.
import QtQuick
import QtQuick.Controls
import QtWebEngine

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
                                    && !Extensions.isInstalled(sheet.addon.manifestUrl)
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

    // Harbor's capture set (installer-viewport.tsx + browser.rs), ported. QtWebEngine never
    // reports a stremio:// navigation to onNavigationRequested (unknown schemes go straight to
    // the OS), so a setup page's "Install" button died silently — AIOStreams, 2026-09-29. The
    // injected script rewrites every hand-over (stremio:// link click, window.open, location
    // change, postMessage, "Copy link") into an https://…/manifest.json top-level navigation,
    // which onNavigationRequested catches like any other install link.
    readonly property string captureScript: "(function(){" +
        "if (window.__colosseumAddonHook) return; window.__colosseumAddonHook = true;" +
        "function isAddon(u){ u = String(u || '').trim(); return u.indexOf('stremio://') === 0 || /^https?:\\/\\/[^\\s]+\\/manifest\\.json(\\?[^\\s]*)?$/i.test(u); }" +
        "function hand(u){ u = String(u).trim(); if (u.indexOf('stremio://') === 0) u = 'https://' + u.slice(10);" +
        "  try { window.top.location.href = u; } catch (e) { window.location.href = u; } }" +
        "document.addEventListener('click', function(e){ var a = e.target && e.target.closest ? e.target.closest('a[href]') : null;" +
        "  if (a && isAddon(a.getAttribute('href'))) { e.preventDefault(); e.stopPropagation(); hand(a.getAttribute('href')); } }, true);" +
        "var o = window.open; window.open = function(u){ if (isAddon(u)) { hand(u); return null; } return o.apply(window, arguments); };" +
        "if (window.navigation) window.navigation.addEventListener('navigate', function(e){" +
        "  var u = e.destination && e.destination.url; if (u && u.indexOf('stremio://') === 0 && e.cancelable) { e.preventDefault(); hand(u); } });" +
        "window.addEventListener('message', function(e){ var d = e.data;" +
        "  var c = typeof d === 'string' ? d : (d && (d.url || d.manifestUrl));" +
        "  if (isAddon(c)) hand(c); });" +
        "if (navigator.clipboard && navigator.clipboard.writeText) {" +
        "  var w = navigator.clipboard.writeText.bind(navigator.clipboard);" +
        "  navigator.clipboard.writeText = function(t){ if (isAddon(t)) hand(t); return w(t); }; }" +
        "document.addEventListener('copy', function(){ var s = String(window.getSelection() || '');" +
        "  if (isAddon(s)) hand(s); }, true);" +
        "})();"

    // Some setup pages refuse to load inside an app. After 7.5 s without a load (or on a
    // failed one) the sheet says so and offers a reload plus the paste box. (Harbor opens the
    // outside browser here; the Store never does — tests/extensions_store_contract_test.mjs.)
    property bool pageLoaded: false
    property bool pageBlocked: false
    Timer {
        interval: 7500
        running: sheet.open && !sheet.pageLoaded && !sheet.pageBlocked
        onTriggered: sheet.pageBlocked = true
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
            onLoadingChanged: function(info) {
                if (info.status === WebEngineLoadingInfo.LoadSucceededStatus) sheet.pageLoaded = true
                else if (info.status === WebEngineLoadingInfo.LoadFailedStatus && !sheet.pageLoaded)
                    sheet.pageBlocked = true
            }
            userScripts.collection: [{
                name: "colosseumAddonCapture",
                sourceCode: sheet.captureScript,
                injectionPoint: WebEngineScript.DocumentCreation,
                worldId: WebEngineScript.MainWorld,
                runsOnSubFrames: true
            }]
        }

        Rectangle {
            visible: sheet.pageBlocked && !sheet.pageLoaded
            anchors.fill: web
            color: "#0e1016"
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 80, 520)
                spacing: 14
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "This setup page won't open inside Colosseum."
                    color: theme.ink
                    font.family: theme.display; font.pixelSize: 20
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "Try again, or set it up in your web browser, copy the add-on link it gives you, and paste it below."
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 14
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: openLabel.implicitWidth + 40; height: 38; radius: 19
                    color: "#f5f3ee"
                    Text {
                        id: openLabel
                        anchors.centerIn: parent
                        text: "Try again"
                        color: "#111217"
                        font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                    }
                    KeyboardAction {
                        anchors.fill: parent
                        accessibleName: "Reload the setup page"
                        focusRadius: 19
                        onTriggered: { sheet.pageBlocked = false; web.reload() }
                    }
                }
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
                    width: parent.width - 260 - pasteClip.width - 12 - (skip.visible ? skip.width + 12 : 0)
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
                Rectangle {                          // Harbor's clipboard button: paste + install in one
                    id: pasteClip
                    anchors.verticalCenter: parent.verticalCenter
                    width: clipLabel.implicitWidth + 32; height: 34; radius: 17
                    color: clipAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.16)
                    Text {
                        id: clipLabel
                        anchors.centerIn: parent
                        text: "Paste link"
                        color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 13
                    }
                    KeyboardAction {
                        id: clipAction
                        anchors.fill: parent
                        accessibleName: "Paste link from clipboard and install"
                        focusRadius: 17
                        onTriggered: {
                            paste.clear(); paste.paste()
                            var t = paste.text.trim()
                            if (sheet.isAddonLink(t)) sheet.finish(t)
                            else sheet.status = t.length ? "That isn't an add-on link." : "The clipboard is empty."
                        }
                    }
                }
                Rectangle {
                    id: skip
                    visible: sheet.canSkip
                    anchors.verticalCenter: parent.verticalCenter
                    width: skipLabel.implicitWidth + 32; height: 34; radius: 17
                    color: skipAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.16)
                    Text {
                        id: skipLabel
                        anchors.centerIn: parent
                        text: "Install without setup"
                        color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 13
                    }
                    KeyboardAction {
                        id: skipAction
                        anchors.fill: parent
                        accessibleName: "Install without setup"
                        focusRadius: 17
                        onTriggered: sheet.finish(sheet.addon.manifestUrl)
                    }
                }
            }
        }
    }

    onAddonChanged: {
        sheet.status = ""; paste.text = ""; sheet.pendingUrl = ""
        sheet.pageLoaded = false; sheet.pageBlocked = false
    }
}
