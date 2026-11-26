import QtQuick
import QtTest

// Exercise Main's focus recovery with a fullscreen player and a disappearing
// HUD control. The native player is represented by a key-event recorder.
TestCase {
    id: testCase
    name: "PlayerHudKeyboard"
    when: windowShown
    property string shellRecovery: ""
    property var host: null

    function initTestCase() {
        const request = new XMLHttpRequest()
        request.open("GET", Qt.resolvedUrl("../../qml/Main.qml"), false)
        request.send()
        const source = request.responseText
        const start = source.indexOf("readonly property bool keyboardFocusDead:")
        const end = source.indexOf("// The secret developer door:", start)
        verify(start >= 0 && end > start, "Read Main.qml with QML_XHR_ALLOW_FILE_READ=1")
        shellRecovery = source.slice(start, end)
    }

    function init() {
        host = Qt.createQmlObject([
            "import QtQuick",
            "Window { id: win; width: 320; height: 240",
            "property bool playerOpen: true",
            "property bool controlsShown: true",
            "property alias playerItem: player",
            "property alias hudButton: hudButton",
            "property alias shellFocus: keyboardIgnition",
            "property alias otherControl: otherControl",
            "property int lastPlaybackKey: 0",
            'Item { id: keyboardIgnition; objectName: "shellKeyboard"; focus: true }',
            "Item { id: otherControl }",
            "Item { id: playerLayer; visible: win.playerOpen; property Item item: player",
            'Item { id: player; objectName: "player"; Keys.onPressed: event => { win.lastPlaybackKey = event.key; event.accepted = true }',
            'Item { visible: win.controlsShown; Item { id: hudButton; objectName: "hudButton"; activeFocusOnTab: true } }',
            "} }",
            shellRecovery,
            "}"
        ].join("\n"), testCase, "PlayerHudKeyboardHost")
        host.showFullScreen()
        host.requestActivate()
        tryCompare(host, "active", true)
        wait(0)
        host.playerItem.forceActiveFocus(Qt.OtherFocusReason)
        tryCompare(host, "activeFocusItem", host.playerItem)
        host.hudButton.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(host, "activeFocusItem", host.hudButton)
    }

    function cleanup() {
        if (host) {
            host.close()
            host.destroy()
            host = null
        }
    }

    function test_hiddenHudReturnsFocusToPlayer() {
        host.controlsShown = false
        tryCompare(host, "activeFocusItem", host.playerItem)
        keyClick(Qt.Key_Space)
        compare(host.lastPlaybackKey, Qt.Key_Space)
        keyClick(Qt.Key_Right)
        compare(host.lastPlaybackKey, Qt.Key_Right)
    }

    function test_closingPlayerReturnsFocusToShell() {
        host.playerOpen = false
        tryCompare(host, "activeFocusItem", host.shellFocus)
    }

    function test_deferredRecoveryPreservesNewFocus() {
        host.controlsShown = false
        host.otherControl.forceActiveFocus(Qt.PopupFocusReason)
        wait(0)
        compare(host.activeFocusItem, host.otherControl)
    }
}
