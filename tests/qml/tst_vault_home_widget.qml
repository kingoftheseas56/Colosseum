import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// The Home Vault portal is permanent shell chrome: it does not depend on the
// local-media service and should still invite a first-run user into the Vault.
TestCase {
    name: "VaultHomeWidget"
    when: windowShown

    Window {
        id: testWindow
        width: 1000; height: 620; visible: true
        Item { id: backdrop; anchors.fill: parent }
    }

    Component { id: widgetComponent; Colosseum.VaultHomeWidget {} }
    property var widget: null

    SignalSpy { id: clickedSpy; signalName: "clicked" }

    function init() {
        mouseMove(testWindow, testWindow.width - 1, testWindow.height - 1)
        wait(10)
        verify(typeof VaultLibrary === "undefined")
        widget = widgetComponent.createObject(testWindow, {
            "backdrop": backdrop,
            "track": 0,
            "width": 900
        })
        verify(widget !== null)
        clickedSpy.target = widget
        wait(30)
    }

    function cleanup() {
        clickedSpy.clear()
        clickedSpy.target = null
        if (widget) widget.destroy()
        widget = null
    }

    function test_default_identity_and_data_independent_construction() {
        compare(widget.heading, "Vault")
    }

    function test_hover_state_responds_to_pointer() {
        compare(widget.hovered, false)
        var hit = findChild(widget, "vaultHomeWidgetHitArea")
        verify(hit !== null)
        compare(hit.hoverEnabled, true)
        mouseMove(widget, widget.width / 2, widget.height / 2)
        tryVerify(function() { return widget.hovered })
    }

    function test_click_emits_clicked() {
        mouseClick(widget, widget.width / 2, widget.height / 2)
        compare(clickedSpy.count, 1)
    }

    function findChild(root, objectName) {
        if (!root) return null
        if (root.objectName === objectName) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], objectName)
            if (found) return found
        }
        return null
    }
}
