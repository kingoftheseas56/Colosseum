import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

// PF-08: the production DownloadsPage explains a paused/failed background host using the
// native BackgroundDownloadBridge.backgroundHostError projection (double here), clears it on
// recovery, and shows nothing on desktop. Native epochs/gating are not exercised here.
TestCase {
    id: testCase
    name: "AndroidBackgroundDownloadMessage"
    when: windowShown

    Window { id: testWindow; width: 1185; height: 533; visible: true }
    QtObject {
        id: bridge
        property string backgroundHostError: ""
        property bool backgroundHostReady: true
    }
    Component {
        id: pageComponent
        App.DownloadsPage { anchors.fill: parent; downloadsApi: null; audiobooksApi: null; platformDownloads: bridge }
    }
    function find(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; ++i) { const f = find(kids[i], name); if (f) return f }
        return null
    }
    function test_error_shown_then_cleared_on_recovery() {
        bridge.backgroundHostError = ""
        const page = createTemporaryObject(pageComponent, testWindow.contentItem, { backgroundHostApplicable: true })
        verify(page !== null)
        const message = find(page, "backgroundDownloadMessage")
        verify(message !== null)
        verify(!message.visible)
        bridge.backgroundHostError = "Downloads paused: Android has not started the background download service yet."
        tryCompare(message, "visible", true)
        compare(message.text, bridge.backgroundHostError)
        bridge.backgroundHostError = ""
        tryCompare(message, "visible", false)
    }
    function test_desktop_never_shows_host_error() {
        bridge.backgroundHostError = "service error"
        const page = createTemporaryObject(pageComponent, testWindow.contentItem, { backgroundHostApplicable: false })
        compare(page.backgroundDownloadMessage, "")
        verify(!find(page, "backgroundDownloadMessage").visible)
        bridge.backgroundHostError = ""
    }
}
